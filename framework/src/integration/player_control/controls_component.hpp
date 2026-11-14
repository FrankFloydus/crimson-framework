// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/player_control/player_control.hpp"

#include <godot_cpp/variant/packed_string_array.hpp>

namespace crimson::integration {

// Authors the local input and look configuration for its character body.
class ControlsComponent : public godot::Node {
    GDCLASS(ControlsComponent, godot::Node)

    // Reuse the profile's defaults and boot path while exposing local input settings only.
    godot::Ref<PlayerControlProfile> profile;

protected:
    // Register the Inspector settings and explicit local attachment API.
    static void _bind_methods();

public:
    // Initialize this component's private local profile.
    ControlsComponent();
    // Assign the resource that defines input actions and bindings.
    void set_input_layout(const godot::Ref<input::InputLayout> &value);
    // Read the Input Layout shared by the component and its runtime input scope.
    godot::Ref<input::InputLayout> get_input_layout() const;
    // Select the directional action for movement.
    void set_move_action(const godot::StringName &value);
    // Read the selected directional action ID.
    godot::StringName get_move_action() const;
    // Select the button action for grounded takeoff, or an empty ID to disable it.
    void set_jump_action(const godot::StringName &value);
    // Read the selected jump action ID.
    godot::StringName get_jump_action() const;
    // Select the mouse motion action for look, or an empty ID to disable it.
    void set_mouse_look_action(const godot::StringName &value);
    // Read the selected mouse look action ID.
    godot::StringName get_mouse_look_action() const;
    // Select the stick action for look, or an empty ID to disable it.
    void set_stick_look_action(const godot::StringName &value);
    // Read the selected stick look action ID.
    godot::StringName get_stick_look_action() const;
    // Set mouse look sensitivity in radians per pixel.
    void set_mouse_sensitivity(double value);
    // Read the mouse look sensitivity.
    double get_mouse_sensitivity() const;
    // Set stick look speed in radians per second at full deflection.
    void set_stick_look_speed(double value);
    // Read the stick look speed.
    double get_stick_look_speed() const;
    // Request pointer capture when local control attaches successfully.
    void set_capture_pointer_on_boot(bool value);
    // Read whether local control should request pointer capture.
    bool get_capture_pointer_on_boot() const;
    // Resolve the unique direct ControlsComponent of the supplied character body.
    static ControlsComponent *for_body(godot::Node3D *body);
    // Attach the profile to the parent body and its view once the player is local.
    godot::Ref<PlayerControl> boot(godot::Node3D *view, const godot::Ref<input::Controls> &controls = {}) const;
    // Explain how to fix invalid parentage, duplicate components, or missing resources.
    godot::PackedStringArray _get_configuration_warnings() const override;
};

} // namespace crimson::integration
