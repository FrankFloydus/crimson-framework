// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace crimson::scene_load {

class CrimsonSceneLoadManager : public godot::Node {
    GDCLASS(CrimsonSceneLoadManager, godot::Node)

public:
    // Keep transition polling active while gameplay is paused.
    CrimsonSceneLoadManager();

    // Select the persistent node that owns the mounted world.
    godot::Error set_world_container(godot::Node *container);
    // Resolve the configured world container while it remains valid.
    godot::Node *get_world_container() const;
    // Map a stable scene ID to a canonical local PackedScene path.
    godot::Error register_scene(const godot::StringName &scene_id, const godot::String &scene_path);
    // Start an offline or host-led scene transition.
    godot::Error request_load(const godot::StringName &scene_id);
    // Start a transition that publishes prepared authority state before mounting the scene.
    godot::Error request_coordinated_load(const godot::StringName &scene_id,
            const godot::Callable &publish_state);
    // Abort an active transition before the commit decision.
    godot::Error cancel_load();
    // Unmount the current scene and release any loader-owned admission gate while idle.
    godot::Error clear_world();
    // Report whether preparation or peer confirmation is pending.
    bool is_loading() const;
    // Resolve the mounted world while it belongs to its container.
    godot::Node *get_current_scene() const;
    // Return the scene ID of the mounted world.
    godot::StringName get_current_scene_id() const;
    // Set the preparation and confirmation deadline for later requests.
    godot::Error set_load_timeout_msec(int64_t timeout);
    // Return the configured per-phase timeout in milliseconds.
    int64_t get_load_timeout_msec() const;
    // Choose whether the loader or an external orchestrator owns the host admission gate.
    godot::Error set_manage_admission(bool enabled);
    // Return whether the loader owns the host admission gate.
    bool get_manage_admission() const;

    // Configure scene transition RPCs and start polling.
    void _enter_tree() override;
    // Release staged worlds and admission state when the node leaves.
    void _exit_tree() override;
    // Advance the active transition while checking deadlines and membership.
    void _process(double delta) override;

protected:
    // Expose scene configuration, requests, RPCs, and signals to Godot.
    static void _bind_methods();

private:
    // Authority mode fixed for the active request.
    enum class Role { Offline, Host, Client };
    // Local progress through the preparation and activation barriers.
    enum class Phase { Preparing, Ready, CommitOrdered, Applied };

    struct Transition {
        // Authority role captured when the request starts.
        Role role = Role::Offline;
        // Current preparation or activation phase.
        Phase phase = Phase::Preparing;
        // Server-issued identifier for messages belonging to this request.
        int64_t token = 0;
        // Stable ID of the requested scene.
        godot::StringName scene_id;
        // Local resource selected for the scene ID.
        godot::String resource_path;
        // Authority-only callback that atomically publishes detached state or returns an error.
        godot::Callable publish_state;
        // Multiplayer scope captured for the request.
        godot::Ref<godot::MultiplayerAPI> network;
        // Peer resource captured for the request.
        godot::Ref<godot::MultiplayerPeer> transport;
        // Deadline duration used for both phases.
        int64_t timeout_msec = 0;
        // Monotonic deadline for the current phase.
        uint64_t deadline_msec = 0;
        // Identity of the detached staged world.
        uint64_t candidate_id = 0;
        // Records whether Godot still owns a threaded load request.
        bool resource_requested = false;
        // Records a host send failure after a commit decision.
        bool commit_send_failed = false;
        // Records that authority state was published and recovery is required on later failure.
        bool state_published = false;
        // Remote participants fixed at the start of the transition.
        std::set<int64_t> members;
        // Participants that completed preparation.
        std::set<int64_t> ready_peers;
        // Participants that confirmed local activation.
        std::set<int64_t> applied_peers;
    };

    // Active transition, cleared before its terminal signal.
    std::optional<Transition> pending;
    // Registered scene IDs and their canonical resource paths.
    godot::Dictionary scenes;
    // Instance ID of the persistent mount.
    uint64_t container_id = 0;
    // Instance ID of the mounted managed world.
    uint64_t world_id = 0;
    // Stable scene ID associated with the current mount.
    godot::StringName current_scene_id;
    // Last transition token allocated by this authority.
    int64_t next_token = 0;
    // Newest server token accepted by this client.
    int64_t last_server_token = 0;
    // Transport identity associated with the last server token.
    uint64_t last_server_transport_id = 0;
    // Default deadline for future preparation and activation phases.
    int64_t load_timeout_msec = 15000;
    // Prevents requests while the persistent manager is being removed.
    bool leaving_tree = false;
    // Guards the transition during state publication and scene-tree callbacks.
    bool publishing_world = false;
    // Canceled resource requests that still need to be collected.
    std::vector<godot::String> abandoned_loads;

    // Host transport whose admission setting is currently reserved.
    godot::Ref<godot::MultiplayerPeer> gated_transport;
    // Whether this manager owns admission for its host transitions.
    bool manage_admission = true;
    // Admission setting to restore when the gate is released.
    bool restore_refuse_connections = false;
    // Keeps admission closed while a managed network world is active.
    bool gate_has_committed_world = false;
    // Keeps admission closed until a failed committed transition is recovered.
    bool gate_requires_recovery = false;

    // Check that the configured mount is valid and reserved for the managed world.
    bool slot_is_available();
    // Resolve the detached candidate while it can still be mounted.
    godot::Node *get_staged_scene() const;
    // Admit a local request, optionally with an authority state publication callback.
    godot::Error begin_load(const godot::StringName &scene_id, const godot::Callable &publish_state);
    // Verify that the transition still has its original scope, transport, and peer set.
    bool transport_matches(const Transition &transition) const;
    // Close admission for the host transport and remember its previous setting.
    void acquire_admission_gate(const godot::Ref<godot::MultiplayerPeer> &transport);
    // Restore the host transport's previous admission setting.
    void release_admission_gate();
    // Release stale admission and authority state when the world or transport changes.
    void reconcile_admission_gate();
    // Collect completed threaded requests for canceled candidates.
    void poll_abandoned_loads();
    // Load and instantiate the requested scene before announcing readiness.
    void prepare_world();
    // Record and broadcast the host's decision to activate the prepared scene.
    void order_commit();
    // Replace the mounted scene and restore the prior mount if local attachment fails.
    bool publish_world();
    // Publish locally and notify the host when activation finishes.
    void apply_commit();
    // Confirm completion after all required peers acknowledge activation.
    void finish_host_barrier();
    // Clear the active request and emit successful completion.
    void finish_success();
    // Discard the candidate and report preparation or synchronization failure.
    void finish_failure(godot::Error error, const godot::String &message, bool notify_remote = true);
    // Free an unpublished world and track outstanding resource work for cleanup.
    void discard_candidate(Transition &transition);

    // Accept scene instructions from the admitted server.
    godot::Ref<godot::MultiplayerAPI> incoming_server() const;
    // Resolve an admitted participant reporting on the current transition.
    int64_t incoming_client(int64_t token) const;
    // Begin a client preparation using the server's scene ID and token.
    void rpc_prepare(int64_t token, const godot::StringName &scene_id, int64_t timeout);
    // Record a peer's preparation result on the host.
    void rpc_ready(int64_t token, int64_t error);
    // Advance a prepared client after the host's commit decision.
    void rpc_commit(int64_t token);
    // Record a client's local activation result on the host.
    void rpc_applied(int64_t token, int64_t error);
    // Finish the client transition after the host confirms the barrier.
    void rpc_finalize(int64_t token);
    // Discard or recover a client transition after a host failure.
    void rpc_abort(int64_t token, int64_t error, bool after_commit);
};

} // namespace crimson::scene_load
