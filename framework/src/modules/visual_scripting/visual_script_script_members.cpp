// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_script_members.hpp"

#include "modules/visual_scripting/visual_script_members.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <set>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// Bound traversal of inheritance to avoid looping on damaged custom Script resources.
constexpr int max_script_ancestors = 128;

/// Load the metadata source without constructing a script instance.
Ref<Script> load_project_script(const String &path) {
    auto *loader = ResourceLoader::get_singleton();
    if (!valid_script_path(path) || !loader->exists(path, "Script")) return {};
    return loader->load(path, "Script");
}

/// Return the script's registered type name, falling back to its resource name.
String script_label(const Ref<Script> &script) {
    const String global_name = script->get_global_name();
    return global_name.is_empty() ? external_script_path(script).get_file().get_basename() : global_name;
}

/// Convert the engine registry to a name-indexed table once for one reflection pass.
Dictionary script_classes() {
    Dictionary by_name;
    for (const Dictionary &entry : ProjectSettings::get_singleton()->get_global_class_list()) {
        by_name[entry.get("class", StringName())] = entry;
    }
    return by_name;
}

/// Follow globally named scripts until the registry reaches a native base.
StringName script_native_base(const Dictionary &classes, StringName name) {
    for (int depth = 0; depth < max_script_ancestors && classes.has(name); ++depth) {
        name = Dictionary(classes[name]).get("base", StringName());
    }
    return ClassDB::class_exists(name) ? name : StringName();
}

/// Preserve both the native class and the nominal Script type when Godot publishes it.
void describe_object_class(Dictionary &pin, const StringName &name, const Dictionary &classes) {
    if (ClassDB::class_exists(name)) {
        pin["class"] = name;
        return;
    }
    pin["class"] = StringName("Object");
    if (!classes.has(name)) return;
    const Dictionary metadata = classes[name];
    const String path = metadata.get("path", String());
    const StringName native = script_native_base(classes, name);
    if (!valid_script_path(path) || native.is_empty()) return;
    pin["class"] = native;
    pin["script_path"] = path;
    pin["script_name"] = String(name);
}

/// Decode the plain element name used by Godot for typed Array and Dictionary script metadata.
Dictionary container_terminal(const String &name, const Dictionary &classes) {
    for (int type = Variant::BOOL; type < Variant::VARIANT_MAX; ++type) {
        const auto tag = static_cast<Variant::Type>(type);
        if (tag != Variant::OBJECT && Variant::get_type_name(tag) == name) {
            Dictionary result;
            result["type"] = type;
            return result;
        }
    }
    if (ClassDB::class_exists(name) || classes.has(StringName(name))) {
        Dictionary result;
        result["type"] = Variant::OBJECT;
        describe_object_class(result, name, classes);
        return result;
    }
    return {};
}

/// Build one graph pin from published argument, property, or result metadata.
Dictionary reflected_pin(const Dictionary &metadata, const StringName &id, const String &label, const Dictionary &classes) {
    const int64_t type = metadata.get("type", int64_t(Variant::VARIANT_MAX));
    if (type < Variant::NIL || type >= Variant::VARIANT_MAX) return {};
    Dictionary pin;
    pin["id"] = id;
    pin["label"] = label;
    pin["type"] = type;
    if (type == Variant::OBJECT) {
        describe_object_class(pin, metadata.get("class_name", StringName()), classes);
    }
    const PropertyHint hint = static_cast<PropertyHint>(static_cast<int64_t>(metadata.get("hint", 0)));
    const String hint_string = metadata.get("hint_string", String());
    if (type == Variant::ARRAY && hint == PROPERTY_HINT_ARRAY_TYPE) {
        const Dictionary element = container_terminal(hint_string, classes);
        if (!element.is_empty()) pin["element"] = element;
    } else if (type == Variant::DICTIONARY && hint == PROPERTY_HINT_DICTIONARY_TYPE) {
        const Dictionary key = container_terminal(hint_string.get_slice(";", 0), classes);
        const Dictionary value = container_terminal(hint_string.get_slice(";", 1), classes);
        if (!key.is_empty() && collection_key_type_allowed(key)) pin["key"] = key;
        if (!value.is_empty()) pin["value"] = value;
    }
    return pin;
}

/// Keep members that are callable on an instance and appear in the public graph picker.
bool public_member(const StringName &name) {
    const String text = name;
    return !text.is_empty() && text.is_valid_unicode_identifier() && !text.begins_with("_");
}

/// Reject engine callbacks, script-static operations, and dynamic arity in Script reflection.
bool instance_method(const Dictionary &metadata) {
    constexpr int64_t excluded = METHOD_FLAG_EDITOR | METHOD_FLAG_STATIC | METHOD_FLAG_VIRTUAL |
            METHOD_FLAG_VIRTUAL_REQUIRED | METHOD_FLAG_VARARG;
    return public_member(metadata.get("name", StringName())) &&
            (static_cast<int64_t>(metadata.get("flags", 0)) & excluded) == 0;
}

/// Decode declared positional arguments and remember defaults that must be evaluated by the script.
bool reflected_arguments(const Dictionary &metadata, const Dictionary &classes, Array &inputs) {
    const Array arguments = metadata.get("args", Array());
    const Array defaults = metadata.get("default_args", Array());
    const int64_t first_default = arguments.size() - defaults.size();
    // A foreign Script can report malformed or excessive metadata.
    if (first_default < 0 || arguments.size() > 64) return false;
    for (int64_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index].get_type() != Variant::DICTIONARY) return false;
        const Dictionary argument = arguments[index];
        const String label = String(argument.get("name", String())).capitalize();
        Dictionary pin = reflected_pin(argument, "arg_" + String::num_int64(index),
                label.is_empty() ? String("Argument ") + String::num_int64(index + 1) : label, classes);
        if (pin.is_empty()) return false;
        if (index >= first_default) {
            pin["script_optional"] = true;
            const Variant initial = defaults[index - first_default];
            // GDScript reports null when a default expression must be evaluated on call.
            if (initial.get_type() != Variant::NIL && initial.get_type() != Variant::OBJECT && pin_accepts_value(pin, initial)) {
                pin["default"] = initial;
            }
        }
        inputs.append(pin);
    }
    return true;
}

/// Emit the standard graph member record with an explicit Script receiver.
void append_member(Array &result, const StringName &kind, const Ref<Script> &script, const StringName &name,
        const Array &inputs, const Dictionary &output = {}) {
    Dictionary member;
    member["class"] = script->get_instance_base_type();
    member["name"] = name;
    member["inputs"] = inputs;
    member["script_path"] = external_script_path(script);
    member["script_name"] = script_label(script);
    if (!output.is_empty()) member["output"] = output;
    Dictionary entry;
    entry["kind"] = kind;
    entry["member"] = member;
    result.append(entry);
}

/// Compare stable pin IDs and exact types while leaving labels as authoring information.
bool same_pin_type(const Dictionary &saved, const Dictionary &current) {
    for (const char *key : {"id", "type", "class", "script_path", "element", "key", "value", "script_optional"}) {
        if (saved.get(key, Variant()) != current.get(key, Variant())) return false;
    }
    return !saved.has("default") || current.has("default");
}

/// Compare a saved positional signature with the reflected signature.
bool same_inputs(const Variant &saved, const Variant &current) {
    if (saved.get_type() != Variant::ARRAY || current.get_type() != Variant::ARRAY) return false;
    const Array left = saved;
    const Array right = current;
    if (left.size() != right.size()) return false;
    for (int64_t index = 0; index < left.size(); ++index) {
        if (left[index].get_type() != Variant::DICTIONARY || !same_pin_type(left[index], right[index])) return false;
    }
    return true;
}

} // namespace

bool valid_script_path(const String &path) {
    const String relative = path.trim_prefix("res://");
    return path.begins_with("res://") && !relative.is_empty() && !path.get_file().is_empty() &&
            relative != ".." && !relative.begins_with("../") && !relative.contains(":") && path == path.simplify_path();
}

Ref<Script> load_script_type(const String &path) {
    return load_project_script(path);
}

Array global_script_type_pins() {
    const Dictionary classes = script_classes();
    Array types;
    for (const StringName &name : classes.keys()) {
        Dictionary pin;
        pin["type"] = Variant::OBJECT;
        describe_object_class(pin, name, classes);
        if (pin.has("script_path")) types.append(pin);
    }
    return types;
}

Dictionary global_script_type_pin(const StringName &name) {
    Dictionary pin;
    pin["type"] = Variant::OBJECT;
    describe_object_class(pin, name, script_classes());
    return pin.has("script_path") ? pin : Dictionary();
}

StringName global_script_pin_name(const Dictionary &pin) {
    const StringName name = pin.get("script_name", StringName());
    const String path = pin.get("script_path", String());
    if (name.is_empty() || path.is_empty()) return {};
    const Dictionary known = global_script_type_pin(name);
    return known.get("script_path", String()) == Variant(path) ? name : StringName();
}

String external_script_path(const Ref<Script> &script) {
    if (script.is_null() || script->is_built_in()) return {};
    const String path = script->get_path();
    return valid_script_path(path) ? path : String();
}

void describe_attached_script(Dictionary &record, Object *object) {
    if (!object) return;
    const Ref<Script> script = object->get_script();
    const String path = external_script_path(script);
    if (path.is_empty()) return;
    record["script_path"] = path;
    record["script_name"] = script_label(script);
}

Dictionary describe_script_constructor(const Ref<Script> &script) {
    const String path = external_script_path(script);
    if (path.is_empty() || !script->is_class("GDScript") || script->is_abstract()) return {};
    const StringName base = script->get_instance_base_type();
    if (!ClassDB::class_exists(base) || !ClassDB::can_instantiate(base) ||
            (!ClassDB::is_parent_class(base, "Node") && !ClassDB::is_parent_class(base, "RefCounted"))) return {};
    Array inputs;
    for (const Dictionary &method : script->get_script_method_list()) {
        if (StringName(method.get("name", StringName())) != StringName("_init")) continue;
        if (!reflected_arguments(method, script_classes(), inputs)) return {};
        break;
    }
    Dictionary record;
    record["kind"] = StringName("create_script_instance");
    record["class"] = base;
    record["script_path"] = path;
    record["script_name"] = script_label(script);
    record["inputs"] = inputs;
    return record;
}

bool matches_script_constructor(const Dictionary &record, const Ref<Script> &script) {
    const Dictionary current = describe_script_constructor(script);
    return !current.is_empty() && current.get("class", Variant()) == record.get("class", Variant()) &&
            same_inputs(record.get("inputs", Array()), current["inputs"]);
}

bool script_derives_from(const Ref<Script> &actual, const String &expected_path) {
    if (expected_path.is_empty()) return false;
    Ref<Script> ancestor = actual;
    for (int depth = 0; depth < max_script_ancestors && ancestor.is_valid(); ++depth) {
        if (ancestor->get_path() == expected_path) return true;
        ancestor = ancestor->get_base_script();
    }
    return false;
}

bool object_has_script_type(const Object *object, const String &expected_path) {
    return object && script_derives_from(object->get_script(), expected_path);
}

bool script_paths_compatible(const String &source_path, const String &target_path) {
    return !source_path.is_empty() && (source_path == target_path || script_derives_from(load_project_script(source_path), target_path));
}

Array find_script_members(const String &script_path) {
    Array result;
    const Ref<Script> script = load_project_script(script_path);
    if (script.is_null() || !ClassDB::class_exists(script->get_instance_base_type())) return result;
    const Dictionary classes = script_classes();
    std::set<StringName> methods;
    // Godot provides the current script first, so derived overrides own the saved signature.
    for (const Dictionary &method : script->get_script_method_list()) {
        const StringName name = method.get("name", StringName());
        if (!instance_method(method) || !methods.insert(name).second) continue;
        Array inputs;
        if (!reflected_arguments(method, classes, inputs)) continue;
        const Dictionary return_info = method.get("return", Dictionary());
        const int64_t type = return_info.get("type", 0);
        const int64_t usage = return_info.get("usage", 0);
        const bool returns_value = type != Variant::NIL || (usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0;
        const Dictionary output = returns_value ? reflected_pin(return_info, "result", "Result", classes) : Dictionary();
        if (returns_value && output.is_empty()) continue;
        append_member(result, "call_method", script, name, inputs, output);
    }
    std::set<StringName> properties;
    for (const Dictionary &property : script->get_script_property_list()) {
        const StringName name = property.get("name", StringName());
        const int64_t usage = property.get("usage", 0);
        constexpr int64_t excluded = PROPERTY_USAGE_INTERNAL | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY;
        if (!public_member(name) || (usage & excluded) != 0 || !properties.insert(name).second) continue;
        const Dictionary value = reflected_pin(property, "value", "Value", classes);
        if (value.is_empty()) continue;
        append_member(result, "get_property", script, name, Array(), value);
        if ((usage & PROPERTY_USAGE_READ_ONLY) == 0) append_member(result, "set_property", script, name, Array::make(value));
    }
    return result;
}

Array find_script_signals(const String &script_path) {
    Array result;
    const Ref<Script> script = load_project_script(script_path);
    if (script.is_null() || !ClassDB::class_exists(script->get_instance_base_type())) return result;
    const Dictionary classes = script_classes();
    std::set<StringName> names;
    for (const Dictionary &signal : script->get_script_signal_list()) {
        const StringName name = signal.get("name", StringName());
        if (!public_member(name) || !names.insert(name).second) continue;
        Array arguments;
        if (!reflected_arguments(signal, classes, arguments)) continue;
        Dictionary event;
        event["kind"] = "native_signal";
        event["class"] = script->get_instance_base_type();
        event["target_class"] = event["class"];
        event["signal"] = name;
        event["inputs"] = arguments;
        event["script_path"] = script_path;
        event["script_name"] = script_label(script);
        event["wired_target"] = true;
        result.append(event);
    }
    return result;
}

String validate_script_member(const StringName &kind, const Dictionary &member) {
    const String path = member.get("script_path", String());
    const StringName name = member.get("name", StringName());
    const Variant output = member.get("output", Variant());
    for (const Dictionary &entry : find_script_members(path)) {
        const Dictionary current = entry["member"];
        if (StringName(entry["kind"]) != kind || StringName(current["name"]) != name) continue;
        if (current["class"] != member.get("class", Variant()) || !same_inputs(member.get("inputs", Variant()), current["inputs"])) break;
        const Variant return_pin = current.get("output", Variant());
        if (return_pin.get_type() == Variant::NIL && output.get_type() == Variant::NIL) return {};
        if (return_pin.get_type() == Variant::DICTIONARY && output.get_type() == Variant::DICTIONARY && same_pin_type(output, return_pin)) return {};
        break;
    }
    return "The script member is unavailable or its signature changed. Refresh the node from its typed target.";
}

String validate_script_signal(const Dictionary &record) {
    const String path = record.get("script_path", String());
    for (const Dictionary &current : find_script_signals(path)) {
        if (current["signal"] != record.get("signal", Variant())) continue;
        if (current["class"] == record.get("class", Variant()) && same_inputs(record.get("inputs", Variant()), current["inputs"])) return {};
        break;
    }
    return "The script signal is unavailable or its signature changed. Refresh the node from its typed target.";
}

} // namespace crimson::visual_scripting
