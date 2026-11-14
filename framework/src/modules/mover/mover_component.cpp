// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/mover/mover_component.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cmath>

using namespace godot;

namespace crimson::mover {

void MoverComponent::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_move_speed", "speed"), &MoverComponent::set_move_speed);
    ClassDB::bind_method(D_METHOD("get_move_speed"), &MoverComponent::get_move_speed);
    ClassDB::bind_method(D_METHOD("set_move_relative_to", "reference"), &MoverComponent::set_move_relative_to);
    ClassDB::bind_method(D_METHOD("get_move_relative_to"), &MoverComponent::get_move_relative_to);
    ClassDB::bind_method(D_METHOD("set_controller_style", "value"), &MoverComponent::set_controller_style);
    ClassDB::bind_method(D_METHOD("get_controller_style"), &MoverComponent::get_controller_style);
    ClassDB::bind_method(D_METHOD("set_predict_movement_enabled", "enabled"), &MoverComponent::set_predict_movement_enabled);
    ClassDB::bind_method(D_METHOD("is_predict_movement_enabled"), &MoverComponent::is_predict_movement_enabled);
    ClassDB::bind_method(D_METHOD("mover"), &MoverComponent::mover);
    ClassDB::bind_method(D_METHOD("set_acceleration", "value"), &MoverComponent::set_acceleration);
    ClassDB::bind_method(D_METHOD("get_acceleration"), &MoverComponent::get_acceleration);
    ClassDB::bind_method(D_METHOD("set_deceleration", "value"), &MoverComponent::set_deceleration);
    ClassDB::bind_method(D_METHOD("get_deceleration"), &MoverComponent::get_deceleration);
    ClassDB::bind_method(D_METHOD("set_ground_friction", "value"), &MoverComponent::set_ground_friction);
    ClassDB::bind_method(D_METHOD("get_ground_friction"), &MoverComponent::get_ground_friction);
    ClassDB::bind_method(D_METHOD("set_gravity_scale", "value"), &MoverComponent::set_gravity_scale);
    ClassDB::bind_method(D_METHOD("get_gravity_scale"), &MoverComponent::get_gravity_scale);
    ClassDB::bind_method(D_METHOD("set_air_control", "value"), &MoverComponent::set_air_control);
    ClassDB::bind_method(D_METHOD("get_air_control"), &MoverComponent::get_air_control);
    ClassDB::bind_method(D_METHOD("set_jump_speed", "value"), &MoverComponent::set_jump_speed);
    ClassDB::bind_method(D_METHOD("get_jump_speed"), &MoverComponent::get_jump_speed);
    BIND_ENUM_CONSTANT(VIEW);
    BIND_ENUM_CONSTANT(CHARACTER);
    BIND_ENUM_CONSTANT(WORLD);
    BIND_ENUM_CONSTANT(FPS_AIM);
    BIND_ENUM_CONSTANT(FREE_ORBIT);
    BIND_ENUM_CONSTANT(FORWARD_ONLY);
    BIND_ENUM_CONSTANT(LOCK_ON);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "move_relative_to", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE),
            "set_move_relative_to", "get_move_relative_to");
    ADD_GROUP("Controller", "");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "controller_style", PROPERTY_HINT_ENUM, CONTROLLER_STYLE_LABELS),
            "set_controller_style", "get_controller_style");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "forward_direction", PROPERTY_HINT_ENUM,
                         "View Heading,Character Heading,World Axes"),
            "set_move_relative_to", "get_move_relative_to");
    ADD_GROUP("Ground Movement", "");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "move_speed", PROPERTY_HINT_RANGE, "0,1000,0.1,suffix:m/s"),
            "set_move_speed", "get_move_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration", PROPERTY_HINT_RANGE, String(U"0,1000,0.1,suffix:m/s\u00B2")),
            "set_acceleration", "get_acceleration");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deceleration", PROPERTY_HINT_RANGE, String(U"0,1000,0.1,suffix:m/s\u00B2")),
            "set_deceleration", "get_deceleration");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_friction", PROPERTY_HINT_RANGE, "0,100,0.1,suffix:1/s"),
            "set_ground_friction", "get_ground_friction");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"),
            "set_gravity_scale", "get_gravity_scale");
    ADD_GROUP("Air Movement", "");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "air_control", PROPERTY_HINT_RANGE, "0,1,0.01"),
            "set_air_control", "get_air_control");
    ADD_GROUP("Jumping", "");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_speed", PROPERTY_HINT_RANGE, "0,1000,0.1,suffix:m/s"),
            "set_jump_speed", "get_jump_speed");
    ADD_GROUP("Network", "");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "predict_movement"),
            "set_predict_movement_enabled", "is_predict_movement_enabled");
}

MoverComponent::MoverComponent() {
    grounded_rule = Locomotion::ground();
}

MoverComponent *MoverComponent::find_on(Node *body, bool &duplicate) {
    duplicate = false;
    if (!body) {
        return nullptr;
    }
    MoverComponent *found = nullptr;
    for (int32_t index = 0; index < body->get_child_count(); ++index) {
        if (auto *component = Object::cast_to<MoverComponent>(body->get_child(index));
                component && !component->is_queued_for_deletion()) {
            if (found) {
                duplicate = true;
                return nullptr;
            }
            found = component;
        }
    }
    return found;
}

bool MoverComponent::has_valid_settings() const {
    return controller_style >= FPS_AIM && controller_style <= LOCK_ON &&
            move_reference >= VIEW && move_reference <= WORLD && std::isfinite(move_speed) &&
            move_speed >= 0.0 && move_speed <= 1000.0;
}

PackedStringArray MoverComponent::_get_configuration_warnings() const {
    PackedStringArray warnings;
    auto *body = Object::cast_to<CharacterBody3D>(get_parent());
    if (!body) {
        warnings.append("Add MoverComponent directly under a CharacterBody3D.");
    } else {
        bool duplicate = false;
        find_on(body, duplicate);
        if (duplicate) {
            warnings.append("Use only one MoverComponent per CharacterBody3D.");
        }
        if (body->get_motion_mode() != CharacterBody3D::MOTION_MODE_GROUNDED) {
            warnings.append("Grounded movement requires the parent body's Motion Mode to be Grounded.");
        }
        if (controller_style != FPS_AIM && !body->get_up_direction().is_equal_approx(Vector3(0, 1, 0))) {
            warnings.append("This Controller Style requires the body's Up Direction to be Y-up (0, 1, 0).");
        }
    }
    if (!has_valid_settings()) {
        warnings.append("Select a controller style, a movement reference, and a finite speed from 0 to 1000 m/s.");
    }
    return warnings;
}

Ref<MotionRule> MoverComponent::snapshot_rule() const {
    return MotionRule::snapshot(grounded_rule);
}

void MoverComponent::set_acceleration(double value) { grounded_rule->set_acceleration(value); }
double MoverComponent::get_acceleration() const { return grounded_rule->get_acceleration(); }
void MoverComponent::set_deceleration(double value) { grounded_rule->set_deceleration(value); }
double MoverComponent::get_deceleration() const { return grounded_rule->get_deceleration(); }
void MoverComponent::set_ground_friction(double value) { grounded_rule->set_ground_friction(value); }
double MoverComponent::get_ground_friction() const { return grounded_rule->get_ground_friction(); }
void MoverComponent::set_gravity_scale(double value) { grounded_rule->set_gravity_scale(value); }
double MoverComponent::get_gravity_scale() const { return grounded_rule->get_gravity_scale(); }
void MoverComponent::set_air_control(double value) { grounded_rule->set_air_control(value); }
double MoverComponent::get_air_control() const { return grounded_rule->get_air_control(); }
void MoverComponent::set_jump_speed(double value) { grounded_rule->set_jump_speed(value); }
double MoverComponent::get_jump_speed() const { return grounded_rule->get_jump_speed(); }

void MoverComponent::set_move_speed(double value) {
    move_speed = value;
    update_configuration_warnings();
}

double MoverComponent::get_move_speed() const { return move_speed; }

void MoverComponent::set_move_relative_to(MoveReference value) {
    move_reference = value;
    update_configuration_warnings();
}

MoverComponent::MoveReference MoverComponent::get_move_relative_to() const { return move_reference; }

void MoverComponent::set_controller_style(ControllerStyle value) {
    if (controller_style == value) {
        return;
    }
    controller_style = value;
    notify_property_list_changed();
    update_configuration_warnings();
}

MoverComponent::ControllerStyle MoverComponent::get_controller_style() const { return controller_style; }

void MoverComponent::_validate_property(PropertyInfo &property) const {
    if (controller_style != FPS_AIM && property.name == StringName("forward_direction")) {
        property.usage &= ~PROPERTY_USAGE_EDITOR;
    }
}

void MoverComponent::set_predict_movement_enabled(bool enabled) { predict_movement = enabled; }
bool MoverComponent::is_predict_movement_enabled() const { return predict_movement; }

Ref<Mover> MoverComponent::mover() const {
    return Mover::for_node(Object::cast_to<CharacterBody3D>(get_parent()));
}

} // namespace crimson::mover
