// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_clipboard.hpp"

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/editor/visual_script_authored_enums.hpp"
#include "modules/visual_scripting/editor/visual_script_organization.hpp"
#include "modules/visual_scripting/visual_script.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_cast_input.hpp"
#include "modules/visual_scripting/visual_script_collections.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"

#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// Identify this format before reading any external clipboard data.
constexpr char clipboard_prefix[] = "Crimson Visual Graph 1\n";
/// Reserve the document format family separately from scoped selection copies.
constexpr char document_prefix[] = "Crimson Visual Script Document ";
/// Whole documents are transported as stock Godot v1 graph records.
constexpr int64_t portable_graph_version = 1;
/// Match the runtime's upper bound for executable nodes per graph body.
constexpr int64_t max_nodes = 4096;
/// Match the runtime's upper bound for wires per graph body.
constexpr int64_t max_wires = max_nodes * 8;
/// Limit clipboard reads before base64 and Variant decoding.
constexpr int64_t max_text_length = 4 * 1024 * 1024;
/// Reject deeply nested authored values and recursive containers.
constexpr int max_container_depth = 24;
/// Bound recursive inspection of graph and literal data.
constexpr int64_t max_values = 200000;

/// Copyable nodes and edges, with the upper-left node origin in graph coordinates.
struct GraphSelection {
    /// Node records with their original stable IDs.
    Array nodes;
    /// Wires whose endpoints are both selected.
    Array wires;
    /// Wires from an unselected producer to a selected node.
    Array incoming;
    /// Upper-left corner of the selected node positions.
    Vector2 origin;
};

/// Read a saved array without coercing another value type.
Array array_at(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Read integer IDs without coercing floats or strings.
int64_t id_at(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::INT ? static_cast<int64_t>(value) : 0;
}

/// Read authored names in either of the saved string forms.
StringName name_at(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME ? StringName(value) : StringName();
}

/// Scope clipboard contents to the original editor process, script and graph.
String scoped_header(int64_t graph_id, uint64_t script_instance_id) {
    static const int64_t session = std::chrono::steady_clock::now().time_since_epoch().count();
    return String(clipboard_prefix) + String::num_int64(OS::get_singleton()->get_process_id()) + "." +
            String::num_int64(session) + "." + String::num_uint64(script_instance_id) + "." +
            String::num_int64(graph_id) + "\n";
}

/// Only graph literals and value types belong in a clipboard document.
bool is_data(const Variant &value, int depth, int64_t &remaining, bool portable = false) {
    if (--remaining < 0 || depth > max_container_depth) {
        return false;
    }
    switch (value.get_type()) {
        case Variant::OBJECT:
        case Variant::CALLABLE:
        case Variant::SIGNAL:
        case Variant::RID:
            return false;
        case Variant::ARRAY: {
            const Array array = value;
            if (portable && array.get_typed_builtin() == Variant::OBJECT) return false;
            for (const Variant &item : array) {
                if (!is_data(item, depth + 1, remaining, portable)) return false;
            }
            return true;
        }
        case Variant::DICTIONARY: {
            const Dictionary dictionary = value;
            if (portable) {
                const int64_t key_type = dictionary.get_typed_key_builtin();
                if (key_type == Variant::OBJECT || dictionary.get_typed_value_builtin() == Variant::OBJECT) return false;
                if ((key_type == Variant::NIL || key_type == Variant::STRING || key_type == Variant::STRING_NAME) &&
                        (dictionary.has("script_path") || !asset_reference_path(value).is_empty())) return false;
            }
            for (const Variant &key : dictionary.keys()) {
                if (!is_data(key, depth + 1, remaining, portable) ||
                        !is_data(dictionary[key], depth + 1, remaining, portable)) return false;
            }
            return true;
        }
        default:
            return true;
    }
}

/// Keep unique graph entry points with their owning definitions.
bool is_copyable(const StringName &kind) {
    return !kind.is_empty() && kind != StringName("ready") && kind != StringName("lifecycle") &&
            kind != StringName("native_signal") && kind != StringName("custom_event") &&
            kind != StringName("function_entry") && kind != StringName("macro_entry") && kind != StringName("return");
}

/// Check a selected record's identity, position and literal data before copying it.
bool can_copy_node(const Dictionary &node, int64_t &remaining) {
    const Variant position = node.get("position", Vector2());
    const Variant values = node.get("values", Dictionary());
    return id_at(node, "id") > 0 && is_copyable(name_at(node, "kind")) &&
            position.get_type() == Variant::VECTOR2 && Vector2(position).is_finite() &&
            values.get_type() == Variant::DICTIONARY && is_data(node, 0, remaining);
}

/// Select exactly the requested nodes and the applicable wires without mutating the graph.
GraphSelection collect_selection(const Dictionary &graph, const PackedInt64Array &selection, bool keep_inputs, String &error) {
    if (selection.is_empty() || selection.size() > max_nodes) {
        error = "Select up to 4096 graph nodes to copy.";
        return {};
    }
    const std::set<int64_t> selected(selection.begin(), selection.end());
    std::set<int64_t> found;
    GraphSelection result;
    int64_t remaining = max_values;
    for (const Variant &value : array_at(graph, "nodes")) {
        if (value.get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = value;
        const int64_t id = id_at(node, "id");
        if (selected.count(id) == 0) continue;
        if (!found.insert(id).second || !can_copy_node(node, remaining)) {
            error = "Select ordinary nodes with valid values. Graph entry and exit nodes belong to their declarations.";
            return {};
        }
        const Vector2 position = node.get("position", Vector2());
        result.origin = result.nodes.is_empty() ? position : result.origin.min(position);
        result.nodes.append(node);
    }
    if (found != selected) {
        error = "A selected node is no longer in the graph.";
        return {};
    }
    for (const Variant &value : array_at(graph, "connections")) {
        if (value.get_type() != Variant::DICTIONARY) continue;
        const Dictionary wire = value;
        if (selected.count(id_at(wire, "to")) == 0) continue;
        if (selected.count(id_at(wire, "from")) != 0) {
            result.wires.append(wire);
        } else if (keep_inputs) {
            result.incoming.append(wire);
        }
    }
    if (result.wires.size() > max_wires || result.incoming.size() > max_wires) {
        error = "The selected nodes have too many connections to copy.";
        return {};
    }
    return result;
}

/// Resolve references by their exact declaration kind and existing ID.
bool has_declaration(const Dictionary &document, const Dictionary &graph, const Dictionary &node) {
    const StringName kind = name_at(node, "kind");
    if (kind == StringName("get_variable") || kind == StringName("set_variable")) {
        const StringName scope = name_at(node, "scope");
        if (!scope.is_empty() && scope != StringName("local") && scope != StringName("instance")) return false;
        const Array variables = scope == StringName("local") ? array_at(graph, "locals") : array_at(document, "variables");
        return !find_graph_record(variables, id_at(node, "variable")).is_empty();
    }
    const char *declarations = nullptr;
    if (kind == StringName("call_function")) declarations = "functions";
    if (kind == StringName("call_macro")) declarations = "macros";
    if (kind == StringName("emit_event")) declarations = "events";
    if (kind == StringName("custom_event")) declarations = "events";
    return !declarations || !find_graph_record(array_at(document, declarations), id_at(node, "definition")).is_empty();
}

/// Build a port index from the same descriptors the editor and runtime use.
std::map<StringName, Dictionary> index_ports(const Dictionary &document, const Dictionary &graph,
        const Dictionary &node, bool output, const StringName &base_class) {
    std::map<StringName, Dictionary> indexed;
    for (const Variant &value : graph_pins(document, graph, node, output, base_class)) {
        if (value.get_type() != Variant::DICTIONARY) continue;
        const Dictionary port = value;
        const StringName id = name_at(port, "id");
        if (!id.is_empty()) indexed.emplace(id, port);
    }
    return indexed;
}

/// Resolve a saved port name without inserting an entry.
Dictionary find_port(const std::map<StringName, Dictionary> &ports, const StringName &name) {
    const auto port = ports.find(name);
    return port == ports.end() ? Dictionary() : port->second;
}

/// Check node shape and references against the destination before resolving any wires.
bool validate_node(const Dictionary &document, const Dictionary &graph, const Dictionary &node, String &error) {
    const StringName kind = name_at(node, "kind");
    if (!has_declaration(document, graph, node)) {
        error = "A copied node refers to a declaration that no longer exists in this graph.";
        return false;
    }
    if (kind == StringName("create_node")) {
        const StringName native_class = name_at(node, "class");
        if (native_class.is_empty() || !ClassDB::class_exists(native_class) || !ClassDB::is_parent_class(native_class, "Node") ||
                !ClassDB::can_instantiate(native_class)) {
            error = "A copied class is no longer a Godot Node that can be instantiated.";
            return false;
        }
    }
    if (kind == StringName("get_property") || kind == StringName("set_property") || kind == StringName("call_method")) {
        const Variant descriptor = node.get("member", Variant());
        if (descriptor.get_type() != Variant::DICTIONARY) {
            error = "A copied native member has no descriptor.";
            return false;
        }
        error = validate_native_member(kind, descriptor);
        if (!error.is_empty()) return false;
    }
    if (kind == StringName("switch_enum")) {
        error = validate_enum_switch(node);
        if (!error.is_empty()) return false;
    }
    return true;
}

/// Verify copied execution edges can be applied without creating a cycle.
bool execution_is_acyclic(const std::map<int64_t, std::vector<int64_t>> &outgoing, std::map<int64_t, size_t> incoming) {
    std::vector<int64_t> ready;
    for (const auto &[id, count] : incoming) {
        if (count == 0) ready.push_back(id);
    }
    for (size_t index = 0; index < ready.size(); ++index) {
        const auto children = outgoing.find(ready[index]);
        if (children == outgoing.end()) continue;
        for (const int64_t child : children->second) {
            if (--incoming.at(child) == 0) ready.push_back(child);
        }
    }
    return ready.size() == incoming.size();
}

/// Place and rewire a selection as one validated document change.
Dictionary insert_selection(const Dictionary &document, int64_t graph_id, const GraphSelection &selection, const Vector2 &translation,
        const StringName &base_class, PackedInt64Array &created, String &error) {
    Dictionary graph = graph_body(document, graph_id).duplicate();
    const Array existing_nodes = array_at(graph, "nodes");
    const Array existing_wires = array_at(graph, "connections");
    if (graph.is_empty() || !translation.is_finite() || selection.nodes.is_empty() || selection.nodes.size() > max_nodes ||
            existing_nodes.size() + selection.nodes.size() > max_nodes ||
            existing_wires.size() + selection.wires.size() > max_wires) {
        error = "This selection cannot fit in the destination graph.";
        return {};
    }
    int64_t largest = 0;
    std::map<int64_t, Dictionary> original_nodes;
    for (const Variant &value : existing_nodes) {
        if (value.get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = value;
        const int64_t id = id_at(node, "id");
        largest = std::max(largest, id);
        original_nodes.emplace(id, node);
    }
    for (const Variant &value : selection.nodes) {
        if (value.get_type() == Variant::DICTIONARY) largest = std::max(largest, id_at(value, "id"));
    }
    if (largest > std::numeric_limits<int64_t>::max() - selection.nodes.size()) {
        error = "This graph has no more available node IDs.";
        return {};
    }

    Array nodes = existing_nodes.duplicate();
    Array wires = existing_wires.duplicate();
    PackedInt64Array new_ids;
    std::map<int64_t, int64_t> mapped;
    std::map<int64_t, std::map<StringName, Dictionary>> inputs;
    std::map<int64_t, std::map<StringName, Dictionary>> outputs;
    int64_t remaining = max_values;
    for (const Variant &value : selection.nodes) {
        if (value.get_type() != Variant::DICTIONARY || !can_copy_node(value, remaining)) {
            error = "The clipboard contains an invalid graph node.";
            return {};
        }
        Dictionary node = Dictionary(value).duplicate(true);
        const int64_t previous_id = id_at(node, "id");
        if (!mapped.emplace(previous_id, ++largest).second) {
            error = "The clipboard contains duplicate node IDs.";
            return {};
        }
        if (!validate_node(document, graph, node, error)) return {};
        const Vector2 position = Vector2(node.get("position", Vector2())) + translation;
        if (!position.is_finite()) {
            error = "A copied node position is outside the graph.";
            return {};
        }
        auto input_ports = index_ports(document, graph, node, false, base_class);
        auto output_ports = index_ports(document, graph, node, true, base_class);
        if (input_ports.empty() && output_ports.empty()) {
            error = "A copied node has no valid ports in this graph.";
            return {};
        }
        inputs.emplace(previous_id, std::move(input_ports));
        outputs.emplace(previous_id, std::move(output_ports));
        node["id"] = largest;
        node["position"] = position;
        nodes.append(node);
        new_ids.append(largest);
    }

    std::set<std::pair<int64_t, StringName>> data_inputs;
    std::set<std::pair<int64_t, StringName>> execution_outputs;
    std::map<int64_t, std::vector<int64_t>> execution_children;
    std::map<int64_t, size_t> execution_parents;
    for (const Variant &value : selection.wires) {
        if (value.get_type() != Variant::DICTIONARY) {
            error = "The clipboard contains an invalid connection.";
            return {};
        }
        const Dictionary wire = value;
        const int64_t from = id_at(wire, "from");
        const int64_t to = id_at(wire, "to");
        const StringName from_pin = name_at(wire, "from_pin");
        const StringName to_pin = name_at(wire, "to_pin");
        if (from == to || !mapped.count(from) || !mapped.count(to)) {
            error = "A copied connection references a node outside the selection.";
            return {};
        }
        const Dictionary source = find_port(outputs.at(from), from_pin);
        const Dictionary destination = find_port(inputs.at(to), to_pin);
        const bool execution = source.get("exec", false);
        const bool target_execution = destination.get("exec", false);
        if (source.is_empty() || destination.is_empty() || execution != target_execution ||
                (execution ? !execution_outputs.emplace(from, from_pin).second :
                !pins_compatible(source, destination) || !data_inputs.emplace(to, to_pin).second)) {
            error = "A copied connection has incompatible or occupied pins.";
            return {};
        }
        Dictionary copy = wire.duplicate();
        copy["from"] = mapped.at(from);
        copy["to"] = mapped.at(to);
        wires.append(copy);
        if (execution) {
            execution_children[from].push_back(to);
            execution_parents.try_emplace(from, 0);
            ++execution_parents[to];
        }
    }
    if (!execution_is_acyclic(execution_children, std::move(execution_parents))) {
        error = "The copied nodes contain a loop of execution wires.";
        return {};
    }
    for (const Dictionary &wire : selection.incoming) {
        const int64_t from = id_at(wire, "from");
        const int64_t to = id_at(wire, "to");
        const auto found = original_nodes.find(from);
        if (found == original_nodes.end() || !mapped.count(to)) continue;
        const StringName to_pin = name_at(wire, "to_pin");
        const Dictionary destination = find_port(inputs.at(to), to_pin);
        if (destination.is_empty() || destination.get("exec", false) || data_inputs.count({to, to_pin})) continue;
        const auto ports = index_ports(document, graph, found->second, true, base_class);
        const Dictionary source = find_port(ports, name_at(wire, "from_pin"));
        if (source.is_empty() || source.get("exec", false) || !pins_compatible(source, destination)) continue;
        if (wires.size() >= max_wires) {
            error = "The duplicated inputs exceed the graph's connection limit.";
            return {};
        }
        data_inputs.emplace(to, to_pin);
        Dictionary copy = wire.duplicate();
        copy["to"] = mapped.at(to);
        wires.append(copy);
    }
    graph["nodes"] = nodes;
    graph["connections"] = wires;
    Dictionary changed = replace_graph_body(document, graph_id, graph);
    if (!changed.is_empty()) created = new_ids;
    return changed;
}

/// Accept canonical-sized base64 characters before calling Godot's decoder.
bool is_base64(const String &text) {
    const int64_t length = text.length();
    if (length == 0 || length % 4 != 0) return false;
    int64_t data_end = length;
    if (text[data_end - 1] == U'=') --data_end;
    if (text[data_end - 1] == U'=') --data_end;
    for (int64_t index = 0; index < data_end; ++index) {
        const char32_t character = text[index];
        if ((character < U'A' || character > U'Z') && (character < U'a' || character > U'z') &&
                (character < U'0' || character > U'9') && character != U'+' && character != U'/') return false;
    }
    return true;
}

/// Check the size and type of a serialized graph list before reading its elements.
bool is_record_list(const Dictionary &record, const char *key, int64_t limit, bool required = false) {
    if (!record.has(key)) return !required;
    const Variant value = record[key];
    return value.get_type() == Variant::ARRAY && Array(value).size() <= limit;
}

/// Check a project-independent default using the same accepted value types as the runtime.
bool is_portable_pin_value(const Dictionary &pin, const Variant &value) {
    if (pin_has_collection_schema(pin)) {
        Variant resolved;
        return resolve_collection_value(pin, value, resolved);
    }
    return pin_accepts_value(pin, value);
}

/// Keep malformed metadata and project-only types out of a saved pin declaration.
bool is_portable_pin(const Variant &value, bool require_id = true) {
    if (value.get_type() != Variant::DICTIONARY) return false;
    const Dictionary pin = value;
    const StringName name = name_at(pin, "id");
    return (!require_id || !name.is_empty()) && RuntimePinType::from_pin(pin).type != Variant::VARIANT_MAX &&
            (!pin.has("exec") || pin["exec"].get_type() == Variant::BOOL) &&
            (!pin.has("default") || is_portable_pin_value(pin, pin["default"]));
}

/// Check one saved signature before asking the native graph for its boundary ports.
bool is_portable_signature(const Dictionary &record, const char *key) {
    if (!is_record_list(record, key, max_nodes)) return false;
    std::set<StringName> used;
    for (const Variant &value : array_at(record, key)) {
        if (!is_portable_pin(value) || !used.insert(name_at(value, "id")).second) return false;
    }
    return true;
}

/// Check declarations within one scope, where names and positive IDs are unique.
bool is_portable_variable_list(const Dictionary &record, const char *key) {
    if (!is_record_list(record, key, max_nodes)) return false;
    std::set<int64_t> ids;
    std::set<StringName> names;
    for (const Variant &value : array_at(record, key)) {
        if (value.get_type() != Variant::DICTIONARY) return false;
        const Dictionary declaration = value;
        const int64_t id = id_at(declaration, "id");
        const StringName name = name_at(declaration, "name");
        const Variant pin = declaration.get("pin", Variant());
        if (id <= 0 || !ids.insert(id).second || !String(name).is_valid_identifier() ||
                !names.insert(name).second || !is_portable_pin(pin, false) ||
                !is_portable_pin_value(pin, declaration.get("value", Variant()))) return false;
    }
    return true;
}

/// Check authored enums without querying the engine or evaluating stored defaults.
bool is_portable_enum_list(const Dictionary &document) {
    if (!is_record_list(document, "enums", max_nodes)) return false;
    std::set<int64_t> ids;
    std::set<StringName> names;
    for (const Variant &value : array_at(document, "enums")) {
        if (value.get_type() != Variant::DICTIONARY) return false;
        const Dictionary enumeration = value;
        const int64_t id = id_at(enumeration, "id");
        const StringName name = name_at(enumeration, "name");
        if (id <= 0 || !ids.insert(id).second || !String(name).is_valid_identifier() || !names.insert(name).second ||
                !is_record_list(enumeration, "entries", max_nodes, true)) return false;
        std::set<int64_t> entry_ids;
        for (const Variant &record : array_at(enumeration, "entries")) {
            if (record.get_type() != Variant::DICTIONARY) return false;
            const Dictionary entry = record;
            const int64_t entry_id = id_at(entry, "id");
            if (entry_id <= 0 || !entry_ids.insert(entry_id).second || name_at(entry, "name").is_empty() ||
                    entry.get("value", Variant()).get_type() != Variant::INT) return false;
        }
    }
    return true;
}

/// Check finite coordinates before handing them to the editor's native canvas.
bool is_graph_coordinate(const Variant &value) {
    if (value.get_type() != Variant::VECTOR2) return false;
    const Vector2 position = value;
    constexpr float max_position = 1000000.0f;
    return position.is_finite() && position.abs().x <= max_position && position.abs().y <= max_position;
}

/// Check comment geometry and ensure membership points into this body.
bool is_portable_comment_list(const Dictionary &graph, const std::set<int64_t> &nodes) {
    if (!is_record_list(graph, "comments", max_nodes)) return false;
    std::set<int64_t> ids;
    std::set<int64_t> members;
    for (const Variant &value : array_at(graph, "comments")) {
        if (value.get_type() != Variant::DICTIONARY) return false;
        const Dictionary comment = value;
        const int64_t id = id_at(comment, "id");
        const Variant size = comment.get("size", Variant());
        if (id <= 0 || !ids.insert(id).second || !is_graph_coordinate(comment.get("position", Variant())) ||
                !is_graph_coordinate(size) || Vector2(size).x <= 0 || Vector2(size).y <= 0 ||
                !is_record_list(comment, "members", max_nodes)) return false;
        for (const Variant &member : array_at(comment, "members")) {
            if (member.get_type() != Variant::INT || !nodes.count(int64_t(member)) ||
                    !members.insert(int64_t(member)).second) return false;
        }
    }
    return true;
}

/// Reject node kinds that require project-local objects or embedded source before inspecting their ports.
bool is_portable_node_kind(const Dictionary &node) {
    const StringName kind = name_at(node, "kind");
    return kind != StringName("custom_script") && kind != StringName("scene_node") &&
            kind != StringName("preload_resource") && kind != StringName("load_resource") &&
            kind != StringName("create_script_instance") &&
            !(kind == StringName("singleton") && name_at(node, "source") == StringName("autoload"));
}

/// Match a saved native signal's argument IDs and types to the destination engine.
bool has_portable_native_signal(const Dictionary &node) {
    const StringName signal = name_at(node, "signal");
    const StringName target = name_at(node, "target_class");
    const StringName native_class = target.is_empty() ? name_at(node, "class") : target;
    if (signal.is_empty() || native_class.is_empty() || !is_portable_signature(node, "inputs")) return false;
    const Array saved = array_at(node, "inputs");
    for (const Dictionary &candidate : find_native_signals(native_class)) {
        if (name_at(candidate, "signal") != signal) continue;
        const Array inputs = array_at(candidate, "inputs");
        if (inputs.size() != saved.size()) return false;
        for (int64_t index = 0; index < saved.size(); ++index) {
            const Dictionary before = saved[index];
            const Dictionary current = inputs[index];
            if (name_at(before, "id") != name_at(current, "id") ||
                    collection_terminal_type(before) != collection_terminal_type(current)) return false;
        }
        return true;
    }
    return false;
}

/// Validate node metadata and ownership before resolving ports against the full document.
bool validate_document_node(const Dictionary &document, const Dictionary &graph, const Dictionary &node,
        const StringName &entry_kind, String &error) {
    if (!is_portable_node_kind(node)) {
        error = "This graph uses project-specific references or embedded code that cannot be pasted as a standalone document.";
        return false;
    }
    const StringName kind = name_at(node, "kind");
    const bool entry = kind == StringName("function_entry") || kind == StringName("macro_entry");
    const bool event = kind == StringName("ready") || kind == StringName("lifecycle") ||
            kind == StringName("native_signal") || kind == StringName("custom_event");
    const Variant values = node.get("values", Dictionary());
    if (!is_graph_coordinate(node.get("position", Vector2())) || values.get_type() != Variant::DICTIONARY ||
            (entry && kind != entry_kind) || (kind == StringName("return") && entry_kind.is_empty()) ||
            (event && !entry_kind.is_empty()) ||
            ((entry || kind == StringName("return")) && node.has("definition") &&
                    id_at(node, "definition") != id_at(graph, "definition"))) {
        error = "The graph clipboard contains an invalid node or graph boundary.";
        return false;
    }
    const auto operation = collection_operation(kind);
    if ((operation == CollectionOperation::MAKE_ARRAY || operation == CollectionOperation::MAKE_DICTIONARY) &&
            !valid_collection_arity(node)) {
        error = "The graph clipboard contains an invalid collection size.";
        return false;
    }
    if (kind == StringName("cast")) {
        error = validate_cast_node(node);
        if (!error.is_empty()) return false;
    }
    if ((kind == StringName("native_signal") || (kind == StringName("await_signal") && !name_at(node, "signal").is_empty())) &&
            !has_portable_native_signal(node)) {
        error = "The graph clipboard refers to a Godot signal whose signature is unavailable.";
        return false;
    }
    if (kind == StringName("literal") && node.has("global_constant") && !find_global_constant(name_at(node, "global_constant"))) {
        error = "The graph clipboard refers to an unavailable Godot constant.";
        return false;
    }
    return validate_node(document, graph, node, error);
}

/// Resolve every supplied edge against the engine's live port contracts without replacing any.
bool validate_document_body(const Dictionary &document, const Dictionary &graph, const StringName &entry_kind,
        const StringName &base_class, String &error) {
    if (!is_record_list(graph, "nodes", max_nodes, true) || !is_record_list(graph, "connections", max_wires, true) ||
            !is_portable_variable_list(graph, "locals")) {
        error = "The graph clipboard has an invalid node, connection, or local variable list.";
        return false;
    }
    const Array nodes = graph["nodes"];
    const Array connections = graph["connections"];
    std::set<int64_t> ids;
    std::set<StringName> callbacks;
    std::set<int64_t> events;
    int entries = 0;
    for (const Variant &value : nodes) {
        if (value.get_type() != Variant::DICTIONARY || id_at(value, "id") <= 0 ||
                !ids.insert(id_at(value, "id")).second) {
            error = "The graph clipboard contains an invalid or duplicate node ID.";
            return false;
        }
        const Dictionary node = value;
        if (!validate_document_node(document, graph, node, entry_kind, error)) return false;
        const StringName kind = name_at(node, "kind");
        if (!entry_kind.is_empty() && kind == entry_kind) ++entries;
        if ((kind == StringName("ready") || kind == StringName("lifecycle")) &&
                !callbacks.insert(kind == StringName("ready") ? StringName("_ready") : name_at(node, "callback")).second) {
            error = "The graph clipboard contains duplicate lifecycle entries.";
            return false;
        }
        if (kind == StringName("custom_event") && !events.insert(id_at(node, "definition")).second) {
            error = "The graph clipboard contains duplicate Custom Event entries.";
            return false;
        }
    }
    if ((!entry_kind.is_empty() && entries != 1) || !is_portable_comment_list(graph, ids)) {
        error = "The graph clipboard has an invalid comment or graph entry.";
        return false;
    }
    for (const Variant &value : connections) {
        if (value.get_type() != Variant::DICTIONARY || id_at(value, "from") == id_at(value, "to") ||
                !ids.count(id_at(value, "from")) || !ids.count(id_at(value, "to")) ||
                name_at(value, "from_pin").is_empty() || name_at(value, "to_pin").is_empty()) {
            error = "A connection in the graph clipboard has an invalid endpoint.";
            return false;
        }
    }
    std::map<int64_t, std::map<StringName, Dictionary>> inputs;
    std::map<int64_t, std::map<StringName, Dictionary>> outputs;
    for (const Dictionary &node : nodes) {
        const auto id = id_at(node, "id");
        inputs.emplace(id, index_ports(document, graph, node, false, base_class));
        outputs.emplace(id, index_ports(document, graph, node, true, base_class));
        if (inputs.at(id).empty() && outputs.at(id).empty()) {
            error = "A graph clipboard node has no available ports in this build.";
            return false;
        }
    }
    std::set<std::pair<int64_t, StringName>> data_inputs;
    std::set<std::pair<int64_t, StringName>> execution_outputs;
    std::map<int64_t, std::vector<int64_t>> execution_children;
    std::map<int64_t, size_t> execution_parents;
    for (const Dictionary &connection : connections) {
        const int64_t from = id_at(connection, "from");
        const int64_t to = id_at(connection, "to");
        const StringName from_pin = name_at(connection, "from_pin");
        const StringName to_pin = name_at(connection, "to_pin");
        const Dictionary source = find_port(outputs.at(from), from_pin);
        const Dictionary target = find_port(inputs.at(to), to_pin);
        const bool execution = source.get("exec", false);
        if (source.is_empty() || target.is_empty() || execution != bool(target.get("exec", false)) ||
                (execution ? !execution_outputs.emplace(from, from_pin).second :
                        !pins_compatible(source, target) || !data_inputs.emplace(to, to_pin).second)) {
            error = "A connection in the graph clipboard has incompatible or occupied pins.";
            return false;
        }
        if (execution) {
            execution_children[from].push_back(to);
            execution_parents.try_emplace(from, 0);
            ++execution_parents[to];
        }
    }
    if (!execution_is_acyclic(execution_children, std::move(execution_parents))) {
        error = "The graph clipboard contains a cycle of execution wires.";
        return false;
    }
    return true;
}

/// Check all declarations before resolving calls and event references in their graph bodies.
bool validate_portable_document(const Dictionary &document, const StringName &base_class, String &error) {
    if (id_at(document, "version") != portable_graph_version || !is_portable_variable_list(document, "variables") ||
            !is_portable_enum_list(document)) {
        error = "The graph clipboard uses an unsupported version or contains invalid declarations.";
        return false;
    }
    std::set<int64_t> definitions;
    std::set<StringName> callables;
    for (const char *key : {"functions", "macros", "events"}) {
        if (!is_record_list(document, key, max_nodes)) {
            error = "The graph clipboard has an invalid declaration list.";
            return false;
        }
        const bool macro = StringName(key) == StringName("macros");
        const bool event = StringName(key) == StringName("events");
        std::set<StringName> macro_names;
        for (const Variant &value : array_at(document, key)) {
            if (value.get_type() != Variant::DICTIONARY) {
                error = "The graph clipboard contains an invalid callable declaration.";
                return false;
            }
            const Dictionary record = value;
            const int64_t id = id_at(record, "id");
            const StringName name = name_at(record, "name");
            if (id <= 0 || !definitions.insert(id).second || !String(name).is_valid_identifier() ||
                    !(macro ? macro_names : callables).insert(name).second ||
                    !is_portable_signature(record, "inputs") || !is_portable_signature(record, "outputs") ||
                    (!event && record.get("graph", Variant()).get_type() != Variant::DICTIONARY)) {
                error = "The graph clipboard has an invalid or duplicate callable declaration.";
                return false;
            }
        }
    }
    if (!validate_document_body(document, document, StringName(), base_class, error)) return false;
    for (const char *key : {"functions", "macros"}) {
        const StringName kind = StringName(key) == StringName("functions") ? "function_entry" : "macro_entry";
        for (const Dictionary &record : array_at(document, key)) {
            const Dictionary graph = graph_body(document, id_at(record, "id"));
            if (!validate_document_body(document, graph, kind, base_class, error)) return false;
        }
    }
    return true;
}

/// Only an empty root event graph can receive a standalone document.
bool is_empty_destination(const Dictionary &document) {
    if (id_at(document, "version") != portable_graph_version || !is_record_list(document, "nodes", 0, true) ||
            !is_record_list(document, "connections", 0, true)) return false;
    for (const Variant &key : document.keys()) {
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) return false;
        const String field = key;
        if (field == "version" || field == "nodes" || field == "connections") continue;
        if (field != "locals" && field != "variables" && field != "enums" && field != "functions" &&
                field != "macros" && field != "events" && field != "comments") return false;
        const Variant existing = document[key];
        if (existing.get_type() != Variant::ARRAY || !Array(existing).is_empty()) return false;
    }
    return true;
}

/// Declaration lists that belong to the document rather than one callable body.
constexpr const char *document_sections[] = {"enums", "variables", "locals", "functions", "macros", "events"};

using IdMap = std::map<int64_t, int64_t>;
using DeclarationName = std::pair<StringName, StringName>;

/// One incoming declaration and the alternatives available if its name is occupied.
struct DeclarationImport {
    /// Serialized declaration list.
    StringName section;
    /// Incoming record before any IDs have been assigned.
    Dictionary source;
    /// Candidate existing declaration with the same name, if any.
    Dictionary target;
    /// Serialized declaration list of the existing candidate.
    StringName target_section;
    /// Reserved name used when inserting a distinct declaration.
    StringName copy_name;
    /// Why this conflict cannot replace its target.
    String replace_problem;
    /// Whether an existing variable or local has a contract that must be kept.
    bool target_variable_is_used = true;
    /// Choice list index, or -1 when the original name is available.
    int64_t conflict_index = -1;
};

/// Stable addresses and pending names for the imported graph only.
struct DocumentImport {
    /// Declaration rows in deterministic order.
    std::vector<DeclarationImport> declarations;
    /// Incoming lifecycle node ID to generated Custom Event name.
    std::map<int64_t, StringName> lifecycle_copies;
    /// Display records for the declaration conflict dialog.
    Array conflicts;
};

/// Report the distinct declaration name namespace.
StringName name_scope(const StringName &section) {
    return section == StringName("functions") || section == StringName("events") ? StringName("callables") : section;
}

/// Functions, macros, and events share numeric IDs even where their names have separate namespaces.
StringName id_scope(const StringName &section) {
    return name_scope(section) == StringName("callables") || section == StringName("macros") ? StringName("definitions") : section;
}

/// Describe a conflict row using the declaration names shown in the workbench.
const char *declaration_label(const StringName &section) {
    if (section == StringName("variables")) return "Variable";
    if (section == StringName("locals")) return "Local Variable";
    if (section == StringName("enums")) return "Enum";
    if (section == StringName("functions")) return "Function";
    if (section == StringName("macros")) return "Macro";
    return "Custom Event";
}

/// Enforce the same reserved callable names as declaration editing.
bool reserved_callable(const StringName &scope, const StringName &base, const StringName &name) {
    return scope == StringName("callables") && (String(name).begins_with("_") || ClassDB::class_has_method(base, name));
}

/// Use the editor's active scene-owner export rules when available.
bool can_paste_export(const StringName &base, const StringName &name, const Dictionary &pin, const Callable &export_check) {
    return export_check.is_valid() ? bool(export_check.call(name, pin)) : can_export_variable(base, name, pin);
}

/// Reserve a suffix without capturing names from other incoming declarations.
StringName reserve_copy_name(std::set<DeclarationName> &names, const StringName &scope,
        const StringName &base, const StringName &source, const Dictionary *exported_pin = nullptr, const Callable &export_check = {}) {
    String prefix = source;
    if (scope == StringName("callables")) {
        int64_t first = 0;
        while (first < prefix.length() && prefix[first] == U'_') ++first;
        if (first > 0) prefix = prefix.substr(first);
        if (prefix.is_empty()) prefix = "custom_event";
    }
    int64_t rejected_exports = 0;
    for (int64_t index = 1; index < std::numeric_limits<int64_t>::max(); ++index) {
        const StringName candidate = prefix + "_" + String::num_int64(index);
        if (reserved_callable(scope, base, candidate) || names.count({scope, candidate})) continue;
        if (exported_pin && !can_paste_export(base, candidate, *exported_pin, export_check)) {
            // A malformed export type can make every name unavailable.
            if (++rejected_exports >= max_nodes) return {};
            continue;
        }
        names.emplace(scope, candidate);
        return candidate;
    }
    return {};
}

/// Read the unique callback identity for either saved spelling of a native lifecycle entry.
StringName lifecycle_callback(const Dictionary &node) {
    const StringName kind = name_at(node, "kind");
    if (kind == StringName("ready")) return "_ready";
    return kind == StringName("lifecycle") ? name_at(node, "callback") : StringName();
}

/// Compare authored enum types in the namespace that will own the pasted records.
bool same_enum_identity(const Dictionary &old_pin, const Dictionary &new_pin, const IdMap &enum_ids) {
    const StringName previous = name_at(old_pin, "enum_type");
    const StringName incoming = name_at(new_pin, "enum_type");
    const int64_t id = authored_enum_id(incoming);
    if (id <= 0) return previous == incoming;
    const auto mapped = enum_ids.find(id);
    return mapped != enum_ids.end() && previous == authored_enum_identity(mapped->second);
}

/// Keep existing users of a declaration bound to exactly the same data contract.
bool same_pin_contract(const Dictionary &before, const Dictionary &after, const IdMap &enum_ids) {
    if (before.is_empty() || after.is_empty()) return before.is_empty() && after.is_empty();
    if (before.get("type", Variant()) != after.get("type", Variant()) ||
            before.get("exec", false) != after.get("exec", false) ||
            name_at(before, "class") != name_at(after, "class") ||
            name_at(before, "script_path") != name_at(after, "script_path") ||
            !same_enum_identity(before, after, enum_ids)) return false;
    for (const char *lane : {"element", "key", "value"}) {
        const Variant left = before.get(lane, Variant());
        const Variant right = after.get(lane, Variant());
        if (left.get_type() != right.get_type() ||
                (left.get_type() == Variant::DICTIONARY && !same_pin_contract(left, right, enum_ids))) return false;
    }
    return true;
}

/// Keep stable argument order, pin IDs, and required/defaulted parameter status.
bool same_signature(const Array &before, const Array &after, const IdMap &enum_ids) {
    if (before.size() != after.size()) return false;
    for (int64_t index = 0; index < before.size(); ++index) {
        if (before[index].get_type() != Variant::DICTIONARY || after[index].get_type() != Variant::DICTIONARY) return false;
        const Dictionary first = before[index];
        const Dictionary second = after[index];
        if (name_at(first, "id") != name_at(second, "id") || first.has("default") != second.has("default") ||
                !same_pin_contract(first, second, enum_ids)) return false;
    }
    return true;
}

/// Keep authored switch case identities and numbers valid when replacing an enum.
bool same_enum_entries(const Dictionary &before, const Dictionary &after) {
    const Array first = array_at(before, "entries");
    const Array second = array_at(after, "entries");
    if (first.size() != second.size()) return false;
    for (const Variant &value : second) {
        if (value.get_type() != Variant::DICTIONARY) return false;
        const Dictionary entry = value;
        const Dictionary previous = find_graph_record(first, id_at(entry, "id"));
        if (previous.is_empty() || name_at(previous, "name") != name_at(entry, "name") ||
                id_at(previous, "value") != id_at(entry, "value")) return false;
    }
    return true;
}

/// Inspect one scope for direct references and Custom Script bodies that could read the variable.
bool body_uses_variable(const Dictionary &body, int64_t id, bool local) {
    for (const Variant &value : array_at(body, "nodes")) {
        if (value.get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = value;
        const StringName kind = name_at(node, "kind");
        const bool local_node = name_at(node, "scope") == StringName("local");
        if (kind == StringName("custom_script") || ((kind == StringName("get_variable") || kind == StringName("set_variable")) &&
                local_node == local && id_at(node, "variable") == id)) return true;
    }
    return false;
}

/// Allow an unused private variable's replacement to introduce a different type.
bool variable_is_used(const Dictionary &document, const Dictionary &variable, bool local) {
    if (variable.get("exported", false)) return true;
    const int64_t id = id_at(variable, "id");
    if (body_uses_variable(document, id, local)) return true;
    if (local) return false;
    for (const char *section : {"functions", "macros"}) {
        for (const Variant &value : array_at(document, section)) {
            if (value.get_type() != Variant::DICTIONARY) continue;
            const Variant body = Dictionary(value).get("graph", Variant());
            if (body.get_type() == Variant::DICTIONARY && body_uses_variable(body, id, false)) return true;
        }
    }
    return false;
}

/// Reject replacement if dependent pins, enum cases, or Inspector overrides would change type.
String replacement_problem(const DeclarationImport &declaration, const IdMap &enum_ids) {
    const auto &section = declaration.section;
    const auto &source = declaration.source;
    const auto &target = declaration.target;
    if (target.is_empty()) return "This name is reserved by Godot.";
    if (declaration.target_section != section) return "The existing declaration is a different kind.";
    if (id_at(target, "id") <= 0) return "The existing declaration does not have a valid ID.";
    if (section == StringName("enums")) {
        return same_enum_entries(target, source) ? String() : String("Replacing this enum would change its existing members.");
    }
    if (section == StringName("variables") || section == StringName("locals")) {
        if (!declaration.target_variable_is_used) return {};
        const Variant old_pin = target.get("pin", Variant());
        const Variant new_pin = source.get("pin", Variant());
        if (old_pin.get_type() != Variant::DICTIONARY || new_pin.get_type() != Variant::DICTIONARY ||
                !same_pin_contract(old_pin, new_pin, enum_ids) || target.get("exported", false) != source.get("exported", false)) {
            return "Replacing this variable would change its existing type or Inspector exposure.";
        }
        return {};
    }
    if (target.get("pure", false) != source.get("pure", false) ||
            !same_signature(array_at(target, "inputs"), array_at(source, "inputs"), enum_ids) ||
            !same_signature(array_at(target, "outputs"), array_at(source, "outputs"), enum_ids)) {
        return "Replacing this declaration would change the existing call signature.";
    }
    return {};
}

/// Plan named conflicts before any address is allocated or graph is modified.
DocumentImport plan_document_import(const Dictionary &destination, const Dictionary &source,
        const StringName &base, const Callable &export_check) {
    DocumentImport plan;
    std::map<DeclarationName, std::pair<StringName, Dictionary>> existing;
    std::set<DeclarationName> reserved;
    for (const char *section : document_sections) {
        const StringName scope = name_scope(section);
        for (const Variant &value : array_at(destination, section)) {
            if (value.get_type() != Variant::DICTIONARY) continue;
            const Dictionary record = value;
            const DeclarationName key{scope, name_at(record, "name")};
            existing.try_emplace(key, section, record);
            reserved.insert(key);
        }
        for (const Dictionary &record : array_at(source, section)) {
            reserved.emplace(scope, name_at(record, "name"));
        }
    }

    IdMap matching_enums;
    for (const char *section : document_sections) {
        const StringName scope = name_scope(section);
        for (const Dictionary &record : array_at(source, section)) {
            const StringName name = name_at(record, "name");
            DeclarationImport declaration;
            declaration.section = section;
            declaration.source = record;
            declaration.copy_name = name;
            const auto found = existing.find({scope, name});
            const bool exported = scope == StringName("variables") && record.get("exported", false);
            const Dictionary pin = exported ? Dictionary(record.get("pin", Dictionary())) : Dictionary();
            const bool blocks_export = exported && !can_paste_export(base, name, pin, export_check);
            if (found != existing.end() || reserved_callable(scope, base, name) || blocks_export) {
                if (found != existing.end()) {
                    declaration.target_section = found->second.first;
                    declaration.target = found->second.second;
                    if (scope == StringName("variables") || scope == StringName("locals")) {
                        declaration.target_variable_is_used = variable_is_used(destination, declaration.target,
                                scope == StringName("locals"));
                    }
                }
                declaration.replace_problem = blocks_export ? String("This name or type cannot be exported on the destination node.") :
                        reserved_callable(scope, base, name) ? String("This name is reserved by Godot.") :
                        replacement_problem(declaration, matching_enums);
                if (scope == StringName("enums") && declaration.replace_problem.is_empty()) {
                    matching_enums[id_at(record, "id")] = id_at(declaration.target, "id");
                }
                declaration.copy_name = reserve_copy_name(reserved, scope, base, name, exported ? &pin : nullptr, export_check);
                declaration.conflict_index = plan.conflicts.size();
                Dictionary conflict;
                conflict["section"] = section;
                conflict["kind_label"] = declaration_label(section);
                conflict["name"] = String(name);
                conflict["copy_name"] = String(declaration.copy_name);
                conflict["replace_supported"] = declaration.replace_problem.is_empty();
                conflict["copy_supported"] = !declaration.copy_name.is_empty();
                conflict["reason"] = declaration.replace_problem;
                plan.conflicts.append(conflict);
            }
            plan.declarations.push_back(std::move(declaration));
        }
    }
    std::set<StringName> callbacks;
    for (const Variant &value : array_at(destination, "nodes")) {
        if (value.get_type() != Variant::DICTIONARY) continue;
        const StringName callback = lifecycle_callback(value);
        if (!callback.is_empty()) callbacks.insert(callback);
    }
    for (const Dictionary &record : array_at(source, "nodes")) {
        const StringName callback = lifecycle_callback(record);
        if (!callback.is_empty() && callbacks.count(callback)) {
            const StringName prefix = "on" + String(callback);
            plan.lifecycle_copies.emplace(id_at(record, "id"), reserve_copy_name(reserved, "callables", base, prefix));
        }
    }
    return plan;
}

/// Apply a mapped reference only when it was declared by the clipboard source.
bool remap_id(Dictionary record, const char *field, const IdMap &ids) {
    const auto found = ids.find(id_at(record, field));
    if (found == ids.end()) return false;
    record[field] = found->second;
    return true;
}

/// Keep an authored type connected to its imported enum, never an unrelated destination ID.
bool remap_enum_identity(Dictionary record, const IdMap &ids) {
    const int64_t id = authored_enum_id(name_at(record, "enum_type"));
    if (id <= 0) return true;
    const auto found = ids.find(id);
    if (found == ids.end()) return false;
    record["enum_type"] = authored_enum_identity(found->second);
    return true;
}

/// Traverse only terminal descriptors. Default and literal dictionaries stay byte-for-byte authored data.
bool remap_enum_pin(Dictionary pin, const IdMap &ids) {
    if (!remap_enum_identity(pin, ids)) return false;
    for (const char *field : {"element", "key", "value"}) {
        const Variant child = pin.get(field, Variant());
        if (child.get_type() == Variant::DICTIONARY && !remap_enum_pin(child, ids)) return false;
    }
    return true;
}

/// Remap type metadata on a declaration, node, or reflected member.
bool remap_record_enums(Dictionary record, const IdMap &ids) {
    if (!remap_enum_identity(record, ids)) return false;
    for (const char *field : {"pin", "output", "dictionary_types"}) {
        const Variant pin = record.get(field, Variant());
        if (pin.get_type() == Variant::DICTIONARY && !remap_enum_pin(pin, ids)) return false;
    }
    for (const char *field : {"inputs", "outputs"}) {
        for (const Variant &pin : array_at(record, field)) {
            if (pin.get_type() == Variant::DICTIONARY && !remap_enum_pin(pin, ids)) return false;
        }
    }
    const Variant member = record.get("member", Variant());
    return member.get_type() != Variant::DICTIONARY || remap_record_enums(member, ids);
}

/// Keep address namespaces distinct while importing or replacing declarations.
struct DocumentIds {
    /// Map source to target IDs in each address scope.
    std::map<StringName, IdMap> scopes;
    /// Root node IDs, including any existing event entries selected for replacement.
    IdMap nodes;
    /// Root comment IDs.
    IdMap comments;
    /// Fresh definitions for converted native callbacks.
    IdMap lifecycle_events;
    /// Existing named handler entries whose outgoing wires are replaced.
    std::set<int64_t> replaced_event_nodes;
};

/// Reserve the next positive ID without overflowing a saved signed ID.
bool allocate_id(int64_t &highest, int64_t &id) {
    if (highest >= std::numeric_limits<int64_t>::max()) return false;
    id = ++highest;
    return true;
}

/// Read the highest saved positive ID in one list.
int64_t maximum_id(const Array &records) {
    int64_t largest = 0;
    for (const Variant &value : records) {
        if (value.get_type() == Variant::DICTIONARY) largest = std::max(largest, id_at(value, "id"));
    }
    return largest;
}

/// Find the handler entry for a named Custom Event in a root graph.
Dictionary custom_event_entry(const Dictionary &graph, int64_t definition) {
    for (const Variant &value : array_at(graph, "nodes")) {
        if (value.get_type() == Variant::DICTIONARY && name_at(value, "kind") == StringName("custom_event") &&
                id_at(value, "definition") == definition && id_at(value, "id") > 0) return value;
    }
    return {};
}

/// Assign target addresses for all declarations and root nodes before remapping any links.
bool allocate_document_ids(const Dictionary &destination, const Dictionary &source, const DocumentImport &plan,
        const PackedStringArray &choices, bool preserve_ids, DocumentIds &ids, String &error) {
    std::map<StringName, int64_t> high;
    for (const char *section : document_sections) {
        const StringName scope = id_scope(section);
        high[scope] = std::max(high[scope], maximum_id(array_at(destination, section)));
        ids.scopes.try_emplace(scope);
    }
    for (const auto &declaration : plan.declarations) {
        const bool replace = declaration.conflict_index >= 0 && choices[declaration.conflict_index] == "replace";
        const auto scope = id_scope(declaration.section);
        const int64_t source_id = id_at(declaration.source, "id");
        int64_t id = source_id;
        if (replace) id = id_at(declaration.target, "id");
        else if (!preserve_ids && !allocate_id(high.at(scope), id)) {
            error = "This Visual Script has no available declaration IDs.";
            return false;
        }
        ids.scopes.at(scope).emplace(source_id, id);
        if (replace && declaration.section == StringName("events")) {
            const int64_t previous = id_at(custom_event_entry(source, source_id), "id");
            const int64_t existing = id_at(custom_event_entry(destination, id), "id");
            if (previous > 0 && existing > 0) {
                ids.nodes.emplace(previous, existing);
                ids.replaced_event_nodes.insert(existing);
            }
        }
    }
    for (const auto &[node, name] : plan.lifecycle_copies) {
        int64_t definition = 0;
        if (name.is_empty() || !allocate_id(high.at("definitions"), definition)) {
            error = "This Visual Script has no available Custom Event names or IDs.";
            return false;
        }
        ids.lifecycle_events.emplace(node, definition);
    }
    for (const char *section : {"nodes", "comments"}) {
        int64_t largest = maximum_id(array_at(destination, section));
        auto &map = StringName(section) == StringName("nodes") ? ids.nodes : ids.comments;
        for (const Dictionary &record : array_at(source, section)) {
            const int64_t previous = id_at(record, "id");
            if (map.count(previous)) continue;
            int64_t id = previous;
            if (!preserve_ids && !allocate_id(largest, id)) {
                error = "This Event Graph has no available node or comment IDs.";
                return false;
            }
            map.emplace(previous, id);
        }
    }
    return true;
}

/// Keep local IDs inside reusable bodies and rebind graph-wide references to the imported declarations.
bool remap_body_records(Dictionary body, const DocumentIds &ids, bool root) {
    const auto &enums = ids.scopes.at("enums");
    const auto &definitions = ids.scopes.at("definitions");
    for (const Dictionary &local : array_at(body, "locals")) {
        if (!remap_record_enums(local, enums)) return false;
    }
    if (!root && body.has("definition") && !remap_id(body, "definition", definitions)) return false;
    for (const Dictionary &node : array_at(body, "nodes")) {
        if (!remap_record_enums(node, enums)) return false;
        const StringName kind = name_at(node, "kind");
        if (kind == StringName("get_variable") || kind == StringName("set_variable")) {
            const bool local = name_at(node, "scope") == StringName("local");
            if ((!local || root) && !remap_id(node, "variable", ids.scopes.at(local ? "locals" : "variables"))) return false;
        }
        const bool boundary = kind == StringName("function_entry") || kind == StringName("macro_entry") ||
                kind == StringName("return");
        if (kind == StringName("call_function") || kind == StringName("call_macro") || kind == StringName("custom_event") ||
                kind == StringName("emit_event") || (boundary && node.has("definition"))) {
            if (!remap_id(node, "definition", definitions)) return false;
        }
    }
    return true;
}

/// Carry a native callback's data pins to a standalone Custom Event.
Array lifecycle_data_pins(const StringName &callback) {
    for (const Dictionary &record : lifecycle_events()) {
        if (name_at(record, "callback") == callback) return array_at(record, "inputs");
    }
    return {};
}

/// Align a pasted Event Graph group without changing its internal layout.
Vector2 root_paste_offset(const Dictionary &source, const Vector2 &anchor) {
    Vector2 top_left;
    bool first = true;
    for (const char *section : {"nodes", "comments"}) {
        for (const Dictionary &record : array_at(source, section)) {
            const Vector2 point = record.get("position", Vector2());
            top_left = first ? point : top_left.min(point);
            first = false;
        }
    }
    return first ? Vector2() : anchor - top_left;
}

/// Position and rebind the imported root body, converting duplicate callbacks without touching the old entries.
void remap_root_geometry(Dictionary body, const Dictionary &destination, const DocumentIds &ids,
        const DocumentImport &plan, const Vector2 &translation, Array &new_events, PackedInt64Array &created) {
    for (const Dictionary &source : array_at(body, "nodes")) {
        Dictionary node = source;
        const int64_t previous = id_at(node, "id");
        const int64_t id = ids.nodes.at(previous);
        node["id"] = id;
        if (ids.replaced_event_nodes.count(id)) {
            const Dictionary previous_entry = find_graph_record(array_at(destination, "nodes"), id);
            node["position"] = previous_entry.get("position", Vector2());
        } else if (translation != Vector2()) {
            node["position"] = Vector2(node.get("position", Vector2())) + translation;
        }
        created.append(id);
        const auto callback = ids.lifecycle_events.find(previous);
        if (callback != ids.lifecycle_events.end()) {
            Dictionary event;
            event["id"] = callback->second;
            event["name"] = plan.lifecycle_copies.at(previous);
            event["inputs"] = lifecycle_data_pins(lifecycle_callback(node));
            new_events.append(event);
            node["kind"] = "custom_event";
            node["definition"] = callback->second;
            node.erase("callback");
        }
    }
    for (const Dictionary &source : array_at(body, "connections")) {
        remap_id(source, "from", ids.nodes);
        remap_id(source, "to", ids.nodes);
    }
    for (const Dictionary &source : array_at(body, "comments")) {
        Dictionary comment = source;
        remap_id(comment, "id", ids.comments);
        if (translation != Vector2()) comment["position"] = Vector2(comment.get("position", Vector2())) + translation;
        if (comment.has("members")) {
            Array members;
            for (const Variant &id : array_at(comment, "members")) {
                const int64_t mapped = ids.nodes.at(int64_t(id));
                if (!ids.replaced_event_nodes.count(mapped)) members.append(mapped);
            }
            comment["members"] = members;
        }
    }
}

/// Replace a known stable declaration in place, or append it in source order.
void insert_declaration(Array &records, const Dictionary &record, bool replace) {
    if (replace) {
        const int64_t id = id_at(record, "id");
        for (int64_t index = 0; index < records.size(); ++index) {
            const Variant current = records[index];
            if (current.get_type() == Variant::DICTIONARY && id_at(current, "id") == id) {
                records[index] = record;
                return;
            }
        }
    }
    records.append(record);
}

} // namespace

String encode_graph_clipboard(const Dictionary &document, int64_t graph_id, uint64_t script_instance_id,
        const PackedInt64Array &selection, String &error) {
    error = String();
    GraphSelection copied = collect_selection(graph_body(document, graph_id), selection, false, error);
    if (copied.nodes.is_empty()) return {};
    Array nodes;
    for (const Dictionary &source : copied.nodes) {
        Dictionary node = source.duplicate();
        const Vector2 relative = Vector2(node.get("position", Vector2())) - copied.origin;
        if (!relative.is_finite()) {
            error = "The selected nodes are too far apart to copy.";
            return {};
        }
        node["position"] = relative;
        nodes.append(node);
    }
    Dictionary payload;
    payload["nodes"] = nodes;
    payload["connections"] = copied.wires;
    const PackedByteArray bytes = UtilityFunctions::var_to_bytes(payload);
    const String header = scoped_header(graph_id, script_instance_id);
    if (bytes.is_empty() || (bytes.size() + 2) / 3 * 4 + header.length() > max_text_length) {
        error = "The selected nodes are too large to copy.";
        return {};
    }
    return header + Marshalls::get_singleton()->raw_to_base64(bytes);
}

bool is_visual_script_document_clipboard(const String &clipboard_text) {
    return clipboard_text.begins_with(document_prefix);
}

Dictionary decode_visual_script_document(const String &clipboard_text, const StringName &base_class, String &error) {
    error = String();
    if (!is_visual_script_document_clipboard(clipboard_text)) return {};
    if (clipboard_text.length() > max_text_length) {
        error = "The Visual Script clipboard is too large.";
        return {};
    }
    const String lf_header = String(document_prefix) + "1\n";
    const String crlf_header = String(document_prefix) + "1\r\n";
    const int64_t header_length = clipboard_text.begins_with(lf_header) ? lf_header.length() :
            clipboard_text.begins_with(crlf_header) ? crlf_header.length() : 0;
    if (header_length == 0) {
        error = "The Visual Script clipboard uses an unsupported format version.";
        return {};
    }
    const String encoded = clipboard_text.substr(header_length);
    if (!is_base64(encoded)) {
        error = "The Visual Script clipboard has invalid data.";
        return {};
    }
    Marshalls *marshalls = Marshalls::get_singleton();
    const PackedByteArray bytes = marshalls->base64_to_raw(encoded);
    if (bytes.is_empty() || marshalls->raw_to_base64(bytes) != encoded) {
        error = "The Visual Script clipboard has invalid data.";
        return {};
    }
    const Variant decoded = UtilityFunctions::bytes_to_var(bytes);
    int64_t remaining = max_values;
    if (decoded.get_type() != Variant::DICTIONARY || !is_data(decoded, 0, remaining, true)) {
        error = "The Visual Script clipboard contains project assets or unsupported data.";
        return {};
    }
    if (UtilityFunctions::var_to_bytes(decoded) != bytes) {
        error = "The Visual Script clipboard has invalid or noncanonical graph data.";
        return {};
    }
    const Dictionary payload = decoded;
    const StringName base = name_at(payload, "base");
    const Variant source = payload.get("document", Variant());
    if (payload.size() != 2 || base.is_empty() || source.get_type() != Variant::DICTIONARY) {
        error = "The Visual Script clipboard is missing its base class or document.";
        return {};
    }
    if (!ClassDB::class_exists(base) || !ClassDB::is_parent_class(base, "Node") ||
            !ClassDB::is_parent_class(base_class, base)) {
        error = "This graph extends " + String(base) + ". Paste into a Visual Script whose base inherits it.";
        return {};
    }
    const Dictionary imported = source;
    if (!validate_portable_document(imported, base_class, error)) return {};
    for (const Dictionary &variable : array_at(imported, "variables")) {
        if (bool(variable.get("exported", false)) && !can_export_variable(base, name_at(variable, "name"), variable["pin"])) {
            error = "The clipboard contains a variable that cannot be exported by the source Visual Script.";
            return {};
        }
    }
    return imported;
}

Array visual_script_document_conflicts(const Dictionary &destination, const Dictionary &source,
        const StringName &base_class, String &error, const Callable &export_check) {
    error = String();
    if (id_at(destination, "version") != portable_graph_version ||
            !is_record_list(destination, "nodes", max_nodes, true) ||
            !is_record_list(destination, "connections", max_wires, true)) {
        error = "The destination Event Graph has an unsupported format.";
        return {};
    }
    return plan_document_import(destination, source, base_class, export_check).conflicts;
}

Dictionary merge_visual_script_document(const Dictionary &destination, const Dictionary &source, const StringName &base_class,
        const Vector2 &anchor, const PackedStringArray &choices, PackedInt64Array &created, String &error,
        int64_t *converted_lifecycle, const Callable &export_check) {
    error = String();
    created.clear();
    if (converted_lifecycle) *converted_lifecycle = 0;
    if (id_at(destination, "version") != portable_graph_version ||
            !is_record_list(destination, "nodes", max_nodes, true) ||
            !is_record_list(destination, "connections", max_wires, true) || !anchor.is_finite()) {
        error = "The destination Event Graph has an unsupported format or paste position.";
        return {};
    }
    const DocumentImport plan = plan_document_import(destination, source, base_class, export_check);
    if (plan.conflicts.size() != choices.size()) {
        error = "Choose an action for every conflicting declaration before pasting.";
        return {};
    }
    for (int64_t index = 0; index < choices.size(); ++index) {
        const Dictionary conflict = plan.conflicts[index];
        if ((choices[index] == "replace" && !conflict.get("replace_supported", false)) ||
                (choices[index] == "keep_both" && !conflict.get("copy_supported", false)) ||
                (choices[index] != "replace" && choices[index] != "keep_both")) {
            error = "Choose an available action for " + String(conflict.get("name", String())) + ".";
            return {};
        }
    }
    const bool preserve_ids = plan.conflicts.is_empty() && is_empty_destination(destination);
    DocumentIds ids;
    if (!allocate_document_ids(destination, source, plan, choices, preserve_ids, ids, error)) return {};
    const IdMap &enums = ids.scopes.at("enums");
    Dictionary next = destination.duplicate();
    Dictionary pasted = source.duplicate(true);
    if (!remap_body_records(pasted, ids, true)) {
        error = "The pasted Event Graph refers to a declaration that is missing from the clipboard.";
        return {};
    }
    std::map<StringName, Array> combined;
    for (const char *section : document_sections) {
        combined.emplace(section, array_at(destination, section).duplicate());
    }
    Array pasted_locals;
    std::vector<int64_t> pasted_bodies;
    for (const DeclarationImport &declaration : plan.declarations) {
        const bool replace = declaration.conflict_index >= 0 && choices[declaration.conflict_index] == "replace";
        if (replace) {
            const String problem = replacement_problem(declaration, enums);
            if (!problem.is_empty()) {
                error = "Cannot replace " + String(name_at(declaration.source, "name")) + ". " + problem +
                        " Choose Keep Both or review the related enum choices.";
                return {};
            }
        }
        Dictionary record = declaration.source.duplicate(true);
        if (!remap_id(record, "id", ids.scopes.at(id_scope(declaration.section))) || !remap_record_enums(record, enums)) {
            error = "A pasted declaration refers to an enum that is missing from the clipboard.";
            return {};
        }
        if (!replace && declaration.conflict_index >= 0) record["name"] = declaration.copy_name;
        if (declaration.section == StringName("locals")) pasted_locals.append(record);
        if (declaration.section == StringName("functions") || declaration.section == StringName("macros")) {
            if (!remap_body_records(record["graph"], ids, false)) {
                error = "A pasted callable refers to a declaration that is missing from the clipboard.";
                return {};
            }
            pasted_bodies.push_back(id_at(record, "id"));
        }
        insert_declaration(combined.at(declaration.section), record, replace);
    }
    if (pasted.has("locals")) pasted["locals"] = pasted_locals;
    PackedInt64Array new_nodes;
    remap_root_geometry(pasted, destination, ids, plan, preserve_ids ? Vector2() : root_paste_offset(source, anchor),
            combined.at("events"), new_nodes);
    for (const auto &[section, entries] : combined) {
        if (entries.size() > max_nodes) {
            error = "The pasted declarations would exceed this Visual Script's declaration limits.";
            return {};
        }
        if (!entries.is_empty()) next[section] = entries;
    }
    if (!validate_document_body(next, pasted, StringName(), base_class, error)) return {};
    for (const int64_t id : pasted_bodies) {
        const StringName entry_kind = find_graph_record(array_at(next, "macros"), id).is_empty() ? "function_entry" : "macro_entry";
        if (!validate_document_body(next, graph_body(next, id), entry_kind, base_class, error)) return {};
    }

    for (const char *section : {"nodes", "connections", "comments"}) {
        Array entries;
        for (const Variant &value : array_at(destination, section)) {
            if (value.get_type() == Variant::DICTIONARY) {
                const Dictionary record = value;
                const StringName list(section);
                if ((list == StringName("nodes") && ids.replaced_event_nodes.count(id_at(record, "id"))) ||
                        (list == StringName("connections") && (ids.replaced_event_nodes.count(id_at(record, "from")) ||
                         ids.replaced_event_nodes.count(id_at(record, "to"))))) continue;
            }
            entries.append(value);
        }
        entries.append_array(array_at(pasted, section));
        if (entries.size() > (StringName(section) == StringName("connections") ? max_wires : max_nodes)) {
            error = "The pasted nodes, connections, or comments would exceed this Event Graph's limits.";
            return {};
        }
        if (StringName(section) != StringName("comments") || !entries.is_empty()) next[section] = entries;
    }
    created = new_nodes;
    if (converted_lifecycle) *converted_lifecycle = static_cast<int64_t>(plan.lifecycle_copies.size());
    return preserve_ids ? source : next;
}

Dictionary paste_graph_clipboard(const Dictionary &document, int64_t graph_id, uint64_t script_instance_id,
        const String &clipboard_text, const Vector2 &anchor, PackedInt64Array &created, String &error,
        const StringName &base_class) {
    error = String();
    created.clear();
    if (!clipboard_text.begins_with(clipboard_prefix)) return {};
    const String header = scoped_header(graph_id, script_instance_id);
    if (!clipboard_text.begins_with(header)) {
        error = "Paste these nodes into the same open script and graph they came from.";
        return {};
    }
    if (clipboard_text.length() > max_text_length || !anchor.is_finite()) {
        error = "The graph clipboard is too large or its paste position is invalid.";
        return {};
    }
    const String encoded = clipboard_text.substr(header.length());
    if (!is_base64(encoded)) {
        error = "The graph clipboard has invalid data.";
        return {};
    }
    Marshalls *marshalls = Marshalls::get_singleton();
    const PackedByteArray bytes = marshalls->base64_to_raw(encoded);
    if (bytes.is_empty() || marshalls->raw_to_base64(bytes) != encoded) {
        error = "The graph clipboard has invalid data.";
        return {};
    }
    const Variant decoded = UtilityFunctions::bytes_to_var(bytes);
    if (decoded.get_type() != Variant::DICTIONARY || bytes.decode_var_size(0, false) != bytes.size()) {
        error = "The graph clipboard could not be read.";
        return {};
    }
    int64_t remaining = max_values;
    if (!is_data(decoded, 0, remaining)) {
        error = "The graph clipboard contains unsupported values.";
        return {};
    }
    const Dictionary payload = decoded;
    GraphSelection selection;
    selection.nodes = array_at(payload, "nodes");
    selection.wires = array_at(payload, "connections");
    if (payload.get("connections", Variant()).get_type() != Variant::ARRAY) {
        error = "The graph clipboard is missing its connection list.";
        return {};
    }
    return insert_selection(document, graph_id, selection, anchor, base_class, created, error);
}

Dictionary cut_graph_selection(const Dictionary &document, int64_t graph_id, const PackedInt64Array &selection, String &error) {
    error = String();
    Dictionary graph = graph_body(document, graph_id).duplicate();
    const GraphSelection copied = collect_selection(graph, selection, false, error);
    if (copied.nodes.is_empty()) return {};
    const std::set<int64_t> removed(selection.begin(), selection.end());
    Array nodes;
    for (const Variant &value : array_at(graph, "nodes")) {
        if (value.get_type() != Variant::DICTIONARY || !removed.count(id_at(value, "id"))) nodes.append(value);
    }
    Array wires;
    for (const Variant &value : array_at(graph, "connections")) {
        if (value.get_type() != Variant::DICTIONARY || (!removed.count(id_at(value, "from")) && !removed.count(id_at(value, "to")))) {
            wires.append(value);
        }
    }
    graph["nodes"] = nodes;
    graph["connections"] = wires;
    const Dictionary pruned = prune_graph_comment_members(graph);
    return replace_graph_body(document, graph_id, pruned.is_empty() ? graph : pruned);
}

Dictionary duplicate_graph_selection(const Dictionary &document, int64_t graph_id, const PackedInt64Array &selection,
        const Vector2 &offset, bool keep_external_data_inputs, PackedInt64Array &created, String &error,
        const StringName &base_class) {
    error = String();
    created.clear();
    const GraphSelection copied = collect_selection(graph_body(document, graph_id), selection, keep_external_data_inputs, error);
    if (copied.nodes.is_empty()) return {};
    return insert_selection(document, graph_id, copied, offset, base_class, created, error);
}

} // namespace crimson::visual_scripting

#endif
