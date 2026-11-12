// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/multiplayer_rules.hpp"
#include "integration/game/game_services.hpp"

#include <cstdint>

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::integration {

class GameRuntime;

// Serializable game setup shared by the Inspector and fluent configuration.
class GameDefinition : public godot::Resource {
    GDCLASS(GameDefinition, godot::Resource)

    // Stable application identifier written into checkpoints.
    godot::String app_id = "crimson.game";
    // Root directory for this game's checkpoint storage.
    godot::String save_root = "user://crimson";
    // Version of the game state schema used by the checkpoint coordinator.
    int64_t schema_version = 1;
    // World IDs mapped to scene IDs and local scene resource paths.
    godot::Dictionary worlds;
    // Default world ID used to start a new match.
    godot::StringName starting_world;
    // Local starting scene used by the immediate-play actions.
    godot::Ref<godot::PackedScene> starting_world_scene;
    // Actor scene spawned for participants in immediate play.
    godot::Ref<godot::PackedScene> player_scene;
    // Listen-game settings. An empty resource permits local play only.
    godot::Ref<MultiplayerRules> multiplayer_rules;
    // Custom factories selected before the game attaches its native services.
    godot::Ref<GameServiceProvider> service_provider;

    // Copy the configuration through the same setters used by the Inspector.
    godot::Ref<GameDefinition> copy_definition() const;

protected:
    // Registers the definition's Inspector properties and script methods.
    static void _bind_methods();

public:
    // Create a new definition with the same defaults as an Inspector resource.
    static godot::Ref<GameDefinition> configure();
    // Return an independent definition with the supplied catalog.
    godot::Ref<GameDefinition> with_worlds(const godot::Dictionary &p_worlds) const;
    // Return an independent definition with a different starting world.
    godot::Ref<GameDefinition> starting_in(const godot::StringName &p_world_id) const;
    // Return an independent definition with the supplied persistence settings.
    godot::Ref<GameDefinition> with_saves(const godot::String &p_app_id,
            const godot::String &p_root = "user://crimson", int64_t p_schema_version = 1) const;
    // Return an independent definition with a named immediate-play world.
    godot::Ref<GameDefinition> with_starting_world(const godot::StringName &world_id,
            const godot::Ref<godot::PackedScene> &scene) const;
    // Return an independent definition with the supplied player scene.
    godot::Ref<GameDefinition> with_player(const godot::Ref<godot::PackedScene> &scene) const;
    // Return an independent definition with a copy of the supplied multiplayer rules.
    godot::Ref<GameDefinition> with_multiplayer(const godot::Ref<MultiplayerRules> &rules) const;
    // Return an independent definition using the selected game service factories.
    godot::Ref<GameDefinition> with_provider(const godot::Ref<GameServiceProvider> &provider) const;
    // Create a detached Game node with an independent copy of this definition.
    GameRuntime *create() const;
    // Attach a configured Game to a live scene-tree owner and return the surviving attachment.
    GameRuntime *boot(godot::Node *owner) const;

    // Sets the checkpoint application identifier for future matches.
    void set_app_id(const godot::String &p_app_id);
    // Returns the configured checkpoint application identifier.
    godot::String get_app_id() const;
    // Sets the checkpoint storage root for future matches.
    void set_save_root(const godot::String &p_save_root);
    // Returns the configured checkpoint storage root.
    godot::String get_save_root() const;
    // Sets the game state schema version for future matches.
    void set_schema_version(int64_t p_schema_version);
    // Returns the configured game state schema version.
    int64_t get_schema_version() const;
    // Copies the supplied catalog so edits to the input cannot change this definition.
    void set_worlds(const godot::Dictionary &p_worlds);
    // Returns a detached catalog of configured worlds.
    godot::Dictionary get_worlds() const;
    // Sets the default world ID for future matches.
    void set_starting_world(const godot::StringName &p_starting_world);
    // Returns the configured starting world ID.
    godot::StringName get_starting_world() const;
    // Set the scene used by immediate-play actions.
    void set_starting_world_scene(const godot::Ref<godot::PackedScene> &scene);
    // Get the authored immediate-play scene.
    godot::Ref<godot::PackedScene> get_starting_world_scene() const;
    // Set the immediate-play actor scene.
    void set_player_scene(const godot::Ref<godot::PackedScene> &scene);
    // Get the authored actor scene.
    godot::Ref<godot::PackedScene> get_player_scene() const;
    // Assign the multiplayer rule resource for future starts.
    void set_multiplayer_rules(const godot::Ref<MultiplayerRules> &rules);
    // Get the authored multiplayer rule resource.
    godot::Ref<MultiplayerRules> get_multiplayer_rules() const;
    // Select a provider before the game enters the scene tree.
    void set_service_provider(const godot::Ref<GameServiceProvider> &provider);
    // Return the selected provider, or null to use native defaults.
    godot::Ref<GameServiceProvider> get_service_provider() const;
};

} // namespace crimson::integration
