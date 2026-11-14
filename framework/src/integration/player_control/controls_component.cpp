// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/player_control/controls_component.hpp"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::integration {

void ControlsComponent::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("for_body", "body"), &ControlsComponent::for_body);
    ClassDB::bind_method(D_METHOD("boot", "view", "controls"), &ControlsComponent::boot, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("set_input_layout", "layout"), &ControlsComponent::set_input_layout);
    ClassDB::bind_method(D_METHOD("get_input_layout"), &ControlsComponent::get_input_layout);
    ClassDB::bind_method(D_METHOD("set_move_action", "value"), &ControlsComponent::set_move_action);
    ClassDB::bind_method(D_METHOD("get_move_action"), &ControlsComponent::get_move_action);
    ClassDB::bind_method(D_METHOD("set_jump_action", "value"), &ControlsComponent::set_jump_action);
    ClassDB::bind_method(D_METHOD("get_jump_action"), &ControlsComponent::get_jump_action);
    ClassDB::bind_method(D_METHOD("set_mouse_look_action", "value"), &ControlsComponent::set_mouse_look_action);
    ClassDB::bind_method(D_METHOD("get_mouse_look_action"), &ControlsComponent::get_mouse_look_action);
    ClassDB::bind_method(D_METHOD("set_stick_look_action", "value"), &ControlsComponent::set_stick_look_action);
    ClassDB::bind_method(D_METHOD("get_stick_look_action"), &ControlsComponent::get_stick_look_action);
    ClassDB::bind_method(D_METHOD("set_mouse_sensitivity", "value"), &ControlsComponent::set_mouse_sensitivity);
    ClassDB::bind_method(D_METHOD("get_mouse_sensitivity"), &ControlsComponent::get_mouse_sensitivity);
    ClassDB::bind_method(D_METHOD("set_stick_look_speed", "value"), &ControlsComponent::set_stick_look_speed);
    ClassDB::bind_method(D_METHOD("get_stick_look_speed"), &ControlsComponent::get_stick_look_speed);
    ClassDB::bind_method(D_METHOD("set_capture_pointer_on_boot", "value"), &ControlsComponent::set_capture_pointer_on_boot);
    ClassDB::bind_method(D_METHOD("get_capture_pointer_on_boot"), &ControlsComponent::get_capture_pointer_on_boot);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_layout", PROPERTY_HINT_RESOURCE_TYPE, "InputLayout"),
            "set_input_layout", "get_input_layout");
    ADD_GROUP("Player Setup", "");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "move_action"), "set_move_action", "get_move_action");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "jump_action"), "set_jump_action", "get_jump_action");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "mouse_look_action"), "set_mouse_look_action", "get_mouse_look_action");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mouse_sensitivity", PROPERTY_HINT_RANGE, "0,0.05,0.0001,or_greater,suffix:rad/px"),
            "set_mouse_sensitivity", "get_mouse_sensitivity");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "stick_look_action"), "set_stick_look_action", "get_stick_look_action");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stick_look_speed", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:rad/s"),
            "set_stick_look_speed", "get_stick_look_speed");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "capture_pointer_on_boot"),
            "set_capture_pointer_on_boot", "get_capture_pointer_on_boot");
}

ControlsComponent::ControlsComponent() {
    profile.instantiate();
}

void ControlsComponent::set_input_layout(const Ref<input::InputLayout> &value) {
    profile->set_input_layout(value);
    update_configuration_warnings();
}

Ref<input::InputLayout> ControlsComponent::get_input_layout() const { return profile->get_input_layout(); }
void ControlsComponent::set_move_action(const StringName &value) { profile->set_move_action(value); }
StringName ControlsComponent::get_move_action() const { return profile->get_move_action(); }
void ControlsComponent::set_jump_action(const StringName &value) { profile->set_jump_action(value); }
StringName ControlsComponent::get_jump_action() const { return profile->get_jump_action(); }
void ControlsComponent::set_mouse_look_action(const StringName &value) { profile->set_mouse_look_action(value); }
StringName ControlsComponent::get_mouse_look_action() const { return profile->get_mouse_look_action(); }
void ControlsComponent::set_stick_look_action(const StringName &value) { profile->set_stick_look_action(value); }
StringName ControlsComponent::get_stick_look_action() const { return profile->get_stick_look_action(); }
void ControlsComponent::set_mouse_sensitivity(double value) { profile->set_mouse_sensitivity(value); }
double ControlsComponent::get_mouse_sensitivity() const { return profile->get_mouse_sensitivity(); }
void ControlsComponent::set_stick_look_speed(double value) { profile->set_stick_look_speed(value); }
double ControlsComponent::get_stick_look_speed() const { return profile->get_stick_look_speed(); }
void ControlsComponent::set_capture_pointer_on_boot(bool value) { profile->set_capture_pointer_on_boot(value); }
bool ControlsComponent::get_capture_pointer_on_boot() const { return profile->get_capture_pointer_on_boot(); }

ControlsComponent *ControlsComponent::for_body(Node3D *body) {
    if (!Object::cast_to<CharacterBody3D>(body) || body->is_queued_for_deletion()) {
        return nullptr;
    }
    ControlsComponent *component = nullptr;
    for (int index = 0; index < body->get_child_count(); ++index) {
        auto *child = Object::cast_to<ControlsComponent>(body->get_child(index));
        if (!child || child->is_queued_for_deletion()) {
            continue;
        }
        if (component) {
            return nullptr;
        }
        component = child;
    }
    return component;
}

Ref<PlayerControl> ControlsComponent::boot(Node3D *view, const Ref<input::Controls> &controls) const {
    auto *body = Object::cast_to<CharacterBody3D>(get_parent());
    const Ref<PlayerControlProfile> active_profile = profile;
    if (for_body(body) != this) {
        return {};
    }
    return active_profile->boot(body, view, controls);
}

PackedStringArray ControlsComponent::_get_configuration_warnings() const {
    PackedStringArray warnings;
    auto *body = Object::cast_to<CharacterBody3D>(get_parent());
    if (!body) {
        warnings.append("Add this ControlsComponent directly under a CharacterBody3D.");
    } else if (for_body(body) != this) {
        warnings.append("Keep exactly one ControlsComponent on the player body.");
    }
    if (get_input_layout().is_null()) {
        warnings.append("Assign an InputLayout in the Inspector.");
    }
    return warnings;
}

} // namespace crimson::integration
