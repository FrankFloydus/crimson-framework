// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_builtin_data.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// Resolve a type spelling used by Godot's generated API metadata.
Variant::Type builtin_type(const char *name) {
    static const auto types = [] {
        std::map<StringName, Variant::Type> result;
        result.emplace(StringName("Variant"), Variant::NIL);
        for (int value = Variant::BOOL; value < Variant::VARIANT_MAX; ++value) {
            const auto type = static_cast<Variant::Type>(value);
            result.emplace(StringName(Variant::get_type_name(type)), type);
        }
        return result;
    }();
    const auto found = types.find(StringName(name));
    return found == types.end() ? Variant::VARIANT_MAX : found->second;
}

/// Use the generated lexical order to find the first method belonging to a type.
const BuiltinMethod *first_method(std::string_view owner) {
    return std::lower_bound(builtin_methods, builtin_methods + builtin_methods_count, owner,
            [](const BuiltinMethod &method, std::string_view key) { return std::string_view(method.owner) < key; });
}

/// Match a method by the immutable pair of engine owner and name.
const BuiltinMethod *find_method(std::string_view owner, std::string_view name) {
    const auto *method = std::lower_bound(first_method(owner), builtin_methods + builtin_methods_count, std::pair(owner, name),
            [](const BuiltinMethod &entry, const auto &key) {
                return std::pair(std::string_view(entry.owner), std::string_view(entry.name)) < key;
            });
    return method != builtin_methods + builtin_methods_count && method->owner == owner && method->name == name ? method : nullptr;
}

/// Make a stable pin descriptor for a built-in method argument or result.
Dictionary make_pin(const StringName &id, const String &label, const char *type_name) {
    Dictionary pin;
    pin["id"] = id;
    pin["label"] = label;
    const auto type = builtin_type(type_name);
    pin["type"] = static_cast<int64_t>(type);
    if (type == Variant::OBJECT) pin["class"] = StringName(type_name);
    return pin;
}

/// Build the signature saved by the editor and validate it against the current build.
Dictionary describe(const BuiltinMethod &method, Variant::Type receiver) {
    Dictionary member;
    member["class"] = StringName(method.owner);
    member["name"] = StringName(method.name);
    if (receiver == Variant::VARIANT_MAX) {
        member["utility"] = true;
    } else {
        member["variant_type"] = static_cast<int64_t>(receiver);
    }
    if (method.read_only) member["const"] = true;
    if (method.is_static) member["static"] = true;
    if (method.variadic) member["vararg"] = true;
    Array inputs;
    for (size_t index = 0; index < method.count; ++index) {
        const auto &argument = method.arguments[index];
        Dictionary pin = make_pin(StringName("arg_" + String::num_int64(index)), String(argument.name).capitalize(), argument.type);
        if (argument.default_literal) pin["default"] = UtilityFunctions::str_to_var(argument.default_literal);
        inputs.append(pin);
    }
    member["inputs"] = inputs;
    if (*method.result) member["output"] = make_pin("result", "Result", method.result);
    return member;
}

/// Package a catalog owner as graph nodes the picker can insert without further reflection.
Array describe_owner(std::string_view owner, Variant::Type type) {
    Array result;
    for (const auto *method = first_method(owner);
            method != builtin_methods + builtin_methods_count && method->owner == owner; ++method) {
        Dictionary entry;
        entry["kind"] = StringName("call_method");
        entry["member"] = describe(*method, type);
        result.append(entry);
    }
    return result;
}

/// Accept signatures using saved string/name IDs while rejecting changes to engine types or defaults.
bool valid_pin(const Variant &saved, const Dictionary &engine) {
    if (saved.get_type() != Variant::DICTIONARY) return false;
    const Dictionary pin = saved;
    const Variant label = pin.get("label", Variant());
    return (label.get_type() == Variant::STRING || label.get_type() == Variant::STRING_NAME) &&
            pin.get("id", Variant()) == engine["id"] && pin.get("type", Variant()) == engine["type"] &&
            pin.get("class", Variant()) == engine.get("class", Variant()) && !pin_has_collection_schema(pin) &&
            !bool(pin.get("exec", false)) && pin.has("default") == engine.has("default") &&
            (!pin.has("default") || pin["default"] == engine["default"]);
}

} // namespace

Array find_builtin_members(Variant::Type type) {
    if (type <= Variant::NIL || type == Variant::OBJECT || type >= Variant::VARIANT_MAX) return {};
    const auto owner = Variant::get_type_name(type).utf8();
    return describe_owner(owner.get_data(), type);
}

Array find_utility_members() {
    return describe_owner("@GlobalScope", Variant::VARIANT_MAX);
}

Variant::Type builtin_receiver_type(const Dictionary &member) {
    const Variant stored = member.get("variant_type", Variant());
    if (stored.get_type() != Variant::INT) return Variant::VARIANT_MAX;
    const int64_t type = stored;
    return type > Variant::NIL && type != Variant::OBJECT && type < Variant::VARIANT_MAX ?
            static_cast<Variant::Type>(type) : Variant::VARIANT_MAX;
}

bool builtin_updates_target(const Dictionary &member) {
    const auto type = builtin_receiver_type(member);
    return type >= Variant::PACKED_BYTE_ARRAY && type < Variant::VARIANT_MAX &&
            !bool(member.get("const", false)) && !bool(member.get("static", false));
}

String validate_builtin_member(const Dictionary &member) {
    const auto type = builtin_receiver_type(member);
    const bool utility = member.get("utility", false);
    const String owner = member.get("class", String());
    if ((utility && (owner != "@GlobalScope" || member.has("variant_type"))) ||
            (!utility && (type == Variant::VARIANT_MAX || owner != Variant::get_type_name(type)))) {
        return "The saved receiver type is unavailable.";
    }
    const String name = member.get("name", String());
    const auto *definition = find_method(owner.utf8().get_data(), name.utf8().get_data());
    if (!definition) return "The built-in method is unavailable.";
    const Dictionary expected = describe(*definition, type);
    for (const auto *flag : {"const", "static", "vararg"}) {
        if (member.get(flag, false) != expected.get(flag, false)) return "The built-in call signature changed.";
    }
    const Variant arguments = member.get("inputs", Variant());
    if (arguments.get_type() != Variant::ARRAY) return "The built-in call is missing its arguments.";
    const Array inputs = arguments;
    const Array expected_inputs = expected["inputs"];
    if (inputs.size() != expected_inputs.size()) return "The built-in call argument count changed.";
    for (int64_t index = 0; index < inputs.size(); ++index) {
        if (!valid_pin(inputs[index], expected_inputs[index])) return "A built-in call argument type or default changed.";
    }
    if (member.has("output") != expected.has("output") ||
            (expected.has("output") && !valid_pin(member["output"], expected["output"]))) {
        return "The built-in call return type changed.";
    }
    return {};
}

const BuiltinMethod *find_utility_method(const StringName &name) {
    return find_method("@GlobalScope", String(name).utf8().get_data());
}

String invoke_utility(const BuiltinMethod &method, const StringName &name,
        const Variant **arguments, size_t count, Variant &result) {
    if (method.call_utility) {
        result = method.call_utility(arguments);
        return {};
    }
    const auto call = gdextension_interface::variant_get_ptr_utility_function(name._native_ptr(), method.hash);
    if (!call) return "The global utility function is unavailable in this Godot build.";
    auto *raw_arguments = reinterpret_cast<GDExtensionConstTypePtr *>(arguments);
    if (builtin_type(method.result) == Variant::STRING) {
        String text;
        call(text._native_ptr(), raw_arguments, count);
        result = text;
    } else {
        call(result._native_ptr(), raw_arguments, count);
    }
    return {};
}

} // namespace crimson::visual_scripting
