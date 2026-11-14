// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_definition.hpp"

#include <godot_cpp/classes/node3d.hpp>

namespace crimson::match_runtime {
class TransactionCoordinator;
}

namespace crimson::replication {
class NetworkCodecs;
}

namespace crimson::steam {
class SteamConfig;
class SteamServer;
class Steamworks;
}

namespace crimson::integration {

class Game;
class GameInvites;
class SteamInvite;
class GameVoice;
class GameInventory;
class GameNetworkDiagnostics;
class InventoryConfig;
class PlayerControlProfile;
class SaveSlot;

// A durable world choice that can be selected in code or stored as a resource.
class WorldDefinition : public godot::Resource {
    GDCLASS(WorldDefinition, godot::Resource)

    // Stable identity used by travel and checkpoints.
    godot::StringName id;
    // Deferred PackedScene locator used by Scene Loader.
    godot::String path;

protected:
    // Register world construction and stored configuration.
    static void _bind_methods();

public:
    // Create a world choice with an explicit durable identity and scene path.
    static godot::Ref<WorldDefinition> at(const godot::StringName &id, const godot::String &path);
    // Assign the durable world identifier.
    void set_id(const godot::StringName &value);
    // Read the durable world identifier.
    godot::StringName get_id() const;
    // Assign the local scene asset path.
    void set_path(const godot::String &value);
    // Read the local scene asset path.
    godot::String get_path() const;
};

// A provider receives a fresh binding draft for each code-first attachment.
class GameBindings : public godot::RefCounted {
    GDCLASS(GameBindings, godot::RefCounted)

    friend class GameConfig;

    // Optional factory returning the selected typed SessionTransport.
    godot::Callable transport_factory;
    // Once sealed, a provider cannot mutate the accepted draft.
    bool sealed = false;
    // Tracks a rejected registration so bootstrap can fail once.
    bool invalid = false;

protected:
    // Register the typed replacement seam.
    static void _bind_methods();

public:
    // Supply a factory for the session's gameplay transport before boot.
    godot::Error session_transport(const godot::Callable &factory);
};

// Optional code-first provider that customizes typed bindings before a game attaches.
class GameProvider : public godot::Resource {
    GDCLASS(GameProvider, godot::Resource)

    friend class GameConfig;

protected:
    // Let a game register replacements on its fresh binding draft.
    GDVIRTUAL1(_register, godot::Ref<GameBindings>)
    // Register the provider hook.
    static void _bind_methods();
};

// Branchable setup for one code-first game scope.
class GameConfig : public godot::RefCounted {
    GDCLASS(GameConfig, godot::RefCounted)

    friend class Game;

    // Detached legacy engine settings used by both public entry points.
    godot::Ref<GameDefinition> definition;
    // Optional provider for typed code-first overrides.
    godot::Ref<GameProvider> overrides;
    // Optional coordinated inventory feature settings.
    godot::Ref<InventoryConfig> inventory_config;
    // Optional explicit fixed-world movement settings replacing scene discovery.
    godot::Ref<PlayerControlProfile> control_profile;
    // Whether to enable fixed-world movement from the profile or the player's Mover component.
    bool use_player_controls = false;
    // Whether play starts the coordinated authority lifecycle.
    bool coordinated = false;
    // Whether this draft selected an online directory rather than a direct transport.
    bool online = false;
    // First invalid configuration change, carried into the boot result.
    godot::String invalid;

    // Copy only the detached settings and explicit choices.
    godot::Ref<GameConfig> branch() const;
    // Add or replace a world on a detached coordinated draft.
    static bool put_world(godot::Dictionary &worlds, const godot::Ref<WorldDefinition> &world);

protected:
    // Register the branchable settings and boot terminal.
    static void _bind_methods();

public:
    // Create an empty draft for native and script construction.
    GameConfig();
    // Release stored settings after their concrete types are complete.
    ~GameConfig() override;
    // Adapt saved or legacy settings without changing their original resource.
    static godot::Ref<GameConfig> from_definition(const godot::Ref<GameDefinition> &definition);
    // Select direct multiplayer and set total capacity, including the listen host.
    godot::Ref<GameConfig> lan(int64_t players = 2) const;
    // Select Steam listen and guest play using the launch context by default.
    godot::Ref<GameConfig> steam(const godot::Ref<steam::SteamConfig> &settings = {}, int64_t players = 2) const;
    // Select a configured Steam dedicated server and remote capacity.
    godot::Ref<GameConfig> steam_server(const godot::Ref<steam::SteamServer> &settings, int64_t players = 2) const;
    // Override the UDP port for a configured direct multiplayer profile.
    godot::Ref<GameConfig> on_port(int64_t port) const;
    // Assign a durable starting world ID to a fixed-world draft.
    godot::Ref<GameConfig> world_id(const godot::StringName &id) const;
    // Add a world to a coordinated game draft.
    godot::Ref<GameConfig> with_world(const godot::Ref<WorldDefinition> &world) const;
    // Select an optional typed service provider.
    godot::Ref<GameConfig> provider(const godot::Ref<GameProvider> &provider) const;
    // Enable a coordinated inventory feature on this draft.
    godot::Ref<GameConfig> inventory(const godot::Ref<InventoryConfig> &settings) const;
    // Enable authoritative movement using an explicit profile or the player scene's component.
    godot::Ref<GameConfig> player_controls(const godot::Ref<PlayerControlProfile> &profile = {}) const;
    // Attach to an existing scene owner and return a typed handle even when boot fails.
    godot::Ref<Game> boot(godot::Node *owner) const;
    // Attach a game and its native dedicated launcher for the explicit server role.
    godot::Ref<Game> boot_server(godot::Node *owner) const;
};

// Direct authority-aware world actions on the existing scoped scene service.
class GameScenes : public godot::RefCounted {
    GDCLASS(GameScenes, godot::RefCounted)

    friend class GameConfig;

    // Exact originating scene service, which invalidates on retirement.
    godot::Ref<SceneService> service;

protected:
    // Register direct scene loading and the mounted world lookup.
    static void _bind_methods();

public:
    // Travel to a registered destination and await its typed terminal result.
    godot::Variant load(const godot::Ref<WorldDefinition> &world);
    // Return the currently mounted world while this scene service is live.
    godot::Node *get_world() const;
};

// Code-first scoped facade. Godot owns the runtime node; this handle never keeps it alive.
class Game : public godot::RefCounted {
    GDCLASS(Game, godot::RefCounted)

    friend class GameConfig;
    friend class GameServiceContainer;

    // Weak ID of the runtime bound to this facade.
    godot::ObjectID game_id;
    // Exact attachment identity from the service container.
    godot::Ref<SceneService> attachment;
    // Startup validation outcome, retained for a failed or detached facade.
    godot::Ref<GameResult> boot_outcome;
    // Direct scene actions sharing the originating scene service.
    godot::Ref<GameScenes> scene_actions;
    // Coordinated inventory service, when configured.
    godot::Ref<GameInventory> inventory_service;
    // Game-owned registrations shared across the lifetime of this attachment.
    godot::Ref<replication::NetworkCodecs> codec_service;
    // Steam capability facade resolved once per game attachment.
    godot::Ref<steam::Steamworks> steam_service;
    // Pending leave request targeting the currently retiring session.
    godot::Ref<GameOperation> leave_operation;
    // Whether play uses the coordinated authority lifecycle.
    bool coordinated = false;
    // Whether hosting uses the configured online provider.
    bool online = false;

    // Resolve only the original published attachment on the scene thread.
    GameRuntime *resolve_game() const;
    // Return the native startup outcome immediately or await its typed completion.
    static godot::Variant await_operation(const godot::Ref<GameOperation> &operation);
    // Reuse the boot failure when an action has no live game.
    godot::Ref<GameResult> reject(const godot::String &code, const godot::String &message) const;
    // Select the configured directory for a host action.
    bool uses_online() const;
    // Register configured authority-owned feature stores before entering a coordinated lobby.
    godot::Error prepare_authority();
    // Observe the invitation gateway belonging to the exact attached provider.
    void observe_invites(GameRuntime &game);
    // Relay an invitation snapshot change to the code-first facade.
    void on_invite_changed(const godot::Ref<SteamInvite> &invite);
    // Publish readiness changes from the underlying runtime.
    void on_phase_changed(int64_t phase);
    // Relay a newly admitted peer for the coordinated game's identity policy.
    void on_peer_joined(int64_t peer_id);
    // Publish retirement and settle the leave targeting that session.
    void on_session_ended(const godot::StringName &reason);
    // Publish the mounted world readiness boundary.
    void on_world_ready(const godot::StringName &world, int64_t epoch);
    // Settle a pending leave when its scope leaves the tree.
    void on_scope_closed();

protected:
    // Register the code-first action surface and lifecycle observations.
    static void _bind_methods();

public:
    // Compatible readiness values for menus and game scripts.
    enum Phase { IDLE, LOBBY, TRANSITIONING, RUNNING, RECOVERING };

    // Create an initially detached code-first handle.
    Game();
    // Release scoped service references.
    ~Game() override;
    // Create a fixed-world game draft with offline defaults.
    static godot::Ref<GameConfig> configure(const godot::Ref<godot::PackedScene> &level,
            const godot::Ref<godot::PackedScene> &player);
    // Create a coordinated draft with one authored starting world.
    static godot::Ref<GameConfig> campaign(const godot::Ref<WorldDefinition> &start);
    // Resolve the code-first handle for the selected or sole live game.
    static godot::Ref<Game> current();
    // Resolve a code-first game from its own scene branch.
    static godot::Ref<Game> for_node(godot::Node *node);
    // Select a live game for global access, or clear the explicit selection.
    static godot::Error set_default(const godot::Ref<Game> &game);
    // Return the typed result of booting this handle.
    godot::Ref<GameResult> boot_result() const;
    // Start the selected offline game and settle after its world is playable.
    godot::Variant play();
    // Host with the configured direct or online connection.
    godot::Variant host(const godot::String &bind_address = "*", const godot::String &public_address = {});
    // Join the given direct-address host.
    godot::Variant join(const godot::String &address);
    // Start an explicitly chosen dedicated role.
    godot::Variant serve(const godot::String &bind_address = "*", const godot::String &public_address = {});
    // Search the configured provider and attempt one authenticated join.
    godot::Variant find_and_join();
    // Join the exact typed listing selected by a custom browser.
    godot::Variant join_listing(const godot::Ref<OnlineListing> &listing);
    // Open an explicit coordinated ENet lobby without starting the world.
    godot::Ref<GameResult> open_lobby(const godot::String &bind_address = "*");
    // Begin admission to a coordinated ENet lobby.
    godot::Ref<GameResult> join_lobby(const godot::String &address);
    // Bind a verified identity to an admitted peer before the coordinated match.
    godot::Error bind_player(int64_t peer_id, const godot::String &player_id);
    // Start the coordinated world once the authority has bound its players.
    godot::Variant start_match(const godot::StringName &world = {});
    // Open the invite chooser for the owned lobby.
    godot::Ref<GameResult> invite_friends();
    // Accept the exact pending offer shown to the player.
    godot::Variant accept_invite(const godot::Ref<SteamInvite> &invite);
    // Read the pending offer, or null if there is none.
    godot::Ref<SteamInvite> pending_invite() const;
    // Dismiss the exact pending offer without joining it.
    godot::Ref<GameResult> dismiss_invite(const godot::Ref<SteamInvite> &invite);
    // Cancel the pending start or leave the active session, settling after retirement.
    godot::Variant leave();
    // Route a gameplay command through the coordinated authority.
    godot::Variant perform(const godot::String &name, const godot::Dictionary &payload = {});
    // Register one game-owned coordinated command before play.
    godot::Error register_command(const godot::String &name, const godot::Callable &handler, bool requires_primary = false);
    // Resolve the authority coordinator to register game-owned stores.
    godot::Ref<match_runtime::TransactionCoordinator> transactions() const;
    // Select a save slot on the same coordinated game.
    godot::Ref<SaveSlot> save_slot(const godot::String &profile, const godot::String &slot);
    // Resolve direct scene actions for this attachment.
    godot::Ref<GameScenes> scenes() const;
    // Resolve the configured coordinated inventory service.
    godot::Ref<GameInventory> inventory() const;
    // Return the game-owned gameplay codec registry while this attachment is live.
    godot::Ref<replication::NetworkCodecs> codecs() const;
    // Resolve the matching game Steam scope.
    godot::Ref<steam::Steamworks> steam() const;
    // Sample the exact local game's connection and movement without mutating either.
    godot::Ref<GameNetworkDiagnostics> network_diagnostics() const;
    // Report current readiness or IDLE after retirement.
    Phase get_phase() const;
    // Whether this attachment configured fixed-world player movement.
    bool is_player_controlled() const;
    // Return the configured direct gameplay port, or zero when unavailable.
    int64_t get_port() const;
    // Return the mounted world until travel or teardown.
    godot::Node *get_world() const;
    // Return the local spawned actor, when available.
    godot::Node3D *get_local_player() const;
    // Return a spawned actor by session peer ID.
    godot::Node3D *get_player(int64_t peer_id) const;
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::Game::Phase);
