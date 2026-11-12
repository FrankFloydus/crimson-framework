// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/world_session/world_session_definition.hpp"
#include "modules/scene_load/scene_load_manager.hpp"
#include "modules/session/game_session.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <cstdint>
#include <map>
#include <vector>

namespace crimson::integration {

// Mount one shared, locally packaged world and assign player scenes to authority-owned slots.
class CrimsonWorldSession : public godot::Node {
    GDCLASS(CrimsonWorldSession, godot::Node)

public:
    // Maximum number of local and remote participants supported by the fixed-world backend.
    static constexpr int64_t MAX_PLAYERS = 64;

    // Readiness of the world and the local player's assigned spawn.
    enum State {
        IDLE,     // There is no active world or transport.
        STARTING, // The session is preparing the local world and initial state.
        RUNNING,  // The local world and role-specific readiness requirements are satisfied.
    };

    // Start a branchable setup with the same defaults as the Inspector definition.
    static godot::Ref<CrimsonWorldSessionDefinition> configure();
    // Set the authoring definition while idle. Each start captures its fields.
    void set_definition(const godot::Ref<CrimsonWorldSessionDefinition> &value);
    // Return the resource used to configure the next start.
    godot::Ref<CrimsonWorldSessionDefinition> get_definition() const;
    // Return the configured ENet port, frozen while a session is active.
    int64_t get_port() const;
    // Start a local-only world and spawn the local player.
    godot::Error start_offline();
    // Start a listen host and keep vacant spawn slots available to guests.
    godot::Error host();
    // Begin a connection to the configured port at the supplied hostname or IP address.
    godot::Error join(const godot::String &address);
    // Cancel a connection or retire the active world and its transport.
    godot::Error leave();
    // Return the local startup or readiness state.
    State get_state() const;
    // Return the mounted world for the current session, if present.
    godot::Node3D *get_world() const;
    // Return the actor assigned to this process, if present.
    godot::Node3D *get_local_player() const;
    // Return the mounted actor assigned to an occupied session-local peer ID.
    godot::Node3D *get_player(int64_t peer_id) const;
    // Configure the shared backend for CrimsonGame's typed, capacity-limited immediate play.
    void configure_immediate(const godot::Ref<godot::PackedScene> &world,
            const godot::Ref<godot::PackedScene> &player, const godot::StringName &world_key,
            int64_t max_players, int64_t listen_port, int64_t authority_epoch);
    // Inject the owning game's persistent services before this backend enters the tree.
    void use_services(session::CrimsonGameSession *session, scene_load::CrimsonSceneLoadManager *loader);
    // Host the configured immediate game as a listen or headless authority.
    godot::Error host_immediate(const godot::String &bind_address, bool dedicated, bool hold_admission = false);
    // Join the owning game's authenticated route without changing its authored listen port.
    godot::Error join_endpoint(const godot::Ref<session::CrimsonSessionEndpoint> &endpoint);
    // Return the current authority epoch for immediate readiness.
    int64_t get_world_epoch() const;
    // Return whether the native session currently owns authority.
    bool is_authority() const;

    // Configure the authority-only slot roster RPC before session admission.
    void _enter_tree() override;
    // Install the persistent native transport and observe its lifecycle.
    void _ready() override;
    // Retire staged or mounted world state when this owner exits.
    void _exit_tree() override;
    // Expire bounded late-join reservations even if a peer stops acknowledging.
    void _process(double delta) override;

protected:
    // Register the startup actions, definition property, queries, and lifecycle signals.
    static void _bind_methods();

private:
    // Transport action used after common configuration and scene preflight.
    enum class Mode { Offline, Host, Join, Dedicated };
    // Progress of one authority-issued late-join handshake.
    enum class GuestPhase { Mounting, ApplyingBaseline };

    // A bounded reservation for a single current transport connection.
    struct PendingGuest {
        // Unique ticket for this transport admission.
        int64_t ticket = 0;
        // Monotonic expiration for the current handshake step.
        uint64_t deadline = 0;
        // Next report expected from the admitted peer.
        GuestPhase phase = GuestPhase::Mounting;
    };

    // One ordered spawn marker and its current session-local occupant.
    struct Slot {
        // Marker pose relative to the mounted world's root.
        godot::Transform3D pose;
        // Session-local peer ID, with zero denoting a vacant slot.
        int32_t peer = 0;
        // Weak reference to the managed actor, if this slot is occupied.
        uint64_t actor_id = 0;
    };

    // Resource authored in the Inspector or via the branchable builder.
    godot::Ref<CrimsonWorldSessionDefinition> definition;
    // Scene-tree-owned native transport, at a matching RPC path on every peer.
    session::CrimsonGameSession *transport = nullptr;
    // Owning game's shared loader for immediate play. Legacy standalone sessions retain their mount path.
    scene_load::CrimsonSceneLoadManager *scene_loader = nullptr;
    // Role selected for the active or starting session.
    Mode active_mode = Mode::Offline;
    // Player scene captured at the start of this session.
    godot::Ref<godot::PackedScene> player_scene;
    // Optional actor-local camera selected for the local player.
    godot::NodePath camera_path;
    // Optional actor-local subtree hidden for the local player.
    godot::NodePath hidden_path;
    // Port captured at the start of this session.
    int64_t port = 0;
    // Explicit total seat limit for immediate play. Zero selects the standalone legacy convention.
    int64_t player_limit = 0;
    // Local world identity used to validate the authority's immediate-play baseline.
    godot::StringName world_key;
    // Current authority-issued world epoch, accepted from the host on a guest.
    int64_t authority_epoch = 0;
    // Whether the remote authority runs without a local player.
    bool remote_dedicated = false;
    // Monotonic ticket allocator for admitted guest attempts.
    int64_t next_ticket = 0;
    // Nonzero client ticket after the server offers the current world.
    int64_t join_ticket = 0;
    // Number of placements announced by the authority, validated after local staging.
    int64_t expected_candidates = 0;
    // Whether this client has applied and acknowledged its first roster baseline.
    bool baseline_applied = false;
    // Leave the mounted authority's gate closed until its online listing is published.
    bool admission_held = false;
    // One pending admission per actual transport sender, bounded by the transport seats.
    std::map<int32_t, PendingGuest> pending_guests;
    // Weak reference to the staged or mounted world.
    uint64_t world_id = 0;
    // Weak reference to the world-owned player container.
    uint64_t players_id = 0;
    // Validated detached player instance reserved for the first occupied slot.
    uint64_t first_actor_id = 0;
    // Ordered, bounded spawn slots with stable assignments until disconnect.
    std::vector<Slot> slots;
    // Real-time deadline for receiving the first roster after transport admission.
    godot::Ref<godot::SceneTreeTimer> spawn_deadline;
    // Reason used when world synchronization requires a local disconnect.
    godot::StringName failure_reason;
    // Last published readiness state.
    State state = IDLE;
    // Blocks reentrant startup or leave while applying scene or teardown callbacks.
    bool mutating = false;

    // Validate scene configuration, start the transport, and publish initial readiness.
    godot::Error start(Mode mode, const godot::String &address = {},
            const godot::Ref<session::CrimsonSessionEndpoint> &endpoint = {}, bool hold_admission = false);
    // Capture and validate the local resources before claiming the transport.
    godot::Error configure_start(Mode mode);
    // Validate placements and attach a player container to the detached scene.
    int64_t configure_candidate(godot::Node *candidate);
    // Request the already allowlisted immediate world through the shared loader.
    godot::Error request_world();
    // Complete authority startup or report a targeted guest scene mount.
    void on_world_loaded(const godot::StringName &scene_id);
    // Retire the starting session if local scene preparation or mount failed.
    void on_world_load_failed(const godot::StringName &scene_id, int64_t error, const godot::String &message);
    // Instantiate a detached actor and validate its optional local presentation paths.
    godot::Node3D *instantiate_actor() const;
    // Resolve the authored presentation paths or optional typed local-view component.
    bool resolve_view(godot::Node3D &actor, godot::Node3D *&camera, godot::Node3D *&hidden) const;
    // Apply a complete roster, preserving actors whose slots did not change.
    bool apply_roster(const godot::PackedInt32Array &next);
    // Snapshot the current ordered assignments as the sole application wire record.
    godot::PackedInt32Array get_roster() const;
    // Validate a host roster before changing any guest actors.
    bool valid_roster(const godot::PackedInt32Array &roster) const;
    // Apply the authority roster and send it only to admitted peers.
    void publish_roster(const godot::PackedInt32Array &roster, int32_t joining_peer = 0);
    // Receive a bounded snapshot from the admitted host.
    void receive_roster(const godot::PackedInt32Array &roster);
    // Expand authoritative peer-to-placement records into local authored candidates.
    void receive_assignments(int64_t epoch, int64_t ticket,
            const godot::PackedInt32Array &assignments);
    // Accept the current world offer after native transport admission.
    void receive_world_offer(const godot::StringName &received_world, int64_t epoch, int64_t ticket,
            int64_t candidate_count, int64_t host_limit, bool dedicated);
    // Accept one matching scene mount report from the actual admitted sender.
    void receive_scene_mounted(int64_t epoch, int64_t ticket);
    // Accept one matching initial baseline acknowledgement from the actual admitted sender.
    void receive_baseline_applied(int64_t epoch, int64_t ticket);
    // Complete guest readiness only after the authority confirms its baseline acknowledgment.
    void receive_join_ready(int64_t epoch, int64_t ticket);
    // Validate and resolve a pending handshake for the actual remote sender.
    int32_t reporting_guest(int64_t epoch, int64_t ticket, GuestPhase phase) const;
    // Publish local readiness after the role's startup or join handshake completes.
    void finish_start();
    // Apply a validated authority snapshot and complete the guest's initial mount.
    void apply_authority_roster(const godot::PackedInt32Array &roster);
    // End a pending guest with the authority's spawn rejection.
    void receive_spawn_rejection(const godot::StringName &reason);
    // Deliver a typed denial before retiring only the rejected remote connection.
    void reject_spawn(int32_t peer, const godot::StringName &reason = godot::StringName("spawn_unavailable"));
    // Check the transport and the actual sender for the fixed-world RPCs.
    bool accepts_authority_rpc() const;
    // Start the guest's initial spawn deadline after native admission.
    void on_transport_started(int64_t role, int64_t local_peer);
    // Clear all session state before notifying observers of the end reason.
    void on_transport_ended(const godot::StringName &reason);
    // Reserve the first vacant guest slot for this admitted peer.
    void on_peer_joined(int64_t peer_id);
    // Vacate the departing guest's slot and publish the changed roster.
    void on_peer_left(int64_t peer_id);
    // End a guest session which never received a valid first roster.
    void on_spawn_timeout();
    // Remove the deadline listener when its session settles.
    void clear_spawn_deadline();
    // Start a new real-time deadline for this guest startup.
    void start_spawn_deadline(double seconds);
    // End a session with the supplied world synchronization failure.
    void fail_session(const godot::StringName &reason);
    // Retire the active or staged world and clear all captured configuration.
    void clear_world(bool exiting = false);
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::CrimsonWorldSession::State);
