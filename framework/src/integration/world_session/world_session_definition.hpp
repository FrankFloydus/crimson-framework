// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <cstdint>

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/node_path.hpp>

namespace crimson::integration {

class WorldSession;

// Defines one playable 3D world and its default player spawning policy.
class WorldSessionDefinition : public godot::Resource {
    GDCLASS(WorldSessionDefinition, godot::Resource)

public:
    // ENet port used when a game leaves its definition at the default.
    static constexpr int64_t DEFAULT_PORT = 7000;

private:
    // Local asset mounted on every participant.
    godot::Ref<godot::PackedScene> world_scene;
    // Local actor asset instantiated in each occupied slot.
    godot::Ref<godot::PackedScene> player_scene;
    // World-relative node whose ordered Marker3D children define spawn slots.
    godot::NodePath spawn_points_path = godot::NodePath("SpawnPoints");
    // Actor-relative camera to make current for the local player, when assigned.
    godot::NodePath local_camera_path;
    // Actor-relative Node3D to hide in the local player's instance, when assigned.
    godot::NodePath local_hidden_path;
    // Default ENet UDP port used for hosting and joining.
    int64_t port = DEFAULT_PORT;

    // Clone the configuration while sharing its referenced scene assets.
    godot::Ref<WorldSessionDefinition> copy_definition() const;

protected:
    // Register fluent operations and matching Inspector properties.
    static void _bind_methods();

public:
    // Create a new configuration with Inspector defaults.
    static godot::Ref<WorldSessionDefinition> configure();
    // Return an independent configuration with the supplied local world.
    godot::Ref<WorldSessionDefinition> with_world(const godot::Ref<godot::PackedScene> &scene) const;
    // Return an independent configuration with an actor scene and its world-relative spawn container.
    godot::Ref<WorldSessionDefinition> with_players(const godot::Ref<godot::PackedScene> &scene,
            const godot::NodePath &spawn_path = godot::NodePath("SpawnPoints")) const;
    // Return an independent configuration with optional local camera and visibility setup.
    godot::Ref<WorldSessionDefinition> with_local_view(const godot::NodePath &camera_path,
            const godot::NodePath &hidden_path = {}) const;
    // Return an independent configuration with the supplied ENet port.
    godot::Ref<WorldSessionDefinition> on_port(int64_t value) const;
    // Create a detached WorldSession node with a copy of this configuration.
    WorldSession *create() const;

    // Set the authored world resource.
    void set_world_scene(const godot::Ref<godot::PackedScene> &scene);
    // Get the authored world resource.
    godot::Ref<godot::PackedScene> get_world_scene() const;
    // Set the authored player resource.
    void set_player_scene(const godot::Ref<godot::PackedScene> &scene);
    // Get the authored player resource.
    godot::Ref<godot::PackedScene> get_player_scene() const;
    // Set the world-relative path to the spawn container.
    void set_spawn_points_path(const godot::NodePath &path);
    // Get the world-relative path to the spawn container.
    godot::NodePath get_spawn_points_path() const;
    // Set the optional actor-relative path to the local camera.
    void set_local_camera_path(const godot::NodePath &path);
    // Get the optional actor-relative path to the local camera.
    godot::NodePath get_local_camera_path() const;
    // Set the optional actor-relative path to the local hidden visuals.
    void set_local_hidden_path(const godot::NodePath &path);
    // Get the optional actor-relative path to the local hidden visuals.
    godot::NodePath get_local_hidden_path() const;
    // Set the port to use for future network sessions.
    void set_port(int64_t value);
    // Get the configured network port.
    int64_t get_port() const;
};

} // namespace crimson::integration
