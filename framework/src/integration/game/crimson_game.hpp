// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/online/online_directory.hpp"

#include "integration/game/game_definition.hpp"
#include "integration/game/game_operation.hpp"
#include "integration/game/game_save_slot.hpp"
#include "integration/game/match_checkpoint.hpp"
#include "integration/game/match_network.hpp"
#include "integration/world_session/world_session.hpp"
#include "modules/match_runtime/command_router.hpp"
#include "modules/match_runtime/player_bindings.hpp"
#include "modules/replication/replication.hpp"
#include "modules/save/save_coordinator.hpp"
#include "modules/scene_load/scene_load_manager.hpp"
#include "modules/session/game_session.hpp"

#include <map>
#include <set>

namespace crimson::integration {

class GameVoice;

// Composes standalone modules into one Godot game, with an explicit authority and world lifecycle.
class GameRuntime : public godot::Node {
    GDCLASS(GameRuntime, godot::Node)

    friend class GameVoice;

public:
    // Public readiness of the integrated game.
    enum Phase { IDLE, LOBBY, TRANSITIONING, RUNNING, RECOVERING };

    // Start the shared fluent game definition with Inspector defaults.
    static godot::Ref<GameDefinition> configure();
    // Assign the game definition before the first start, host, or join.
    void set_definition(const godot::Ref<GameDefinition> &value);
    // Return the authoring definition.
    godot::Ref<GameDefinition> get_definition() const;
    // Open offline authority and start the configured world.
    godot::Ref<GameOperation> start_offline();
    // Enter the configured starting world with a local actor.
    godot::Ref<GameOperation> play_solo();
    // Open an immediate listen game and allow guests to join its active world.
    godot::Ref<GameOperation> host_game(const godot::String &bind_address = "*");
    // Start a dedicated authority with all configured seats available to remote players.
    godot::Ref<GameOperation> start_dedicated(const godot::String &bind_address = "*");
    // Join an immediate game at the configured port and wait for local readiness.
    godot::Ref<GameOperation> join_game(const godot::String &address);
    // Mount and publish an authenticated listen game at the advertised address.
    godot::Ref<GameOperation> host_online(const godot::String &public_address = {}, const godot::String &bind_address = "*");
    // Mount and publish an authenticated playerless authority.
    godot::Ref<GameOperation> start_dedicated_online(const godot::String &public_address = {},
            const godot::String &bind_address = "*");
    // Resolve a directory listing and finish after the authenticated late join is ready.
    godot::Ref<GameOperation> join_online(const godot::String &listing_id);
    // Validate a provider-owned listing before using the normal online join lifecycle.
    godot::Ref<GameOperation> join_listing(const godot::Ref<OnlineListing> &listing);
    // Return the invite gateway attached to this game when its provider supplies one.
    godot::Ref<GameInvites> invites() const;
    // Return this Game's voice service when its Steam capability is active.
    GameVoice *voice() const;
    // Return the active authority's directory listing, or an empty string.
    godot::String get_online_listing() const;
    // Open an offline lobby so a title screen can restore directly into it.
    godot::Error open_offline();
    // Open an ENet lobby without starting its world.
    godot::Error host(int64_t port, int64_t max_clients = 32, const godot::String &bind_address = "*");
    // Connect to an ENet lobby. Admission is reported by phase_changed.
    godot::Error join(const godot::String &address, int64_t port, double timeout_seconds = 10.0);
    // Leave the current game and release its match, transport, and mounted world.
    godot::Error leave();
    // Start the selected or configured world from an authority lobby.
    godot::Ref<GameOperation> start_match(const godot::StringName &world = {});
    // Transition a running authority to another registered world.
    godot::Ref<GameOperation> travel_to(const godot::StringName &world);
    // Select a save slot for fluent capture, history, or restore.
    godot::Ref<SaveSlot> save_slot(const godot::String &profile, const godot::String &slot);
    // Capture committed match state and publish a new generation.
    godot::Ref<GameOperation> save(const godot::String &profile, const godot::String &slot,
            const godot::Dictionary &metadata = {}, bool autosave = false);
    // Stage the selected generation and activate its destination with its state.
    godot::Ref<GameOperation> restore(const godot::String &profile, const godot::String &slot,
            const godot::String &sequence = {}, const godot::String &generation_id = {});
    // Read a page from a known save slot.
    godot::Ref<GameOperation> save_history(const godot::String &profile, const godot::String &slot,
            const godot::String &before_sequence = {}, const godot::String &before_id = {}, int64_t limit = 16);
    // Associate an admitted peer with an identity supplied by trusted authority code.
    godot::Error bind_player(int64_t peer, const godot::String &player_id);
    // Register a gameplay command on this game. Configure matching command names on peers.
    godot::Error register_command(const godot::String &name, const godot::Callable &handler,
            bool requires_primary = false);
    // Declare a client-facing command without installing its authority handler.
    godot::Error declare_command(const godot::String &name, bool requires_primary = false);
    // Route local input through the same command contract offline and online.
    godot::Ref<GameOperation> submit(const godot::String &name, const godot::Dictionary &payload = {});
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
    // Return the local immediate-play actor while the configured world is mounted.
    godot::Node3D *get_local_player() const;
    // Return an immediate-play actor by its session-local peer ID.
    godot::Node3D *get_player(int64_t peer_id) const;
    // Return the authority coordinator for game-owned store registration and transactions.
    godot::Ref<match_runtime::TransactionCoordinator> get_transactions() const;
    // Return the authority service for possession setup and mutations.
    godot::Ref<match_runtime::PlayerPossession> get_possession() const;
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
    // Ask for a fresh ready notification when an existing Game is reattached.
    void _enter_tree() override;
    // Advance replication and synchronization deadlines on the main thread.
    void _process(double delta) override;
    // Release pending operations when the game node is removed.
    void _exit_tree() override;

protected:
    // Bind the public facade and lifecycle signals to Godot.
    static void _bind_methods();

private:
    // Persistent voice RPC path shared by all peers of this Game.
    GameVoice *voice_service = nullptr;

    // Immediate startup action selected by the caller.
    enum class ImmediateMode { Solo, Host, Join, Dedicated };
    // One online start, from directory resolution or local mount through readiness.
    struct OnlineStart {
        // Terminal game result returned to the caller.
        godot::Ref<GameOperation> operation;
        // Inner start that uses the same transport, Scene Load and roster as direct games.
        godot::Ref<GameOperation> world_start;
        // Publish or resolve result owned by the provider until it completes.
        godot::Ref<OnlineDirectoryRequest> request;
        // Authenticator selected before directory work, checked again before opening the transport.
        godot::Ref<session::SessionAuthenticator> authenticator;
        // Direct advertised endpoint if the caller supplied one. A platform directory may build its own route.
        godot::Ref<session::SessionEndpoint> publication;
        // Whether this request mounts an authority before publication.
        bool hosting = false;
        // Scene-thread deadline for a pending directory request.
        uint64_t deadline = 0;
    };
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
        godot::Ref<GameOperation> operation;
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
    godot::Ref<GameDefinition> definition;
    // Normalized world ID to scene ID mapping used by the running game.
    std::map<godot::StringName, godot::StringName> worlds;
    // Default world captured during configuration.
    godot::StringName starting_world;
    // Scene-tree-owned standalone session.
    session::GameSession *session_node = nullptr;
    // Lazily allocated world and player backend for immediate play.
    WorldSession *immediate = nullptr;
    // Pending immediate startup operation, settled after the local actor or failure.
    godot::Ref<GameOperation> immediate_start;
    // Starting-world identity captured for the immediate session.
    godot::StringName immediate_world_id;
    // Whether this game currently owns the immediate backend path.
    bool immediate_active = false;
    // Startup role used to report a pending operation's terminal failure.
    ImmediateMode immediate_mode = ImmediateMode::Solo;
    // Trusted online directory supplied when this game scope opened.
    godot::Ref<OnlineDirectory> online_directory;
    // Active listing owned by the current authority, retired before teardown notification.
    godot::String online_listing;
    // Listing whose directory membership belongs to the current or pending guest.
    godot::String online_join;
    // Optional directory work belonging to the pending immediate session.
    OnlineStart online_start;
    // Whether the service provider failed to construct the persistent native services.
    bool service_setup_failed = false;
    // Scene-tree-owned standalone loader.
    scene_load::SceneLoadManager *loader = nullptr;
    // Scene-tree-owned standalone checkpoint coordinator.
    checkpoint::SaveCoordinator *saves = nullptr;
    // Scene-tree-owned per-peer replication transport.
    replication::StateReplication *replication_node = nullptr;
    // Scene-tree-owned command and world control transport.
    MatchNetwork *network = nullptr;
    // Persistent parent for the mounted world.
    godot::Node *world_container = nullptr;
    // Checkpoint adapter bound to the current authority stores.
    godot::Ref<MatchCheckpoint> checkpoint_adapter;
    // Current match's authoritative transaction stores.
    godot::Ref<match_runtime::TransactionCoordinator> transactions;
    // Match-level primary character assignments.
    godot::Ref<match_runtime::PlayerPossession> possession;
    // Admitted connection generations and stable player identities.
    godot::Ref<match_runtime::PlayerBindings> bindings;
    // Transport-independent authority command dispatch.
    godot::Ref<match_runtime::CommandRouter> router;
    // Command declarations retained across game sessions.
    std::map<godot::String, Command> commands;
    // Explicit public stores used by the default projector.
    godot::PackedStringArray shared_stores;
    // Optional game-owned visibility policy.
    godot::Callable project_view;
    // In-flight reads and writes owned by the checkpoint coordinator.
    std::map<int64_t, godot::Ref<GameOperation>> save_operations;
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
    // Re-expose a retained game's scope only after its previous session retires.
    bool republish_after_cleanup = false;
    // Reason retained until the old world and transport are fully retired.
    godot::StringName ended_reason;
    // Whether the definition has already been applied to persistent services.
    bool configured = false;
    // Prevents leaving reentrantly while scene or store observers are running.
    bool publishing = false;

    // Validate and freeze the definition for the persistent services.
    godot::Error configure_game();
    // Validate the declaration, open the immediate session, and track the operation.
    godot::Ref<GameOperation> start_immediate(ImmediateMode mode, const godot::String &address = {},
            const godot::Ref<session::SessionEndpoint> &endpoint = {}, bool online = false);
    // Validate the directory and authenticator before claiming online startup.
    godot::Ref<GameOperation> check_online_start() const;
    // Start a world to publish only after its authority has mounted it.
    godot::Ref<GameOperation> begin_online_host(ImmediateMode mode, const godot::String &public_address,
            const godot::String &bind_address);
    // Advance completed directory and world operations at the scene-thread boundary.
    void process_online_start();
    // Cancel provider work and remove the pending public handle from the game.
    godot::Ref<GameOperation> retire_online_start();
    // Remove the current listing or guest membership exactly once.
    void retire_online_presence();
    // Settle a failed online start and roll back any world mounted for publication.
    void fail_online_start(const godot::Dictionary &result);
    // Publish the normal readiness signals after directory and world work both succeed.
    void finish_online_start();
    // Publish game readiness after the backend mounts the local actor.
    void on_immediate_state(int64_t state);
    // Settle a pending start and release the facade after backend cleanup.
    void on_immediate_ended(const godot::StringName &reason);
    // Allocate an authority scope and attach it to the save bridge.
    void reset_match();
    // Update readiness before notifying callers.
    void set_phase(Phase value);
    // Return a new operation already settled with a structured outcome.
    godot::Ref<GameOperation> completed(bool ok, const godot::String &code,
            const godot::String &message = {}) const;
    // Build a result shared by lifecycle and command adapters.
    godot::Dictionary outcome(bool ok, const godot::String &code, const godot::String &message = {}) const;
    // Close command and connection admission for an authority transition.
    godot::Error begin_transition(const godot::Ref<GameOperation> &operation);
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

VARIANT_ENUM_CAST(crimson::integration::GameRuntime::Phase);
