// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_builtin_data.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string_view>

namespace crimson::visual_scripting {

/// The decoded native types of an Array element or a Dictionary key and value.
struct RuntimeCollectionType {
    /// One terminal in Godot's collection metadata and the graph's enum identity.
    struct Terminal {
        /// Godot built-in type, or NIL for an unconstrained slot.
        godot::Variant::Type type = godot::Variant::NIL;
        /// Native class for an object terminal, or empty for any Object.
        godot::StringName object_class;
        /// Integral enum identity retained by the graph and erased in Godot's container.
        godot::StringName enumeration;
        /// Saved Script identity on a nominal object terminal.
        godot::String script_path;

        /// Compare container terminals invariantly, including their graph enum identities.
        bool operator==(const Terminal &other) const {
            return type == other.type && object_class == other.object_class && enumeration == other.enumeration &&
                    script_path == other.script_path;
        }
    };

    /// Array element or Dictionary key; unused for the latter's value.
    Terminal first;
    /// Dictionary value type.
    Terminal second;

    /// Match the complete typed shape of two Array or Dictionary pins.
    bool operator==(const RuntimeCollectionType &other) const {
        return first == other.first && second == other.second;
    }
};

namespace {

using namespace godot;

/// Call the same nonvirtual Object check as godot-cpp without acquiring an instance binding.
bool matches_object_class(GDExtensionObjectPtr object, ObjectID id, const StringName &expected) {
    // Match the compatibility hash in Godot 4.7's generated Object::is_class binding.
    static const StringName method("is_class");
    static const auto binding = gdextension_interface::classdb_get_method_bind(
            Object::get_class_static()._native_ptr(), method._native_ptr(), 2619796661);
    if (!binding) {
        const auto *wrapper = ObjectDB::get_instance(id);
        return wrapper && wrapper->is_class(expected);
    }
    const GDExtensionConstTypePtr args[] = {expected._native_ptr()};
    int8_t result = 0;
    gdextension_interface::object_method_bind_ptrcall(binding, object, args, &result);
    return result != 0;
}

/// Accept serialized names as either strings or interned names.
bool is_text(const Variant &value) {
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME;
}

/// Read only a name of the correct Variant type.
StringName read_name(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return is_text(value) ? StringName(value) : StringName();
}

/// Read the declared type, distinguishing invalid records from a Variant pin.
Variant::Type read_type(const Dictionary &record) {
    const Variant value = record.get("type", Variant());
    if (value.get_type() != Variant::INT) {
        return Variant::VARIANT_MAX;
    }
    const int64_t type = value;
    return type >= Variant::NIL && type < Variant::VARIANT_MAX ?
            static_cast<Variant::Type>(type) : Variant::VARIANT_MAX;
}

/// Match exact types and the two implicit conversions supported by graph wires.
bool accepts_type(Variant::Type source, Variant::Type target) {
    return source == target || target == Variant::NIL ||
           (source == Variant::INT && target == Variant::FLOAT) ||
           (source == Variant::STRING && target == Variant::STRING_NAME) ||
           (source == Variant::STRING_NAME && target == Variant::STRING);
}

/// Recognize runtime classes declared by Godot or GDExtension.
bool is_runtime_class(const StringName &name) {
    if (name.is_empty() || !ClassDB::class_exists(name)) {
        return false;
    }
    const auto api = ClassDBSingleton::get_singleton()->class_get_api_type(name);
    return api == ClassDBSingleton::API_CORE || api == ClassDBSingleton::API_EXTENSION;
}

/// Resolve a declared ClassDB enum and exclude flag sets and global enum names.
bool resolve_enum(const StringName &qualified, StringName &owner, StringName &enumeration) {
    const String text = qualified;
    const int64_t separator = text.find(".");
    if (separator <= 0 || separator >= text.length() - 1) {
        return false;
    }
    owner = text.substr(0, separator);
    enumeration = text.substr(separator + 1);
    return is_runtime_class(owner) && ClassDB::class_has_enum(owner, enumeration, true) &&
           !ClassDB::is_class_enum_bitfield(owner, enumeration, true);
}

/// Check a saved native enum name or the stable identity of an authored enum.
bool is_enum_identity(const StringName &name) {
    const String identity = name;
    const String prefix("GraphEnum.Enum_");
    if (identity.begins_with(prefix)) {
        const String suffix = identity.substr(prefix.length());
        return suffix.is_valid_int() && suffix.to_int() > 0 && String::num_int64(suffix.to_int()) == suffix;
    }
    StringName owner;
    StringName enumeration;
    return resolve_enum(name, owner, enumeration);
}

/// Check terminals that can form stable Dictionary keys.
bool valid_key_type(Variant::Type type) {
    return type < Variant::RID;
}

/// Decode one optional collection field. A missing field is a Variant terminal.
bool read_terminal(const Dictionary &pin, const char *field, RuntimeCollectionType::Terminal &terminal) {
    if (!pin.has(field)) {
        return true;
    }
    const Variant value = pin[field];
    if (value.get_type() != Variant::DICTIONARY) {
        return false;
    }
    const Dictionary descriptor = value;
    terminal.type = read_type(descriptor);
    terminal.object_class = read_name(descriptor, "class");
    terminal.enumeration = read_name(descriptor, "enum_type");
    terminal.script_path = read_name(descriptor, "script_path");
    return terminal.type != Variant::VARIANT_MAX && !descriptor.has("element") && !descriptor.has("key") &&
            !descriptor.has("value") &&
            (!descriptor.has("class") || (terminal.type == Variant::OBJECT && is_runtime_class(terminal.object_class))) &&
            (!descriptor.has("enum_type") || (terminal.type == Variant::INT && is_enum_identity(terminal.enumeration))) &&
            (!descriptor.has("script_path") || (terminal.type == Variant::OBJECT && valid_script_path(terminal.script_path)));
}

/// Decode the shape shared by graph wire validation, literals, and runtime slots.
bool read_collection(const Dictionary &pin, Variant::Type type, RuntimeCollectionType &collection) {
    if (type == Variant::ARRAY) {
        return !pin.has("key") && !pin.has("value") && read_terminal(pin, "element", collection.first);
    }
    return type == Variant::DICTIONARY && !pin.has("element") &&
            read_terminal(pin, "key", collection.first) && valid_key_type(collection.first.type) &&
            read_terminal(pin, "value", collection.second);
}

/// Identify a container that Godot constrains at runtime.
bool constrained(const RuntimeCollectionType &collection) {
    return collection.first.type != Variant::NIL || collection.second.type != Variant::NIL;
}

/// Match one native collection terminal without iterating its contents.
bool matches_terminal(const RuntimeCollectionType::Terminal &terminal, int64_t builtin, const StringName &class_name,
        const Variant &script) {
    if (terminal.type != builtin || terminal.object_class != class_name) return false;
    const Ref<Script> actual = script;
    return terminal.script_path.is_empty() ? actual.is_null() :
            actual.is_valid() && actual->get_path() == terminal.script_path;
}

/// Check exact Variant and class identity before copying a saved element to its typed container.
bool terminal_accepts(const RuntimeCollectionType::Terminal &terminal, const Variant &value) {
    if (terminal.type == Variant::NIL) {
        return true;
    }
    if (terminal.type != Variant::OBJECT) {
        return terminal.type == value.get_type();
    }
    if (value.get_type() == Variant::NIL) {
        return true;
    }
    if (value.get_type() != Variant::OBJECT) {
        return false;
    }
    const ObjectID id = value;
    const auto object = id != ObjectID() ? gdextension_interface::object_get_instance_from_id(id) : nullptr;
    return id == ObjectID() || (object && (terminal.object_class.is_empty() ||
            matches_object_class(object, id, terminal.object_class)) &&
            (terminal.script_path.is_empty() || object_has_script_type(ObjectDB::get_instance(id), terminal.script_path)));
}

/// Read the actual enum type of a method result, argument, or explicitly typed property.
StringName reflected_enum(const Dictionary &metadata) {
    const int64_t usage = metadata.get("usage", 0);
    if (read_type(metadata) != Variant::INT || (usage & PROPERTY_USAGE_CLASS_IS_ENUM) == 0 ||
            (usage & PROPERTY_USAGE_CLASS_IS_BITFIELD) != 0) {
        return {};
    }
    const StringName qualified = read_name(metadata, "class_name");
    StringName owner;
    StringName enumeration;
    return resolve_enum(qualified, owner, enumeration) ? qualified : StringName();
}

/// Keep callable public names in the member picker.
bool is_public_name(const StringName &name) {
    return !name.is_empty() && !String(name).begins_with("_");
}

/// Check one pin's persistent shape independently of editor labels and hints.
bool is_pin(const Dictionary &pin, const StringName &id) {
    const auto type = read_type(pin);
    return read_name(pin, "id") == id && is_text(pin.get("label", Variant())) &&
           type != Variant::VARIANT_MAX &&
           (type != Variant::OBJECT || is_runtime_class(read_name(pin, "class")));
}

/// Lift the native ClassDB array hint into the graph's typed element descriptor.
Dictionary native_array_element(const String &name) {
    if (name.is_empty()) {
        return {};
    }
    Dictionary element;
    if (is_runtime_class(name)) {
        element["type"] = Variant::OBJECT;
        element["class"] = StringName(name);
        return element;
    }
    for (int type = Variant::BOOL; type < Variant::VARIANT_MAX; ++type) {
        const auto builtin = static_cast<Variant::Type>(type);
        if (builtin != Variant::OBJECT && Variant::get_type_name(builtin) == name) {
            element["type"] = type;
            return element;
        }
    }
    return {};
}

/// Build a pin from the engine's property, argument or result metadata.
Dictionary make_pin(const Dictionary &metadata, const StringName &id, const String &label) {
    const auto type = read_type(metadata);
    const int64_t hint = metadata.get("hint", 0);
    const String hint_string = metadata.get("hint_string", String());
    Dictionary pin;
    pin["id"] = id;
    pin["label"] = label;
    pin["type"] = static_cast<int64_t>(type);
    if (hint != PROPERTY_HINT_NONE) {
        pin["hint"] = hint;
        pin["hint_string"] = hint_string;
    }
    if (type == Variant::OBJECT) {
        StringName name = read_name(metadata, "class_name");
        if (name.is_empty() && (hint == PROPERTY_HINT_RESOURCE_TYPE || hint == PROPERTY_HINT_NODE_TYPE)) {
            name = hint_string;
        }
        pin["class"] = is_runtime_class(name) ? name : StringName("Object");
    } else if (type == Variant::ARRAY && hint == PROPERTY_HINT_ARRAY_TYPE) {
        const Dictionary element = native_array_element(hint_string);
        if (!element.is_empty()) {
            pin["element"] = element;
        }
    } else if (const StringName enumeration = reflected_enum(metadata); !enumeration.is_empty()) {
        pin["enum_type"] = enumeration;
    }
    return pin;
}

/// Add a picker entry that contains a complete saved binding.
void append_member(Array &entries, const StringName &kind, const StringName &owner, const StringName &name,
        const Array &inputs, const Dictionary &output = {}, int64_t flags = 0) {
    Dictionary member;
    member["class"] = owner;
    member["name"] = name;
    member["inputs"] = inputs;
    if ((flags & METHOD_FLAG_CONST) != 0) {
        member["const"] = true;
    }
    if ((flags & METHOD_FLAG_STATIC) != 0) {
        member["static"] = true;
    }
    if ((flags & METHOD_FLAG_VARARG) != 0) {
        member["vararg"] = true;
    }
    if (!output.is_empty()) {
        member["output"] = output;
    }
    Dictionary entry;
    entry["kind"] = kind;
    entry["member"] = member;
    entries.append(entry);
}

/// Create a method argument ID that stays independent of the displayed name.
StringName argument_id(int64_t index) {
    return StringName("arg_" + String::num_int64(index));
}

/// Keep callable runtime methods; callbacks are authored separately as events.
bool is_native_method(const Dictionary &method, bool is_static) {
    constexpr int64_t excluded = METHOD_FLAG_EDITOR | METHOD_FLAG_VIRTUAL | METHOD_FLAG_VIRTUAL_REQUIRED;
    const int64_t flags = method.get("flags", 0);
    return is_public_name(read_name(method, "name")) && (flags & excluded) == 0 &&
            ((flags & METHOD_FLAG_STATIC) != 0) == is_static;
}

/// Skip property list headers and storage details that have no authorable property action.
bool is_public_property(const Dictionary &property) {
    constexpr int64_t excluded = PROPERTY_USAGE_INTERNAL | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_CATEGORY |
            PROPERTY_USAGE_SUBGROUP;
    const int64_t usage = property.get("usage", 0);
    return is_public_name(read_name(property, "name")) && (usage & excluded) == 0 &&
           read_type(property) != Variant::VARIANT_MAX;
}

/// Compare saved pin types with the currently registered member.
bool same_type(const Dictionary &left, const Dictionary &right) {
    const auto type = read_type(left);
    return type == read_type(right) && (type != Variant::OBJECT ||
           read_name(left, "class") == read_name(right, "class")) &&
           (!left.has("element") || left.get("element", Variant()) == right.get("element", Variant()));
}

/// Map the result metadata to a pin, leaving out a void result.
Dictionary method_output(const Dictionary &method) {
    const Dictionary result = method.get("return", Dictionary());
    const auto type = read_type(result);
    const int64_t usage = result.get("usage", 0);
    if (type == Variant::VARIANT_MAX || (type == Variant::NIL && (usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0)) {
        return {};
    }
    return make_pin(result, "result", "Result");
}

/// Capture an authorable ClassDB method with stable argument IDs and native defaults.
void append_native_method(Array &entries, const StringName &owner, const Dictionary &method) {
    const Array arguments = method.get("args", Array());
    const Array defaults = method.get("default_args", Array());
    const int64_t first_default = arguments.size() - defaults.size();
    Array inputs;
    for (int64_t index = 0; index < arguments.size(); ++index) {
        const Dictionary argument = arguments[index];
        Dictionary pin = make_pin(argument, argument_id(index), String(read_name(argument, "name")).capitalize());
        if (index >= first_default) {
            pin["default"] = defaults[index - first_default];
        }
        inputs.append(pin);
    }
    append_member(entries, "call_method", owner, read_name(method, "name"), inputs, method_output(method), method.get("flags", 0));
}

/// Find the shared constant prefix at a word boundary for readable branch labels.
String enum_label_prefix(const PackedStringArray &symbols) {
    if (symbols.size() < 2) {
        return {};
    }
    const String first = symbols[0];
    int64_t shared = first.length();
    for (const String &symbol : symbols) {
        int64_t index = 0;
        while (index < shared && index < symbol.length() && first[index] == symbol[index]) {
            ++index;
        }
        shared = index;
    }
    const int64_t boundary = first.substr(0, shared).rfind("_");
    return boundary > 0 ? first.substr(0, boundary + 1) : String();
}

} // namespace

const GlobalConstant *find_global_constant(const StringName &name) {
    const CharString encoded = String(name).utf8();
    const std::string_view symbol(encoded.get_data());
    const GlobalConstant *end = global_constants + global_constants_count;
    const GlobalConstant *entry = std::lower_bound(global_constants, end, symbol,
            [](const GlobalConstant &constant, std::string_view expected) { return constant.name < expected; });
    return entry != end && entry->name == symbol ? entry : nullptr;
}

Dictionary make_global_constant(const GlobalConstant &constant) {
    Dictionary node;
    node["kind"] = "literal";
    node["global_constant"] = StringName(constant.name);
    return node;
}

PackedStringArray native_enum_types() {
    PackedStringArray names;
    for (const String &owner : ClassDB::get_class_list()) {
        if (!is_runtime_class(owner)) {
            continue;
        }
        for (const String &name : ClassDB::class_get_enum_list(owner, true)) {
            if (!ClassDB::is_class_enum_bitfield(owner, name, true)) {
                names.append(owner + String(".") + name);
            }
        }
    }
    names.sort();
    return names;
}

Array native_enum_entries(const StringName &qualified_enum) {
    StringName owner;
    StringName enumeration;
    if (!resolve_enum(qualified_enum, owner, enumeration)) {
        return {};
    }
    const PackedStringArray symbols = ClassDB::class_get_enum_constants(owner, enumeration, true);
    const String prefix = enum_label_prefix(symbols);
    std::set<int64_t> seen;
    Array cases;
    for (const String &symbol : symbols) {
        const int64_t value = ClassDB::class_get_integer_constant(owner, symbol);
        if (!seen.insert(value).second) {
            continue;
        }
        const String shortened = symbol.substr(prefix.length());
        const String label = (shortened.is_empty() ? symbol : shortened).to_lower().capitalize();
        Dictionary entry;
        entry["id"] = "case_" + symbol;
        entry["label"] = label;
        entry["value"] = value;
        cases.append(entry);
    }
    return cases;
}

Dictionary make_enum_switch(const StringName &qualified_enum) {
    const Array cases = native_enum_entries(qualified_enum);
    if (cases.is_empty()) {
        return {};
    }
    PackedStringArray options;
    for (const Dictionary &entry : cases) {
        options.append(String(entry["label"]) + String(":") + String::num_int64(entry["value"]));
    }
    Dictionary selection;
    selection["id"] = "value";
    selection["label"] = "Selection";
    selection["type"] = static_cast<int64_t>(Variant::INT);
    selection["enum_type"] = qualified_enum;
    selection["hint"] = static_cast<int64_t>(PROPERTY_HINT_ENUM);
    selection["hint_string"] = String(",").join(options);
    selection["default"] = Dictionary(cases[0])["value"];
    Dictionary node;
    node["kind"] = "switch_enum";
    node["enum_type"] = qualified_enum;
    node["cases"] = cases;
    node["pin"] = selection;
    return node;
}

Array find_native_members(const StringName &target_class) {
    Array result;
    std::set<StringName> properties;
    std::set<StringName> methods;
    std::map<StringName, Array> method_lists;
    const auto class_methods = [&](const StringName &owner) -> const Array & {
        const auto existing = method_lists.find(owner);
        if (existing != method_lists.end()) {
            return existing->second;
        }
        return method_lists.emplace(owner, ClassDB::class_get_method_list(owner, true)).first->second;
    };
    const auto getter_enum = [&](const StringName &owner, const StringName &getter) -> StringName {
        for (StringName declared = owner; is_runtime_class(declared); declared = ClassDB::get_parent_class(declared)) {
            for (const Dictionary &method : class_methods(declared)) {
                if (read_name(method, "name") == getter) {
                    return reflected_enum(method.get("return", Dictionary()));
                }
            }
        }
        return {};
    };
    for (StringName owner = target_class; is_runtime_class(owner); owner = ClassDB::get_parent_class(owner)) {
        for (const Dictionary &property : ClassDB::class_get_property_list(owner, true)) {
            const auto name = read_name(property, "name");
            if (!is_public_property(property) || !properties.insert(name).second) {
                continue;
            }
            Dictionary pin = make_pin(property, "value", "Value");
            const auto getter = ClassDB::class_get_property_getter(owner, name);
            const auto setter = ClassDB::class_get_property_setter(owner, name);
            if (!getter.is_empty() && read_type(pin) == Variant::INT && !pin.has("enum_type")) {
                if (const StringName enumeration = getter_enum(owner, getter); !enumeration.is_empty()) {
                    pin["enum_type"] = enumeration;
                }
            }
            if (!getter.is_empty()) {
                append_member(result, "get_property", owner, name, Array(), pin);
                methods.insert(getter);
            }
            const int64_t usage = property.get("usage", 0);
            if ((usage & PROPERTY_USAGE_READ_ONLY) == 0 && !setter.is_empty()) {
                Array inputs;
                inputs.append(pin);
                append_member(result, "set_property", owner, name, inputs);
                methods.insert(setter);
            }
        }
        for (const Dictionary &method : class_methods(owner)) {
            const auto name = read_name(method, "name");
            if (!is_native_method(method, false) || method.get("args", Variant()).get_type() != Variant::ARRAY ||
                !methods.insert(name).second) {
                continue;
            }
            append_native_method(result, owner, method);
        }
    }
    return result;
}

Array find_native_static_members(const StringName &owner) {
    Array result;
    if (is_runtime_class(owner)) {
        for (const Dictionary &method : ClassDB::class_get_method_list(owner, true)) {
            if (is_native_method(method, true)) {
                append_native_method(result, owner, method);
            }
        }
    }
    return result;
}

Array find_native_signals(const StringName &target_class) {
    Array result;
    std::set<StringName> names;
    for (StringName owner = target_class; is_runtime_class(owner); owner = ClassDB::get_parent_class(owner)) {
        for (const Dictionary &signal : ClassDB::class_get_signal_list(owner, true)) {
            const StringName name = read_name(signal, "name");
            if (!is_public_name(name) || !names.insert(name).second) {
                continue;
            }
            Array inputs;
            const Array arguments = signal.get("args", Array());
            for (int64_t index = 0; index < arguments.size(); ++index) {
                const Dictionary argument = arguments[index];
                const String label = String(read_name(argument, "name")).capitalize();
                inputs.append(make_pin(argument, argument_id(index),
                        label.is_empty() ? String("Argument ") + String::num_int64(index + 1) : label));
            }
            Dictionary event;
            event["kind"] = "native_signal";
            event["class"] = owner;
            event["target_class"] = target_class;
            event["signal"] = name;
            event["inputs"] = inputs;
            event["wired_target"] = true;
            result.append(event);
        }
    }
    return result;
}

bool pins_compatible(const Dictionary &source_pin, const Dictionary &target_pin) {
    const auto source = read_type(source_pin);
    const auto target = read_type(target_pin);
    if (source == Variant::VARIANT_MAX || target == Variant::VARIANT_MAX) {
        return false;
    }
    if (target_pin.get("dictionary_terminal", false) &&
            ((target_pin.get("dictionary_key", false) && !valid_key_type(source)) ||
             (source != Variant::NIL && target != Variant::NIL && source != target))) {
        return false;
    }
    if (target_pin.get("iteration_collection", false)) {
        return source == Variant::ARRAY || source == Variant::DICTIONARY || source == Variant::NIL;
    }
    if (target_pin.get("accept_rewire", false)) {
        return true;
    }
    if (source == Variant::OBJECT && target == Variant::OBJECT) {
        const auto source_class = read_name(source_pin, "class");
        const auto target_class = read_name(target_pin, "class");
        const String target_script = read_name(target_pin, "script_path");
        return is_runtime_class(source_class) && is_runtime_class(target_class) &&
               ClassDB::is_parent_class(source_class, target_class) && (target_script.is_empty() ||
               script_paths_compatible(read_name(source_pin, "script_path"), target_script));
    }
    if (source == Variant::INT && target == Variant::INT) {
        const StringName source_enum = read_name(source_pin, "enum_type");
        const StringName target_enum = read_name(target_pin, "enum_type");
        return source_enum.is_empty() || target_enum.is_empty() || source_enum == target_enum;
    }
    if (source == target && pin_has_collection_schema(target_pin)) {
        RuntimeCollectionType from;
        RuntimeCollectionType to;
        return read_collection(source_pin, source, from) && read_collection(target_pin, target, to) &&
                (!constrained(to) || from == to);
    }
    return source == Variant::NIL || accepts_type(source, target);
}

bool pin_accepts_value(const Dictionary &pin, const Variant &value) {
    return RuntimePinType::from_pin(pin).accepts(value);
}

bool collection_key_type_allowed(const Dictionary &type) {
    return valid_key_type(read_type(type));
}

bool pin_has_collection_schema(const Dictionary &pin) {
    const auto type = read_type(pin);
    return (type == Variant::ARRAY || type == Variant::DICTIONARY) &&
            (pin.has("element") || pin.has("key") || pin.has("value"));
}

Variant default_collection_value(const Dictionary &pin) {
    const auto type = read_type(pin);
    RuntimeCollectionType collection;
    if (!read_collection(pin, type, collection)) {
        return {};
    }
    const auto &first = collection.first;
    if (type == Variant::ARRAY) {
        Array result;
        if (first.type != Variant::NIL) {
            const Ref<Script> script = load_script_type(first.script_path);
            if (!first.script_path.is_empty() && script.is_null()) return {};
            result.set_typed(first.type, first.object_class, script.is_valid() ? Variant(script) : Variant());
        }
        return result;
    }
    Dictionary result;
    const auto &second = collection.second;
    if (first.type != Variant::NIL || second.type != Variant::NIL) {
        const Ref<Script> key_script = load_script_type(first.script_path);
        const Ref<Script> value_script = load_script_type(second.script_path);
        if ((!first.script_path.is_empty() && key_script.is_null()) || (!second.script_path.is_empty() && value_script.is_null())) return {};
        result.set_typed(first.type, first.object_class, key_script.is_valid() ? Variant(key_script) : Variant(),
                second.type, second.object_class, value_script.is_valid() ? Variant(value_script) : Variant());
    }
    return result;
}

bool resolve_collection_value(const Dictionary &pin, const Variant &source, Variant &resolved) {
    const auto shape = RuntimePinType::from_pin(pin);
    if (shape.type != source.get_type() || (shape.type != Variant::ARRAY && shape.type != Variant::DICTIONARY)) {
        return false;
    }
    if (shape.accepts(source)) {
        resolved = source;
        return true;
    }
    if (shape.type == Variant::ARRAY) {
        const Array input = source;
        const Variant empty = default_collection_value(pin);
        if (empty.get_type() != Variant::ARRAY) return false;
        Array result = empty;
        for (const Variant &entry : input) {
            if (!terminal_accepts(shape.collection->first, entry)) {
                return false;
            }
            result.append(entry);
        }
        resolved = result;
        return true;
    }

    const Dictionary input = source;
    const Variant empty = default_collection_value(pin);
    if (empty.get_type() != Variant::DICTIONARY) return false;
    Dictionary result = empty;
    for (const Variant &key : input.keys()) {
        const Variant entry = input.get(key, Variant());
        if (!terminal_accepts(shape.collection->first, key) || !terminal_accepts(shape.collection->second, entry) ||
                !result.set(key, entry)) {
            return false;
        }
    }
    resolved = result;
    return true;
}

bool is_live_method_receiver(const Variant &value) {
    if (value.get_type() != Variant::OBJECT) return false;
    const ObjectID id = value;
    const auto object = gdextension_interface::object_get_instance_from_id(id);
    if (!object) return false;
    // Match the compatibility hash in Godot 4.7's generated Object::is_queued_for_deletion binding.
    static const StringName method("is_queued_for_deletion");
    static const auto binding = gdextension_interface::classdb_get_method_bind(
            Object::get_class_static()._native_ptr(), method._native_ptr(), 36873697);
    if (!binding) {
        const auto *wrapper = ObjectDB::get_instance(id);
        return wrapper && !wrapper->is_queued_for_deletion();
    }
    int8_t result = 0;
    gdextension_interface::object_method_bind_ptrcall(binding, object, nullptr, &result);
    return result == 0;
}

RuntimePinType RuntimePinType::from_pin(const Dictionary &pin) {
    const auto type = read_type(pin);
    if (type == Variant::INT && pin.has("enum_type") && !is_enum_identity(read_name(pin, "enum_type"))) {
        return {};
    }
    RuntimePinType result{type, type == Variant::OBJECT ? read_name(pin, "class") : StringName(), {},
            static_cast<bool>(pin.get("iteration_collection", false)), static_cast<bool>(pin.get("iteration_shape_known", false))};
    if (pin.has("script_path")) {
        result.script_path = read_name(pin, "script_path");
        if (type != Variant::OBJECT || !valid_script_path(result.script_path)) return {};
    }
    if ((type == Variant::ARRAY || type == Variant::DICTIONARY) &&
            (pin.has("element") || pin.has("key") || pin.has("value"))) {
        RuntimeCollectionType collection;
        if (!read_collection(pin, type, collection)) {
            return {};
        }
        if (constrained(collection)) {
            result.collection = std::make_shared<RuntimeCollectionType>(std::move(collection));
        }
    }
    return result;
}

bool RuntimePinType::accepts(const Variant &value) const {
    if (type == Variant::VARIANT_MAX) {
        return false;
    }
    const auto value_type = value.get_type();
    if (iteration_collection && !iteration_shape_known) {
        return value_type == Variant::ARRAY || value_type == Variant::DICTIONARY;
    }
    if (collection) {
        if (value_type != type) {
            return false;
        }
        if (type == Variant::ARRAY) {
            const Array array = value;
            return matches_terminal(collection->first, array.get_typed_builtin(), array.get_typed_class_name(), array.get_typed_script());
        }
        const Dictionary map = value;
        return matches_terminal(collection->first, map.get_typed_key_builtin(), map.get_typed_key_class_name(), map.get_typed_key_script()) &&
                matches_terminal(collection->second, map.get_typed_value_builtin(), map.get_typed_value_class_name(), map.get_typed_value_script());
    }
    if (value_type == Variant::OBJECT) {
        const ObjectID id = value;
        const auto object = id != ObjectID() ? gdextension_interface::object_get_instance_from_id(id) : nullptr;
        if (!object && id != ObjectID()) return false;
        if (type == Variant::OBJECT) {
            return !object || (matches_object_class(object, id, object_class) &&
                    (script_path.is_empty() || object_has_script_type(ObjectDB::get_instance(id), script_path)));
        }
    }
    if (type != Variant::OBJECT) {
        return accepts_type(value_type, type);
    }
    return value_type == Variant::NIL;
}

String validate_native_member(const StringName &kind, const Dictionary &member) {
    if (member.has("variant_type") || member.has("utility")) {
        return kind == StringName("call_method") ? validate_builtin_member(member) : String("This built-in entry is not a method call.");
    }
    if (member.has("script_path")) {
        return validate_script_member(kind, member);
    }
    const auto owner = read_name(member, "class");
    const auto name = read_name(member, "name");
    if (!is_runtime_class(owner) || !is_public_name(name) || member.get("inputs", Variant()).get_type() != Variant::ARRAY ||
        (member.has("output") && member["output"].get_type() != Variant::DICTIONARY)) {
        return "The native member descriptor is incomplete or names an unavailable class.";
    }
    const Array inputs = member["inputs"];
    const Dictionary output = member.get("output", Dictionary());
    const bool getter = kind == StringName("get_property");
    if (getter || kind == StringName("set_property")) {
        if (getter ? (inputs.size() != 0 || !is_pin(output, "value")) :
                     (inputs.size() != 1 || member.has("output") || inputs[0].get_type() != Variant::DICTIONARY ||
                      !is_pin(inputs[0], "value"))) {
            return "The property pins do not match the saved operation.";
        }
        for (const Dictionary &property : ClassDB::class_get_property_list(owner, true)) {
            if (read_name(property, "name") != name || !is_public_property(property)) {
                continue;
            }
            const int64_t usage = property.get("usage", 0);
            const bool available = getter ? !ClassDB::class_get_property_getter(owner, name).is_empty() :
                    ((usage & PROPERTY_USAGE_READ_ONLY) == 0 && !ClassDB::class_get_property_setter(owner, name).is_empty());
            if (available && same_type(getter ? output : Dictionary(inputs[0]), make_pin(property, "value", "Value"))) {
                return {};
            }
            break;
        }
        return "The property is unavailable or its declared type changed.";
    }
    if (kind != StringName("call_method") || !ClassDB::class_has_method(owner, name, true) ||
        inputs.size() != ClassDB::class_get_method_argument_count(owner, name, true) ||
        (member.has("output") && !is_pin(output, "result"))) {
        return "The method is unavailable or its signature changed.";
    }
    for (int64_t index = 0; index < inputs.size(); ++index) {
        if (inputs[index].get_type() != Variant::DICTIONARY || !is_pin(inputs[index], argument_id(index))) {
            return "The saved method has an invalid argument pin.";
        }
        const Dictionary pin = inputs[index];
        if (pin.has("default") && !pin_accepts_value(pin, pin["default"])) {
            return "The saved method has an invalid argument default.";
        }
    }
    for (const Dictionary &method : ClassDB::class_get_method_list(owner, true)) {
        if (read_name(method, "name") != name) {
            continue;
        }
        const int64_t flags = method.get("flags", 0);
        if (!is_native_method(method, member.get("static", false)) ||
                ((flags & METHOD_FLAG_VARARG) != 0) != bool(member.get("vararg", false))) {
            return "The method's call mode changed.";
        }
        if (method.get("args", Variant()).get_type() != Variant::ARRAY) {
            return {};
        }
        const Array arguments = method["args"];
        const Array defaults = method.get("default_args", Array());
        if (arguments.size() != inputs.size() || defaults.size() > arguments.size()) {
            return "The method signature changed.";
        }
        for (int64_t index = 0; index < arguments.size(); ++index) {
            const Dictionary pin = inputs[index];
            if (!same_type(pin, make_pin(arguments[index], argument_id(index), String()))) {
                return "The method argument type changed.";
            }
            if (pin.has("default") && index < arguments.size() - defaults.size()) {
                return "The method argument no longer has a default.";
            }
        }
        const auto current_output = method_output(method);
        if (output.is_empty() != current_output.is_empty() ||
            (!output.is_empty() && !same_type(output, current_output))) {
            return "The method return type changed.";
        }
        return {};
    }
    return "The method is unavailable.";
}

} // namespace crimson::visual_scripting
