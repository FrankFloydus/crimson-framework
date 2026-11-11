// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "match_network.hpp"

#include "../modules/match_runtime/transaction_coordinator.hpp"

#include <godot_cpp/classes/offline_multiplayer_peer.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <limits>
#include <utility>
#include <vector>

using namespace godot;

namespace crimson::integration {
namespace {

// The server owns command execution, receipts, and transition notices.
constexpr int32_t server_id = MultiplayerPeer::TARGET_PEER_SERVER;
// Limit the resources retained for this client's unacknowledged submissions.
constexpr std::size_t max_pending_commands = 128;
// Bound the wait for one command receipt without relying on scaled game time.
constexpr uint64_t receipt_timeout_msec = 10000;
// RPC names are identical on each peer and reserved for the match integration.
constexpr const char *submit_method = "_crimson_match_submit";
constexpr const char *receipt_method = "_crimson_match_receipt";
constexpr const char *announce_method = "_crimson_match_world_announced";
constexpr const char *finish_method = "_crimson_match_world_finished";

// Create a terminal operation result with a stable transport code.
Dictionary failure(const char *code, const char *message) {
    Dictionary result;
    result["ok"] = false;
    result["code"] = code;
    result["message"] = message;
    return result;
}

// Verify the minimum command result shape and detach the reply for transport or publication.
bool copy_result(const Variant &value, Dictionary &output) {
    return value.get_type() == Variant::DICTIONARY &&
            match_runtime::copy_runtime_dto(value, output) &&
            output.get("ok", Variant()).get_type() == Variant::BOOL;
}

// Check whether the peer is still able to exchange traffic.
bool is_transport_connected(const Ref<MultiplayerPeer> &peer) {
    return peer.is_valid() && peer->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED;
}

// Match the runtime's bounded, trimmed identifier format for destinations.
bool valid_world_id(const StringName &world_id) {
    const String text = world_id;
    return !text.is_empty() && text.length() <= 128 && text.strip_edges() == text;
}

} // namespace

CrimsonMatchNetwork::CrimsonMatchNetwork() {
    set_process_mode(PROCESS_MODE_ALWAYS);
}

void CrimsonMatchNetwork::_bind_methods() {
    ClassDB::bind_method(D_METHOD(submit_method, "request_id", "intent"), &CrimsonMatchNetwork::rpc_submit);
    ClassDB::bind_method(D_METHOD(receipt_method, "request_id", "result"), &CrimsonMatchNetwork::rpc_receipt);
    ClassDB::bind_method(D_METHOD(announce_method, "world_id", "epoch"), &CrimsonMatchNetwork::rpc_world_announced);
    ClassDB::bind_method(D_METHOD(finish_method, "epoch", "ok", "recovery"), &CrimsonMatchNetwork::rpc_world_finished);
    ADD_SIGNAL(MethodInfo("world_announced", PropertyInfo(Variant::STRING_NAME, "world_id"),
            PropertyInfo(Variant::INT, "epoch")));
    ADD_SIGNAL(MethodInfo("world_finished", PropertyInfo(Variant::INT, "epoch"),
            PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::BOOL, "recovery")));
}

void CrimsonMatchNetwork::_enter_tree() {
    leaving_tree = false;
    set_multiplayer_authority(server_id, false);
    Dictionary authority_config;
    authority_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    authority_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    authority_config["call_local"] = false;
    authority_config["channel"] = 0;
    rpc_config(receipt_method, authority_config);
    rpc_config(announce_method, authority_config);
    rpc_config(finish_method, authority_config);

    Dictionary submit_config = authority_config.duplicate();
    submit_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
    rpc_config(submit_method, submit_config);
}

void CrimsonMatchNetwork::_exit_tree() {
    leaving_tree = true;
    reset();
}

Error CrimsonMatchNetwork::bind_transport() {
    if (leaving_tree || !is_inside_tree()) {
        return ERR_UNCONFIGURED;
    }
    if (network.is_valid()) {
        return owns_transport() ? OK : ERR_BUSY;
    }
    const Ref<MultiplayerAPI> current = get_multiplayer();
    if (current.is_null()) {
        return ERR_UNCONFIGURED;
    }
    const Ref<MultiplayerPeer> peer = current->get_multiplayer_peer();
    if (!is_transport_connected(peer)) {
        return ERR_CONNECTION_ERROR;
    }
    const Ref<SceneMultiplayer> scene_api = current;
    if (Object::cast_to<OfflineMultiplayerPeer>(peer.ptr()) == nullptr && scene_api.is_valid() &&
            scene_api->is_object_decoding_allowed()) {
        return ERR_UNAUTHORIZED;
    }
    network = current;
    transport = peer;
    set_process(true);
    return OK;
}

void CrimsonMatchNetwork::set_dispatcher(const Callable &callback) {
    dispatcher = callback;
}

Ref<CrimsonGameOperation> CrimsonMatchNetwork::submit_remote(const Dictionary &intent) {
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
    if (!owns_transport() || network->is_server()) {
        operation->finish(failure("Disconnected", "A connected client session is required."));
        return operation;
    }
    if (pending.size() >= max_pending_commands ||
            last_request_id == (std::numeric_limits<int64_t>::max)()) {
        operation->finish(failure("Busy", "No command receipt slot is available."));
        return operation;
    }

    Dictionary detached;
    if (!match_runtime::copy_runtime_dto(intent, detached)) {
        operation->finish(failure("InvalidIntent", "The command intent must contain bounded plain data."));
        return operation;
    }
    const int64_t request_id = ++last_request_id;
    pending.emplace(request_id, PendingCommand{ operation, Time::get_singleton()->get_ticks_msec() + receipt_timeout_msec });
    if (rpc_id(server_id, submit_method, request_id, detached) != OK) {
        pending.erase(request_id);
        operation->finish(failure("SendFailed", "The command could not be sent to the authority."));
    }
    return operation;
}

void CrimsonMatchNetwork::reset() {
    auto disconnected = std::exchange(pending, {});
    network.unref();
    transport.unref();
    ++generation;
    set_process(false);
    for (const auto &entry : disconnected) {
        entry.second.operation->finish(failure("Disconnected", "The session ended before the command receipt arrived."));
    }
}

Error CrimsonMatchNetwork::announce_world(const StringName &world_id, int64_t epoch) {
    if (!is_bound_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (epoch <= 0 || !valid_world_id(world_id)) {
        return ERR_INVALID_PARAMETER;
    }
    return network->get_peers().is_empty() ? OK : rpc(announce_method, world_id, epoch);
}

Error CrimsonMatchNetwork::finish_world(int64_t epoch, bool ok, bool recovery) {
    if (!is_bound_authority()) {
        return ERR_UNAUTHORIZED;
    }
    if (epoch <= 0 || (ok && recovery)) {
        return ERR_INVALID_PARAMETER;
    }
    return network->get_peers().is_empty() ? OK : rpc(finish_method, epoch, ok, recovery);
}

void CrimsonMatchNetwork::_process(double) {
    if (!owns_transport()) {
        reset();
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<Ref<CrimsonGameOperation>> expired;
    for (auto entry = pending.begin(); entry != pending.end();) {
        if (entry->second.deadline_msec > now) {
            ++entry;
            continue;
        }
        expired.push_back(entry->second.operation);
        entry = pending.erase(entry);
    }
    for (const Ref<CrimsonGameOperation> &operation : expired) {
        operation->finish(failure("Timeout", "The authority's command receipt did not arrive before the deadline."));
    }
}

bool CrimsonMatchNetwork::owns_transport() const {
    if (leaving_tree || !is_inside_tree() || network.is_null() || !is_transport_connected(transport)) {
        return false;
    }
    const Ref<MultiplayerAPI> current = get_multiplayer();
    return current == network && current->get_multiplayer_peer() == transport;
}

bool CrimsonMatchNetwork::is_bound_authority() const {
    return owns_transport() && network->is_server() && network->get_unique_id() == server_id;
}

bool CrimsonMatchNetwork::is_incoming_authority() const {
    return owns_transport() && !network->is_server() && network->get_remote_sender_id() == server_id;
}

int64_t CrimsonMatchNetwork::get_incoming_client() const {
    if (!is_bound_authority()) {
        return 0;
    }
    const int32_t sender = network->get_remote_sender_id();
    return sender > server_id && network->get_peers().has(sender) ? sender : 0;
}

void CrimsonMatchNetwork::rpc_submit(int64_t request_id, const Dictionary &intent) {
    const int64_t sender = get_incoming_client();
    if (sender == 0 || request_id <= 0) {
        return;
    }

    const uint64_t started_generation = generation;
    const Callable callback = dispatcher;
    Dictionary detached;
    Dictionary result;
    if (!match_runtime::copy_runtime_dto(intent, detached)) {
        result = failure("InvalidIntent", "The command intent must contain bounded plain data.");
    } else if (!callback.is_valid()) {
        result = failure("HandlerUnavailable", "The authority has no active match dispatcher.");
    } else if (!copy_result(callback.call(sender, detached), result)) {
        result = failure("InvalidHandlerResult", "The authority dispatcher returned an invalid command result.");
    }
    if (generation == started_generation && is_bound_authority() &&
            network->get_peers().has(static_cast<int32_t>(sender))) {
        rpc_id(sender, receipt_method, request_id, result);
    }
}

void CrimsonMatchNetwork::rpc_receipt(int64_t request_id, const Dictionary &result) {
    if (!is_incoming_authority()) {
        return;
    }
    const auto entry = pending.find(request_id);
    if (entry == pending.end()) {
        return;
    }
    const Ref<CrimsonGameOperation> operation = entry->second.operation;
    const uint64_t deadline_msec = entry->second.deadline_msec;
    pending.erase(entry);
    Dictionary detached;
    if (Time::get_singleton()->get_ticks_msec() >= deadline_msec) {
        operation->finish(failure("Timeout", "The authority's command receipt did not arrive before the deadline."));
    } else if (copy_result(result, detached)) {
        operation->finish(detached);
    } else {
        operation->finish(failure("InvalidReceipt", "The authority sent an invalid command receipt."));
    }
}

void CrimsonMatchNetwork::rpc_world_announced(const StringName &world_id, int64_t epoch) {
    if (is_incoming_authority() && epoch > 0 && valid_world_id(world_id)) {
        emit_signal("world_announced", world_id, epoch);
    }
}

void CrimsonMatchNetwork::rpc_world_finished(int64_t epoch, bool ok, bool recovery) {
    if (is_incoming_authority() && epoch > 0 && !(ok && recovery)) {
        emit_signal("world_finished", epoch, ok, recovery);
    }
}

} // namespace crimson::integration