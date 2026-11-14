// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "modules/input/controls.hpp"
#include "modules/mover/mover_component.hpp"

namespace crimson::integration {

class PlayerControlDriver;

/// One steering decision shared by local, authoritative, and replayed movement.
struct ControllerStep {
    godot::Vector3 velocity; ///< Requested horizontal world velocity in m/s.
    double body_yaw; ///< Body heading in radians, independent of view yaw.
};

/// Resolve one Y-up controller tick using the previous committed heading and optional lock heading.
ControllerStep step_controller(mover::MoverComponent::ControllerStyle style, int reference,
        const godot::Basis &character_basis, const godot::Vector2 &axes, double view_yaw,
        double previous_body_yaw, double speed, double delta, bool has_lock_heading = false,
        double lock_heading = 0.0);

/// Handle for one local controller attached to an existing body.
class PlayerControl : public godot::RefCounted {
    GDCLASS(PlayerControl, godot::RefCounted)
    friend class PlayerControlProfile;

    godot::Ref<input::Controls> controls_scope; ///< The exact input scope shared with settings.
    godot::Ref<mover::Mover> body_mover; ///< The body's single movement owner.
    godot::ObjectID driver_id; ///< The private controller follows the body lifetime.
    godot::Error error = godot::ERR_UNCONFIGURED; ///< Initial attachment result.
    godot::String message; ///< Specific failure to show at the boot boundary.

protected:
    /// Publish the typed local handle.
    static void _bind_methods();

public:
    /// Return true while the controller and its input scope are live.
    bool is_ready() const;
    /// Return the initial attachment error.
    godot::Error get_error() const;
    /// Return the initial attachment diagnostic.
    godot::String get_message() const;
    /// Return the controls used by this controller.
    godot::Ref<input::Controls> controls() const;
    /// Return the mover used by this controller.
    godot::Ref<mover::Mover> mover() const;
    /// Follow a game-selected node as a facing target for Lock-On steering.
    godot::Error lock_on(godot::Node3D *target);
    /// Return Lock-On steering to the view heading.
    void clear_lock_on();
};

/// Reusable settings for standard movement and look on a local player.
class PlayerControlProfile : public godot::Resource {
    GDCLASS(PlayerControlProfile, godot::Resource)
    friend class PlayerControlDriver;

public:
    /// Choose which direction the move action treats as forward.
    enum MoveReference { VIEW, CHARACTER, WORLD };

private:
    godot::Ref<input::InputLayout> input_layout; ///< Authored local input defaults.
    godot::Ref<mover::MotionRule> movement_rule; ///< Optional rule replacing default grounded locomotion.
    godot::StringName move_action = "move"; ///< Two-axis directional action.
    godot::StringName jump_action = "jump"; ///< Optional button action for grounded takeoff.
    godot::StringName mouse_look_action = "mouse_aim"; ///< Optional relative pointer action.
    godot::StringName stick_look_action = "stick_aim"; ///< Optional two-axis stick action.
    MoveReference move_reference = VIEW; ///< Reference used to resolve forward and right.
    mover::MoverComponent::ControllerStyle controller_style = mover::MoverComponent::FPS_AIM; ///< Facing and steering style.
    double move_speed = 6.0; ///< Requested speed at full input, in m/s.
    bool predict_movement = false; ///< Opt into replayable grounded movement against immutable static collision.
    double mouse_sensitivity = 0.002; ///< Mouse look in radians per unscaled pixel.
    double stick_look_speed = 2.5; ///< Stick look in radians per second at full deflection.
    bool capture_pointer_on_boot = false; ///< Whether the controller claims the desktop pointer.

protected:
    /// Publish Inspector fields and code-first setup.
    static void _bind_methods();
    /// Hide the legacy input frame when a style supplies its own.
    void _validate_property(godot::PropertyInfo &property) const;

public:
    /// Construct an editable profile with the standard action names.
    static godot::Ref<PlayerControlProfile> configure(const godot::Ref<input::InputLayout> &layout);
    /// Set the resource that defines input actions and bindings.
    void set_input_layout(const godot::Ref<input::InputLayout> &value);
    /// Read the authored input layout.
    godot::Ref<input::InputLayout> get_input_layout() const;
    /// Override the standard grounded movement rule.
    void set_movement_rule(const godot::Ref<mover::MotionRule> &value);
    /// Read the authored movement rule, if supplied.
    godot::Ref<mover::MotionRule> get_movement_rule() const;
    /// Select the action that drives movement.
    void set_move_action(const godot::StringName &value);
    /// Read the directional action ID.
    godot::StringName get_move_action() const;
    /// Select the button action that requests a jump, or an empty ID to disable it.
    void set_jump_action(const godot::StringName &value);
    /// Read the jump action ID.
    godot::StringName get_jump_action() const;
    /// Select the action that supplies mouse motion.
    void set_mouse_look_action(const godot::StringName &value);
    /// Read the mouse look action ID.
    godot::StringName get_mouse_look_action() const;
    /// Select the action that supplies stick deflection.
    void set_stick_look_action(const godot::StringName &value);
    /// Read the stick look action ID.
    godot::StringName get_stick_look_action() const;
    /// Select the frame that defines forward movement.
    void set_move_relative_to(MoveReference value);
    /// Read the movement frame.
    MoveReference get_move_relative_to() const;
    /// Choose how the body turns and travels when the profile supplies movement settings.
    void set_controller_style(mover::MoverComponent::ControllerStyle value);
    /// Return the authored facing and steering style.
    mover::MoverComponent::ControllerStyle get_controller_style() const;
    /// Set full-input speed in m/s.
    void set_move_speed(double value);
    /// Read full-input speed in m/s.
    double get_move_speed() const;
    /// Choose the fixed-world session's prediction preference before boot.
    void set_predict_movement_enabled(bool enabled);
    /// Report whether the profile requests prediction for compatible fixed-world play.
    bool is_predict_movement_enabled() const;
    /// Set mouse look sensitivity in radians per pixel.
    void set_mouse_sensitivity(double value);
    /// Read mouse sensitivity.
    double get_mouse_sensitivity() const;
    /// Set stick look speed in radians per second.
    void set_stick_look_speed(double value);
    /// Read stick look speed.
    double get_stick_look_speed() const;
    /// Request pointer capture on successful boot.
    void set_capture_pointer_on_boot(bool value);
    /// Read the initial pointer capture preference.
    bool get_capture_pointer_on_boot() const;
    /// Check movement settings without requiring an Input Layout on the dedicated authority.
    bool has_valid_motion_settings() const;
    /// Attach to a mounted body, using supplied local controls or creating a body-scoped instance.
    godot::Ref<PlayerControl> boot(godot::Node3D *body, godot::Node3D *view,
            const godot::Ref<input::Controls> &existing = {}) const;
};

/// Private update node that submits local steering before the mover's physics step.
class PlayerControlDriver : public godot::Node {
    GDCLASS(PlayerControlDriver, godot::Node)
    friend class PlayerControlProfile;
    friend class PlayerControl;

    godot::Ref<PlayerControl> player; ///< Keeps the published handle alive for the body lifetime.
    godot::ObjectID body_id; ///< Movement target.
    godot::ObjectID view_id; ///< Aim node and view-relative frame.
    godot::ObjectID movement_session_id; ///< Optional authority-side player-motion route.
    godot::ObjectID lock_target_id; ///< Weak local reference to the selected facing target.
    godot::StringName move_action; ///< Validated directional action.
    godot::StringName jump_action; ///< Validated jump button, if available.
    godot::StringName mouse_look_action; ///< Validated relative action, if enabled.
    godot::StringName stick_look_action; ///< Validated stick action, if enabled.
    PlayerControlProfile::MoveReference move_reference = PlayerControlProfile::VIEW; ///< Direction frame.
    mover::MoverComponent::ControllerStyle controller_style = mover::MoverComponent::FPS_AIM; ///< Standalone steering style.
    double move_speed = 0.0; ///< Full-input speed in m/s.
    double mouse_sensitivity = 0.0; ///< Radians per unscaled pixel.
    double stick_look_speed = 0.0; ///< Radians per second.
    bool attached = false; ///< True after the controller claims its input scope.

    /// Resolve the active standalone or session steering style.
    mover::MoverComponent::ControllerStyle active_style() const;

protected:
    /// This implementation node has no public API.
    static void _bind_methods();

public:
    /// Report whether the body and view still belong to the live attachment.
    bool is_attached() const;
    /// Translate the current local action frame into aim and steering.
    void _physics_process(double delta) override;
    /// Clear this controller's steering and pointer claim when the body exits.
    void _exit_tree() override;
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::PlayerControlProfile::MoveReference);
