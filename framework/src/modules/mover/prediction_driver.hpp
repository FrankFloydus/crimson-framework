// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "mover.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/physics_test_motion_parameters3d.hpp>
#include <godot_cpp/classes/physics_test_motion_result3d.hpp>

#include <cstdint>

namespace crimson::mover {

// Complete state for one step of the default grounded static-world solver.
struct PredictionState {
    // World-space origin of the upright character body, in metres.
    godot::Vector3 position;
    // Resulting world-space simulation velocity, in metres per second.
    godot::Vector3 velocity;
    // Whether the capsule has walkable support at this step.
    bool grounded = false;
};

// Replayable upright-capsule motion against immutable static world collision.
class PredictionDriver {
    // A borrowed body RID supplies the capsule shape and authored collision mask.
    godot::RID body_rid;
    // Private scratch values reused for sweeps. They carry no state between steps.
    godot::Ref<godot::PhysicsTestMotionParameters3D> parameters;
    godot::Ref<godot::PhysicsTestMotionResult3D> result;
    // Constant gravity copied from the project and the authored rule at attachment.
    godot::real_t gravity = 0.0;
    // Immutable ground steering, air steering, and takeoff settings.
    GroundedSettings settings;
    // Minimum world-up normal component that counts as walkable support.
    godot::real_t walkable_normal = 0.0;
    // Maximum downward sweep distance used to follow a descending floor.
    godot::real_t floor_snap = 0.0;
    // Apply at most this many collision slides per step.
    int slide_limit = 0;
    // Fingerprint of effective solver settings used by the baseline handshake.
    int64_t signature = 0;

    // Find the first blocking static sweep. On a hit, travel and normal describe the result.
    bool sweep(const godot::Vector3 &position, const godot::Vector3 &motion,
            godot::Vector3 &travel, godot::Vector3 &remaining, godot::Vector3 &normal);

public:
    // Validate the built-in grounded rule, mounted upright body, and one centered capsule.
    static bool supports(godot::CharacterBody3D *body, const godot::Ref<MotionRule> &rule);
    // Capture immutable settings from a body accepted by supports().
    PredictionDriver(godot::CharacterBody3D *body, const godot::Ref<MotionRule> &rule);
    // Compare effective shape and movement settings across network peers.
    int64_t settings_signature() const;
    // Return the next state without moving any scene node or dispatching scene events.
    PredictionState step(const PredictionState &previous, const godot::Vector3 &world_steering,
            double fixed_delta, bool jump_pressed = false);
};

} // namespace crimson::mover
