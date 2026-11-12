// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_voice.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/online/steam/steam_online.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace crimson::integration {
using namespace godot;

namespace {

// Voice is isolated from reliable admission, world state and gameplay commands.
constexpr int32_t authority_id = 1;
constexpr int32_t voice_channel = 3;
constexpr const char *submit_method = "_voice_submit";
constexpr const char *receive_method = "_voice_receive";
// Gameplay uses small datagrams rather than forwarding oversized capture batches.
constexpr int64_t maximum_frame_bytes = 1200;
constexpr size_t maximum_peers = 64;
constexpr size_t maximum_queue = 16;
constexpr int32_t decode_per_frame = 4;
// Bound one sender's bandwidth and the authority's aggregate amplification.
constexpr uint64_t rate_window_ms = 1000;
constexpr uint32_t maximum_sender_packets = 60;
constexpr uint32_t maximum_sender_bytes = 32 * 1024;
constexpr uint32_t maximum_relay_bytes = 256 * 1024;
constexpr uint32_t maximum_recipient_checks = 4096;
constexpr uint64_t talking_timeout_ms = 350;
constexpr uint64_t maximum_playback_age_ms = 250;

// Resolve the scoped Steam codec without using a default process account as a substitute.
Ref<steam::SteamVoice> codec_for(GameRuntime *owner) {
    const auto steam = SteamServices::for_game(owner);
    return steam.is_valid() ? steam->voice() : Ref<steam::SteamVoice>();
}

// Check that an encoded frame can be forwarded through the dedicated gameplay datagram lane.
bool valid_voice_bytes(const PackedByteArray &bytes) {
    return !bytes.is_empty() && bytes.size() <= maximum_frame_bytes;
}

// Verify that the actual admission flow certified this connection as Steam.
bool verified_steam_peer(session::GameSession &session, int32_t peer) {
    const auto identity = session.get_peer_identity(peer);
    return identity.is_valid() && identity->get_provider() == StringName("steam");
}

} // namespace

void GameVoice::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start_talking"), &GameVoice::start_talking);
    ClassDB::bind_method(D_METHOD("stop_talking"), &GameVoice::stop_talking);
    ClassDB::bind_method(D_METHOD("set_muted", "peer_id", "muted"), &GameVoice::set_muted, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("set_channel", "channel"), &GameVoice::set_channel);
    ClassDB::bind_method(D_METHOD("set_recipient_policy", "policy"), &GameVoice::set_recipient_policy);
    ClassDB::bind_method(D_METHOD(submit_method, "epoch", "sequence", "channel", "bytes"), &GameVoice::rpc_submit);
    ClassDB::bind_method(D_METHOD(receive_method, "epoch", "sender", "sequence", "bytes"), &GameVoice::rpc_receive);
    ADD_SIGNAL(MethodInfo("talking_changed", PropertyInfo(Variant::INT, "peer_id"),
            PropertyInfo(Variant::BOOL, "talking")));
    ADD_SIGNAL(MethodInfo("frame_received", PropertyInfo(Variant::INT, "peer_id"),
            PropertyInfo(Variant::OBJECT, "pcm", PROPERTY_HINT_RESOURCE_TYPE, "SteamVoicePcm")));
    BIND_ENUM_CONSTANT(GROUP);
    BIND_ENUM_CONSTANT(TEAM);
    BIND_ENUM_CONSTANT(PROXIMITY);
}

void GameVoice::_enter_tree() {
    leaving_tree = false;
    set_multiplayer_authority(authority_id, false);
    Dictionary config;
    config["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
    config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED;
    config["call_local"] = false;
    config["channel"] = voice_channel;
    rpc_config(submit_method, config);
    config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    rpc_config(receive_method, config);
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process(true);
}

GameRuntime *GameVoice::game() const {
    auto *owner = leaving_tree || !is_inside_tree() ? nullptr : Object::cast_to<GameRuntime>(get_parent());
    return owner && !owner->cleaning && owner->session_node ? owner : nullptr;
}

void GameVoice::_exit_tree() {
    leaving_tree = true;
    reset(false);
}

void GameVoice::reset(bool notify) {
    const ObjectID self(get_instance_id());
    const int32_t local = std::exchange(local_talker, 0);
    const auto active_capture = std::exchange(capture, {});
    const auto activity = std::exchange(last_audio, {});
    voice_epoch = 0;
    local_sequence = 0;
    senders.clear();
    received.clear();
    muted_peers.clear();
    incoming.clear();
    relay_window = 0;
    relayed_bytes = 0;
    recipient_checks = 0;
    if (active_capture.is_valid()) {
        active_capture->abandon();
    }
    if (notify) {
        if (active_capture.is_valid() && local > 0) {
            emit_signal("talking_changed", local, false);
            if (ObjectDB::get_instance(self) != this) {
                return;
            }
        }
        for (const auto &[peer, timestamp] : activity) {
            emit_signal("talking_changed", peer, false);
            if (ObjectDB::get_instance(self) != this) {
                return;
            }
        }
    }
}

bool GameVoice::authority_peer(const GameRuntime &owner, int32_t peer) const {
    if (peer <= 0) {
        return false;
    }
    if (peer == owner.session_node->get_local_peer_id()) {
        return owner.immediate_active ? owner.get_local_player() != nullptr :
                owner.bindings.is_valid() && !owner.bindings->get_player(peer).is_empty();
    }
    return verified_steam_peer(*owner.session_node, peer) &&
            (owner.immediate_active ? owner.immediate && owner.immediate->is_peer_world_ready(peer) :
                    owner.replication_node && owner.replication_node->is_peer_ready(peer));
}

Error GameVoice::start_talking() {
    auto *owner = game();
    if (!owner || owner->get_phase() != GameRuntime::RUNNING || owner->get_world_epoch() <= 0) {
        return ERR_UNAVAILABLE;
    }
    const auto state = owner->session_node->get_state();
    if (state != session::GameSession::HOSTING && state != session::GameSession::CLIENT) {
        return ERR_UNAVAILABLE;
    }
    const int32_t local = owner->session_node->get_local_peer_id();
    if ((owner->is_authority() && !authority_peer(*owner, local)) ||
            (!owner->is_authority() && !verified_steam_peer(*owner->session_node, authority_id))) {
        return ERR_UNAUTHORIZED;
    }
    if (capture.is_valid() && voice_epoch == owner->get_world_epoch()) {
        return OK;
    }
    if (voice_epoch != owner->get_world_epoch()) {
        reset(false);
        voice_epoch = owner->get_world_epoch();
    }
    const auto codec = codec_for(owner);
    const auto microphone = codec.is_valid() ? codec->capture_for_game() : Ref<steam::SteamVoiceCapture>();
    const Error status = microphone.is_valid() ? microphone->start() : ERR_UNAVAILABLE;
    if (status == OK) {
        capture = microphone;
        local_talker = local;
        emit_signal("talking_changed", local, true);
    }
    return status;
}

Error GameVoice::stop_talking() {
    const auto active_capture = std::exchange(capture, {});
    const int32_t local = std::exchange(local_talker, 0);
    if (active_capture.is_null()) {
        return ERR_UNAVAILABLE;
    }
    const Error result = active_capture->abandon();
    if (local > 0 && !leaving_tree) {
        emit_signal("talking_changed", local, false);
    }
    return result;
}

Error GameVoice::set_muted(int64_t peer_id, bool muted) {
    auto *owner = game();
    if (!owner || peer_id <= 0 || peer_id > (std::numeric_limits<int32_t>::max)() ||
            peer_id == owner->session_node->get_local_peer_id()) {
        return ERR_INVALID_PARAMETER;
    }
    const auto peer = static_cast<int32_t>(peer_id);
    if (!muted) {
        muted_peers.erase(peer);
        return OK;
    }
    if (muted_peers.size() >= maximum_peers && !muted_peers.count(peer)) {
        return ERR_BUSY;
    }
    muted_peers.insert(peer);
    incoming.erase(std::remove_if(incoming.begin(), incoming.end(),
            [peer](const Incoming &frame) { return frame.sender == peer; }), incoming.end());
    if (last_audio.erase(peer)) {
        emit_signal("talking_changed", peer, false);
    }
    return OK;
}

Error GameVoice::set_channel(Channel channel) {
    if (channel < GROUP || channel > PROXIMITY) {
        return ERR_INVALID_PARAMETER;
    }
    outgoing_channel = channel;
    return OK;
}

Error GameVoice::set_recipient_policy(const Callable &policy) {
    if (!policy.is_null() && !policy.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    recipient_policy = policy;
    return OK;
}

void GameVoice::rpc_submit(int64_t epoch, int64_t sequence, int64_t channel, const PackedByteArray &bytes) {
    auto *owner = game();
    if (!owner || !owner->is_authority() || owner->get_phase() != GameRuntime::RUNNING ||
            epoch <= 0 || epoch != owner->get_world_epoch() || channel < GROUP || channel > PROXIMITY) {
        return;
    }
    const auto network = get_multiplayer();
    const int32_t sender = network.is_valid() ? network->get_remote_sender_id() : 0;
    if (sender > authority_id) {
        relay(*owner, sender, sequence, static_cast<Channel>(channel), bytes);
    }
}

void GameVoice::rpc_receive(int64_t epoch, int64_t sender, int64_t sequence, const PackedByteArray &bytes) {
    auto *owner = game();
    const auto network = get_multiplayer();
    if (!owner || owner->is_authority() || owner->get_phase() != GameRuntime::RUNNING ||
            epoch <= 0 || epoch != owner->get_world_epoch() || sender <= 0 ||
            sender > (std::numeric_limits<int32_t>::max)() || sender == owner->session_node->get_local_peer_id() ||
            network.is_null() || network->get_remote_sender_id() != authority_id ||
            (owner->immediate_active && !owner->get_player(sender)) ||
            !verified_steam_peer(*owner->session_node, authority_id)) {
        return;
    }
    enqueue(static_cast<int32_t>(sender), sequence, bytes);
}

void GameVoice::relay(GameRuntime &owner, int32_t sender, int64_t sequence, Channel channel,
        const PackedByteArray &bytes) {
    if (voice_epoch != owner.get_world_epoch() || sequence <= 0 || !valid_voice_bytes(bytes) ||
            !authority_peer(owner, sender)) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (now - relay_window >= rate_window_ms) {
        relay_window = now;
        relayed_bytes = 0;
        recipient_checks = 0;
    }
    if (recipient_checks >= maximum_recipient_checks || relayed_bytes >= maximum_relay_bytes ||
            (channel != GROUP && !recipient_policy.is_valid())) {
        return;
    }
    auto found = senders.find(sender);
    if (found == senders.end()) {
        if (senders.size() >= maximum_peers) {
            return;
        }
        found = senders.emplace(sender, SenderWindow{}).first;
    }
    auto &window = found->second;
    if (now - window.started >= rate_window_ms) {
        window.started = now;
        window.packets = 0;
        window.bytes = 0;
    }
    const auto payload = static_cast<uint32_t>(bytes.size());
    if (sequence <= window.latest || window.packets >= maximum_sender_packets ||
            window.bytes + payload > maximum_sender_bytes) {
        return;
    }
    window.latest = sequence;
    ++window.packets;
    window.bytes += payload;

    const int32_t local = owner.session_node->get_local_peer_id();
    auto recipients = owner.session_node->get_connected_peer_ids();
    recipients.append(local);
    const ObjectID self(get_instance_id());
    const int64_t epoch = voice_epoch;
    const Callable policy = recipient_policy;
    for (int64_t index = 0; index < recipients.size(); ++index) {
        const int32_t recipient = recipients[index];
        if (recipient == sender || !authority_peer(owner, recipient)) {
            continue;
        }
        if (recipient_checks >= maximum_recipient_checks || relayed_bytes + payload > maximum_relay_bytes) {
            break;
        }
        ++recipient_checks;
        if (policy.is_valid()) {
            const Variant allowed = policy.call(sender, recipient, channel);
            if (ObjectDB::get_instance(self) != this || game() != &owner ||
                    owner.get_phase() != GameRuntime::RUNNING || owner.get_world_epoch() != epoch ||
                    !authority_peer(owner, sender)) {
                return;
            }
            if (allowed.get_type() != Variant::BOOL || !bool(allowed) || !authority_peer(owner, recipient)) {
                continue;
            }
        }
        if (recipient == local) {
            enqueue(sender, sequence, bytes);
        } else if (rpc_id(recipient, receive_method, epoch, sender, sequence, bytes) == OK) {
            relayed_bytes += payload;
        }
    }
}

void GameVoice::enqueue(int32_t sender, int64_t sequence, const PackedByteArray &bytes) {
    if (voice_epoch <= 0 || sequence <= 0 || !valid_voice_bytes(bytes) || muted_peers.count(sender)) {
        return;
    }
    auto found = received.find(sender);
    if (found == received.end()) {
        if (received.size() >= maximum_peers) {
            return;
        }
        found = received.emplace(sender, 0).first;
    }
    if (sequence <= found->second) {
        return;
    }
    found->second = sequence;
    if (incoming.size() >= maximum_queue) {
        incoming.pop_front();
    }
    incoming.push_back({ sender, bytes, Time::get_singleton()->get_ticks_msec() });
}

void GameVoice::send_local(GameRuntime &owner) {
    if (capture.is_null() || local_sequence == (std::numeric_limits<int64_t>::max)()) {
        return;
    }
    const auto frame = capture->read_frame();
    if (frame.is_null() || !frame->is_ok()) {
        return;
    }
    const auto bytes = frame->get_value()->get_bytes();
    if (!valid_voice_bytes(bytes)) {
        return;
    }
    const int64_t sequence = ++local_sequence;
    if (owner.is_authority()) {
        relay(owner, owner.session_node->get_local_peer_id(), sequence, outgoing_channel, bytes);
    } else {
        rpc_id(authority_id, submit_method, voice_epoch, sequence, outgoing_channel, bytes);
    }
}

void GameVoice::expire_peers(GameRuntime &owner, uint64_t now) {
    if (owner.is_authority()) {
        for (auto it = senders.begin(); it != senders.end();) {
            if (!authority_peer(owner, it->first)) {
                received.erase(it->first);
                muted_peers.erase(it->first);
                it = senders.erase(it);
            } else {
                ++it;
            }
        }
    } else if (owner.immediate_active) {
        for (auto it = received.begin(); it != received.end();) {
            if (!owner.get_player(it->first)) {
                muted_peers.erase(it->first);
                it = received.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::vector<int32_t> stopped;
    for (auto it = last_audio.begin(); it != last_audio.end();) {
        if (now - it->second >= talking_timeout_ms ||
                (owner.is_authority() ? !authority_peer(owner, it->first) :
                        owner.immediate_active && !owner.get_player(it->first))) {
            stopped.push_back(it->first);
            it = last_audio.erase(it);
        } else {
            ++it;
        }
    }
    const ObjectID self(get_instance_id());
    for (const int32_t peer : stopped) {
        emit_signal("talking_changed", peer, false);
        if (ObjectDB::get_instance(self) != this || game() != &owner) {
            break;
        }
    }
}

void GameVoice::play_frames(GameRuntime &owner, uint64_t now) {
    if (incoming.empty()) {
        return;
    }
    const auto codec = codec_for(&owner);
    if (codec.is_null()) {
        incoming.clear();
        return;
    }
    const ObjectID self(get_instance_id());
    const int64_t epoch = voice_epoch;
    for (int32_t index = 0; index < decode_per_frame && !incoming.empty(); ++index) {
        const auto frame = std::move(incoming.front());
        incoming.pop_front();
        if ((now >= frame.received_at && now - frame.received_at >= maximum_playback_age_ms) ||
                muted_peers.count(frame.sender) ||
                (owner.is_authority() ? !authority_peer(owner, frame.sender) :
                        owner.immediate_active && !owner.get_player(frame.sender))) {
            continue;
        }
        const auto decoded = codec->decode(frame.bytes);
        if (!decoded->is_ok()) {
            continue;
        }
        const bool speaking = last_audio.count(frame.sender) != 0;
        last_audio[frame.sender] = now;
        if (!speaking) {
            emit_signal("talking_changed", frame.sender, true);
            if (ObjectDB::get_instance(self) != this || game() != &owner ||
                    owner.get_phase() != GameRuntime::RUNNING || owner.get_world_epoch() != epoch) {
                return;
            }
        }
        if (!muted_peers.count(frame.sender)) {
            emit_signal("frame_received", frame.sender, decoded->get_value());
            if (ObjectDB::get_instance(self) != this || game() != &owner ||
                    owner.get_phase() != GameRuntime::RUNNING || owner.get_world_epoch() != epoch) {
                return;
            }
        }
    }
}

void GameVoice::_process(double) {
    auto *owner = game();
    if (!owner || owner->get_phase() != GameRuntime::RUNNING ||
            owner->session_node->get_state() == session::GameSession::OFFLINE) {
        if (voice_epoch) {
            reset(true);
        }
        return;
    }
    const ObjectID self(get_instance_id());
    const int64_t epoch = owner->get_world_epoch();
    if (voice_epoch != epoch) {
        reset(true);
        if (ObjectDB::get_instance(self) != this || game() != owner ||
                owner->get_phase() != GameRuntime::RUNNING || owner->get_world_epoch() != epoch) {
            return;
        }
        voice_epoch = epoch;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    expire_peers(*owner, now);
    if (ObjectDB::get_instance(self) != this || game() != owner ||
            owner->get_phase() != GameRuntime::RUNNING || owner->get_world_epoch() != epoch) {
        return;
    }
    send_local(*owner);
    if (ObjectDB::get_instance(self) == this && game() == owner &&
            owner->get_phase() == GameRuntime::RUNNING && owner->get_world_epoch() == epoch) {
        play_frames(*owner, now);
    }
}

} // namespace crimson::integration
