// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/mover/mover.hpp"

#include <godot_cpp/classes/node.hpp>

namespace crimson::mover {

// Authors the movement settings of one character scene.
class MoverComponent : public godot::Node {
    GDCLASS(MoverComponent, godot::Node)

public:
    // Selects the direction a controller treats as forward.
    enum MoveReference { VIEW, CHARACTER, WORLD };
    // Choose how input drives body facing and horizontal steering.
    enum ControllerStyle { FPS_AIM, FREE_ORBIT, FORWARD_ONLY, LOCK_ON };
    // Shared Inspector labels for scenes and explicit controller profiles.
    static constexpr const char *CONTROLLER_STYLE_LABELS =
            "FPS / Shoulder Aim,Free-Orbit Third Person,Forward-Only Third Person,Lock-On";

private:
    // Inline grounded settings authored on this scene component.
    godot::Ref<MotionRule> grounded_rule;
    // Requested movement speed at full input, in metres per second.
    double move_speed = 6.0;
    // Coordinate frame for directional movement input.
    MoveReference move_reference = VIEW;
    // Authored relationship between view, body facing, and travel.
    ControllerStyle controller_style = FPS_AIM;
    // Whether to use client prediction for compatible fixed-world sessions.
    bool predict_movement = false;

protected:
    // Register the authored settings and live mover query.
    static void _bind_methods();
    // Keep the legacy input frame editable only for the style that uses it.
    void _validate_property(godot::PropertyInfo &property) const;

public:
    // Create per-component grounded settings for direct Inspector authoring.
    MoverComponent();
    // Find a single direct component on a mounted or detached player scene root.
    static MoverComponent *find_on(godot::Node *body, bool &duplicate);
    // Report whether speed and reference fit the supported movement protocol.
    bool has_valid_settings() const;
    // Explain invalid authoring in the scene tree.
    godot::PackedStringArray _get_configuration_warnings() const override;

    // Copy the inline settings for standalone or session movement.
    godot::Ref<MotionRule> snapshot_rule() const;
    // Set and read the inline grounded acceleration in metres per second squared.
    void set_acceleration(double value);
    double get_acceleration() const;
    // Set and read the inline grounded braking rate in metres per second squared.
    void set_deceleration(double value);
    double get_deceleration() const;
    // Set and read the inline grounded turning and braking coefficient in 1/s.
    void set_ground_friction(double value);
    double get_ground_friction() const;
    // Set and read the inline gravity multiplier.
    void set_gravity_scale(double value);
    double get_gravity_scale() const;
    // Set and read the inline fraction of acceleration available in the air.
    void set_air_control(double value);
    double get_air_control() const;
    // Set and read the inline takeoff speed in m/s.
    void set_jump_speed(double value);
    double get_jump_speed() const;
    // Set full-input movement speed in metres per second.
    void set_move_speed(double value);
    // Read full-input movement speed in metres per second.
    double get_move_speed() const;
    // Select the frame that defines forward movement.
    void set_move_relative_to(MoveReference value);
    // Read the movement frame.
    MoveReference get_move_relative_to() const;
    // Set the style copied into new controllers and sessions.
    void set_controller_style(ControllerStyle value);
    // Return the authored controller style.
    ControllerStyle get_controller_style() const;
    // Enable prediction when the session and body support it.
    void set_predict_movement_enabled(bool enabled);
    // Report whether prediction is requested.
    bool is_predict_movement_enabled() const;
    // Return the existing movement handle on the parent body, if one is attached.
    godot::Ref<Mover> mover() const;
};

} // namespace crimson::mover

VARIANT_ENUM_CAST(crimson::mover::MoverComponent::MoveReference);
VARIANT_ENUM_CAST(crimson::mover::MoverComponent::ControllerStyle);
