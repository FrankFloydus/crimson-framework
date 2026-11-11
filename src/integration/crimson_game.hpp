// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "game_definition.hpp"
#include "game_operation.hpp"
#include "game_save_slot.hpp"
#include "match_checkpoint.hpp"
#include "match_network.hpp"
#include "modules/match_runtime/command_router.hpp"
#include "modules/match_runtime/player_bindings.hpp"
#include "modules/replication/replication.hpp"
#include "modules/save/save_coordinator.hpp"
#include "modules/scene_load/scene_load_manager.hpp"
#include "modules/session/game_session.hpp"

#include <map>
#include <set>

namespace crimson::integration {

// Composes standalone modules into one Godot game, with an explicit authority and world lifecycle.
class CrimsonGame : public godot::Node {
    GDCLASS(CrimsonGame, godot::Node)

public:
    // Public readiness of the integrated game.
    enum Phase { IDLE, LOBBY, TRANSITIONING, RUNNING, RECOVERING };

    // Assign the game definition before the first start, host, or join.
    void set_definition(const godot::Ref<CrimsonGameDefinition> &value);
    // Return the authoring definition.
    godot::Ref<CrimsonGameDefinition> get_definition() const;
    // Open offline authority and start the configured world.
    godot::Ref<CrimsonGameOperation> start_offline();
    // Open an offline lobby so a title screen can restore directly into it.
    godot::Error open_offline();
    // Open an ENet lobby without starting its world.
    godot::Error host(int64_t port, int64_t max_clients = 32, const godot::String &bind_address = "*");
    // Connect to an ENet lobby. Admission is reported by phase_changed.
    godot::Error join(const godot::String &address, int64_t port, double timeout_seconds = 10.0);
    // Leave the current game and release its match, transport, and mounted world.
    godot::Error leave();
    // Start the selected or configured world from an authority lobby.
    godot::Ref<CrimsonGameOperation> start_match(const godot::StringName &world = {});
    // Transition a running authority to another registered world.
    godot::Ref<CrimsonGameOperation> travel_to(const godot::StringName &world);
    // Select a save slot for fluent capture, history, or restore.
    godot::Ref<CrimsonSaveSlot> save_slot(const godot::String &profile, const godot::String &slot);
    // Capture committed match state and publish a new generation.
    godot::Ref<CrimsonGameOperation> save(const godot::String &profile, const godot::String &slot,
            const godot::Dictionary &metadata = {}, bool autosave = false);
    // Stage the selected generation and activate its destination with its state.
    godot::Ref<CrimsonGameOperation> restore(const godot::String &profile, const godot::String &slot,
            const godot::String &sequence = {}, const godot::String &generation_id = {});
    // Read a page from a known save slot.
    godot::Ref<CrimsonGameOperation> save_history(const godot::String &profile, const godot::String &slot,
            const godot::String &before_sequence = {}, const godot::String &before_id = {}, int64_t limit = 16);
    // Associate an admitted peer with an identity supplied by trusted authority code.
    godot::Error bind_player(int64_t peer, const godot::String &player_id);
    // Register a gameplay command on this game. Configure matching command names on peers.
    godot::Error register_command(const godot::String &name, const godot::Callable &handler,
            bool requires_primary = false);
    // Declare a client-facing command without installing its authority handler.
    godot::Error declare_command(const godot::String &name, bool requires_primary = false);
    // Route local input through the same command contract offline and online.
    godot::Ref<CrimsonGameOperation> submit(const godot::String &name, const godot::Dictionary &payload = {});
    // Select the registered stores visible in their entirety to every admitted client.
    godot::Error set_shared_stores(const godot::PackedStringArray &store_ids);
    // Supply a per-player projection returning {ok, view} from committed state.
    godot::Error set_projector(const godot::Callable &projector);
    // Return current game readiness.
    Phase get_phase() const;
    // Return the committed world ID, or an empty ID before the first world.
    godot::StringName get_world_id() const;
    // Return the committed world epoch.
    int64_t get_world_epoch() const;
    // Return the currently mounted root scene, valid until its next replacement.
    godot::Node *get_world() const;
    // Return the authority coordinator for game-owned store registration and transactions.
    godot::Ref<match_runtime::CrimsonTransactionCoordinator> get_transactions() const;
    // Return the authority service for possession setup and mutations.
    godot::Ref<match_runtime::CrimsonPossession> get_possession() const;
    // Return the local player identity from trusted authority state or projection.
    godot::String get_local_player_id() const;
    // Return the local player's current primary character, if any.
    godot::String get_local_character_id() const;
    // Return the last installed client view for this world.
    godot::Dictionary get_projection() const;
    // Return whether this instance currently owns gameplay authority.
    bool is_authority() const;

    // Create persistent child nodes for the standalone services.
    void _ready() override;
    // Advance replication and synchronization deadlines on the main thread.
    void _process(double delta) override;
    // Release pending operations when the game node is removed.
    void _exit_tree() override;

protected:
    // Bind the public facade and lifecycle signals to Godot.
    static void _bind_methods();

private:
    // Command declaration shared by the router and the submitting client.
    struct Command {
        // Authority callback with the command router's player and intent arguments.
        godot::Callable handler;
        // Requires the submitting character and its current possession revision.
        bool requires_primary = false;
    };

    // One authority transition from scene preparation through baseline readiness.
    struct Transition {
        // Caller-owned handle settled once the world becomes ready or the request fails.
        godot::Ref<CrimsonGameOperation> operation;
        // Game-owned destination, distinct from the reusable scene asset ID.
        godot::StringName target;
        // Reserved transition epoch that becomes active at publication.
        int64_t epoch = 0;
        // Checkpoint ticket for a restore, or zero for an ordinary transition.
        int64_t save_ticket = 0;
        // Selected generation from checkpoint preparation.
        godot::Dictionary generation;
        // Admitted peers required to acknowledge the destination state.
        std::set<int64_t> waiting;
        // Deadline for baseline readiness after the scene barrier.
        uint64_t deadline = 0;
        // Previous lifecycle state restored after a preparation failure.
        Phase previous = LOBBY;
        // Whether the authority announced the destination to clients.
        bool announced = false;
        // Whether scene and state publication has crossed its commit boundary.
        bool published = false;
        // Whether admission should reopen if preparation fails.
        bool reopen_lobby = false;
    };

    // Authoring definition held until configuration is frozen.
    godot::Ref<CrimsonGameDefinition> definition;
    // Normalized world ID to scene ID mapping used by the running game.
    std::map<godot::StringName, godot::StringName> worlds;
    // Default world captured during configuration.
    godot::StringName starting_world;
    // Scene-tree-owned standalone session.
    session::CrimsonGameSession *session_node = nullptr;
    // Scene-tree-owned standalone loader.
    scene_load::CrimsonSceneLoadManager *loader = nullptr;
    // Scene-tree-owned standalone checkpoint coordinator.
    checkpoint::SaveCoordinator *saves = nullptr;
    // Scene-tree-owned per-peer replication transport.
    replication::CrimsonReplication *replication_node = nullptr;
    // Scene-tree-owned command and world control transport.
    CrimsonMatchNetwork *network = nullptr;
    // Persistent parent for the mounted world.
    godot::Node *world_container = nullptr;
    // Checkpoint adapter bound to the current authority stores.
    godot::Ref<CrimsonMatchCheckpoint> checkpoint_adapter;
    // Current match's authoritative transaction stores.
    godot::Ref<match_runtime::CrimsonTransactionCoordinator> transactions;
    // Match-level primary character assignments.
    godot::Ref<match_runtime::CrimsonPossession> possession;
    // Admitted connection generations and stable player identities.
    godot::Ref<match_runtime::CrimsonPlayerBindings> bindings;
    // Transport-independent authority command dispatch.
    godot::Ref<match_runtime::CrimsonCommandRouter> router;
    // Command declarations retained across game sessions.
    std::map<godot::String, Command> commands;
    // Explicit public stores used by the default projector.
    godot::PackedStringArray shared_stores;
    // Optional game-owned visibility policy.
    godot::Callable project_view;
    // In-flight reads and writes owned by the checkpoint coordinator.
    std::map<int64_t, godot::Ref<CrimsonGameOperation>> save_operations;
    // Saves waiting for the next main-thread capture boundary.
    int64_t pending_captures = 0;
    // Active authority transition, or an empty handle when no transition is in flight.
    Transition transition;
    // Public lifecycle phase.
    Phase phase = IDLE;
    // Last committed world identity.
    godot::StringName world_id;
    // Highest authority transition epoch allocated by this game.
    int64_t next_epoch = 1;
    // Current committed world version.
    int64_t world_epoch = 0;
    // Match identity generated by the authority and projected to admitted clients.
    godot::String match_id;
    // Sequence of intents submitted by this local player in its current world.
    int64_t local_sequence = 0;
    // Destination announced by the authority to this client.
    godot::StringName remote_target;
    // Remote transition epoch awaiting the authority's completion message.
    int64_t remote_epoch = 0;
    // Client phase restored if the authority aborts before publication.
    Phase remote_previous = LOBBY;
    // Prevents reuse until the last session's deferred cleanup runs.
    bool cleaning = false;
    // Reason retained until the old world and transport are fully retired.
    godot::StringName ended_reason;
    // Whether the definition has already been applied to persistent services.
    bool configured = false;
    // Prevents leaving reentrantly while scene or store observers are running.
    bool publishing = false;

    // Validate and freeze the definition for the persistent services.
    godot::Error configure_game();
    // Allocate an authority scope and attach it to the save bridge.
    void reset_match();
    // Update readiness before notifying callers.
    void set_phase(Phase value);
    // Return a new operation already settled with a structured outcome.
    godot::Ref<CrimsonGameOperation> completed(bool ok, const godot::String &code,
            const godot::String &message = {}) const;
    // Build a result shared by lifecycle and command adapters.
    godot::Dictionary outcome(bool ok, const godot::String &code, const godot::String &message = {}) const;
    // Close command and connection admission for an authority transition.
    godot::Error begin_transition(const godot::Ref<CrimsonGameOperation> &operation);
    // Tell participants about the world and request coordinated scene staging.
    void stage_scene(const godot::StringName &target);
    // Publish detached state immediately before the scene loader mounts the new world.
    int64_t publish_world();
    // Settle an authority transition and restore or close gameplay admission.
    void finish_transition(bool ok, const godot::String &code, const godot::String &message = {},
            bool recovery = false);
    // Apply session configuration after local or remote admission.
    void on_session_started(int64_t role, int64_t local_peer);
    // Queue cleanup when the owning transport leaves.
    void on_session_ended(const godot::StringName &reason);
    // Free the old world and match after transport and scene polling settle.
    void cleanup_session();
    // Revoke trust and readiness for a departed connection.
    void on_peer_left(int64_t peer);
    // Report a roster addition so the game can authenticate the new player.
    void on_peer_joined(int64_t peer);
    // Close client input when the authority announces an upcoming world.
    void on_world_announced(const godot::StringName &target, int64_t epoch);
    // Publish the client world only after matching replication and scene readiness.
    void on_world_finished(int64_t epoch, bool ok, bool recovery);
    // Start replication after all peers attach their scenes.
    void on_scene_completed(const godot::StringName &scene_id);
    // Preserve the previous world after an unsuccessful preparation.
    void on_scene_failed(const godot::StringName &scene_id, int64_t error, const godot::String &message);
    // Enter recovery after an unsuccessful distributed commit.
    void on_scene_sync_failed(const godot::StringName &scene_id, int64_t error, const godot::String &message);
    // Resolve the saved world after the candidate has validated.
    void on_load_prepared(int64_t ticket, const godot::Dictionary &generation);
    // Complete a save or report a failed prepared load.
    void on_save_finished(int64_t ticket, const godot::Dictionary &result);
    // Mark one required peer ready for the current destination.
    void on_peer_ready(int64_t peer, int64_t epoch, int64_t generation);
    // Recover if a required peer cannot synchronize.
    void on_peer_sync_failed(int64_t peer, int64_t epoch, int64_t error);
    // Forward projection updates to game-owned client views.
    void on_projection_updated(int64_t epoch, int64_t revision, bool baseline);
    // Create the recipient's allowed view and its private command context.
    godot::Dictionary project(int64_t peer, const godot::String &player, int64_t epoch, int64_t revision);
    // Authenticate network command sender and forward the intent to the router.
    godot::Dictionary dispatch_remote(int64_t peer, const godot::Dictionary &intent);
    // Return whether all currently connected peers have authenticated bindings.
    bool peers_are_bound() const;
    // Resolve local or projected possession fields used by the command facade.
    godot::Dictionary local_command_context() const;
    // Capture all admitted save requests at the latest committed revision.
    void capture_saves();
    // Settle tracked save tickets as a session leaves or its node is removed.
    void cancel_save_operations();
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::CrimsonGame::Phase);