// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "mover.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <utility>

using namespace godot;

namespace crimson::mover {

namespace {

// Scene tree attachment and steering state belong to the main thread.
bool on_main_thread() {
    const OS *os = OS::get_singleton();
    return os != nullptr && os->get_thread_caller_id() == os->get_main_thread_id();
}

// Leave the driver in the body's processing group so it can write that body's pose.
bool uses_main_thread_group(const Node *node) {
    while (node != nullptr) {
        const Node::ProcessThreadGroup group = node->get_process_thread_group();
        if (group != Node::PROCESS_THREAD_GROUP_INHERIT) {
            return group == Node::PROCESS_THREAD_GROUP_MAIN_THREAD;
        }
        node = node->get_parent();
    }
    return true;
}

// Preserve the previous value when a script submits an invalid tuning setting.
void set_rate(real_t &setting, double value, double maximum, const char *name) {
    if (!std::isfinite(value) || value < 0.0 || value > maximum) {
        UtilityFunctions::push_error(String("[Crimson] Invalid ") + name + ". Use a finite value from 0 to " +
                String::num(maximum) + ".");
        return;
    }
    setting = static_cast<real_t>(value);
}

} // namespace

Vector3 GroundedSettings::steer(const Vector3 &current, const Vector3 &target, bool on_floor, real_t delta) const {
    const real_t desired_speed = target.length();
    const real_t speed = current.length();
    if (!on_floor) {
        if (desired_speed <= CMP_EPSILON || air_control == 0.0) {
            return current;
        }
        const Vector3 direction = target / desired_speed;
        const real_t headroom = MAX(desired_speed - current.dot(direction), real_t(0.0));
        const real_t change = MIN(acceleration * air_control * delta, headroom);
        return (current + direction * change).limit_length(MAX(speed, desired_speed));
    }

    Vector3 next = current;
    if (desired_speed > CMP_EPSILON) {
        next = next.lerp(target * (speed / desired_speed), MIN(ground_friction * delta, real_t(1.0)));
    }
    const real_t adjusted_speed = next.length();
    if (adjusted_speed > desired_speed) {
        const real_t braking = (deceleration + ground_friction * adjusted_speed) * delta;
        return next * (MAX(desired_speed, adjusted_speed - braking) / adjusted_speed);
    }
    if (desired_speed > CMP_EPSILON) {
        next = next.move_toward(target, acceleration * delta);
    }
    return next;
}

real_t GroundedSettings::rise(real_t current, real_t gravity, real_t delta, bool takeoff) const {
    return takeoff ? jump_speed : current + gravity * delta;
}

void MotionRule::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_grounded", "enabled"), &MotionRule::set_grounded);
    ClassDB::bind_method(D_METHOD("is_grounded"), &MotionRule::is_grounded);
    ClassDB::bind_method(D_METHOD("set_gravity_scale", "scale"), &MotionRule::set_gravity_scale);
    ClassDB::bind_method(D_METHOD("get_gravity_scale"), &MotionRule::get_gravity_scale);
    ClassDB::bind_method(D_METHOD("set_acceleration", "value"), &MotionRule::set_acceleration);
    ClassDB::bind_method(D_METHOD("get_acceleration"), &MotionRule::get_acceleration);
    ClassDB::bind_method(D_METHOD("set_deceleration", "value"), &MotionRule::set_deceleration);
    ClassDB::bind_method(D_METHOD("get_deceleration"), &MotionRule::get_deceleration);
    ClassDB::bind_method(D_METHOD("set_ground_friction", "value"), &MotionRule::set_ground_friction);
    ClassDB::bind_method(D_METHOD("get_ground_friction"), &MotionRule::get_ground_friction);
    ClassDB::bind_method(D_METHOD("set_air_control", "value"), &MotionRule::set_air_control);
    ClassDB::bind_method(D_METHOD("get_air_control"), &MotionRule::get_air_control);
    ClassDB::bind_method(D_METHOD("set_jump_speed", "speed"), &MotionRule::set_jump_speed);
    ClassDB::bind_method(D_METHOD("get_jump_speed"), &MotionRule::get_jump_speed);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "grounded"), "set_grounded", "is_grounded");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"),
            "set_gravity_scale", "get_gravity_scale");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration", PROPERTY_HINT_RANGE, String(U"0,1000,0.1,suffix:m/s\u00B2")),
            "set_acceleration", "get_acceleration");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deceleration", PROPERTY_HINT_RANGE, String(U"0,1000,0.1,suffix:m/s\u00B2")),
            "set_deceleration", "get_deceleration");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_friction", PROPERTY_HINT_RANGE, "0,100,0.1,suffix:1/s"),
            "set_ground_friction", "get_ground_friction");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "air_control", PROPERTY_HINT_RANGE, "0,1,0.01"),
            "set_air_control", "get_air_control");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_speed", PROPERTY_HINT_RANGE, "0,1000,0.1,suffix:m/s"),
            "set_jump_speed", "get_jump_speed");
}

void MotionRule::set_grounded(bool enabled) {
    grounded = enabled;
}

bool MotionRule::is_grounded() const {
    return grounded;
}

void MotionRule::set_gravity_scale(double scale) {
    if (!std::isfinite(scale) || scale < 0.0) {
        UtilityFunctions::push_error("[Crimson] Cannot set grounded gravity scale. Use a finite nonnegative value.");
        return;
    }
    gravity_scale = scale;
}

double MotionRule::get_gravity_scale() const {
    return gravity_scale;
}

void MotionRule::set_acceleration(double value) { set_rate(settings.acceleration, value, 1000.0, "acceleration"); }
double MotionRule::get_acceleration() const { return settings.acceleration; }
void MotionRule::set_deceleration(double value) { set_rate(settings.deceleration, value, 1000.0, "deceleration"); }
double MotionRule::get_deceleration() const { return settings.deceleration; }
void MotionRule::set_ground_friction(double value) { set_rate(settings.ground_friction, value, 100.0, "ground friction"); }
double MotionRule::get_ground_friction() const { return settings.ground_friction; }
void MotionRule::set_air_control(double value) { set_rate(settings.air_control, value, 1.0, "air control"); }
double MotionRule::get_air_control() const { return settings.air_control; }
void MotionRule::set_jump_speed(double speed) { set_rate(settings.jump_speed, speed, 1000.0, "jump speed"); }
double MotionRule::get_jump_speed() const { return settings.jump_speed; }
const GroundedSettings &MotionRule::get_grounded_settings() const { return settings; }

Ref<MotionRule> MotionRule::snapshot(const Ref<MotionRule> &authored) {
    if (authored.is_null()) {
        return Locomotion::ground();
    }
    Ref<MotionRule> copy;
    copy.instantiate();
    copy->grounded = authored->grounded;
    copy->gravity_scale = authored->gravity_scale;
    copy->settings = authored->settings;
    return copy;
}

void Locomotion::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("ground"), &Locomotion::ground);
}

Ref<MotionRule> Locomotion::ground() {
    Ref<MotionRule> rule;
    rule.instantiate();
    rule->set_grounded(true);
    return rule;
}

void Mover::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("attach", "body", "rule"), &Mover::attach, DEFVAL(Variant()));
    ClassDB::bind_static_method(get_class_static(), D_METHOD("for_node", "body"), &Mover::for_node);
    ClassDB::bind_method(D_METHOD("move", "world_velocity"), &Mover::move);
    ClassDB::bind_method(D_METHOD("stop"), &Mover::stop);
    ClassDB::bind_method(D_METHOD("jump"), &Mover::jump);
    ClassDB::bind_method(D_METHOD("get_velocity"), &Mover::get_velocity);
    ClassDB::bind_method(D_METHOD("use_rule", "rule"), &Mover::use_rule);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_velocity");
}

Error Mover::validate_attachment(Node3D *body, const Ref<MotionRule> &rule) {
    if (!on_main_thread()) {
        return ERR_BUSY;
    }
    if (body == nullptr || body->is_queued_for_deletion()) {
        return ERR_INVALID_PARAMETER;
    }
    if (!uses_main_thread_group(body)) {
        return ERR_UNAVAILABLE;
    }

    auto *character = Object::cast_to<CharacterBody3D>(body);
    if (character == nullptr && Object::cast_to<CollisionObject3D>(body) != nullptr) {
        return ERR_UNAVAILABLE;
    }
    if (rule.is_valid() && rule->is_grounded() &&
            (character == nullptr || character->get_motion_mode() != CharacterBody3D::MOTION_MODE_GROUNDED)) {
        return ERR_INVALID_PARAMETER;
    }
    return OK;
}

Ref<Mover> Mover::attach(Node3D *body, const Ref<MotionRule> &rule) {
    Ref<Mover> existing = for_node(body);
    if (existing.is_valid()) {
        return existing;
    }
    const Error validation = validate_attachment(body, rule);
    if (validation != OK) {
        if (validation == ERR_BUSY) {
            UtilityFunctions::push_error("[Crimson] Cannot attach Mover from a worker thread. Call attach on the main thread.");
        } else if (validation == ERR_UNAVAILABLE) {
            UtilityFunctions::push_error("[Crimson] Cannot attach Mover to this node. Use a CharacterBody3D or a plain Node3D in a main-thread process group.");
        } else {
            UtilityFunctions::push_error("[Crimson] Cannot attach Mover with this target and rule. Provide a live node and use grounded rules with a grounded CharacterBody3D.");
        }
        return {};
    }

    Ref<Mover> attachment;
    attachment.instantiate();
    attachment->configure_rule(rule);

    MoverDriver *driver = memnew(MoverDriver);
    driver->mover = attachment;
    driver->body_id = ObjectID(body->get_instance_id());
    driver->set_physics_process(true);
    driver->set_physics_process_priority(PHYSICS_PRIORITY);
    attachment->driver_id = ObjectID(driver->get_instance_id());
    body->add_child(driver, false, Node::INTERNAL_MODE_BACK);

    if (attachment->get_driver() == nullptr) {
        if (auto *detached = Object::cast_to<MoverDriver>(ObjectDB::get_instance(attachment->driver_id))) {
            if (detached->get_parent() == nullptr && !detached->is_queued_for_deletion()) {
                memdelete(detached);
            } else if (!detached->is_queued_for_deletion()) {
                detached->queue_free();
            }
        }
        UtilityFunctions::push_error("[Crimson] Cannot attach Mover while this node is changing its children. Attach after it is ready.");
        return {};
    }
    return attachment;
}

Ref<Mover> Mover::for_node(Node3D *body) {
    if (!on_main_thread() || body == nullptr || body->is_queued_for_deletion()) {
        return {};
    }
    const int32_t count = body->get_child_count(true);
    for (int32_t index = 0; index < count; ++index) {
        auto *driver = Object::cast_to<MoverDriver>(body->get_child(index, true));
        if (driver != nullptr && driver->get_body() == body) {
            return driver->mover;
        }
    }
    return {};
}

Error Mover::move(const Vector3 &world_velocity) {
    return move_owned(world_velocity, ObjectID());
}

Error Mover::stop() {
    return move(Vector3());
}

Error Mover::jump() {
    if (!on_main_thread()) {
        return ERR_BUSY;
    }
    MoverDriver *driver = get_driver();
    if (driver == nullptr || !driver->is_inside_tree() || external_steps || !grounded || settings.jump_speed <= 0.0) {
        return ERR_UNAVAILABLE;
    }
    jump_requested = true;
    return OK;
}

Vector3 Mover::get_velocity() const {
    return get_driver() != nullptr ? velocity : Vector3();
}

Error Mover::use_rule(const Ref<MotionRule> &rule) {
    if (!on_main_thread()) {
        return ERR_BUSY;
    }
    MoverDriver *driver = get_driver();
    if (driver == nullptr || external_steps) {
        return ERR_UNAVAILABLE;
    }
    const Error validation = validate_attachment(driver->get_body(), rule);
    if (validation == OK) {
        configure_rule(rule);
    }
    return validation;
}

Error Mover::move_owned(const Vector3 &world_velocity, ObjectID source) {
    if (!on_main_thread()) {
        return ERR_BUSY;
    }
    if (!world_velocity.is_finite()) {
        return ERR_INVALID_PARAMETER;
    }
    if (get_driver() == nullptr) {
        return ERR_UNAVAILABLE;
    }
    steering = world_velocity;
    steering_source = source;
    return OK;
}

void Mover::release_steering(ObjectID source) {
    if (on_main_thread() && source.is_valid() && steering_source == source) {
        steering = Vector3();
        steering_source = ObjectID();
    }
}

void Mover::retire_attachment() {
    MoverDriver *driver = get_driver();
    if (driver == nullptr) {
        return;
    }

    Node *body = driver->get_parent();
    driver_id = ObjectID();
    clear_motion();
    driver->set_physics_process(false);
    driver->queue_free();
    body->remove_child(driver);
}

bool Mover::begin_external_steps() {
    MoverDriver *driver = get_driver();
    if (driver == nullptr) {
        return false;
    }
    external_steps = true;
    driver->set_physics_process(false);
    return true;
}

Vector3 Mover::get_external_steering() const {
    return get_driver() != nullptr && external_steps ? steering : Vector3();
}

void Mover::set_external_velocity(const Vector3 &actual_velocity) {
    if (get_driver() != nullptr && external_steps) {
        velocity = actual_velocity;
    }
}

MoverDriver *Mover::get_driver() const {
    if (driver_id.is_null() || !on_main_thread()) {
        return nullptr;
    }
    auto *driver = Object::cast_to<MoverDriver>(ObjectDB::get_instance(driver_id));
    return driver != nullptr && driver->get_body() != nullptr ? driver : nullptr;
}

void Mover::configure_rule(const Ref<MotionRule> &rule) {
    const bool next_grounded = rule.is_valid() && rule->is_grounded();
    if (next_grounded != grounded) {
        simulation_velocity = Vector3();
    }
    grounded = next_grounded;
    gravity_scale = grounded ? rule->get_gravity_scale() : 1.0;
    if (grounded) {
        settings = rule->get_grounded_settings();
    }
    jump_requested = false;
}

void Mover::step(Node3D *body, double delta) {
    auto *character = Object::cast_to<CharacterBody3D>(body);
    if (character == nullptr) {
        if (steering != Vector3()) {
            body->set_global_position(body->get_global_position() + steering * delta);
        }
        velocity = steering;
        return;
    }

    Vector3 requested_velocity = steering;
    if (grounded) {
        const Vector3 up = character->get_up_direction();
        const bool on_floor = character->is_on_floor();
        const bool takeoff = std::exchange(jump_requested, false) && on_floor;
        const real_t step_time = static_cast<real_t>(delta);
        const real_t gravity = character->get_gravity().dot(up) * gravity_scale;
        const real_t rise = settings.rise(simulation_velocity.dot(up), gravity, step_time, takeoff);
        requested_velocity = settings.steer(simulation_velocity.slide(up), steering.slide(up),
                on_floor && !takeoff, step_time) + up * rise;
    }
    character->set_velocity(requested_velocity);
    character->move_and_slide();
    velocity = character->get_real_velocity();
    if (grounded) {
        simulation_velocity = character->get_velocity();
    }
}

void Mover::clear_motion() {
    steering = Vector3();
    steering_source = ObjectID();
    velocity = Vector3();
    simulation_velocity = Vector3();
    jump_requested = false;
}

void MoverDriver::_bind_methods() {}

Node3D *MoverDriver::get_body() const {
    if (is_queued_for_deletion()) {
        return nullptr;
    }
    auto *body = Object::cast_to<Node3D>(get_parent());
    return body != nullptr && ObjectID(body->get_instance_id()) == body_id && !body->is_queued_for_deletion() ? body : nullptr;
}

void MoverDriver::_notification(int what) {
    if (mover.is_null()) {
        return;
    }
    if (what == NOTIFICATION_ENTER_TREE) {
        set_physics_process(!mover->external_steps && uses_main_thread_group(this));
    } else if (what == NOTIFICATION_EXIT_TREE) {
        mover->clear_motion();
    }
}

void MoverDriver::_physics_process(double delta) {
    if (Node3D *body = mover.is_valid() && on_main_thread() ? get_body() : nullptr) {
        mover->step(body, delta);
    }
}

} // namespace crimson::mover
