// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/world_session/world_session_definition.hpp"
#include "integration/world_session/movement_wire.hpp"
#include "modules/scene_manager/scene_load_manager.hpp"
#include "modules/session/game_session.hpp"
#include "modules/mover/mover.hpp"
#include "modules/mover/mover_component.hpp"
#include "modules/mover/prediction_driver.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace crimson::integration {

// Mount one shared, locally packaged world and assign player scenes to authority-owned slots.
class WorldSession : public godot::Node {
    GDCLASS(WorldSession, godot::Node)

public:
    // Maximum number of local and remote participants supported by the fixed-world backend.
    static constexpr int64_t MAX_PLAYERS = 64;

    // Trusted fixed-world player motion settings captured at game boot.
    struct MovementSettings {
        godot::Ref<mover::MotionRule> rule; ///< Null disables the movement stream.
        double speed = 0.0; ///< Metres per second at full directional input.
        int32_t reference = 0; ///< PlayerControlProfile's movement reference frame.
        bool predict = false; ///< Use the replayable static solver when the body supports it.
        mover::MoverComponent::ControllerStyle style = mover::MoverComponent::FPS_AIM; ///< Heading and steering policy.
    };

    // Native-only view of the exact session's movement stream for developer diagnostics.
    struct MovementDiagnostics {
        // Distinguish the local processing role and active movement driver.
        enum Mode { DISABLED, AUTHORITY_LIVE, AUTHORITY_STATIC, GUEST_INTERPOLATED, GUEST_PREDICTED };
        Mode mode = DISABLED; ///< Active processing role.
        int64_t simulation_tick = 0; ///< Latest completed server step, or last state received on a guest.
        int64_t acknowledged_input = 0; ///< Exact input sequence retired by the latest committed state.
        int64_t pending_inputs = 0; ///< Local commands awaiting confirmation.
        int64_t corrections = 0; ///< Number of visible-size owner corrections in this attachment.
        double correction_distance = 0.0; ///< Most recent owner correction in metres.
        godot::Vector3 authority_position; ///< Last received authority position for the local actor.
        godot::Vector3 predicted_position; ///< Last locally resolved or presented owner position.
        int64_t state_age_ms = -1; ///< Wall-clock age of the last received state, or unavailable.
        int64_t replayed_inputs = 0; ///< Commands replayed during the most recent reconciliation.
    };
    // Return the authority or owner movement counters without affecting simulation.
    MovementDiagnostics get_movement_diagnostics() const;

    // Readiness of the world and the local player's assigned spawn.
    enum State {
        IDLE,     // There is no active world or transport.
        STARTING, // The session is preparing the local world and initial state.
        RUNNING,  // The local world and role-specific readiness requirements are satisfied.
    };

    // Start a branchable setup with the same defaults as the Inspector definition.
    static godot::Ref<WorldSessionDefinition> configure();
    // Set the authoring definition while idle. Each start captures its fields.
    void set_definition(const godot::Ref<WorldSessionDefinition> &value);
    // Return the resource used to configure the next start.
    godot::Ref<WorldSessionDefinition> get_definition() const;
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
    // Return the local actor's one-based placement, or zero when no local actor is mounted.
    int64_t get_local_player_number() const;
    // Return the mounted actor assigned to an occupied session-local peer ID.
    godot::Node3D *get_player(int64_t peer_id) const;
    // Check on the authority whether this player acknowledged the current mounted world.
    bool is_peer_world_ready(int32_t peer_id) const;
    // Count fixed-world seats remaining after all currently admitted guests and the local actor.
    int32_t available_remote_seats() const;
    // Configure the shared backend for GameRuntime's typed, capacity-limited immediate play.
    void configure_immediate(const godot::Ref<godot::PackedScene> &world,
            const godot::Ref<godot::PackedScene> &player, const godot::StringName &world_key,
            int64_t max_players, int64_t listen_port, int64_t authority_epoch);
    // Freeze this start's trusted movement settings before activating the session.
    void configure_movement(const MovementSettings &settings);
    // Find the movement owner of an exact actor, independent of local network authority.
    static WorldSession *movement_for(godot::Node3D *actor);
    // Return the authority-owned camera contract for the assigned local controller.
    bool controls_movement_view(godot::Node3D *actor, godot::Node3D *view);
    // Return the heading policy configured for the attached player controller.
    mover::MoverComponent::ControllerStyle get_controller_style() const;
    // Submit one local input, including optional steering yaw in lock-on mode.
    godot::Error submit_player_motion(godot::Node3D *actor, const godot::Vector2 &axes, const godot::Vector2 &aim,
            bool release = false, bool jump_pressed = false, bool has_lock_heading = false, double lock_heading = 0.0);
    // Inject the owning game's persistent services before this backend enters the tree.
    void use_services(session::GameSession *session, scene_load::SceneLoadManager *loader);
    // Host the configured immediate game as a listen or headless authority.
    godot::Error host_immediate(const godot::String &bind_address, bool dedicated, bool hold_admission = false);
    // Join the owning game's authenticated route without changing its authored listen port.
    godot::Error join_endpoint(const godot::Ref<session::SessionEndpoint> &endpoint);
    // Return the current authority epoch for immediate readiness.
    int64_t get_world_epoch() const;
    // Return the native detail accompanying the most recent session_ended signal.
    godot::String get_last_end_detail() const;
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
    // Drive remote steering or play back the authority's motion samples.
    void _physics_process(double delta) override;

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

    // One confirmed physical pose and aim on the authority's stream timeline.
    struct MotionSample {
        godot::Vector3 position; ///< World position resolved by authority physics.
        godot::Vector2 aim; ///< World yaw and pitch in radians.
        double body_yaw = 0.0; ///< World yaw of the body, independent of the view.
        int64_t tick = 0; ///< Monotonic authority snapshot sequence.
    };

    // An immutable movement command sampled for one local physics tick.
    struct MotionInput {
        godot::Vector2 axes; ///< Unit-length-bounded directional intent.
        godot::Vector2 aim; ///< Absolute yaw and pitch in radians.
        int64_t tick = 0; ///< Slot-local command sequence independent of server clock origin.
        bool jump_pressed = false; ///< One jump attempt on this exact command, including during replay.
        bool has_lock_heading = false; ///< Whether this command steers around a lock direction.
        double lock_heading = 0.0; ///< World lock yaw in radians, or zero when absent.
        mover::PredictionState predicted; ///< Owner state after this input, when predicted.
        double predicted_yaw = 0.0; ///< Simulated body yaw after this input.
        bool has_prediction = false; ///< Whether the saved state matches this command's frame.
    };

    // Latest complete authoritative owner state, staged until client physics.
    struct OwnerCorrection {
        mover::PredictionState state; ///< Static solver restart state.
        double body_yaw = 0.0; ///< World body yaw used to resume controller steering.
        int64_t server_tick = 0; ///< Exact committed physics tick.
        int64_t acknowledged_input = 0; ///< Input consumed or expired at that step.
    };

    // One ordered spawn marker and its current session-local occupant.
    struct Slot {
        // Marker pose relative to the mounted world's root.
        godot::Transform3D pose;
        // Session-local peer ID, with zero denoting a vacant slot.
        int32_t peer = 0;
        // Weak reference to the managed actor, if this slot is occupied.
        uint64_t actor_id = 0;
        uint64_t camera_id = 0; ///< Exact view resolved from the authored actor.
        godot::Ref<mover::Mover> mover; ///< Authority-only physics owner.
        std::unique_ptr<mover::PredictionDriver> predictor; ///< Static collision solver used by authority or owner.
        mover::PredictionState simulated; ///< Latest committed static authority or local predicted state.
        godot::Basis spawn_basis; ///< Authored character heading before aim changes.
        godot::Vector2 axes; ///< Latest accepted directional intent.
        godot::Vector2 aim; ///< Latest accepted absolute view angles.
        godot::Vector2 committed_aim; ///< View angle applied before the completed authority solve.
        double body_yaw = 0.0; ///< Latest simulated body yaw in world radians.
        bool has_lock_heading = false; ///< Whether the held input includes a lock direction.
        double lock_heading = 0.0; ///< Last admitted lock yaw in world radians, or zero when absent.
        bool jump_pending = false; ///< One local attempt awaiting the next authority step.
        bool local_input_pending = false; ///< Local controller submitted input for this physics step.
        uint64_t received_at = 0; ///< Local monotonic milliseconds of the last accepted sample.
        int64_t input_sequence = 0; ///< Highest accepted input sequence for this admitted actor.
        int64_t retired_input = 0; ///< Highest command whose step was committed or deliberately expired.
        int64_t committed_input = 0; ///< Cursor sampled only after the prior physics step.
        uint64_t sample_received_at = 0; ///< Latest received pose time for diagnostic age.
        std::map<int64_t, MotionInput> pending_inputs; ///< Bounded commands awaiting the server step.
        uint64_t first_input_at = 0; ///< When a new stream started filling its initial jitter cushion.
        bool input_started = false; ///< Whether this stream is consuming sequence slots.
        int64_t admission_ticket = 0; ///< Connection generation captured on the authority.
        MotionSample previous_sample; ///< Start of this proxy's interpolation interval.
        MotionSample next_sample; ///< End of this proxy's interpolation interval.
        double playback = 1.0; ///< Interpolation progress since the latest accepted sample.
        double playback_seconds = 0.05; ///< Duration of the active proxy interval.
    };

    // Resource authored in the Inspector or via the branchable builder.
    godot::Ref<WorldSessionDefinition> definition;
    // Scene-tree-owned native transport, at a matching RPC path on every peer.
    session::GameSession *transport = nullptr;
    // Owning game's shared loader for immediate play. Legacy standalone sessions retain their mount path.
    scene_load::SceneLoadManager *scene_loader = nullptr;
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
    MovementSettings movement; ///< Settings retained for the current start only.
    int64_t motion_revision = 0; ///< Reliable roster generation gating movement snapshots.
    int64_t motion_tick = 0; ///< Monotonic authority sequence for full pose samples.
    int64_t motion_input_sequence = 0; ///< Next local guest intent sequence.
    uint64_t motion_input_sent_at = 0; ///< Last guest upload time on the local real-time clock.
    double motion_snapshot_elapsed = 0.0; ///< Authority interval since publishing a pose sample.
    int32_t motion_rate = 0; ///< Authority physics steps per second, sent in the reliable offer.
    int64_t motion_server_tick = 0; ///< Completed authority physics steps.
    std::deque<MotionInput> owner_inputs; ///< Bounded recent local commands for resending and replay.
    OwnerCorrection owner_correction; ///< Newest complete owner snapshot awaiting application.
    int64_t owner_ack = 0; ///< Latest applied reconciliation cursor.
    int64_t owner_applied_server_tick = 0; ///< Most recent owner state used to restore local physics.
    int64_t owner_correction_count = 0; ///< Number of applied corrections above the visual threshold.
    int64_t owner_replayed_inputs = 0; ///< Number of commands replayed in the latest reconciliation.
    bool owner_history_gap = false; ///< Suspend speculative steps until retained history rejoins authority.
    bool owner_release_pending = false; ///< Controller retired and queued neutral for a real physics tick.
    godot::Vector2 owner_release_aim; ///< Preserved aim for a controller's neutral handoff.
    double owner_correction_distance = 0.0; ///< Magnitude of the most recent correction.
    godot::Vector3 owner_view_offset; ///< Presentation-only offset decayed after a small correction.
    godot::Vector3 owner_view_base; ///< Last camera position without our correction.
    godot::Vector3 owner_view_applied; ///< Last local offset written by our presentation layer.
    godot::ObjectID owner_view_id; ///< Exact owner camera associated with the presentation offset.
    // Real-time deadline for receiving the first roster after transport admission.
    godot::Ref<godot::SceneTreeTimer> spawn_deadline;
    // Reason used when world synchronization requires a local disconnect.
    godot::StringName failure_reason;
    // Detail retained until transport teardown settles the failure.
    godot::String failure_detail;
    // Detail for the settled session, published before the terminal signal.
    godot::String last_end_detail;
    // Last published readiness state.
    State state = IDLE;
    // Blocks reentrant startup or leave while applying scene or teardown callbacks.
    bool mutating = false;

    // Validate scene configuration, start the transport, and publish initial readiness.
    godot::Error start(Mode mode, const godot::String &address = {},
            const godot::Ref<session::SessionEndpoint> &endpoint = {}, bool hold_admission = false);
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
    // Accept the motion-capable variant of the same ready-gated fixed-world offer.
    void receive_movement_offer(const godot::StringName &received_world, int64_t epoch, int64_t ticket,
            int64_t candidate_count, int64_t host_limit, bool dedicated, int32_t tick_rate);
    // Validate capability and offer together before entering the shared mount path.
    void accept_world_offer(const godot::StringName &received_world, int64_t epoch, int64_t ticket,
            int64_t candidate_count, int64_t host_limit, bool dedicated, bool controlled);
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
    // Expand bounded assignment pairs into a full placement roster without mutating actors.
    bool expand_assignments(const godot::PackedInt32Array &assignments, godot::PackedInt32Array &roster) const;
    // Acknowledge the mounted roster only after its required baseline is installed.
    void acknowledge_roster();
    // End a pending guest with the authority's spawn rejection.
    void receive_spawn_rejection(const godot::StringName &reason);
    // Deliver a typed denial before retiring only the rejected remote connection.
    void reject_spawn(int32_t peer, const godot::StringName &reason = godot::StringName("spawn_unavailable"));
    // Check the transport and the actual sender for the fixed-world RPCs.
    bool accepts_authority_rpc() const;
    // Configure and bind the motion RPCs on their fixed channels.
    static void bind_movement_methods();
    // Use authority-only reliable baselines and bounded unreliable samples.
    void configure_movement_rpc();
    // Validate a movement-capable body before publishing a roster change.
    bool validate_movement_actor(godot::Node3D *actor) const;
    // Retire all per-occupant state without changing its authored spawn marker.
    static void reset_slot_movement(Slot &slot);
    // Prepare a slot's view and authority Mover after the actor is mounted.
    bool attach_movement(Slot &slot, godot::Node3D *actor);
    // Reset current movement timeline and captured configuration before actor retirement.
    void clear_movement();
    // Resolve one currently occupied slot by the transport's authenticated peer identity.
    Slot *movement_slot(int64_t peer);
    // Author and transmit one reliable roster baseline containing the latest poses.
    void send_movement_roster(const godot::PackedInt32Array &assignments);
    // Validate a movement roster and install its pose before acknowledging readiness.
    void receive_movement_roster(int64_t epoch, int64_t ticket, int64_t revision, int64_t tick,
            const godot::PackedByteArray &payload, int64_t signature, double speed, int32_t reference, int32_t style);
    // Apply an already validated authority roster while blocking synchronous lifecycle reentry.
    bool commit_authority_roster(const godot::PackedInt32Array &roster);
    // Stage one rate-limited finite input for the actual ready sender's actor.
    void receive_motion_input(int64_t epoch, int64_t ticket, int64_t newest,
            const godot::PackedByteArray &payload);
    // Accept a committed owner state, including the exact retired input cursor.
    void receive_owner_correction(int64_t epoch, int64_t revision, int64_t server_tick, int64_t ack,
            const godot::Vector3 &position, const godot::Vector3 &velocity, bool grounded, double body_yaw);
    // Receive a bounded full-state snapshot for the confirmed roster generation.
    void receive_motion_pose(int64_t epoch, int64_t revision, int64_t tick,
            const godot::PackedByteArray &payload);
    // Publish completed previous-tick motion to ready guests at a bounded cadence.
    void publish_motion_pose();
    // Advance one remote command, falling back to held steering with no jump press if a row is missing.
    godot::Vector2 take_remote_input(Slot &slot, uint64_t now, bool &jump_pressed);
    // Step or replay an owner command through the same static solver as authority.
    godot::Error update_local_motion(Slot &slot, const godot::Vector2 &axes, const godot::Vector2 &aim, bool flush,
            bool jump_pressed, bool has_lock_heading = false, double lock_heading = 0.0);
    // Predict or replay the same steering, rotation, and collision used by the authority.
    void predict_input(Slot &slot, MotionInput &input);
    // Restore the newest correction, replay unconfirmed commands, and publish the pose.
    void reconcile_owner(Slot &slot);
    // Advance and publish a static authority actor through its Mover.
    void step_authority(Slot &slot, bool jump_pressed);
    // Apply heading, view aim, and steering for the current authority step.
    void drive_movement(Slot &slot, const godot::Vector2 &axes, bool jump_pressed);
    // Write interpolated world position and preserve immediate local camera look.
    void set_motion_pose(Slot &slot, const MotionSample &sample, bool initialize);
    // Advance guest presentation once per render frame without extrapolating.
    void interpolate_movement(double delta);
    // Add the current pose to a compact packet in roster order.
    bool capture_motion_pose(const Slot &slot, motion_wire::PoseRow &pose) const;
    // Check and install a newer pose as the endpoint for a non-authoritative actor.
    void stage_motion_pose(Slot &slot, const MotionSample &sample);
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
    // End a session with the supplied stable reason and available local detail.
    void fail_session(const godot::StringName &reason, const godot::String &detail = {});
    // Retire the active or staged world and clear all captured configuration.
    void clear_world(bool exiting = false);
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::WorldSession::State);
