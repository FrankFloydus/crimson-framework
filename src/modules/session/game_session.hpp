// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <array>
#include <cstdint>
#include <set>

namespace crimson::session {

class CrimsonGameSession : public godot::Node {
    GDCLASS(CrimsonGameSession, godot::Node)

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
    CrimsonGameSession();

    // Install an offline transport with a local authority.
    godot::Error start_offline();
    // Create the ENet server and claim the node's multiplayer scope.
    godot::Error host(int64_t port, int64_t max_clients = 32, const godot::String &bind_address = "*");
    // Connect to an ENet host and wait for server admission.
    godot::Error join(const godot::String &address, int64_t port, double timeout_seconds = 10.0);
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
    // Role of the transport currently owned by this node.
    State state = IDLE;
    // Guards against entering a new session during teardown.
    bool ending = false;
    // Changes with the session so callbacks can detect reentrant replacement.
    uint64_t generation = 0;
    // Monotonic deadline for a pending authority confirmation.
    uint64_t join_deadline_ms = 0;
    // Multiplayer scope reserved by the active session.
    godot::Ref<godot::MultiplayerAPI> session_api;
    // Transport installed by this session.
    godot::Ref<godot::MultiplayerPeer> transport;
    // Offline transport to restore when ownership is released.
    godot::Ref<godot::MultiplayerPeer> previous_transport;
    // Admitted remote peers tracked by the session.
    std::set<int32_t> connected_peers;
    // Exact callables used to remove the scope's signal subscriptions.
    std::array<godot::Callable, 4> callbacks;

    // Find the multiplayer scope and verify that the session can claim it.
    godot::Error get_available_api(godot::Ref<godot::MultiplayerAPI> &api, bool networked) const;
    // Reserve the scope, bind callbacks, and publish the initial session state.
    godot::Error activate_transport(const godot::Ref<godot::MultiplayerAPI> &api,
            const godot::Ref<godot::MultiplayerPeer> &peer, State new_state, uint64_t deadline = 0);
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

VARIANT_ENUM_CAST(crimson::session::CrimsonGameSession::State);
