// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "controls.hpp"
#include "input_sources.hpp"

#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cmath>
#include <set>

namespace crimson::input {
using namespace godot;

namespace {

// Create a stable child binding ID from the logical action and its editable slot.
StringName slot_id(const StringName &action, const char *suffix) {
    return StringName(String(action) + "/" + suffix);
}

// Assemble a draft binding with an explicit contribution and settings label.
Ref<InputBinding> make_binding(const StringName &id, const String &label, const Ref<InputEvent> &event, const Vector3 &contribution) {
    Ref<InputBinding> binding;
    binding.instantiate();
    binding->set_id(id);
    binding->set_display_name(label);
    binding->set_event(event);
    binding->set_contribution(contribution);
    return binding;
}

// Prepare a logical action for one authored or code-built layout.
Ref<InputAction> make_action(const StringName &id, InputAction::ValueKind kind, double deadzone = 0.0) {
    Ref<InputAction> action;
    action.instantiate();
    action->set_id(id);
    action->set_kind(kind);
    action->set_deadzone(deadzone);
    return action;
}

// Encode a keyboard location independent of the player's layout language.
Ref<InputEventKey> key_event(Key key) {
    Ref<InputEventKey> event;
    event.instantiate();
    event->set_physical_keycode(key);
    return event;
}

// Encode one direction of a controller axis for any assigned gamepad.
Ref<InputEventJoypadMotion> stick_event(JoyAxis axis, float sign) {
    Ref<InputEventJoypadMotion> event;
    event.instantiate();
    event->set_device(-1);
    event->set_axis(axis);
    event->set_axis_value(sign);
    return event;
}

// Validate a value definition independent of the identity and sources assigned by a layout.
bool valid_action(const InputAction &action) {
    return action.get_kind() >= InputAction::BUTTON && action.get_kind() <= InputAction::MOTION &&
            std::isfinite(action.get_deadzone()) && action.get_deadzone() >= 0.0 && action.get_deadzone() < 1.0;
}

// Record a required ID, distinguishing malformed resources from duplicate identities.
Error record_id(std::set<StringName> &ids, const StringName &id) {
    if (String(id).strip_edges().is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    return ids.insert(id).second ? OK : ERR_ALREADY_EXISTS;
}

// Verify physical sources and the ordered stack before any router compiles them.
Error validate_bindings(const TypedArray<InputBinding> &bindings, InputAction::ValueKind kind, std::set<StringName> &ids) {
    for (int64_t i = 0; i < bindings.size(); ++i) {
        const Ref<InputBinding> binding = bindings[i];
        if (binding.is_null() || !binding->get_contribution().is_finite() ||
                !valid_binding_event(binding->get_event(), kind)) {
            return ERR_INVALID_PARAMETER;
        }
        if (binding->get_stick() != InputBinding::NONE && (kind != InputAction::VECTOR || binding->get_event().is_valid() ||
                (binding->get_stick() != InputBinding::LEFT_STICK && binding->get_stick() != InputBinding::RIGHT_STICK))) {
            return ERR_INVALID_PARAMETER;
        }
        if (binding->get_mouse_axes() < InputBinding::MOUSE_XY || binding->get_mouse_axes() > InputBinding::MOUSE_Y) {
            return ERR_INVALID_PARAMETER;
        }
        const Error result = record_id(ids, binding->get_id());
        if (result != OK) {
            return result;
        }
        const auto modifiers = binding->get_modifiers();
        for (int64_t j = 0; j < modifiers.size(); ++j) {
            const Ref<InputModifier> modifier = modifiers[j];
            if (modifier.is_null() || !modifier->supports(kind)) {
                return ERR_INVALID_PARAMETER;
            }
        }
    }
    return OK;
}

// Check one mapping against the current set of action and binding IDs.
Error validate_mapping(const Ref<InputActionMapping> &mapping, std::set<StringName> &action_ids, std::set<StringName> &binding_ids) {
    if (mapping.is_null() || mapping->get_action().is_null() || !valid_action(*mapping->get_action().ptr())) {
        return ERR_INVALID_PARAMETER;
    }
    const Error result = record_id(action_ids, mapping->get_id());
    return result == OK ? validate_bindings(mapping->get_bindings(), mapping->get_action()->get_kind(), binding_ids) : result;
}

} // namespace

void InputBinding::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_id", "value"), &InputBinding::set_id);
    ClassDB::bind_method(D_METHOD("get_id"), &InputBinding::get_id);
    ClassDB::bind_method(D_METHOD("get_action"), &InputBinding::get_action);
    ClassDB::bind_method(D_METHOD("set_display_name", "value"), &InputBinding::set_display_name);
    ClassDB::bind_method(D_METHOD("get_display_name"), &InputBinding::get_display_name);
    ClassDB::bind_method(D_METHOD("set_event", "value"), &InputBinding::set_event);
    ClassDB::bind_method(D_METHOD("get_event"), &InputBinding::get_event);
    ClassDB::bind_method(D_METHOD("set_contribution", "value"), &InputBinding::set_contribution);
    ClassDB::bind_method(D_METHOD("get_contribution"), &InputBinding::get_contribution);
    ClassDB::bind_method(D_METHOD("set_stick", "value"), &InputBinding::set_stick);
    ClassDB::bind_method(D_METHOD("get_stick"), &InputBinding::get_stick);
    ClassDB::bind_method(D_METHOD("set_mouse_axes", "value"), &InputBinding::set_mouse_axes);
    ClassDB::bind_method(D_METHOD("get_mouse_axes"), &InputBinding::get_mouse_axes);
    ClassDB::bind_method(D_METHOD("set_mouse_motion", "axes"), &InputBinding::set_mouse_motion, DEFVAL(MOUSE_XY));
    ClassDB::bind_method(D_METHOD("set_modifiers", "value"), &InputBinding::set_modifiers);
    ClassDB::bind_method(D_METHOD("get_modifiers"), &InputBinding::get_modifiers);
    BIND_ENUM_CONSTANT(NONE);
    BIND_ENUM_CONSTANT(LEFT_STICK);
    BIND_ENUM_CONSTANT(RIGHT_STICK);
    BIND_ENUM_CONSTANT(MOUSE_XY);
    BIND_ENUM_CONSTANT(MOUSE_X);
    BIND_ENUM_CONSTANT(MOUSE_Y);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent"), "set_event", "get_event");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "contribution", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_contribution", "get_contribution");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "stick", PROPERTY_HINT_ENUM, "None,Left Stick,Right Stick"), "set_stick", "get_stick");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "mouse_axes", PROPERTY_HINT_ENUM, "Mouse XY,Mouse X,Mouse Y"), "set_mouse_axes", "get_mouse_axes");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "modifiers", PROPERTY_HINT_ARRAY_TYPE, "InputModifier"), "set_modifiers", "get_modifiers");
}

void InputBinding::set_id(const StringName &value) { id = value; emit_changed(); }
StringName InputBinding::get_id() const { return id; }
void InputBinding::set_action(const StringName &value) { action = value; }
StringName InputBinding::get_action() const { return action; }
void InputBinding::set_display_name(const String &value) { display_name = value; emit_changed(); }
String InputBinding::get_display_name() const { return display_name; }
void InputBinding::set_event(const Ref<InputEvent> &value) {
    event = value;
    if (value.is_valid()) {
        stick = NONE;
    }
    emit_changed();
}
Ref<InputEvent> InputBinding::get_event() const { return event; }
void InputBinding::set_contribution(const Vector3 &value) { contribution = value; emit_changed(); }
Vector3 InputBinding::get_contribution() const { return contribution; }
void InputBinding::set_stick(Stick value) {
    stick = value;
    if (value != NONE) {
        event.unref();
    }
    emit_changed();
}
InputBinding::Stick InputBinding::get_stick() const { return stick; }
void InputBinding::set_mouse_axes(MouseAxes value) { mouse_axes = value; emit_changed(); }
InputBinding::MouseAxes InputBinding::get_mouse_axes() const { return mouse_axes; }

void InputBinding::set_mouse_motion(MouseAxes axes) {
    Ref<InputEventMouseMotion> motion;
    motion.instantiate();
    mouse_axes = axes;
    contribution = Vector3(1, 1, 0);
    set_event(motion);
}

void InputBinding::set_modifiers(const TypedArray<InputModifier> &value) { modifiers = value.duplicate(); emit_changed(); }
TypedArray<InputModifier> InputBinding::get_modifiers() const { return modifiers.duplicate(); }

void InputAction::_bind_methods() {
    ClassDB::bind_static_method("InputAction", D_METHOD("create", "kind"), &InputAction::create);
    ClassDB::bind_method(D_METHOD("set_id", "value"), &InputAction::set_id);
    ClassDB::bind_method(D_METHOD("get_id"), &InputAction::get_id);
    ClassDB::bind_method(D_METHOD("set_display_name", "value"), &InputAction::set_display_name);
    ClassDB::bind_method(D_METHOD("get_display_name"), &InputAction::get_display_name);
    ClassDB::bind_method(D_METHOD("set_map", "value"), &InputAction::set_map);
    ClassDB::bind_method(D_METHOD("get_map"), &InputAction::get_map);
    ClassDB::bind_method(D_METHOD("set_kind", "value"), &InputAction::set_kind);
    ClassDB::bind_method(D_METHOD("get_kind"), &InputAction::get_kind);
    ClassDB::bind_method(D_METHOD("set_deadzone", "value"), &InputAction::set_deadzone);
    ClassDB::bind_method(D_METHOD("get_deadzone"), &InputAction::get_deadzone);
    ClassDB::bind_method(D_METHOD("set_requires_pointer", "value"), &InputAction::set_requires_pointer);
    ClassDB::bind_method(D_METHOD("get_requires_pointer"), &InputAction::get_requires_pointer);
    ClassDB::bind_method(D_METHOD("set_bindings", "value"), &InputAction::set_bindings);
    ClassDB::bind_method(D_METHOD("get_bindings"), &InputAction::get_bindings);
    BIND_ENUM_CONSTANT(BUTTON);
    BIND_ENUM_CONSTANT(AXIS);
    BIND_ENUM_CONSTANT(VECTOR);
    BIND_ENUM_CONSTANT(MOTION);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_id", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "map", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_map", "get_map");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Button,Axis,Vector,Motion"), "set_kind", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "deadzone", PROPERTY_HINT_RANGE, "0,0.99,0.01"), "set_deadzone", "get_deadzone");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "requires_pointer"), "set_requires_pointer", "get_requires_pointer");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bindings", PROPERTY_HINT_ARRAY_TYPE, "InputBinding", PROPERTY_USAGE_STORAGE), "set_bindings", "get_bindings");
}

Ref<InputAction> InputAction::create(ValueKind kind) {
    Ref<InputAction> result;
    result.instantiate();
    result->set_kind(kind);
    return result;
}

void InputAction::set_id(const StringName &value) { id = value; emit_changed(); }
StringName InputAction::get_id() const { return id; }
void InputAction::set_display_name(const String &value) { display_name = value; emit_changed(); }
String InputAction::get_display_name() const { return display_name; }
void InputAction::set_map(const StringName &value) { map = value; emit_changed(); }
StringName InputAction::get_map() const { return map; }
void InputAction::set_kind(ValueKind value) { kind = value; emit_changed(); }
InputAction::ValueKind InputAction::get_kind() const { return kind; }
void InputAction::set_deadzone(double value) { deadzone = value; emit_changed(); }
double InputAction::get_deadzone() const { return deadzone; }
void InputAction::set_requires_pointer(bool value) { requires_pointer = value; emit_changed(); }
bool InputAction::get_requires_pointer() const { return requires_pointer; }
void InputAction::set_bindings(const TypedArray<InputBinding> &value) { bindings = value.duplicate(); emit_changed(); }
TypedArray<InputBinding> InputAction::get_bindings() const { return bindings.duplicate(); }

void InputActionMapping::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_id", "value"), &InputActionMapping::set_id);
    ClassDB::bind_method(D_METHOD("get_id"), &InputActionMapping::get_id);
    ClassDB::bind_method(D_METHOD("set_action", "value"), &InputActionMapping::set_action);
    ClassDB::bind_method(D_METHOD("get_action"), &InputActionMapping::get_action);
    ClassDB::bind_method(D_METHOD("set_bindings", "value"), &InputActionMapping::set_bindings);
    ClassDB::bind_method(D_METHOD("get_bindings"), &InputActionMapping::get_bindings);
    ClassDB::bind_method(D_METHOD("add_binding", "binding"), &InputActionMapping::add_binding);
    ClassDB::bind_method(D_METHOD("validate"), &InputActionMapping::validate);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "action", PROPERTY_HINT_RESOURCE_TYPE, "InputAction"), "set_action", "get_action");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bindings", PROPERTY_HINT_ARRAY_TYPE, "InputBinding"), "set_bindings", "get_bindings");
}

void InputActionMapping::set_id(const StringName &value) { id = value; emit_changed(); }
StringName InputActionMapping::get_id() const { return id; }
void InputActionMapping::set_action(const Ref<InputAction> &value) { action = value; emit_changed(); }
Ref<InputAction> InputActionMapping::get_action() const { return action; }
void InputActionMapping::set_bindings(const TypedArray<InputBinding> &value) { bindings = value.duplicate(); emit_changed(); }
TypedArray<InputBinding> InputActionMapping::get_bindings() const { return bindings.duplicate(); }

Ref<InputActionMapping> InputActionMapping::add_binding(const Ref<InputBinding> &value) {
    bindings.push_back(value);
    emit_changed();
    return Ref<InputActionMapping>(this);
}

Error InputActionMapping::validate() const {
    if (String(id).strip_edges().is_empty() || action.is_null() || !valid_action(*action.ptr())) {
        return ERR_INVALID_PARAMETER;
    }
    std::set<StringName> binding_ids;
    return validate_bindings(bindings, action->get_kind(), binding_ids);
}

void InputActionMap::_bind_methods() {
    ClassDB::bind_static_method("InputActionMap", D_METHOD("create", "id"), &InputActionMap::create);
    ClassDB::bind_method(D_METHOD("set_id", "value"), &InputActionMap::set_id);
    ClassDB::bind_method(D_METHOD("get_id"), &InputActionMap::get_id);
    ClassDB::bind_method(D_METHOD("set_display_name", "value"), &InputActionMap::set_display_name);
    ClassDB::bind_method(D_METHOD("get_display_name"), &InputActionMap::get_display_name);
    ClassDB::bind_method(D_METHOD("set_mappings", "value"), &InputActionMap::set_mappings);
    ClassDB::bind_method(D_METHOD("get_mappings"), &InputActionMap::get_mappings);
    ClassDB::bind_method(D_METHOD("add_action", "id", "action"), &InputActionMap::add_action);
    ClassDB::bind_method(D_METHOD("validate"), &InputActionMap::validate);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "mappings", PROPERTY_HINT_ARRAY_TYPE, "InputActionMapping"), "set_mappings", "get_mappings");
}

Ref<InputActionMap> InputActionMap::create(const StringName &id) {
    Ref<InputActionMap> result;
    result.instantiate();
    result->set_id(id);
    return result;
}

void InputActionMap::set_id(const StringName &value) { id = value; emit_changed(); }
StringName InputActionMap::get_id() const { return id; }
void InputActionMap::set_display_name(const String &value) { display_name = value; emit_changed(); }
String InputActionMap::get_display_name() const { return display_name; }
void InputActionMap::set_mappings(const TypedArray<InputActionMapping> &value) { mappings = value.duplicate(); emit_changed(); }
TypedArray<InputActionMapping> InputActionMap::get_mappings() const { return mappings.duplicate(); }

Ref<InputActionMapping> InputActionMap::add_action(const StringName &id, const Ref<InputAction> &action) {
    Ref<InputActionMapping> result;
    result.instantiate();
    result->set_id(id);
    result->set_action(action);
    mappings.push_back(result);
    emit_changed();
    return result;
}

Error InputActionMap::validate() const {
    if (String(id).strip_edges().is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    std::set<StringName> action_ids;
    std::set<StringName> binding_ids;
    for (int64_t i = 0; i < mappings.size(); ++i) {
        const Error result = validate_mapping(mappings[i], action_ids, binding_ids);
        if (result != OK) {
            return result;
        }
    }
    return OK;
}

void InputLayout::_bind_methods() {
    ClassDB::bind_static_method("InputLayout", D_METHOD("create"), &InputLayout::create);
    ClassDB::bind_method(D_METHOD("set_actions", "value"), &InputLayout::set_actions);
    ClassDB::bind_method(D_METHOD("get_actions"), &InputLayout::get_actions);
    ClassDB::bind_method(D_METHOD("set_maps", "value"), &InputLayout::set_maps);
    ClassDB::bind_method(D_METHOD("get_maps"), &InputLayout::get_maps);
    ClassDB::bind_method(D_METHOD("with_map", "map"), &InputLayout::with_map);
    ClassDB::bind_method(D_METHOD("resolved_actions"), &InputLayout::resolved_actions);
    ClassDB::bind_method(D_METHOD("set_click_to_capture", "value"), &InputLayout::set_click_to_capture);
    ClassDB::bind_method(D_METHOD("get_click_to_capture"), &InputLayout::get_click_to_capture);
    ClassDB::bind_method(D_METHOD("set_release_action", "value"), &InputLayout::set_release_action);
    ClassDB::bind_method(D_METHOD("get_release_action"), &InputLayout::get_release_action);
    ClassDB::bind_method(D_METHOD("validate"), &InputLayout::validate);
    ClassDB::bind_method(D_METHOD("has_action_kind", "id", "kind"), &InputLayout::has_action_kind);
    ClassDB::bind_method(D_METHOD("add_move", "id", "deadzone"), &InputLayout::add_move, DEFVAL(0.18));
    ClassDB::bind_method(D_METHOD("add_mouse_motion", "id"), &InputLayout::add_mouse_motion);
    ClassDB::bind_method(D_METHOD("add_right_stick", "id", "deadzone"), &InputLayout::add_right_stick, DEFVAL(0.18));
    ClassDB::bind_method(D_METHOD("add_button", "id", "key", "button"), &InputLayout::add_button, DEFVAL(JOY_BUTTON_INVALID));
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "actions", PROPERTY_HINT_ARRAY_TYPE, "InputAction", PROPERTY_USAGE_STORAGE), "set_actions", "get_actions");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "maps", PROPERTY_HINT_ARRAY_TYPE, "InputActionMap"), "set_maps", "get_maps");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "click_to_capture"), "set_click_to_capture", "get_click_to_capture");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "release_action"), "set_release_action", "get_release_action");
}

Ref<InputLayout> InputLayout::create() { return memnew(InputLayout); }
void InputLayout::set_actions(const TypedArray<InputAction> &value) { actions = value.duplicate(); emit_changed(); }
TypedArray<InputAction> InputLayout::get_actions() const { return actions.duplicate(); }
void InputLayout::set_maps(const TypedArray<InputActionMap> &value) { maps = value.duplicate(); emit_changed(); }
TypedArray<InputActionMap> InputLayout::get_maps() const { return maps.duplicate(); }

Ref<InputLayout> InputLayout::with_map(const Ref<InputActionMap> &value) {
    maps.push_back(value);
    emit_changed();
    return Ref<InputLayout>(this);
}

TypedArray<InputAction> InputLayout::resolved_actions() const {
    TypedArray<InputAction> result = actions.duplicate();
    for (int64_t i = 0; i < maps.size(); ++i) {
        const Ref<InputActionMap> map = maps[i];
        if (map.is_null()) {
            continue;
        }
        const auto mappings = map->get_mappings();
        for (int64_t j = 0; j < mappings.size(); ++j) {
            const Ref<InputActionMapping> mapping = mappings[j];
            if (mapping.is_null() || mapping->get_action().is_null()) {
                continue;
            }
            const Ref<InputAction> definition = mapping->get_action();
            const Ref<InputAction> action = InputAction::create(definition->get_kind());
            action->set_id(mapping->get_id());
            action->set_map(map->get_id());
            action->set_display_name(definition->get_display_name());
            action->set_deadzone(definition->get_deadzone());
            action->set_requires_pointer(definition->get_requires_pointer());
            action->set_bindings(mapping->get_bindings());
            result.push_back(action);
        }
    }
    return result;
}

void InputLayout::set_click_to_capture(bool value) { click_to_capture = value; emit_changed(); }
bool InputLayout::get_click_to_capture() const { return click_to_capture; }
void InputLayout::set_release_action(const StringName &value) { release_action = value; emit_changed(); }
StringName InputLayout::get_release_action() const { return release_action; }

Error InputLayout::validate() const {
    std::set<StringName> action_ids;
    std::set<StringName> binding_ids;
    std::set<StringName> map_ids;
    for (int64_t i = 0; i < actions.size(); ++i) {
        const Ref<InputAction> action = actions[i];
        if (action.is_null() || String(action->get_map()).strip_edges().is_empty() || !valid_action(*action.ptr())) {
            return ERR_INVALID_PARAMETER;
        }
        const Error identity = record_id(action_ids, action->get_id());
        if (identity != OK) {
            return identity;
        }
        const Error result = validate_bindings(action->get_bindings(), action->get_kind(), binding_ids);
        if (result != OK) {
            return result;
        }
    }
    for (int64_t i = 0; i < maps.size(); ++i) {
        const Ref<InputActionMap> map = maps[i];
        if (map.is_null()) {
            return ERR_INVALID_PARAMETER;
        }
        const Error identity = record_id(map_ids, map->get_id());
        if (identity != OK) {
            return identity;
        }
        const auto mappings = map->get_mappings();
        for (int64_t j = 0; j < mappings.size(); ++j) {
            const Error result = validate_mapping(mappings[j], action_ids, binding_ids);
            if (result != OK) {
                return result;
            }
        }
    }
    if (!release_action.is_empty() && !has_action_kind(release_action, InputAction::BUTTON)) {
        return ERR_DOES_NOT_EXIST;
    }
    return OK;
}

bool InputLayout::has_action_kind(const StringName &id, InputAction::ValueKind kind) const {
    for (int64_t i = 0; i < actions.size(); ++i) {
        const Ref<InputAction> action = actions[i];
        if (action.is_valid() && action->get_id() == id) {
            return action->get_kind() == kind;
        }
    }
    for (int64_t i = 0; i < maps.size(); ++i) {
        const Ref<InputActionMap> map = maps[i];
        if (map.is_null()) {
            continue;
        }
        const auto mappings = map->get_mappings();
        for (int64_t j = 0; j < mappings.size(); ++j) {
            const Ref<InputActionMapping> mapping = mappings[j];
            if (mapping.is_valid() && mapping->get_id() == id) {
                return mapping->get_action().is_valid() && mapping->get_action()->get_kind() == kind;
            }
        }
    }
    return false;
}

Ref<InputLayout> InputLayout::add_stick(const StringName &id, double deadzone, bool right, bool keyboard) {
    const Ref<InputAction> action = make_action(id, InputAction::VECTOR, deadzone);
    const Vector3 directions[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    const char *key_ids[] = {"key_left", "key_right", "key_up", "key_down"};
    const char *stick_ids[] = {"stick_left", "stick_right", "stick_up", "stick_down"};
    const char *labels[] = {"Left", "Right", "Up", "Down"};
    const Key keys[] = {KEY_A, KEY_D, KEY_W, KEY_S};
    const JoyAxis horizontal = right ? JOY_AXIS_RIGHT_X : JOY_AXIS_LEFT_X;
    const JoyAxis vertical = right ? JOY_AXIS_RIGHT_Y : JOY_AXIS_LEFT_Y;
    TypedArray<InputBinding> bindings;
    for (int i = 0; i < 4; ++i) {
        if (keyboard) {
            bindings.push_back(make_binding(slot_id(id, key_ids[i]), labels[i], key_event(keys[i]), directions[i]));
        }
        const Ref<InputEvent> event = stick_event(i < 2 ? horizontal : vertical, (i % 2 == 0) ? -1.0f : 1.0f);
        bindings.push_back(make_binding(slot_id(id, stick_ids[i]), labels[i], event, directions[i]));
    }
    action->set_bindings(bindings);
    actions.push_back(action);
    emit_changed();
    return Ref<InputLayout>(this);
}

Ref<InputLayout> InputLayout::add_move(const StringName &id, double deadzone) {
    return add_stick(id, deadzone, false, true);
}

Ref<InputLayout> InputLayout::add_right_stick(const StringName &id, double deadzone) {
    return add_stick(id, deadzone, true, false);
}

Ref<InputLayout> InputLayout::add_mouse_motion(const StringName &id) {
    const Ref<InputAction> action = make_action(id, InputAction::MOTION);
    action->set_requires_pointer(true);
    Ref<InputEventMouseMotion> event;
    event.instantiate();
    TypedArray<InputBinding> bindings;
    bindings.push_back(make_binding(slot_id(id, "mouse"), "Mouse motion", event, Vector3(1, 1, 0)));
    action->set_bindings(bindings);
    actions.push_back(action);
    emit_changed();
    return Ref<InputLayout>(this);
}

Ref<InputLayout> InputLayout::add_button(const StringName &id, Key key, JoyButton button) {
    const Ref<InputAction> action = make_action(id, InputAction::BUTTON);
    TypedArray<InputBinding> bindings;
    if (key != KEY_NONE) {
        bindings.push_back(make_binding(slot_id(id, "key"), "Keyboard", key_event(key), Vector3(1, 0, 0)));
    }
    if (button != JOY_BUTTON_INVALID) {
        Ref<InputEventJoypadButton> event;
        event.instantiate();
        event->set_device(-1);
        event->set_button_index(button);
        bindings.push_back(make_binding(slot_id(id, "pad"), "Gamepad", event, Vector3(1, 0, 0)));
    }
    action->set_bindings(bindings);
    actions.push_back(action);
    emit_changed();
    return Ref<InputLayout>(this);
}

} // namespace crimson::input
