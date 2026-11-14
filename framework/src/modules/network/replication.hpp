// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstdint>
#include <map>

namespace crimson::replication {

// Delivers an explicit authority-provided view to each admitted multiplayer peer.
class StateReplication : public godot::Node {
    GDCLASS(StateReplication, godot::Node)

public:
    // Continue checking synchronization deadlines while gameplay is paused.
    StateReplication();

    // Set the authority's synchronous recipient-specific projection callback.
    godot::Error set_projector(const godot::Callable &callback);
    // Begin an authority world epoch and invalidate every registered peer stream.
    godot::Error advance_world(int64_t world_epoch, int64_t committed_revision);
    // Register a connected, admitted remote peer with its current player binding.
    godot::Error register_peer(int64_t peer_id, const godot::String &player_id, int64_t binding_generation);
    // Start a fresh baseline for a peer whose destination scene is active.
    godot::Error send_baseline(int64_t peer_id);
    // Deliver changed records and removals from an authority commit to ready peers.
    godot::Error publish_changes(int64_t committed_revision);
    // Remove a peer when its registered binding matches, or remove it unconditionally with zero.
    void remove_peer(int64_t peer_id, int64_t binding_generation = 0);
    // Check whether the current stream acknowledged its baseline for this binding.
    bool is_peer_ready(int64_t peer_id, int64_t binding_generation = 0) const;

    // Check whether the client has an active, continuous stream from the server.
    bool is_client_ready() const;
    // Read a detached copy of the client's current filtered view.
    godot::Dictionary get_projection() const;
    // Invalidate the client's visible view during scene transition while keeping its stream identity.
    void clear_projection();
    // Return the active authority or received client epoch, or zero before initialization.
    int64_t get_world_epoch() const;
    // Return the authority revision or the revision last delivered to this client.
    int64_t get_revision() const;
    // Drop all streams and views while retaining the configured projector.
    void reset();

    // Register authority and client RPC modes on this persistent node.
    void _enter_tree() override;
    // Release active streams when the node leaves its multiplayer scope.
    void _exit_tree() override;
    // Reconcile transport changes, remote disconnects, and baseline deadlines.
    void _process(double delta) override;

protected:
    // Expose the stream API, RPC endpoints, and readiness notifications to Godot.
    static void _bind_methods();

private:
    // Identity and last delivered view for one registered authority-side peer.
    struct PeerStream {
        // Stable admitted player identity used only by the authority's projector.
        godot::String player_id;
        // Host-issued generation of this player's current connection binding.
        int64_t generation = 0;
        // Authority-issued identifier of the baseline governing the stream.
        int64_t baseline = 0;
        // Number of visible deltas sent after the active baseline.
        int64_t sequence = 0;
        // Authority revision used to produce the last delivered view.
        int64_t revision = 0;
        // Monotonic expiry time while waiting for an acknowledgment, or zero.
        uint64_t deadline = 0;
        // True after the matching acknowledgment and initial catch-up succeed.
        bool ready = false;
        // Latest detached view delivered to this peer.
        godot::Dictionary view;
    };

    // Identity and last published view for the local client stream.
    struct ClientStream {
        // Binding generation supplied in the authenticated server baseline.
        int64_t generation = 0;
        // Most recent accepted baseline identifier on this transport.
        int64_t baseline = 0;
        // Most recent accepted delta sequence for the baseline.
        int64_t sequence = 0;
        // World epoch of the last accepted baseline.
        int64_t epoch = 0;
        // Authority revision represented by the current visible view.
        int64_t revision = 0;
        // True while the client has a usable, gap-free view.
        bool ready = false;
        // Latest detached projected records visible to the client.
        godot::Dictionary view;
    };

    // Scope in which this node currently exchanges replication RPCs.
    godot::Ref<godot::MultiplayerAPI> network;
    // Transport retained to detect replacement and reject an old stream immediately.
    godot::Ref<godot::MultiplayerPeer> transport;
    // Game-supplied per-player visibility and projection policy.
    godot::Callable projector;
    // Admitted authority-side streams indexed by transport peer ID.
    std::map<int64_t, PeerStream> peers;
    // Current local client projection and stream cursor.
    ClientStream client;
    // Authority's active world epoch.
    int64_t epoch = 0;
    // Latest authority revision made available for projection.
    int64_t revision = 0;
    // Last baseline token allocated by the authority across local resets.
    int64_t last_baseline = 0;
    // Highest baseline accepted from the server on the current transport.
    int64_t last_server_baseline = 0;
    // Suppress repeated baseline errors until a baseline succeeds or the stream resets.
    bool baseline_rejection_reported = false;
    // Advances when teardown invalidates references held across a callback.
    uint64_t state_generation = 0;
    // Rejects mutations from user code while a projector callback is active.
    bool projecting = false;
    // Suppresses RPCs and state changes during tree teardown.
    bool leaving_tree = false;

    // Reconcile the MultiplayerAPI and peer resource with the persistent node's scope.
    void refresh_transport();
    // Clear streams after transport changes and optionally notify view consumers.
    void clear_state(bool notify);
    // Verify that the remembered multiplayer peer is still connected and installed.
    bool transport_is_current() const;
    // Verify that this node owns authority on its current multiplayer scope.
    bool is_authority() const;
    // Check whether a peer is still in Godot's connected peer list.
    bool peer_is_connected(int64_t peer_id) const;
    // Resolve the actual remote sender for a client-to-authority message.
    int64_t incoming_client() const;
    // Verify that a client received a message from its connected server.
    bool incoming_server() const;
    // Produce a bounded view and optionally report the reason a projector rejected it.
    godot::Error project(int64_t peer_id, const PeerStream &peer, godot::Dictionary &view, bool report_failure = true);
    // Send a recipient's delta and update the cursor, suppressing repeated projector errors when requested.
    godot::Error send_changes(int64_t peer_id, PeerStream &peer, bool report_failure = true);
    // Explain an invalid delta, request a fresh baseline, and invalidate the old view.
    void request_resync(const char *reason);

    // Validate and apply an authority baseline, then acknowledge its exact identity.
    void rpc_baseline(int64_t world_epoch, int64_t generation, int64_t baseline, int64_t committed_revision,
            const godot::Dictionary &view);
    // Validate and apply one continuous delta from the authenticated server.
    void rpc_delta(int64_t world_epoch, int64_t generation, int64_t baseline, int64_t sequence,
            int64_t committed_revision, const godot::Dictionary &changed, const godot::PackedStringArray &removed);
    // Accept an acknowledgment only from the connection that received the current baseline.
    void rpc_ack(int64_t world_epoch, int64_t generation, int64_t baseline);
    // Reopen a baseline only for the connected sender's current acknowledged stream.
    void rpc_resync(int64_t world_epoch, int64_t generation, int64_t baseline);
};

} // namespace crimson::replication
