// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <cstdint>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::integration {

// Serializable game setup shared by the Inspector and fluent configuration.
class CrimsonGameDefinition : public godot::Resource {
    GDCLASS(CrimsonGameDefinition, godot::Resource)

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

    // Copy the configuration through the same setters used by the Inspector.
    godot::Ref<CrimsonGameDefinition> copy_definition() const;

protected:
    // Registers the definition's Inspector properties and script methods.
    static void _bind_methods();

public:
    // Create a new definition with the same defaults as an Inspector resource.
    static godot::Ref<CrimsonGameDefinition> configure();
    // Return an independent definition with the supplied catalog.
    godot::Ref<CrimsonGameDefinition> with_worlds(const godot::Dictionary &p_worlds) const;
    // Return an independent definition with a different starting world.
    godot::Ref<CrimsonGameDefinition> starting_in(const godot::StringName &p_world_id) const;
    // Return an independent definition with the supplied persistence settings.
    godot::Ref<CrimsonGameDefinition> with_saves(const godot::String &p_app_id,
            const godot::String &p_root = "user://crimson", int64_t p_schema_version = 1) const;

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
};

} // namespace crimson::integration
