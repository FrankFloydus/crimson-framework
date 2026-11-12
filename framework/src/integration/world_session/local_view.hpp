// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/object.hpp>

namespace crimson::integration {

// Declares the local camera and optional hidden visuals inside a player scene.
class LocalPlayerView : public godot::Node {
    GDCLASS(LocalPlayerView, godot::Node)

    // Weak scene-local reference to the configured camera.
    godot::ObjectID camera_id;
    // Weak scene-local reference to the visuals hidden for the local player.
    godot::ObjectID hidden_visuals_id;

protected:
    // Register typed node properties for same-scene Inspector picking.
    static void _bind_methods();

public:
    // Set the camera used when this player is local.
    void set_camera(godot::Camera3D *camera);
    // Resolve the camera while its scene instance is alive.
    godot::Camera3D *get_camera() const;
    // Set the optional visual subtree hidden for this player's own view.
    void set_hidden_visuals(godot::Node3D *visuals);
    // Resolve the visual subtree while its scene instance is alive.
    godot::Node3D *get_hidden_visuals() const;
};

} // namespace crimson::integration
