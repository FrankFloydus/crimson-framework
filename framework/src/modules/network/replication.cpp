// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "replication.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace crimson::replication {

using namespace godot;

namespace {

constexpr int64_t server_id = MultiplayerPeer::TARGET_PEER_SERVER;
constexpr uint64_t baseline_timeout_msec = 15000;
constexpr int64_t max_depth = 20;
constexpr int64_t max_nodes = 16384;
constexpr int64_t max_characters = 65536;
constexpr int64_t max_record_id = 128;

constexpr const char *baseline_method = "_crimson_replication_baseline_v1";
constexpr const char *delta_method = "_crimson_replication_delta_v1";
constexpr const char *ack_method = "_crimson_replication_ack_v1";
constexpr const char *resync_method = "_crimson_replication_resync_v1";

// Bound the aggregate DTO before detaching or transmitting it.
struct DtoBudget {
    // Total number of visited values and dictionary keys.
    int64_t nodes = 0;
    // Total number of codepoints across strings and dictionary keys.
    int64_t characters = 0;

    // Consume part of the bounded string budget.
    bool accept_string(const String &value) {
        characters += value.length();
        return characters <= max_characters;
    }
};

// Reject cycles, Objects, unsupported variants, and views beyond the DTO budget.
bool validate_value(const Variant &value, int64_t depth, DtoBudget &budget) {
    if (++budget.nodes > max_nodes || depth > max_depth) {
        return false;
    }
    switch (value.get_type()) {
        case Variant::NIL:
        case Variant::BOOL:
        case Variant::INT:
            return true;
        case Variant::FLOAT:
            return std::isfinite(static_cast<double>(value));
        case Variant::STRING:
            return budget.accept_string(value);
        case Variant::ARRAY: {
            const Array array = value;
            if (array.size() > max_nodes - budget.nodes) {
                return false;
            }
            for (int64_t index = 0; index < array.size(); ++index) {
                if (!validate_value(array[index], depth + 1, budget)) {
                    return false;
                }
            }
            return true;
        }
        case Variant::DICTIONARY: {
            const Dictionary dictionary = value;
            if (dictionary.size() > (max_nodes - budget.nodes) / 2) {
                return false;
            }
            const Array keys = dictionary.keys();
            for (int64_t index = 0; index < keys.size(); ++index) {
                const Variant &key = keys[index];
                if (key.get_type() != Variant::STRING || !validate_value(key, depth + 1, budget) ||
                        !validate_value(dictionary[key], depth + 1, budget)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

// Accept stable nonempty identifiers suitable for keyed view records.
bool valid_record_id(const String &id) {
    return !id.is_empty() && id.length() <= max_record_id;
}

// Validate the whole view and return a copy with no mutable container aliases.
bool copy_view(const Dictionary &view, Dictionary &output) {
    DtoBudget budget;
    if (!validate_value(view, 0, budget)) {
        return false;
    }
    const Array keys = view.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        if (!valid_record_id(keys[index])) {
            return false;
        }
    }
    output = view.duplicate(true);
    return true;
}

// Read the engine's monotonic clock for acknowledgment deadlines.
uint64_t ticks_msec() {
    return Time::get_singleton()->get_ticks_msec();
}

} // namespace

StateReplication::StateReplication() {
    set_process_mode(PROCESS_MODE_ALWAYS);
}

void StateReplication::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_projector", "callback"), &StateReplication::set_projector);
    ClassDB::bind_method(D_METHOD("advance_world", "world_epoch", "committed_revision"), &StateReplication::advance_world);
    ClassDB::bind_method(D_METHOD("register_peer", "peer_id", "player_id", "binding_generation"),
            &StateReplication::register_peer);
    ClassDB::bind_method(D_METHOD("send_baseline", "peer_id"), &StateReplication::send_baseline);
    ClassDB::bind_method(D_METHOD("publish_changes", "committed_revision"), &StateReplication::publish_changes);
    ClassDB::bind_method(D_METHOD("remove_peer", "peer_id", "binding_generation"), &StateReplication::remove_peer,
            DEFVAL(0));
    ClassDB::bind_method(D_METHOD("is_peer_ready", "peer_id", "binding_generation"), &StateReplication::is_peer_ready,
            DEFVAL(0));
    ClassDB::bind_method(D_METHOD("is_client_ready"), &StateReplication::is_client_ready);
    ClassDB::bind_method(D_METHOD("get_projection"), &StateReplication::get_projection);
    ClassDB::bind_method(D_METHOD("clear_projection"), &StateReplication::clear_projection);
    ClassDB::bind_method(D_METHOD("get_world_epoch"), &StateReplication::get_world_epoch);
    ClassDB::bind_method(D_METHOD("get_revision"), &StateReplication::get_revision);
    ClassDB::bind_method(D_METHOD("reset"), &StateReplication::reset);

    ClassDB::bind_method(D_METHOD("_crimson_replication_baseline_v1", "world_epoch", "generation", "baseline",
                                 "committed_revision", "view"),
            &StateReplication::rpc_baseline);
    ClassDB::bind_method(D_METHOD("_crimson_replication_delta_v1", "world_epoch", "generation", "baseline",
                                 "sequence", "committed_revision", "changed", "removed"),
            &StateReplication::rpc_delta);
    ClassDB::bind_method(D_METHOD("_crimson_replication_ack_v1", "world_epoch", "generation", "baseline"),
            &StateReplication::rpc_ack);
    ClassDB::bind_method(D_METHOD("_crimson_replication_resync_v1", "world_epoch", "generation", "baseline"),
            &StateReplication::rpc_resync);

    ADD_SIGNAL(MethodInfo("peer_ready", PropertyInfo(Variant::INT, "peer_id"),
            PropertyInfo(Variant::INT, "world_epoch"), PropertyInfo(Variant::INT, "binding_generation")));
    ADD_SIGNAL(MethodInfo("peer_sync_failed", PropertyInfo(Variant::INT, "peer_id"),
            PropertyInfo(Variant::INT, "world_epoch"), PropertyInfo(Variant::INT, "error")));
    ADD_SIGNAL(MethodInfo("projection_updated", PropertyInfo(Variant::INT, "world_epoch"),
            PropertyInfo(Variant::INT, "committed_revision"), PropertyInfo(Variant::BOOL, "baseline")));
    ADD_SIGNAL(MethodInfo("projection_cleared"));
}

void StateReplication::_enter_tree() {
    leaving_tree = false;

    Dictionary authority_config;
    authority_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    authority_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    authority_config["call_local"] = false;
    authority_config["channel"] = 0;
    rpc_config(baseline_method, authority_config);
    rpc_config(delta_method, authority_config);

    Dictionary client_config = authority_config.duplicate();
    client_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
    rpc_config(ack_method, client_config);
    rpc_config(resync_method, client_config);
    set_process(true);
}

void StateReplication::_exit_tree() {
    leaving_tree = true;
    clear_state(false);
    network.unref();
    transport.unref();
    last_server_baseline = 0;
}

void StateReplication::clear_state(bool notify) {
    const bool had_projection = client.ready;
    ++state_generation;
    peers.clear();
    client = ClientStream{};
    baseline_rejection_reported = false;
    epoch = 0;
    revision = 0;
    if (had_projection && notify) {
        emit_signal("projection_cleared");
    }
}

void StateReplication::refresh_transport() {
    if (!is_inside_tree() || leaving_tree) {
        return;
    }
    const Ref<MultiplayerAPI> current = get_multiplayer();
    const Ref<MultiplayerPeer> current_transport = current.is_valid() ? current->get_multiplayer_peer() :
                                                                     Ref<MultiplayerPeer>();
    if (current != network || current_transport != transport) {
        network = current;
        transport = current_transport;
        last_server_baseline = 0;
        clear_state(true);
    }
}

bool StateReplication::transport_is_current() const {
    return !leaving_tree && is_inside_tree() && network.is_valid() && transport.is_valid() &&
            get_multiplayer() == network && network->get_multiplayer_peer() == transport &&
            transport->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED;
}

bool StateReplication::is_authority() const {
    return transport_is_current() && network->is_server();
}

bool StateReplication::peer_is_connected(int64_t peer_id) const {
    return peer_id > server_id && peer_id <= std::numeric_limits<int32_t>::max() &&
            network->get_peers().has(static_cast<int32_t>(peer_id));
}

int64_t StateReplication::incoming_client() const {
    if (!is_authority()) {
        return 0;
    }
    const int64_t sender = network->get_remote_sender_id();
    return peers.find(sender) != peers.end() && peer_is_connected(sender) ? sender : 0;
}

bool StateReplication::incoming_server() const {
    return transport_is_current() && !network->is_server() && network->get_remote_sender_id() == server_id;
}

Error StateReplication::set_projector(const Callable &callback) {
    if (projecting) {
        return ERR_BUSY;
    }
    refresh_transport();
    if (!peers.empty()) {
        return ERR_BUSY;
    }
    if (!callback.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    projector = callback;
    return OK;
}

Error StateReplication::advance_world(int64_t world_epoch, int64_t committed_revision) {
    if (projecting) {
        return ERR_BUSY;
    }
    refresh_transport();
    if (!is_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (world_epoch <= epoch || committed_revision < 0) {
        return ERR_INVALID_PARAMETER;
    }

    epoch = world_epoch;
    revision = committed_revision;
    for (auto &[id, peer] : peers) {
        PeerStream reset_stream;
        reset_stream.player_id = peer.player_id;
        reset_stream.generation = peer.generation;
        peer = std::move(reset_stream);
    }
    return OK;
}

Error StateReplication::register_peer(int64_t peer_id, const String &player_id, int64_t binding_generation) {
    if (projecting) {
        return ERR_BUSY;
    }
    refresh_transport();
    if (!is_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (!peer_is_connected(peer_id) || !valid_record_id(player_id) || binding_generation <= 0) {
        return ERR_INVALID_PARAMETER;
    }

    const auto existing = peers.find(peer_id);
    if (existing != peers.end() && binding_generation <= existing->second.generation) {
        return binding_generation == existing->second.generation && player_id == existing->second.player_id ?
                OK : ERR_INVALID_PARAMETER;
    }
    PeerStream stream;
    stream.player_id = player_id;
    stream.generation = binding_generation;
    peers.insert_or_assign(peer_id, std::move(stream));
    return OK;
}

void StateReplication::remove_peer(int64_t peer_id, int64_t binding_generation) {
    if (projecting) {
        return;
    }
    const auto found = peers.find(peer_id);
    if (found != peers.end() && (binding_generation == 0 || found->second.generation == binding_generation)) {
        peers.erase(found);
    }
}

bool StateReplication::is_peer_ready(int64_t peer_id, int64_t binding_generation) const {
    if (!is_authority() || !peer_is_connected(peer_id)) {
        return false;
    }
    const auto found = peers.find(peer_id);
    return found != peers.end() && found->second.ready &&
            (binding_generation == 0 || binding_generation == found->second.generation);
}

bool StateReplication::is_client_ready() const {
    return client.ready && transport_is_current();
}

Dictionary StateReplication::get_projection() const {
    return is_client_ready() ? client.view.duplicate(true) : Dictionary();
}

void StateReplication::clear_projection() {
    if (projecting || !client.ready) {
        return;
    }
    client.ready = false;
    client.view.clear();
    emit_signal("projection_cleared");
}

int64_t StateReplication::get_world_epoch() const {
    if (!transport_is_current()) {
        return 0;
    }
    return network->is_server() ? epoch : client.epoch;
}

int64_t StateReplication::get_revision() const {
    if (!transport_is_current()) {
        return 0;
    }
    return network->is_server() ? revision : client.revision;
}

void StateReplication::reset() {
    if (!projecting) {
        clear_state(true);
    }
}

Error StateReplication::project(int64_t peer_id, const PeerStream &peer, Dictionary &view, bool report_failure) {
    if (!projector.is_valid()) {
        if (report_failure) {
            UtilityFunctions::printerr("[Crimson] Replication cannot project a view. Configure an authority projector first.");
        }
        return ERR_UNCONFIGURED;
    }

    const uint64_t original_state = state_generation;
    projecting = true;
    const Variant result = projector.call(peer_id, peer.player_id, epoch, revision);
    projecting = false;
    if (original_state != state_generation || !is_authority() || !peer_is_connected(peer_id)) {
        return ERR_CONNECTION_ERROR;
    }
    if (result.get_type() != Variant::DICTIONARY) {
        if (report_failure) {
            UtilityFunctions::printerr("[Crimson] Replication projector returned a non-Dictionary result.");
        }
        return ERR_INVALID_DATA;
    }
    const Dictionary projection = result;
    const Variant accepted = projection.get("ok", false);
    const Variant payload = projection.get("view", Variant());
    if (accepted.get_type() != Variant::BOOL || !static_cast<bool>(accepted)) {
        if (report_failure) {
            const Variant detail = projection.get("message", "");
            const String message = detail.get_type() == Variant::STRING ?
                    String(detail).left(512).replace("\r", " ").replace("\n", " ").strip_edges() : String();
            UtilityFunctions::printerr("[Crimson] Replication projector rejected the view. ", message);
        }
        return ERR_INVALID_DATA;
    }
    if (payload.get_type() != Variant::DICTIONARY || !copy_view(payload, view)) {
        if (report_failure) {
            UtilityFunctions::printerr("[Crimson] Replication projector returned a view outside the bounded plain-data contract.");
        }
        return ERR_INVALID_DATA;
    }
    return OK;
}

Error StateReplication::send_baseline(int64_t peer_id) {
    if (projecting) {
        return ERR_BUSY;
    }
    refresh_transport();
    if (!is_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (epoch <= 0) {
        return ERR_UNCONFIGURED;
    }
    const auto found = peers.find(peer_id);
    if (found == peers.end() || !peer_is_connected(peer_id)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (last_baseline == std::numeric_limits<int64_t>::max()) {
        return ERR_OUT_OF_MEMORY;
    }

    PeerStream &peer = found->second;
    peer.ready = false;
    peer.baseline = 0;
    peer.deadline = 0;
    peer.view.clear();
    Dictionary view;
    const Error error = project(peer_id, peer, view);
    if (error != OK) {
        return error;
    }

    const int64_t baseline = ++last_baseline;
    const Error sent = rpc_id(peer_id, baseline_method, epoch, peer.generation, baseline, revision, view);
    if (sent != OK) {
        return sent;
    }
    peer.view = view;
    peer.baseline = baseline;
    peer.sequence = 0;
    peer.revision = revision;
    peer.deadline = ticks_msec() + baseline_timeout_msec;
    return OK;
}

Error StateReplication::send_changes(int64_t peer_id, PeerStream &peer, bool report_failure) {
    Dictionary next_view;
    const Error error = project(peer_id, peer, next_view, report_failure);
    if (error != OK) {
        return error;
    }

    Dictionary changed;
    PackedStringArray removed;
    const Array keys = next_view.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const Variant &key = keys[index];
        const Variant &value = next_view[key];
        if (!peer.view.has(key) || value != peer.view[key]) {
            changed[key] = value;
        }
    }
    const Array old_keys = peer.view.keys();
    for (int64_t index = 0; index < old_keys.size(); ++index) {
        const Variant &key = old_keys[index];
        if (!next_view.has(key)) {
            removed.append(key);
        }
    }
    if (changed.is_empty() && removed.is_empty()) {
        return OK;
    }
    if (peer.sequence == std::numeric_limits<int64_t>::max()) {
        return ERR_OUT_OF_MEMORY;
    }

    const int64_t next_sequence = peer.sequence + 1;
    const Error sent = rpc_id(peer_id, delta_method, epoch, peer.generation, peer.baseline, next_sequence,
            revision, changed, removed);
    if (sent != OK) {
        return sent;
    }
    peer.sequence = next_sequence;
    peer.revision = revision;
    peer.view = next_view;
    return OK;
}

Error StateReplication::publish_changes(int64_t committed_revision) {
    if (projecting) {
        return ERR_BUSY;
    }
    refresh_transport();
    if (!is_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (epoch <= 0 || committed_revision < revision) {
        return ERR_INVALID_PARAMETER;
    }

    revision = committed_revision;
    const int64_t publishing_epoch = epoch;
    const uint64_t original_state = state_generation;
    std::vector<int64_t> recipients;
    for (const auto &[id, peer] : peers) {
        if (peer.ready) {
            recipients.push_back(id);
        }
    }

    Error first_error = OK;
    for (const int64_t id : recipients) {
        auto found = peers.find(id);
        if (found == peers.end() || !found->second.ready || !peer_is_connected(id)) {
            continue;
        }
        const Error error = send_changes(id, found->second, first_error == OK);
        if (state_generation != original_state || epoch != publishing_epoch || revision != committed_revision) {
            return ERR_CONNECTION_ERROR;
        }
        if (error != OK) {
            if (first_error == OK) {
                first_error = error;
            }
            found->second.ready = false;
            found->second.baseline = 0;
            found->second.view.clear();
            emit_signal("peer_sync_failed", id, publishing_epoch, error);
            if (state_generation != original_state || epoch != publishing_epoch || revision != committed_revision ||
                    !is_authority()) {
                return first_error;
            }
        }
    }
    return first_error;
}

void StateReplication::_process(double) {
    refresh_transport();
    if (!transport_is_current()) {
        if (client.baseline != 0 || epoch != 0 || !peers.empty()) {
            clear_state(true);
        }
        return;
    }
    if (!network->is_server()) {
        return;
    }

    const uint64_t now = ticks_msec();
    const PackedInt32Array connected = network->get_peers();
    std::vector<std::pair<int64_t, int64_t>> expired;
    for (auto iterator = peers.begin(); iterator != peers.end();) {
        const int64_t id = iterator->first;
        PeerStream &peer = iterator->second;
        if (!connected.has(id)) {
            iterator = peers.erase(iterator);
            continue;
        }
        if (peer.deadline != 0 && now >= peer.deadline) {
            peer.deadline = 0;
            peer.baseline = 0;
            peer.view.clear();
            expired.emplace_back(id, peer.generation);
        }
        ++iterator;
    }

    const int64_t expired_epoch = epoch;
    const uint64_t original_state = state_generation;
    for (const auto &[id, generation] : expired) {
        const auto found = peers.find(id);
        if (found != peers.end() && found->second.generation == generation && found->second.baseline == 0) {
            emit_signal("peer_sync_failed", id, expired_epoch, ERR_TIMEOUT);
        }
        if (state_generation != original_state || epoch != expired_epoch || !is_authority()) {
            break;
        }
    }
}

void StateReplication::rpc_baseline(int64_t world_epoch, int64_t generation, int64_t baseline,
        int64_t committed_revision, const Dictionary &view) {
    refresh_transport();
    if (!incoming_server() || world_epoch <= 0 || generation <= 0 || baseline <= last_server_baseline ||
            committed_revision < 0 || world_epoch < client.epoch ||
            (world_epoch == client.epoch && committed_revision < client.revision)) {
        return;
    }

    Dictionary detached;
    const bool copied = copy_view(view, detached);
    const Error acknowledgment = copied ? rpc_id(server_id, ack_method, world_epoch, generation, baseline) : ERR_INVALID_DATA;
    if (acknowledgment != OK) {
        if (!baseline_rejection_reported) {
            baseline_rejection_reported = true;
            if (!copied) {
                UtilityFunctions::printerr("[Crimson] Rejected the authority baseline because it violates the bounded plain-data contract.");
            } else {
                UtilityFunctions::printerr("[Crimson] Could not acknowledge the authority baseline (",
                        UtilityFunctions::error_string(acknowledgment), "). The client view is unavailable.");
            }
        }
        clear_projection();
        return;
    }
    baseline_rejection_reported = false;
    last_server_baseline = baseline;
    client.generation = generation;
    client.baseline = baseline;
    client.sequence = 0;
    client.epoch = world_epoch;
    client.revision = committed_revision;
    client.view = detached;
    client.ready = true;
    emit_signal("projection_updated", world_epoch, committed_revision, true);
}

void StateReplication::rpc_delta(int64_t world_epoch, int64_t generation, int64_t baseline, int64_t sequence,
        int64_t committed_revision, const Dictionary &changed, const PackedStringArray &removed) {
    refresh_transport();
    if (!incoming_server() || !client.ready || world_epoch != client.epoch || generation != client.generation ||
            baseline != client.baseline || sequence <= client.sequence) {
        return;
    }
    if (sequence != client.sequence + 1 || committed_revision < client.revision || removed.size() > max_nodes) {
        request_resync("The delta sequence, revision, or removal count is invalid.");
        return;
    }

    Dictionary candidate = client.view.duplicate();
    for (int64_t index = 0; index < removed.size(); ++index) {
        const String &key = removed[index];
        if (!valid_record_id(key) || changed.has(key) || !candidate.erase(key)) {
            request_resync("The delta contains an invalid, duplicate, or conflicting record removal.");
            return;
        }
    }
    if (changed.size() > max_nodes) {
        request_resync("The delta contains too many changed records.");
        return;
    }
    candidate.merge(changed, true);
    Dictionary detached;
    if (!copy_view(candidate, detached)) {
        request_resync("The updated view violates the bounded plain-data contract.");
        return;
    }

    client.sequence = sequence;
    client.revision = committed_revision;
    client.view = detached;
    emit_signal("projection_updated", world_epoch, committed_revision, false);
}

void StateReplication::request_resync(const char *reason) {
    if (client.ready) {
        const Error sent = rpc_id(server_id, resync_method, client.epoch, client.generation, client.baseline);
        if (sent == OK) {
            UtilityFunctions::printerr("[Crimson] Replication view invalidated. ", reason, " Requested a fresh baseline.");
        } else {
            UtilityFunctions::printerr("[Crimson] Replication view invalidated. ", reason,
                    " Could not request a fresh baseline (", UtilityFunctions::error_string(sent), "). Rejoin to synchronize again.");
        }
        clear_projection();
    }
}

void StateReplication::rpc_ack(int64_t world_epoch, int64_t generation, int64_t baseline) {
    if (projecting) {
        return;
    }
    refresh_transport();
    const int64_t sender = incoming_client();
    if (sender == 0 || world_epoch != epoch) {
        return;
    }
    PeerStream &peer = peers.at(sender);
    if (peer.generation != generation || peer.baseline != baseline || peer.deadline == 0 ||
            ticks_msec() >= peer.deadline) {
        return;
    }

    peer.deadline = 0;
    if (peer.revision != revision) {
        const uint64_t original_state = state_generation;
        const Error catchup = send_changes(sender, peer);
        if (state_generation != original_state) {
            return;
        }
        if (catchup != OK) {
            peer.baseline = 0;
            peer.view.clear();
            emit_signal("peer_sync_failed", sender, world_epoch, catchup);
            return;
        }
    }
    peer.ready = true;
    emit_signal("peer_ready", sender, world_epoch, generation);
}

void StateReplication::rpc_resync(int64_t world_epoch, int64_t generation, int64_t baseline) {
    if (projecting) {
        return;
    }
    refresh_transport();
    const int64_t sender = incoming_client();
    if (sender == 0 || world_epoch != epoch) {
        return;
    }
    const PeerStream &peer = peers.at(sender);
    if (!peer.ready || peer.generation != generation || peer.baseline != baseline) {
        return;
    }
    const uint64_t original_state = state_generation;
    const Error error = send_baseline(sender);
    if (error != OK && state_generation == original_state) {
        emit_signal("peer_sync_failed", sender, world_epoch, error);
    }
}

} // namespace crimson::replication
