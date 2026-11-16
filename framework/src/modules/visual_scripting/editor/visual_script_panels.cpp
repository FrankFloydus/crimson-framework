// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_panels.hpp"

#ifdef DEBUG_ENABLED

#include "editor/ui/editor_controls.hpp"
#include "editor/ui/editor_compositions.hpp"
#include "editor/ui/editor_graph.hpp"
#include "editor/ui/editor_spacing.hpp"
#include "modules/visual_scripting/editor/visual_script_authored_enums.hpp"
#include "modules/visual_scripting/editor/visual_script_asset_field.hpp"
#include "modules/visual_scripting/editor/visual_script_editor_style.hpp"
#include "modules/visual_scripting/editor/visual_script_organization.hpp"
#include "modules/visual_scripting/editor/visual_script_help.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_custom_source.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_picker_button.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_selection.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/popup_panel.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/theme.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/tree_item.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace crimson::visual_scripting {
namespace {
using namespace godot;
namespace ui = crimson::editor_ui;

/// The outline rail sits one shade behind the graph and its inspector.
constexpr Color outline_surface(9.0f / 255, 9.0f / 255, 10.0f / 255, 1.0f);
/// Stable Tree button IDs so row actions do not depend on column position.
constexpr int declaration_add_button = 1;
constexpr int variable_export_button = 2;
/// The variable eye uses a native button at the end of its narrow leading cell.
constexpr int variable_eye_column = 0;
constexpr int variable_label_column = 1;
constexpr const char *export_variable_help = "Show this variable in the attached node's Inspector and save overrides per scene instance.";
constexpr const char *unexport_variable_help = "Hide from Inspector and remove overrides set on scene instances.";
constexpr const char *export_variable_unavailable = "Use a type the Inspector can save and a name that is free on the attached node.";

/// Attach a native control to a container owned by the graph window.
template <typename T>
T *add_control(Node *parent, const StringName &name = {}) {
    auto *control = memnew(T);
    if (!name.is_empty()) {
        control->set_name(name);
    }
    parent->add_child(control);
    return control;
}

/// Keep popup styling with its graph workspace after the host moves into Godot's main window.
Ref<Theme> inherited_panel_theme(Node *node) {
    for (auto *ancestor = node; ancestor; ancestor = ancestor->get_parent()) {
        if (auto *control = Object::cast_to<Control>(ancestor)) {
            if (const auto theme = control->get_theme(); theme.is_valid()) return theme;
        } else if (auto *window = Object::cast_to<Window>(ancestor)) {
            return window->get_theme();
        }
    }
    return {};
}

/// Read a document array without converting malformed data.
Array records(const Dictionary &record, const StringName &key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Read a typed record without converting malformed data.
Dictionary object_record(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}

/// Read the stable identifier of a record.
int64_t record_id(const Dictionary &record) {
    const Variant id = record.get("id", Variant());
    return id.get_type() == Variant::INT ? static_cast<int64_t>(id) : 0;
}

/// Return the first ID unused by the supplied records, or zero when exhausted.
int64_t next_id(const Array &entries) {
    int64_t maximum = 0;
    for (const Variant &entry : entries) {
        if (entry.get_type() == Variant::DICTIONARY) {
            maximum = std::max(maximum, record_id(entry));
        }
    }
    return maximum == std::numeric_limits<int64_t>::max() ? 0 : maximum + 1;
}

/// Construct a typed declaration or parameter pin.
Dictionary typed_pin(const StringName &id, const String &label, Variant::Type type, const StringName &object_class = {}) {
    Dictionary pin;
    pin["id"] = id;
    pin["label"] = label;
    pin["type"] = static_cast<int64_t>(type);
    if (type == Variant::OBJECT) {
        pin["class"] = object_class.is_empty() ? StringName("Object") : object_class;
    }
    return pin;
}

/// Return the recorded type or Variant when a legacy pin omitted it.
Variant::Type pin_type(const Dictionary &pin) {
    const int64_t type = pin.get("type", Variant::NIL);
    return type > Variant::NIL && type < Variant::VARIANT_MAX ? static_cast<Variant::Type>(type) : Variant::NIL;
}

/// Display type names alongside declaration labels without changing storage.
String type_name(const Dictionary &pin) {
    if (pin.get("exec", false)) {
        return "Execution";
    }
    if (pin_type(pin) == Variant::OBJECT) {
        return pin.get("script_name", pin.get("class", StringName("Object")));
    }
    const String enum_label = pin.get("enum_label", String());
    if (!enum_label.is_empty()) {
        return enum_label;
    }
    const StringName enumeration = pin.get("enum_type", StringName());
    if (!enumeration.is_empty() && authored_enum_id(enumeration) == 0) {
        return enumeration;
    }
    return pin_type(pin) == Variant::NIL ? String("Variant") : Variant::get_type_name(pin_type(pin));
}

/// Read a collection-side descriptor when it is present.
Dictionary collection_side(const Dictionary &pin, const char *side) {
    const Variant value = pin.get(side, Variant());
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}

/// Resolve a current enum label for the picker without storing that label on declarations.
Dictionary presented_pin(const Dictionary &pin, const Dictionary &document) {
    Dictionary presented = pin.duplicate();
    const String label = authored_enum_label(document, pin.get("enum_type", StringName()));
    if (!label.is_empty()) {
        presented["enum_label"] = label;
    }
    return presented;
}

/// Describe the full declaration type, including any authored collection sides.
String declared_type_name(const Dictionary &pin, const Dictionary &document) {
    if (pin_type(pin) == Variant::ARRAY) {
        const Dictionary element = collection_side(pin, "element");
        if (!element.is_empty()) {
            return String("Array[") + type_name(presented_pin(element, document)) + "]";
        }
    } else if (pin_type(pin) == Variant::DICTIONARY) {
        const Dictionary key = collection_side(pin, "key");
        const Dictionary value = collection_side(pin, "value");
        if (!key.is_empty() || !value.is_empty()) {
            return String("Dictionary[") + type_name(presented_pin(key, document)) + ", " +
                    type_name(presented_pin(value, document)) + "]";
        }
    }
    return type_name(presented_pin(pin, document));
}

/// Enumerated values are drawn and edited by identity, even though Godot stores an int.
bool enum_pin(const Dictionary &pin) {
    return !StringName(pin.get("enum_type", StringName())).is_empty();
}

/// Show the array element or dictionary key as the compact row type.
std::pair<String, Ref<Texture2D>> variable_row_type(Control *host, const Dictionary &pin, const Dictionary &document) {
    const Variant::Type type = pin_type(pin);
    const ui::GraphValueShape shape = ui::graph_value_shape(type, enum_pin(pin));
    if (type == Variant::ARRAY) {
        const Dictionary element = collection_side(pin, "element");
        return { element.is_empty() ? type_name(pin) : type_name(presented_pin(element, document)),
            ui::graph_type_marker(host, shape, ui::graph_pin_color(pin_type(element), enum_pin(element))) };
    }
    if (type == Variant::DICTIONARY) {
        const Dictionary key = collection_side(pin, "key");
        const Dictionary value = collection_side(pin, "value");
        return { key.is_empty() && value.is_empty() ? type_name(pin) : type_name(presented_pin(key, document)),
            ui::graph_type_marker(host, shape, ui::graph_pin_color(pin_type(key), enum_pin(key)),
                    ui::graph_pin_color(pin_type(value), enum_pin(value))) };
    }
    return { type_name(pin), ui::graph_type_marker(host, type, enum_pin(pin)) };
}

/// Read named values from either the active script or the native class database.
Array enum_members(const Dictionary &pin, const Dictionary &document) {
    const StringName enumeration = pin.get("enum_type", StringName());
    if (enumeration.is_empty()) {
        return {};
    }
    return authored_enum_id(enumeration) > 0 ? authored_enum_entries(document, enumeration) : native_enum_entries(enumeration);
}

/// Match saved pin type, object and Script identity, and authored enum identity.
bool same_authored_type(const Dictionary &left, const Dictionary &right) {
    return pin_type(left) == pin_type(right) && left.get("class", Variant()) == right.get("class", Variant()) &&
            left.get("enum_type", Variant()) == right.get("enum_type", Variant()) &&
            left.get("script_path", Variant()) == right.get("script_path", Variant());
}

/// Default to an empty value of the declared type where Godot provides one.
Variant initial_value(const Dictionary &pin) {
    const auto type = pin_type(pin);
    if (type == Variant::OBJECT || type == Variant::NIL) {
        return {};
    }
    return UtilityFunctions::type_convert(Variant(), type);
}

/// Represent common text-backed values without wrapping strings in quotes.
String display_value(const Variant &value, const Dictionary &pin) {
    switch (pin_type(pin)) {
        case Variant::STRING:
        case Variant::STRING_NAME:
        case Variant::NODE_PATH:
            return value.stringify();
        default:
            return UtilityFunctions::var_to_str(value);
    }
}

/// Reject object-bearing literal data inside scalar, array, or dictionary defaults.
bool persistent_value(const Variant &value, int depth = 0) {
    if (depth > 32) {
        return false;
    }
    switch (value.get_type()) {
        case Variant::OBJECT:
        case Variant::CALLABLE:
        case Variant::SIGNAL:
        case Variant::RID:
            return false;
        case Variant::ARRAY:
            for (const Variant &item : Array(value)) {
                if (!persistent_value(item, depth + 1)) {
                    return false;
                }
            }
            return true;
        case Variant::DICTIONARY: {
            const Dictionary entries = value;
            for (const Variant &key : entries.keys()) {
                if (!persistent_value(key, depth + 1) || !persistent_value(entries[key], depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return true;
    }
}

/// Parse the complete numeric field without clamping or accepting truncated input.
template <typename Number>
bool read_number(const String &trimmed, Number &number) {
    if (trimmed.is_empty()) {
        return false;
    }
    const CharString bytes = trimmed.ascii();
    const char *begin = bytes.get_data();
    const char *end = begin + bytes.length();
    if (*begin == '+') {
        ++begin;
        if (begin == end || *begin == '+' || *begin == '-') {
            return false;
        }
    }
    const auto parsed = std::from_chars(begin, end, number);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

/// Parse a default as a literal and validate it against the declared pin.
bool read_value(const String &text, const Dictionary &pin, Variant &result) {
    const String trimmed = text.strip_edges();
    switch (pin_type(pin)) {
        case Variant::STRING:
            result = text;
            return true;
        case Variant::STRING_NAME:
            result = StringName(text);
            return true;
        case Variant::NODE_PATH:
            result = NodePath(text);
            return true;
        case Variant::INT: {
            int64_t number = 0;
            if (!read_number(trimmed, number)) {
                return false;
            }
            result = number;
            return true;
        }
        case Variant::FLOAT: {
            double number = 0;
            if (!read_number(trimmed, number) || !std::isfinite(number)) {
                return false;
            }
            result = number;
            return true;
        }
        case Variant::BOOL:
            if (trimmed != "true" && trimmed != "false") {
                return false;
            }
            result = trimmed == "true";
            return true;
        case Variant::OBJECT:
            result = Variant();
            return trimmed == "null";
        default:
            break;
    }
    if (trimmed.is_empty() || trimmed.contains("Object(") || trimmed.contains("Resource(")) {
        return false;
    }
    result = UtilityFunctions::str_to_var(trimmed);
    return (result.get_type() != Variant::NIL || trimmed == "null") &&
           persistent_value(result) && pin_accepts_value(pin, result);
}

/// Describe the editable axes and exact component type of a vector.
std::pair<int32_t, Variant::Type> vector_default_components(Variant::Type type) {
    switch (type) {
        case Variant::VECTOR2: return {2, Variant::FLOAT};
        case Variant::VECTOR2I: return {2, Variant::INT};
        case Variant::VECTOR3: return {3, Variant::FLOAT};
        case Variant::VECTOR3I: return {3, Variant::INT};
        case Variant::VECTOR4: return {4, Variant::FLOAT};
        case Variant::VECTOR4I: return {4, Variant::INT};
        default: return {0, Variant::NIL};
    }
}

/// Read the definition's array from either the root or its local graph.
Array declaration_records(const Dictionary &document, int64_t graph_id, const StringName &section) {
    return section == StringName("locals") ? records(graph_body(document, graph_id), section) : records(document, section);
}

/// Replace a declaration array without disturbing other graph fields.
Dictionary with_declarations(const Dictionary &document, int64_t graph_id, const StringName &section, const Array &entries) {
    if (section == StringName("locals")) {
        Dictionary body = graph_body(document, graph_id).duplicate();
        body[section] = entries;
        return replace_graph_body(document, graph_id, body);
    }
    Dictionary replacement = document.duplicate();
    replacement[section] = entries;
    return replacement;
}

/// Identify a variable declaration section.
bool is_variable_section(const StringName &section) {
    return section == StringName("variables") || section == StringName("locals");
}

/// Identify a section containing editable graph bodies.
bool is_graph_section(const StringName &section) {
    return section == StringName("functions") || section == StringName("macros");
}

/// Functions and custom events share the Godot method namespace of the attached node.
bool is_callable_section(const StringName &section) {
    return section == StringName("functions") || section == StringName("events");
}

/// Identify authored rows that support declaration clipboard actions.
bool is_editable_declaration_section(const StringName &section) {
    return is_variable_section(section) || is_graph_section(section) || section == StringName("events");
}

/// Check that a proposed name is unique in the declaration's effective namespace.
bool declaration_name_used(const Dictionary &document, int64_t graph_id, const StringName &section,
        int64_t except_id, const String &name, const StringName &base_class) {
    const auto matches = [&](const StringName &key, int64_t ignore) {
        const Array entries = declaration_records(document, graph_id, key);
        return std::any_of(entries.begin(), entries.end(), [&](const Variant &entry) {
            if (entry.get_type() != Variant::DICTIONARY) {
                return false;
            }
            const Dictionary record = entry;
            return record_id(record) != ignore && String(record.get("name", String())) == name;
        });
    };
    if (matches(section, except_id)) {
        return true;
    }
    if (!is_callable_section(section)) {
        return false;
    }
    const StringName other = section == StringName("functions") ? StringName("events") : StringName("functions");
    return name.begins_with("_") || matches(other, 0) || ClassDB::class_has_method(base_class, name);
}

/// Give new named declarations stable initial labels and editable names.
String make_name(const Dictionary &document, int64_t graph_id, const StringName &section,
        const String &prefix, const StringName &base_class) {
    String label = prefix;
    for (int64_t suffix = 2; declaration_name_used(document, graph_id, section, 0, label, base_class); ++suffix) {
        label = prefix + String::num_int64(suffix);
    }
    return label;
}

/// Make an event or function graph node at a known initial canvas position.
Dictionary graph_node(int64_t id, const String &kind, const Vector2 &position, int64_t definition) {
    Dictionary record;
    record["id"] = id;
    record["kind"] = kind;
    record["position"] = position;
    record["definition"] = definition;
    return record;
}

/// A new function and macro open with the entry and return already present.
Dictionary default_body(int64_t definition, bool macro, const Dictionary &return_values = {}) {
    Dictionary wire;
    wire["from"] = 1;
    wire["from_pin"] = StringName("then");
    wire["to"] = 2;
    wire["to_pin"] = StringName("in");
    Dictionary body;
    body["definition"] = definition;
    Dictionary return_node = graph_node(2, "return", Vector2(440, 0), definition);
    if (!return_values.is_empty()) {
        return_node["values"] = return_values;
    }
    body["nodes"] = Array::make(graph_node(1, macro ? "macro_entry" : "function_entry", Vector2(0, 0), definition), return_node);
    body["connections"] = Array::make(wire);
    body["locals"] = Array();
    return body;
}

/// Build the options a declaration can contribute to the graph.
Array declaration_actions(const StringName &section, const Dictionary &record, int64_t graph_id) {
    if (record.is_empty()) {
        return {};
    }
    const String name = record.get("name", String());
    const int64_t id = record_id(record);
    Array options;
    const auto add = [&](const String &kind, const String &label) {
        Dictionary node;
        node["kind"] = kind;
        if (is_variable_section(section)) {
            node["variable"] = id;
            node["scope"] = section == StringName("locals") ? String("local") : String("instance");
        } else {
            node["definition"] = id;
        }
        Dictionary option;
        option["label"] = label;
        option["node"] = node;
        options.append(option);
    };
    if (is_variable_section(section)) {
        add("get_variable", "Get " + name);
        add("set_variable", "Set " + name);
    } else if (section == StringName("functions") || section == StringName("macros")) {
        add(section == StringName("functions") ? "call_function" : "call_macro", "Call " + name);
    } else if (section == StringName("events")) {
        if (graph_id == 0) {
            add("custom_event", "On " + name);
        }
        add("emit_event", "Emit " + name);
    }
    return options;
}

/// Package tree drag options for the canvas to present or insert.
Variant drag_options(const Array &options) {
    if (options.is_empty()) {
        return {};
    }
    Dictionary payload;
    payload["type"] = "crimson_visual_script_symbol";
    payload["options"] = options;
    return payload;
}

/// Return true for nodes the native Scene dock shows in the edited scene.
bool visible_scene_node(const Node *node, Node *scene_root) {
    Node *owner = node->get_owner();
    return node == scene_root || owner == scene_root || (owner && scene_root->is_editable_instance(owner));
}

/// Locate a node that uses the exact graph resource or the same saved script path.
bool owns_script(Node *node, const Ref<VisualScript> &script) {
    const Ref<Script> attached = node->get_script();
    return attached.is_valid() && (attached.ptr() == script.ptr() ||
           (!script->get_path().is_empty() && attached->get_path() == script->get_path()));
}

/// Return the sole selected attachment and flag ambiguous selections without changing the editor selection.
Node *selected_script_owner(Node *scene_root, const Ref<VisualScript> &script, bool &multiple) {
    Node *owner = nullptr;
    for (const Variant &entry : EditorInterface::get_singleton()->get_selection()->get_selected_nodes()) {
        auto *node = Object::cast_to<Node>(entry.get_validated_object());
        if (!node || (node != scene_root && !scene_root->is_ancestor_of(node)) || !owns_script(node, script)) {
            continue;
        }
        if (owner) {
            multiple = true;
            return nullptr;
        }
        owner = node;
    }
    return owner;
}

/// Gather at most two attached candidates so ambiguous references are never authored silently.
void find_owners(Node *node, Node *scene_root, const Ref<VisualScript> &script, Node *&first, bool &multiple) {
    if (multiple || !visible_scene_node(node, scene_root)) {
        return;
    }
    if (owns_script(node, script)) {
        if (first) {
            multiple = true;
            return;
        }
        first = node;
    }
    for (const Variant &child : node->get_children()) {
        if (auto *child_node = Object::cast_to<Node>(child.get_validated_object())) {
            find_owners(child_node, scene_root, script, first, multiple);
        }
    }
}

/// Build a pure typed reference from the attachment to a node in the same edited scene.
Dictionary reference_node(Node *owner, Node *target) {
    Dictionary record;
    if (owner && target) {
        record["kind"] = owner == target ? String("self") : String("scene_node");
        record["source"] = owner->get_path_to(target);
        record["class"] = target->get_class();
        record["label"] = target->get_name();
        describe_attached_script(record, target);
    }
    return record;
}

/// Select a stable port from the authoritative shared pin model.
Dictionary find_pin(const Dictionary &document, const Dictionary &body, const Dictionary &node, bool output,
        const StringName &id, const StringName &base) {
    for (const Variant &entry : graph_pins(document, body, node, output, base)) {
        if (entry.get_type() == Variant::DICTIONARY && StringName(Dictionary(entry).get("id", StringName())) == id) {
            return entry;
        }
    }
    return {};
}

/// Validate one saved edge against the current declarations.
bool accepts_wire(const Dictionary &document, const Dictionary &body, const Array &nodes, const Dictionary &wire,
        const StringName &base) {
    const Dictionary from = find_graph_record(nodes, wire.get("from", 0));
    const Dictionary to = find_graph_record(nodes, wire.get("to", 0));
    if (from.is_empty() || to.is_empty()) {
        return false;
    }
    const Dictionary output = find_pin(document, body, from, true, wire.get("from_pin", StringName()), base);
    const Dictionary input = find_pin(document, body, to, false, wire.get("to_pin", StringName()), base);
    if (output.is_empty() || input.is_empty()) {
        return false;
    }
    const bool execution = output.get("exec", false);
    return execution == static_cast<bool>(input.get("exec", false)) && (execution || pins_compatible(output, input));
}

} // namespace

bool visual_script_edit_breaks_wires(const Dictionary &before, const Dictionary &after, const StringName &base) {
    PackedInt64Array bodies;
    bodies.append(0);
    for (const StringName &section : {StringName("functions"), StringName("macros")}) {
        for (const Variant &entry : records(before, section)) {
            if (entry.get_type() == Variant::DICTIONARY && record_id(entry) > 0) {
                bodies.append(record_id(entry));
            }
        }
    }
    for (const int64_t id : bodies) {
        const Dictionary old_body = graph_body(before, id);
        const Dictionary new_body = graph_body(after, id);
        const Array old_nodes = records(old_body, "nodes");
        const Array new_nodes = records(new_body, "nodes");
        for (const Variant &entry : records(old_body, "connections")) {
            if (entry.get_type() == Variant::DICTIONARY &&
                accepts_wire(before, old_body, old_nodes, entry, base) &&
                !accepts_wire(after, new_body, new_nodes, entry, base)) {
                return true;
            }
        }
    }
    return false;
}

namespace {

/// Detect a local enum reference anywhere in a saved document except its declarations.
bool uses_authored_enum(const Variant &value, const StringName &type, int depth = 0) {
    if (depth > 48) {
        return true;
    }
    if (value.get_type() == Variant::ARRAY) {
        for (const Variant &entry : Array(value)) {
            if (uses_authored_enum(entry, type, depth + 1)) {
                return true;
            }
        }
    } else if (value.get_type() == Variant::DICTIONARY) {
        const Dictionary record = value;
        const Variant saved_type = record.get("enum_type", Variant());
        if ((saved_type.get_type() == Variant::STRING || saved_type.get_type() == Variant::STRING_NAME) &&
                StringName(saved_type) == type) {
            return true;
        }
        for (const Variant &entry : record.values()) {
            if (uses_authored_enum(entry, type, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}

/// Connect a named authored enum choice to an integer-valued mutation.
void choose_enum_value(int64_t index, const Array &values, const Callable &changed) {
    if (index >= 0 && index < values.size()) {
        changed.call(values[index]);
    }
}

/// Create a sidebar surface in editor-scaled pixels.
Ref<StyleBoxFlat> panel_box(Control *control, GraphTone tone, int inset_x = 0, int inset_y = 0, bool framed = false) {
    return ui::box(graph_color(tone), framed ? graph_color(GraphTone::DIVIDER) : Color(0, 0, 0, 0),
            inset_x, inset_y, control->get_theme_default_base_scale());
}

/// Read a neutral native editor glyph for an existing scene or declaration role.
Ref<Texture2D> editor_icon(const StringName &name) {
    const auto theme = EditorInterface::get_singleton()->get_editor_theme();
    return theme.is_valid() && theme->has_icon(name, "EditorIcons") ? theme->get_icon(name, "EditorIcons") : Ref<Texture2D>();
}

/// Set a compact outline glyph without changing the tree item's text or selection.
void row_icon(TreeItem *item, Tree *tree, const StringName &name) {
    const auto icon = editor_icon(name);
    if (icon.is_valid()) {
        item->set_icon(0, icon);
        item->set_icon_max_width(0, ui::pixels(tree, 13));
        item->set_icon_modulate(0, graph_color(GraphTone::MUTED));
    }
}

/// Show the dragged symbol's icon and name near the pointer until the drag ends.
void show_symbol_drag_preview(Tree *source, TreeItem *item, const String &caption) {
    ui::show_drag_preview(source, caption, item->get_icon(0), item->get_icon_modulate(0), "GraphDragPreview");
}

/// Create a flat native tree with a clear active row and compact disclosure icons.
Tree *panel_tree(Node *parent, const StringName &name, bool hidden_root, bool navigation = false) {
    auto *tree = add_control<Tree>(parent, name);
    tree->set_hide_root(hidden_root);
    tree->set_column_clip_content(0, true);
    tree->set_h_scroll_enabled(false);
    tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    tree->set_allow_reselect(true);
    const Color no_fill(0, 0, 0, 0);
    auto selected = panel_box(tree, GraphTone::SELECTED, 2, 1);
    const auto no_frame = ui::box(no_fill, no_fill, 0, 0, tree->get_theme_default_base_scale());
    const auto hover = panel_box(tree, GraphTone::HOVER);
    for (const auto &style : {selected, hover}) {
        style->set_corner_radius_all(ui::pixels(tree, 8));
        style->set_anti_aliased(true);
    }
    const double scale = tree->get_theme_default_base_scale();
    tree->add_theme_stylebox_override("panel", ui::box(navigation ? outline_surface : Color::html("#1f1f20"), no_fill, 6, 3, scale));
    tree->add_theme_stylebox_override("hovered", hover);
    tree->add_theme_stylebox_override("hovered_dimmed", hover);
    tree->add_theme_stylebox_override("button_hover", hover);
    tree->add_theme_stylebox_override("button_pressed", selected);
    for (const char *state : {"selected", "hovered_selected", "selected_focus", "hovered_selected_focus"}) {
        tree->add_theme_stylebox_override(state, selected);
    }
    tree->add_theme_stylebox_override("focus", no_frame);
    tree->add_theme_color_override("font_color", graph_color(GraphTone::MUTED));
    for (const char *state : {"font_selected_color", "font_hovered_color", "font_hovered_selected_color"}) {
        tree->add_theme_color_override(state, graph_color(GraphTone::TEXT));
    }
    tree->add_theme_color_override("font_hovered_dimmed_color", graph_color(GraphTone::MUTED));
    tree->add_theme_constant_override("outline_size", 0);
    tree->add_theme_constant_override("draw_guides", 0);
    tree->add_theme_constant_override("draw_relationship_lines", 0);
    tree->add_theme_constant_override("item_margin", ui::pixels(tree, 16));
    tree->add_theme_constant_override("icon_h_separation", ui::pixels(tree, 6));
    tree->add_theme_constant_override("v_separation", ui::pixels(tree, navigation ? 3 : 4));
    if (navigation) {
        tree->add_theme_constant_override("inner_item_margin_top",
                std::max(0, tree->get_theme_constant("inner_item_margin_top") - ui::pixels(tree, 1)));
    }
    tree->add_theme_font_size_override("font_size", ui::pixels(tree, 13));
    tree->add_theme_icon_override("arrow", ui::icon(tree, "chevron_down"));
    tree->add_theme_icon_override("arrow_collapsed", ui::icon(tree, "chevron_right"));
    return tree;
}

/// Frame a pane independently so the gutter and resize handle remain visible around it.
VBoxContainer *panel_frame(Control *parent, const StringName &name, GraphTone tone, int minimum_height = 0) {
    auto *frame = add_control<PanelContainer>(parent, name);
    frame->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    frame->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    frame->set_custom_minimum_size(Vector2(0, ui::pixels(frame, minimum_height)));
    auto style = panel_box(frame, tone, 0, 0, tone != GraphTone::SIDEBAR);
    if (tone != GraphTone::SIDEBAR) {
        style->set_corner_radius_all(ui::pixels(frame, ui::control_corner_radius));
        style->set_anti_aliased(true);
    } else {
        style->set_bg_color(outline_surface);
    }
    frame->add_theme_stylebox_override("panel", style);
    if (!Object::cast_to<Container>(parent)) {
        frame->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    }
    auto *content = add_control<VBoxContainer>(frame);
    content->add_theme_constant_override("separation", 0);
    return content;
}

/// Give Details a compact title aligned with the declaration rows above it.
void details_header(Node *parent) {
    auto *frame = add_control<PanelContainer>(parent);
    auto surface = panel_box(frame, GraphTone::PANE);
    surface->set_corner_radius(CORNER_TOP_LEFT, ui::pixels(frame, ui::control_corner_radius));
    surface->set_corner_radius(CORNER_TOP_RIGHT, ui::pixels(frame, ui::control_corner_radius));
    surface->set_anti_aliased(true);
    surface->set_border_width(SIDE_BOTTOM, std::max(1, ui::pixels(frame, 1)));
    surface->set_border_color(graph_color(GraphTone::DIVIDER));
    frame->add_theme_stylebox_override("panel", surface);
    frame->set_custom_minimum_size(Vector2(0, ui::pixels(frame, 28)));
    auto *label = ui::label(ui::inset(frame, ui::spacing::lg, ui::spacing::xs), "Details", ui::LabelStyle::HEADING);
    label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    label->add_theme_font_size_override("font_size", ui::pixels(label, 12));
    label->add_theme_constant_override("outline_size", 0);
}

/// Use a quiet rule between Details sections and pin records.
void details_rule(Node *parent) {
    auto *separator = add_control<HSeparator>(parent);
    separator->add_theme_stylebox_override("separator", panel_box(separator, GraphTone::DIVIDER));
    separator->add_theme_constant_override("separation", std::max(1, ui::pixels(separator, 1)));
}

/// Mark the selected item or Details section with the shared medium Inter title.
HBoxContainer *details_title(Node *parent, const String &name, const StringName &icon_name = {}) {
    auto *row = add_control<HBoxContainer>(parent);
    row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    row->add_theme_constant_override("separation", ui::pixels(row, ui::spacing::sm));
    const auto icon = icon_name.is_empty() ? Ref<Texture2D>() : editor_icon(icon_name);
    if (icon.is_valid()) {
        auto *glyph = add_control<TextureRect>(row);
        glyph->set_texture(icon);
        glyph->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
        glyph->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
        glyph->set_custom_minimum_size(Vector2(ui::pixels(glyph, 13), ui::pixels(glyph, 13)));
        glyph->set_self_modulate(graph_color(GraphTone::MUTED));
        glyph->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    }
    auto *label = ui::label(row, name, ui::LabelStyle::HEADING);
    label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
    label->add_theme_color_override("font_color", graph_color(GraphTone::TEXT));
    label->add_theme_font_size_override("font_size", ui::pixels(label, 12));
    label->add_theme_constant_override("outline_size", 0);
    label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
    return row;
}

/// Align a small group of related commands without stretching them into field-width blocks.
HBoxContainer *action_row(Node *parent) {
    auto *row = add_control<HBoxContainer>(parent);
    row->add_theme_constant_override("separation", ui::pixels(row, 4));
    return row;
}

/// Use the same compact height for repeated card controls and sidebar actions.
void compact_details_control(Control *control) {
    control->set_custom_minimum_size(Vector2(0, ui::pixels(control, 24)));
    control->add_theme_font_size_override("font_size", ui::pixels(control, 12));
}

/// Allow the editable text to scroll within a narrow field while keeping the native caret and focus.
void compact_details_text(LineEdit *field) {
    compact_details_control(field);
    field->add_theme_constant_override("minimum_character_width", 0);
}

/// Give a small trailing command enough hit area while keeping its row compact.
void compact_details_icon(Button *button) {
    const int side = ui::pixels(button, 24);
    button->set_custom_minimum_size(Vector2(side, side));
    button->add_theme_constant_override("icon_max_width", ui::pixels(button, 12));
}

/// Draw a secondary declaration command in the framed Details pane.
Button *details_action(Node *parent, const String &name, const String &help, const Callable &command,
        bool add_icon = true) {
    auto *button = ui::button(parent, name, help, command, ui::ButtonStyle::QUIET);
    compact_details_control(button);
    button->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
    button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
    if (add_icon) {
        button->set_button_icon(ui::icon(button, "add"));
        button->add_theme_constant_override("icon_max_width", ui::pixels(button, 13));
    }
    return button;
}

/// Place a labeled add action in a section header without an extra full-width command row.
Button *details_add_button(Node *parent, const StringName &id, const String &name, const String &help, const Callable &command) {
    auto *button = ui::icon_button(parent, "add", help, command, ui::ButtonStyle::QUIET);
    compact_details_icon(button);
    button->set_name(id);
    button->set_accessibility_name(name);
    return button;
}

/// Attach a compact field label above its native control.
void field_label(Node *parent, const String &name, const String &help) {
    auto *label = ui::label(parent, name, ui::LabelStyle::MUTED);
    label->add_theme_color_override("font_color", graph_color(GraphTone::HINT));
    label->set_tooltip_text(help);
    label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
}

/// Logical caption gutter for the compact top-level fields.
constexpr int details_label_width = 76;
/// Pin details have shorter captions and a smaller indent than top-level fields.
constexpr int pin_label_width = 42;
/// Signature inputs also need room for the Default caption.
constexpr int signature_label_width = 52;

/// Fit a caption and native control group side by side or stack them when space runs out.
class DetailsField final : public Container {
    GDCLASS(DetailsField, Container)

    /// Keep the caption and controls alive when the pane changes width.
    Label *caption = nullptr;
    /// Native editor controls maintain their own input and focus state.
    HBoxContainer *controls = nullptr;
    /// Caption width in logical editor pixels, independent of display scale.
    int gutter = details_label_width;
    /// Height depends on whether the allocated row can fit both columns.
    bool stacked = false;

protected:
    /// Internal Details layout with native, separately focused children.
    static void _bind_methods() {}

public:
    /// Measure the stacked width independently of the current row orientation.
    Vector2 _get_minimum_size() const override {
        if (!caption || !controls) return {};
        const Vector2 caption_minimum = caption->get_combined_minimum_size();
        const Vector2 control_minimum = controls->get_combined_minimum_size();
        const real_t height = stacked ? caption_minimum.y + ui::pixels(caption, ui::spacing::xxs) + control_minimum.y :
                std::max(caption_minimum.y, control_minimum.y);
        return Vector2(std::max(caption_minimum.x, control_minimum.x), height);
    }

protected:
    /// Refit the same controls in place when the split or a child minimum changes.
    void _notification(int what) {
        if (what != NOTIFICATION_SORT_CHILDREN || !caption || !controls) return;
        const Vector2 extent = get_size();
        const real_t caption_width = std::max<real_t>(ui::pixels(this, gutter),
                caption->get_combined_minimum_size().x + ui::pixels(caption, ui::spacing::xxs));
        const real_t gap = ui::pixels(this, ui::spacing::sm);
        const real_t readable_controls = std::max<real_t>(ui::pixels(this, 88), controls->get_combined_minimum_size().x);
        const bool next_stacked = extent.x < caption_width + gap + readable_controls;
        if (stacked != next_stacked) {
            stacked = next_stacked;
            update_minimum_size();
        }
        if (stacked) {
            const real_t caption_height = caption->get_combined_minimum_size().y;
            const real_t control_top = caption_height + ui::pixels(this, ui::spacing::xxs);
            fit_child_in_rect(caption, Rect2(0, 0, extent.x, caption_height));
            fit_child_in_rect(controls, Rect2(0, control_top, extent.x, std::max<real_t>(0, extent.y - control_top)));
        } else {
            const real_t control_left = caption_width + gap;
            fit_child_in_rect(caption, Rect2(0, 0, caption_width, extent.y));
            fit_child_in_rect(controls, Rect2(control_left, 0, extent.x - control_left, extent.y));
        }
    }

public:
    /// Keep labels readable at normal width and let fields use the full narrow row.
    HBoxContainer *configure(const String &name, const String &help, int label_width) {
        gutter = label_width;
        set_h_size_flags(SIZE_EXPAND_FILL);
        caption = ui::label(this, name, ui::LabelStyle::MUTED);
        caption->add_theme_font_size_override("font_size", ui::pixels(caption, 11));
        caption->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
        caption->set_mouse_filter(MOUSE_FILTER_PASS);
        ui::tooltip(caption, help.is_empty() ? name : help);
        controls = add_control<HBoxContainer>(this);
        controls->add_theme_constant_override("separation", ui::pixels(controls, ui::spacing::xs));
        return controls;
    }
};

/// Add an adaptive Details field and return its native control group.
HBoxContainer *details_field(Node *parent, const String &name, const String &help, int label_width = details_label_width) {
    return add_control<DetailsField>(parent)->configure(name, help, label_width);
}

/// Keep repeated signature entries together without framing each individual field.
VBoxContainer *pin_card(Node *parent, const StringName &name = {}) {
    auto *frame = add_control<PanelContainer>(parent, name);
    frame->add_theme_stylebox_override("panel", ui::rounded_box(graph_color(GraphTone::HEADER), graph_color(GraphTone::DIVIDER),
            ui::spacing::sm, ui::spacing::xs, frame->get_theme_default_base_scale(), ui::spacing::sm));
    auto *fields = add_control<VBoxContainer>(frame);
    fields->add_theme_constant_override("separation", ui::pixels(fields, ui::spacing::xxs));
    return fields;
}

/// Add a labeled text field that edits only when Enter is pressed.
LineEdit *text_field(Node *parent, const StringName &id, const String &title, const String &text,
        const String &help, const Callable &submit, int label_width = details_label_width) {
    auto *editor = ui::text_input(details_field(parent, title, help, label_width), text, help);
    editor->set_name(id);
    editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    editor->set_accessibility_name(title);
    compact_details_text(editor);
    editor->connect("text_submitted", submit);
    return editor;
}

/// Limit nested collection selectors to types the authoring model can use.
enum class TypeChoices { ALL, ELEMENT, KEY };

/// Draw a declaration name and compact type over the native tree's selection background.
void draw_variable_row(Control *paint, const String &name, const Rect2 &bounds, const String &type,
        const Ref<Texture2D> &marker) {
    auto *tree = Object::cast_to<Tree>(paint->get_parent());
    const auto font = tree->get_theme_font("font", "Tree");
    const int font_size = tree->get_theme_font_size("font_size", "Tree");
    const int type_size = ui::pixels(tree, 11);
    const real_t end = bounds.get_end().x - ui::pixels(tree, ui::spacing::xs);
    const real_t content_width = std::max<real_t>(0, end - bounds.position.x);
    const real_t field_gap = ui::pixels(tree, ui::spacing::sm);
    const real_t marker_gap = ui::pixels(tree, ui::spacing::xs);
    const real_t marker_width = marker.is_valid() ? marker->get_width() : 0;
    const real_t name_width = font->get_string_size(name, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x;
    const real_t type_width = font->get_string_size(type, HORIZONTAL_ALIGNMENT_LEFT, -1, type_size).x;
    const real_t type_prefix = marker_width + (marker.is_valid() ? marker_gap : 0);
    const real_t minimum_name = std::min<real_t>(name_width, content_width * 0.44f);
    const real_t type_budget = std::min(type_width, std::max<real_t>(0, content_width - minimum_name - field_gap - type_prefix));
    const real_t type_left = end - type_budget;
    const real_t marker_left = type_left - type_prefix;
    const real_t name_budget = type_budget > 0 ? marker_left - field_gap - bounds.position.x : content_width;
    const auto baseline = [&](int size) {
        return std::floor(bounds.get_center().y - font->get_height(size) * 0.5f + font->get_ascent(size));
    };
    if (name_budget > 0) {
        paint->draw_string(font, Vector2(bounds.position.x, baseline(font_size)), name, HORIZONTAL_ALIGNMENT_LEFT,
                name_budget, font_size, graph_color(GraphTone::TEXT));
    }
    if (type_budget <= 0) return;
    if (marker.is_valid()) {
        paint->draw_texture(marker, Vector2(marker_left, std::floor(bounds.get_center().y - marker->get_height() * 0.5f)));
    }
    const Color ink = graph_color(GraphTone::MUTED).lerp(outline_surface, 0.17f);
    paint->draw_string(font, Vector2(type_left, baseline(type_size)), type, HORIZONTAL_ALIGNMENT_RIGHT,
            type_budget, type_size, ink);
}

/// Paint custom text above the native selection layer while keeping row geometry and input native.
class VariableRowLabels final : public Control {
    GDCLASS(VariableRowLabels, Control)

    /// A visible cell reported by Tree's own draw pass.
    struct Row {
        /// Declaration name retained independently of a TreeItem's lifetime.
        String name;
        /// The native row content bounds, already adjusted for scrolling.
        Rect2 bounds;
        /// The short type label aligned to the trailing edge of the row.
        String type;
        /// Cached glyph for the selected value type.
        Ref<Texture2D> marker;
    };

    /// Cells gathered during the current native Tree draw.
    std::vector<Row> collecting;
    /// Latest complete set of visible cells, consumed only by this overlay.
    std::vector<Row> rows;

protected:
    /// This internal drawing overlay exposes no script API.
    static void _bind_methods() {}

    /// Use the child CanvasItem, which sits in front of the native Tree selection paint.
    void _notification(int what) {
        if (what == NOTIFICATION_DRAW) {
            const Rect2 bounds(Vector2(), get_size());
            for (const Row &row : rows) {
                if (bounds.has_point(row.bounds.get_center())) {
                    draw_variable_row(this, row.name, row.bounds, row.type, row.marker);
                }
            }
        }
    }

public:
    /// Accept row bounds from a native Tree cell without issuing a draw outside our canvas.
    void collect(TreeItem *item, const Rect2 &bounds, const String &type, const Ref<Texture2D> &marker) {
        collecting.push_back({item->get_text(0), bounds, type, marker});
    }

    /// Tree's draw signal follows its native draw pass and publishes the complete visible set.
    void finish_tree_draw() {
        rows.swap(collecting);
        collecting.clear();
        queue_redraw();
    }
};

/// Compact, searchable type tree used by declaration and signature fields.
class VisualScriptTypePicker final : public VBoxContainer {
    GDCLASS(VisualScriptTypePicker, VBoxContainer)

    /// Opens the bounded popup and shows the current type.
    Button *anchor = nullptr;
    /// Owns the search field and available type tree.
    PopupPanel *popup = nullptr;
    /// Filters values and native classes while retaining their categories.
    LineEdit *search = nullptr;
    /// The option tree contains only names and color markers.
    Tree *types = nullptr;
    /// Persisted type, used to highlight the current choice when its category is expanded.
    Dictionary current;
    /// Receives the selected graph pin descriptor.
    Callable changed;
    /// Named enums authored in the active document.
    Array enum_options;
    /// Native class names are discovered once per opened picker.
    std::array<PackedStringArray, 3> reference_types;
    /// Global classes registered by project Scripts, in author-facing name order.
    std::map<String, Dictionary> script_reference_types;
    /// Native enum names must retain their class qualification to be unambiguous.
    PackedStringArray godot_enums;
    /// A collection authoring field accepts a restricted set of element or key types.
    TypeChoices choices = TypeChoices::ALL;

    /// Decide whether a type belongs in this field and in the current search.
    bool accepts(const Dictionary &pin) const {
        if (choices == TypeChoices::KEY) {
            return collection_key_type_allowed(pin);
        }
        return choices != TypeChoices::ELEMENT || (pin_type(pin) != Variant::ARRAY && pin_type(pin) != Variant::DICTIONARY);
    }

    /// Map Variant types to their compact menu category.
    static int value_group(Variant::Type type) {
        if ((type >= Variant::VECTOR2 && type <= Variant::PROJECTION) || type == Variant::COLOR) {
            return 1;
        }
        return type >= Variant::DICTIONARY ? 2 : 0;
    }

    /// Create a category, expanding matching branches while a search is active.
    TreeItem *category(TreeItem *root, const String &label, bool expanded) {
        auto *item = types->create_item(root);
        item->set_text(0, label);
        item->set_custom_color(0, graph_color(GraphTone::MUTED));
        item->set_collapsed(!expanded);
        return item;
    }

    /// Add a selectable value or object class to the tree.
    TreeItem *option(TreeItem *group, const Dictionary &pin) {
        auto *item = types->create_item(group);
        const String label = type_name(pin);
        item->set_text(0, label);
        item->set_metadata(0, pin);
        item->set_icon(0, ui::graph_type_marker(this, pin_type(pin), enum_pin(pin)));
        item->set_tooltip_text(0, label);
        if (pin_type(pin) == pin_type(current) && pin.get("enum_type", Variant()) == current.get("enum_type", Variant()) &&
                (pin_type(pin) != Variant::OBJECT || type_name(current) == label)) {
            item->set_custom_bg_color(0, graph_color(GraphTone::SELECTED));
        }
        return item;
    }

    /// Offer native node, resource, and other Object types with their normal class names.
    void collect_reference_types() {
        for (const String &name : ClassDB::get_class_list()) {
            const auto api = ClassDBSingleton::get_singleton()->class_get_api_type(name);
            if (api != ClassDBSingleton::API_CORE && api != ClassDBSingleton::API_EXTENSION) {
                continue;
            }
            const int index = ClassDB::is_parent_class(name, "Node") ? 0 :
                    (ClassDB::is_parent_class(name, "Resource") ? 1 : 2);
            reference_types[index].append(name);
        }
        for (auto &group : reference_types) {
            group.sort();
        }
        script_reference_types.clear();
        for (const Dictionary &pin : global_script_type_pins()) {
            script_reference_types.emplace(type_name(pin), pin);
        }
        godot_enums = native_enum_types();
    }

    /// Rebuild only the choices and categories that match the current query.
    void filter(const String &text) {
        const String query = text.strip_edges().to_lower();
        const bool active_search = !query.is_empty();
        const auto matches = [&](const String &text_value) {
            return !active_search || text_value.to_lower().contains(query);
        };
        types->clear();
        auto *root = types->create_item();
        TreeItem *first_match = nullptr;
        TreeItem *exact_match = nullptr;
        const auto add_option = [&](TreeItem *group, const Dictionary &pin) {
            auto *item = option(group, pin);
            if (!first_match) {
                first_match = item;
            }
            if (item->get_text(0).to_lower() == query) {
                exact_match = item;
            }
        };
        constexpr std::array quick_types{Variant::FLOAT, Variant::INT, Variant::STRING, Variant::STRING_NAME, Variant::BOOL,
            Variant::DICTIONARY, Variant::ARRAY, Variant::VECTOR2, Variant::VECTOR3, Variant::OBJECT};
        for (Variant::Type type : quick_types) {
            const Dictionary pin = typed_pin("value", String(), type, type == Variant::OBJECT ? StringName("PackedScene") : StringName());
            if (accepts(pin) && matches(type_name(pin))) {
                add_option(root, pin);
            }
        }

        TreeItem *enums = nullptr;
        for (const Dictionary &choice : enum_options) {
            const String label = choice.get("name", String());
            if (matches(label) || matches("Enums")) {
                Dictionary descriptor = typed_pin("value", String(), Variant::INT);
                descriptor["enum_type"] = choice.get("id", StringName());
                descriptor["enum_label"] = label;
                if (accepts(descriptor)) {
                    if (!enums) {
                        enums = category(root, "Enums", true);
                    }
                    add_option(enums, descriptor);
                }
            }
        }
        if (!enums && !active_search) {
            enums = category(root, "Enums", true);
            auto *hint = types->create_item(enums);
            hint->set_text(0, "Add an enum in My Script");
            hint->set_selectable(0, false);
        }

        const char *group_labels[] = {"Other Types", "Math", "Packed Collections"};
        for (int group_index = 0; group_index < 3; ++group_index) {
            TreeItem *group = nullptr;
            for (int64_t value = Variant::NIL; value < Variant::VARIANT_MAX; ++value) {
                const auto type = static_cast<Variant::Type>(value);
                if (value_group(type) != group_index ||
                        std::find(quick_types.begin(), quick_types.end(), type) != quick_types.end()) {
                    continue;
                }
                const Dictionary pin = typed_pin("value", String(), type);
                if (!accepts(pin) || (!matches(type_name(pin)) && !matches(group_labels[group_index]))) {
                    continue;
                }
                if (!group) {
                    group = category(root, group_labels[group_index], active_search);
                }
                add_option(group, pin);
            }
        }
        const char *reference_labels[] = {"Node References", "Resource References", "Object References"};
        for (size_t index = 0; index < reference_types.size(); ++index) {
            TreeItem *group = nullptr;
            for (const String &name : reference_types[index]) {
                const Dictionary pin = typed_pin("value", String(), Variant::OBJECT, StringName(name));
                if (name == "PackedScene" || !accepts(pin) || (!matches(name) && !matches(reference_labels[index]))) {
                    continue;
                }
                if (!group) {
                    group = category(root, reference_labels[index], active_search);
                }
                add_option(group, pin);
            }
        }
        TreeItem *script_group = nullptr;
        for (const auto &[name, pin] : script_reference_types) {
            if (!accepts(pin) || (!matches(name) && !matches("Script References"))) continue;
            if (!script_group) script_group = category(root, "Script References", active_search);
            add_option(script_group, pin);
        }
        TreeItem *godot_group = nullptr;
        for (const String &label : godot_enums) {
            if (matches(label) || matches("Godot Enums")) {
                Dictionary descriptor = typed_pin("value", String(), Variant::INT);
                descriptor["enum_type"] = StringName(label);
                if (!accepts(descriptor)) {
                    continue;
                }
                if (!godot_group) {
                    godot_group = category(root, "Godot Enums", active_search);
                }
                add_option(godot_group, descriptor);
            }
        }
        if (!first_match) {
            auto *empty = types->create_item(root);
            empty->set_text(0, "No matching types");
            empty->set_selectable(0, false);
        } else if (active_search) {
            auto *result = exact_match ? exact_match : first_match;
            result->select(0);
            types->scroll_to_item(result);
        }
    }

    /// Commit a leaf or toggle the selected category.
    void choose() {
        auto *selection = types->get_selected();
        if (!selection) {
            return;
        }
        const Variant item = selection->get_metadata(0);
        if (item.get_type() != Variant::DICTIONARY) {
            selection->set_collapsed(!selection->is_collapsed());
            return;
        }
        const Dictionary pin = item;
        popup->hide();
        changed.call(pin);
    }

    /// Activate a row with one left click without committing keyboard navigation.
    void click_row(const Vector2 &, int64_t button) {
        if (button == MOUSE_BUTTON_LEFT && types->get_selected() &&
                types->get_selected()->get_metadata(0).get_type() == Variant::DICTIONARY) {
            choose();
        }
    }

    /// Enter commits the current search result.
    void submit(const String &) {
        choose();
    }

    /// Move keyboard navigation from the search field into the tree.
    void search_input(const Ref<InputEvent> &event) {
        const Ref<InputEventKey> key = event;
        if (key.is_valid() && key->is_pressed() && key->get_keycode() == KEY_DOWN) {
            if (!types->get_selected() && types->get_root() && types->get_root()->get_first_child()) {
                types->get_root()->get_first_child()->select(0);
            }
            types->grab_focus();
            search->accept_event();
        }
    }

    /// Return focus to the type field when the popup is dismissed.
    void restore_focus() {
        if (anchor->is_visible_in_tree()) {
            anchor->grab_focus();
        }
    }

    /// Create the popup on demand, leaving closed fields small.
    void build_popup() {
        popup = add_control<PopupPanel>(this, "VisualScriptTypePopup");
        popup->hide();
        popup->set_wrap_controls(true);
        popup->set_theme(inherited_panel_theme(this));
        popup->connect("popup_hide", callable_mp(this, &VisualScriptTypePicker::restore_focus));
        auto *margin = ui::inset(popup, ui::spacing::sm, ui::spacing::sm);
        auto *content = add_control<VBoxContainer>(margin);
        content->add_theme_constant_override("separation", ui::pixels(this, 4));
        search = ui::popup_search_input(content, "Search types", "Filter value types and native Godot classes by name.");
        search->set_name("VisualScriptTypeSearch");
        search->set_accessibility_name("Search types");
        types = panel_tree(content, "VisualScriptTypeTree", true);
        types->set_accessibility_name("Godot types");
        types->add_theme_constant_override("v_separation", ui::pixels(this, 4));
        types->connect("item_mouse_selected", callable_mp(this, &VisualScriptTypePicker::click_row));
        types->connect("item_activated", callable_mp(this, &VisualScriptTypePicker::choose));
        search->connect("text_changed", callable_mp(this, &VisualScriptTypePicker::filter));
        search->connect("text_submitted", callable_mp(this, &VisualScriptTypePicker::submit));
        search->connect("gui_input", callable_mp(this, &VisualScriptTypePicker::search_input));
        collect_reference_types();
    }

    /// Start each browse with all categories collapsed and an empty search.
    void open() {
        if (!popup) {
            build_popup();
        }
        search->clear();
        filter(String());
        const Vector2 origin = anchor->get_global_position();
        const Rect2 bounds = get_viewport_rect();
        const int gap = ui::pixels(this, 4);
        const int width = std::max(1, std::min<int>(bounds.size.x, ui::pixels(this, 248)));
        const int below = std::max(0, static_cast<int>(bounds.get_end().y - origin.y - anchor->get_size().y) - gap);
        const int above = std::max(0, static_cast<int>(origin.y - bounds.position.y) - gap);
        const int preferred = ui::pixels(this, 370);
        const bool flip = preferred > below && above > below;
        const int height = std::max(1, std::min(preferred, flip ? above : below));
        const int x = std::clamp<int>(origin.x, bounds.position.x, bounds.get_end().x - width);
        const int y = flip ? static_cast<int>(origin.y) - height - gap : static_cast<int>(origin.y + anchor->get_size().y) + gap;
        popup->popup_on_parent(Rect2i(x, y, width, height));
        search->grab_focus();
    }

protected:
    /// Internal authoring control.
    static void _bind_methods() {}

public:
    /// Show the current type and send chosen types to the document editor.
    void configure(const Dictionary &pin, const Array &enums, const Callable &select, const String &help,
            const StringName &field_name, TypeChoices allowed_choices) {
        current = pin;
        enum_options = enums;
        changed = select;
        choices = allowed_choices;
        const String selected_type = type_name(pin);
        anchor = ui::button(this, selected_type, String("Current type: ") + selected_type + String(". ") + help,
                callable_mp(this, &VisualScriptTypePicker::open));
        anchor->set_name(field_name);
        anchor->set_accessibility_name("Type");
        anchor->set_theme_type_variation("CrimsonSelect");
        anchor->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        anchor->set_button_icon(ui::graph_type_marker(this, pin_type(pin), enum_pin(pin)));
        for (const auto state : {"icon_normal_color", "icon_hover_color", "icon_pressed_color",
                     "icon_hover_pressed_color", "icon_focus_color"}) {
            anchor->add_theme_color_override(state, Color(1, 1, 1));
        }
        anchor->set_clip_text(true);
        anchor->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        compact_details_control(anchor);
        for (const auto state : {"normal", "hover", "pressed", "hover_pressed"}) {
            const Ref<StyleBox> shared = anchor->get_theme_stylebox(state);
            const Ref<StyleBox> inset = shared->duplicate();
            inset->set_content_margin(SIDE_LEFT, ui::pixels(anchor, 6));
            inset->set_content_margin(SIDE_RIGHT, ui::pixels(anchor, 18));
            anchor->add_theme_stylebox_override(state, inset);
        }
        auto *arrow = add_control<TextureRect>(anchor);
        arrow->set_texture(ui::icon(anchor, "chevron_down"));
        arrow->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
        arrow->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
        arrow->set_mouse_filter(MOUSE_FILTER_IGNORE);
        arrow->set_size(Vector2(ui::pixels(anchor, 12), ui::pixels(anchor, 12)));
        arrow->set_anchors_and_offsets_preset(PRESET_CENTER_RIGHT, PRESET_MODE_KEEP_SIZE, ui::pixels(anchor, ui::spacing::xs));
        set_h_size_flags(SIZE_EXPAND_FILL);
    }
};

/// Create a row of typed choices used by variables and signature pins.
void type_field(Node *parent, const Dictionary &pin, const Dictionary &document, const Callable &select,
        int label_width = details_label_width, const String &caption = "Type",
        const StringName &field_name = "VisualScriptDeclarationType", TypeChoices choices = TypeChoices::ALL) {
    const String help = "Choose the Godot type accepted by this value.";
    add_control<VisualScriptTypePicker>(details_field(parent, caption, help, label_width))->configure(presented_pin(pin, document),
            authored_enum_options(document), select, help, field_name, choices);
}

/// Draw a named enum default without making authors enter integer values.
void enum_default_field(Node *parent, const StringName &field_name, const Array &options, const Variant &value,
        const Callable &changed, bool optional) {
    const String help = "Choose the enum member used when no value is supplied.";
    PackedStringArray labels;
    Array values;
    int64_t selected = -1;
    if (optional) {
        labels.append("No default");
        values.append(Variant());
        selected = value.get_type() == Variant::NIL ? 0 : -1;
    }
    for (const Dictionary &choice : options) {
        const Variant member_value = choice.get("value", Variant());
        if (member_value == value && selected < 0) {
            selected = labels.size();
        }
        labels.append(choice.get("name", choice.get("label", String())));
        values.append(member_value);
    }
    const bool unnamed = selected < 0;
    if (unnamed) {
        selected = labels.size();
        labels.append(value.stringify() + " (unnamed)");
    }
    auto *field = ui::select(details_field(parent, "Default", help, details_label_width), labels, selected,
            callable_mp_static(&choose_enum_value).bind(values, changed));
    field->set_name(field_name);
    compact_details_control(field->get_trigger());
    field->get_trigger()->set_tooltip_text(help);
    field->get_trigger()->set_accessibility_name("Default enum member");
    field->set_item_disabled(selected, unnamed);
}

/// Start a native drag whose payload is valid only within the rendered list that produced it.
Variant drag_authored_row(const Vector2 &, Button *handle, int64_t list_id, int64_t row_id, const String &label) {
    ui::show_drag_preview(handle, handle->get_meta("drag_caption", label), {}, Color(1, 1, 1), "DetailsDragPreview");
    Dictionary payload;
    payload["authored_row_window"] = handle->get_window()->get_instance_id();
    payload["authored_row_list"] = list_id;
    payload["authored_row_id"] = row_id;
    return payload;
}

/// Return the source row for this list or -1 for unrelated drags.
int64_t dragged_authored_row(const Variant &data, int64_t list_id) {
    if (data.get_type() != Variant::DICTIONARY) {
        return -1;
    }
    const Dictionary payload = data;
    return static_cast<int64_t>(payload.get("authored_row_list", 0)) == list_id ?
            static_cast<int64_t>(payload.get("authored_row_id", -1)) : -1;
}

/// Allow reordering over both the row and interactive children with their own mouse input.
void authored_row_drop_target(Control *control, int64_t list_id, int64_t row_id, const Callable &move, const Callable &drag = {}) {
    control->set_drag_forwarding(drag,
            callable_mp_static(+[](const Vector2 &, const Variant &data, int64_t list, int64_t target) {
                const int64_t from = dragged_authored_row(data, list);
                return from >= 0 && from != target;
            }).bind(list_id, row_id),
            callable_mp_static(+[](const Vector2 &, const Variant &data, int64_t list, int64_t target, const Callable &change) {
                const int64_t from = dragged_authored_row(data, list);
                if (from >= 0 && from != target) {
                    change.call_deferred(from, target);
                }
            }).bind(list_id, row_id, move));
}

/// Mark the visible frame of one row for pointer position testing, including its input fields.
void register_authored_row(Control *frame, int64_t list_id, int64_t row_id, const Callable &move) {
    frame->set_meta("authored_row_id", row_id);
    frame->set_meta("authored_row_move", move);
    authored_row_drop_target(frame, list_id, row_id, move);
}

/// Resolve a same-list drop position even when the pointer is over the visible gap between rows.
Control *authored_drop_row(Control *list, const Vector2 &point, int64_t source, bool &after) {
    Control *above = nullptr;
    bool past_source = false;
    for (int index = 0; index < list->get_child_count(); ++index) {
        auto *row = Object::cast_to<Control>(list->get_child(index));
        if (!row || !row->is_visible() || !row->has_meta("authored_row_id")) {
            continue;
        }
        const Rect2 bounds = row->get_global_rect();
        if (point.x < bounds.position.x || point.x >= bounds.get_end().x) {
            return nullptr;
        }
        const int64_t id = row->get_meta("authored_row_id");
        if (bounds.has_point(point)) {
            after = past_source;
            return id == source ? nullptr : row;
        }
        if (point.y < bounds.position.y) {
            after = past_source;
            if (above) {
                auto *target = after ? above : row;
                return int64_t(target->get_meta("authored_row_id")) == source ? nullptr : target;
            }
            if (bounds.position.y - point.y <= ui::pixels(list, ui::spacing::sm) && id != source) {
                return row;
            }
            return nullptr;
        }
        above = row;
        past_source |= id == source;
    }
    after = past_source;
    if (above && int64_t(above->get_meta("authored_row_id")) != source &&
            point.y - above->get_global_rect().get_end().y <= ui::pixels(list, ui::spacing::sm)) {
        return above;
    }
    return nullptr;
}

/// Expose the same move action on a visible grip using pointer or keyboard.
Button *authored_row_grip(Node *parent, int64_t list_id, int64_t row_id, int64_t previous_row, int64_t next_row,
        const String &label, const StringName &name, const Callable &move) {
    auto *handle = ui::drag_grip(parent, "Drag to reorder, or press Alt+Up or Alt+Down.");
    handle->set_name(name);
    handle->set_accessibility_name(String("Reorder ") + label);
    handle->set_meta("previous_authored_row", previous_row);
    handle->set_meta("next_authored_row", next_row);
    authored_row_drop_target(handle, list_id, row_id, move,
            callable_mp_static(&drag_authored_row).bind(handle, list_id, row_id, label));
    handle->connect("gui_input", callable_mp_static(+[](const Ref<InputEvent> &event, Button *grip, int64_t row,
            const Callable &change) {
        const Ref<InputEventKey> key = event;
        if (key.is_null() || !key->is_pressed() || key->is_echo() || !key->is_alt_pressed() || key->is_shift_pressed() ||
                key->is_ctrl_pressed() || key->is_meta_pressed()) {
            return;
        }
        const Key code = key->get_keycode();
        if (code == KEY_UP || code == KEY_DOWN) {
            grip->accept_event();
            const int64_t target = grip->get_meta(code == KEY_UP ? "previous_authored_row" : "next_authored_row");
            if (target >= 0) {
                change.call_deferred(row, target);
            }
        }
    }).bind(handle, row_id, move));
    return handle;
}

/// Capture a pending text edit and its selection independently of the saved Variant value.
Dictionary capture_row_text(LineEdit *field) {
    Dictionary state;
    if (!field) {
        return state;
    }
    state["text"] = field->get_text();
    state["caret"] = field->get_caret_column();
    if (field->has_selection()) {
        state["from"] = field->get_selection_from_column();
        state["to"] = field->get_selection_to_column();
    }
    state["focus"] = field->has_focus();
    state["editing"] = field->is_editing();
    return state;
}

/// Restore a pending edit without emitting an input signal or taking focus from another control.
void restore_row_text(const Dictionary &state, LineEdit *field, bool restore_text) {
    if (state.is_empty() || !field) {
        return;
    }
    if (restore_text) {
        field->set_text(state["text"]);
    }
    if (bool(state["focus"]) && field->is_visible_in_tree()) {
        field->grab_focus();
        if (bool(state["editing"])) {
            field->edit();
        }
        field->set_caret_column(state["caret"]);
        if (state.has("from")) {
            field->select(state["from"], state["to"]);
        }
    }
}

/// Edit real Variant collection entries one row at a time through the owning graph transaction.
class VisualScriptCollectionEdit final : public VBoxContainer {
    GDCLASS(VisualScriptCollectionEdit, VBoxContainer)

    /// Value displayed by this editor instance, replaced on each accepted edit.
    Variant current;
    /// Commit callback supplied by the owning declaration.
    Callable changed;
    /// Apply the exact before and after identity order inside the owning UndoRedo action.
    Callable apply_order;
    /// Monotonic row ID generator owned by the editor window.
    int64_t *next_row_id = nullptr;
    /// Report rejected text and duplicate keys beside the collection being edited.
    Label *feedback = nullptr;
    /// Distinguish indexed arrays from key/value dictionaries.
    bool dictionary = false;
    /// The declaration determines the exact class even when storage contains only asset tokens.
    Dictionary declared_pin;
    /// Keep authored dictionary rows in their own order across text resource saves.
    Array key_order;
    /// Whether the input has a default, separate from its editable empty collection.
    bool has_value = false;
    /// Whether omission and an explicitly empty default are different for this field.
    bool optional_default = false;
    /// Identifies a default within Details. Signature inputs use their own stable pin IDs.
    StringName identity;
    /// A text-backed cell's currently rendered controls and canonical saved text.
    struct Cell {
        /// Line edit carrying any pending text.
        LineEdit *field = nullptr;
        /// Type selector carrying any pending type change.
        ui::Picker *chooser = nullptr;
        /// Available pin types in the same order as picker entries.
        PackedInt32Array types;
        /// Canonical type index and text when the controls were constructed.
        int32_t saved_type = -1;
        String saved_text;
    };
    /// One live row. The ID never changes when rows move, so editor callbacks and in-progress drafts stay with their row.
    struct Row {
        /// Identifier for input and drag gestures within this editor.
        int64_t id;
        /// Dictionary key editor or empty for an array.
        Cell key;
        /// Array element or dictionary value editor.
        Cell value;
    };
    /// Live rows in their current order.
    std::vector<Row> rows;

    /// Resolve a stable editor-session row ID to its current committed index.
    int64_t row_index(int64_t id) const {
        const auto found = std::find_if(rows.begin(), rows.end(), [id](const Row &row) { return row.id == id; });
        return found == rows.end() ? -1 : int64_t(std::distance(rows.begin(), found));
    }

    /// Save the document mutation and its exact row identity order in a single history action.
    bool commit_collection(const Variant &value, const Array &order, const PackedInt64Array &next_ids) {
        if (!is_inside_tree() || is_queued_for_deletion()) {
            return false;
        }
        const PackedInt64Array previous_ids = row_ids();
        if (!bool(changed.call(value, order, apply_order.bind(previous_ids, next_ids),
                                      apply_order.bind(next_ids, previous_ids)))) {
            feedback->set_text("The collection could not be changed.");
            feedback->show();
            return false;
        }
        feedback->hide();
        return true;
    }

    /// Return any native constraint carried by a Godot typed Array or Dictionary.
    Variant::Type constrained_type(bool key) const {
        const Dictionary side = collection_side(declared_pin, dictionary ? (key ? "key" : "value") : "element");
        if (!side.is_empty()) {
            return pin_type(side);
        }
        if (!dictionary) {
            return static_cast<Variant::Type>(Array(current).get_typed_builtin());
        }
        const Dictionary map = current;
        return static_cast<Variant::Type>(key ? map.get_typed_key_builtin() : map.get_typed_value_builtin());
    }

    /// Create a default with the required type or a plain editable string for Variant entries.
    Variant cell_default(bool key) const {
        const auto type = constrained_type(key);
        return type == Variant::NIL ? Variant(String()) : initial_value(typed_pin("value", "Value", type));
    }

    /// Read a cell's current type and value without changing the stored collection.
    Variant cell(int64_t index, bool key) const {
        if (!dictionary) {
            return Array(current)[index];
        }
        const Dictionary map = current;
        const Variant name = key_order[index];
        return key ? name : map[name];
    }

    /// Replace one array value or dictionary cell while protecting other keys and entries.
    bool edit_cell(const Variant &value, int64_t id, bool key) {
        const int64_t index = row_index(id);
        if (index < 0) {
            return false;
        }
        const Variant previous = cell(index, key);
        if (previous.get_type() == value.get_type() && previous == value) {
            feedback->hide();
            return true;
        }
        if (!dictionary) {
            Array array = Array(current).duplicate();
            array[index] = value;
            return commit_collection(array, Array(), row_ids());
        }
        const Dictionary map = current;
        const Variant previous_key = key_order[index];
        Dictionary replacement = map.duplicate();
        Array next_order = key_order.duplicate();
        if (key) {
            const Variant old_value = map[previous_key];
            replacement.erase(previous_key);
            if (replacement.has(value)) {
                feedback->set_text("That key already exists.");
                feedback->show();
                return false;
            }
            replacement[value] = old_value;
            next_order[index] = value;
        } else {
            replacement[previous_key] = value;
        }
        return commit_collection(replacement, next_order, row_ids());
    }

    /// Commit cell text according to the chosen Variant type.
    void submit_cell(const String &text, int64_t id, bool key, ui::Picker *chooser, const PackedInt32Array &types) {
        const int selected = chooser->get_selected_index();
        if (selected < 0 || selected >= types.size()) {
            return;
        }
        const auto type = static_cast<Variant::Type>(types[selected]);
        Variant value;
        if (read_value(text, typed_pin("value", "Value", type), value)) {
            if (edit_cell(value, id, key)) {
                const int64_t index = row_index(id);
                if (index >= 0) {
                    Cell &edited = key ? rows[index].key : rows[index].value;
                    edited.saved_type = selected;
                    edited.saved_text = text;
                }
            }
        } else {
            feedback->set_text("Enter a valid value for this type.");
            feedback->show();
        }
    }

    /// Change one cell editor's type, preserving the entry until the new text is accepted.
    void select_type(int64_t selected, const PackedInt32Array &types, LineEdit *field, int64_t id, bool key) {
        const int64_t index = row_index(id);
        if (index < 0 || selected < 0 || selected >= types.size()) {
            return;
        }
        const auto kind = static_cast<Variant::Type>(types[selected]);
        const Variant value = cell(index, key);
        const Dictionary pin = typed_pin("value", "Value", kind);
        field->set_text(value.get_type() == kind ? display_value(value, pin) : display_value(initial_value(pin), pin));
        field->grab_focus();
        field->select_all();
    }

    /// Draw a compact per-row type selector and editable cell.
    Cell draw_cell(Node *parent, const String &label, int64_t id, bool key) {
        const int64_t index = row_index(id);
        const Variant value = cell(index, key);
        const Variant::Type fixed_type = constrained_type(key);
        const Variant::Type type = fixed_type == Variant::NIL ? value.get_type() : fixed_type;
        const String help = key ? "Edit this unique dictionary key, then press Enter." : "Edit this entry, then press Enter.";
        Node *row = label.is_empty() ? parent : details_field(parent, label, help, pin_label_width);
        const Dictionary side = collection_side(declared_pin, dictionary ? "value" : "element");
        const StringName resource_class = side.get("class", StringName());
        if (!key && fixed_type == Variant::OBJECT && ClassDB::is_parent_class(resource_class, "Resource")) {
            const String resource_help = "Choose or drop a saved " + String(resource_class) + " asset.";
            auto *field = asset_field(row, resource_class, resource_help, value,
                    [this, id, side](const Ref<Resource> &asset) {
                        if (row_index(id) < 0) return false;
                        const Dictionary reference = make_asset_reference(asset);
                        if (asset.is_valid() && (reference.is_empty() || !pin_accepts_value(side, asset))) {
                            feedback->set_text("Choose a compatible saved project asset.");
                            feedback->show();
                            return false;
                        }
                        return edit_cell(asset.is_null() ? Variant() : Variant(reference), id, false);
                    });
            field->set_name("VisualScriptCollectionValue");
            field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            return {};
        }
        if (fixed_type == Variant::OBJECT) {
            ui::label(row, "Set in graph", ui::LabelStyle::MUTED)->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            return {};
        }
        auto *field = ui::text_input(row, display_value(value, typed_pin("value", "Value", type)), help);
        compact_details_text(field);
        field->set_name(key ? "VisualScriptCollectionKey" : "VisualScriptCollectionValue");
        field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        set_row_drop_target(field, id);
        std::vector<ui::PickerOption> options;
        PackedInt32Array types;
        int32_t selected = -1;
        const auto offer_type = [&](Variant::Type option) {
            const int position = static_cast<int>(options.size());
            if (option == type) {
                selected = position;
            }
            ui::PickerOption entry;
            entry.id = StringName(String::num_int64(position));
            entry.label = option == Variant::NIL ? String("Variant") : Variant::get_type_name(option);
            entry.leading_icon = ui::graph_type_marker(field, option);
            options.push_back(std::move(entry));
            types.append(option);
        };
        for (const auto option : {Variant::STRING, Variant::STRING_NAME, Variant::INT, Variant::FLOAT, Variant::BOOL,
                Variant::VECTOR2, Variant::VECTOR3, Variant::COLOR, Variant::NODE_PATH, Variant::NIL}) {
            offer_type(option);
        }
        if (selected < 0) {
            offer_type(type);
        }
        auto *chooser = ui::select(row, {}, -1,
                callable_mp(this, &VisualScriptCollectionEdit::select_type).bind(types, field, id, key),
                fixed_type == Variant::NIL ? ui::FieldState::EDITABLE : ui::FieldState::DISABLED);
        chooser->set_options(std::move(options));
        chooser->select(selected);
        chooser->set_name("VisualScriptCollectionType");
        chooser->set_h_size_flags(Control::SIZE_SHRINK_END);
        chooser->set_custom_minimum_size(Vector2(ui::pixels(chooser, 92), 0));
        chooser->set_popup_minimum_width(156);
        compact_details_control(chooser->get_trigger());
        chooser->get_trigger()->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        set_row_drop_target(chooser->get_trigger(), id);
        const String type_help = "Press Enter in the adjacent field to commit the value.";
        ui::tooltip(chooser->get_trigger(), chooser->get_trigger()->get_text() + String(". ") + type_help);
        field->connect("text_submitted", callable_mp(this, &VisualScriptCollectionEdit::submit_cell).bind(id, key, chooser, types));
        return {field, chooser, types, selected, field->get_text()};
    }

    /// Append an entry with an editable initial value and a distinct key where needed.
    void append_entry() {
        PackedInt64Array next_ids = row_ids();
        next_ids.append((*next_row_id)++);
        if (dictionary) {
            Dictionary map = Dictionary(current).duplicate();
            const auto type = constrained_type(true);
            Variant name = cell_default(true);
            if (type == Variant::STRING || type == Variant::STRING_NAME || type == Variant::NIL) {
                String text = "key";
                for (int64_t suffix = 2; map.has(type == Variant::STRING_NAME ? Variant(StringName(text)) : Variant(text)); ++suffix) {
                    text = "key" + String::num_int64(suffix);
                }
                name = type == Variant::STRING_NAME ? Variant(StringName(text)) : Variant(text);
            } else if (type == Variant::INT || type == Variant::FLOAT) {
                int64_t number = 0;
                while (map.has(type == Variant::INT ? Variant(number) : Variant(static_cast<double>(number)))) {
                    ++number;
                }
                name = type == Variant::INT ? Variant(number) : Variant(static_cast<double>(number));
            } else if (type == Variant::BOOL && map.has(false) && !map.has(true)) {
                name = true;
            }
            if (map.has(name)) {
                feedback->set_text("Rename an existing key to make its initial key available.");
                feedback->show();
                return;
            }
            map[name] = cell_default(false);
            Array next_order = key_order.duplicate();
            next_order.append(name);
            commit_collection(map, next_order, next_ids);
        } else {
            Array array = Array(current).duplicate();
            array.append(cell_default(false));
            commit_collection(array, Array(), next_ids);
        }
    }

    /// Remove one entry and keep the order of the remaining array values.
    void remove_entry(int64_t id) {
        const int64_t index = row_index(id);
        if (index < 0) {
            return;
        }
        PackedInt64Array next_ids = row_ids();
        next_ids.remove_at(index);
        if (dictionary) {
            Dictionary map = Dictionary(current).duplicate();
            map.erase(key_order[index]);
            Array next_order = key_order.duplicate();
            next_order.remove_at(index);
            commit_collection(map, next_order, next_ids);
        } else {
            Array array = Array(current).duplicate();
            array.remove_at(index);
            commit_collection(array, Array(), next_ids);
        }
    }

    /// Remove the optional caller default, leaving the port required.
    void clear_default() {
        commit_collection(Variant(), Array(), PackedInt64Array());
    }

    /// Store an empty optional collection without adding an unwanted entry.
    void set_empty_default() {
        commit_collection(current, key_order, row_ids());
    }

    /// Move a row to its final position, keeping dictionary values independent of display order.
    void move_entry(int64_t source, int64_t target) {
        if (!is_inside_tree() || is_queued_for_deletion()) {
            return;
        }
        const int64_t from = row_index(source);
        const int64_t to = row_index(target);
        Array ordered = dictionary ? key_order.duplicate() : Array(current).duplicate();
        if (from < 0 || to < 0 || from >= ordered.size() || to >= ordered.size() || from == to) {
            return;
        }
        const Variant value = ordered.pop_at(from);
        ordered.insert(to, value);
        PackedInt64Array next_ids = row_ids();
        const int64_t moved_id = next_ids[from];
        next_ids.remove_at(from);
        next_ids.insert(to, moved_id);
        commit_collection(dictionary ? current : Variant(ordered), dictionary ? ordered : Array(), next_ids);
    }

    /// Bind reorder handling without affecting the field's ordinary input behavior.
    void set_row_drop_target(Control *control, int64_t index) {
        authored_row_drop_target(control, get_instance_id(), index, callable_mp(this, &VisualScriptCollectionEdit::move_entry));
    }

protected:
    /// The collection editor is private to the graph workspace.
    static void _bind_methods() {}

public:
    /// Return the stable IDs in their currently rendered list order.
    PackedInt64Array row_ids() const {
        PackedInt64Array ids;
        for (const Row &row : rows) {
            ids.append(row.id);
        }
        return ids;
    }

    /// Identify this first-party editor within a specific declaration.
    const StringName &get_identity() const { return identity; }

    /// Capture only pending fields and focus, along with the rendered data identifying those rows.
    Dictionary capture_drafts() const {
        Dictionary drafts;
        drafts["ids"] = row_ids();
        drafts["pin"] = declared_pin;
        drafts["value"] = has_value ? current : Variant();
        drafts["order"] = key_order;
        Dictionary pending;
        for (const Row &row : rows) {
            Dictionary cells;
            const auto capture = [&cells](const char *name, const Cell &cell) {
                if (!cell.field) {
                    return;
                }
                const int actual_type = cell.chooser->get_selected_index();
                const bool dirty = cell.field->get_text() != cell.saved_text || actual_type != cell.saved_type;
                if (!dirty && !cell.field->has_focus()) {
                    return;
                }
                Dictionary edit = capture_row_text(cell.field);
                edit["dirty"] = dirty;
                if (dirty && actual_type >= 0 && actual_type < cell.types.size()) {
                    edit["type"] = cell.types[actual_type];
                }
                cells[name] = edit;
            };
            capture("key", row.key);
            capture("value", row.value);
            if (!cells.is_empty()) {
                pending[row.id] = cells;
            }
        }
        drafts["pending"] = pending;
        return drafts;
    }

    /// Build the default editor using current Godot Array or Dictionary values.
    void configure(const Dictionary &pin, const Variant &value, const Callable &commit, const Callable &order_callback,
            const StringName &key, bool optional, const Array &stored_order, int64_t &id_source,
            const Dictionary &drafts, const PackedInt64Array &next_ids) {
        declared_pin = pin.duplicate();
        // Labels and defaults may change within one declaration while the cell schema stays the same.
        for (const char *field : {"label", "default", "default_key_order"}) {
            declared_pin.erase(field);
        }
        const auto type = pin_type(pin);
        dictionary = type == Variant::DICTIONARY;
        has_value = value.get_type() == type;
        optional_default = optional;
        current = has_value ? value : (dictionary ? Variant(Dictionary()) : Variant(Array()));
        identity = key;
        next_row_id = &id_source;
        apply_order = order_callback;
        if (resource_collection_pin(pin)) {
            // A native typed collection cannot hold the path/UID tokens stored by the editor.
            const auto encode_resource = [](const Variant &entry) -> Variant {
                if (entry.get_type() != Variant::OBJECT) return entry;
                const Ref<Resource> asset = entry;
                const Dictionary reference = make_asset_reference(asset);
                return reference.is_empty() ? Variant() : Variant(reference);
            };
            if (dictionary) {
                const Dictionary source = current;
                if (source.is_typed()) {
                    Dictionary encoded;
                    for (const Variant &key : source.keys()) encoded[key] = encode_resource(source[key]);
                    current = encoded;
                }
            } else {
                const Array source = current;
                if (source.is_typed()) {
                    Array encoded;
                    for (const Variant &entry : source) encoded.append(encode_resource(entry));
                    current = encoded;
                }
            }
        }
        changed = commit;
        if (dictionary) {
            const Dictionary map = current;
            for (const Variant &key : stored_order) {
                if (map.has(key) && !key_order.has(key)) {
                    key_order.append(key);
                }
            }
            for (const Variant &key : map.keys()) {
                if (!key_order.has(key)) {
                    key_order.append(key);
                }
            }
        }
        add_theme_constant_override("separation", ui::pixels(this, ui::spacing::xs));
        const int64_t count = dictionary ? Dictionary(current).size() : Array(current).size();
        PackedInt64Array restored_ids = next_ids;
        bool keep_drafts = drafts.get("pin", Variant()) == Variant(declared_pin);
        if (!keep_drafts) {
            restored_ids.clear();
        } else if (restored_ids.is_empty() && count > 0) {
            if (drafts.get("value", Variant()) == (has_value ? current : Variant()) &&
                    drafts.get("order", Array()) == Variant(key_order)) {
                restored_ids = drafts.get("ids", PackedInt64Array());
            } else {
                keep_drafts = false;
            }
        }
        if (restored_ids.size() != count) {
            restored_ids.clear();
            keep_drafts = false;
        }
        const Dictionary pending = keep_drafts ? Dictionary(drafts.get("pending", Dictionary())) : Dictionary();
        auto *commands = details_title(this, "Default");
        auto *add = ui::button(commands, dictionary ? "Add Pair" : "Add Item",
                dictionary ? "Add a key and value to this dictionary." : "Add a value to the end of this array.",
                callable_mp(this, &VisualScriptCollectionEdit::append_entry), ui::ButtonStyle::QUIET);
        add->set_button_icon(ui::icon(add, "add"));
        compact_details_control(add);
        add->set_name("VisualScriptAddCollectionEntry");
        if (optional_default && has_value) {
            auto *clear = ui::icon_button(commands, "remove", "Clear the optional collection default.",
                    callable_mp(this, &VisualScriptCollectionEdit::clear_default), ui::ButtonStyle::QUIET);
            compact_details_icon(clear);
            clear->set_accessibility_name("Clear default");
        } else if (optional_default) {
            details_action(this, "Use Empty", "Set an empty collection as the default.",
                    callable_mp(this, &VisualScriptCollectionEdit::set_empty_default), false);
        }
        if (count == 0) {
            auto *hint = ui::label(this, optional_default && !has_value ? "No default value" :
                    dictionary ? "No key-value pairs" : "No array items", ui::LabelStyle::MUTED);
            hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        }
        if (restored_ids.size() != count) {
            for (int64_t index = 0; index < count; ++index) {
                restored_ids.append((*next_row_id)++);
            }
        }
        for (int64_t index = 0; index < count; ++index) {
            const int64_t id = restored_ids[index];
            auto *fields = pin_card(this, "VisualScriptCollectionEntry");
            auto *tools = action_row(fields);
            auto *frame = Object::cast_to<Control>(fields->get_parent());
            auto *ordinal = ui::label(tools, dictionary ? String("Pair ") + String::num_int64(index + 1) : String::num_int64(index + 1),
                    dictionary ? ui::LabelStyle::HEADING : ui::LabelStyle::MUTED);
            ordinal->add_theme_font_size_override("font_size", ui::pixels(ordinal, 11));
            ordinal->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
            Button *grip = nullptr;
            if (count > 1) {
                register_authored_row(frame, get_instance_id(), id,
                        callable_mp(this, &VisualScriptCollectionEdit::move_entry));
                const String label = (dictionary ? String("Pair ") : String("Item ")) + String::num_int64(index + 1);
                grip = authored_row_grip(tools, get_instance_id(), id, index > 0 ? restored_ids[index - 1] : -1,
                        index + 1 < count ? restored_ids[index + 1] : -1, label,
                        "VisualScriptReorderCollectionEntry", callable_mp(this, &VisualScriptCollectionEdit::move_entry));
                tools->move_child(grip, 0);
            }
            rows.push_back({id});
            Row &row = rows.back();
            if (dictionary) {
                ordinal->set_h_size_flags(SIZE_EXPAND_FILL);
            } else {
                row.value = draw_cell(tools, String(), id, false);
            }
            auto *remove = ui::icon_button(tools, "remove", "Remove this collection entry.",
                    callable_mp(this, &VisualScriptCollectionEdit::remove_entry).bind(id), ui::ButtonStyle::QUIET);
            compact_details_icon(remove);
            remove->set_name("VisualScriptRemoveCollectionEntry");
            remove->set_accessibility_name(dictionary ? "Remove pair" : "Remove item");
            set_row_drop_target(remove, id);
            if (dictionary) {
                row.key = draw_cell(fields, "Key", id, true);
                row.value = draw_cell(fields, "Value", id, false);
            }
            const Dictionary saved = pending.get(id, Dictionary());
            const auto restore = [&saved](const char *name, Cell &cell) {
                if (!cell.field || !saved.has(name)) {
                    return;
                }
                const Dictionary state = saved[name];
                const int target = state.has("type") ? cell.types.find(int32_t(state["type"])) : -1;
                if (target >= 0 && target != cell.chooser->get_selected_index()) {
                    cell.chooser->select(target);
                }
                restore_row_text(state, cell.field, state.get("dirty", false));
            };
            restore("key", row.key);
            restore("value", row.value);
        }
        feedback = ui::label(this, String(), ui::LabelStyle::MUTED);
        feedback->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        feedback->hide();
    }
};

/// Return a copy of a pin with a new compatible type and no stale default.
Dictionary change_pin_type(const Dictionary &old_pin, const Dictionary &choice) {
    Dictionary next = typed_pin(old_pin.get("id", StringName()), old_pin.get("label", String()),
            pin_type(choice), choice.get("class", StringName()));
    if (choice.has("enum_type")) {
        next["enum_type"] = choice["enum_type"];
    }
    if (choice.has("script_path")) {
        next["script_path"] = choice["script_path"];
        next["script_name"] = choice["script_name"];
    }
    return next;
}

/// Read one pin from a signature using its stable ID.
Dictionary signature_pin(const Dictionary &record, const StringName &direction, const StringName &id) {
    for (const Variant &entry : records(record, direction)) {
        if (entry.get_type() == Variant::DICTIONARY && StringName(Dictionary(entry).get("id", StringName())) == id) {
            return entry;
        }
    }
    return {};
}

/// List the Event Graph and every reusable graph in this document.
PackedInt64Array document_bodies(const Dictionary &document) {
    PackedInt64Array bodies;
    bodies.append(0);
    for (const StringName &section : {StringName("functions"), StringName("macros")}) {
        for (const Variant &entry : records(document, section)) {
            if (entry.get_type() == Variant::DICTIONARY && record_id(entry) > 0) {
                bodies.append(record_id(entry));
            }
        }
    }
    return bodies;
}

/// Detect a node that refers to the declaration being removed, including its variable scope.
bool refers_to_declaration(const Dictionary &node, const StringName &group, int64_t id) {
    const StringName kind = node.get("kind", StringName());
    if (is_variable_section(group)) {
        const bool local = StringName(node.get("scope", StringName())) == StringName("local");
        return (kind == StringName("get_variable") || kind == StringName("set_variable")) &&
                local == (group == StringName("locals")) && static_cast<int64_t>(node.get("variable", 0)) == id;
    }
    if (static_cast<int64_t>(node.get("definition", 0)) != id) {
        return false;
    }
    if (group == StringName("events")) {
        return kind == StringName("custom_event") || kind == StringName("emit_event");
    }
    return kind == StringName(group == StringName("functions") ? "call_function" : "call_macro");
}

/// Remove references and their incident wires, preserving every other node and layout record.
Dictionary without_declaration_references(const Dictionary &graph, const StringName &group, int64_t id) {
    std::set<int64_t> removed;
    Array nodes;
    for (const Variant &entry : records(graph, "nodes")) {
        if (entry.get_type() == Variant::DICTIONARY && refers_to_declaration(entry, group, id)) {
            removed.insert(record_id(entry));
        } else {
            nodes.append(entry);
        }
    }
    if (removed.empty()) {
        return graph;
    }
    Array wires;
    for (const Variant &entry : records(graph, "connections")) {
        if (entry.get_type() == Variant::DICTIONARY) {
            const Dictionary wire = entry;
            if (removed.count(wire.get("from", 0)) || removed.count(wire.get("to", 0))) {
                continue;
            }
        }
        wires.append(entry);
    }
    Dictionary result = graph.duplicate();
    result["nodes"] = nodes;
    result["connections"] = wires;
    return prune_graph_comment_members(result);
}

/// Rebind copied graph boundary nodes and recursive calls to the copied declaration.
void rebind_declaration_body(Dictionary &record, const StringName &group, int64_t id) {
    const int64_t original = record_id(record);
    Dictionary body = object_record(record, "graph");
    if (body.is_empty()) {
        return;
    }
    body.erase("definition");
    for (const Variant &entry : records(body, "nodes")) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        Dictionary node = entry;
        const StringName kind = node.get("kind", StringName());
        if (kind == StringName("function_entry") || kind == StringName("macro_entry") || kind == StringName("return") ||
                refers_to_declaration(node, group, original)) {
            node["definition"] = id;
        }
    }
}

/// Distinguish authored execution ports from the shared model's implicit lane.
bool has_execution_pin(const Array &pins) {
    return std::any_of(pins.begin(), pins.end(), [](const Variant &value) {
        return value.get_type() == Variant::DICTIONARY && static_cast<bool>(Dictionary(value).get("exec", false));
    });
}

/// Replace the first implicit macro lane with an explicit lane while keeping every existing wire.
Dictionary migrate_macro_execution(Dictionary document, int64_t definition, bool input, const StringName &id) {
    const StringName caller_side(input ? "to" : "from");
    const StringName caller_pin(input ? "to_pin" : "from_pin");
    const StringName caller_old(input ? "in" : "then");
    const StringName body_side(input ? "from" : "to");
    const StringName body_pin(input ? "from_pin" : "to_pin");
    const StringName body_old(input ? "then" : "in");
    const String boundary_kind(input ? "macro_entry" : "return");
    for (const int64_t graph_id : document_bodies(document)) {
        Dictionary body = graph_body(document, graph_id).duplicate();
        const Array nodes = records(body, "nodes");
        Array wires = records(body, "connections").duplicate();
        bool changed = false;
        for (int64_t index = 0; index < wires.size(); ++index) {
            if (wires[index].get_type() != Variant::DICTIONARY) {
                continue;
            }
            Dictionary wire = Dictionary(wires[index]).duplicate();
            if (StringName(wire.get(caller_pin, StringName())) == caller_old) {
                const Dictionary caller = find_graph_record(nodes, wire.get(caller_side, 0));
                if (String(caller.get("kind", String())) == "call_macro" &&
                    static_cast<int64_t>(caller.get("definition", 0)) == definition) {
                    wire[caller_pin] = id;
                }
            }
            if (graph_id == definition && StringName(wire.get(body_pin, StringName())) == body_old) {
                const Dictionary boundary = find_graph_record(nodes, wire.get(body_side, 0));
                if (String(boundary.get("kind", String())) == boundary_kind) {
                    wire[body_pin] = id;
                }
            }
            if (wire != wires[index]) {
                wires[index] = wire;
                changed = true;
            }
        }
        if (changed) {
            body["connections"] = wires;
            document = replace_graph_body(document, graph_id, body);
        }
    }
    return document;
}

} // namespace

void VisualScriptPanels::build_left(Control *container, Commit commit, OpenGraph open_graph, InsertNode insert_node,
        EditCode edit_code) {
    on_commit = std::move(commit);
    on_open_graph = std::move(open_graph);
    on_insert_node = std::move(insert_node);
    on_edit_code = std::move(edit_code);
    auto *symbols = panel_frame(container, "VisualScriptDeclarationsFrame", GraphTone::SIDEBAR, 125);
    declarations = panel_tree(symbols, "VisualScriptDeclarations", true, true);
    declarations->set_h_scroll_enabled(true);
    declarations->set_select_mode(Tree::SELECT_ROW);
    declarations->set_columns(2);
    declarations->set_column_expand(variable_eye_column, false);
    declarations->set_column_clip_content(variable_eye_column, true);
    declarations->set_column_clip_content(variable_label_column, true);
    auto *variable_labels = add_control<VariableRowLabels>(declarations, "VariableRowLabels");
    variable_labels->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    variable_labels->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    variable_labels->set_clip_contents(true);
    declarations->connect("draw", callable_mp(variable_labels, &VariableRowLabels::finish_tree_draw));
    const Color transparent(0, 0, 0, 0);
    const double scale = declarations->get_theme_default_base_scale();
    const auto rail = ui::box(outline_surface, transparent, ui::spacing::md, ui::spacing::xs, scale);
    declarations->add_theme_stylebox_override("panel", rail);
    declarations->add_theme_stylebox_override("hovered_dimmed", declarations->get_theme_stylebox("hovered"));
    declarations->add_theme_stylebox_override("button_hover", ui::box(transparent, transparent));
    const int item_indent = ui::pixels(declarations, 14);
    const int button_gap = ui::pixels(declarations, ui::spacing::xs);
    declarations->add_theme_constant_override("item_margin", item_indent);
    declarations->add_theme_constant_override("button_margin", button_gap);
    const auto eye = ui::icon(declarations, "eye");
    const auto button_frame = declarations->get_theme_stylebox("button_pressed");
    const real_t eye_width = eye.is_valid() ? eye->get_width() : 0;
    const real_t button_inset = button_frame.is_valid() ? button_frame->get_minimum_size().x : 0;
    declarations->set_column_custom_minimum_width(variable_eye_column,
            static_cast<int>(std::ceil(2 * item_indent + eye_width + button_inset + button_gap)));
    declarations->connect("item_selected", callable_mp(this, &VisualScriptPanels::select_declaration));
    declarations->connect("item_activated", callable_mp(this, &VisualScriptPanels::activate_declaration));
    declarations->connect("button_clicked", callable_mp(this, &VisualScriptPanels::press_declaration_button));
    declarations->connect("gui_input", callable_mp(this, &VisualScriptPanels::declaration_input));
    declarations->set_drag_forwarding(callable_mp(this, &VisualScriptPanels::drag_declaration),
            callable_mp(this, &VisualScriptPanels::can_reorder_declaration),
            callable_mp(this, &VisualScriptPanels::reorder_declaration));
    declaration_drag_rail = ui::drag_insertion_rail(declarations);
    declaration_menu = add_control<PopupMenu>(declarations, "VisualScriptDeclarationMenu");
    declaration_menu->set_theme(inherited_panel_theme(declarations));
    declaration_menu->connect("id_pressed", callable_mp(this, &VisualScriptPanels::declaration_menu_command));
    declaration_menu->connect("popup_hide", callable_mp(static_cast<Control *>(declarations), &Control::grab_focus).bind(false));

    EditorInterface::get_singleton()->get_selection()->connect("selection_changed",
            callable_mp(this, &VisualScriptPanels::remember_scene_owner));
}

void VisualScriptPanels::build_right(Control *container) {
    auto *layout = panel_frame(container, "VisualScriptDetailsFrame", GraphTone::PANE);
    details_header(layout);
    auto *scroll = add_control<ScrollContainer>(layout, "VisualScriptDetailsScroll");
    details_scroll = scroll;
    details_drag_rail = ui::drag_insertion_rail(layout);
    scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
    scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_RESERVE);
    scroll->set_follow_focus(true);
    scroll->add_theme_stylebox_override("panel", ui::box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 0,
            scroll->get_theme_default_base_scale()));
    details = add_control<VBoxContainer>(ui::inset(scroll, ui::spacing::md, ui::spacing::sm), "VisualScriptDetails");
    details->add_theme_constant_override("separation", ui::pixels(details, ui::spacing::sm));
    details->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    rebuild_details();
}

void VisualScriptPanels::_notification(int what) {
    if (what == NOTIFICATION_DRAG_END || what == NOTIFICATION_WM_MOUSE_EXIT || what == NOTIFICATION_WM_WINDOW_FOCUS_OUT) {
        if (declaration_drag_rail) declaration_drag_rail->hide();
        if (details_drag_rail) details_drag_rail->hide();
    }
}

void VisualScriptPanels::close_popups() {
    if (declaration_menu && declaration_menu->is_visible()) {
        declaration_menu->hide();
    }
    if (!details) return;
    for (const Variant &item : details->find_children("*", "Window", true, false)) {
        auto *popup = Object::cast_to<Window>(item.get_validated_object());
        if (popup && popup->is_visible()) popup->hide();
    }
}

void VisualScriptPanels::_input(const Ref<InputEvent> &event) {
    const Ref<InputEventMouse> pointer = event;
    if (pointer.is_null() || !details || !declarations || !details->is_visible_in_tree() || !get_viewport()->gui_is_dragging()) {
        return;
    }
    const Ref<InputEventMouseMotion> motion = pointer;
    const Ref<InputEventMouseButton> button = pointer;
    const bool released = button.is_valid() && button->get_button_index() == MOUSE_BUTTON_LEFT && !button->is_pressed();
    const Variant data = get_viewport()->gui_get_drag_data();
    if ((!released && motion.is_null()) || data.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary payload = data;
    const Vector2 point = pointer->get_position();
    // Native Viewport drop routing queries the OS cursor again. Use this event's position for list hit testing.
    // This also treats controls inside a row, such as a text field or a Select, as part of the target.
    const int64_t list_id = payload.get("authored_row_list", 0);
    if (list_id > 0 && uint64_t(payload.get("authored_row_window", 0)) == get_viewport()->get_instance_id()) {
        details_drag_rail->hide();
        auto *list = Object::cast_to<Control>(ObjectDB::get_instance(list_id));
        if (list && details->is_ancestor_of(list) && list->is_visible_in_tree()) {
            const Rect2 clip = details_scroll->get_global_rect();
            if (clip.has_point(point)) {
                const int64_t from = dragged_authored_row(data, list_id);
                bool after = false;
                if (auto *target = from >= 0 ? authored_drop_row(list, point, from, after) : nullptr) {
                    if (released) {
                        const Callable move = target->get_meta("authored_row_move");
                        move.call_deferred(from, target->get_meta("authored_row_id"));
                    } else {
                        ui::place_drag_insertion_rail(details_drag_rail, target->get_global_rect(), clip, after);
                    }
                }
            }
        }
        if (released) {
            get_viewport()->gui_cancel_drag();
            get_viewport()->set_input_as_handled();
        }
    } else if (payload.has("declaration")) {
        declaration_drag_rail->hide();
        if (declarations->get_global_rect().has_point(point)) {
            const Vector2 local = declarations->get_global_transform().affine_inverse().xform(point);
            if (can_reorder_declaration(local, data)) {
                if (released) {
                    callable_mp(this, &VisualScriptPanels::reorder_declaration).call_deferred(local, data);
                } else {
                    Rect2 area = declarations->get_item_area_rect(declarations->get_item_at_position(local));
                    area.position += declarations->get_global_position();
                    ui::place_drag_insertion_rail(declaration_drag_rail, area, declarations->get_global_rect(),
                            point.y >= area.get_center().y);
                }
            }
            if (released) {
                get_viewport()->gui_cancel_drag();
                get_viewport()->set_input_as_handled();
            }
        }
    }
}

void VisualScriptPanels::refresh(const Dictionary &next_document, int64_t next_graph_id, const Ref<VisualScript> &next_script) {
    const auto address = selected_declaration_address();
    const bool keep_details = !details_applied_document.is_empty() &&
            !collection_refresh_pending &&
            next_script == script && next_graph_id == graph_id && next_document == details_applied_document &&
            address.section == details_applied_address.section && address.id == details_applied_address.id;
    details_applied_document = Dictionary();
    if (script != next_script) {
        section = StringName();
        declaration_id = 0;
        graph_node_id = 0;
        remembered_scene_owner = ObjectID();
        folded_signature_pins.clear();
        declaration_clipboard.clear();
        clipboard_dependencies.clear();
        clipboard_script = 0;
        details_section_expansion.clear();
    }
    if (graph_id != next_graph_id) {
        graph_node_id = 0;
        if (section == StringName("locals")) {
            section = StringName();
            declaration_id = 0;
        }
    }
    update_document(next_document);
    graph_id = next_graph_id;
    script = next_script;
    remember_scene_owner();
    rebuild_declarations();
    if (!keep_details) {
        rebuild_details();
    }
}

void VisualScriptPanels::set_collection_commit(CommitCollection commit) {
    on_collection_commit = std::move(commit);
}

void VisualScriptPanels::discard_collection_drafts() {
    draft_address = {};
    collection_orders.clear();
}

void VisualScriptPanels::update_document(const Dictionary &next_document) {
    check_clipboard_dependencies(next_document);
    document = next_document;
}

void VisualScriptPanels::select_graph_node(const Dictionary &node) {
    const int64_t id = record_id(node);
    if (id == 0 && graph_node_id == 0) {
        return;
    }
    section = StringName();
    declaration_id = 0;
    graph_node_id = id;
    if (declarations) {
        declarations->deselect_all();
    }
    rebuild_details();
}

Dictionary VisualScriptPanels::new_custom_event(const Dictionary &source, int64_t &event_id, const String &name,
        const Array &inputs) const {
    event_id = script.is_valid() ? next_definition_id(source) : 0;
    if (event_id == 0) {
        return source;
    }
    Dictionary declaration;
    declaration["id"] = event_id;
    declaration["name"] = name.is_empty() ? make_name(source, 0, "events", "event", script->get_graph_base()) : name;
    declaration["inputs"] = inputs;
    Array events = records(source, "events").duplicate();
    events.append(declaration);
    return with_declarations(source, 0, "events", events);
}

bool VisualScriptPanels::can_create_named_callable(const String &name) const {
    return script.is_valid() && name.is_valid_identifier() &&
            !declaration_name_used(document, 0, "functions", 0, name, script->get_graph_base());
}

bool VisualScriptPanels::create_named_function(const String &name, const Array &inputs, const Array &outputs,
        const Dictionary &return_values) {
    if (!on_commit || !can_create_named_callable(name)) {
        return false;
    }
    const int64_t id = next_definition_id(document);
    if (id == 0) {
        return false;
    }
    Dictionary record;
    record["id"] = id;
    record["name"] = name;
    record["inputs"] = inputs;
    record["outputs"] = outputs;
    record["graph"] = default_body(id, false, return_values);
    Array functions = records(document, "functions").duplicate();
    functions.append(record);
    on_commit("Add " + name.capitalize(), with_declarations(document, 0, "functions", functions));
    show_declaration("functions", id);
    if (on_open_graph) {
        on_open_graph(id);
    }
    return true;
}

void VisualScriptPanels::show_declaration(const StringName &key, int64_t id) {
    section = key;
    declaration_id = id;
    graph_node_id = 0;
    if (!find_graph_record(declaration_records(document, graph_id, key), id).is_empty()) {
        rebuild_declarations();
        rebuild_details();
    }
}

Dictionary VisualScriptPanels::selected_symbol() const {
    if (declaration_id <= 0 || selected_declaration().is_empty()) {
        return {};
    }
    Dictionary symbol;
    symbol["section"] = section;
    symbol["id"] = declaration_id;
    symbol["owner"] = section == StringName("locals") ? graph_id : 0;
    return symbol;
}

Node *VisualScriptPanels::script_owner(Node *scene_root) const {
    if (!scene_root || script.is_null()) {
        return nullptr;
    }
    bool multiple = false;
    if (auto *selected = selected_script_owner(scene_root, script, multiple); selected || multiple) {
        return selected;
    }
    auto *remembered = remembered_scene_owner.is_valid() ?
            Object::cast_to<Node>(ObjectDB::get_instance(remembered_scene_owner)) : nullptr;
    if (remembered && (remembered == scene_root || scene_root->is_ancestor_of(remembered)) &&
            visible_scene_node(remembered, scene_root) && owns_script(remembered, script)) {
        return remembered;
    }
    Node *first = nullptr;
    find_owners(scene_root, scene_root, script, first, multiple);
    return multiple ? nullptr : first;
}

void VisualScriptPanels::remember_scene_owner() {
    auto *root = EditorInterface::get_singleton()->get_edited_scene_root();
    if (!root || script.is_null()) return;
    bool multiple = false;
    if (auto *owner = selected_script_owner(root, script, multiple)) {
        remembered_scene_owner = ObjectID(owner->get_instance_id());
    } else if (multiple) {
        remembered_scene_owner = ObjectID();
    }
}

void VisualScriptPanels::rebuild_declarations() {
    if (!declarations) {
        return;
    }
    std::map<StringName, bool> collapsed;
    if (auto *old_root = declarations->get_root()) {
        for (auto *group = old_root->get_first_child(); group; group = group->get_next()) {
            const Variant value = group->get_metadata(0);
            if (value.get_type() == Variant::STRING_NAME) {
                collapsed[value] = group->is_collapsed();
            }
        }
    }
    refreshing = true;
    declarations->clear();
    auto *variable_labels = Object::cast_to<VariableRowLabels>(declarations->get_node_or_null(NodePath("VariableRowLabels")));
    auto *root = declarations->create_item();
    const auto active_row = ui::box(graph_color(GraphTone::SELECTED),
            Color(0, 0, 0, 0), 0, 0, declarations->get_theme_default_base_scale());
    active_row->set_corner_radius_all(ui::pixels(declarations, 8));
    active_row->set_anti_aliased(true);
    active_row->set_expand_margin(SIDE_LEFT, -ui::pixels(declarations, 2));
    active_row->set_expand_margin(SIDE_RIGHT, -ui::pixels(declarations, 3));

    const auto append_group = [&](const StringName &key, const String &label, const String &creation_label) {
        auto *group = declarations->create_item(root);
        group->set_text(0, label);
        group->set_metadata(0, key);
        group->set_selectable(0, false);
        group->set_selectable(variable_label_column, false);
        group->set_expand_right(0, true);
        group->set_custom_color(0, graph_color(GraphTone::MUTED));
        group->set_custom_font(0, declarations->get_theme_font("font", "CrimsonHeading"));
        group->set_custom_font_size(0, ui::pixels(declarations, 11));
        if (script.is_valid()) {
            group->add_button(0, ui::icon(declarations, "add"), declaration_add_button, false, creation_label, creation_label);
        }
        const auto last_fold = collapsed.find(key);
        group->set_collapsed(last_fold != collapsed.end() && last_fold->second && section != key);
        for (const Variant &entry : declaration_records(document, graph_id, key)) {
            if (entry.get_type() != Variant::DICTIONARY || record_id(entry) <= 0) {
                continue;
            }
            const Dictionary record = entry;
            auto *item = declarations->create_item(group);
            const String name = record.get("name", String());
            item->set_text(0, name);
            item->set_custom_color(0, graph_color(GraphTone::TEXT));
            Dictionary metadata;
            metadata["section"] = key;
            metadata["id"] = record_id(record);
            item->set_metadata(0, metadata);
            const String description = record.get("description", String());
            if (is_variable_section(key)) {
                const Dictionary pin = presented_pin(object_record(record, "pin"), document);
                const auto [type, marker] = variable_row_type(declarations, pin, document);
                item->set_custom_color(0, Color(0, 0, 0, 0));
                item->set_cell_mode(variable_label_column, TreeItem::CELL_MODE_CUSTOM);
                item->set_custom_draw_callback(variable_label_column,
                        callable_mp(variable_labels, &VariableRowLabels::collect).bind(type, marker));
                const String action = "Drag into the graph to read this variable. Hold Ctrl while dropping to write it.";
                const String help = name + String(" (") + declared_type_name(pin, document) + "). " +
                        (description.is_empty() ? action : description);
                item->set_tooltip_text(0, help);
                item->set_tooltip_text(variable_label_column, help);
                if (key == StringName("variables") && script.is_valid()) {
                    const bool exported = record.get("exported", false);
                    const bool available = exported || script->can_export_for_owners(name, pin);
                    const String label = exported ? "Hide from Inspector" : "Show in Inspector";
                    const String export_help = exported ? unexport_variable_help : (available ? export_variable_help : export_variable_unavailable);
                    item->add_button(variable_eye_column, ui::icon(declarations, exported ? "eye" : "eye_off", exported),
                            variable_export_button, !available, export_help, label);
                }
            } else if (is_graph_section(key)) {
                item->set_expand_right(0, true);
                row_icon(item, declarations, "MemberMethod");
                item->set_tooltip_text(0, description.is_empty() ?
                        String("Double-click to edit the graph. Drag into a graph to place a call.") : description);
                if (record_id(record) == graph_id) {
                    item->set_custom_color(0, graph_color(GraphTone::TEXT));
                    item->set_custom_stylebox(0, active_row);
                }
            } else if (key == StringName("enums")) {
                item->set_expand_right(0, true);
                item->set_icon(0, ui::graph_type_marker(declarations, Variant::INT, true, 13));
                item->set_tooltip_text(0, "Select to edit the enum's named integer values.");
            } else {
                item->set_expand_right(0, true);
                row_icon(item, declarations, "Signal");
                item->set_tooltip_text(0, description.is_empty() ?
                        String("Drag to add the event entry or emit the event.") : description);
            }
            if (key == section && record_id(record) == declaration_id) {
                item->select(0);
            }
        }
    };
    append_group("variables", "Variables", "Add instance variable");
    if (graph_id > 0 && !find_graph_record(records(document, "functions"), graph_id).is_empty()) {
        append_group("locals", "Local Variables", "Add variable local to this function");
    }
    append_group("functions", "Functions", "Add function");
    append_group("macros", "Macros", "Add macro");
    append_group("events", "Custom Events", "Add custom event");
    append_group("enums", "Enums", "Add an enum to this visual script");
    refreshing = false;
}

VisualScriptPanels::DeclarationAddress VisualScriptPanels::selected_declaration_address() const {
    if (graph_node_id == 0) {
        return {section, declaration_id};
    }
    const Dictionary node = find_graph_record(records(graph_body(document, graph_id), "nodes"), graph_node_id);
    const String kind = node.get("kind", String());
    if (kind == "get_variable" || kind == "set_variable") {
        const String scope = node.get("scope", String());
        return {StringName(scope == "local" ? "locals" : "variables"), node.get("variable", 0)};
    }
    if (kind == "custom_event" || kind == "emit_event") {
        return {StringName("events"), node.get("definition", 0)};
    }
    if (kind == "call_function" || kind == "call_macro") {
        return {StringName(kind == "call_function" ? "functions" : "macros"), node.get("definition", 0)};
    }
    if (kind == "switch_enum") {
        const int64_t id = authored_enum_id(node.get("enum_type", StringName()));
        if (id > 0) {
            return {StringName("enums"), id};
        }
    }
    return {};
}

Dictionary VisualScriptPanels::selected_declaration() const {
    const auto address = selected_declaration_address();
    return address.id > 0 ? find_graph_record(declaration_records(document, graph_id, address.section), address.id) :
            Dictionary();
}

void VisualScriptPanels::rebuild_details() {
    if (!details) {
        return;
    }
    const auto address = selected_declaration_address();
    const uint64_t script_id = script.is_valid() ? uint64_t(script->get_instance_id()) : 0;
    const bool same_scope = address.id > 0 && address == draft_address && draft_graph == graph_id && draft_script == script_id;
    collection_drafts.clear();
    enum_drafts.clear();
    if (same_scope) {
        for (const Variant &node : details->find_children("VisualScript*Default*", "", true, false)) {
            auto *collection = Object::cast_to<VisualScriptCollectionEdit>(node.get_validated_object());
            if (collection) {
                collection_drafts[collection->get_identity()] = collection->capture_drafts();
            }
        }
        if (auto *members = details->find_child("VisualScriptEnumMembers", true, false)) {
            for (int index = 0; index < members->get_child_count(); ++index) {
                auto *row = members->get_child(index);
                Dictionary cells;
                for (const char *name : {"VisualScriptEnumMemberName", "VisualScriptEnumMemberValue"}) {
                    auto *field = Object::cast_to<LineEdit>(row->find_child(name, true, false));
                    if (!field) {
                        continue;
                    }
                    const bool dirty = field->get_text() != String(field->get_meta("saved_text", String()));
                    if (dirty || field->has_focus()) {
                        Dictionary state = capture_row_text(field);
                        state["dirty"] = dirty;
                        cells[name] = state;
                    }
                }
                if (!cells.is_empty()) {
                    enum_drafts[row->get_meta("authored_row_id")] = cells;
                }
            }
        }
    } else {
        ++draft_scope;
        collection_orders.clear();
    }
    draft_address = address;
    draft_graph = graph_id;
    draft_script = script_id;
    details_applied_document = Dictionary();
    collection_refresh_pending = false;
    details_feedback = nullptr;
    for (const Variant &child : details->get_children()) {
        if (auto *child_node = Object::cast_to<Node>(child.get_validated_object())) {
            details->remove_child(child_node);
            child_node->queue_free();
        }
    }
    const Dictionary record = address.id > 0 ?
            find_graph_record(declaration_records(document, graph_id, address.section), address.id) : Dictionary();
    if (!record.is_empty()) {
        draw_declaration(record, address.section);
    } else if (graph_node_id > 0) {
        const Dictionary node = find_graph_record(records(graph_body(document, graph_id), "nodes"), graph_node_id);
        if (!node.is_empty()) {
            draw_graph_node(node);
        }
    } else {
        auto *empty = ui::label(details, "Select a node or declaration.", ui::LabelStyle::MUTED);
        empty->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    }
    details_feedback = ui::label(details, "", ui::LabelStyle::MUTED);
    details_feedback->set_name("VisualScriptDetailsFeedback");
    details_feedback->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    details_feedback->hide();
    collection_drafts.clear();
    collection_orders.clear();
    enum_drafts.clear();
}

VBoxContainer *VisualScriptPanels::details_section(const StringName &id, const String &title, const String &help,
        bool initially_expanded, HBoxContainer **actions) {
    auto *rows = memnew(VBoxContainer);
    const auto saved = details_section_expansion.find(id);
    const bool expanded = saved == details_section_expansion.end() ? initially_expanded : saved->second;
    auto *disclosure = ui::disclosure(details, title, rows, expanded, help);
    disclosure->set_name(id);
    disclosure->set_clip_text(true);
    disclosure->set_accessibility_name(title);
    compact_details_control(disclosure);
    disclosure->add_theme_stylebox_override("pressed", panel_box(disclosure, GraphTone::GROUP, 4, 3));
    disclosure->add_theme_stylebox_override("hover_pressed", panel_box(disclosure, GraphTone::HOVER, 4, 3));
    disclosure->connect("toggled", callable_mp(this, &VisualScriptPanels::set_details_section_expanded).bind(id));
    rows->add_theme_constant_override("separation", ui::pixels(rows, ui::spacing::xs));
    if (actions) {
        auto *section = disclosure->get_parent();
        auto *heading = action_row(section);
        section->move_child(heading, 0);
        disclosure->reparent(heading, false);
        disclosure->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        *actions = action_row(heading);
        (*actions)->set_visible(expanded);
        disclosure->connect("toggled", callable_mp(static_cast<CanvasItem *>(*actions), &CanvasItem::set_visible));
    }
    return rows;
}

void VisualScriptPanels::set_details_section_expanded(bool expanded, const StringName &id) {
    details_section_expansion[id] = expanded;
}

void VisualScriptPanels::draw_collection(Node *parent, const Dictionary &pin, const Variant &value, const Callable &commit,
        const StringName &key, bool optional, const Array &key_order) {
    const Dictionary drafts = collection_drafts.get(key, Dictionary());
    const PackedInt64Array next_ids = collection_orders.get(key, PackedInt64Array());
    add_control<VisualScriptCollectionEdit>(parent, optional ? "VisualScriptParameterDefault" : "VisualScriptVariableDefault")->configure(
            pin, value, commit, callable_mp(this, &VisualScriptPanels::apply_collection_order).bind(key, draft_scope),
            key, optional, key_order, next_collection_row, drafts, next_ids);
}

void VisualScriptPanels::apply_collection_order(const PackedInt64Array &from, const PackedInt64Array &to,
        const StringName &key, int64_t scope) {
    if (scope != draft_scope || !details || !(selected_declaration_address() == draft_address)) {
        return;
    }
    for (const Variant &node : details->find_children("VisualScript*Default*", "", true, false)) {
        auto *collection = Object::cast_to<VisualScriptCollectionEdit>(node.get_validated_object());
        if (collection && collection->get_identity() == key) {
            const PackedInt64Array actual = collection_orders.get(key, collection->row_ids());
            if (actual == from) {
                collection_orders[key] = to;
                collection_refresh_pending = true;
            }
            return;
        }
    }
}

void VisualScriptPanels::draw_declaration(const Dictionary &record, const StringName &declaration_section) {
    if (declaration_section == StringName("enums")) {
        draw_enum(record);
        return;
    }
    const String name = record.get("name", String());
    const bool variable = is_variable_section(declaration_section);
    const bool macro = declaration_section == StringName("macros");
    const bool event = declaration_section == StringName("events");
    const String heading = variable ? (declaration_section == StringName("locals") ? "Local Variable" : "Instance Variable") :
            (event ? "Custom Event" : (macro ? "Macro" : "Function"));
    const bool instance = declaration_section == StringName("variables");
    Node *fields = details;
    if (instance) {
        fields = details_section("VisualScriptInstanceVariableDisclosure", heading,
                "Show or hide the variable's name, type, and starting value.", true);
    } else {
        details_title(details, heading, variable ? "MemberProperty" : (event ? "Signal" : "MemberMethod"));
    }
    const bool exported = instance && record.get("exported", false);
    text_field(fields, "VisualScriptDeclarationName", "Name", name,
            exported ? "Enter a unique identifier and press Enter. Existing graph references keep their identity. Renaming removes Inspector overrides saved under the old name." :
                       "Enter a unique identifier and press Enter. Existing graph references keep their identity.",
            callable_mp(this, &VisualScriptPanels::rename_declaration));
    auto *description = text_field(fields, is_graph_section(declaration_section) ? "VisualScriptFunctionDescription" : "VisualScriptDeclarationDescription",
            "Description", record.get("description", String()), "Shown when you hover this declaration or a node that uses it.",
            callable_mp(this, &VisualScriptPanels::edit_declaration_description));
    description->set_placeholder("Optional");
    if (variable) {
        const Dictionary pin = object_record(record, "pin");
        const bool array = pin_type(pin) == Variant::ARRAY;
        const bool dictionary = pin_type(pin) == Variant::DICTIONARY;
        const Dictionary base = array ? collection_side(pin, "element") : dictionary ? collection_side(pin, "key") : pin;
        type_field(fields, base, document, callable_mp(this, &VisualScriptPanels::change_variable_type), details_label_width,
                array ? "Element Type" : dictionary ? "Key Type" : "Type", "VisualScriptDeclarationType",
                array ? TypeChoices::ELEMENT : dictionary ? TypeChoices::KEY : TypeChoices::ALL);
        const String container_help = "Choose one value, an ordered array, or a dictionary of key-value pairs.";
        auto *container = ui::select(details_field(fields, "Container", container_help), {}, -1,
                callable_mp(this, &VisualScriptPanels::change_variable_container));
        container->set_name("VisualScriptVariableContainerPicker");
        compact_details_control(container->get_trigger());
        container->get_trigger()->set_name("VisualScriptVariableContainer");
        ui::tooltip(container->get_trigger(), container_help);
        std::vector<ui::PickerOption> container_choices;
        for (const auto &[name, type] : {std::pair{"Single", Variant::NIL}, {"Array", Variant::ARRAY}, {"Dictionary", Variant::DICTIONARY}}) {
            ui::PickerOption option;
            option.id = name;
            option.label = name;
            option.leading_icon = ui::graph_type_marker(container, type);
            container_choices.push_back(std::move(option));
        }
        container->set_options(std::move(container_choices));
        container->select(array ? 1 : dictionary ? 2 : 0);
        if (dictionary) {
            type_field(fields, collection_side(pin, "value"), document,
                    callable_mp(this, &VisualScriptPanels::change_variable_dictionary_value), details_label_width,
                    "Value Type", "VisualScriptDictionaryValueType", TypeChoices::ELEMENT);
        }
        if (instance && script.is_valid()) {
            const bool available = exported || script->can_export_for_owners(name, pin);
            const String help = exported ? unexport_variable_help : (available ? export_variable_help : export_variable_unavailable);
            auto *control = ui::checkbox(details_field(fields, "Export", help), "Show in Inspector",
                    exported ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED,
                    callable_mp(this, &VisualScriptPanels::change_variable_export).bind(record_id(record)), !available);
            control->set_name("VisualScriptVariableExported");
            compact_details_control(control);
            ui::tooltip(control, help);
        }
        const Variant value = record.get("value", initial_value(pin));
        const Array enum_values = enum_members(pin, document);
        const auto [axes, axis_type] = vector_default_components(pin_type(pin));
        if (!enum_values.is_empty()) {
            enum_default_field(fields, "VisualScriptVariableDefault", enum_values, value,
                    callable_mp(this, &VisualScriptPanels::commit_variable_value), false);
        } else if (array || dictionary) {
            draw_collection(fields, pin, value, callable_mp(this, &VisualScriptPanels::edit_variable_collection),
                    "variable", false, records(record, "default_key_order"));
        } else if (pin_type(pin) == Variant::BOOL) {
            auto *row = details_field(fields, "Default", "Initial value for each new variable scope.", details_label_width);
            auto *control = ui::checkbox(row, "Enabled", static_cast<bool>(value) ? ui::CheckState::CHECKED :
                    ui::CheckState::UNCHECKED, callable_mp(this, &VisualScriptPanels::change_variable_bool));
            control->set_name("VisualScriptVariableDefault");
            compact_details_control(control);
            control->set_tooltip_text("Initial value for each new variable scope.");
        } else if (pin_type(pin) == Variant::OBJECT) {
            const StringName resource_class = pin.get("class", StringName());
            const bool resource = ClassDB::is_parent_class(resource_class, "Resource");
            const String help = resource ? "Choose a saved project asset for this variable's initial value." :
                    "Assign a live object in the graph with Set Variable.";
            auto *row = details_field(fields, "Default", help, details_label_width);
            if (resource) {
                auto *field = asset_field(row, resource_class, help, value,
                        [this, pin](const Ref<Resource> &asset) {
                            const Dictionary encoded = make_asset_reference(asset);
                            if (asset.is_valid() && (encoded.is_empty() || !pin_accepts_value(pin, asset))) {
                                report("Choose a compatible saved project asset.");
                                return false;
                            }
                            return commit_variable_value(asset.is_null() ? Variant() : Variant(encoded));
                        });
                field->set_name("VisualScriptVariableDefault");
            } else {
                ui::label(row, "Null", ui::LabelStyle::MUTED);
            }
        } else if (pin_type(pin) == Variant::COLOR) {
            const String help = "Choose the initial color for each new variable scope.";
            auto *row = details_field(fields, "Default", help, details_label_width);
            auto *control = add_control<ColorPickerButton>(row, "VisualScriptVariableDefault");
            control->set_pick_color(value);
            control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            ui::tooltip(control, help);
            control->connect("popup_closed", callable_mp_static(+[](ColorPickerButton *picker, VisualScriptPanels *panels) {
                if (picker->is_inside_tree() && !picker->is_queued_for_deletion()) {
                    panels->commit_variable_value(picker->get_pick_color());
                }
            }).bind(control, this));
        } else if (pin_type(pin) == Variant::CALLABLE || pin_type(pin) == Variant::SIGNAL || pin_type(pin) == Variant::RID) {
            const String help = "Assign this runtime value from a graph pin.";
            ui::label(details_field(fields, "Default", help, details_label_width), "Set in graph", ui::LabelStyle::MUTED);
        } else if (axes > 0) {
            const String help = axis_type == Variant::INT ? "Enter a signed 32-bit number for each axis."
                    : "Enter a finite number for each axis.";
            auto *column = add_control<VBoxContainer>(details_field(fields, "Default", help, details_label_width),
                    "VisualScriptVariableDefault");
            column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            column->add_theme_constant_override("separation", ui::pixels(column, ui::spacing::xs));
            const Variant vector = value.get_type() == pin_type(pin) ? value : initial_value(pin);
            for (int32_t axis = 0; axis < axes; ++axis) {
                const String caption = String::chr(U"XYZW"[axis]);
                bool valid = false;
                bool out_of_bounds = false;
                const Variant coordinate = vector.get_indexed(axis, valid, out_of_bounds);
                if (valid && !out_of_bounds) {
                    text_field(column, StringName(String("VisualScriptVariableAxis") + caption), caption,
                            UtilityFunctions::var_to_str(coordinate), help,
                            callable_mp(this, &VisualScriptPanels::change_variable_axis).bind(axis), ui::spacing::xxl);
                }
            }
        } else if (pin_type(pin) == Variant::INT || pin_type(pin) == Variant::FLOAT) {
            const String help = pin_type(pin) == Variant::INT ?
                    "Enter a signed 64-bit integer and press Enter." : "Enter a finite decimal or scientific number and press Enter.";
            text_field(fields, "VisualScriptVariableDefault", "Default", display_value(value, pin), help,
                    callable_mp(this, &VisualScriptPanels::change_variable_value));
        } else {
            text_field(fields, "VisualScriptVariableDefault", "Default", display_value(value, pin),
                    "Enter the initial value for each new scope. Structured values use Godot literal syntax.",
                    callable_mp(this, &VisualScriptPanels::change_variable_value));
        }
        if (exported) draw_variable_inspector(record, pin);
        return;
    }
    auto *actions = action_row(details);
    if (is_graph_section(declaration_section)) {
        details_action(actions, "Open Graph", "Open this declaration's own graph.",
                callable_mp(this, &VisualScriptPanels::open_selected_declaration), false);
    }
    details_action(actions, event && graph_id == 0 ? "Add Event Entry" : (event ? "Add Emit Event" : "Add Call"),
            "Place the corresponding node on the active graph.",
            callable_mp(this, &VisualScriptPanels::insert_selected_declaration).bind(false));
    if (event && graph_id == 0) {
        details_action(actions, "Add Emit Event", "Place a node that emits the custom event.",
                callable_mp(this, &VisualScriptPanels::insert_selected_declaration).bind(true));
    }
    if (is_graph_section(declaration_section)) {
        details_rule(details);
        auto *settings = details_section("VisualScriptSettingsDisclosure", "Settings", "Show or hide the graph's picker settings.");
        text_field(settings, "VisualScriptFunctionCategory", "Category", record.get("category", String()),
                "Sets the group where this operation appears in the node picker.",
                callable_mp(this, &VisualScriptPanels::edit_function_setting).bind(StringName("category")));
        text_field(settings, "VisualScriptFunctionKeywords", "Keywords", record.get("keywords", String()),
                "Add words to help find this operation in the node picker.",
                callable_mp(this, &VisualScriptPanels::edit_function_setting).bind(StringName("keywords")));
        text_field(settings, "VisualScriptFunctionCompactTitle", "Card Title", record.get("compact_title", String()),
                "Optional short name on the call card. The declared name is used to call the function.",
                callable_mp(this, &VisualScriptPanels::edit_function_setting).bind(StringName("compact_title")));
        const String behavior_help = "Pure graphs run when an output is read. Graphs with execution wires run when triggered.";
        auto *behavior = ui::label(details_field(settings, "Execution", behavior_help, details_label_width),
                record.get("pure", false) ? "Pure" : "With execution wires", ui::LabelStyle::MUTED);
        behavior->set_tooltip_text(behavior_help);
    }
    draw_signature(record, "inputs", macro);
    if (!event) {
        draw_signature(record, "outputs", macro);
    }
}

void VisualScriptPanels::draw_graph_node(const Dictionary &node) {
    const String kind = node.get("kind", String());
    if (kind == "custom_script") {
        draw_custom_script(node);
        return;
    }
    if (kind == "switch_enum") {
        details_title(details, "Switch on Enum");
        auto *row = details_field(details, "Enum", "Godot enum used by the Selection pin.", details_label_width);
        const String enum_name = node.get("enum_type", String());
        auto *label = ui::label(row, enum_name, ui::LabelStyle::BODY);
        label->set_tooltip_text(enum_name);
        label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        auto *help = ui::label(details, "Each enum value has its own output. Default handles other values.",
                ui::LabelStyle::MUTED);
        help->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        return;
    }
    details_title(details, kind.capitalize());
    if (kind == "switch") {
        const Dictionary pin = object_record(node, "pin");
        PackedStringArray cases;
        for (const Variant &entry : records(node, "cases")) {
            cases.append(display_value(entry, pin));
        }
        text_field(details, "VisualScriptSwitchCases", "Cases", String(", ").join(cases),
                "Comma-separated cases of the switch's input type. Reordering cases also changes their wire destinations.",
                callable_mp(this, &VisualScriptPanels::change_switch_cases));
    } else if (kind == "native_signal" || kind == "scene_node" || kind == "self") {
        const String source = node.get("source", NodePath());
        field_label(details, "Source", "Path from the node running this graph.");
        auto *label = ui::label(details, source.is_empty() ? String(".") : source, ui::LabelStyle::BODY);
        label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
        if (kind != "native_signal") {
            draw_scene_reference_signals(node);
        }
    } else if (kind == "call_function" || kind == "call_macro" || kind == "custom_event" || kind == "emit_event") {
        const StringName key = kind == "call_function" ? StringName("functions") :
                (kind == "call_macro" ? StringName("macros") : StringName("events"));
        const int64_t id = node.get("definition", 0);
        const Dictionary definition = find_graph_record(records(document, key), id);
        ui::label(details, definition.get("name", String()), ui::LabelStyle::MUTED);
        if (is_graph_section(key)) {
            ui::button(details, "Open Graph", "Open the referenced graph.",
                    callable_mp(this, &VisualScriptPanels::open_referenced_declaration).bind(id));
        }
    }
}

void VisualScriptPanels::draw_custom_script(const Dictionary &node) {
    auto *heading = details_title(details, "Custom Script");
    auto *edit = details_action(heading, "Edit Code", "Edit the GDScript that runs when this node is reached.", {}, false);
    edit->set_h_size_flags(Control::SIZE_SHRINK_END);
    edit->set_name("VisualScriptEditCustomCode");
    edit->connect("pressed", callable_mp(this, &VisualScriptPanels::edit_selected_custom_code).bind(edit));
    const Variant title = node.get("title", Variant());
    auto *title_field = text_field(details, "VisualScriptCustomScriptTitle", "Name",
            title.get_type() == Variant::STRING ? String(title).strip_edges() : String(),
            "Set the graph card title. Press Enter to apply. Clear it to use Custom Script.",
            callable_mp(this, &VisualScriptPanels::rename_custom_script).bind(graph_node_id, graph_id), pin_label_width);
    title_field->set_placeholder("Custom Script");

    for (const StringName &direction : {StringName("inputs"), StringName("outputs")}) {
        const bool input = direction == StringName("inputs");
        details_rule(details);
        HBoxContainer *actions = nullptr;
        auto *group = details_section(input ? "VisualScriptCustomInputsDisclosure" : "VisualScriptCustomOutputsDisclosure",
                input ? "Inputs" : "Outputs", input ? "Show or hide the script's input pins." : "Show or hide the script's output pins.",
                false, &actions);
        group->set_name(input ? "VisualScriptCustomInputs" : "VisualScriptCustomOutputs");
        details_add_button(actions, input ? "VisualScriptAddCustomInput" : "VisualScriptAddCustomOutput",
                input ? "Add input" : "Add output",
                input ? "Add a typed input." : "Add a typed output.",
                callable_mp(this, &VisualScriptPanels::add_custom_pin).bind(direction));
        for (const Variant &entry : records(node, direction)) {
            if (entry.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary pin = entry;
            const StringName id = pin.get("id", StringName());
            const bool source_owned = pin.get("source_owned", false);
            const String source_help = input ? "Edit Code to change this input's declaration or unwired value." :
                    "Edit Code to change this output's declaration.";
            auto *fields = pin_card(group);
            const String name_help = source_owned ? source_help :
                    String("GDScript identifier. Press Enter to rename it. Existing wires keep their pin ID.");
            auto *row = details_field(fields, "Name", name_help, pin_label_width);
            auto *name = ui::text_input(row, pin.get("label", String()),
                    name_help, source_owned ? ui::FieldState::READ_ONLY : ui::FieldState::EDITABLE);
            name->set_name(String("VisualScriptCustomPinName_") + String(id));
            name->set_accessibility_name(input ? "Input name" : "Output name");
            name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            compact_details_text(name);
            if (source_owned) {
                auto *type_row = details_field(fields, "Type", source_help, pin_label_width);
                auto *type = ui::label(type_row, type_name(pin), ui::LabelStyle::BODY);
                type->set_h_size_flags(Control::SIZE_EXPAND_FILL);
                type->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
                type->set_tooltip_text(type->get_text() + "\n" + source_help);
                type->set_mouse_filter(Control::MOUSE_FILTER_PASS);
                auto *origin = ui::label(type_row, "In code", ui::LabelStyle::MUTED);
                origin->set_name(String("VisualScriptSourcePin_") + String(id));
                origin->set_tooltip_text(source_help);
                origin->set_mouse_filter(Control::MOUSE_FILTER_PASS);
                continue;
            }
            name->connect("text_submitted", callable_mp(this, &VisualScriptPanels::rename_custom_pin).bind(direction, id));
            auto *remove = ui::icon_button(row, "remove", "Remove this pin after disconnecting its wires.",
                    callable_mp(this, &VisualScriptPanels::remove_custom_pin).bind(direction, id), ui::ButtonStyle::QUIET);
            compact_details_icon(remove);
            remove->set_name(String("VisualScriptRemoveCustomPin_") + String(id));
            remove->set_accessibility_name(input ? "Remove input" : "Remove output");
            type_field(fields, pin, document, callable_mp(this, &VisualScriptPanels::change_custom_pin_type).bind(direction, id), pin_label_width);
        }
    }
}

void VisualScriptPanels::rename_custom_script(const String &title, int64_t node_id, int64_t body_id) {
    if (graph_node_id != node_id || graph_id != body_id || selected_custom_script().is_empty()) {
        return;
    }
    const String name = title.strip_edges();
    replace_node_field("Rename Custom Script", "title", name.is_empty() ? Variant() : Variant(name));
}

void VisualScriptPanels::edit_selected_custom_code(Control *trigger) {
    if (on_edit_code && !selected_custom_script().is_empty()) {
        on_edit_code(graph_node_id, trigger);
    }
}

Dictionary VisualScriptPanels::selected_custom_script() const {
    const Dictionary node = find_graph_record(records(graph_body(document, graph_id), "nodes"), graph_node_id);
    return StringName(node.get("kind", StringName())) == StringName("custom_script") ? node : Dictionary();
}

void VisualScriptPanels::add_custom_pin(const StringName &direction) {
    const Dictionary node = selected_custom_script();
    if (node.is_empty()) {
        return;
    }
    std::set<String> occupied_labels;
    for (const StringName &key : {StringName("inputs"), StringName("outputs")}) {
        for (const Dictionary &pin : records(node, key)) {
            occupied_labels.insert(pin.get("label", String()));
        }
    }
    const String prefix = direction == StringName("inputs") ? "input_" : "output_";
    int64_t suffix = records(node, direction).size() + 1;
    String name;
    do {
        name = prefix + String::num_int64(suffix++);
    } while (!signature_pin(node, direction, StringName(name)).is_empty() || occupied_labels.count(name));
    edit_custom_pin(direction, name, typed_pin(StringName(name), name, Variant::FLOAT));
}

void VisualScriptPanels::rename_custom_pin(const String &label, const StringName &direction, const StringName &id) {
    const Dictionary node = selected_custom_script();
    Dictionary pin = signature_pin(node, direction, id).duplicate();
    if (pin.is_empty()) {
        return;
    }
    const String name = label.strip_edges();
    if (!custom_script_identifier_available(name)) {
        report("Use an available GDScript identifier, such as health or max_speed.");
        return;
    }
    for (const StringName &key : {StringName("inputs"), StringName("outputs")}) {
        for (const Dictionary &other : records(node, key)) {
            const bool same_pin = key == direction && StringName(other.get("id", StringName())) == id;
            if (!same_pin && String(other.get("label", String())) == name) {
                report("Use a different name. All Custom Script inputs and outputs share one scope.");
                return;
            }
        }
    }
    pin["label"] = name;
    edit_custom_pin(direction, id, pin);
}

void VisualScriptPanels::change_custom_pin_type(const Dictionary &choice, const StringName &direction, const StringName &id) {
    const Dictionary previous = signature_pin(selected_custom_script(), direction, id);
    if (previous.is_empty() || (pin_type(choice) == Variant::OBJECT && !ClassDB::class_exists(choice.get("class", StringName())))) {
        return;
    }
    const Dictionary replacement = change_pin_type(previous, choice);
    if (!same_authored_type(previous, replacement)) {
        edit_custom_pin(direction, id, replacement);
    }
}

void VisualScriptPanels::remove_custom_pin(const StringName &direction, const StringName &id) {
    edit_custom_pin(direction, id, Dictionary());
}

void VisualScriptPanels::edit_custom_pin(const StringName &direction, const StringName &id, const Dictionary &replacement) {
    const Dictionary node = selected_custom_script();
    if (node.is_empty() || !on_commit || script.is_null()) {
        return;
    }
    Array next_pins;
    const Dictionary previous = signature_pin(node, direction, id);
    if (previous.get("source_owned", false)) {
        report("Edit Code to change this pin.");
        return;
    }
    for (const Dictionary &pin : records(node, direction)) {
        if (StringName(pin.get("id", StringName())) != id) {
            next_pins.append(pin);
        } else if (!replacement.is_empty()) {
            next_pins.append(replacement);
        }
    }
    if (previous.is_empty() && !replacement.is_empty()) {
        next_pins.append(replacement);
    }
    if (next_pins == records(node, direction)) {
        report(String());
        return;
    }
    Dictionary next_node = node.duplicate();
    next_node[direction] = next_pins;
    if (direction == StringName("inputs") &&
            (replacement.is_empty() || (!previous.is_empty() && !same_authored_type(previous, replacement)))) {
        Dictionary values = object_record(node, "values").duplicate();
        if (replacement.is_empty() || (values.has(id) && !pin_accepts_value(replacement, values[id]))) {
            values.erase(id);
            if (values.is_empty()) {
                next_node.erase("values");
            } else {
                next_node["values"] = values;
            }
        }
    }
    Dictionary body = graph_body(document, graph_id).duplicate();
    Array nodes = records(body, "nodes").duplicate();
    for (int64_t index = 0; index < nodes.size(); ++index) {
        if (record_id(nodes[index]) == graph_node_id) {
            nodes[index] = next_node;
            body["nodes"] = nodes;
            const Dictionary next_document = replace_graph_body(document, graph_id, body);
            if (visual_script_edit_breaks_wires(document, next_document, script->get_graph_base())) {
                report("Disconnect the affected wires before changing this pin.");
                return;
            }
            on_commit("Edit Custom Script Pin", next_document);
            return;
        }
    }
}

void VisualScriptPanels::draw_scene_reference_signals(const Dictionary &node) {
    if (graph_id != 0) {
        return;
    }
    const StringName type = node.get("class", script->get_graph_base());
    const NodePath source = node.get("source", NodePath("."));
    std::map<String, Dictionary> signals;
    for (const Array &available : {find_native_signals(type), find_script_signals(node.get("script_path", String()))}) {
        for (Dictionary signal : available) {
            signal.erase("wired_target");
            signal.erase("target_class");
            signal["source"] = source;
            signals.emplace(String(signal["signal"]), signal);
        }
    }

    details_rule(details);
    auto *rows = details_section("VisualScriptSignalsDisclosure", "Signals", "Show or hide the selected node's signals.");
    rows->set_name("VisualScriptSignalRows");
    rows->add_theme_constant_override("separation", ui::pixels(rows, 2));
    if (signals.empty()) {
        ui::label(rows, "No signals.", ui::LabelStyle::MUTED);
        return;
    }
    for (const auto &[name, record] : signals) {
        const String title = "On " + name.capitalize();
        const String script_name = record.get("script_name", String());
        const String help = title + String("\n\n") + (script_name.is_empty() ?
                visual_script_signal_help(type, StringName(name)) : script_name + String(".") + name);
        auto *row = add_control<HBoxContainer>(rows);
        row->add_theme_constant_override("separation", ui::pixels(row, 6));
        auto *label = ui::label(row, title);
        label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
        label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
        ui::tooltip(label, help);
        auto *add = ui::icon_button(row, "add", help,
                callable_mp(this, &VisualScriptPanels::insert_native_signal).bind(record), ui::ButtonStyle::QUIET);
        add->set_name(String("AddSignal_") + name);
        add->set_accessibility_name("Add " + title);
    }
}

void VisualScriptPanels::select_declaration() {
    if (refreshing || !declarations->get_selected()) {
        return;
    }
    const Variant selection = declarations->get_selected()->get_metadata(0);
    if (selection.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary address = selection;
    section = address.get("section", StringName());
    declaration_id = record_id(address);
    graph_node_id = 0;
    rebuild_details();
}

void VisualScriptPanels::activate_declaration() {
    select_declaration();
    if (is_graph_section(section)) {
        open_selected_declaration();
    } else {
        insert_selected_declaration(false);
    }
}

void VisualScriptPanels::declaration_input(const Ref<InputEvent> &event) {
    const Ref<InputEventMouseButton> mouse = event;
    if (mouse.is_valid() && mouse->is_pressed() && mouse->get_button_index() == MOUSE_BUTTON_RIGHT) {
        if (open_declaration_menu(mouse->get_position())) {
            declarations->accept_event();
        }
        return;
    }
    const Ref<InputEventKey> key = event;
    if (key.is_null() || !key->is_pressed()) {
        return;
    }
    const bool modified = key->is_alt_pressed() || key->is_shift_pressed();
    const bool command_pressed = key->is_command_or_control_pressed();
    DeclarationCommand command;
    switch (key->get_keycode()) {
        case KEY_X: if (!command_pressed) return; command = DeclarationCommand::CUT; break;
        case KEY_C: if (!command_pressed) return; command = DeclarationCommand::COPY; break;
        case KEY_D: if (!command_pressed) return; command = DeclarationCommand::DUPLICATE; break;
        case KEY_V: if (!command_pressed && !key->is_alt_pressed()) return; command = DeclarationCommand::PASTE; break;
        case KEY_DELETE: if (command_pressed) return; command = DeclarationCommand::DELETE_DECLARATION; break;
        case KEY_F2: if (command_pressed) return; command = DeclarationCommand::RENAME; break;
        default: return;
    }
    // A declaration with no applicable edit must not send its shortcut to Godot's Scene dock.
    declarations->accept_event();
    if (!key->is_echo() && !modified) run_declaration_command(command);
}

bool VisualScriptPanels::open_declaration_menu(const Vector2 &point) {
    TreeItem *item = declarations->get_item_at_position(point);
    if (!item) {
        return false;
    }
    const Variant address = item->get_metadata(0);
    const bool row = address.get_type() == Variant::DICTIONARY;
    const StringName group = row ? StringName(Dictionary(address).get("section", StringName())) :
            (address.get_type() == Variant::STRING_NAME ? StringName(address) : StringName());
    if (!is_editable_declaration_section(group) || (!row && !can_paste_declaration(group))) {
        return false;
    }
    declarations->grab_focus();
    declaration_menu->clear();
    const auto add_item = [&](const char *label, DeclarationCommand command, Key shortcut, const StringName &glyph) {
        declaration_menu->add_item(label, static_cast<int32_t>(command), shortcut);
        set_graph_menu_icon(declaration_menu, declaration_menu->get_item_count() - 1, glyph);
    };
    if (row) {
        item->select(0);
        select_declaration();
        add_item("Rename", DeclarationCommand::RENAME, KEY_F2, "Edit");
        add_item("Cut", DeclarationCommand::CUT, static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_X), "ActionCut");
        add_item("Copy", DeclarationCommand::COPY, static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_C), "ActionCopy");
        add_item("Duplicate", DeclarationCommand::DUPLICATE, static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_D), "Duplicate");
        add_item("Delete", DeclarationCommand::DELETE_DECLARATION, KEY_DELETE, "Remove");
    }
    if (can_paste_declaration(group)) {
        if (row) {
            declaration_menu->add_separator();
        }
        add_item("Paste", DeclarationCommand::PASTE, static_cast<Key>(KEY_MASK_CMD_OR_CTRL | KEY_V), "ActionPaste");
    }
    const Vector2i position = declarations->get_global_transform_with_canvas().xform(point);
    const Vector2i room = get_window()->get_size();
    declaration_menu->set_max_size(room);
    const Vector2i extent = Vector2i(declaration_menu->get_contents_minimum_size()).min(room);
    declaration_menu->popup_on_parent(Rect2i(position.clamp(Vector2i(), (room - extent).max(Vector2i())), extent));
    return true;
}

void VisualScriptPanels::declaration_menu_command(int64_t command) {
    run_declaration_command(static_cast<DeclarationCommand>(command));
}

bool VisualScriptPanels::run_declaration_command(DeclarationCommand command) {
    if (script.is_null() || !on_commit) {
        return false;
    }
    if (command == DeclarationCommand::PASTE) {
        if (!can_paste_declaration(clipboard_address.section)) {
            if (clipboard_stale && clipboard_script == script->get_instance_id()) {
                report("A declaration used by the clipboard was deleted. Copy the updated declaration to paste it.");
                return true;
            }
            return false;
        }
        duplicate_declaration(clipboard_address.section, declaration_clipboard, "Paste Declaration");
        return true;
    }
    const DeclarationAddress address{section, declaration_id};
    if (graph_node_id > 0 || address.id <= 0 || !is_editable_declaration_section(address.section)) {
        return false;
    }
    const Dictionary record = selected_declaration();
    if (record.is_empty()) {
        return false;
    }
    switch (command) {
        case DeclarationCommand::RENAME:
            callable_mp(this, &VisualScriptPanels::focus_declaration_name).call_deferred();
            return true;
        case DeclarationCommand::CUT:
            delete_declaration(address, record, true);
            return true;
        case DeclarationCommand::COPY:
            copy_declaration(address, record);
            return true;
        case DeclarationCommand::DUPLICATE:
            duplicate_declaration(address.section, record, "Duplicate Declaration");
            return true;
        case DeclarationCommand::DELETE_DECLARATION:
            delete_declaration(address, record, false);
            return true;
        default:
            return false;
    }
}

void VisualScriptPanels::focus_declaration_name() {
    if (!details) {
        return;
    }
    if (auto *field = Object::cast_to<LineEdit>(details->find_child("VisualScriptDeclarationName", true, false))) {
        if (!field->is_visible_in_tree()) {
            if (auto *disclosure = Object::cast_to<Button>(details->find_child("VisualScriptInstanceVariableDisclosure", true, false))) {
                disclosure->set_pressed(true);
            }
        }
        field->grab_focus();
        field->select_all();
    }
}

bool VisualScriptPanels::can_paste_declaration(const StringName &group) const {
    return script.is_valid() && !clipboard_stale && !declaration_clipboard.is_empty() && clipboard_script == script->get_instance_id() &&
            clipboard_address.section == group && (clipboard_owner == 0 || clipboard_owner == graph_id);
}

void VisualScriptPanels::copy_declaration(const DeclarationAddress &address, const Dictionary &record) {
    declaration_clipboard = record.duplicate(true);
    clipboard_address = address;
    clipboard_owner = address.section == StringName("locals") ? graph_id : 0;
    clipboard_script = script->get_instance_id();
    clipboard_dependencies.clear();
    clipboard_stale = false;
    if (clipboard_owner > 0) {
        clipboard_dependencies.push_back({StringName("functions"), clipboard_owner});
    }
    for (const Variant &entry : records(object_record(record, "graph"), "nodes")) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary node = entry;
        const StringName kind = node.get("kind", StringName());
        if (kind == StringName("get_variable") || kind == StringName("set_variable")) {
            if (StringName(node.get("scope", StringName())) != StringName("local")) {
                clipboard_dependencies.push_back({StringName("variables"), node.get("variable", 0)});
            }
        } else if (kind == StringName("call_function") || kind == StringName("call_macro") || kind == StringName("emit_event")) {
            const StringName group(kind == StringName("call_function") ? "functions" : kind == StringName("call_macro") ? "macros" : "events");
            const int64_t id = node.get("definition", 0);
            if (group != address.section || id != address.id) {
                clipboard_dependencies.push_back({group, id});
            }
        }
    }
    for (const Variant &entry : records(document, "enums")) {
        if (entry.get_type() == Variant::DICTIONARY && uses_authored_enum(record, authored_enum_identity(record_id(entry)))) {
            clipboard_dependencies.push_back({StringName("enums"), record_id(entry)});
        }
    }
    check_clipboard_dependencies(document);
}

void VisualScriptPanels::check_clipboard_dependencies(const Dictionary &updated) {
    if (script.is_null() || clipboard_stale || clipboard_script != script->get_instance_id()) {
        return;
    }
    clipboard_stale = std::any_of(clipboard_dependencies.begin(), clipboard_dependencies.end(), [&](const DeclarationAddress &dependency) {
        return find_graph_record(records(updated, dependency.section), dependency.id).is_empty();
    });
}

void VisualScriptPanels::duplicate_declaration(const StringName &group, const Dictionary &record, const String &action) {
    Array entries = declaration_records(document, graph_id, group).duplicate();
    const int64_t id = is_variable_section(group) ? next_id(entries) : next_definition_id(document);
    if (id == 0) {
        report("This script has no available declaration IDs.");
        return;
    }
    Dictionary cloned = record.duplicate(true);
    const String original_name = record.get("name", String());
    const StringName base_class = script->get_graph_base();
    if (declaration_name_used(document, graph_id, group, 0, original_name, base_class)) {
        cloned["name"] = make_name(document, graph_id, group, original_name + String("_copy"), base_class);
    }
    if (is_graph_section(group)) {
        rebind_declaration_body(cloned, group, id);
    }
    cloned["id"] = id;
    entries.append(cloned);
    show_declaration(group, id);
    on_commit(action, with_declarations(document, graph_id, group, entries));
}

void VisualScriptPanels::delete_declaration(const DeclarationAddress &address, const Dictionary &record, bool cut) {
    Array retained;
    for (const Variant &entry : declaration_records(document, graph_id, address.section)) {
        if (entry.get_type() != Variant::DICTIONARY || record_id(entry) != address.id) {
            retained.append(entry);
        }
    }
    Dictionary next = with_declarations(document, graph_id, address.section, retained);
    const PackedInt64Array bodies = address.section == StringName("locals") ? PackedInt64Array(Array::make(graph_id)) :
            document_bodies(next);
    for (const int64_t body_id : bodies) {
        const Dictionary body = graph_body(next, body_id);
        const Dictionary pruned = without_declaration_references(body, address.section, address.id);
        if (pruned != body) {
            next = replace_graph_body(next, body_id, pruned);
        }
    }
    if (address.section == StringName("variables") && record.get("exported", false)) {
        if (confirm_variable_edit) {
            const String explanation = "Removing this exported variable removes overrides saved on scene instances. "
                                       "Graph Undo restores the declaration and its references. Scene overrides are lost.";
            const auto finish = callable_mp(this, &VisualScriptPanels::finish_declaration_removal).bind(cut, record, address.section, address.id);
            confirm_variable_edit(cut ? VariableEdit::CUT_EXPORTED : VariableEdit::DELETE_EXPORTED, next, explanation, finish);
        }
        return;
    }
    finish_declaration_removal(cut, record, address.section, address.id);
    on_commit(cut ? "Cut Declaration" : "Delete Declaration", next);
}

void VisualScriptPanels::finish_declaration_removal(bool cut, const Dictionary &record, const StringName &group, int64_t id) {
    if (cut) {
        copy_declaration({group, id}, record);
    }
    show_declaration(StringName(), 0);
}

Dictionary VisualScriptPanels::scene_dock_reference(const Dictionary &drag) const {
    auto *root = EditorInterface::get_singleton()->get_edited_scene_root();
    if (!root || drag.get("scene_root", Variant()).get_validated_object() != root ||
            drag.get("nodes", Variant()).get_type() != Variant::ARRAY) {
        return {};
    }
    const Array paths = drag["nodes"];
    if (paths.size() != 1 || paths[0].get_type() != Variant::NODE_PATH) {
        return {};
    }
    const NodePath path = paths[0];
    if (!path.is_absolute()) {
        return {};
    }
    Node *target = root->get_node_or_null(path);
    const bool in_scene = target && (target == root || root->is_ancestor_of(target));
    return in_scene && visible_scene_node(target, root) ? reference_node(script_owner(root), target) : Dictionary();
}

Variant VisualScriptPanels::drag_declaration(const Vector2 &point) {
    if (declarations->get_button_id_at_position(point) >= 0) return {};
    auto *item = declarations->get_item_at_position(point);
    if (!item || item->get_metadata(0).get_type() != Variant::DICTIONARY) {
        return {};
    }
    const Dictionary address = item->get_metadata(0);
    const StringName key = address.get("section", StringName());
    const Dictionary record = find_graph_record(declaration_records(document, graph_id, key), record_id(address));
    const Array options = declaration_actions(key, record, graph_id);
    if (is_variable_section(key) && script.is_valid()) {
        for (int64_t index = 0; index < options.size(); ++index) {
            Dictionary option = options[index];
            option["source_script"] = script->get_instance_id();
            option["source_graph"] = graph_id;
        }
    }
    if (record.is_empty() || script.is_null()) return {};
    show_symbol_drag_preview(declarations, item, record.get("name", String()));
    Dictionary payload = options.is_empty() ? Dictionary() : Dictionary(drag_options(options));
    payload["declaration"] = address;
    payload["source_script"] = script->get_instance_id();
    payload["source_graph"] = graph_id;
    return payload;
}

bool VisualScriptPanels::can_reorder_declaration(const Vector2 &point, const Variant &payload) {
    const Dictionary drag = payload.get_type() == Variant::DICTIONARY ? Dictionary(payload) : Dictionary();
    const Dictionary source = drag.get("declaration", Dictionary());
    if (!point.is_finite() || source.is_empty() || script.is_null() ||
            uint64_t(drag.get("source_script", 0)) != script->get_instance_id()) {
        return false;
    }
    const StringName group = source.get("section", StringName());
    if (group == StringName("locals") && int64_t(drag.get("source_graph", -1)) != graph_id) return false;
    const auto *row = declarations->get_item_at_position(point);
    const Dictionary target = row && row->get_metadata(0).get_type() == Variant::DICTIONARY ?
            Dictionary(row->get_metadata(0)) : Dictionary();
    return StringName(target.get("section", StringName())) == group &&
            record_id(source) > 0 && record_id(target) > 0 && record_id(source) != record_id(target);
}

void VisualScriptPanels::reorder_declaration(const Vector2 &point, const Variant &payload) {
    if (!on_commit || !can_reorder_declaration(point, payload)) return;
    const Dictionary drag = payload;
    const Dictionary source = drag["declaration"];
    const Dictionary target = declarations->get_item_at_position(point)->get_metadata(0);
    const StringName group = source["section"];
    const Array ordered = declaration_records(document, graph_id, group);
    const int64_t source_id = record_id(source);
    const int64_t target_id = record_id(target);
    const Dictionary moved = find_graph_record(ordered, source_id);
    if (moved.is_empty() || find_graph_record(ordered, target_id).is_empty()) return;
    const bool after = point.y >= declarations->get_item_area_rect(declarations->get_item_at_position(point)).get_center().y;
    Array next;
    for (const Variant &record : ordered) {
        const int64_t id = record.get_type() == Variant::DICTIONARY ? record_id(Dictionary(record)) : 0;
        if (id == source_id) continue;
        if (id == target_id && !after) next.append(moved);
        next.append(record);
        if (id == target_id && after) next.append(moved);
    }
    if (next == ordered) return;
    on_commit("Reorder Declaration", with_declarations(document, graph_id, group, next));
}

void VisualScriptPanels::press_declaration_button(TreeItem *item, int64_t, int64_t id, int64_t mouse_button) {
    if (!item) {
        return;
    }
    const Variant metadata = item->get_metadata(0);
    if (id == declaration_add_button && metadata.get_type() == Variant::STRING_NAME) {
        create_declaration(metadata);
    } else if (id == variable_export_button && mouse_button == MOUSE_BUTTON_LEFT && metadata.get_type() == Variant::DICTIONARY) {
        const Dictionary row = metadata;
        if (StringName(row.get("section", StringName())) == StringName("variables")) {
            const int64_t variable_id = record_id(row);
            const Dictionary variable = find_graph_record(records(document, "variables"), variable_id);
            if (!variable.is_empty()) {
                change_variable_export(!static_cast<bool>(variable.get("exported", false)), variable_id);
            }
        }
    }
}

void VisualScriptPanels::create_declaration(const StringName &key, const StringName &enum_type) {
    if (script.is_null() || !on_commit) {
        return;
    }
    if (key == StringName("events")) {
        int64_t event_id = 0;
        const Dictionary next = new_custom_event(document, event_id);
        if (event_id == 0) {
            report("This script has no available declaration IDs.");
            return;
        }
        on_commit("Create Event", next);
        show_declaration(key, event_id);
        return;
    }
    const bool variable = is_variable_section(key);
    const bool enumeration = key == StringName("enums");
    Array entries = declaration_records(document, graph_id, key).duplicate();
    const int64_t id = variable || enumeration ? next_id(entries) : next_definition_id(document);
    if (id == 0) {
        report("This script has no available declaration IDs.");
        return;
    }
    const char *prefix = variable ? (key == StringName("locals") ? "local" : "variable") :
            (enumeration ? "NewEnum" : (key == StringName("functions") ? "function" : "macro"));
    Dictionary record;
    record["id"] = id;
    record["name"] = make_name(document, graph_id, key, prefix, script->get_graph_base());
    if (variable) {
        const Array entries_for_type = authored_enum_entries(document, enum_type);
        const bool has_enum = !entries_for_type.is_empty();
        Dictionary pin = typed_pin("value", "Value", has_enum ? Variant::INT : Variant::BOOL);
        if (has_enum) {
            pin["enum_type"] = enum_type;
        }
        record["pin"] = pin;
        record["value"] = has_enum ? Dictionary(entries_for_type[0]).get("value", int64_t(0)) : Variant(false);
    } else if (enumeration) {
        Dictionary first;
        first["id"] = int64_t(1);
        first["name"] = "Value 1";
        first["value"] = int64_t(0);
        record["entries"] = Array::make(first);
        record["next_entry_id"] = int64_t(2);
    } else {
        record["inputs"] = Array();
        record["outputs"] = Array();
        record["graph"] = default_body(id, key == StringName("macros"));
    }
    entries.append(record);
    section = key;
    declaration_id = id;
    graph_node_id = 0;
    on_commit(enumeration ? String("Create Enum") : "Create " + String(prefix).capitalize(),
            with_declarations(document, graph_id, key, entries));
    if (is_graph_section(key) && on_open_graph) {
        on_open_graph(id);
    }
}

void VisualScriptPanels::rename_declaration(const String &value) {
    const String name = value.strip_edges();
    Dictionary record = selected_declaration().duplicate();
    if (record.is_empty() || !name.is_valid_identifier()) {
        report("Use a name containing letters, digits, and underscores. Begin with a letter or underscore.");
        return;
    }
    const String previous_name = record.get("name", String());
    if (previous_name == name) {
        report(String());
        return;
    }
    const auto address = selected_declaration_address();
    if (declaration_name_used(document, graph_id, address.section, address.id, name, script->get_graph_base())) {
        report(is_callable_section(address.section) ? "Use a public name that is free on this node and in Functions and Custom Events." :
                "This name is already used in this section.");
        return;
    }
    const bool exported = address.section == StringName("variables") && record.get("exported", false);
    if (exported) {
        if (!script->can_export_for_owners(name, object_record(record, "pin"))) {
            report("Choose a name that is free on the attached node or turn off Export first.");
            return;
        }
        if (auto *field = Object::cast_to<LineEdit>(details->find_child("VisualScriptDeclarationName", true, false))) {
            field->set_text(previous_name);
        }
        record["name"] = name;
        confirm_export_edit(address, VariableEdit::RENAME_EXPORTED, record);
        return;
    }
    record["name"] = name;
    replace_declaration("Rename Declaration", record);
}

void VisualScriptPanels::edit_declaration_description(const String &value) {
    Dictionary record = selected_declaration().duplicate();
    if (record.is_empty()) {
        return;
    }
    const String description = value.strip_edges();
    if (description.is_empty()) {
        record.erase("description");
    } else {
        record["description"] = description;
    }
    replace_declaration("Edit Declaration Description", record);
}

void VisualScriptPanels::draw_variable_inspector(const Dictionary &record, const Dictionary &pin) {
    const Dictionary inspector = object_record(record, "inspector");
    details_rule(details);
    auto *fields = details_section("VisualScriptInspectorDisclosure", "Inspector", "Show or hide scene Inspector options for this export.");
    text_field(fields, "VisualScriptInspectorGroup", "Group", inspector.get("group", String()),
            "Group scene properties with the same group name. Leave blank to show the variable without a group.",
            callable_mp(this, &VisualScriptPanels::edit_variable_inspector_text).bind(StringName("group")))->set_placeholder("Optional");
    if (!has_numeric_inspector_options(pin)) return;

    text_field(fields, "VisualScriptInspectorUnits", "Units", inspector.get("suffix", String()),
            "Show units beside the number in the Inspector, for example m/s. Units do not change the stored value.",
            callable_mp(this, &VisualScriptPanels::edit_variable_inspector_text).bind(StringName("suffix")))->set_placeholder("Optional");
    const Dictionary range = object_record(inspector, "range");
    const bool use_range = is_valid_inspector_range(pin, range);
    const String range_help = "Set the interval and step of the Inspector control. Values outside the interval can still be typed.";
    auto *range_toggle = ui::checkbox(details_field(fields, "Range", range_help, details_label_width), "Use Range",
            use_range ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED,
            callable_mp(this, &VisualScriptPanels::toggle_variable_inspector_range));
    range_toggle->set_name("VisualScriptInspectorUseRange");
    compact_details_control(range_toggle);
    ui::tooltip(range_toggle, range_help);
    if (!use_range) return;

    const auto submit = callable_mp(this, &VisualScriptPanels::edit_variable_inspector_range);
    for (const auto &[field, caption, help] : {
                 std::tuple{"min", "Minimum", "Set the low end of the editing interval. Press Enter to apply all three values."},
                 std::tuple{"max", "Maximum", "Set the high end of the editing interval. Press Enter to apply all three values."},
                 std::tuple{"step", "Step", "Set the positive adjustment increment. Press Enter to apply all three values."}}) {
        text_field(fields, String("VisualScriptInspectorRange_") + field, caption, display_value(range[field], pin), help, submit);
    }
    const String slider_help = "Show a slider in the Inspector. Turn it off to use a number field without the slider or arrow controls.";
    auto *slider = ui::checkbox(details_field(fields, "Control", slider_help, details_label_width), "Show Slider",
            bool(range.get("slider", true)) ? ui::CheckState::CHECKED : ui::CheckState::UNCHECKED,
            callable_mp(this, &VisualScriptPanels::toggle_variable_inspector_slider));
    slider->set_name("VisualScriptInspectorSlider");
    compact_details_control(slider);
    ui::tooltip(slider, slider_help);
}

void VisualScriptPanels::edit_variable_inspector_text(const String &text, const StringName &field) {
    const Dictionary record = selected_declaration();
    Dictionary inspector = object_record(record, "inspector").duplicate();
    const bool group = field == StringName("group");
    if ((!group && !has_numeric_inspector_options(object_record(record, "pin"))) ||
            !(group ? is_valid_inspector_group(text) : is_valid_inspector_suffix(text))) {
        report(group ? "Enter a group name without slashes or line breaks." : "Enter units without commas or line breaks.");
        return;
    }
    const String value = text.strip_edges();
    if (value.is_empty()) inspector.erase(field);
    else inspector[field] = value;
    commit_variable_inspector(inspector, value == text);
}

void VisualScriptPanels::toggle_variable_inspector_range(bool enabled) {
    const Dictionary record = selected_declaration();
    const Dictionary pin = object_record(record, "pin");
    if (!has_numeric_inspector_options(pin)) return;
    Dictionary inspector = object_record(record, "inspector").duplicate();
    if (enabled) {
        Dictionary range;
        const bool integer = pin_type(pin) == Variant::INT;
        range["min"] = 0;
        range["max"] = integer ? 100 : 1;
        range["step"] = integer ? Variant(1) : Variant(0.01);
        range["slider"] = true;
        inspector["range"] = range;
    } else {
        inspector.erase("range");
    }
    commit_variable_inspector(inspector);
}

void VisualScriptPanels::edit_variable_inspector_range(const String &) {
    const Dictionary record = selected_declaration();
    const Dictionary pin = object_record(record, "pin");
    Dictionary inspector = object_record(record, "inspector").duplicate();
    Dictionary range = object_record(inspector, "range").duplicate();
    if (range.is_empty()) return;
    const String error = "Enter a valid minimum and maximum and a positive step. For an integer variable use whole numbers.";
    for (const char *key : {"min", "max", "step"}) {
        const auto *field = Object::cast_to<LineEdit>(details->find_child(String("VisualScriptInspectorRange_") + key, true, false));
        Variant value;
        if (!field || !read_value(field->get_text(), pin, value)) {
            report(error);
            return;
        }
        range[key] = value;
    }
    if (!is_valid_inspector_range(pin, range)) {
        report(error);
        return;
    }
    inspector["range"] = range;
    commit_variable_inspector(inspector, true);
}

void VisualScriptPanels::toggle_variable_inspector_slider(bool enabled) {
    Dictionary inspector = object_record(selected_declaration(), "inspector").duplicate();
    Dictionary range = object_record(inspector, "range").duplicate();
    if (range.is_empty()) return;
    range["slider"] = enabled;
    inspector["range"] = range;
    commit_variable_inspector(inspector, true);
}

void VisualScriptPanels::commit_variable_inspector(const Dictionary &inspector, bool keep_details) {
    Dictionary record = selected_declaration().duplicate();
    if (selected_declaration_address().section != StringName("variables") || !bool(record.get("exported", false))) return;
    if (inspector.is_empty()) record.erase("inspector");
    else record["inspector"] = inspector;
    replace_declaration("Edit Variable Inspector Settings", record, keep_details);
}

void VisualScriptPanels::edit_function_setting(const String &value, const StringName &field) {
    if (!is_graph_section(selected_declaration_address().section)) {
        return;
    }
    Dictionary record = selected_declaration().duplicate();
    const String text = value.strip_edges();
    if (text.is_empty()) {
        record.erase(field);
    } else {
        record[field] = text;
    }
    replace_declaration("Edit Function Settings", record);
}

void VisualScriptPanels::change_variable_type(const Dictionary &choice) {
    const auto type = pin_type(choice);
    if (type == Variant::OBJECT && !ClassDB::class_exists(choice.get("class", StringName()))) {
        return;
    }
    const Dictionary record = selected_declaration();
    const Dictionary previous = object_record(record, "pin");
    if (type == Variant::ARRAY || type == Variant::DICTIONARY) {
        change_variable_container(type == Variant::ARRAY ? 1 : 2);
    } else if (pin_type(previous) == Variant::ARRAY || pin_type(previous) == Variant::DICTIONARY) {
        Dictionary next = previous.duplicate();
        const char *slot = pin_type(previous) == Variant::ARRAY ? "element" : "key";
        if (type == Variant::NIL) {
            next.erase(slot);
        } else {
            if (pin_type(previous) == Variant::DICTIONARY && !collection_key_type_allowed(choice)) {
                report("Choose a dictionary key type that can be saved as a literal.");
                return;
            }
            next[slot] = change_pin_type({}, choice);
        }
        prepare_variable_type(next);
    } else {
        prepare_variable_type(change_pin_type(previous, choice));
    }
}

void VisualScriptPanels::set_variable_edit_confirmation(ConfirmVariableEdit confirm) {
    confirm_variable_edit = std::move(confirm);
}

void VisualScriptPanels::change_variable_container(int64_t container) {
    const Dictionary previous = object_record(selected_declaration(), "pin");
    const auto old_type = pin_type(previous);
    const auto new_type = container == 1 ? Variant::ARRAY : container == 2 ? Variant::DICTIONARY : Variant::NIL;
    const bool old_container = old_type == Variant::ARRAY || old_type == Variant::DICTIONARY;
    if (previous.is_empty() || (container == 0 && !old_container) || (container != 0 && old_type == new_type)) {
        return;
    }
    const Dictionary primary = old_container ? object_record(previous, old_type == Variant::ARRAY ? "element" : "key") : previous;
    if (container == 0) {
        prepare_variable_type(change_pin_type(previous, primary));
        return;
    }
    Dictionary next = typed_pin(previous.get("id", StringName()), previous.get("label", String()), new_type);
    const auto primary_type = pin_type(primary);
    if (!primary.is_empty() && primary_type != Variant::NIL && (container == 1 || collection_key_type_allowed(primary))) {
        next[container == 1 ? "element" : "key"] = change_pin_type({}, primary);
    }
    prepare_variable_type(next);
}

void VisualScriptPanels::change_variable_dictionary_value(const Dictionary &choice) {
    Dictionary next = object_record(selected_declaration(), "pin").duplicate();
    const auto type = pin_type(choice);
    if (pin_type(next) != Variant::DICTIONARY || type == Variant::ARRAY || type == Variant::DICTIONARY) {
        return;
    }
    if (type == Variant::NIL) {
        next.erase("value");
    } else {
        next["value"] = change_pin_type({}, choice);
    }
    prepare_variable_type(next);
}

void VisualScriptPanels::prepare_variable_type(const Dictionary &pin) {
    const auto address = selected_declaration_address();
    Dictionary record = selected_declaration().duplicate();
    if (!confirm_variable_edit || script.is_null() || !is_variable_section(address.section) || record.is_empty() ||
            object_record(record, "pin") == pin) {
        return;
    }
    if (address.section == StringName("variables") && record.get("exported", false) &&
            !script->can_export_for_owners(record.get("name", StringName()), pin)) {
        report("Turn off Export before choosing a type that the Inspector cannot save.");
        return;
    }
    record["pin"] = pin;
    const StringName enum_id = pin.get("enum_type", StringName());
    Array values = authored_enum_entries(document, enum_id);
    if (!enum_id.is_empty() && values.is_empty()) {
        values = make_enum_switch(enum_id).get("cases", Array());
    }
    record["value"] = values.is_empty() ? (pin_has_collection_schema(pin) && !resource_collection_pin(pin) ?
            default_collection_value(pin) : initial_value(pin)) :
            Dictionary(values[0]).get("value", Variant());
    record.erase("default_key_order");
    Array entries = declaration_records(document, graph_id, address.section).duplicate();
    for (int64_t index = 0; index < entries.size(); ++index) {
        if (record_id(entries[index]) == address.id) {
            entries[index] = record;
            break;
        }
    }
    Dictionary updated = with_declarations(document, graph_id, address.section, entries);
    const StringName base = script->get_graph_base();
    int64_t disconnected = 0;
    int64_t cleared_values = 0;
    for (const int64_t id : document_bodies(document)) {
        if (address.section == StringName("locals") && id != graph_id) continue;
        const Dictionary before = graph_body(document, id);
        Dictionary after = graph_body(updated, id).duplicate();
        const Array old_nodes = records(before, "nodes");
        Array nodes = records(after, "nodes").duplicate();
        bool altered = false;
        for (int64_t index = 0; index < nodes.size(); ++index) {
            Dictionary node = nodes[index];
            if (String(node.get("kind", String())) != "set_variable" || !refers_to_declaration(node, address.section, address.id)) continue;
            Dictionary literals = object_record(node, "values").duplicate();
            if (literals.has("value") && !pin_accepts_value(pin, literals["value"])) {
                literals.erase("value");
                node = node.duplicate();
                node["values"] = literals;
                nodes[index] = node;
                ++cleared_values;
                altered = true;
            }
        }
        Array wires;
        for (const Variant &wire : records(before, "connections")) {
            if (wire.get_type() == Variant::DICTIONARY && accepts_wire(document, before, old_nodes, wire, base) &&
                    !accepts_wire(updated, after, nodes, wire, base)) {
                ++disconnected;
                altered = true;
            } else {
                wires.append(wire);
            }
        }
        if (altered) {
            after["connections"] = wires;
            after["nodes"] = nodes;
        }
        const Dictionary normalized = prune_incompatible_loop_wires(updated, after, base);
        const int64_t invalidated_loop_wires = records(after, "connections").size() - records(normalized, "connections").size();
        if (altered || invalidated_loop_wires > 0) {
            disconnected += invalidated_loop_wires;
            updated = replace_graph_body(updated, id, normalized);
        }
    }
    String explanation = String("Changing the type of ") + String(record.get("name", String("this variable"))) + " resets its default.";
    if (address.section == StringName("variables") && record.get("exported", false)) {
        explanation += " Incompatible values set in the Inspector will be cleared.";
    }
    if (disconnected == 1) {
        explanation += " One incompatible wire will be disconnected.";
    } else if (disconnected > 1) {
        explanation += " " + String::num_int64(disconnected) + " incompatible wires will be disconnected.";
    }
    if (cleared_values == 1) {
        explanation += " One incompatible setter value will be cleared.";
    } else if (cleared_values > 1) {
        explanation += " " + String::num_int64(cleared_values) + " incompatible setter values will be cleared.";
    }
    confirm_variable_edit(VariableEdit::TYPE, updated, explanation, {});
}

void VisualScriptPanels::change_variable_value(const String &value) {
    const Dictionary pin = object_record(selected_declaration(), "pin");
    Variant parsed;
    if (read_value(value, pin, parsed)) {
        commit_variable_value(parsed);
    } else if (pin_type(pin) == Variant::INT || pin_type(pin) == Variant::FLOAT) {
        report(pin_type(pin) == Variant::INT ? "Enter an integer within the signed 64-bit range." : "Enter a finite floating-point number.");
    } else {
        report("Enter a valid literal of the declared type.");
    }
}

void VisualScriptPanels::change_variable_axis(const String &value, int32_t axis) {
    const Dictionary record = selected_declaration();
    const Dictionary pin = object_record(record, "pin");
    const auto [axes, axis_type] = vector_default_components(pin_type(pin));
    if (axis < 0 || axis >= axes) {
        return;
    }
    Variant coordinate;
    const bool valid_number = read_value(value, typed_pin("value", "Axis", axis_type), coordinate);
    if (axis_type == Variant::INT) {
        const int64_t integer = coordinate;
        if (!valid_number || integer < std::numeric_limits<int32_t>::min() || integer > std::numeric_limits<int32_t>::max()) {
            report("Enter an axis value within the signed 32-bit range.");
            return;
        }
    } else if (!valid_number || std::abs(static_cast<double>(coordinate)) > std::numeric_limits<real_t>::max()) {
        report("Enter a finite axis value within the vector's range.");
        return;
    }
    Variant vector = record.get("value", Variant());
    if (vector.get_type() != pin_type(pin)) {
        vector = initial_value(pin);
    }
    bool valid = false;
    bool out_of_bounds = false;
    vector.set_indexed(axis, coordinate, valid, out_of_bounds);
    if (valid && !out_of_bounds) {
        commit_variable_value(vector);
    }
}

void VisualScriptPanels::change_variable_bool(bool value) {
    commit_variable_value(value);
}

void VisualScriptPanels::change_variable_export(bool enabled, int64_t variable_id) {
    Dictionary variable = find_graph_record(records(document, "variables"), variable_id).duplicate();
    if (script.is_null() || variable.is_empty() || static_cast<bool>(variable.get("exported", false)) == enabled) {
        return;
    }
    if (enabled && !script->can_export_for_owners(variable.get("name", StringName()), object_record(variable, "pin"))) {
        report(export_variable_unavailable);
        return;
    }
    const DeclarationAddress address{StringName("variables"), variable_id};
    if (!enabled) {
        if (selected_declaration_address() == address) {
            if (auto *checkbox = Object::cast_to<CheckBox>(details->find_child("VisualScriptVariableExported", true, false))) {
                checkbox->set_pressed_no_signal(true);
            }
        }
        variable.erase("exported");
        confirm_export_edit(address, VariableEdit::HIDE_EXPORTED, variable);
        return;
    }
    variable["exported"] = true;
    replace_declaration(address, "Show Variable in Inspector", variable);
}

void VisualScriptPanels::confirm_export_edit(const DeclarationAddress &address, VariableEdit edit, const Dictionary &record) {
    if (!confirm_variable_edit) {
        return;
    }
    Array entries = declaration_records(document, graph_id, address.section).duplicate();
    for (int64_t index = 0; index < entries.size(); ++index) {
        if (entries[index].get_type() == Variant::DICTIONARY && record_id(entries[index]) == address.id) {
            entries[index] = record;
            const String explanation = edit == VariableEdit::RENAME_EXPORTED ?
                    "Renaming this variable removes Inspector overrides saved under its old name. Graph Undo restores the name. Scene overrides are lost." :
                    "Hiding this variable removes overrides saved on scene instances. Graph Undo shows the variable again. Scene overrides are lost.";
            confirm_variable_edit(edit, with_declarations(document, graph_id, address.section, entries), explanation, {});
            return;
        }
    }
}

bool VisualScriptPanels::commit_variable_value(const Variant &value) {
    Dictionary record = selected_declaration().duplicate();
    if (record.is_empty()) return false;
    if (record.get("value", Variant()) != value) {
        record["value"] = value;
        return replace_declaration("Change Variable Default", record);
    }
    return true;
}

bool VisualScriptPanels::edit_variable_collection(const Variant &value, const Array &key_order,
        const Callable &redo, const Callable &undo) {
    if (collection_refresh_pending) {
        return false;
    }
    Dictionary record = selected_declaration().duplicate();
    const Dictionary pin = object_record(record, "pin");
    const auto type = pin_type(pin);
    if ((type != Variant::ARRAY && type != Variant::DICTIONARY) || value.get_type() != type ||
            !persistent_value(value) || (resource_collection_pin(pin) && !valid_resource_collection_storage(pin, value))) {
        return false;
    }
    record["value"] = value;
    if (key_order.is_empty()) {
        record.erase("default_key_order");
    } else {
        record["default_key_order"] = key_order;
    }
    return replace_declaration("Change Variable Default", record, false, redo, undo);
}

bool VisualScriptPanels::edit_signature_collection(const Variant &value, const Array &key_order,
        const Callable &redo, const Callable &undo,
        const StringName &direction, const StringName &id) {
    if (collection_refresh_pending) {
        return false;
    }
    Dictionary pin = signature_pin(selected_declaration(), direction, id).duplicate();
    if (pin.is_empty()) {
        return false;
    }
    if (value.get_type() == Variant::NIL) {
        pin.erase("default");
    } else if (persistent_value(value) && (resource_collection_pin(pin) ?
            valid_resource_collection_storage(pin, value) : pin_accepts_value(pin, value))) {
        pin["default"] = value;
    } else {
        report("Choose a persistent default of the declared type.");
        return false;
    }
    if (key_order.is_empty()) {
        pin.erase("default_key_order");
    } else {
        pin["default_key_order"] = key_order;
    }
    return edit_signature_pin(direction, id, pin, false, redo, undo);
}

void VisualScriptPanels::draw_enum(const Dictionary &record) {
    details_title(details, "Enum", "Enum");
    text_field(details, "VisualScriptDeclarationName", "Name", record.get("name", String()),
            "Enter a unique enum name. Existing variable and switch references keep their identity.",
            callable_mp(this, &VisualScriptPanels::rename_declaration));
    auto *create = details_action(details, "Create Variable", "Create an instance variable that stores this enum's values.",
            callable_mp(this, &VisualScriptPanels::create_declaration).bind(StringName("variables"), authored_enum_identity(record_id(record))));
    create->set_name("VisualScriptCreateEnumVariable");
    details_rule(details);
    HBoxContainer *actions = nullptr;
    auto *section = details_section("VisualScriptMembersDisclosure", "Members", "Show or hide this enum's named values.", false, &actions);
    details_add_button(actions, "VisualScriptAddEnumMember", "Add Member", "Add a named integer value.",
            callable_mp(this, &VisualScriptPanels::add_enum_entry));
    const Array entries = records(record, "entries");
    auto *members = add_control<VBoxContainer>(section, "VisualScriptEnumMembers");
    const int64_t list_id = members->get_instance_id();
    const auto move = callable_mp(this, &VisualScriptPanels::move_enum_entry).bind(record_id(record), list_id);
    for (int64_t index = 0; index < entries.size(); ++index) {
        const Dictionary entry = entries[index];
        const int64_t id = record_id(entry);
        auto *fields = pin_card(members, "VisualScriptEnumMember");
        auto *row = action_row(fields);
        const String name = entry.get("name", String());
        if (entries.size() > 1) {
            authored_row_grip(row, list_id, id, index > 0 ? record_id(entries[index - 1]) : -1,
                    index + 1 < entries.size() ? record_id(entries[index + 1]) : -1, name, "VisualScriptReorderEnumMember", move);
        }
        auto *label_field = ui::text_input(row, name, "Name shown on switches and value pickers. Renaming keeps existing branch wires.");
        compact_details_text(label_field);
        label_field->set_name("VisualScriptEnumMemberName");
        label_field->set_accessibility_name("Member name");
        label_field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        label_field->connect("text_submitted", callable_mp(this, &VisualScriptPanels::rename_enum_entry).bind(id));
        auto *value_field = ui::text_input(row, String::num_int64(entry.get("value", 0)),
                "Distinct integer stored by this member. Existing stored numbers keep their value.");
        compact_details_text(value_field);
        value_field->set_custom_minimum_size(Vector2(ui::pixels(value_field, 48), ui::pixels(value_field, 24)));
        value_field->set_name("VisualScriptEnumMemberValue");
        value_field->set_accessibility_name("Member value");
        value_field->connect("text_submitted", callable_mp(this, &VisualScriptPanels::change_enum_entry_value).bind(id));
        auto *remove = ui::icon_button(row, "remove", "Remove this member after disconnecting its case wires.",
                callable_mp(this, &VisualScriptPanels::remove_enum_entry).bind(id), ui::ButtonStyle::QUIET);
        compact_details_icon(remove);
        remove->set_name("VisualScriptRemoveEnumMember");
        remove->set_accessibility_name("Remove Member");
        remove->set_disabled(entries.size() <= 1);
        const Dictionary pending = enum_drafts.get(id, Dictionary());
        for (LineEdit *field : {label_field, value_field}) {
            field->set_meta("saved_text", field->get_text());
            const Dictionary state = pending.get(String(field->get_name()), Dictionary());
            restore_row_text(state, field, state.get("dirty", false));
        }
        register_authored_row(Object::cast_to<Control>(fields->get_parent()), list_id, id, move);
        for (auto *control : { static_cast<Control *>(label_field), static_cast<Control *>(value_field), static_cast<Control *>(remove) }) {
            authored_row_drop_target(control, list_id, id, move);
        }
    }
    details_action(action_row(details), "Delete Enum", "Delete this declaration after removing references to it.",
            callable_mp(this, &VisualScriptPanels::remove_enum), false);
}

void VisualScriptPanels::add_enum_entry() {
    Dictionary enumeration = selected_declaration().duplicate();
    Array entries = records(enumeration, "entries").duplicate();
    const int64_t next = next_id(entries);
    const int64_t id = std::max(next, static_cast<int64_t>(enumeration.get("next_entry_id", int64_t(1))));
    if (id <= 0 || id == std::numeric_limits<int64_t>::max() || entries.size() >= 1024) {
        report("This enum has no available member slots.");
        return;
    }
    std::set<int64_t> values;
    std::set<String> names;
    for (const Dictionary &entry : entries) {
        values.insert(entry.get("value", int64_t(0)));
        names.insert(entry.get("name", String()));
    }
    int64_t value = 0;
    while (values.count(value) != 0) {
        ++value;
    }
    int64_t suffix = 1;
    String name;
    do {
        name = "Value " + String::num_int64(suffix++);
    } while (names.count(name) != 0);
    Dictionary entry;
    entry["id"] = id;
    entry["name"] = name;
    entry["value"] = value;
    entries.append(entry);
    enumeration["entries"] = entries;
    enumeration["next_entry_id"] = id + 1;
    replace_declaration("Add Enum Member", enumeration);
}

void VisualScriptPanels::rename_enum_entry(const String &value, int64_t entry_id) {
    const String name = value.strip_edges();
    if (name.is_empty()) {
        report("Enter a name for this member.");
        return;
    }
    Dictionary entry = find_graph_record(records(selected_declaration(), "entries"), entry_id).duplicate();
    if (!entry.is_empty()) {
        entry["name"] = name;
        edit_enum_entry(entry_id, entry, "VisualScriptEnumMemberName");
    }
}

void VisualScriptPanels::change_enum_entry_value(const String &value, int64_t entry_id) {
    int64_t parsed = 0;
    if (!read_number(value.strip_edges(), parsed)) {
        report("Enter a whole number within the signed 64-bit range.");
        return;
    }
    Dictionary entry = find_graph_record(records(selected_declaration(), "entries"), entry_id).duplicate();
    if (!entry.is_empty()) {
        entry["value"] = parsed;
        edit_enum_entry(entry_id, entry, "VisualScriptEnumMemberValue");
    }
}

void VisualScriptPanels::remove_enum_entry(int64_t entry_id) {
    edit_enum_entry(entry_id, Dictionary());
}

void VisualScriptPanels::move_enum_entry(int64_t from, int64_t to, int64_t enum_id, int64_t list_id) {
    const auto address = selected_declaration_address();
    auto *list = Object::cast_to<Control>(ObjectDB::get_instance(list_id));
    if (address.section != StringName("enums") || address.id != enum_id || !list || !list->is_inside_tree() ||
            list->is_queued_for_deletion()) {
        return;
    }
    Dictionary enumeration = selected_declaration().duplicate();
    Array entries = records(enumeration, "entries").duplicate();
    int64_t source = -1;
    int64_t target = -1;
    for (int64_t index = 0; index < entries.size(); ++index) {
        const int64_t id = record_id(entries[index]);
        if (id == from) {
            source = index;
        }
        if (id == to) {
            target = index;
        }
    }
    if (source < 0 || target < 0 || source == target || list->get_child_count() != entries.size()) {
        return;
    }
    auto *moved_row = Object::cast_to<Control>(list->get_child(source));
    if (!moved_row || int64_t(moved_row->get_meta("authored_row_id", -1)) != from) {
        return;
    }
    const Variant entry = entries.pop_at(source);
    entries.insert(target, entry);
    enumeration["entries"] = entries;
    if (!replace_declaration("Reorder Enum Member", enumeration, true)) {
        return;
    }
    list->move_child(moved_row, target);
    for (int64_t index = 0; index < entries.size(); ++index) {
        auto *row = list->get_child(index);
        auto *grip = Object::cast_to<Control>(row->find_child("VisualScriptReorderEnumMember*", true, false));
        if (grip) {
            const Dictionary member = entries[index];
            const String name = member.get("name", String());
            grip->set_accessibility_name(String("Reorder ") + name);
            grip->set_meta("drag_caption", name);
            grip->set_meta("previous_authored_row", index > 0 ? record_id(entries[index - 1]) : -1);
            grip->set_meta("next_authored_row", index + 1 < entries.size() ? record_id(entries[index + 1]) : -1);
        }
    }
}

void VisualScriptPanels::remove_enum() {
    const auto address = selected_declaration_address();
    if (!on_commit || address.section != StringName("enums") || address.id <= 0) {
        return;
    }
    Dictionary references = document.duplicate();
    references.erase("enums");
    if (uses_authored_enum(references, authored_enum_identity(address.id))) {
        report("Remove this enum's switches, variables, and signature references before deleting it.");
        return;
    }
    Array retained;
    for (const Dictionary &record : records(document, "enums")) {
        if (record_id(record) != address.id) {
            retained.append(record);
        }
    }
    on_commit("Delete Enum", with_declarations(document, graph_id, "enums", retained));
}

void VisualScriptPanels::edit_enum_entry(int64_t entry_id, const Dictionary &replacement, const char *submitted_field) {
    Dictionary enumeration = selected_declaration().duplicate();
    Array entries;
    std::set<String> names;
    std::set<int64_t> values;
    for (const Dictionary &original : records(enumeration, "entries")) {
        const Dictionary entry = record_id(original) == entry_id ? replacement : original;
        if (entry.is_empty()) {
            continue;
        }
        if (!names.insert(entry.get("name", String())).second || !values.insert(entry.get("value", 0)).second) {
            report("Each enum member needs a distinct label and integer value.");
            return;
        }
        entries.append(entry);
    }
    if (entries.is_empty()) {
        report("An enum needs at least one member.");
        return;
    }
    enumeration["entries"] = entries;
    if (replace_declaration("Edit Enum Member", enumeration, !replacement.is_empty()) && !replacement.is_empty()) {
        auto *list = details->find_child("VisualScriptEnumMembers", true, false);
        if (!list) {
            return;
        }
        for (int index = 0; index < list->get_child_count(); ++index) {
            auto *row = list->get_child(index);
            if (int64_t(row->get_meta("authored_row_id", -1)) != entry_id) {
                continue;
            }
            if (auto *grip = Object::cast_to<Control>(row->find_child("VisualScriptReorderEnumMember*", true, false))) {
                const String name = replacement.get("name", String());
                grip->set_accessibility_name(String("Reorder ") + name);
                grip->set_meta("drag_caption", name);
            }
            if (submitted_field) {
                if (auto *field = Object::cast_to<LineEdit>(row->find_child(submitted_field, true, false))) {
                    field->set_meta("saved_text", field->get_text());
                }
            }
            break;
        }
    }
}

void VisualScriptPanels::open_selected_declaration() {
    open_referenced_declaration(selected_declaration_address().id);
}

void VisualScriptPanels::open_referenced_declaration(int64_t id) {
    if (on_open_graph) {
        on_open_graph(id);
    }
}

void VisualScriptPanels::insert_selected_declaration(bool alternative) {
    const auto address = selected_declaration_address();
    const Array options = declaration_actions(address.section, selected_declaration(), graph_id);
    const int64_t index = alternative ? 1 : 0;
    if (on_insert_node && index < options.size()) {
        const Dictionary option = options[index];
        on_insert_node(option["node"]);
    }
}

void VisualScriptPanels::add_signature_pin(const StringName &direction, bool execution) {
    Dictionary record = selected_declaration().duplicate();
    Array pins = records(record, direction).duplicate();
    const bool input = direction == StringName("inputs");
    const char *prefix = input ? "input_" : "output_";
    int64_t suffix = pins.size() + 1;
    StringName id;
    do {
        id = String(prefix) + String::num_int64(suffix++);
    } while (!signature_pin(record, direction, id).is_empty());
    Dictionary pin = typed_pin(id, String(id).capitalize(), execution ? Variant::NIL : Variant::BOOL);
    if (execution) {
        pin["exec"] = true;
    }
    if (execution && selected_declaration_address().section == StringName("macros") && !has_execution_pin(pins)) {
        const StringName default_id(input ? "input_default" : "output_default");
        if (!signature_pin(record, direction, default_id).is_empty()) {
            report("Rename the existing signature pin before adding an execution route.");
            return;
        }
        Dictionary first = typed_pin(default_id, input ? "In" : "Then", Variant::NIL);
        first["exec"] = true;
        pins.push_front(first);
        pins.append(pin);
        record[direction] = pins;
        Array definitions = records(document, "macros").duplicate();
        for (int64_t index = 0; index < definitions.size(); ++index) {
            if (definitions[index].get_type() == Variant::DICTIONARY && record_id(definitions[index]) == declaration_id) {
                definitions[index] = record;
                Dictionary updated = with_declarations(document, graph_id, section, definitions);
                updated = migrate_macro_execution(updated, declaration_id, input, default_id);
                on_commit("Add Macro Execution Pin", updated);
                return;
            }
        }
        return;
    }
    edit_signature_pin(direction, id, pin);
}

void VisualScriptPanels::rename_signature_pin(const String &value, const StringName &direction, const StringName &id) {
    const String name = value.strip_edges();
    Dictionary pin = signature_pin(selected_declaration(), direction, id).duplicate();
    if (name.is_empty() || pin.is_empty()) {
        report("Give the pin a name.");
        return;
    }
    pin["label"] = name;
    edit_signature_pin(direction, id, pin);
}

void VisualScriptPanels::change_signature_type(const Dictionary &choice,
        const StringName &direction, const StringName &id) {
    const Dictionary previous = signature_pin(selected_declaration(), direction, id);
    if (previous.is_empty() || (pin_type(choice) == Variant::OBJECT && !ClassDB::class_exists(choice.get("class", StringName())))) {
        return;
    }
    const Dictionary pin = change_pin_type(previous, choice);
    if (!same_authored_type(previous, pin)) {
        edit_signature_pin(direction, id, pin);
    }
}

void VisualScriptPanels::change_signature_default(const String &value, const StringName &direction, const StringName &id) {
    Dictionary pin = signature_pin(selected_declaration(), direction, id).duplicate();
    if (pin.is_empty()) {
        return;
    }
    if (value.is_empty() && pin_type(pin) != Variant::STRING && pin_type(pin) != Variant::STRING_NAME) {
        pin.erase("default");
    } else {
        Variant parsed;
        if (!read_value(value, pin, parsed)) {
            report("Enter a valid default of the declared type.");
            return;
        }
        pin["default"] = parsed;
    }
    edit_signature_pin(direction, id, pin);
}

void VisualScriptPanels::remove_signature_pin(const StringName &direction, const StringName &id) {
    edit_signature_pin(direction, id, Dictionary());
}

bool VisualScriptPanels::edit_signature_pin(const StringName &direction, const StringName &id, const Dictionary &replacement,
        bool keep_details, const Callable &redo, const Callable &undo) {
    Dictionary record = selected_declaration().duplicate();
    if (record.is_empty()) {
        return false;
    }
    Array updated;
    bool found = false;
    for (const Variant &entry : records(record, direction)) {
        if (entry.get_type() != Variant::DICTIONARY || StringName(Dictionary(entry).get("id", StringName())) != id) {
            updated.append(entry);
            continue;
        }
        found = true;
        if (!replacement.is_empty()) {
            updated.append(replacement);
        }
    }
    if (!found && !replacement.is_empty()) {
        updated.append(replacement);
    }
    record[direction] = updated;
    return replace_declaration("Edit Signature", record, keep_details, redo, undo);
}

void VisualScriptPanels::draw_signature(const Dictionary &record, const StringName &direction, bool macro) {
    const bool input = direction == StringName("inputs");
    details_rule(details);
    HBoxContainer *actions = nullptr;
    auto *section = details_section(input ? "VisualScriptInputsDisclosure" : "VisualScriptOutputsDisclosure",
            input ? "Inputs" : "Outputs", "Show or hide the graph signature's pin settings.", false, &actions);
    details_add_button(actions, input ? "VisualScriptAddInput" : "VisualScriptAddOutput", input ? "Add input" : "Add output",
            "Add a typed data pin to this signature.", callable_mp(this, &VisualScriptPanels::add_signature_pin).bind(direction, false));
    for (const Variant &entry : records(record, direction)) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary pin = entry;
        const StringName id = pin.get("id", StringName());
        const String key = String::num_int64(record_id(record)) + "/" + String(direction) + "/" + String(id);
        const bool folded = folded_signature_pins.count(key) != 0;
        auto *card = pin_card(section, "VisualScriptSignatureFrame");
        auto *heading = action_row(card);
        const String title = pin.get("label", String());
        auto *toggle = ui::button(heading, title, "Show or hide this pin's settings.", Callable(), ui::ButtonStyle::QUIET);
        compact_details_control(toggle);
        toggle->set_name("VisualScriptTogglePin");
        toggle->set_h_size_flags(Control::SIZE_EXPAND_FILL);
        toggle->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        toggle->set_clip_text(true);
        toggle->set_toggle_mode(true);
        toggle->set_pressed_no_signal(folded);
        toggle->set_button_icon(ui::icon(toggle, folded ? "chevron_right" : "chevron_down"));
        const double scale = toggle->get_theme_default_base_scale();
        toggle->add_theme_stylebox_override("pressed", ui::rounded_box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 6, 2, scale));
        toggle->add_theme_stylebox_override("hover_pressed", ui::rounded_box(graph_color(GraphTone::HOVER), Color(0, 0, 0, 0), 6, 2, scale));
        auto *remove = ui::icon_button(heading, "remove", "Remove this pin after disconnecting any wires that use it.",
                callable_mp(this, &VisualScriptPanels::remove_signature_pin).bind(direction, id), ui::ButtonStyle::QUIET);
        compact_details_icon(remove);
        remove->set_name("VisualScriptRemovePin");
        remove->set_accessibility_name("Remove Pin");
        auto *pin_fields = add_control<VBoxContainer>(card);
        pin_fields->add_theme_constant_override("separation", ui::pixels(pin_fields, ui::spacing::xs));
        pin_fields->set_visible(!folded);
        toggle->connect("toggled", callable_mp(this, &VisualScriptPanels::toggle_signature_pin).bind(key, pin_fields, toggle));
        text_field(pin_fields, StringName(String("VisualScriptSignature_") + String(id)), "Name", title,
                "Enter a display name and press Enter. Existing wires keep their pin identity.",
                callable_mp(this, &VisualScriptPanels::rename_signature_pin).bind(direction, id), signature_label_width);
        if (pin.get("exec", false)) {
            auto *row = details_field(pin_fields, "Type", "Execution pin. Carries the order of operations.", signature_label_width);
            ui::label(row, "Execution", ui::LabelStyle::MUTED);
        } else {
            type_field(pin_fields, pin, document, callable_mp(this, &VisualScriptPanels::change_signature_type).bind(direction, id), signature_label_width);
            if (input && selected_declaration_address().section != StringName("events") && pin_type(pin) != Variant::OBJECT) {
                const Array enum_values = enum_members(pin, document);
                if (!enum_values.is_empty()) {
                    enum_default_field(pin_fields, "VisualScriptParameterDefault", enum_values, pin.get("default", Variant()),
                            callable_mp(this, &VisualScriptPanels::edit_signature_collection).bind(
                                    Array(), Callable(), Callable(), direction, id), true);
                } else if (pin_type(pin) == Variant::ARRAY || pin_type(pin) == Variant::DICTIONARY) {
                    draw_collection(pin_fields, pin, pin.get("default", Variant()),
                            callable_mp(this, &VisualScriptPanels::edit_signature_collection).bind(direction, id),
                            String(direction) + "/" + String(id), true, records(pin, "default_key_order"));
                } else {
                    text_field(pin_fields, "VisualScriptParameterDefault", "Default",
                            pin.has("default") ? display_value(pin["default"], pin) : String(),
                            "Enter a default used when the caller leaves this input unwired.",
                            callable_mp(this, &VisualScriptPanels::change_signature_default).bind(direction, id), signature_label_width);
                }
            }
        }
    }
    if (macro && !record.get("pure", false)) {
        ui::button(action_row(section), input ? "+ Execution Input" : "+ Execution Output", "Add a named execution route.",
                callable_mp(this, &VisualScriptPanels::add_signature_pin).bind(direction, true), ui::ButtonStyle::QUIET);
    }
}

void VisualScriptPanels::toggle_signature_pin(bool folded, const String &key, Control *fields, Button *trigger) {
    if (folded) {
        folded_signature_pins.insert(key);
    } else {
        folded_signature_pins.erase(key);
    }
    fields->set_visible(!folded);
    trigger->set_button_icon(ui::icon(trigger, folded ? "chevron_right" : "chevron_down"));
}

bool VisualScriptPanels::replace_declaration(const String &title, const Dictionary &record, bool keep_details,
        const Callable &redo, const Callable &undo) {
    return replace_declaration(selected_declaration_address(), title, record, keep_details, redo, undo);
}

bool VisualScriptPanels::replace_declaration(const DeclarationAddress &address, const String &title, const Dictionary &record,
        bool keep_details, const Callable &redo, const Callable &undo) {
    if (record.is_empty() || !on_commit) {
        return false;
    }
    Array entries = declaration_records(document, graph_id, address.section).duplicate();
    for (int64_t index = 0; index < entries.size(); ++index) {
        if (entries[index].get_type() != Variant::DICTIONARY || record_id(entries[index]) != address.id) {
            continue;
        }
        const bool with_row_history = on_collection_commit && redo.is_valid() && undo.is_valid();
        if (entries[index] == record && !with_row_history) {
            report(String());
            return true;
        }
        entries[index] = record;
        Dictionary updated = with_declarations(document, graph_id, address.section, entries);
        if (address.section == StringName("enums")) {
            updated = refresh_authored_enum_switches(updated, authored_enum_identity(address.id));
            if (updated.is_empty()) {
                report("The enum needs distinct names and integer values, and at least one entry.");
                return false;
            }
        }
        if (script.is_valid() && visual_script_edit_breaks_wires(document, updated, script->get_graph_base())) {
            report("This edit would break a wire. Disconnect the affected pin before changing it.");
            return false;
        }
        if (keep_details) {
            details_applied_document = updated;
            details_applied_address = address;
        }
        report(String());
        if (with_row_history) {
            collection_refresh_pending = true;
            on_collection_commit(title, updated, redo, undo);
        } else {
            on_commit(title, updated);
        }
        return true;
    }
    return false;
}

void VisualScriptPanels::change_switch_cases(const String &value) {
    const Dictionary body = graph_body(document, graph_id);
    const Dictionary node = find_graph_record(records(body, "nodes"), graph_node_id);
    const Dictionary pin = object_record(node, "pin");
    Array cases;
    for (const String &entry : value.split(",", false)) {
        Variant parsed;
        if (!read_value(entry.strip_edges(), pin, parsed) || cases.has(parsed) || cases.size() >= 64) {
            report("Enter up to 64 distinct cases of the switch's input type.");
            return;
        }
        cases.append(parsed);
    }
    const Array old_cases = records(node, "cases");
    for (const Variant &entry : records(body, "connections")) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary wire = entry;
        if (static_cast<int64_t>(wire.get("from", 0)) != graph_node_id) {
            continue;
        }
        const String output = wire.get("from_pin", String());
        const String suffix = output.trim_prefix("case_");
        if (suffix == output || !suffix.is_valid_int()) {
            continue;
        }
        const int64_t index = suffix.to_int();
        if (index >= 0 && index < old_cases.size() && (index >= cases.size() || old_cases[index] != cases[index])) {
            report("Disconnect the affected case before changing or reordering it.");
            return;
        }
    }
    replace_node_field("Change Switch Cases", "cases", cases);
}

void VisualScriptPanels::replace_node_field(const String &title, const StringName &field, const Variant &value) {
    if (!on_commit || script.is_null() || graph_node_id <= 0) {
        return;
    }
    Dictionary body = graph_body(document, graph_id).duplicate();
    Array nodes = records(body, "nodes").duplicate();
    for (int64_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].get_type() != Variant::DICTIONARY || record_id(nodes[index]) != graph_node_id) {
            continue;
        }
        Dictionary node = Dictionary(nodes[index]).duplicate();
        const bool erase = value.get_type() == Variant::NIL;
        if (erase ? !node.has(field) : node.get(field, Variant()) == value) {
            report(String());
            return;
        }
        if (erase) {
            node.erase(field);
        } else {
            node[field] = value;
        }
        nodes[index] = node;
        body["nodes"] = nodes;
        const Dictionary updated = replace_graph_body(document, graph_id, body);
        if (visual_script_edit_breaks_wires(document, updated, script->get_graph_base())) {
            report("This edit would remove a wired output. Disconnect it before changing the node.");
            return;
        }
        on_commit(title, updated);
        return;
    }
}

void VisualScriptPanels::insert_native_signal(const Dictionary &node) {
    if (graph_id == 0 && on_insert_node) {
        on_insert_node(node);
    }
}

void VisualScriptPanels::report(const String &message) {
    if (details_feedback) {
        details_feedback->set_text(message);
        details_feedback->set_visible(!message.is_empty());
    }
}

void register_visual_script_panels() {
    GDREGISTER_INTERNAL_CLASS(DetailsField);
    GDREGISTER_INTERNAL_CLASS(VisualScriptTypePicker);
    GDREGISTER_INTERNAL_CLASS(VisualScriptCollectionEdit);
    GDREGISTER_INTERNAL_CLASS(VariableRowLabels);
    GDREGISTER_INTERNAL_CLASS(VisualScriptPanels);
}

} // namespace crimson::visual_scripting

#endif
