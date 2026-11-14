// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "session_auth.hpp"
#include "session_transport.hpp"

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>

namespace crimson::session {

class GameSession : public godot::Node {
    GDCLASS(GameSession, godot::Node)

    friend class SessionAuthRequest;

public:
    // Roles exposed to gameplay and lobby code.
    enum State {
        IDLE,       // The session owns no transport.
        OFFLINE,    // The local process is the authority.
        HOSTING,    // The local process is listening as the authority.
        CONNECTING, // The client is waiting for admission.
        CLIENT,     // The server has admitted the local client.
    };

    // Keep connection deadlines and cleanup active while gameplay is paused.
    GameSession();

    // Install an offline transport with a local authority.
    godot::Error start_offline();
    // Create a server with the selected adapter and claim the node's multiplayer scope.
    godot::Error host(int64_t port, int64_t max_clients = 32, const godot::String &bind_address = "*");
    // Install a host transport with its admission gate set before any lifecycle signal.
    godot::Error host_with_admission(int64_t port, int64_t max_clients, const godot::String &bind_address,
            bool accepting);
    // Connect to a direct host address and wait for server admission.
    godot::Error join(const godot::String &address, int64_t port, double timeout_seconds = 10.0);
    // Connect to an endpoint understood by the selected adapter and wait for admission.
    godot::Error join_endpoint(const godot::Ref<SessionEndpoint> &endpoint, double timeout_seconds = 10.0);
    // Select the adapter while idle. Null restores the default ENet implementation.
    godot::Error set_transport_adapter(const godot::Ref<SessionTransport> &adapter);
    // Select optional bilateral connection authentication while idle. Null uses the API's ordinary policy.
    godot::Error set_authenticator(const godot::Ref<SessionAuthenticator> &authenticator);
    // Return the authenticator selected for network starts, or null for the API's ordinary policy.
    godot::Ref<SessionAuthenticator> get_authenticator() const;
    // Read the verified identity of an admitted direct peer, if this session authenticated it.
    godot::Ref<SessionIdentity> get_peer_identity(int64_t peer_id) const;
    // Revoke the exact native identity while pending or admitted on this authority.
    void revoke_verified_peer(int32_t peer_id, const godot::Ref<SessionIdentity> &identity);
    // End the current session and restore the previous transport.
    void leave();

    // Report the current role while the owned transport is valid.
    State get_state() const;
    // Report whether this session owns authoritative gameplay.
    bool is_authority() const;
    // Return the admitted local peer ID, or zero while idle or connecting.
    int32_t get_local_peer_id() const;
    // Return admitted remote peers in ascending order.
    godot::PackedInt32Array get_connected_peer_ids() const;
    // Set the host's admission flag for new connections.
    godot::Error set_accepting_connections(bool enabled);
    // Read the admission flag while hosting.
    bool is_accepting_connections() const;
    // Ask the active adapter to retire an admitted peer after queued reliable messages.
    void disconnect_admitted_peer_after_flush(int32_t peer_id);

    // Configure the authority-only admission RPC.
    void _enter_tree() override;
    // Release the transport silently when the session node leaves.
    void _exit_tree() override;
    // Detect expired joins and transports replaced outside this node.
    void _process(double delta) override;

protected:
    // Expose the session API, state enum, and signals to Godot.
    static void _bind_methods();

private:
    // Accepted frames advance this sequence exactly once per pending connection.
    enum class AuthStage {
        CHALLENGE,
        CHALLENGE_RECEIVED,
        BUILDING_PROOF,
        PROOF,
        VERIFYING,
        VERIFIED,
        CONFIRMING,
        WAITING_REMOTE,
    };

    // Challenge and verification state retained only during the transport's pending admission.
    struct AuthAttempt {
        // Unique attempt token within this session node.
        uint64_t id;
        // Stage of the bilateral challenge exchange.
        AuthStage stage;
        // Local challenge answered by the remote proof.
        godot::PackedByteArray challenge;
        // Remote challenge staged until the provider constructs a response.
        godot::PackedByteArray remote_challenge;
        // Bounded remote proof staged until provider verification begins.
        godot::PackedByteArray remote_proof;
        // Validated identity staged before Godot announces the connection.
        godot::Ref<SessionIdentity> identity;
        // Godot announced the physical peer, possibly during complete_auth().
        bool engine_connected = false;
    };

    // Role of the transport currently owned by this node.
    State state = IDLE;
    // Guards against entering a new session during teardown.
    bool ending = false;
    // Guards startup while a trusted transport factory or peer virtual runs.
    bool opening = false;
    // Changes with the session so callbacks can detect reentrant replacement.
    uint64_t generation = 0;
    // Monotonic deadline for a pending authority confirmation.
    uint64_t join_deadline_ms = 0;
    // Multiplayer scope reserved by the active session.
    godot::Ref<godot::MultiplayerAPI> session_api;
    // Transport installed by this session.
    godot::Ref<godot::MultiplayerPeer> transport;
    // Optional factory selected for the next network session.
    godot::Ref<SessionTransport> configured_adapter;
    // Factory retained by the active transport until teardown completes.
    godot::Ref<SessionTransport> active_adapter;
    // Maximum simultaneous admitted remote peers for the current host.
    size_t remote_limit = 0;
    // Offline transport to restore when ownership is released.
    godot::Ref<godot::MultiplayerPeer> previous_transport;
    // Admitted remote peers tracked by the session.
    std::set<int32_t> connected_peers;
    // Exact callables used to remove the scope's signal subscriptions.
    std::array<godot::Callable, 4> callbacks;
    // Optional verifier selected for upcoming network starts.
    godot::Ref<SessionAuthenticator> configured_authenticator;
    // Scene API on which the current verifier owns authentication.
    godot::Ref<godot::SceneMultiplayer> auth_api;
    // Previously installed callback restored if Crimson still owns the auth slot.
    godot::Callable previous_auth_callback;
    // Exact installed proof callback used to detect replacement and restore ownership.
    godot::Callable auth_callback;
    // Signal handlers for the Scene API's pending connection events.
    std::array<godot::Callable, 2> auth_signals;
    // Monotonic ID for each pending auth request, including retries on the same connection ID.
    uint64_t next_auth_attempt = 0;
    // Pending challenges indexed by the engine-reported connection ID.
    std::map<int32_t, AuthAttempt> auth_attempts;
    // Verified identities of direct remotes admitted by this API.
    std::map<int32_t, godot::Ref<SessionIdentity>> identities;
    // Settles a rejected authority connection after Godot finishes its current packet callback.
    bool auth_failed = false;

    // Find the multiplayer scope and verify that the session can claim it.
    godot::Error get_available_api(godot::Ref<godot::MultiplayerAPI> &api, bool networked, const char *&detail) const;
    // Invoke the selected factory and verify peer role and scope ownership before installation.
    godot::Error open_network_transport(State role, uint64_t timeout_ms, size_t max_remote, bool accepting,
            const std::function<godot::Ref<SessionTransportResult>(SessionTransport &)> &factory);
    // Reserve the scope, bind callbacks, and publish the initial session state.
    godot::Error activate_transport(const godot::Ref<godot::MultiplayerAPI> &api,
            const godot::Ref<godot::MultiplayerPeer> &peer, State new_state, uint64_t deadline = 0,
            const godot::Ref<SessionTransport> &adapter = {}, size_t max_remote = 0);
    // Subscribe to connection and roster events on the claimed scope.
    godot::Error connect_api_signals();
    // Remove this session's callbacks before returning the scope.
    void disconnect_api_signals();
    // Verify that the node and multiplayer scope still use the installed transport.
    bool owns_current_transport() const;
    // Publish lifecycle events while checking for reentrant session replacement.
    void notify_state_started(State new_state);
    // Tear down the active role and notify observers when requested.
    void end_session(const godot::StringName &reason, bool notify = true);

    // Check that the selected verifier can claim this scope without replacing foreign authentication.
    godot::Error check_auth_scope(const godot::Ref<godot::MultiplayerAPI> &api) const;
    // Install the optional proof callback and subscribe to pending connection events.
    godot::Error connect_auth(const godot::Ref<godot::MultiplayerAPI> &api);
    // Remove exact callbacks and clear pending and verified authentication state.
    void disconnect_auth();
    // Check that this session still owns both the transport and authentication callbacks.
    bool owns_auth_scope() const;
    // Send the first challenge after Godot identifies the pending sender.
    void on_peer_authenticating(int64_t peer_id);
    // Release pending work when Godot expires or disconnects an unauthenticated peer.
    void on_peer_authentication_failed(int64_t peer_id);
    // Validate framing and route an incoming authentication packet from the actual sender.
    void on_auth_data(int64_t peer_id, const godot::PackedByteArray &data);
    // Settle the exact current verification on the Scene API after checking its attempt and generation.
    godot::Error finish_auth(const SessionAuthRequest &request,
            const godot::Ref<SessionIdentity> &identity, bool accepted);
    // Run provider callbacks and completion after Godot finishes its packet callbacks.
    void process_authentication();
    // Advance one pending proof or completed verification outside the engine packet callback.
    void advance_auth_attempt(int32_t peer_id);
    // Move verified state into the connection identity roster after Godot announces admission.
    bool admit_authenticated_peer(int32_t peer_id);
    // Remove a denied pending or just-admitted peer without publishing gameplay admission.
    void reject_auth_peer(int32_t peer_id);

    // Admit a client after a compatible confirmation from the authority.
    void rpc_confirm_admission(int64_t version);
    // Finish a join that failed at the transport level.
    void on_connection_failed();
    // Close a pending or admitted client after its server disconnects.
    void on_server_disconnected();
    // Confirm admission on the host and publish the roster addition.
    void on_peer_connected(int64_t id);
    // Remove an admitted remote peer and notify the roster.
    void on_peer_disconnected(int64_t id);
};

} // namespace crimson::session

VARIANT_ENUM_CAST(crimson::session::GameSession::State);
