// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_messages.hpp"

#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <set>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// A bounded project-defined ASCII channel name.
constexpr int64_t maximum_channel_length = 32;
constexpr int64_t maximum_channel_count = 16;

// Use the same canonical grammar before serializing and after parsing a remote name.
bool valid_channel_bytes(const char *bytes, int64_t size) {
    if (size < 1 || size > maximum_channel_length) {
        return false;
    }
    for (int64_t index = 0; index < size; ++index) {
        const auto ch = bytes[index];
        const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!alnum && (index == 0 || (ch != '-' && ch != '_' && ch != '.'))) {
            return false;
        }
    }
    return true;
}

#ifdef CRIMSON_WITH_STEAM
// Reserved native ISteamNetworkingMessages channel for all Crimson routes.
constexpr int32_t native_channel = 0x43524d53;
// Compact little-endian envelope. The first bytes are the fixed magic and version.
constexpr uint32_t wire_magic = 0x534d5243;
constexpr uint8_t wire_version = 2;
constexpr uint8_t wire_data = 0;
constexpr uint8_t wire_hello = 1;
constexpr uint8_t wire_proof = 2;
constexpr int64_t header_size = 56;
constexpr int64_t maximum_namespace_length = 64;
constexpr int64_t maximum_payload = 4096;
// Reliable payloads wait briefly for the current peer binding before transmission.
constexpr size_t maximum_pending_packets = 8;
constexpr int64_t maximum_pending_bytes = 16 * 1024;
constexpr uint64_t pending_timeout_ms = 15000;
constexpr uint64_t challenge_timeout_ms = 5000;
constexpr uint64_t control_interval_ms = 150;
// Bound native message and authorization work per client and per process.
constexpr int32_t maximum_native_batch = 32;
constexpr size_t maximum_process_bindings = 128;
constexpr size_t maximum_scope_bindings = 64;
// Per-peer bandwidth limits apply in each direction before app delivery.
constexpr uint32_t maximum_packets_per_second = 60;
constexpr uint32_t maximum_bytes_per_second = 64 * 1024;

// One native receiver and exact remote-owner map per active Steam process generation.
struct ProcessMessages {
    // SDK pipe observed without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Unique scoped consumer for each Steam account on the reserved native channel.
    std::map<uint64_t, ObjectID> owners;
    // Native receive budget spent once per Godot process frame.
    uint64_t last_receive_frame = (std::numeric_limits<uint64_t>::max)();
};

// Accessed only on the Steam main thread.
ProcessMessages process_messages;

// The runtime generation changes only when Steam is actually reinitialized.
ProcessMessages &broker_for(const std::shared_ptr<SteamRuntime> &runtime) {
    if (process_messages.runtime.lock() != runtime) {
        process_messages = {};
        process_messages.runtime = runtime;
    }
    return process_messages;
}

// Resolve the exact SDK Messages interface.
ISteamNetworkingMessages *messages_api(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamNetworkingMessages_SteamAPI_v002)>(
                                     "SteamAPI_SteamNetworkingMessages_SteamAPI_v002") : nullptr;
    return get ? get() : nullptr;
}

// Compare a native packet's receipt time with the exact local authorization.
int64_t native_timestamp(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime->symbol<decltype(&SteamAPI_SteamNetworkingUtils_SteamAPI_v004)>(
            "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    const auto now = runtime->symbol<decltype(&SteamAPI_ISteamNetworkingUtils_GetLocalTimestamp)>(
            "SteamAPI_ISteamNetworkingUtils_GetLocalTimestamp");
    auto *utils = get ? get() : nullptr;
    return utils && now ? now(utils) : 0;
}

// Retain the exact owning facade through an SDK observation.
Ref<SteamMessages> message_service(ObjectID id) {
    return Ref<SteamMessages>(Object::cast_to<SteamMessages>(ObjectDB::get_instance(id)));
}

// Accept only Steam-certified individual identities on this app channel.
uint64_t account_from(const SteamNetworkingIdentity &identity) {
    const CSteamID account = identity.GetSteamID();
    return account.IsValid() && account.BIndividualAccount() ? account.ConvertToUint64() : 0;
}

// Map native queueing outcomes without exposing Steam connection internals.
Error message_error(EResult error) {
    switch (error) {
        case k_EResultOK: return OK;
        case k_EResultNoConnection: return ERR_CONNECTION_ERROR;
        case k_EResultLimitExceeded:
        case k_EResultBusy: return ERR_BUSY;
        case k_EResultInvalidParam: return ERR_INVALID_PARAMETER;
        case k_EResultAccessDenied: return ERR_UNAUTHORIZED;
        case k_EResultServiceUnavailable: return ERR_UNAVAILABLE;
        default: return FAILED;
    }
}

// Decode a bounded little-endian integer without assuming SDK payload alignment.
uint64_t read_uint(const uint8_t *bytes, size_t count) {
    uint64_t value = 0;
    for (size_t index = 0; index < count; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

// Copy one fixed native nonce after the packet's total length is verified.
SteamMessageNonce read_nonce(const uint8_t *bytes) {
    SteamMessageNonce nonce{};
    std::memcpy(nonce.data(), bytes, nonce.size());
    return nonce;
}

// Generate a binding or challenge nonce using Godot's platform crypto source.
SteamMessageNonce new_nonce() {
    Ref<Crypto> crypto;
    crypto.instantiate();
    const auto generated = crypto->generate_random_bytes(16);
    return generated.size() == 16 ? read_nonce(generated.ptr()) : SteamMessageNonce{};
}

// Queue a bounded app/control frame only after the scoped caller authorized this account.
Error send_packet(const std::shared_ptr<SteamRuntime> &active, uint64_t account, int64_t epoch,
        const StringName &name_space, const StringName &channel, uint8_t kind, uint64_t sequence,
        const SteamMessageNonce &source, const SteamMessageNonce &target,
        const uint8_t *payload, int64_t size, SteamDelivery::Mode mode) {
    ISteamNetworkingMessages *api = messages_api(active);
    const auto send = api ? active->symbol<decltype(&SteamAPI_ISteamNetworkingMessages_SendMessageToUser)>(
                                    "SteamAPI_ISteamNetworkingMessages_SendMessageToUser") : nullptr;
    if (!send) {
        return ERR_UNAVAILABLE;
    }
    const auto namespace_bytes = String(name_space).utf8();
    const auto channel_bytes = String(channel).utf8();
    if (namespace_bytes.length() > maximum_namespace_length || channel_bytes.length() > maximum_channel_length ||
            size < 0 || size > maximum_payload) {
        return ERR_INVALID_DATA;
    }
    PackedByteArray packet;
    const int64_t names_end = header_size + namespace_bytes.length() + channel_bytes.length();
    if (packet.resize(names_end + size) != OK) {
        return ERR_OUT_OF_MEMORY;
    }
    packet.encode_u32(0, wire_magic);
    packet.set(4, wire_version);
    packet.set(5, kind);
    packet.set(6, static_cast<uint8_t>(namespace_bytes.length()));
    packet.set(7, static_cast<uint8_t>(channel_bytes.length()));
    packet.encode_u64(8, static_cast<uint64_t>(epoch));
    packet.encode_u64(16, sequence);
    uint8_t *destination = packet.ptrw();
    std::memcpy(destination + 24, source.data(), source.size());
    std::memcpy(destination + 40, target.data(), target.size());
    if (namespace_bytes.length()) {
        std::memcpy(destination + header_size, namespace_bytes.get_data(),
                static_cast<size_t>(namespace_bytes.length()));
    }
    if (channel_bytes.length()) {
        std::memcpy(destination + header_size + namespace_bytes.length(), channel_bytes.get_data(),
                static_cast<size_t>(channel_bytes.length()));
    }
    if (size) {
        std::memcpy(destination + names_end, payload, static_cast<size_t>(size));
    }
    SteamNetworkingIdentity remote{};
    remote.SetSteamID64(account);
    int flags = mode == SteamDelivery::RELIABLE ? k_nSteamNetworkingSend_Reliable :
                                                 k_nSteamNetworkingSend_UnreliableNoDelay;
    if (kind == wire_hello) {
        // An explicit rebind may inherit a broken native session for this authorized account.
        flags |= k_nSteamNetworkingSend_AutoRestartBrokenSession;
    }
    return message_error(send(api, remote, destination, static_cast<uint32_t>(packet.size()), flags, native_channel));
}
#endif

} // namespace

bool valid_steam_message_channels(const TypedArray<StringName> &channels, bool allow_empty) {
    if (channels.size() > maximum_channel_count || (channels.is_empty() && !allow_empty)) {
        return false;
    }
    std::set<StringName> seen;
    for (int64_t index = 0; index < channels.size(); ++index) {
        const StringName name = channels[index];
        const CharString utf8 = String(name).utf8();
        if (!valid_channel_bytes(utf8.get_data(), utf8.length()) || !seen.insert(name).second) {
            return false;
        }
    }
    return true;
}

void SteamDelivery::_bind_methods() {
    BIND_ENUM_CONSTANT(RELIABLE);
    BIND_ENUM_CONSTANT(UNRELIABLE);
}

void SteamMessagePeer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("send", "channel", "bytes", "delivery"), &SteamMessagePeer::send,
            DEFVAL(SteamDelivery::RELIABLE));
    ClassDB::bind_method(D_METHOD("close"), &SteamMessagePeer::close);
    ADD_SIGNAL(MethodInfo("received", PropertyInfo(Variant::STRING_NAME, "channel"),
            PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes"), PropertyInfo(Variant::INT, "delivery")));
    ADD_SIGNAL(MethodInfo("closed", PropertyInfo(Variant::INT, "reason")));
}

Ref<SteamMessages> SteamMessagePeer::service() const {
#ifdef CRIMSON_WITH_STEAM
    return SteamRuntime::on_main_thread() ? message_service(service_id) : Ref<SteamMessages>();
#else
    return {};
#endif
}

Error SteamMessagePeer::send(const StringName &channel, const PackedByteArray &bytes, SteamDelivery::Mode delivery) {
    const auto owner = service();
    return owner.is_valid() ? owner->send_to(this, channel, bytes, delivery) : ERR_UNAVAILABLE;
}

Error SteamMessagePeer::close() {
    const auto owner = service();
    return owner.is_valid() ? owner->close_peer(this, OK) : ERR_UNAVAILABLE;
}

void SteamMessagePeerResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamMessagePeerResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamMessagePeer",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamMessagePeer> SteamMessagePeerResult::get_value() const { return value; }

Ref<SteamMessagePeerResult> SteamMessagePeerResult::create(Error error, const Ref<SteamMessagePeer> &peer) {
    Ref<SteamMessagePeerResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = peer;
    return result;
}

void SteamMessages::_bind_methods() {
    ClassDB::bind_method(D_METHOD("for_peer", "peer_id"), &SteamMessages::for_peer);
    ClassDB::bind_method(D_METHOD("authorize", "steam_id", "channels"), &SteamMessages::authorize);
}

void SteamMessages::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
    runtime = active;
    client_scope = scope;
}

void SteamMessages::detach() {
    client_scope.reset();
    game_source = {};
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(std::exchange(request_subscription, 0));
    }
    while (!peers.empty()) {
        const auto peer = peers.begin()->second;
        close_peer(peer.ptr(), ERR_SKIP);
    }
#endif
    runtime.reset();
}

std::shared_ptr<SteamRuntime> SteamMessages::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto scope = client_scope.lock();
        if (scope && scope->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

void SteamMessages::set_game_source(std::function<SteamMessageGamePeer(int64_t)> source,
        const StringName &name_space, const TypedArray<StringName> &channels) {
    game_source = std::move(source);
    game_namespace = name_space;
    game_channels.assign(channels);
    game_channels.make_read_only();
}

Ref<SteamMessagePeerResult> SteamMessages::for_peer(int64_t peer_id) {
    if (!active_runtime()) {
        return SteamMessagePeerResult::create(ERR_UNAVAILABLE);
    }
    if (!game_source || game_channels.is_empty()) {
        return SteamMessagePeerResult::create(ERR_UNCONFIGURED);
    }
    const auto selected = game_source(peer_id);
    uint64_t account = 0;
    if (peer_id <= 0 || selected.epoch <= 0 || selected.admission == ObjectID() ||
            !parse_steam_lobby_account(selected.steam_id, account)) {
        return SteamMessagePeerResult::create(ERR_UNAUTHORIZED);
    }
    return bind_peer(account, peer_id, selected.epoch, selected.admission, game_channels);
}

Ref<SteamMessagePeerResult> SteamMessages::authorize(const String &steam_id, const TypedArray<StringName> &channels) {
    if (!active_runtime()) {
        return SteamMessagePeerResult::create(ERR_UNAVAILABLE);
    }
    if (game_source) {
        return SteamMessagePeerResult::create(ERR_UNAUTHORIZED);
    }
    uint64_t account = 0;
    if (!parse_steam_lobby_account(steam_id, account) || !valid_steam_message_channels(channels)) {
        return SteamMessagePeerResult::create(ERR_INVALID_PARAMETER);
    }
    return bind_peer(account, 0, 0, ObjectID(), channels);
}

bool SteamMessages::owns_peer(const SteamMessagePeer *peer) const {
    if (!peer || !active_runtime()) {
        return false;
    }
    const auto found = peers.find(peer->account);
    if (found == peers.end() || found->second.ptr() != peer) {
        return false;
    }
    if (peer->game_peer == 0) {
        return !game_source;
    }
    const auto selected = game_source ? game_source(peer->game_peer) : SteamMessageGamePeer{};
    uint64_t account = 0;
    return selected.epoch == peer->epoch && selected.admission == peer->admission &&
            parse_steam_lobby_account(selected.steam_id, account) &&
            account == peer->account;
}

Ref<SteamMessagePeerResult> SteamMessages::bind_peer(uint64_t account, int64_t game_peer, int64_t epoch,
        ObjectID admission, const TypedArray<StringName> &channels) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!messages_api(active)) {
        return SteamMessagePeerResult::create(ERR_UNAVAILABLE);
    }
    if (account == active->get_user_id()) {
        return SteamMessagePeerResult::create(ERR_INVALID_PARAMETER);
    }
    const auto existing = peers.find(account);
    if (existing != peers.end()) {
        const auto peer = existing->second;
        if (!owns_peer(peer.ptr()) || peer->game_peer != game_peer || peer->channels.size() != static_cast<size_t>(channels.size())) {
            return SteamMessagePeerResult::create(ERR_BUSY);
        }
        for (int64_t index = 0; index < channels.size(); ++index) {
            const StringName channel = channels[index];
            if (!peer->channels.count(channel)) {
                return SteamMessagePeerResult::create(ERR_BUSY);
            }
        }
        return SteamMessagePeerResult::create(OK, peer);
    }
    auto &broker = broker_for(active);
    if (peers.size() >= maximum_scope_bindings || broker.owners.size() >= maximum_process_bindings ||
            broker.owners.count(account)) {
        return SteamMessagePeerResult::create(ERR_BUSY);
    }
    const int64_t created_at = native_timestamp(active);
    const auto nonce = new_nonce();
    if (created_at <= 0 || nonce == SteamMessageNonce{}) {
        return SteamMessagePeerResult::create(ERR_UNAVAILABLE);
    }
    if (!request_subscription) {
        const ObjectID self(get_instance_id());
        request_subscription = active->subscribe(SteamNetworkingMessagesSessionRequest_t::k_iCallback,
                [self](const void *data, size_t size) {
                    if (const auto service = message_service(self); service.is_valid()) {
                        service->on_session_request(data, size);
                    }
                });
        if (!request_subscription) {
            return SteamMessagePeerResult::create(ERR_BUSY);
        }
    }
    Ref<SteamMessagePeer> peer;
    peer.instantiate();
    peer->service_id = ObjectID(get_instance_id());
    peer->account = account;
    peer->game_peer = game_peer;
    peer->epoch = epoch;
    peer->admission = admission;
    peer->created_at_usec = created_at;
    peer->local_nonce = nonce;
    for (int64_t index = 0; index < channels.size(); ++index) {
        peer->channels.emplace(StringName(channels[index]), SteamMessagePeer::Channel{});
    }
    peers.emplace(account, peer);
    broker.owners.emplace(account, peer->service_id);
    return SteamMessagePeerResult::create(OK, peer);
#else
    return SteamMessagePeerResult::create(ERR_UNAVAILABLE);
#endif
}

bool SteamMessages::spend_budget(SteamMessagePeer::Budget &budget, uint32_t bytes, uint64_t now) {
#ifdef CRIMSON_WITH_STEAM
    if (now - budget.started >= 1000) {
        budget = SteamMessagePeer::Budget{ now, 0, 0 };
    }
    if (budget.packets >= maximum_packets_per_second || budget.bytes + bytes > maximum_bytes_per_second) {
        return false;
    }
    ++budget.packets;
    budget.bytes += bytes;
    return true;
#else
    return false;
#endif
}

Error SteamMessages::send_to(SteamMessagePeer *peer, const StringName &channel, const PackedByteArray &bytes,
        SteamDelivery::Mode delivery) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || !owns_peer(peer)) {
        return ERR_UNAVAILABLE;
    }
    if (delivery < SteamDelivery::RELIABLE || delivery > SteamDelivery::UNRELIABLE ||
            bytes.is_empty() || bytes.size() > maximum_payload) {
        return ERR_INVALID_PARAMETER;
    }
    const auto allowed = peer->channels.find(channel);
    if (allowed == peer->channels.end()) {
        return ERR_UNAUTHORIZED;
    }
    if (!spend_budget(peer->outgoing, static_cast<uint32_t>(bytes.size()), Time::get_singleton()->get_ticks_msec())) {
        return ERR_BUSY;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    const bool negotiating = peer->remote_nonce == SteamMessageNonce{} || peer->candidate_nonce != SteamMessageNonce{};
    if (negotiating) {
        request_proof(active, *peer, peer->candidate_nonce, now);
        if (delivery == SteamDelivery::UNRELIABLE) {
            return ERR_BUSY;
        }
    }
    if (delivery == SteamDelivery::RELIABLE && (negotiating || !peer->pending.empty())) {
        if (peer->pending.size() >= maximum_pending_packets || peer->pending_bytes + bytes.size() > maximum_pending_bytes) {
            return ERR_BUSY;
        }
        if (peer->pending.empty()) {
            peer->pending_deadline = now + pending_timeout_ms;
        }
        peer->pending.push_back({ channel, bytes });
        peer->pending_bytes += bytes.size();
        return OK;
    }
    const Error result = transmit(active, *peer, channel, bytes, delivery);
    if (result == ERR_CONNECTION_ERROR) {
        close_peer(peer, result);
    }
    return result;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamMessages::transmit(const std::shared_ptr<SteamRuntime> &active, SteamMessagePeer &peer,
        const StringName &channel, const PackedByteArray &bytes, SteamDelivery::Mode mode) {
#ifdef CRIMSON_WITH_STEAM
    auto allowed = peer.channels.find(channel);
    if (allowed == peer.channels.end()) {
        return ERR_UNAUTHORIZED;
    }
    uint64_t &previous = allowed->second.sent[mode];
    if (previous == (std::numeric_limits<uint64_t>::max)()) {
        return ERR_UNAVAILABLE;
    }
    const Error result = send_packet(active, peer.account, peer.epoch, game_namespace, channel, wire_data, previous + 1,
            peer.local_nonce, peer.remote_nonce, bytes.ptr(), bytes.size(), mode);
    if (result == OK) {
        ++previous;
    }
    return result;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamMessages::request_proof(const std::shared_ptr<SteamRuntime> &active, SteamMessagePeer &peer,
        const SteamMessageNonce &candidate, uint64_t now) {
#ifdef CRIMSON_WITH_STEAM
    if (now < peer.next_hello) {
        return;
    }
    if (peer.challenge == SteamMessageNonce{} || now >= peer.challenge_deadline ||
            (peer.candidate_nonce != SteamMessageNonce{} && candidate != peer.candidate_nonce)) {
        peer.challenge = new_nonce();
        peer.challenge_deadline = now + challenge_timeout_ms;
    }
    if (peer.challenge == SteamMessageNonce{}) {
        return;
    }
    peer.candidate_nonce = candidate;
    peer.next_hello = now + control_interval_ms;
    send_packet(active, peer.account, peer.epoch, game_namespace, {}, wire_hello, 0, peer.local_nonce,
            peer.candidate_nonce, peer.challenge.data(), peer.challenge.size(), SteamDelivery::RELIABLE);
#endif
}

void SteamMessages::handle_proof(const std::shared_ptr<SteamRuntime> &active, SteamMessagePeer &peer, uint8_t kind,
        const SteamMessageNonce &source, const SteamMessageNonce &target, const SteamMessageNonce &challenge,
        uint64_t now) {
#ifdef CRIMSON_WITH_STEAM
    if (source == SteamMessageNonce{} || challenge == SteamMessageNonce{}) {
        return;
    }
    if (kind == wire_hello && (target == SteamMessageNonce{} || target == peer.local_nonce)) {
        if (source != peer.remote_nonce) {
            request_proof(active, peer, source, now);
        }
        if (now >= peer.next_proof) {
            peer.next_proof = now + control_interval_ms;
            send_packet(active, peer.account, peer.epoch, game_namespace, {}, wire_proof, 0, peer.local_nonce,
                    source, challenge.data(), challenge.size(), SteamDelivery::RELIABLE);
        }
    } else if (kind == wire_proof && target == peer.local_nonce && challenge == peer.challenge &&
            now < peer.challenge_deadline &&
            (peer.candidate_nonce == SteamMessageNonce{} || peer.candidate_nonce == source)) {
        if (peer.remote_nonce != source) {
            if (peer.remote_nonce != SteamMessageNonce{}) {
                peer.pending.clear();
                peer.pending_bytes = 0;
            }
            for (auto &[name, lane] : peer.channels) {
                lane = {};
                lane.received_at.fill(now);
            }
        }
        peer.remote_nonce = source;
        peer.candidate_nonce = {};
        peer.challenge = {};
    }
#endif
}

void SteamMessages::flush_peer(const std::shared_ptr<SteamRuntime> &active,
        const Ref<SteamMessagePeer> &peer, uint64_t now) {
#ifdef CRIMSON_WITH_STEAM
    if (peer->challenge != SteamMessageNonce{} && now >= peer->challenge_deadline) {
        peer->candidate_nonce = {};
        peer->challenge = {};
    }
    if (peer->pending.empty()) {
        return;
    }
    if (now >= peer->pending_deadline) {
        close_peer(peer.ptr(), ERR_TIMEOUT);
        return;
    }
    if (peer->remote_nonce == SteamMessageNonce{} || peer->candidate_nonce != SteamMessageNonce{}) {
        request_proof(active, *peer.ptr(), peer->candidate_nonce, now);
        return;
    }
    for (int32_t count = 0; count < 4 && !peer->pending.empty(); ++count) {
        const auto &frame = peer->pending.front();
        const Error result = transmit(active, *peer.ptr(), frame.channel, frame.bytes, SteamDelivery::RELIABLE);
        if (result != OK) {
            if (result != ERR_BUSY) {
                close_peer(peer.ptr(), result);
            }
            return;
        }
        peer->pending_bytes -= frame.bytes.size();
        peer->pending.pop_front();
    }
#endif
}

Error SteamMessages::close_peer(const SteamMessagePeer *peer, Error reason) {
#ifdef CRIMSON_WITH_STEAM
    if (!peer || !SteamRuntime::on_main_thread()) {
        return ERR_UNAVAILABLE;
    }
    const auto found = peers.find(peer->account);
    if (found == peers.end() || found->second.ptr() != peer) {
        return ERR_UNAVAILABLE;
    }
    const auto retired = found->second;
    peers.erase(found);
    retired->service_id = ObjectID();
    retired->pending.clear();
    retired->pending_bytes = 0;
    if (const auto active = runtime.lock()) {
        auto &broker = broker_for(active);
        const auto owner = broker.owners.find(peer->account);
        if (owner != broker.owners.end() && owner->second == ObjectID(get_instance_id())) {
            broker.owners.erase(owner);
            ISteamNetworkingMessages *api = messages_api(active);
            const auto close = api ? active->symbol<decltype(&SteamAPI_ISteamNetworkingMessages_CloseChannelWithUser)>(
                                            "SteamAPI_ISteamNetworkingMessages_CloseChannelWithUser") : nullptr;
            if (close) {
                SteamNetworkingIdentity remote{};
                remote.SetSteamID64(peer->account);
                close(api, remote, native_channel);
            }
        }
    }
    retired->emit_signal("closed", reason);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamMessages::on_session_request(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || !data || size != sizeof(SteamNetworkingMessagesSessionRequest_t)) {
        return;
    }
    SteamNetworkingMessagesSessionRequest_t event{};
    std::memcpy(&event, data, sizeof(event));
    const uint64_t account = account_from(event.m_identityRemote);
    const auto found = peers.find(account);
    if (found == peers.end() || !owns_peer(found->second.ptr())) {
        return;
    }
    ISteamNetworkingMessages *api = messages_api(active);
    const auto accept = api ? active->symbol<decltype(&SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser)>(
                                     "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser") : nullptr;
    if (accept) {
        accept(api, event.m_identityRemote);
    }
#endif
}

void SteamMessages::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<Ref<SteamMessagePeer>> snapshot;
    for (const auto &[account, peer] : peers) {
        snapshot.push_back(peer);
    }
    for (const auto &peer : snapshot) {
        if (owns_peer(peer.ptr())) {
            flush_peer(active, peer, now);
        } else {
            close_peer(peer.ptr(), ERR_SKIP);
        }
    }
    if (!active_runtime()) {
        return;
    }

    auto &broker = broker_for(active);
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (broker.owners.empty() || frame == broker.last_receive_frame) {
        return;
    }
    broker.last_receive_frame = frame;
    ISteamNetworkingMessages *api = messages_api(active);
    const auto receive = api ? active->symbol<decltype(&SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel)>(
                                       "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel") : nullptr;
    if (!receive) {
        return;
    }

    // Detach each accepted record before invoking any game callbacks.
    struct Delivery {
        // Exact scoped peer that authorized the incoming account.
        Ref<SteamMessagePeer> peer;
        // Permitted logical app channel.
        StringName channel;
        // Owned bounded packet data.
        PackedByteArray bytes;
        // Mode verified from the SDK's message metadata.
        SteamDelivery::Mode mode;
        // Exact remote binding proven when this SDK record was validated.
        SteamMessageNonce source_nonce;
        // Exact local binding selected by the authenticated sender.
        SteamMessageNonce target_nonce;
    };
    std::vector<Delivery> deliveries;
    std::array<SteamNetworkingMessage_t *, maximum_native_batch> messages{};
    const int32_t count = (std::min)(receive(api, native_channel, messages.data(), maximum_native_batch),
            maximum_native_batch);
    for (int32_t index = 0; index < count; ++index) {
        auto *message = messages[index];
        if (!message) {
            continue;
        }
        const std::unique_ptr<SteamNetworkingMessage_t, void (*)(SteamNetworkingMessage_t *)> owned(message,
                [](SteamNetworkingMessage_t *value) { value->Release(); });
        if (message->m_nChannel != native_channel || !message->m_pData || message->m_cbSize <= header_size ||
                message->m_cbSize > header_size + maximum_namespace_length + maximum_channel_length + maximum_payload) {
            continue;
        }
        const auto source = broker.owners.find(account_from(message->m_identityPeer));
        if (source == broker.owners.end()) {
            continue;
        }
        const auto service = message_service(source->second);
        if (service.is_null()) {
            continue;
        }
        const auto found = service->peers.find(source->first);
        if (found == service->peers.end() || !service->owns_peer(found->second.ptr())) {
            continue;
        }
        const auto peer = found->second;
        if (message->m_usecTimeReceived < peer->created_at_usec) {
            continue;
        }
        const auto *data = static_cast<const uint8_t *>(message->m_pData);
        const int64_t namespace_length = data[6];
        const int64_t channel_length = data[7];
        const int64_t names_end = header_size + namespace_length + channel_length;
        const int64_t size = message->m_cbSize - names_end;
        const uint8_t kind = data[5];
        if (read_uint(data, 4) != wire_magic || data[4] != wire_version ||
                namespace_length > maximum_namespace_length || channel_length > maximum_channel_length ||
                size <= 0 || size > maximum_payload || read_uint(data + 8, 8) != static_cast<uint64_t>(peer->epoch)) {
            continue;
        }
        const auto expected_namespace = peer->game_peer ? String(service->game_namespace).utf8() : CharString();
        if (namespace_length != expected_namespace.length() ||
                (namespace_length && std::memcmp(data + header_size, expected_namespace.get_data(),
                                             static_cast<size_t>(namespace_length)))) {
            continue;
        }
        const auto source_nonce = read_nonce(data + 24);
        const auto target_nonce = read_nonce(data + 40);
        const uint64_t sequence = read_uint(data + 16, 8);
        if (kind == wire_hello || kind == wire_proof) {
            if (channel_length == 0 && sequence == 0 && size == static_cast<int64_t>(SteamMessageNonce{}.size()) &&
                    (message->m_nFlags & k_nSteamNetworkingSend_Reliable)) {
                service->handle_proof(active, *peer.ptr(), kind, source_nonce, target_nonce,
                        read_nonce(data + names_end), now);
            }
            continue;
        }
        if (kind != wire_data || source_nonce == SteamMessageNonce{} || target_nonce != peer->local_nonce ||
                source_nonce != peer->remote_nonce || peer->candidate_nonce != SteamMessageNonce{}) {
            if (kind == wire_data && target_nonce == peer->local_nonce &&
                    peer->remote_nonce == SteamMessageNonce{} && source_nonce != SteamMessageNonce{}) {
                service->request_proof(active, *peer.ptr(), source_nonce, now);
            }
            continue;
        }
        const auto *channel_data = reinterpret_cast<const char *>(data + header_size + namespace_length);
        if (!valid_channel_bytes(channel_data, channel_length)) {
            continue;
        }
        const StringName channel(String::utf8(channel_data, channel_length));
        const auto allowed = peer->channels.find(channel);
        if (allowed == peer->channels.end()) {
            continue;
        }
        const auto mode = (message->m_nFlags & k_nSteamNetworkingSend_Reliable) ? SteamDelivery::RELIABLE :
                                                                                 SteamDelivery::UNRELIABLE;
        auto &lane = allowed->second;
        // Two fixed send windows can meet at a boundary. Grow the gap with elapsed
        // time so extended loss can recover without accepting arbitrary sequence jumps.
        const uint64_t elapsed_seconds = (now - lane.received_at[mode]) / 1000;
        const uint64_t forward_allowance = 2 * maximum_packets_per_second +
                elapsed_seconds * (maximum_packets_per_second + 4);
        if (sequence <= lane.received[mode] || sequence - lane.received[mode] > forward_allowance ||
                !spend_budget(peer->incoming, static_cast<uint32_t>(size), now)) {
            continue;
        }
        PackedByteArray bytes;
        if (bytes.resize(size) != OK) {
            continue;
        }
        std::memcpy(bytes.ptrw(), data + names_end, static_cast<size_t>(size));
        lane.received[mode] = sequence;
        lane.received_at[mode] = now;
        deliveries.push_back({ peer, channel, bytes, mode, source_nonce, target_nonce });
    }
    for (const auto &delivery : deliveries) {
        const auto service = delivery.peer->service();
        if (service.is_valid() && service->owns_peer(delivery.peer.ptr()) &&
                delivery.source_nonce == delivery.peer->remote_nonce &&
                delivery.target_nonce == delivery.peer->local_nonce &&
                delivery.peer->candidate_nonce == SteamMessageNonce{}) {
            delivery.peer->emit_signal("received", delivery.channel, delivery.bytes, delivery.mode);
        }
    }
#endif
}

} // namespace crimson::steam
