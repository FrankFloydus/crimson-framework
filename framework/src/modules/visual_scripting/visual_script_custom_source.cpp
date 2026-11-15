// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_custom_source.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <map>
#include <set>
#include <vector>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Read a saved text or interned name while ignoring malformed Variant values.
String saved_name(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME ? String(value) : String();
}

/// Use only pin and declaration records from the requested list.
Array saved_list(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Check names that GDScript makes available outside of the current script.
bool is_gdscript_global(const String &name) {
    static const PackedStringArray globals = String(
            "break continue elif else for if match pass return when while class class_name const enum extends func "
            "namespace signal static trait var await breakpoint self super yield and as in is not or false null true "
            "INF NAN PI TAU assert preload void Variant Error OK FAILED "
            "convert type_exists char ord range load inst_to_dict dict_to_inst Color8 print_debug print_stack get_stack len is_instance_of "
            "sin cos tan sinh cosh tanh asin acos atan atan2 asinh acosh atanh sqrt fmod fposmod posmod "
            "floor floorf floori ceil ceilf ceili round roundf roundi abs absf absi sign signf signi snapped snappedf snappedi "
            "pow log exp is_nan is_inf is_equal_approx is_zero_approx is_finite ease step_decimals lerp lerpf "
            "cubic_interpolate cubic_interpolate_angle cubic_interpolate_in_time cubic_interpolate_angle_in_time "
            "bezier_interpolate bezier_derivative angle_difference lerp_angle inverse_lerp remap smoothstep move_toward rotate_toward "
            "deg_to_rad rad_to_deg linear_to_db db_to_linear wrap wrapi wrapf max maxi maxf min mini minf clamp clampi clampf "
            "nearest_po2 pingpong randomize randi randf randi_range randf_range randfn seed rand_from_seed weakref type_of type_convert "
            "str error_string type_string print print_rich printerr printt prints printraw print_verbose push_error push_warning "
            "var_to_str str_to_var var_to_bytes bytes_to_var var_to_bytes_with_objects bytes_to_var_with_objects hash "
            "instance_from_id is_instance_valid is_instance_id_valid rid_allocate_id rid_from_int64 is_same").split(" ");
    if (globals.has(name) || ClassDB::class_exists(name)) {
        return true;
    }
    for (int type = Variant::BOOL; type < Variant::VARIANT_MAX; ++type) {
        if (name == Variant::get_type_name(static_cast<Variant::Type>(type))) {
            return true;
        }
    }
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return false;
    }
    if (settings->has_setting("autoload/" + name)) {
        return true;
    }
    for (const Variant &entry : settings->get_global_class_list()) {
        if (saved_name(entry, "class") == name) {
            return true;
        }
    }
    return false;
}

/// Use the GDScript spelling for each built-in type and the pin's reflected native class when one is known.
String annotation(const Dictionary &pin) {
    const int64_t type = pin.get("type", Variant::NIL);
    if (type <= Variant::NIL || type >= Variant::VARIANT_MAX) {
        return "Variant";
    }
    if (type == Variant::OBJECT) {
        const StringName script_name = global_script_pin_name(pin);
        if (!script_name.is_empty()) return String(script_name);
        const String class_name = saved_name(pin, "class");
        return ClassDB::class_exists(class_name) ? class_name : String("Object");
    }
    return Variant::get_type_name(static_cast<Variant::Type>(type));
}

/// Keep ordinary names and allocate a deterministic alias for any keyword or duplicate declaration.
String member_alias(const String &requested, const String &kind, int64_t id, std::set<String> &used) {
    String candidate = requested;
    if (!custom_script_identifier_available(candidate) || used.count(candidate) != 0) {
        const String stem = requested.is_valid_unicode_identifier() ? kind + String("_") + requested : kind;
        candidate = stem;
        int64_t suffix = id;
        while (!custom_script_identifier_available(candidate) || used.count(candidate) != 0) {
            candidate = stem + String("_") + String::num_int64(suffix++);
        }
    }
    used.insert(candidate);
    return candidate;
}

/// Expose the selected graph's local or instance variables through live typed GDScript properties.
void append_variables(String &source, const Array &declarations, const char *scope, const String &indent,
        std::set<String> &used, PackedStringArray &aliases) {
    const String nested = indent + indent;
    const String statement = nested + indent;
    for (const Variant &entry : declarations) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary declaration = entry;
        const int64_t id = declaration.get("id", 0);
        const String original = saved_name(declaration, "name");
        if (id <= 0 || original.is_empty()) {
            continue;
        }
        const String label = member_alias(original, scope, id, used);
        if (label != original) {
            aliases.append(String(scope).capitalize() + " variable " + original + " is graph." + label + ".");
        }
        const String arguments = "&\"" + String(scope) + "\", " + String::num_int64(id);
        source += indent + String("var ") + label + ": " + annotation(declaration.get("pin", Dictionary())) + ":\n";
        source += nested + String("get:\n") + statement + "return __crimson_read.call(" + arguments + ")\n";
        source += nested + String("set(__crimson_value):\n") + statement + "__crimson_write.call(" + arguments + ", __crimson_value)\n\n";
    }
}

/// Emit typed method declarations for graph functions and custom events with ID-based dispatch.
void append_methods(String &source, const Array &declarations, bool is_event, const String &indent,
        std::set<String> &used, PackedStringArray &aliases) {
    const String nested = indent + indent;
    const String kind = is_event ? String("event") : String("function");
    for (const Variant &entry : declarations) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary declaration = entry;
        const int64_t id = declaration.get("id", 0);
        const String original = saved_name(declaration, "name");
        if (id <= 0 || original.is_empty()) {
            continue;
        }
        const String label = member_alias(original, kind, id, used);
        if (label != original) {
            aliases.append(kind.capitalize() + " " + original + " is graph." + label + "().");
        }

        PackedStringArray parameters;
        PackedStringArray arguments;
        std::set<String> parameter_names = used;
        for (const Variant &value : saved_list(declaration, "inputs")) {
            if (value.get_type() != Variant::DICTIONARY || Dictionary(value).get("exec", false)) {
                continue;
            }
            const Dictionary pin = value;
            const String argument = member_alias(saved_name(pin, "label").to_snake_case(), "argument", parameters.size() + 1, parameter_names);
            parameters.append(argument + String(": ") + annotation(pin));
            arguments.append(argument);
        }

        Array outputs;
        if (!is_event) {
            for (const Variant &value : saved_list(declaration, "outputs")) {
                if (value.get_type() == Variant::DICTIONARY && !Dictionary(value).get("exec", false)) {
                    outputs.append(value);
                }
            }
        }
        const String return_type = outputs.is_empty() ? String("void") :
                (outputs.size() == 1 ? annotation(outputs[0]) : String("Dictionary"));
        source += indent + String("func ") + label + "(" + String(", ").join(parameters) + ") -> " + return_type + ":\n";
        source += nested;
        if (!outputs.is_empty()) {
            source += "return ";
        }
        source += String(is_event ? "__crimson_emit" : "__crimson_call") + ".call(" + String::num_int64(id) +
                ", [" + String(", ").join(arguments) + "])\n\n";
    }
}

/// Keep lexical continuation state across authored lines without parsing GDScript expressions.
struct LineState {
    /// A nonzero delimiter identifies an open string.
    char32_t quote = 0;
    /// Triple-quoted strings allow physical newlines.
    bool multiline = false;
    /// Nesting depth of arrays, dictionaries, subscripts and parentheses outside comments and strings.
    int nesting = 0;
    /// A backslash outside a comment or string continues an expression on the next line.
    bool continued = false;
};

/// Return true for the first three matching quote characters in a bounded line.
bool is_triple_quote(const String &line, int64_t offset, char32_t quote) {
    return offset + 2 < line.length() && line[offset + 1] == quote && line[offset + 2] == quote;
}

/// Keep reserved words in identifiers, comments and literals out of synchronous body validation.
bool is_identifier_continue(char32_t character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character >= 128;
}

/// Carry quotes and expression continuations across lines and detect unsupported suspension or early return.
String scan_line(const String &line, LineState &state, int64_t *statement_end = nullptr) {
    state.continued = false;
    String keyword;
    for (int64_t offset = 0; offset < line.length(); ++offset) {
        const char32_t current = line[offset];
        if (state.quote != 0) {
            if (current == '\\') {
                ++offset;
            } else if (current == state.quote && (!state.multiline || is_triple_quote(line, offset, current))) {
                offset += state.multiline ? 2 : 0;
                state.quote = 0;
                state.multiline = false;
            }
        } else if (current == '#') {
            break;
        } else if (current == '"' || current == '\'') {
            state.quote = current;
            state.multiline = is_triple_quote(line, offset, current);
            offset += state.multiline ? 2 : 0;
        } else if (current == '(' || current == '[' || current == '{') {
            ++state.nesting;
        } else if (current == ')' || current == ']' || current == '}') {
            if (state.nesting > 0) {
                --state.nesting;
            }
        } else if (current == '\\' && offset + 1 == line.length()) {
            state.continued = true;
        } else if (current == ';' && statement_end && state.nesting == 0 && *statement_end < 0) {
            *statement_end = offset;
        } else if (is_identifier_continue(current)) {
            const int64_t start = offset;
            while (offset + 1 < line.length() && is_identifier_continue(line[offset + 1])) {
                ++offset;
            }
            const String token = line.substr(start, offset - start + 1);
            if (token == "await") {
                keyword = token;
            } else if (keyword.is_empty() && token == "return" && !line.begins_with(" ") && !line.begins_with("\t")) {
                keyword = token;
            }
        }
    }
    if (state.quote != 0 && !state.multiline && !line.ends_with("\\")) {
        state.quote = 0;
    }
    return keyword;
}

/// Follow the author's indentation convention, defaulting to spaces for a fresh snippet.
String body_indentation(const PackedStringArray &lines) {
    LineState state;
    for (const String &line : lines) {
        const String content = line.strip_edges();
        if (state.quote == 0 && !content.is_empty() && !content.begins_with("#")) {
            if (line.begins_with("\t")) {
                return "\t";
            }
            if (line.begins_with(" ")) {
                return "    ";
            }
        }
        scan_line(line, state);
    }
    return "    ";
}

/// Read a top-level named function prefix and leave lambda expressions to the GDScript parser.
String helper_name(String line) {
    if (line.begins_with("static ") || line.begins_with("static\t")) {
        line = line.substr(7).strip_edges();
    }
    if (!line.begins_with("func ") && !line.begins_with("func\t")) {
        return {};
    }
    const String signature = line.substr(5).strip_edges();
    int64_t length = 0;
    while (length < signature.length() && is_identifier_continue(signature[length])) {
        ++length;
    }
    return signature.substr(0, length);
}

/// Keep implicit GDScript engine callbacks out of wrappers that should run only when invoked.
bool is_automatic_helper(const String &name) {
    static const PackedStringArray callbacks = String(
            "_init _static_init _notification _get _set _get_property_list _property_can_revert "
            "_property_get_revert _to_string _validate_property").split(" ");
    return callbacks.has(name);
}

/// Partition authored lines into the executable body and actual GDScript helper declarations.
PackedInt32Array helper_lines(const PackedStringArray &lines, String &error) {
    PackedInt32Array helpers;
    helpers.resize(lines.size());
    LineState state;
    bool in_helper = false;
    int64_t annotation_line = -1;
    for (int64_t row = 0; row < lines.size(); ++row) {
        const String &line = lines[row];
        const bool starts_statement = state.quote == 0 && state.nesting == 0 && !state.continued &&
                !line.is_empty() && !line.begins_with(" ") && !line.begins_with("\t") && !line.begins_with("#");
        if (starts_statement) {
            const String name = helper_name(line);
            in_helper = !name.is_empty();
            if (in_helper && (is_automatic_helper(name) || !custom_script_identifier_available(name))) {
                error = "Line " + String::num_int64(row + 1) + ": '" + name + "' is reserved. Choose another helper function name.";
                return {};
            }
            if (line.begins_with("@")) {
                if (annotation_line < 0) {
                    annotation_line = row;
                }
            } else {
                if (in_helper && annotation_line >= 0) {
                    for (int64_t annotation = annotation_line; annotation < row; ++annotation) {
                        helpers.set(annotation, 1);
                    }
                }
                annotation_line = -1;
            }
        }
        helpers.set(row, in_helper ? 1 : 0);
        scan_line(line, state);
    }
    return helpers;
}

/// A typed pin declared in one physical source row after its directive comment.
struct SourcePin {
    /// Pin direction and signature from the authored declaration.
    Dictionary descriptor;
    /// Zero-based authored row with the `var` declaration.
    int64_t row = 0;
    /// Insertion point for bypassing initialization when an input was supplied, or -1 for no initializer.
    int64_t initializer_at = -1;
    /// End of the variable declaration before its attached initializer assignment.
    int64_t type_end = 0;
    /// True when graph data flows into this declaration.
    bool input = false;
};

/// Normalize the editable body once before finding declaration rows or building the language document.
PackedStringArray source_lines(const Dictionary &node) {
    return saved_name(node, "code").replace("\r\n", "\n").replace("\r", "\n").split("\n", true);
}

/// Match the explicit Variant and native object types that have graph wire representations.
bool source_pin_type(const String &type_name, Dictionary &descriptor) {
    if (type_name == "Variant") {
        descriptor["type"] = Variant::NIL;
        return true;
    }
    for (int type = Variant::BOOL; type < Variant::VARIANT_MAX; ++type) {
        if (type != Variant::OBJECT && Variant::get_type_name(static_cast<Variant::Type>(type)) == type_name) {
            descriptor["type"] = type;
            return true;
        }
    }
    if (type_name.is_valid_unicode_identifier() && ClassDB::class_exists(type_name)) {
        const auto api = ClassDBSingleton::get_singleton()->class_get_api_type(type_name);
        if (api == ClassDB::API_CORE || api == ClassDB::API_EXTENSION) {
            descriptor["type"] = Variant::OBJECT;
            descriptor["class"] = StringName(type_name);
            return true;
        }
    }
    if (type_name.is_valid_unicode_identifier()) {
        const Dictionary script = global_script_type_pin(type_name);
        if (!script.is_empty()) {
            descriptor.merge(script, true);
            return true;
        }
    }
    return false;
}

/// Locate top-level directive comments while treating nested code, comments and multiline strings as regular source.
std::vector<SourcePin> read_source_pins(const PackedStringArray &lines, String &error) {
    std::vector<SourcePin> pins;
    std::set<String> names;
    LineState state;
    for (int64_t row = 0; row < lines.size(); ++row) {
        const String &line = lines[row];
        const bool top_level = state.quote == 0 && state.nesting == 0 && !state.continued;
        scan_line(line, state);
        const String marker = line.rstrip(" \t");
        const bool input = marker == "#@input";
        if (!top_level || (!input && marker != "#@output")) {
            continue;
        }
        const String location = "Line " + String::num_int64(row + 1) + " (" + marker + "): ";
        ++row;
        if (row < lines.size()) {
            const String &declaration = lines[row];
            const int64_t separator = declaration.find(":");
            if ((declaration.begins_with("var ") || declaration.begins_with("var\t")) && separator > 3) {
                const String name = declaration.substr(4, separator - 4).strip_edges();
                const int64_t comment = declaration.find("#", separator + 1);
                const int64_t equal = declaration.find("=", separator + 1);
                int64_t statement_end = -1;
                scan_line(declaration, state, &statement_end);
                if (state.quote != 0 || state.nesting != 0 || state.continued) {
                    error = location + String("Keep the pin declaration and its initializer on one line.");
                    return {};
                }
                if (statement_end >= 0) {
                    const String after = declaration.substr(statement_end + 1).strip_edges();
                    if (!after.is_empty() && !after.begins_with("#")) {
                        error = location + String("Put the next statement on a separate line.");
                        return {};
                    }
                }
                const int64_t declaration_end = statement_end >= 0 ? statement_end : declaration.length();
                const bool initialized = equal >= 0 && equal < declaration_end && (comment < 0 || equal < comment);
                const int64_t type_end = initialized ? equal : (comment >= 0 && comment < declaration_end ? comment : declaration_end);
                const String type_name = declaration.substr(separator + 1, type_end - separator - 1).strip_edges();
                if (!custom_script_identifier_available(name) || !names.insert(name).second) {
                    error = location + String("Choose a unique GDScript name for the pin '") + name + "'.";
                    return {};
                }
                Dictionary pin;
                if (!source_pin_type(type_name, pin)) {
                    error = location + String("Specify a built-in Variant type or a native Godot runtime class after ':'.");
                    return {};
                }
                pin["label"] = name;
                pin["source_owned"] = true;
                if (input) {
                    pin["source_default"] = true;
                }
                int64_t initializer_at = initialized ? equal + 1 : -1;
                while (initializer_at >= 0 && initializer_at < declaration.length() &&
                        (declaration[initializer_at] == ' ' || declaration[initializer_at] == '\t')) {
                    ++initializer_at;
                }
                if (initialized && (initializer_at == declaration.length() || initializer_at == comment ||
                        declaration[initializer_at] == ';')) {
                    error = location + String("Supply an initializer after '=' or remove '=' to use the typed default.");
                    return {};
                }
                pins.push_back({pin, row, initializer_at, type_end, input});
                continue;
            }
        }
        error = location + String("Follow the directive immediately with 'var name: Type' on the next top-level line.");
        return {};
    }
    return pins;
}

/// Reuse saved source IDs by name and reserve every old ID while allocating additions.
Dictionary merge_source_pins(const Dictionary &node, const std::vector<SourcePin> &declared, String &error) {
    Dictionary updated = node.duplicate();
    std::set<String> manual_names;
    for (const char *direction : {"inputs", "outputs"}) {
        for (const Variant &entry : saved_list(node, direction)) {
            if (entry.get_type() == Variant::DICTIONARY && !Dictionary(entry).get("source_owned", false)) {
                manual_names.insert(saved_name(entry, "label"));
            }
        }
    }
    for (const char *direction : {"inputs", "outputs"}) {
        const bool input = String(direction) == "inputs";
        Array combined;
        std::set<String> reserved_ids;
        std::map<String, String> saved_ids;
        std::set<String> source_ids;
        for (const Variant &entry : saved_list(node, direction)) {
            if (entry.get_type() == Variant::DICTIONARY) {
                const Dictionary saved = entry;
                const String id = saved_name(saved, "id");
                reserved_ids.insert(id);
                if (saved.get("source_owned", false)) {
                    saved_ids.emplace(saved_name(saved, "label"), id);
                    source_ids.insert(id);
                    continue;
                }
            }
            combined.append(entry);
        }
        for (const SourcePin &pin : declared) {
            if (pin.input != input) {
                continue;
            }
            const String name = saved_name(pin.descriptor, "label");
            if (manual_names.count(name) != 0) {
                error = "Line " + String::num_int64(pin.row + 1) + ": A Details pin is already named '" + name + "'.";
                return {};
            }
            Dictionary descriptor = pin.descriptor.duplicate();
            const auto previous = saved_ids.find(name);
            String id;
            if (previous != saved_ids.end()) {
                id = previous->second;
            } else {
                const String stem = "source_" + name;
                id = stem;
                for (int suffix = 2; !reserved_ids.insert(id).second; ++suffix) {
                    id = stem + String("_") + String::num_int64(suffix);
                }
            }
            source_ids.insert(id);
            descriptor["id"] = StringName(id);
            combined.append(descriptor);
        }
        updated[direction] = combined;
        if (input && !source_ids.empty() && node.get("values", Variant()).get_type() == Variant::DICTIONARY) {
            Dictionary values = Dictionary(node["values"]).duplicate();
            for (const String &id : source_ids) {
                values.erase(StringName(id));
                values.erase(id);
            }
            updated["values"] = values;
        }
    }
    return updated;
}

/// Check the committed array against the source without requiring absent empty pin lists in older nodes.
bool saved_pins_match(const Dictionary &node, const Dictionary &reconciled) {
    for (const char *direction : {"inputs", "outputs"}) {
        const Variant saved = node.get(direction, Array());
        if (saved.get_type() != Variant::ARRAY || Array(saved) != Array(reconciled[direction])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool custom_script_identifier_available(const String &name) {
    return name.is_valid_unicode_identifier() && name != "owner" && name != "graph" && name != "_" &&
            !name.begins_with("__") && !is_gdscript_global(name) &&
            !ClassDB::class_has_method("RefCounted", name) && !ClassDB::class_has_signal("RefCounted", name);
}

Dictionary reconcile_custom_script_pins(const Dictionary &node, String &error) {
    error = String();
    const auto declared = read_source_pins(source_lines(node), error);
    return error.is_empty() ? merge_source_pins(node, declared, error) : Dictionary();
}

CustomScriptSource build_custom_script_source(const Dictionary &document, const Dictionary &graph_body,
        const Dictionary &node, const String &owner_base, CustomScriptSourcePurpose purpose) {
    CustomScriptSource result;
    if (!ClassDB::class_exists(owner_base)) {
        result.error = "Custom Script needs a valid native Godot class for owner.";
        return result;
    }
    const PackedStringArray lines = source_lines(node);
    const auto declared = read_source_pins(lines, result.error);
    if (!result.error.is_empty()) {
        return result;
    }
    const Dictionary effective = merge_source_pins(node, declared, result.error);
    if (!result.error.is_empty()) {
        return result;
    }
    if (purpose == CustomScriptSourcePurpose::RUNTIME && !saved_pins_match(node, effective)) {
        result.error = "The saved Custom Script pins differ from its code. Open Edit Code, apply the changes, and save the graph.";
        return result;
    }

    PackedStringArray parameters;
    parameters.append("__crimson_owner: " + owner_base);
    for (const SourcePin &pin : declared) {
        if (pin.input && purpose == CustomScriptSourcePurpose::RUNTIME) {
            result.uses_input_defaults = true;
            parameters.append("__crimson_missing_inputs: PackedByteArray");
            break;
        }
    }
    PackedStringArray output_names;
    PackedStringArray initializers;
    std::set<String> pin_names;
    std::set<String> pin_ids;
    std::map<String, int64_t> source_input_indices;
    for (const char *direction : {"inputs", "outputs"}) {
        const bool is_input = String(direction) == "inputs";
        const Array pins = saved_list(effective, direction);
        for (int64_t index = 0; index < pins.size(); ++index) {
            const Variant &entry = pins[index];
            if (entry.get_type() != Variant::DICTIONARY) {
                result.error = "Custom Script pin declaration must contain a name and type.";
                return result;
            }
            const Dictionary pin = entry;
            const String label = saved_name(pin, "label");
            const String id = saved_name(pin, "id");
            const Variant type = pin.get("type", Variant());
            if (id.is_empty() || id == (is_input ? "in" : "then") || !pin_ids.insert(String(direction) + "/" + id).second ||
                    pin.get("exec", false) || type.get_type() != Variant::INT ||
                    static_cast<int64_t>(type) < Variant::NIL || static_cast<int64_t>(type) >= Variant::VARIANT_MAX) {
                result.error = "Invalid Custom Script pin definition for '" + label + "'.";
                return result;
            }
            if (!custom_script_identifier_available(label) || !pin_names.insert(label).second) {
                result.error = "Use a unique GDScript name for the pin '" + label + "'.";
                return result;
            }
            const String typed = label + String(": ") + annotation(pin);
            const bool source_owned = pin.get("source_owned", false);
            if (is_input) {
                if (source_owned && purpose == CustomScriptSourcePurpose::RUNTIME) {
                    parameters.append("__crimson_input_" + String::num_int64(index) + ": Variant");
                    source_input_indices.emplace(label, index);
                } else if (!source_owned) {
                    parameters.append(typed);
                }
            } else {
                if (!source_owned) {
                    initializers.append("var " + typed);
                }
                output_names.append(label);
            }
        }
    }

    const String indent = body_indentation(lines);
    result.body_indent = static_cast<int>(indent.length());
    String source = "extends RefCounted\n\nclass __CrimsonGraphContext:\n";
    source += indent + String("var __crimson_read: Callable\n");
    source += indent + String("var __crimson_write: Callable\n");
    source += indent + String("var __crimson_call: Callable\n");
    source += indent + String("var __crimson_emit: Callable\n\n");
    std::set<String> member_names;
    append_variables(source, saved_list(graph_body, "locals"), "local", indent, member_names, result.aliases);
    append_variables(source, saved_list(document, "variables"), "instance", indent, member_names, result.aliases);
    append_methods(source, saved_list(document, "functions"), false, indent, member_names, result.aliases);
    append_methods(source, saved_list(document, "events"), true, indent, member_names, result.aliases);
    source += "var owner: " + owner_base + "\n";
    source += "var graph: __CrimsonGraphContext";
    if (purpose == CustomScriptSourcePurpose::RUNTIME) {
        // Godot's editor analyzer cannot instantiate an inner class in an unsaved virtual document.
        source += " = __CrimsonGraphContext.new()";
    }
    source += "\n";
    source += "var __crimson_completion: RefCounted = RefCounted.new()\n\n";
    source += "func __crimson_configure(read_slot: Callable, write_slot: Callable, invoke: Callable, dispatch_event: Callable) -> RefCounted:\n";
    source += indent + String("graph.__crimson_read = read_slot\n");
    source += indent + String("graph.__crimson_write = write_slot\n");
    source += indent + String("graph.__crimson_call = invoke\n");
    source += indent + String("graph.__crimson_emit = dispatch_event\n");
    source += indent + String("return __crimson_completion\n\n");
    source += "func __crimson_run(" + String(", ").join(parameters) + ") -> Array:\n";
    source += indent + String("owner = __crimson_owner\n");
    for (const String &initializer : initializers) {
        source += indent + initializer + "\n";
    }

    result.body_line = static_cast<int>(source.count("\n"));
    const PackedInt32Array helpers = helper_lines(lines, result.error);
    if (!result.error.is_empty()) {
        return result;
    }
    LineState state;
    int next_body_line = result.body_line;
    // Store per-section offsets; the helper section follows the body and its generated return.
    int next_helper_line = 0;
    String helper_source;
    size_t next_declared_pin = 0;
    for (int64_t row = 0; row < lines.size(); ++row) {
        const String &line = lines[row];
        const bool is_helper = helpers[row] != 0;
        const bool prefix = !is_helper && state.quote == 0;
        result.body_columns.append(prefix ? result.body_indent : 0);
        const String token = scan_line(line, state);
        if (token == "await") {
            result.error = "Line " + String::num_int64(row + 1) + ": Custom Script runs synchronously and cannot await.";
            return result;
        }
        if (token == "return" && !is_helper) {
            result.error = "Line " + String::num_int64(row + 1) + ": Assign your output pins in the body instead of returning a value.";
            return result;
        }
        result.body_lines.append(is_helper ? next_helper_line++ : next_body_line++);
        const SourcePin *declared_pin = next_declared_pin < declared.size() && declared[next_declared_pin].row == row
                ? &declared[next_declared_pin++] : nullptr;
        if (is_helper) {
            helper_source += line + String("\n");
        } else if (declared_pin && declared_pin->input && purpose == CustomScriptSourcePurpose::RUNTIME) {
            const String name = saved_name(declared_pin->descriptor, "label");
            const String index = String::num_int64(source_input_indices.at(name));
            const String argument = "__crimson_input_" + index;
            const String supplied = "__crimson_missing_inputs[" + index + "] == 0";
            const bool has_initializer = declared_pin->initializer_at >= 0;
            source += indent + (has_initializer ? line.substr(0, declared_pin->type_end) : line) + "\n";
            source += indent + String("if ") + supplied + ":\n";
            source += indent + indent + name + " = " + argument + "\n";
            next_body_line += 2;
            if (has_initializer) {
                source += indent + String("else:\n");
                source += indent + indent + name + " = " + line.substr(declared_pin->initializer_at) + "\n";
                next_body_line += 2;
            }
        } else {
            source += (prefix ? indent : String()) + line + "\n";
        }
    }
    source += indent + String("return [__crimson_completion");
    if (!output_names.is_empty()) {
        source += ", " + String(", ").join(output_names);
    }
    const int helper_start = next_body_line + 2;
    for (int64_t row = 0; row < lines.size(); ++row) {
        if (helpers[row] != 0) {
            result.body_lines.set(row, helper_start + result.body_lines[row]);
        }
    }
    result.text = source + "]\n\n" + helper_source;
    return result;
}

} // namespace crimson::visual_scripting
