// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_cast_input.hpp"

#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_map.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Set the saved pin identity, display label, and its Godot Variant type.
Dictionary typed_pin(const char *id, const String &label, Variant::Type type) {
    Dictionary pin;
    pin["id"] = StringName(id);
    pin["label"] = label;
    pin["type"] = type;
    return pin;
}

/// Make one execution pin without adding a data type constraint.
Dictionary execution_pin(const char *id, const char *label) {
    Dictionary pin = typed_pin(id, label, Variant::NIL);
    pin["exec"] = true;
    return pin;
}

/// An authored action name stays a StringName even when selected in the editor.
Dictionary action_pin(const char *id, const char *label) {
    Dictionary pin = typed_pin(id, label, Variant::STRING_NAME);
    pin["default"] = StringName();
    pin["input_action"] = true;
    return pin;
}

/// Store engine labels and exact argument reconstruction in one table.
struct InputPollSpec {
    InputPollOperation operation; ///< Compiled runtime dispatch key.
    const char *name; ///< Operation key in the graph file.
    const char *label; ///< Human-readable picker and graph card title.
    const char *description; ///< Help shown in the picker and on the card.
    Variant::Type result; ///< Native output type.
    int action_count; ///< Number of input action names at the start of the signature.
};

/// List queries in their normal editor browsing order.
constexpr InputPollSpec input_polls[] = {
    {InputPollOperation::Pressed, "pressed", "Is Action Pressed", "Read whether a Godot Input Map action is held.", Variant::BOOL, 1},
    {InputPollOperation::JustPressed, "just_pressed", "Is Action Just Pressed", "Read whether an action was pressed this frame or physics tick.", Variant::BOOL, 1},
    {InputPollOperation::JustReleased, "just_released", "Is Action Just Released", "Read whether an action was released this frame or physics tick.", Variant::BOOL, 1},
    {InputPollOperation::Strength, "strength", "Get Action Strength", "Read an action's strength from 0 to 1.", Variant::FLOAT, 1},
    {InputPollOperation::Axis, "axis", "Get Axis", "Subtract the negative action's strength from the positive action's strength.", Variant::FLOAT, 2},
    {InputPollOperation::Vector, "vector", "Get Vector", "Read four actions as a direction with length limited to one and a circular deadzone.", Variant::VECTOR2, 4},
};

/// Resolve only valid enum values to a catalogue entry.
const InputPollSpec *input_poll_spec(InputPollOperation operation) {
    const auto offset = static_cast<size_t>(operation);
    return offset > 0 && offset <= sizeof(input_polls) / sizeof(input_polls[0]) ? &input_polls[offset - 1] : nullptr;
}

/// Match the query's result type when an action is unset or absent in the current project.
Variant neutral_input(const InputPollSpec &spec) {
    if (spec.result == Variant::BOOL) return false;
    if (spec.result == Variant::FLOAT) return 0.0;
    return Vector2();
}

} // namespace

Array cast_pins(const Dictionary &node, bool output) {
    if (output) {
        const Dictionary declared = node.get("pin", Dictionary());
        const StringName native_class = declared.get("class", StringName("Object"));
        const String label = declared.get("script_name", String(native_class));
        Dictionary result = typed_pin("result", "As " + label, Variant::OBJECT);
        result["class"] = native_class;
        if (declared.has("script_path")) {
            result["script_path"] = declared["script_path"];
            result["script_name"] = label;
        }
        return Array::make(execution_pin("success", "Success"), execution_pin("failure", "Failure"), result);
    }
    Dictionary input = typed_pin("object", "Object", Variant::OBJECT);
    input["class"] = StringName("Object");
    input["default"] = Variant();
    return Array::make(execution_pin("in", "In"), input);
}

String validate_cast_node(const Dictionary &node) {
    const Variant descriptor = node.get("pin", Variant());
    if (descriptor.get_type() != Variant::DICTIONARY) return "Choose the object type for Cast.";
    const Dictionary declared = descriptor;
    const auto target = RuntimePinType::from_pin(declared);
    if (target.type != Variant::OBJECT || target.object_class.is_empty() || !ClassDB::class_exists(target.object_class)) {
        return "Choose a valid Godot object type for Cast.";
    }
    const auto api = ClassDBSingleton::get_singleton()->class_get_api_type(target.object_class);
    if (api != ClassDBSingleton::API_CORE && api != ClassDBSingleton::API_EXTENSION) {
        return "Choose an object type that exists in the running game.";
    }
    if (!target.script_path.is_empty()) {
        const Ref<Script> script = load_script_type(target.script_path);
        if (script.is_null() || !ClassDB::is_parent_class(script->get_instance_base_type(), target.object_class)) {
            return "The Cast script is missing or does not inherit its declared native type. Choose the script type again.";
        }
    }
    return {};
}

Array find_input_poll_nodes() {
    Array entries;
    for (const InputPollSpec &spec : input_polls) {
        Dictionary node;
        node["kind"] = StringName("input_poll");
        node["operation"] = StringName(spec.name);
        node["label"] = spec.label;
        node["description"] = spec.description;
        entries.append(node);
    }
    return entries;
}

InputPollOperation input_poll_operation(const StringName &operation) {
    for (const InputPollSpec &spec : input_polls) {
        if (operation == StringName(spec.name)) return spec.operation;
    }
    return InputPollOperation::Invalid;
}

Array input_poll_pins(InputPollOperation operation, bool output) {
    const InputPollSpec *spec = input_poll_spec(operation);
    if (!spec) return {};
    if (output) return Array::make(typed_pin("result", "Result", spec->result));
    if (spec->action_count == 1) {
        Dictionary exact = typed_pin("exact_match", "Exact Match", Variant::BOOL);
        exact["default"] = false;
        return Array::make(action_pin("action", "Action"), exact);
    }
    if (operation == InputPollOperation::Axis) {
        return Array::make(action_pin("negative", "Negative"), action_pin("positive", "Positive"));
    }
    Dictionary deadzone = typed_pin("deadzone", "Deadzone", Variant::FLOAT);
    deadzone["default"] = -1.0;
    return Array::make(action_pin("negative_x", "Negative X"), action_pin("positive_x", "Positive X"),
            action_pin("negative_y", "Negative Y"), action_pin("positive_y", "Positive Y"), deadzone);
}

Variant read_input_poll(InputPollOperation operation, const Variant *const *arguments) {
    const InputPollSpec *spec = input_poll_spec(operation);
    if (!spec) return {};
    const auto *actions = InputMap::get_singleton();
    const auto *input = Input::get_singleton();
    if (!actions || !input) return neutral_input(*spec);
    StringName names[4];
    for (int index = 0; index < spec->action_count; ++index) {
        names[index] = *arguments[index];
        if (names[index].is_empty() || !actions->has_action(names[index])) return neutral_input(*spec);
    }
    switch (operation) {
        case InputPollOperation::Pressed: return input->is_action_pressed(names[0], *arguments[1]);
        case InputPollOperation::JustPressed: return input->is_action_just_pressed(names[0], *arguments[1]);
        case InputPollOperation::JustReleased: return input->is_action_just_released(names[0], *arguments[1]);
        case InputPollOperation::Strength: return input->get_action_strength(names[0], *arguments[1]);
        case InputPollOperation::Axis: return input->get_axis(names[0], names[1]);
        case InputPollOperation::Vector: {
            const double deadzone = *arguments[4];
            if (!std::isfinite(deadzone)) return Vector2();
            return input->get_vector(names[0], names[1], names[2], names[3], std::clamp(deadzone, -1.0, 1.0));
        }
        default: return {};
    }
}

} // namespace crimson::visual_scripting
