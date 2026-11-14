// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "prediction_driver.hpp"

#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <cmath>
#include <cstring>

using namespace godot;

namespace crimson::mover {

namespace {

const Vector3 up(0, 1, 0);
constexpr int dynamic_exclusion_limit = 16;

// Hash the exact single-precision values used by both sides of the fixed-step solver.
uint32_t scalar_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Snapshot the effective project gravity along the upright capsule's world Y axis.
real_t grounded_gravity(const MotionRule &rule) {
    const auto *project = ProjectSettings::get_singleton();
    const real_t strength = project->get_setting_with_override("physics/3d/default_gravity");
    const Vector3 direction = project->get_setting_with_override("physics/3d/default_gravity_vector");
    return strength * direction.dot(up) * rule.get_gravity_scale();
}

} // namespace

bool PredictionDriver::supports(CharacterBody3D *body, const Ref<MotionRule> &rule) {
    if (body == nullptr || !body->is_inside_tree() || rule.is_null() || !rule->is_grounded() ||
            body->get_motion_mode() != CharacterBody3D::MOTION_MODE_GROUNDED ||
            !body->get_up_direction().is_equal_approx(up) || !std::isfinite(grounded_gravity(*rule.ptr()))) {
        return false;
    }
    const Basis basis = body->get_global_basis();
    if (!basis.is_equal_approx(basis.orthonormalized()) || !basis.get_column(1).is_equal_approx(up) ||
            basis.determinant() <= 0.0) {
        return false;
    }
    if (body->get_axis_lock(PhysicsServer3D::BODY_AXIS_LINEAR_X) ||
            body->get_axis_lock(PhysicsServer3D::BODY_AXIS_LINEAR_Y) ||
            body->get_axis_lock(PhysicsServer3D::BODY_AXIS_LINEAR_Z) ||
            !std::isfinite(body->get_floor_max_angle()) || !std::isfinite(body->get_floor_snap_length()) ||
            !std::isfinite(body->get_safe_margin())) {
        return false;
    }
    const PackedInt32Array owners = body->get_shape_owners();
    if (owners.size() != 1 || body->is_shape_owner_disabled(owners[0]) ||
            body->shape_owner_get_shape_count(owners[0]) != 1) {
        return false;
    }
    const Ref<CapsuleShape3D> capsule = body->shape_owner_get_shape(owners[0], 0);
    const Transform3D shape = body->shape_owner_get_transform(owners[0]);
    return capsule.is_valid() && shape.origin.is_finite() && shape.basis.is_equal_approx(Basis()) &&
            Math::is_zero_approx(shape.origin.x) && Math::is_zero_approx(shape.origin.z);
}

PredictionDriver::PredictionDriver(CharacterBody3D *body, const Ref<MotionRule> &rule) : body_rid(body->get_rid()) {
    gravity = grounded_gravity(*rule.ptr());
    settings = rule->get_grounded_settings();
    walkable_normal = Math::cos(body->get_floor_max_angle());
    floor_snap = body->get_floor_snap_length();
    slide_limit = Math::clamp(body->get_max_slides(), 1, 8);
    parameters.instantiate();
    parameters->set_margin(body->get_safe_margin());
    parameters->set_recovery_as_collision_enabled(true);
    result.instantiate();

    const int32_t owner = body->get_shape_owners()[0];
    const Ref<CapsuleShape3D> capsule = body->shape_owner_get_shape(owner, 0);
    const uint32_t fields[] = { scalar_bits(gravity), scalar_bits(walkable_normal), scalar_bits(floor_snap),
        static_cast<uint32_t>(slide_limit), body->get_collision_mask(), scalar_bits(body->get_safe_margin()),
        scalar_bits(capsule->get_radius()), scalar_bits(capsule->get_height()),
        scalar_bits(body->shape_owner_get_transform(owner).origin.y), scalar_bits(settings.jump_speed),
        scalar_bits(settings.acceleration), scalar_bits(settings.deceleration), scalar_bits(settings.ground_friction),
        scalar_bits(settings.air_control) };
    uint64_t hash = 14695981039346656037ULL;
    for (uint32_t field : fields) {
        hash = (hash ^ field) * 1099511628211ULL;
    }
    signature = static_cast<int64_t>(hash & 0x7fffffffffffffffULL);
}

int64_t PredictionDriver::settings_signature() const {
    return signature;
}

bool PredictionDriver::sweep(const Vector3 &position, const Vector3 &motion, Vector3 &travel, Vector3 &remaining,
        Vector3 &normal) {
    auto *physics = PhysicsServer3D::get_singleton();
    TypedArray<RID> excluded;
    parameters->set_from(Transform3D(Basis(), position));
    parameters->set_motion(motion);
    for (int attempt = 0; attempt <= dynamic_exclusion_limit; ++attempt) {
        parameters->set_exclude_bodies(excluded);
        if (!physics->body_test_motion(body_rid, parameters, result)) {
            travel = result->get_travel();
            return false;
        }
        const RID collider = result->get_collider_rid(0);
        if (collider.is_valid() && physics->body_get_mode(collider) == PhysicsServer3D::BODY_MODE_STATIC) {
            travel = result->get_travel();
            remaining = result->get_remainder();
            normal = result->get_collision_normal(0);
            return true;
        }
        if (!collider.is_valid() || excluded.has(collider)) {
            break;
        }
        excluded.append(collider);
    }

    // An unresolved crowded query cannot advance through a possible static obstruction.
    travel = Vector3();
    remaining = Vector3();
    normal = Vector3();
    return true;
}

PredictionState PredictionDriver::step(const PredictionState &previous, const Vector3 &world_steering,
        double fixed_delta, bool jump_pressed) {
    PredictionState next = previous;
    next.grounded = false;
    const real_t delta = static_cast<real_t>(fixed_delta);
    const bool takeoff = jump_pressed && previous.grounded && settings.jump_speed > 0.0;
    real_t vertical_speed = settings.rise(previous.velocity.y, gravity, delta, takeoff);
    Vector3 planar_velocity = settings.steer(previous.velocity.slide(up), world_steering.slide(up),
            previous.grounded && !takeoff, delta);
    Vector3 remaining = (planar_velocity + up * vertical_speed) * delta;

    for (int slide = 0; slide < slide_limit && !remaining.is_zero_approx(); ++slide) {
        Vector3 travel;
        Vector3 remainder;
        Vector3 normal;
        const bool hit = sweep(next.position, remaining, travel, remainder, normal);
        next.position += travel;
        if (!hit) {
            break;
        }

        const real_t support = normal.dot(up);
        if (support >= walkable_normal && support > 0.0 && vertical_speed <= 0.0) {
            next.grounded = true;
            vertical_speed = 0.0;
            const Vector3 along_floor = remainder.slide(up);
            remaining = along_floor.slide(normal).normalized() * along_floor.length();
        } else {
            if (support < 0.0 && vertical_speed > 0.0) {
                vertical_speed = 0.0;
            }
            remaining = remainder.slide(normal);
            if (normal.is_zero_approx()) {
                planar_velocity = Vector3();
            } else {
                const Vector3 planar_normal = normal.slide(up).normalized();
                if (planar_velocity.dot(planar_normal) < 0.0) {
                    planar_velocity = planar_velocity.slide(planar_normal);
                }
            }
        }
    }

    if (vertical_speed <= 0.0 && (next.grounded || previous.grounded || gravity == 0.0)) {
        Vector3 travel;
        Vector3 normal;
        const real_t probe_distance = Math::max(floor_snap, static_cast<real_t>(0.001));
        next.grounded = sweep(next.position, -up * probe_distance, travel, remaining, normal) &&
                normal.dot(up) >= walkable_normal && normal.dot(up) > 0.0;
        if (next.grounded) {
            if (floor_snap > 0.0) {
                next.position += travel;
            }
            vertical_speed = 0.0;
        }
    }

    next.velocity = planar_velocity;
    next.velocity.y = vertical_speed;
    return next;
}

} // namespace crimson::mover
