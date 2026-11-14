// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "game_session.hpp"

#include <godot_cpp/classes/offline_multiplayer_peer.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <limits>
#include <unordered_set>

using namespace godot;

namespace crimson::session {
namespace {

constexpr int64_t max_port = 65535;
constexpr int64_t max_remote_clients = 4095;
constexpr double max_join_timeout = 120.0;
constexpr int32_t server_id = 1;
// Protocol 4 adds a separate body yaw to movement state and an optional lock heading to intent.
constexpr int64_t session_protocol_version = 4;
constexpr const char *admission_method = "_crimson_session_confirm_admission";

constexpr std::array<const char *, 4> signal_names = {
    "connection_failed", "server_disconnected", "peer_connected", "peer_disconnected"
};

// Accessed by session Nodes on the scene tree's main thread. Reserve the API even in offline mode.
std::unordered_set<uint64_t> owned_apis;
// Prevent an adapter from transferring a peer already installed by another Crimson session.
std::unordered_set<uint64_t> owned_peers;

// Validate a port before creating an ENet peer.
bool valid_port(int64_t port) {
    return port >= 1 && port <= max_port;
}

// Print the local startup failure without exposing endpoint or authentication data.
Error report_start_failure(GameSession::State role, Error error, const char *detail) {
    const char *action = role == GameSession::HOSTING ? "Could not host the session" :
            role == GameSession::CONNECTING ? "Could not join the session" : "Could not start the offline session";
    UtilityFunctions::printerr("[Crimson] ", action, " (", UtilityFunctions::error_string(error), "). ", detail);
    return error;
}

// Explain a terminal session reason while keeping its signal code stable.
const char *describe_session_end(const StringName &reason) {
    if (reason == StringName("transport_replaced")) {
        return "The installed multiplayer API or transport was replaced. Start a new session on the current scope.";
    }
    if (reason == StringName("authentication_replaced")) {
        return "The session's authentication callback was replaced. Restore the expected multiplayer authentication setup before reconnecting.";
    }
    if (reason == StringName("authentication_failed")) {
        return "Authentication did not complete. Check the connection's authentication setup before retrying.";
    }
    if (reason == StringName("connection_timeout")) {
        return "The host did not confirm admission before the connection deadline. Check host availability and retry.";
    }
    if (reason == StringName("protocol_mismatch")) {
        return "The host uses an incompatible admission protocol. Use matching Crimson builds.";
    }
    if (reason == StringName("server_disconnected")) {
        return "The host connection ended. Join a running host to continue.";
    }
    if (reason == StringName("transport_disconnected")) {
        return "The session transport disconnected. Open a new session to continue.";
    }
    if (reason == StringName("connection_failed")) {
        return "The connection ended before the host confirmed admission. Check host availability and admission settings.";
    }
    return "The session ended unexpectedly. Check the reported reason before retrying.";
}

} // namespace

GameSession::GameSession() {
    set_process_mode(PROCESS_MODE_ALWAYS);
}

void GameSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start_offline"), &GameSession::start_offline);
    ClassDB::bind_method(D_METHOD("host", "port", "max_clients", "bind_address"), &GameSession::host,
            DEFVAL(32), DEFVAL(String("*")));
    ClassDB::bind_method(D_METHOD("join", "address", "port", "timeout_seconds"), &GameSession::join,
            DEFVAL(10.0));
    ClassDB::bind_method(D_METHOD("join_endpoint", "endpoint", "timeout_seconds"), &GameSession::join_endpoint,
            DEFVAL(10.0));
    ClassDB::bind_method(D_METHOD("set_transport_adapter", "adapter"), &GameSession::set_transport_adapter);
    ClassDB::bind_method(D_METHOD("set_authenticator", "authenticator"), &GameSession::set_authenticator);
    ClassDB::bind_method(D_METHOD("get_authenticator"), &GameSession::get_authenticator);
    ClassDB::bind_method(D_METHOD("get_peer_identity", "peer_id"), &GameSession::get_peer_identity);
    ClassDB::bind_method(D_METHOD("leave"), &GameSession::leave);
    ClassDB::bind_method(D_METHOD("get_state"), &GameSession::get_state);
    ClassDB::bind_method(D_METHOD("is_authority"), &GameSession::is_authority);
    ClassDB::bind_method(D_METHOD("get_local_peer_id"), &GameSession::get_local_peer_id);
    ClassDB::bind_method(D_METHOD("get_connected_peer_ids"), &GameSession::get_connected_peer_ids);
    ClassDB::bind_method(D_METHOD("set_accepting_connections", "enabled"), &GameSession::set_accepting_connections);
    ClassDB::bind_method(D_METHOD("is_accepting_connections"), &GameSession::is_accepting_connections);
    ClassDB::bind_method(D_METHOD(admission_method, "version"), &GameSession::rpc_confirm_admission);

    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("session_started", PropertyInfo(Variant::INT, "state"),
            PropertyInfo(Variant::INT, "local_peer_id")));
    ADD_SIGNAL(MethodInfo("session_ended", PropertyInfo(Variant::STRING_NAME, "reason")));
    ADD_SIGNAL(MethodInfo("peer_joined", PropertyInfo(Variant::INT, "peer_id")));
    ADD_SIGNAL(MethodInfo("peer_left", PropertyInfo(Variant::INT, "peer_id")));

    BIND_ENUM_CONSTANT(IDLE);
    BIND_ENUM_CONSTANT(OFFLINE);
    BIND_ENUM_CONSTANT(HOSTING);
    BIND_ENUM_CONSTANT(CONNECTING);
    BIND_ENUM_CONSTANT(CLIENT);
}

Error GameSession::get_available_api(Ref<MultiplayerAPI> &api, bool networked, const char *&detail) const {
    if (!is_inside_tree()) {
        detail = "Add the session node to the scene tree before opening it.";
        return ERR_UNCONFIGURED;
    }
    if (state != IDLE || ending || opening) {
        detail = "Wait for the current session to finish or leave it before starting another.";
        return ERR_BUSY;
    }
    api = get_multiplayer();
    if (api.is_null()) {
        detail = "Assign a MultiplayerAPI to the session's scene tree scope.";
        return ERR_UNCONFIGURED;
    }
    const Ref<MultiplayerPeer> existing = api->get_multiplayer_peer();
    if (owned_apis.count(api->get_instance_id()) ||
            (existing.is_valid() && Object::cast_to<OfflineMultiplayerPeer>(existing.ptr()) == nullptr)) {
        detail = "Use an idle multiplayer scope without another active session or transport.";
        return ERR_ALREADY_IN_USE;
    }
    const Ref<SceneMultiplayer> scene_api = api;
    if (networked && scene_api.is_valid() && scene_api->is_object_decoding_allowed()) {
        detail = "Disable multiplayer object decoding before opening a network session.";
        return ERR_UNAUTHORIZED;
    }
    if (!networked) {
        return OK;
    }
    const Error authentication = check_auth_scope(api);
    if (authentication == ERR_UNAVAILABLE) {
        detail = "The configured authenticator requires SceneMultiplayer.";
    } else if (authentication == ERR_ALREADY_IN_USE) {
        detail = "The multiplayer scope already has an authentication callback.";
    } else if (authentication == ERR_INVALID_PARAMETER) {
        detail = "Configure a finite, positive SceneMultiplayer authentication timeout within Crimson's limit.";
    }
    return authentication;
}

Error GameSession::start_offline() {
    Ref<MultiplayerAPI> api;
    const char *detail = "The multiplayer scope is unavailable.";
    const Error available = get_available_api(api, false, detail);
    if (available != OK) {
        return report_start_failure(OFFLINE, available, detail);
    }
    Ref<OfflineMultiplayerPeer> peer;
    peer.instantiate();
    return activate_transport(api, peer, OFFLINE);
}

Error GameSession::host(int64_t port, int64_t max_clients, const String &bind_address) {
    return host_with_admission(port, max_clients, bind_address, true);
}

Error GameSession::host_with_admission(int64_t port, int64_t max_clients, const String &bind_address,
        bool accepting) {
    if (!valid_port(port)) {
        return report_start_failure(HOSTING, ERR_INVALID_PARAMETER, "Use a port from 1 to 65535.");
    }
    if (max_clients < 1 || max_clients > max_remote_clients) {
        return report_start_failure(HOSTING, ERR_INVALID_PARAMETER, "Use a remote player limit from 1 to 4095.");
    }
    if (bind_address != "*" && !bind_address.is_valid_ip_address()) {
        return report_start_failure(HOSTING, ERR_INVALID_PARAMETER, "Use a local IP address or '*' as the bind address.");
    }
    return open_network_transport(HOSTING, 0, static_cast<size_t>(max_clients), accepting,
            [&](SessionTransport &adapter) { return adapter.open_server(port, max_clients, bind_address); });
}

Error GameSession::join(const String &address, int64_t port, double timeout_seconds) {
    const auto endpoint = SessionEndpoint::direct(address, port);
    if (endpoint.is_null()) {
        return report_start_failure(CONNECTING, ERR_INVALID_PARAMETER, "Provide a host address and a port from 1 to 65535.");
    }
    return join_endpoint(endpoint, timeout_seconds);
}

Error GameSession::join_endpoint(const Ref<SessionEndpoint> &endpoint, double timeout_seconds) {
    if (endpoint.is_null()) {
        return report_start_failure(CONNECTING, ERR_INVALID_PARAMETER, "Provide a valid session endpoint.");
    }
    if (!std::isfinite(timeout_seconds) || timeout_seconds <= 0.0 || timeout_seconds > max_join_timeout) {
        return report_start_failure(CONNECTING, ERR_INVALID_PARAMETER, "Use a finite connection timeout greater than 0 and at most 120 seconds.");
    }
    const auto timeout_ms = static_cast<uint64_t>(std::ceil(timeout_seconds * 1000.0));
    return open_network_transport(CONNECTING, timeout_ms, 0, false,
            [&](SessionTransport &adapter) { return adapter.open_client(endpoint); });
}

Error GameSession::set_transport_adapter(const Ref<SessionTransport> &adapter) {
    if (state != IDLE || ending || opening) {
        return ERR_BUSY;
    }
    configured_adapter = adapter;
    return OK;
}

Error GameSession::open_network_transport(State role, uint64_t timeout_ms, size_t max_remote, bool accepting,
        const std::function<Ref<SessionTransportResult>(SessionTransport &)> &factory) {
    Ref<MultiplayerAPI> api;
    const char *detail = "The multiplayer scope is unavailable.";
    const Error available = get_available_api(api, true, detail);
    if (available != OK) {
        return report_start_failure(role, available, detail);
    }

    const Ref<MultiplayerPeer> prior = api->get_multiplayer_peer();
    const uint64_t api_id = api->get_instance_id();
    if (!owned_apis.insert(api_id).second) {
        return report_start_failure(role, ERR_ALREADY_IN_USE, "Another session owns this multiplayer scope.");
    }
    opening = true;
    const uint64_t opening_generation = generation;
    Ref<SessionTransport> adapter = configured_adapter;
    if (adapter.is_null()) {
        Ref<ENetSessionTransport> enet;
        enet.instantiate();
        adapter = enet;
    }
    const Ref<SessionTransportResult> result = factory(**adapter);
    const Ref<MultiplayerPeer> peer = result.is_valid() ? result->get_peer() : Ref<MultiplayerPeer>();
    Error error = result.is_valid() ? result->get_error() : ERR_CANT_CREATE;
    detail = result.is_valid() ? "The transport adapter could not open the requested peer." :
                                 "The transport adapter did not return an opening result.";
    const bool fresh = peer.is_valid() && peer != prior && peer != api->get_multiplayer_peer() &&
            !owned_peers.count(peer->get_instance_id());
    if (error == OK) {
        if (!fresh) {
            error = ERR_ALREADY_IN_USE;
            detail = "The transport adapter must return a fresh peer that no multiplayer scope owns.";
        } else if (Object::cast_to<OfflineMultiplayerPeer>(peer.ptr()) != nullptr ||
                peer->get_connection_status() == MultiplayerPeer::CONNECTION_DISCONNECTED) {
            error = ERR_INVALID_DATA;
            detail = "The transport adapter returned an offline or disconnected peer for a network session.";
        } else if (role == HOSTING) {
            peer->set_refuse_new_connections(!accepting);
            if (peer->get_connection_status() != MultiplayerPeer::CONNECTION_CONNECTED ||
                    peer->get_unique_id() != server_id || peer->is_refusing_new_connections() != !accepting) {
                error = ERR_INVALID_DATA;
                detail = "The transport adapter must return a connected authority peer with the requested admission setting.";
            }
        } else if (peer->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED &&
                peer->get_unique_id() <= server_id) {
            error = ERR_INVALID_DATA;
            detail = "The transport adapter returned an invalid client peer identity.";
        }
    }

    const Ref<SceneMultiplayer> scene_api = api;
    if (generation != opening_generation || !is_inside_tree() || get_multiplayer() != api ||
            api->get_multiplayer_peer() != prior) {
        error = ERR_BUSY;
        detail = "The multiplayer scope or session changed while the transport adapter was opening.";
    } else if (scene_api.is_valid() && scene_api->is_object_decoding_allowed()) {
        error = ERR_UNAUTHORIZED;
        detail = "Multiplayer object decoding became enabled while the transport adapter was opening.";
    }
    if (error != OK && fresh && peer != api->get_multiplayer_peer() && !owned_peers.count(peer->get_instance_id())) {
        peer->close();
    }
    owned_apis.erase(api_id);
    opening = false;
    if (error != OK) {
        return report_start_failure(role, error, detail);
    }
    const uint64_t deadline = timeout_ms > 0 ? Time::get_singleton()->get_ticks_msec() + timeout_ms : 0;
    return activate_transport(api, peer, role, deadline, adapter, max_remote);
}

Error GameSession::activate_transport(const Ref<MultiplayerAPI> &api,
        const Ref<MultiplayerPeer> &peer, State new_state, uint64_t deadline,
        const Ref<SessionTransport> &adapter, size_t max_remote) {
    const auto api_id = api->get_instance_id();
    if (!owned_apis.insert(api_id).second) {
        if (new_state != OFFLINE) {
            peer->close();
        }
        return report_start_failure(new_state, ERR_ALREADY_IN_USE, "Another session claimed the multiplayer scope before activation.");
    }
    session_api = api;
    previous_transport = api->get_multiplayer_peer();
    transport = peer;
    owned_peers.insert(peer->get_instance_id());
    active_adapter = adapter;
    remote_limit = max_remote;
    state = new_state;
    join_deadline_ms = deadline;
    ++generation;

    const Error connected = connect_api_signals();
    if (connected != OK) {
        end_session("connection_failed", false);
        return report_start_failure(new_state, connected, "The session could not subscribe to multiplayer lifecycle signals.");
    }
    if (new_state != OFFLINE) {
        const Error authentication = connect_auth(api);
        if (authentication != OK) {
            end_session("authentication_failed", false);
            return report_start_failure(new_state, authentication, "The session could not install its authentication callbacks.");
        }
    }
    api->set_multiplayer_peer(peer);
    if (!owns_current_transport()) {
        end_session("transport_replaced", false);
        return report_start_failure(new_state, ERR_CANT_CREATE, "The multiplayer scope did not retain the newly installed peer.");
    }
    set_process(true);
    notify_state_started(new_state);
    return OK;
}

Error GameSession::connect_api_signals() {
    callbacks = {
        callable_mp(this, &GameSession::on_connection_failed),
        callable_mp(this, &GameSession::on_server_disconnected),
        callable_mp(this, &GameSession::on_peer_connected),
        callable_mp(this, &GameSession::on_peer_disconnected),
    };
    for (size_t i = 0; i < callbacks.size(); ++i) {
        const Error error = session_api->connect(signal_names[i], callbacks[i]);
        if (error != OK) {
            return error;
        }
    }
    return OK;
}

void GameSession::disconnect_api_signals() {
    if (session_api.is_valid()) {
        for (size_t i = 0; i < callbacks.size(); ++i) {
            if (session_api->is_connected(signal_names[i], callbacks[i])) {
                session_api->disconnect(signal_names[i], callbacks[i]);
            }
        }
    }
    callbacks = {};
}

bool GameSession::owns_current_transport() const {
    return is_inside_tree() && session_api.is_valid() && transport.is_valid() &&
            get_multiplayer() == session_api && session_api->get_multiplayer_peer() == transport;
}

GameSession::State GameSession::get_state() const {
    if (state == IDLE || !owns_current_transport() ||
            transport->get_connection_status() == MultiplayerPeer::CONNECTION_DISCONNECTED) {
        return IDLE;
    }
    return state;
}

bool GameSession::is_authority() const {
    const State current = get_state();
    return current == OFFLINE || current == HOSTING;
}

int32_t GameSession::get_local_peer_id() const {
    const State current = get_state();
    if (current == OFFLINE || current == HOSTING) {
        return server_id;
    }
    return current == CLIENT ? session_api->get_unique_id() : 0;
}

PackedInt32Array GameSession::get_connected_peer_ids() const {
    PackedInt32Array ids;
    if (get_state() != IDLE) {
        for (int32_t id : connected_peers) {
            ids.append(id);
        }
    }
    return ids;
}

Error GameSession::set_accepting_connections(bool enabled) {
    if (get_state() != HOSTING) {
        return ERR_UNAUTHORIZED;
    }
    const Ref<MultiplayerPeer> peer = transport;
    const uint64_t active_generation = generation;
    peer->set_refuse_new_connections(!enabled);
    if (generation != active_generation || !owns_current_transport()) {
        return ERR_BUSY;
    }
    return peer->is_refusing_new_connections() == !enabled ? OK : ERR_UNAVAILABLE;
}

bool GameSession::is_accepting_connections() const {
    return get_state() == HOSTING && !transport->is_refusing_new_connections();
}

void GameSession::disconnect_admitted_peer_after_flush(int32_t peer_id) {
    if (get_state() != HOSTING || connected_peers.count(peer_id) == 0 || active_adapter.is_null()) {
        return;
    }
    const Ref<SessionTransport> adapter = active_adapter;
    const Ref<MultiplayerPeer> peer = transport;
    adapter->disconnect_after_flush(peer, peer_id);
}

void GameSession::_process(double) {
    if (state == IDLE) {
        return;
    }
    if (!owns_current_transport()) {
        end_session("transport_replaced");
        return;
    }
    if (auth_api.is_valid()) {
        if (!owns_auth_scope()) {
            end_session("authentication_replaced");
            return;
        }
        const uint64_t active_generation = generation;
        if (!auth_failed) {
            process_authentication();
        }
        if (generation != active_generation) {
            return;
        }
        if (auth_failed) {
            end_session("authentication_failed");
            return;
        }
    }
    if (transport->get_connection_status() == MultiplayerPeer::CONNECTION_DISCONNECTED) {
        const StringName reason = state == CONNECTING ? "connection_failed" :
                state == CLIENT ? "server_disconnected" : "transport_disconnected";
        end_session(reason);
        return;
    }
    if (state == CONNECTING && Time::get_singleton()->get_ticks_msec() >= join_deadline_ms) {
        end_session("connection_timeout");
    }
}

void GameSession::_enter_tree() {
    set_multiplayer_authority(server_id, false);
    Dictionary config;
    config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
    config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
    config["call_local"] = false;
    config["channel"] = 0;
    rpc_config(admission_method, config);
}

void GameSession::_exit_tree() {
    if (opening) {
        ++generation;
    }
    end_session("left", false);
}

void GameSession::leave() {
    if (opening) {
        ++generation;
    }
    end_session("left");
}

void GameSession::notify_state_started(State new_state) {
    const auto started_generation = generation;
    emit_signal("state_changed", new_state);
    if (new_state != CONNECTING && generation == started_generation && get_state() == new_state) {
        emit_signal("session_started", new_state, get_local_peer_id());
    }
}

void GameSession::end_session(const StringName &reason, bool notify) {
    if (state == IDLE || ending) {
        return;
    }
    ending = true;
    const State old_state = state;
    state = IDLE;
    ++generation;
    join_deadline_ms = 0;
    remote_limit = 0;
    connected_peers.clear();
    set_process(false);

    disconnect_auth();
    disconnect_api_signals();
    if (session_api.is_valid()) {
        if (session_api->get_multiplayer_peer() == transport) {
            session_api->set_multiplayer_peer(previous_transport);
        }
        owned_apis.erase(session_api->get_instance_id());
    }
    if (transport.is_valid()) {
        if (old_state != OFFLINE) {
            transport->close();
        }
        owned_peers.erase(transport->get_instance_id());
    }
    transport.unref();
    active_adapter.unref();
    previous_transport.unref();
    session_api.unref();

    if (notify && reason != StringName("left")) {
        UtilityFunctions::printerr("[Crimson] Session ended (", reason, "). ", describe_session_end(reason));
    }
    if (notify) {
        emit_signal("state_changed", IDLE);
    }
    ending = false;
    if (notify) {
        emit_signal("session_ended", reason);
    }
}

void GameSession::rpc_confirm_admission(int64_t version) {
    if (state != CONNECTING || !owns_current_transport() ||
            session_api->get_remote_sender_id() != server_id ||
            transport->get_connection_status() != MultiplayerPeer::CONNECTION_CONNECTED) {
        return;
    }
    if (auth_api.is_valid() && (!owns_auth_scope() || auth_failed || !identities.count(server_id))) {
        auth_failed = true;
        return;
    }
    if (version != session_protocol_version) {
        end_session("protocol_mismatch");
        return;
    }
    if (session_api->get_unique_id() <= server_id) {
        end_session("connection_failed");
        return;
    }
    state = CLIENT;
    join_deadline_ms = 0;
    const auto connected_generation = generation;
    notify_state_started(CLIENT);
    if (generation != connected_generation || get_state() != CLIENT) {
        return;
    }
    const PackedInt32Array initial_peers = session_api->get_peers();
    for (int32_t id : initial_peers) {
        if (generation != connected_generation) {
            return;
        }
        on_peer_connected(id);
    }
}

void GameSession::on_connection_failed() {
    if (state == CONNECTING && owns_current_transport()) {
        end_session(auth_failed || auth_attempts.count(server_id) ? "authentication_failed" : "connection_failed");
    }
}

void GameSession::on_server_disconnected() {
    if ((state == CLIENT || state == CONNECTING) && owns_current_transport()) {
        const StringName reason = state == CLIENT ? "server_disconnected" :
                auth_failed || auth_attempts.count(server_id) ? "authentication_failed" : "connection_failed";
        end_session(reason);
    }
}

void GameSession::on_peer_connected(int64_t id) {
    const State current = get_state();
    if (id <= 0 || id > std::numeric_limits<int32_t>::max() || id == get_local_peer_id() ||
            connected_peers.count(static_cast<int32_t>(id))) {
        return;
    }
    const auto peer_id = static_cast<int32_t>(id);
    if (auth_api.is_valid() && (current == HOSTING || (current == CONNECTING && peer_id == server_id)) &&
            !identities.count(peer_id)) {
        auto pending = auth_attempts.find(peer_id);
        if (!owns_auth_scope() || pending == auth_attempts.end() || (pending->second.stage != AuthStage::CONFIRMING &&
                pending->second.stage != AuthStage::WAITING_REMOTE)) {
            reject_auth_peer(peer_id);
        } else {
            pending->second.engine_connected = true;
            if (current == CONNECTING && pending->second.stage == AuthStage::WAITING_REMOTE) {
                // The authority can send its admission RPC in the same Godot poll.
                admit_authenticated_peer(peer_id);
            }
        }
        return;
    }
    if (current != HOSTING && current != CLIENT) {
        return;
    }
    if (current == HOSTING) {
        // ENet can report CONNECT on the client even when the server refuses its admission.
        const Ref<MultiplayerPeer> peer = transport;
        const uint64_t active_generation = generation;
        const bool eligible = !peer->is_refusing_new_connections() && connected_peers.size() < remote_limit;
        const Error confirmation = eligible ? rpc_id(id, admission_method, session_protocol_version) : OK;
        if (generation != active_generation || !owns_current_transport()) {
            return;
        }
        if (!eligible || confirmation != OK) {
            if (confirmation != OK) {
                UtilityFunctions::printerr("[Crimson] Could not confirm multiplayer admission (",
                        UtilityFunctions::error_string(confirmation), "). The peer was disconnected.");
            }
            identities.erase(peer_id);
            peer->disconnect_peer(peer_id);
            return;
        }
    }
    connected_peers.insert(peer_id);
    emit_signal("peer_joined", id);
}

void GameSession::on_peer_disconnected(int64_t id) {
    if (!owns_current_transport() || id <= 0 || id > std::numeric_limits<int32_t>::max()) {
        return;
    }
    const auto peer_id = static_cast<int32_t>(id);
    auth_attempts.erase(peer_id);
    identities.erase(peer_id);
    if (connected_peers.erase(peer_id)) {
        emit_signal("peer_left", id);
    }
}

} // namespace crimson::session
