// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_operation.hpp"
#include "integration/game/multiplayer_rules.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/object.hpp>

namespace crimson::integration {

class GameConfig;
class GameRuntime;

// Composes a fixed-world dedicated LAN game from an authored server scene.
class DedicatedServerEntry : public godot::Node {
    GDCLASS(DedicatedServerEntry, godot::Node)

    godot::Ref<godot::PackedScene> level_scene; ///< World shared by the server and clients.
    godot::Ref<godot::PackedScene> player_scene; ///< Player scene used for remote actors.
    godot::StringName world_id = "start"; ///< Stable identifier shared with clients.
    godot::Ref<MultiplayerRules> rules; ///< LAN capacity, port, and admission policy.
    bool relay_between_guests = true; ///< Whether Godot forwards guest-to-guest RPCs.

protected:
    // Bind dedicated scene configuration to the Inspector.
    static void _bind_methods();

public:
    // Compose the game for the explicit server role and hand it to native startup.
    void _ready() override;

    void set_level_scene(const godot::Ref<godot::PackedScene> &value); ///< Assign the starting world.
    godot::Ref<godot::PackedScene> get_level_scene() const; ///< Read the starting world.
    void set_player_scene(const godot::Ref<godot::PackedScene> &value); ///< Assign the shared player scene.
    godot::Ref<godot::PackedScene> get_player_scene() const; ///< Read the shared player scene.
    void set_world_id(const godot::StringName &value); ///< Assign the durable world ID.
    godot::StringName get_world_id() const; ///< Read the durable world ID.
    void set_rules(const godot::Ref<MultiplayerRules> &value); ///< Assign the dedicated LAN rules.
    godot::Ref<MultiplayerRules> get_rules() const; ///< Read the dedicated LAN rules.
    void set_relay_between_guests(bool value); ///< Set the dedicated scene's Godot relay policy.
    bool get_relay_between_guests() const; ///< Read the dedicated scene's relay policy.
};

// Starts a configured GameRuntime as an explicit playerless authority from a headless launch.
class DedicatedServerBoot : public godot::Node {
    GDCLASS(DedicatedServerBoot, godot::Node)

    friend class GameConfig;

    // Weak reference to the code-first runtime owned by the scene.
    godot::ObjectID game_id;
    // Startup operation retained until its one terminal notification.
    godot::Ref<GameOperation> starting;
    // Effective UDP port announced to the launcher after startup.
    int64_t port = 0;
    // Local interface selected for the dedicated listener.
    godot::String bind_address;

    // Start the already configured server after the scene is ready.
    void start_server();
    // Announce readiness or exit with the native operation's error.
    void on_started(const godot::Dictionary &result);
    // Write the launcher error marker and exit with a failure status.
    static void fail(godot::SceneTree &tree, const godot::String &message);
    // Return the referenced game while it exists.
    GameRuntime *get_game() const;

protected:
    // This internal node has no script API.
    static void _bind_methods();

public:
    // Queue startup after the enclosing scene has finished attaching.
    void _ready() override;
    // Disconnect pending startup feedback before the boot scene leaves the tree.
    void _exit_tree() override;
};

} // namespace crimson::integration
