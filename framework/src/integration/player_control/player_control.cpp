// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/player_control/player_control.hpp"
#include "integration/world_session/world_session.hpp"
#include "modules/mover/mover_component.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/object.hpp>

#include <cmath>

using namespace godot;

namespace crimson::integration {
namespace {

// Resolve a live object without extending the lifetime of a scene node.
template <typename Type>
Type *find_node(ObjectID id) {
    return Object::cast_to<Type>(ObjectDB::get_instance(id));
}

// A profile drives one view inside its body. Two drivers would fight over both.
constexpr const char *driver_name = "_PlayerControl";
// Store the weak controller identity on its input scope to allow exactly one local body.
constexpr const char *controller_key = "_crimson_player_control";

// Turn a move action into world steering, projected onto the body's support plane.
Vector3 steering_velocity(const Basis &basis, const Vector3 &up, const Vector2 &axes, double speed) {
    Vector3 forward = (-basis.get_column(2)).slide(up);
    if (forward.length_squared() < 0.000001) {
        forward = up.cross(basis.get_column(0));
    }
    forward.normalize();
    const Vector3 right = forward.cross(up).normalized();
    return (right * axes.x - forward * axes.y) * speed;
}

} // namespace

ControllerStep step_controller(mover::MoverComponent::ControllerStyle style, int reference,
        const Basis &character_basis, const Vector2 &axes, double view_yaw, double previous_body_yaw,
        double speed, double delta, bool has_lock_heading, double lock_heading) {
    using Component = mover::MoverComponent;
    const Vector3 up(0, 1, 0);
    const double heading = style == Component::LOCK_ON && has_lock_heading ? lock_heading : view_yaw;
    Basis basis(up, heading);
    if (style == Component::FPS_AIM && reference != Component::VIEW) {
        basis = reference == Component::CHARACTER ? character_basis : Basis();
    }
    const Vector3 intent = steering_velocity(basis, up, axes, 1.0);
    ControllerStep step{ intent * speed, heading };
    if (style == Component::FREE_ORBIT || style == Component::FORWARD_ONLY) {
        step.body_yaw = previous_body_yaw;
        if (!axes.is_zero_approx()) {
            const double requested_yaw = std::atan2(-intent.x, -intent.z);
            // Both exploration styles turn at up to 540 degrees per second.
            step.body_yaw = Math::rotate_toward(previous_body_yaw, requested_yaw, 3.0 * Math::PI * delta);
            if (style == Component::FORWARD_ONLY) {
                const Vector3 forward(-std::sin(step.body_yaw), 0, -std::cos(step.body_yaw));
                const double alignment = Math::max(0.0, static_cast<double>(intent.normalized().dot(forward)));
                step.velocity = forward * (speed * axes.length() * alignment);
            }
        }
    }
    step.body_yaw = Math::wrapf(step.body_yaw, -Math::PI, Math::PI);
    return step;
}

void PlayerControl::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ready"), &PlayerControl::is_ready);
    ClassDB::bind_method(D_METHOD("get_error"), &PlayerControl::get_error);
    ClassDB::bind_method(D_METHOD("get_message"), &PlayerControl::get_message);
    ClassDB::bind_method(D_METHOD("controls"), &PlayerControl::controls);
    ClassDB::bind_method(D_METHOD("mover"), &PlayerControl::mover);
    ClassDB::bind_method(D_METHOD("lock_on", "target"), &PlayerControl::lock_on);
    ClassDB::bind_method(D_METHOD("clear_lock_on"), &PlayerControl::clear_lock_on);
}

bool PlayerControl::is_ready() const {
    const auto *driver = find_node<PlayerControlDriver>(driver_id);
    return error == OK && driver && driver->is_attached() && controls_scope->is_ready();
}

Error PlayerControl::get_error() const { return error; }
String PlayerControl::get_message() const { return message; }
Ref<input::Controls> PlayerControl::controls() const { return controls_scope; }
Ref<mover::Mover> PlayerControl::mover() const { return body_mover; }

Error PlayerControl::lock_on(Node3D *target) {
    if (!is_ready()) {
        return ERR_UNCONFIGURED;
    }
    auto *driver = find_node<PlayerControlDriver>(driver_id);
    if (driver->active_style() != mover::MoverComponent::LOCK_ON) {
        return ERR_UNAVAILABLE;
    }
    const auto *body = find_node<CharacterBody3D>(driver->body_id);
    if (!target || target == body || body->is_ancestor_of(target) || !target->is_inside_tree() ||
            target->is_queued_for_deletion() || target->get_viewport() != body->get_viewport()) {
        return ERR_INVALID_PARAMETER;
    }
    driver->lock_target_id = target->get_instance_id();
    return OK;
}

void PlayerControl::clear_lock_on() {
    if (auto *driver = find_node<PlayerControlDriver>(driver_id)) {
        driver->lock_target_id = ObjectID();
    }
}

void PlayerControlProfile::_bind_methods() {
    ClassDB::bind_static_method("PlayerControlProfile", D_METHOD("configure", "layout"), &PlayerControlProfile::configure);
    ClassDB::bind_method(D_METHOD("boot", "body", "view", "controls"), &PlayerControlProfile::boot, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("set_input_layout", "value"), &PlayerControlProfile::set_input_layout);
    ClassDB::bind_method(D_METHOD("get_input_layout"), &PlayerControlProfile::get_input_layout);
    ClassDB::bind_method(D_METHOD("set_movement_rule", "value"), &PlayerControlProfile::set_movement_rule);
    ClassDB::bind_method(D_METHOD("get_movement_rule"), &PlayerControlProfile::get_movement_rule);
    ClassDB::bind_method(D_METHOD("set_move_action", "value"), &PlayerControlProfile::set_move_action);
    ClassDB::bind_method(D_METHOD("get_move_action"), &PlayerControlProfile::get_move_action);
    ClassDB::bind_method(D_METHOD("set_jump_action", "value"), &PlayerControlProfile::set_jump_action);
    ClassDB::bind_method(D_METHOD("get_jump_action"), &PlayerControlProfile::get_jump_action);
    ClassDB::bind_method(D_METHOD("set_mouse_look_action", "value"), &PlayerControlProfile::set_mouse_look_action);
    ClassDB::bind_method(D_METHOD("get_mouse_look_action"), &PlayerControlProfile::get_mouse_look_action);
    ClassDB::bind_method(D_METHOD("set_stick_look_action", "value"), &PlayerControlProfile::set_stick_look_action);
    ClassDB::bind_method(D_METHOD("get_stick_look_action"), &PlayerControlProfile::get_stick_look_action);
    ClassDB::bind_method(D_METHOD("set_move_relative_to", "value"), &PlayerControlProfile::set_move_relative_to);
    ClassDB::bind_method(D_METHOD("get_move_relative_to"), &PlayerControlProfile::get_move_relative_to);
    ClassDB::bind_method(D_METHOD("set_controller_style", "value"), &PlayerControlProfile::set_controller_style);
    ClassDB::bind_method(D_METHOD("get_controller_style"), &PlayerControlProfile::get_controller_style);
    ClassDB::bind_method(D_METHOD("set_move_speed", "value"), &PlayerControlProfile::set_move_speed);
    ClassDB::bind_method(D_METHOD("get_move_speed"), &PlayerControlProfile::get_move_speed);
    ClassDB::bind_method(D_METHOD("set_predict_movement_enabled", "enabled"), &PlayerControlProfile::set_predict_movement_enabled);
    ClassDB::bind_method(D_METHOD("is_predict_movement_enabled"), &PlayerControlProfile::is_predict_movement_enabled);
    ClassDB::bind_method(D_METHOD("set_mouse_sensitivity", "value"), &PlayerControlProfile::set_mouse_sensitivity);
    ClassDB::bind_method(D_METHOD("get_mouse_sensitivity"), &PlayerControlProfile::get_mouse_sensitivity);
    ClassDB::bind_method(D_METHOD("set_stick_look_speed", "value"), &PlayerControlProfile::set_stick_look_speed);
    ClassDB::bind_method(D_METHOD("get_stick_look_speed"), &PlayerControlProfile::get_stick_look_speed);
    ClassDB::bind_method(D_METHOD("set_capture_pointer_on_boot", "value"), &PlayerControlProfile::set_capture_pointer_on_boot);
    ClassDB::bind_method(D_METHOD("get_capture_pointer_on_boot"), &PlayerControlProfile::get_capture_pointer_on_boot);

    BIND_ENUM_CONSTANT(VIEW);
    BIND_ENUM_CONSTANT(CHARACTER);
    BIND_ENUM_CONSTANT(WORLD);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_layout", PROPERTY_HINT_RESOURCE_TYPE, "InputLayout"),
            "set_input_layout", "get_input_layout");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "movement_rule", PROPERTY_HINT_RESOURCE_TYPE, "MotionRule"),
            "set_movement_rule", "get_movement_rule");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "move_relative_to", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
            "set_move_relative_to", "get_move_relative_to");
    ADD_GROUP("Movement", "");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_action"), "set_move_action", "get_move_action");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "jump_action"), "set_jump_action", "get_jump_action");
    ADD_GROUP("Controller", "");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "controller_style", PROPERTY_HINT_ENUM,
                         mover::MoverComponent::CONTROLLER_STYLE_LABELS),
            "set_controller_style", "get_controller_style");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "forward_direction", PROPERTY_HINT_ENUM,
                         "View Heading,Character Heading,World Axes"),
            "set_move_relative_to", "get_move_relative_to");
    ADD_GROUP("Ground Movement", "");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "move_speed", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater,suffix:m/s"),
            "set_move_speed", "get_move_speed");
    ADD_GROUP("Look", "");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "mouse_look_action"), "set_mouse_look_action", "get_mouse_look_action");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mouse_sensitivity", PROPERTY_HINT_RANGE, "0,0.05,0.0001,or_greater,suffix:rad/px"),
            "set_mouse_sensitivity", "get_mouse_sensitivity");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "stick_look_action"), "set_stick_look_action", "get_stick_look_action");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stick_look_speed", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:rad/s"),
            "set_stick_look_speed", "get_stick_look_speed");
    ADD_GROUP("Pointer", "");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "capture_pointer_on_boot"),
            "set_capture_pointer_on_boot", "get_capture_pointer_on_boot");
    ADD_GROUP("Network", "");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "predict_movement"),
            "set_predict_movement_enabled", "is_predict_movement_enabled");
}

Ref<PlayerControlProfile> PlayerControlProfile::configure(const Ref<input::InputLayout> &layout) {
    Ref<PlayerControlProfile> profile;
    profile.instantiate();
    profile->set_input_layout(layout);
    return profile;
}

void PlayerControlProfile::set_input_layout(const Ref<input::InputLayout> &value) { input_layout = value; }
Ref<input::InputLayout> PlayerControlProfile::get_input_layout() const { return input_layout; }
void PlayerControlProfile::set_movement_rule(const Ref<mover::MotionRule> &value) { movement_rule = value; }
Ref<mover::MotionRule> PlayerControlProfile::get_movement_rule() const { return movement_rule; }
void PlayerControlProfile::set_move_action(const StringName &value) { move_action = value; }
StringName PlayerControlProfile::get_move_action() const { return move_action; }
void PlayerControlProfile::set_jump_action(const StringName &value) { jump_action = value; }
StringName PlayerControlProfile::get_jump_action() const { return jump_action; }
void PlayerControlProfile::set_mouse_look_action(const StringName &value) { mouse_look_action = value; }
StringName PlayerControlProfile::get_mouse_look_action() const { return mouse_look_action; }
void PlayerControlProfile::set_stick_look_action(const StringName &value) { stick_look_action = value; }
StringName PlayerControlProfile::get_stick_look_action() const { return stick_look_action; }
void PlayerControlProfile::set_move_relative_to(MoveReference value) { move_reference = value; }
PlayerControlProfile::MoveReference PlayerControlProfile::get_move_relative_to() const { return move_reference; }
void PlayerControlProfile::set_controller_style(mover::MoverComponent::ControllerStyle value) {
    if (controller_style == value) {
        return;
    }
    controller_style = value;
    notify_property_list_changed();
}
mover::MoverComponent::ControllerStyle PlayerControlProfile::get_controller_style() const { return controller_style; }
void PlayerControlProfile::_validate_property(PropertyInfo &property) const {
    if (controller_style != mover::MoverComponent::FPS_AIM && property.name == StringName("forward_direction")) {
        property.usage &= ~PROPERTY_USAGE_EDITOR;
    }
}
void PlayerControlProfile::set_move_speed(double value) { move_speed = value; }
double PlayerControlProfile::get_move_speed() const { return move_speed; }
void PlayerControlProfile::set_predict_movement_enabled(bool enabled) { predict_movement = enabled; }
bool PlayerControlProfile::is_predict_movement_enabled() const { return predict_movement; }
void PlayerControlProfile::set_mouse_sensitivity(double value) { mouse_sensitivity = value; }
double PlayerControlProfile::get_mouse_sensitivity() const { return mouse_sensitivity; }
void PlayerControlProfile::set_stick_look_speed(double value) { stick_look_speed = value; }
double PlayerControlProfile::get_stick_look_speed() const { return stick_look_speed; }
void PlayerControlProfile::set_capture_pointer_on_boot(bool value) { capture_pointer_on_boot = value; }
bool PlayerControlProfile::get_capture_pointer_on_boot() const { return capture_pointer_on_boot; }

bool PlayerControlProfile::has_valid_motion_settings() const {
    return controller_style >= mover::MoverComponent::FPS_AIM && controller_style <= mover::MoverComponent::LOCK_ON &&
            move_reference >= VIEW && move_reference <= WORLD && std::isfinite(move_speed) && move_speed >= 0.0 &&
            move_speed <= 1000.0;
}

Ref<PlayerControl> PlayerControlProfile::boot(Node3D *body, Node3D *view, const Ref<input::Controls> &existing) const {
    Ref<PlayerControl> player;
    player.instantiate();
    // Keep boot errors on a typed handle and avoid publishing a partial controller.
    const auto fail = [&](Error error, const String &message) {
        player->error = error;
        player->message = message;
        return player;
    };
    if (!Object::cast_to<CharacterBody3D>(body) || !body->is_inside_tree() ||
            !view || !body->is_ancestor_of(view)) {
        return fail(ERR_INVALID_PARAMETER, "Use a mounted CharacterBody3D and its child view.");
    }
    const ObjectID initial_body_id(body->get_instance_id());
    const ObjectID initial_view_id(view->get_instance_id());
    if (Node *previous = body->get_node_or_null(NodePath(driver_name))) {
        const auto *prior = Object::cast_to<PlayerControlDriver>(previous);
        if (!prior || prior->is_attached()) {
            return fail(ERR_ALREADY_IN_USE, "This body already has a player controller.");
        }
        previous->queue_free();
        body->remove_child(previous);
        body = find_node<CharacterBody3D>(initial_body_id);
        view = find_node<Node3D>(initial_view_id);
        if (!body || !view || !body->is_inside_tree() || !body->is_ancestor_of(view) ||
                body->has_node(NodePath(driver_name))) {
            return fail(ERR_BUSY, "The body changed while replacing an inactive controller.");
        }
    }
    if (input_layout.is_null() || input_layout->validate() != OK) {
        return fail(ERR_INVALID_DATA, "Assign a valid Input Layout before booting the player.");
    }
    if (existing.is_valid() && (!existing->is_ready() || existing->get_layout() != input_layout ||
            !existing->get_owner() || existing->get_owner()->get_tree() != body->get_tree())) {
        return fail(ERR_INVALID_PARAMETER, "Use live controls for this Input Layout and scene tree.");
    }
    const auto has_action = [&](const StringName &id, input::InputAction::ValueKind kind) {
        return existing.is_valid() ? existing->has_action_kind(id, kind) : input_layout->has_action_kind(id, kind);
    };
    if (!has_action(move_action, input::InputAction::VECTOR)) {
        return fail(ERR_INVALID_DATA, "Move Action must name a Vector2 action in the Input Layout.");
    }
    const bool has_jump = !jump_action.is_empty() && has_action(jump_action, input::InputAction::BUTTON);
    if (!has_jump && !jump_action.is_empty() && jump_action != StringName("jump")) {
        return fail(ERR_INVALID_DATA, "Jump Action must name a Button action in the Input Layout.");
    }
    if ((!mouse_look_action.is_empty() && !has_action(mouse_look_action, input::InputAction::MOTION)) ||
            (!stick_look_action.is_empty() && !has_action(stick_look_action, input::InputAction::VECTOR))) {
        return fail(ERR_INVALID_DATA, "Look actions must be Mouse Motion and Vector2 respectively.");
    }
    if (!std::isfinite(mouse_sensitivity) || mouse_sensitivity < 0.0 ||
            !std::isfinite(stick_look_speed) || stick_look_speed < 0.0) {
        return fail(ERR_INVALID_PARAMETER, "Set finite, nonnegative look rates.");
    }
    auto *movement_session = WorldSession::movement_for(body);
    if (movement_session && !movement_session->controls_movement_view(body, view)) {
        return fail(ERR_UNAUTHORIZED, "Use the assigned local actor and its selected camera.");
    }
    bool duplicate = false;
    const auto *component = movement_session ? nullptr : mover::MoverComponent::find_on(body, duplicate);
    if (!movement_session && (duplicate || (component ? !component->has_valid_settings() : !has_valid_motion_settings()))) {
        return fail(ERR_INVALID_PARAMETER, "Use one valid MoverComponent or valid movement settings in the profile.");
    }
    const auto rule = component ? component->snapshot_rule() : mover::MotionRule::snapshot(movement_rule);
    const auto local_reference = component ? static_cast<MoveReference>(component->get_move_relative_to()) : move_reference;
    const auto local_style = component ? component->get_controller_style() : controller_style;
    if (!movement_session && local_style != mover::MoverComponent::FPS_AIM &&
            !Object::cast_to<CharacterBody3D>(body)->get_up_direction().is_equal_approx(Vector3(0, 1, 0))) {
        return fail(ERR_INVALID_PARAMETER, "Use a Y-up character with this Controller Style.");
    }
    const double local_speed = component ? component->get_move_speed() : move_speed;
    const bool replace_rule = component || movement_rule.is_valid();
    const Error validation = movement_session ? OK : mover::Mover::validate_attachment(body, rule);
    if (validation != OK) {
        return fail(validation, "Use a compatible movement rule and body.");
    }
    const Ref<input::Controls> prior_controls = input::Controls::for_node(body);
    const bool had_body_controls = prior_controls.is_valid() && prior_controls->get_owner() == body;
    const bool created_controls = existing.is_null() && !had_body_controls;
    const Ref<input::Controls> controls = existing.is_valid() ? existing : input::Controls::boot(body, input_layout);
    if (controls.is_null() || !controls->is_ready()) {
        return fail(controls.is_valid() ? controls->get_error() : ERR_CANT_CREATE, "Could not start the local controls.");
    }
    if (controls->has_meta(controller_key)) {
        return fail(ERR_ALREADY_IN_USE, "These local controls already drive a body. Retire that body first.");
    }
    Ref<mover::Mover> motion = mover::Mover::for_node(body);
    const bool created_mover = !movement_session && motion.is_null();
    if (created_mover) {
        motion = mover::Mover::attach(body, rule);
    }
    if (!movement_session && motion.is_null()) {
        if (created_controls) {
            controls->close();
        }
        return fail(ERR_CANT_CREATE, "Could not attach the mover.");
    }

    PlayerControlDriver *driver = memnew(PlayerControlDriver);
    driver->set_name(driver_name);
    driver->set_physics_process_priority(mover::Mover::PHYSICS_PRIORITY - 2);
    driver->set_physics_process(true);
    driver->player = player;
    driver->body_id = body->get_instance_id();
    driver->view_id = view->get_instance_id();
    driver->movement_session_id = movement_session ? ObjectID(movement_session->get_instance_id()) : ObjectID();
    driver->move_action = move_action;
    driver->jump_action = has_jump ? jump_action : StringName();
    driver->mouse_look_action = mouse_look_action;
    driver->stick_look_action = stick_look_action;
    driver->move_reference = local_reference;
    driver->controller_style = local_style;
    driver->move_speed = local_speed;
    driver->mouse_sensitivity = mouse_sensitivity;
    driver->stick_look_speed = stick_look_speed;
    player->controls_scope = controls;
    player->body_mover = motion;
    player->driver_id = driver->get_instance_id();
    body->add_child(driver, false, Node::INTERNAL_MODE_BACK);
    driver = find_node<PlayerControlDriver>(player->driver_id);
    body = find_node<CharacterBody3D>(driver ? driver->body_id : ObjectID());
    const bool mounted = body && driver && driver->get_parent() == body && !driver->is_queued_for_deletion();
    if (!mounted || !controls->is_ready() || controls->has_meta(controller_key) ||
            (!movement_session && !created_mover && replace_rule && motion->use_rule(rule) != OK)) {
        if (mounted) {
            body->remove_child(driver);
        }
        if (auto *failed_driver = find_node<PlayerControlDriver>(player->driver_id)) {
            if (failed_driver->get_parent()) {
                failed_driver->queue_free();
            } else {
                memdelete(failed_driver);
            }
        }
        if (created_mover) {
            motion->retire_attachment();
        }
        if (created_controls) {
            controls->close();
        }
        player->controls_scope.unref();
        player->body_mover.unref();
        player->driver_id = ObjectID();
        return fail(ERR_BUSY, "Could not activate the controller. Attach it after the body enters the scene.");
    }
    controls->set_meta(controller_key, static_cast<int64_t>(driver->get_instance_id()));
    driver->attached = true;
    player->error = OK;
    controls->set_pointer_available(true);
    if (capture_pointer_on_boot) {
        controls->capture_pointer();
    }
    return player;
}

void PlayerControlDriver::_bind_methods() {}

bool PlayerControlDriver::is_attached() const {
    const auto *body = find_node<CharacterBody3D>(body_id);
    auto *view = find_node<Node3D>(view_id);
    return attached && is_inside_tree() && body && view && get_parent() == body && body->is_ancestor_of(view) &&
            (movement_session_id.is_null() || find_node<WorldSession>(movement_session_id) != nullptr);
}

mover::MoverComponent::ControllerStyle PlayerControlDriver::active_style() const {
    const auto *session = find_node<WorldSession>(movement_session_id);
    return session ? session->get_controller_style() : controller_style;
}

void PlayerControlDriver::_physics_process(double delta) {
    const Ref<mover::Mover> motion = player->mover();
    const Ref<input::Controls> controls = player->controls();
    CharacterBody3D *body = find_node<CharacterBody3D>(body_id);
    Node3D *view = find_node<Node3D>(view_id);
    const ObjectID source(get_instance_id());
    if (!is_attached()) {
        _exit_tree();
        set_physics_process(false);
        queue_free();
        return;
    }
    const bool active = controls->is_ready() && !controls->is_suspended();
    Vector2 aim;
    if (active && !mouse_look_action.is_empty()) {
        aim += controls->motion(mouse_look_action) * mouse_sensitivity;
    }
    if (active && !stick_look_action.is_empty()) {
        aim += controls->vector(stick_look_action) * stick_look_speed * delta;
    }
    if (!aim.is_zero_approx()) {
        Vector3 angles = view->get_rotation();
        angles.y = Math::wrapf(static_cast<double>(angles.y - aim.x), -Math::PI, Math::PI);
        angles.x = Math::clamp(static_cast<double>(angles.x - aim.y), -1.553343, 1.553343);
        view->set_rotation(angles);
    }

    const Vector2 axes = active ? controls->vector(move_action).limit_length() : Vector2();
    const bool jump_pressed = active && !jump_action.is_empty() && controls->pressed(jump_action);
    bool has_lock_heading = false;
    double lock_heading = 0.0;
    if (auto *target = find_node<Node3D>(lock_target_id); target && target->is_inside_tree() &&
            !target->is_queued_for_deletion() && target->get_viewport() == body->get_viewport()) {
        const Vector3 offset = target->get_global_position() - body->get_global_position();
        const Vector2 horizontal(offset.x, offset.z);
        has_lock_heading = horizontal.is_finite() && !horizontal.is_zero_approx();
        if (has_lock_heading) {
            lock_heading = Math::wrapf(std::atan2(-static_cast<double>(offset.x), -static_cast<double>(offset.z)),
                    -Math::PI, Math::PI);
        }
    } else {
        lock_target_id = ObjectID();
    }
    const Vector3 view_angles = view->get_global_rotation();
    if (auto *movement_session = find_node<WorldSession>(movement_session_id)) {
        movement_session->submit_player_motion(body, axes, Vector2(
                Math::wrapf(static_cast<double>(view_angles.y), -Math::PI, Math::PI),
                Math::clamp(static_cast<double>(view_angles.x), -1.553343, 1.553343)), false, jump_pressed,
                has_lock_heading, lock_heading);
        return;
    }
    if (!active) {
        motion->release_steering(source);
        return;
    }
    const Vector3 up = body->get_up_direction();
    const bool game_owns_facing = controller_style == mover::MoverComponent::FPS_AIM &&
            move_reference == PlayerControlProfile::CHARACTER;
    if (!game_owns_facing && up.is_equal_approx(Vector3(0, 1, 0))) {
        const ControllerStep step = step_controller(controller_style, move_reference,
                body->get_global_transform().basis, axes, view_angles.y, body->get_global_rotation().y,
                move_speed, delta, has_lock_heading, lock_heading);
        body->set_global_rotation(Vector3(0, step.body_yaw, 0));
        view->set_global_rotation(view_angles);
        motion->move_owned(step.velocity, source);
    } else {
        Basis reference;
        if (move_reference != PlayerControlProfile::WORLD) {
            reference = (move_reference == PlayerControlProfile::VIEW ? view : body)->get_global_transform().basis;
        }
        motion->move_owned(steering_velocity(reference, up, axes, move_speed), source);
    }
    if (jump_pressed) {
        motion->jump();
    }
}

void PlayerControlDriver::_exit_tree() {
    if (attached) {
        attached = false;
        const auto motion = player->mover();
        if (auto *movement_session = find_node<WorldSession>(movement_session_id)) {
            auto *body = find_node<CharacterBody3D>(body_id);
            auto *view = find_node<Node3D>(view_id);
            if (body && view && body->is_inside_tree() && view->is_inside_tree()) {
                const Vector3 angles = view->get_global_rotation();
                movement_session->submit_player_motion(body, Vector2(), Vector2(
                        Math::wrapf(static_cast<double>(angles.y), -Math::PI, Math::PI),
                        Math::clamp(static_cast<double>(angles.x), -1.553343, 1.553343)), true);
            }
        } else if (motion.is_valid()) {
            motion->release_steering(ObjectID(get_instance_id()));
        }
        const Ref<input::Controls> controls = player->controls();
        controls->remove_meta(controller_key);
        controls->set_pointer_available(false);
    }
}

} // namespace crimson::integration
