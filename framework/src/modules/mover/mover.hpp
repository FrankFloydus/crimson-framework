// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/object_id.hpp>

namespace crimson::mover {

class MoverDriver;

// Numeric locomotion settings copied into both physics drivers.
struct GroundedSettings {
    godot::real_t acceleration = 40.0; ///< Ground steering acceleration in metres per second squared.
    godot::real_t deceleration = 40.0; ///< Ground braking rate in metres per second squared before friction.
    godot::real_t ground_friction = 8.0; ///< Ground turning and braking coefficient in 1/s.
    godot::real_t air_control = 0.4; ///< Fraction of steering acceleration available in air.
    godot::real_t jump_speed = 5.0; ///< Grounded takeoff speed in m/s.

    // Advance planar velocity from collision-resolved state toward the requested steering.
    godot::Vector3 steer(const godot::Vector3 &current, const godot::Vector3 &target,
            bool on_floor, godot::real_t delta) const;
    // Resolve the next upward speed using gravity or a grounded takeoff.
    godot::real_t rise(godot::real_t current, godot::real_t gravity, godot::real_t delta, bool takeoff) const;
};

// Shareable configuration for the neutral or grounded motion rule.
class MotionRule : public godot::Resource {
    GDCLASS(MotionRule, godot::Resource)

    // Whether steering follows the CharacterBody3D support plane and applies gravity.
    bool grounded = false;
    // Multiplier applied to gravity along the body's up axis in grounded mode.
    double gravity_scale = 1.0;
    // Numeric settings shared by the live and replayable drivers.
    GroundedSettings settings;

protected:
    // Register the editable rule settings.
    static void _bind_methods();

public:
    // Enable or disable support-plane steering and gravity.
    void set_grounded(bool enabled);
    // Report whether the rule uses grounded motion.
    bool is_grounded() const;
    // Set a finite nonnegative multiplier for grounded gravity.
    void set_gravity_scale(double scale);
    // Read the grounded gravity multiplier.
    double get_gravity_scale() const;
    // Set and read the maximum grounded steering acceleration in metres per second squared.
    void set_acceleration(double value);
    double get_acceleration() const;
    // Set and read the base grounded braking rate in metres per second squared.
    void set_deceleration(double value);
    double get_deceleration() const;
    // Set and read the ground turning and braking coefficient in 1/s.
    void set_ground_friction(double value);
    double get_ground_friction() const;
    // Set and read the airborne steering fraction from zero to one.
    void set_air_control(double value);
    double get_air_control() const;
    // Set the grounded jump speed from 0 to 1000 m/s.
    void set_jump_speed(double speed);
    // Read the grounded takeoff speed in m/s.
    double get_jump_speed() const;
    // Read the settings value without retaining the source resource.
    const GroundedSettings &get_grounded_settings() const;
    // Copy every rule setting for a session, with the standard grounded rule as fallback.
    static godot::Ref<MotionRule> snapshot(const godot::Ref<MotionRule> &authored);
};

// Factory for the built-in locomotion rules.
class Locomotion : public godot::Object {
    GDCLASS(Locomotion, godot::Object)

protected:
    // Register the grounded rule factory.
    static void _bind_methods();

public:
    // Create a grounded rule using standard body gravity, floor collision, and a 5 m/s jump.
    static godot::Ref<MotionRule> ground();
};

// Code-first movement handle for one scene body. The scene owns the driver.
class Mover : public godot::RefCounted {
    GDCLASS(Mover, godot::RefCounted)

    friend class MoverDriver;

    // Weak identity of the private driver so a retained handle cannot keep the body alive.
    godot::ObjectID driver_id;
    // Most recent world-space steering request in metres per second.
    godot::Vector3 steering;
    // Native controller that last wrote steering, or zero for a direct public request.
    godot::ObjectID steering_source;
    // World-space movement actually committed by the latest step, in metres per second.
    godot::Vector3 velocity;
    // Grounded rule configuration copied from the authored resource at activation.
    bool grounded = false;
    // Grounded gravity multiplier copied from the authored resource at activation.
    double gravity_scale = 1.0;
    // Collision-resolved movement velocity carried into the next live step.
    godot::Vector3 simulation_velocity;
    // Grounded tuning copied from the authored resource at activation.
    GroundedSettings settings;
    // One takeoff attempt to consume at the next live physics step.
    bool jump_requested = false;
    // Whether a native session advances the attachment with a replayable driver.
    bool external_steps = false;

    // Resolve the live driver attached to the original body.
    MoverDriver *get_driver() const;
    // Apply a validated rule snapshot and preserve vertical velocity within grounded mode.
    void configure_rule(const godot::Ref<MotionRule> &rule);
    // Advance one world-space motion step through the body's supported driver.
    void step(godot::Node3D *body, double delta);
    // Clear live motion when the body exits the tree.
    void clear_motion();

protected:
    // Register the typed attachment and movement API.
    static void _bind_methods();

public:
    // Control sources should process before this fixed-step driver priority.
    static constexpr int PHYSICS_PRIORITY = 100;

    // Attach once to an existing body and return its movement handle.
    static godot::Ref<Mover> attach(godot::Node3D *body, const godot::Ref<MotionRule> &rule = {});
    // Return the existing attachment for this exact body, if present.
    static godot::Ref<Mover> for_node(godot::Node3D *body);
    // Check the target and rule before creating an attachment or replacing a live rule.
    static godot::Error validate_attachment(godot::Node3D *body, const godot::Ref<MotionRule> &rule);

    // Set the persistent world-space steering velocity, in metres per second.
    godot::Error move(const godot::Vector3 &world_velocity);
    // Clear direct steering while the selected rule continues to advance.
    godot::Error stop();
    // Request a takeoff on the next live physics step if the body has floor support.
    godot::Error jump();
    // Read actual world velocity committed on the last physics step.
    godot::Vector3 get_velocity() const;
    // Switch to the supplied rule or restore the neutral default with null.
    godot::Error use_rule(const godot::Ref<MotionRule> &rule);

    // Set steering from one native controller and record the source for exact release.
    godot::Error move_owned(const godot::Vector3 &world_velocity, godot::ObjectID source);
    // Clear steering if the departing controller is still the current source.
    void release_steering(godot::ObjectID source);
    // Roll back a fresh attachment when an integration fails before publication.
    void retire_attachment();
    // Give the native session sole ownership of simulation stepping and the configured rule.
    bool begin_external_steps();
    // Read the latest steering submitted to an externally stepped attachment.
    godot::Vector3 get_external_steering() const;
    // Publish the actual velocity from the native session's latest committed step.
    void set_external_velocity(const godot::Vector3 &actual_velocity);
};

// Private child which owns one Mover attachment and advances its body in physics.
class MoverDriver : public godot::Node {
    GDCLASS(MoverDriver, godot::Node)

    friend class Mover;

    // Handle retained for the lifetime of the attachment, even if callers drop their copy.
    godot::Ref<Mover> mover;
    // Exact identity of the original body, checked independently of the scene parent.
    godot::ObjectID body_id;

    // Return the original body while it remains the driver's live parent.
    godot::Node3D *get_body() const;

protected:
    // The private driver exposes no script methods.
    static void _bind_methods();
    // Reset stepping state when the owner leaves the scene tree.
    void _notification(int what);

public:
    // Perform one live scene physics step after ordinary steering producers.
    void _physics_process(double delta) override;
};

} // namespace crimson::mover
