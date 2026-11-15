// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_collections.hpp"
#include "modules/visual_scripting/visual_script_cast_input.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_value_nodes.hpp"

#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Limit the number of saved cases to keep editor rebuilds and graph compilation bounded.
constexpr int64_t max_enum_switch_cases = 1024;

/// Read either serialized or interned names without coercing other values.
String text_at(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME ? String(value) : String();
}

/// Read a serialized array without coercing other Variants.
Array array_at(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Find an identifier without coercing other numeric types.
int64_t id_at(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::INT ? static_cast<int64_t>(value) : 0;
}

/// Produce the same pin shape for built-in and authored operations.
Dictionary pin(const StringName &id, const String &label, Variant::Type type = Variant::NIL, const StringName &object_class = {}) {
    Dictionary result;
    result["id"] = id;
    result["label"] = label;
    result["type"] = static_cast<int64_t>(type);
    if (!object_class.is_empty()) {
        result["class"] = object_class;
    }
    return result;
}

/// Copy the attached Script's nominal identity without changing its native receiver type.
Dictionary script_facing_pin(Dictionary descriptor, const Dictionary &source) {
    const String path = text_at(source, "script_path");
    if (!path.is_empty()) {
        descriptor["script_path"] = path;
        descriptor["script_name"] = text_at(source, "script_name");
    }
    return descriptor;
}

/// Create an execution port with a stable name.
Dictionary exec(const StringName &id, const String &label) {
    Dictionary result = pin(id, label);
    result["exec"] = true;
    return result;
}

/// Attach a default to a data port.
Dictionary defaulted(Dictionary descriptor, const Variant &value) {
    descriptor["default"] = value;
    return descriptor;
}

/// Read an authored pin type and give it a node-specific identity.
Dictionary named_pin(const Variant &value, const StringName &id, const String &label) {
    Dictionary descriptor = value.get_type() == Variant::DICTIONARY ? Dictionary(value).duplicate() : pin(id, label);
    descriptor["id"] = id;
    descriptor["label"] = label;
    descriptor.erase("exec");
    return descriptor;
}

/// Build an action's default single execution lane.
Array action_pins(bool output) {
    return output ? Array::make(exec("then", "Then")) : Array::make(exec("in", "In"));
}

/// Add the default execution pin if a declaration only has data ports.
Array tunnel_pins(const Array &authored, bool output) {
    for (const Variant &value : authored) {
        if (value.get_type() == Variant::DICTIONARY && Dictionary(value).get("exec", false)) {
            return authored;
        }
    }
    Array result = action_pins(output);
    result.append_array(authored);
    return result;
}

/// Return a numbered pin using the shared wire naming convention.
String numbered(const char *prefix, int64_t index) {
    return String(prefix) + String::num_int64(index);
}

/// Build a lifecycle descriptor consumed by both the picker and the runtime.
Dictionary lifecycle(const char *callback, const char *title, const Array &inputs = {}) {
    Dictionary record;
    record["callback"] = StringName(callback);
    record["title"] = title;
    record["inputs"] = inputs;
    return record;
}

/// Create a serialized edge from four endpoints.
Dictionary wire(int64_t from, const StringName &from_pin, int64_t to, const StringName &to_pin) {
    Dictionary result;
    result["from"] = from;
    result["from_pin"] = from_pin;
    result["to"] = to;
    result["to_pin"] = to_pin;
    return result;
}

/// Find one stable port by ID on a node.
Dictionary lookup_pin(const Dictionary &root, const Dictionary &body, const Dictionary &node, bool output,
        const StringName &id, const StringName &base) {
    for (const Variant &value : graph_pins(root, body, node, output, base)) {
        if (value.get_type() == Variant::DICTIONARY && StringName(Dictionary(value).get("id", StringName())) == id) {
            return value;
        }
    }
    return {};
}

/// Return all declared pins that travel through an event.
Array event_pins(const Array &inputs) {
    Array result = action_pins(true);
    result.append_array(inputs);
    return result;
}

/// Resolve a function or macro declaration regardless of its kind.
Dictionary definition_at(const Dictionary &root, int64_t id) {
    const Dictionary function = find_graph_record(array_at(root, "functions"), id);
    return function.is_empty() ? find_graph_record(array_at(root, "macros"), id) : function;
}

/// Find the producer of one data input using stable saved wire addresses.
Dictionary upstream_connection(const Dictionary &body, int64_t node, const StringName &input) {
    if (node <= 0) {
        return {};
    }
    for (const Variant &entry : array_at(body, "connections")) {
        if (entry.get_type() != Variant::DICTIONARY) continue;
        const Dictionary connection = entry;
        if (id_at(connection, "to") == node && StringName(connection.get("to_pin", StringName())) == input) {
            return connection;
        }
    }
    return {};
}

/// Reconstruct a native terminal present on a saved typed collection literal.
Dictionary terminal_pin(int64_t type, const StringName &object_class) {
    return pin("value", "Value", static_cast<Variant::Type>(type), object_class);
}

/// Keep a typed literal's native collection schema when tracing it through the graph.
Dictionary literal_pin(const Variant &literal) {
    if (literal.get_type() == Variant::DICTIONARY) {
        const Dictionary value = literal;
        Dictionary result = pin("array", "Dictionary", Variant::DICTIONARY);
        if (value.is_typed_key()) result["key"] = terminal_pin(value.get_typed_key_builtin(), value.get_typed_key_class_name());
        if (value.is_typed_value()) result["value"] = terminal_pin(value.get_typed_value_builtin(), value.get_typed_value_class_name());
        return result;
    }
    if (literal.get_type() == Variant::ARRAY) {
        const Array value = literal;
        Dictionary result = pin("array", "Array", Variant::ARRAY);
        if (value.is_typed()) result["element"] = terminal_pin(value.get_typed_builtin(), value.get_typed_class_name());
        return result;
    }
    return collection_literal_type(literal);
}

/// Resolve a loop's dependent terminal from its input type.
Dictionary loop_output_pin(const Dictionary &collection, const StringName &output) {
    const auto type = id_at(collection, "type");
    if (output == StringName("key") && type != Variant::DICTIONARY) {
        return {};
    }
    const char *field = output == StringName("key") ? "key" : type == Variant::DICTIONARY ? "value" : "element";
    Dictionary result = named_pin(collection.get(field, Variant()), output, output == StringName("key") ? "Key" : "Item");
    result["iteration_output"] = true;
    return result;
}

/// Mark a data reroute whose displayed type follows its actual producer.
bool follows_loop_output(const Dictionary &node) {
    return text_at(node, "kind") == "reroute" && text_at(node, "mode") != "exec" &&
            Dictionary(node.get("pin", Dictionary())).get("iteration_output", false);
}

/// Read a literal even if a connection currently overrides it.
Variant graph_input_literal(const Dictionary &node, const StringName &input) {
    const Variant stored = node.get("values", Variant());
    const Variant fallback = input == StringName("value") && text_at(node, "kind") == "literal" ?
            node.get("value", Variant()) : Variant();
    return stored.get_type() == Variant::DICTIONARY ? Dictionary(stored).get(input, fallback) : fallback;
}

/// Resolve only collections for disconnected For Each node contracts.
Dictionary loop_literal_pin(const Dictionary &loop) {
    const Variant literal = graph_input_literal(loop, "array");
    return literal.get_type() == Variant::ARRAY || literal.get_type() == Variant::DICTIONARY ?
            literal_pin(literal) : Dictionary();
}

/// Follow native collection contracts through arbitrary-length aliases and terminal projections.
Dictionary trace_collection_source(const Dictionary &root, const Dictionary &body, Dictionary connection,
        const StringName &base) {
    const Array nodes = array_at(body, "nodes");
    std::set<std::pair<int64_t, StringName>> seen;
    std::vector<StringName> projections;
    Dictionary result;
    while (!connection.is_empty()) {
        const int64_t from = id_at(connection, "from");
        const StringName output = connection.get("from_pin", StringName());
        if (from <= 0 || !seen.emplace(from, output).second) return {};
        const Dictionary source = find_graph_record(nodes, from);
        const String kind = text_at(source, "kind");
        if (kind == "reroute" && output == StringName("value") && text_at(source, "mode") != "exec") {
            connection = upstream_connection(body, from, "value");
            if (connection.is_empty() && !follows_loop_output(source)) {
                result = named_pin(source.get("pin", Variant()), "value", "Value");
                if (result.get("iteration_collection", false)) result = pin("value", "Value");
            }
        } else if (kind == "foreach" && (output == StringName("item") || output == StringName("key"))) {
            projections.push_back(output);
            connection = upstream_connection(body, from, "array");
            if (connection.is_empty()) result = loop_literal_pin(source);
        } else if (const auto operation = collection_operation(kind);
                collection_operation_edits(operation) && output == StringName("result")) {
            connection = upstream_connection(body, from, collection_source_input(operation));
            if (connection.is_empty()) {
                for (const Dictionary &descriptor : collection_operation_pins(operation, source, true, {})) {
                    if (StringName(descriptor["id"]) == output) result = descriptor;
                }
            }
        } else {
            if (!source.is_empty()) result = lookup_pin(root, body, source, true, output, base);
            connection = {};
        }
    }
    for (auto projection = projections.rbegin(); projection != projections.rend(); ++projection) {
        result = loop_output_pin(result, *projection);
    }
    return result;
}

/// Derive a loop's native collection from its wire or authored fallback.
Dictionary loop_collection_pin(const Dictionary &root, const Dictionary &body, const Dictionary &node, const StringName &base) {
    const Dictionary connection = upstream_connection(body, id_at(node, "id"), "array");
    return connection.is_empty() ? loop_literal_pin(node) : trace_collection_source(root, body, connection, base);
}

/// Copy a presentation type without importing another pin's wiring or authoring rules.
Dictionary type_hint(const Dictionary &source) {
    if (source.is_empty()) return {};
    Dictionary result = collection_terminal_type(source);
    for (const char *field : {"element", "key", "value"}) {
        const Variant value = source.get(field, Variant());
        if (value.get_type() == Variant::DICTIONARY) result[field] = value;
    }
    if (source.get("nullable", false)) result["nullable"] = true;
    return result;
}

/// Add an observer hint to a generic pin or an unbound lane of a known collection.
Dictionary with_type_hint(const Dictionary &descriptor, const Dictionary &source) {
    const int64_t source_type = id_at(source, "type");
    const int64_t target_type = id_at(descriptor, "type");
    if (source_type == Variant::NIL) return descriptor;
    if (target_type == Variant::NIL) return descriptor.merged(type_hint(source), true);
    if (source_type != target_type || (target_type != Variant::ARRAY && target_type != Variant::DICTIONARY)) return descriptor;
    return descriptor.merged(type_hint(source), false);
}

/// Resolve dependent types through one indexed graph snapshot.
class GraphTypeResolver {
public:
    /// Keep references to one graph snapshot and its already-resolved native contracts.
    GraphTypeResolver(const Dictionary &graph, const std::map<int64_t, Dictionary> &nodes,
            const std::map<int64_t, GraphPinLayout> &layouts) : graph(graph), nodes(nodes), layouts(layouts) {}

    /// Describe a rendered pin, using the same structural layout as its contract.
    Dictionary presentation(const Dictionary &node, const Dictionary &descriptor, bool output);

private:
    /// An authored endpoint is unique only within its graph.
    using Endpoint = std::pair<int64_t, StringName>;
    /// A single Event, Function or Macro body.
    const Dictionary &graph;
    /// Source record lookup for upstream traversal.
    const std::map<int64_t, Dictionary> &nodes;
    /// Complete native slot membership for each node.
    const std::map<int64_t, GraphPinLayout> &layouts;
    /// Skip indexing for nodes whose schema needs no upstream information.
    bool indexed = false;
    /// First saved wire targeting each input.
    std::map<Endpoint, Endpoint> incoming;
    /// Memoize complete output hints in this snapshot.
    std::map<Endpoint, Dictionary> resolved;
    /// Ignore cyclic or excessively deep inferred paths.
    std::set<Endpoint> resolving;
    /// Count dependent node evaluations, excluding iterative Reroute traversal.
    size_t trace_depth = 0;
    /// Avoid memoizing any result that depends on a truncated traversal.
    bool incomplete = false;

    /// Build node and wire indexes lazily.
    void index_graph();
    /// Check whether the input has an upstream edge, including a producer with unknown type.
    bool wired(const Dictionary &node, const StringName &name);
    /// Return one input's producer type or literal type when no wire supplies it.
    Dictionary input(const Dictionary &node, const StringName &name);
    /// Read the native descriptor supplying this output address.
    Dictionary contract_output(const Endpoint &endpoint) const;
    /// Read an upstream output without changing the node's authored pins.
    Dictionary output(const Endpoint &endpoint);
    /// Display homogeneous terminals on existing, natively untyped constructors.
    Dictionary constructor_output(const Dictionary &node, const Dictionary &descriptor, CollectionOperation operation);
    /// Derive a hint for a dependent output, keeping unknown paths generic.
    Dictionary display_output(const Dictionary &node, const Dictionary &descriptor);
};

void GraphTypeResolver::index_graph() {
    if (indexed) return;
    indexed = true;
    for (const Variant &entry : array_at(graph, "connections")) {
        if (entry.get_type() != Variant::DICTIONARY) continue;
        const Dictionary edge = entry;
        incoming.try_emplace(Endpoint{id_at(edge, "to"), edge.get("to_pin", StringName())},
                Endpoint{id_at(edge, "from"), edge.get("from_pin", StringName())});
    }
}

bool GraphTypeResolver::wired(const Dictionary &node, const StringName &name) {
    const int64_t id = id_at(node, "id");
    if (id <= 0) return false;
    index_graph();
    return incoming.count({id, name}) != 0;
}

Dictionary GraphTypeResolver::input(const Dictionary &node, const StringName &name) {
    const int64_t id = id_at(node, "id");
    if (id > 0) {
        index_graph();
        if (auto found = incoming.find({id, name}); found != incoming.end()) return output(found->second);
    }
    return literal_pin(graph_input_literal(node, name));
}

Dictionary GraphTypeResolver::contract_output(const Endpoint &endpoint) const {
    const auto layout = layouts.find(endpoint.first);
    if (layout != layouts.end()) {
        for (const Dictionary &candidate : layout->second.contract_outputs) {
            if (StringName(candidate.get("id", StringName())) == endpoint.second) return candidate;
        }
    }
    return {};
}

Dictionary GraphTypeResolver::output(const Endpoint &endpoint) {
    /// Bound recursion on branching and compound dependencies.
    constexpr size_t max_trace_depth = 128;
    if (const auto found = resolved.find(endpoint); found != resolved.end()) return found->second;
    if (trace_depth >= max_trace_depth) {
        incomplete = true;
        return {};
    }
    ++trace_depth;
    Dictionary result;
    Endpoint current = endpoint;
    std::vector<std::pair<Endpoint, Dictionary>> reroutes;
    index_graph();
    while (current.first > 0 && !current.second.is_empty()) {
        if (const auto found = resolved.find(current); found != resolved.end()) {
            result = found->second;
            break;
        }
        if (!resolving.insert(current).second) {
            incomplete = true;
            break;
        }
        const auto origin = nodes.find(current.first);
        const Dictionary descriptor = contract_output(current);
        if (origin != nodes.end() && !descriptor.is_empty()) {
            const auto upstream = incoming.find(current);
            if (upstream != incoming.end() && current.second == StringName("value") && !bool(descriptor.get("exec", false)) &&
                    text_at(origin->second, "kind") == "reroute") {
                reroutes.emplace_back(current, descriptor);
                current = upstream->second;
                continue;
            }
            result = display_output(origin->second, descriptor);
        }
        resolving.erase(current);
        if (!incomplete) resolved.emplace(current, result);
        break;
    }
    for (auto hop = reroutes.rbegin(); hop != reroutes.rend(); ++hop) {
        result = with_type_hint(hop->second, result);
        resolving.erase(hop->first);
        if (!incomplete) resolved.emplace(hop->first, result);
    }
    --trace_depth;
    return result;
}

Dictionary GraphTypeResolver::constructor_output(const Dictionary &node, const Dictionary &descriptor, CollectionOperation operation) {
    const bool array = operation == CollectionOperation::MAKE_ARRAY;
    const Array inputs = collection_operation_pins(operation, node, false, {});
    const int64_t lanes = array ? 1 : 2;
    const std::array<const char *, 2> fields = {array ? "element" : "key", "value"};
    Dictionary result = descriptor.duplicate();
    for (int64_t lane = 0; lane < lanes; ++lane) {
        if (descriptor.has(fields[lane])) continue;
        Dictionary known;
        bool nullable = false;
        for (int64_t index = lane; index < inputs.size(); index += lanes) {
            const Dictionary slot = inputs[index];
            const StringName name = slot["id"];
            const Dictionary source = input(node, name);
            if (source.is_empty() && !wired(node, name)) {
                if (array) nullable = true;
                if (!array && lane == 1 && !node.has("dictionary_types")) {
                    const Dictionary key_slot = inputs[index - 1];
                    const StringName key = key_slot["id"];
                    nullable = nullable || wired(node, key) || !literal_pin(graph_input_literal(node, key)).is_empty();
                }
                continue;
            }
            Dictionary hint = type_hint(source);
            nullable = nullable || bool(hint.get("nullable", false));
            hint.erase("nullable");
            if (id_at(hint, "type") == Variant::NIL || (!known.is_empty() && known != hint)) {
                known.clear();
                break;
            }
            known = hint;
        }
        if (!known.is_empty()) {
            if (nullable) known["nullable"] = true;
            result[fields[lane]] = known;
        }
    }
    return result;
}

Dictionary GraphTypeResolver::display_output(const Dictionary &node, const Dictionary &descriptor) {
    if (descriptor.get("exec", false)) return descriptor;
    const String kind = text_at(node, "kind");
    const StringName name = descriptor.get("id", StringName());
    if (const auto operation = collection_operation(kind); operation != CollectionOperation::NONE) {
        if (collection_source_input(operation).is_empty()) return constructor_output(node, descriptor, operation);
        if (name == StringName("succeeded") || name == StringName("found")) return descriptor;
        const StringName collection_input = collection_source_input(operation);
        const Dictionary source = input(node, collection_input);
        if (!collection_operation_edits(operation)) {
            const Dictionary terminal = source.get(operation == CollectionOperation::GET_ARRAY_ITEM ? "element" : "value",
                    Dictionary());
            Dictionary result = with_type_hint(descriptor, terminal).duplicate();
            if (id_at(result, "type") != Variant::NIL) result["nullable"] = true;
            return result;
        }
        Dictionary shape = source.duplicate();
        const bool array = collection_type(operation) == Variant::ARRAY;
        if (operation == CollectionOperation::SET_ARRAY_ITEM || operation == CollectionOperation::APPEND_ARRAY_ITEM ||
                operation == CollectionOperation::SET_DICTIONARY_VALUE) {
            const std::array<const char *, 2> lanes = {array ? "element" : "key", "value"};
            for (const char *lane : lanes) {
                if (!shape.has(lane) || id_at(shape.get(lane, Dictionary()), "type") == Variant::NIL ||
                        id_at(descriptor.get(lane, Dictionary()), "type") != Variant::NIL) continue;
                Dictionary replacement = type_hint(input(node, array ? "value" : lane));
                Dictionary current = Dictionary(shape[lane]).duplicate();
                const bool nullable = bool(current.get("nullable", false)) || bool(replacement.get("nullable", false));
                current.erase("nullable");
                replacement.erase("nullable");
                if (current != replacement) {
                    shape.erase(lane);
                } else if (nullable) {
                    current["nullable"] = true;
                    shape[lane] = current;
                }
            }
        }
        return with_type_hint(descriptor, shape);
    }
    if (kind == "foreach" && (name == StringName("key") || name == StringName("item"))) {
        return with_type_hint(descriptor, loop_output_pin(input(node, "array"), name));
    }
    if (kind == "reroute" || kind == "literal") return with_type_hint(descriptor, input(node, "value"));
    if (kind == "call_method" && name == StringName("updated_target")) {
        return with_type_hint(descriptor, input(node, "target"));
    }
    if (kind == "select" && !node.has("pin")) {
        const Dictionary first = type_hint(input(node, "true"));
        const Dictionary second = type_hint(input(node, "false"));
        if (id_at(first, "type") != Variant::NIL && first == second) return with_type_hint(descriptor, first);
    }
    return descriptor;
}

Dictionary GraphTypeResolver::presentation(const Dictionary &node, const Dictionary &descriptor, bool is_output) {
    if (descriptor.get("exec", false)) return descriptor;
    incomplete = false;
    const StringName name = descriptor.get("id", StringName());
    if (is_output) {
        const Dictionary hint = output({id_at(node, "id"), name});
        return incomplete || hint.is_empty() ? descriptor : hint;
    }
    if (const auto operation = collection_operation(text_at(node, "kind"));
            operation != CollectionOperation::NONE && !collection_source_input(operation).is_empty() &&
            (name == StringName("key") || name == StringName("value")) && !wired(node, name)) {
        const Dictionary source = input(node, collection_source_input(operation));
        const StringName lane = collection_type(operation) == Variant::ARRAY ? StringName("element") : name;
        return incomplete ? descriptor : with_type_hint(descriptor, source.get(lane, Dictionary()));
    }
    const Dictionary source = input(node, name);
    return incomplete ? descriptor : with_type_hint(descriptor, source);
}

/// Build a graph boundary node for an extracted selection.
Dictionary boundary_node(int64_t id, const char *kind, int64_t definition, const Vector2 &position) {
    Dictionary result;
    result["id"] = id;
    result["kind"] = kind;
    result["definition"] = definition;
    result["position"] = position;
    return result;
}

/// Local references and entry/exit nodes cannot retain their meaning in an extracted graph.
bool is_extraction_boundary(const Dictionary &node) {
    const String kind = text_at(node, "kind");
    return kind == "ready" || kind == "lifecycle" || kind == "native_signal" || kind == "custom_event" ||
            kind == "function_entry" || kind == "macro_entry" || kind == "return" ||
            ((kind == "get_variable" || kind == "set_variable") && text_at(node, "scope") == "local");
}

/// A producer may travel with a selection if it supplies data without consuming or emitting execution.
bool is_parameter_source(const Dictionary &root, const Dictionary &graph, const Dictionary &node,
        const StringName &base_class) {
    if (is_extraction_boundary(node)) {
        return false;
    }
    const Array outputs = graph_pins(root, graph, node, true, base_class);
    const Array inputs = graph_pins(root, graph, node, false, base_class);
    for (const Array &pins : {inputs, outputs}) {
        for (const Dictionary &pin : pins) {
            if (pin.get("exec", false)) {
                return false;
            }
        }
    }
    return !outputs.is_empty();
}

/// Move upstream producers once all their consumers are inside. Shared sources remain at the call site.
std::set<int64_t> include_exclusive_sources(const Dictionary &root, const Dictionary &graph,
        const StringName &base_class, const std::set<int64_t> &selection) {
    std::map<int64_t, Dictionary> nodes_by_id;
    for (const Variant &item : array_at(graph, "nodes")) {
        if (item.get_type() == Variant::DICTIONARY) {
            const Dictionary node = item;
            nodes_by_id.emplace(id_at(node, "id"), node);
        }
    }
    std::map<int64_t, std::vector<int64_t>> incoming;
    std::map<int64_t, size_t> outside_consumers;
    for (const Variant &item : array_at(graph, "connections")) {
        if (item.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary edge = item;
        const int64_t source = id_at(edge, "from");
        incoming[id_at(edge, "to")].push_back(source);
        ++outside_consumers[source];
    }
    std::set<int64_t> included = selection;
    std::vector<int64_t> pending(selection.begin(), selection.end());
    while (!pending.empty()) {
        const int64_t target = pending.back();
        pending.pop_back();
        const auto upstream = incoming.find(target);
        if (upstream == incoming.end()) {
            continue;
        }
        for (const int64_t source : upstream->second) {
            if (included.count(source) != 0 || --outside_consumers.at(source) != 0) {
                continue;
            }
            const auto record = nodes_by_id.find(source);
            if (record != nodes_by_id.end() && is_parameter_source(root, graph, record->second, base_class)) {
                included.insert(source);
                pending.push_back(source);
            }
        }
    }
    return included;
}

/// Detect a path from a replacement call's output back to one of its own inputs.
bool graph_path_returns_to(int64_t call_id, const std::map<int64_t, std::vector<int64_t>> &paths) {
    const auto first = paths.find(call_id);
    if (first == paths.end()) {
        return false;
    }
    std::set<int64_t> checked;
    std::vector<int64_t> pending = first->second;
    while (!pending.empty()) {
        const int64_t destination = pending.back();
        pending.pop_back();
        if (destination == call_id) {
            return true;
        }
        if (checked.insert(destination).second) {
            const auto next = paths.find(destination);
            if (next != paths.end()) {
                pending.insert(pending.end(), next->second.begin(), next->second.end());
            }
        }
    }
    return false;
}

} // namespace

Dictionary infer_legacy_dictionary_types(const Dictionary &root, const Dictionary &graph,
        const Dictionary &node, const StringName &base) {
    const auto input_type = [&](const StringName &name) -> Dictionary {
        const Dictionary connection = upstream_connection(graph, id_at(node, "id"), name);
        return connection.is_empty() ? collection_literal_type(graph_input_literal(node, name)) :
                collection_terminal_type(trace_collection_source(root, graph, connection, base));
    };
    Dictionary result;
    const Array inputs = collection_operation_pins(CollectionOperation::MAKE_DICTIONARY, node, false, {});
    const Variant saved_values = node.get("values", Variant());
    const Dictionary values = saved_values.get_type() == Variant::DICTIONARY ? Dictionary(saved_values) : Dictionary();
    for (int64_t lane = 0; lane < 2; ++lane) {
        const bool key = lane == 0;
        Dictionary common;
        for (int64_t index = lane; index < inputs.size(); index += 2) {
            const Dictionary input = inputs[index];
            const StringName name = input["id"];
            const Dictionary active = input_type(name);
            const Dictionary literal = collection_literal_type(graph_input_literal(node, name));
            const Dictionary preceding_key = key ? Dictionary() : Dictionary(inputs[index - 1]);
            const bool legacy_null_value = !key && ((literal.is_empty() && values.has(name)) ||
                    (active.is_empty() && !input_type(preceding_key["id"]).is_empty()));
            if (legacy_null_value || (!active.is_empty() && (id_at(active, "type") == Variant::NIL ||
                    (!literal.is_empty() && active != literal) || (!common.is_empty() && common != active) ||
                    (key && !collection_key_type_allowed(active))))) {
                common = collection_terminal_type({});
                break;
            }
            if (!active.is_empty()) common = active;
        }
        if (!common.is_empty()) result[key ? "key" : "value"] = common;
    }
    return result;
}

String custom_script_title(const Dictionary &node) {
    const String title = text_at(node, "title").strip_edges();
    return title.is_empty() ? String("Custom Script") : title;
}

Dictionary find_graph_record(const Array &records, int64_t id) {
    if (id <= 0) {
        return {};
    }
    for (const Variant &entry : records) {
        if (entry.get_type() == Variant::DICTIONARY && id_at(entry, "id") == id) {
            return entry;
        }
    }
    return {};
}

int64_t next_definition_id(const Dictionary &root) {
    int64_t largest = 0;
    for (const char *key : {"functions", "macros", "events"}) {
        for (const Variant &entry : array_at(root, key)) {
            if (entry.get_type() == Variant::DICTIONARY) {
                largest = std::max(largest, id_at(entry, "id"));
            }
        }
    }
    return largest < std::numeric_limits<int64_t>::max() ? largest + 1 : 0;
}

Dictionary graph_body(const Dictionary &root, int64_t graph_id) {
    if (graph_id == 0) {
        return root;
    }
    const Variant body = definition_at(root, graph_id).get("graph", Variant());
    if (body.get_type() != Variant::DICTIONARY) {
        return {};
    }
    Dictionary active = Dictionary(body).duplicate();
    active["definition"] = graph_id;
    return active;
}

Dictionary replace_graph_body(const Dictionary &root, int64_t graph_id, const Dictionary &body) {
    if (graph_id == 0) {
        return body;
    }
    Dictionary changed = root.duplicate(true);
    for (const char *key : {"functions", "macros"}) {
        Array definitions = array_at(changed, key);
        for (int64_t index = 0; index < definitions.size(); ++index) {
            if (definitions[index].get_type() != Variant::DICTIONARY || id_at(definitions[index], "id") != graph_id) {
                continue;
            }
            Dictionary definition = definitions[index];
            Dictionary saved = body.duplicate(true);
            saved.erase("definition");
            definition["graph"] = saved;
            return changed;
        }
    }
    return {};
}

Array lifecycle_events() {
    const Array delta = Array::make(pin("delta", "Delta", Variant::FLOAT));
    const Array event = Array::make(pin("event", "Input Event", Variant::OBJECT, "InputEvent"));
    return Array::make(
            lifecycle("_enter_tree", "On Enter Tree"), lifecycle("_ready", "On Ready"),
            lifecycle("_exit_tree", "On Exit Tree"), lifecycle("_process", "On Process", delta),
            lifecycle("_physics_process", "On Physics Process", delta), lifecycle("_input", "On Input", event),
            lifecycle("_shortcut_input", "On Shortcut Input", event),
            lifecycle("_unhandled_input", "On Unhandled Input", event),
            lifecycle("_unhandled_key_input", "On Unhandled Key Input", event));
}

Array graph_pins(const Dictionary &root, const Dictionary &graph, const Dictionary &node, bool output, const StringName &base_class) {
    const String kind = node.get("kind", String());
    if (const auto operation = collection_operation(StringName(kind)); operation != CollectionOperation::NONE) {
        Dictionary source;
        if (output && collection_operation_edits(operation)) {
            source = trace_collection_source(root, graph, upstream_connection(graph, id_at(node, "id"), collection_source_input(operation)),
                    base_class);
        }
        return collection_operation_pins(operation, node, output, source);
    }
    if (kind == "preload_resource") {
        return output ? Array::make(preload_output_pin(node)) : Array();
    }
    if (kind == "load_resource") {
        const StringName resource_class = node.get("class", StringName("Resource"));
        return output ? Array::make(exec("success", "Success"), exec("failure", "Failure"),
                                pin("resource", "Resource", Variant::OBJECT, resource_class)) :
                        Array::make(exec("in", "In"), defaulted(pin("path", "Path", Variant::STRING), String()));
    }
    if (kind == "create_script_instance") {
        if (output) {
            const StringName base = node.get("class", StringName("Object"));
            return Array::make(exec("then", "Then"), script_facing_pin(pin("instance", "Instance", Variant::OBJECT, base), node));
        }
        Array inputs = Array::make(exec("in", "In"));
        inputs.append_array(array_at(node, "inputs"));
        return inputs;
    }
    if (kind == "make_value" || kind == "break_value") {
        return visual_value_pins(visual_value_type(node), kind == "make_value", output);
    }
    if (kind == "delay") {
        return output ? action_pins(true) : Array::make(exec("in", "In"), defaulted(pin("duration", "Seconds", Variant::FLOAT), 0.25));
    }
    if (kind == "await_signal") {
        const bool typed = !text_at(node, "signal").is_empty();
        const StringName receiver = node.get("target_class", node.get("class", StringName("Object")));
        if (!output) {
            return Array::make(exec("in", "In"), typed ?
                    script_facing_pin(pin("target", "Target", Variant::OBJECT, receiver), node) :
                    pin("signal", "Signal", Variant::SIGNAL));
        }
        if (!typed) return Array::make(exec("then", "Then"), pin("arguments", "Arguments", Variant::ARRAY));
        Array outputs = event_pins(array_at(node, "inputs"));
        outputs.append(script_facing_pin(pin("sender", "Sender", Variant::OBJECT, receiver), node));
        return outputs;
    }
    if (kind == "cast") return cast_pins(node, output);
    if (kind == "input_poll") return input_poll_pins(input_poll_operation(StringName(text_at(node, "operation"))), output);
    if (kind == "ready" || kind == "lifecycle" || kind == "custom_event" || kind == "native_signal") {
        if (!output) {
            return kind == "native_signal" && node.get("wired_target", false) ?
                    Array::make(script_facing_pin(pin("target", "Target", Variant::OBJECT,
                            node.get("target_class", node.get("class", StringName("Object")))), node)) : Array();
        }
        if (kind == "lifecycle") {
            const StringName callback = node.get("callback", StringName());
            for (const Variant &value : lifecycle_events()) {
                const Dictionary event = value;
                if (StringName(event["callback"]) == callback) {
                    return event_pins(array_at(event, "inputs"));
                }
            }
            return {};
        }
        const Dictionary declaration = kind == "custom_event" ?
                find_graph_record(array_at(root, "events"), id_at(node, "definition")) : node;
        Array pins = event_pins(array_at(declaration, "inputs"));
        if (kind == "native_signal" && node.get("wired_target", false)) {
            pins.append(script_facing_pin(pin("sender", "Sender", Variant::OBJECT,
                    node.get("target_class", node.get("class", StringName("Object")))), node));
        }
        return pins;
    }
    if (kind == "singleton") {
        const Dictionary identity = node.get("pin", Dictionary());
        const StringName type = identity.get("class", StringName("Object"));
        return output ? Array::make(script_facing_pin(pin("value", text_at(node, "singleton"), Variant::OBJECT, type), identity)) : Array();
    }
    if (kind == "self" || kind == "scene_node") {
        const StringName type = kind == "self" ? base_class : StringName(node.get("class", StringName("Node")));
        const String script_name = text_at(node, "script_name");
        return output ? Array::make(script_facing_pin(pin("node", script_name.is_empty() ? String(type) : script_name,
                Variant::OBJECT, type), node)) : Array();
    }
    if (kind == "print") {
        return output ? action_pins(true) : Array::make(exec("in", "In"), pin("message", "Message", Variant::STRING));
    }
    if (kind == "custom_script") {
        Array pins = action_pins(output);
        for (const Variant &entry : array_at(node, output ? "outputs" : "inputs")) {
            if (entry.get_type() == Variant::DICTIONARY && !Dictionary(entry).get("exec", false)) {
                pins.append(entry);
            }
        }
        return pins;
    }
    if (kind == "create_node" || kind == "add_child") {
        Array result = action_pins(output);
        if (kind == "create_node" && output) {
            const StringName type = node.get("class", StringName("Node"));
            result.append(pin("node", String(type), Variant::OBJECT, type));
        } else if (kind == "add_child" && !output) {
            result.append(pin("node", "Node", Variant::OBJECT, "Node"));
            result.append(pin("parent", "Parent", Variant::OBJECT, "Node"));
        }
        return result;
    }
    if (kind == "get_property" || kind == "set_property" || kind == "call_method") {
        const Dictionary member = node.get("member", Dictionary());
        Array result = kind == "get_property" ? Array() : action_pins(output);
        if (kind == "call_method" && (member.has("variant_type") || member.has("utility"))) {
            const auto receiver = builtin_receiver_type(member);
            if (!output) {
                if (!bool(member.get("static", false)) && receiver < Variant::VARIANT_MAX) {
                    result.append(pin("target", "Target", receiver));
                }
                result.append_array(array_at(member, "inputs"));
                if (bool(member.get("vararg", false))) {
                    result.append(defaulted(pin("arguments", "Additional Arguments", Variant::ARRAY), Array()));
                }
            } else {
                if (member.get("output", Variant()).get_type() == Variant::DICTIONARY) result.append(member["output"]);
                if (builtin_updates_target(member)) result.append(pin("updated_target", "Updated Target", receiver));
            }
            return result;
        }
        if (!output) {
            if (!bool(member.get("static", false))) {
                result.append(script_facing_pin(pin("target", "Target", Variant::OBJECT,
                        member.get("class", StringName("Object"))), member));
            }
            result.append_array(array_at(member, "inputs"));
            if (bool(member.get("vararg", false))) {
                result.append(defaulted(pin("arguments", "Additional Arguments", Variant::ARRAY), Array()));
            }
        } else if (member.get("output", Variant()).get_type() == Variant::DICTIONARY) {
            result.append(member["output"]);
        }
        return result;
    }
    if (kind == "get_variable" || kind == "set_variable") {
        const String scope = node.get("scope", String("instance"));
        const Array variables = scope == "local" ? array_at(graph, "locals") : array_at(root, "variables");
        const Dictionary variable = find_graph_record(variables, id_at(node, "variable"));
        if (variable.is_empty()) {
            return {};
        }
        const Dictionary value = named_pin(variable.get("pin", Variant()), "value", "Value");
        if (kind == "get_variable") {
            return output ? Array::make(value) : Array();
        }
        return output ? action_pins(true) : Array::make(exec("in", "In"), value);
    }
    if (kind == "call_function" || kind == "call_macro" || kind == "function_entry" || kind == "macro_entry" || kind == "return") {
        const int64_t id = id_at(node, "definition") > 0 ? id_at(node, "definition") : id_at(graph, "definition");
        const Dictionary definition = definition_at(root, id);
        if (definition.is_empty()) {
            return {};
        }
        if (kind == "function_entry" || kind == "macro_entry") {
            return output ? tunnel_pins(array_at(definition, "inputs"), true) : Array();
        }
        if (kind == "return") {
            return output ? Array() : tunnel_pins(array_at(definition, "outputs"), false);
        }
        if (definition.get("pure", false)) {
            return array_at(definition, output ? "outputs" : "inputs");
        }
        return tunnel_pins(array_at(definition, output ? "outputs" : "inputs"), output);
    }
    if (kind == "emit_event") {
        const Dictionary event = find_graph_record(array_at(root, "events"), id_at(node, "definition"));
        Array result = action_pins(output);
        if (!output) {
            result.append_array(array_at(event, "inputs"));
        }
        return result;
    }
    if (kind == "reroute") {
        if (String(node.get("mode", String("data"))) == "exec") {
            return action_pins(output);
        }
        Dictionary saved = named_pin(node.get("pin", Variant()), "value", "Value");
        if (follows_loop_output(node)) {
            const Dictionary source = trace_collection_source(root, graph, upstream_connection(graph, id_at(node, "id"), "value"), base_class);
            saved = named_pin(source.is_empty() ? Variant() : Variant(source), "value", "Value");
            saved["iteration_output"] = true;
            if (!output) {
                saved["accept_rewire"] = true;
            }
        }
        return Array::make(saved);
    }
    if (kind == "branch" || kind == "while") {
        if (!output) {
            return Array::make(exec("in", "In"), defaulted(pin("condition", "Condition", Variant::BOOL), false));
        }
        return kind == "branch" ? Array::make(exec("true", "True"), exec("false", "False")) :
                Array::make(exec("body", "Body"), exec("completed", "Completed"));
    }
    if (kind == "for" || kind == "foreach") {
        const Dictionary collection = kind == "foreach" ? loop_collection_pin(root, graph, node, base_class) : Dictionary();
        if (output) {
            Array result = Array::make(exec("body", "Body"), exec("completed", "Completed"),
                    pin("index", "Index", Variant::INT));
            if (kind == "foreach") {
                if (const Dictionary key = loop_output_pin(collection, "key"); !key.is_empty()) {
                    result.append(key);
                }
                result.append(loop_output_pin(collection, "item"));
            }
            return result;
        }
        if (kind == "for") {
            return Array::make(exec("in", "In"), defaulted(pin("first", "First", Variant::INT), 0),
                    defaulted(pin("last", "Last", Variant::INT), 0), defaulted(pin("step", "Step", Variant::INT), 1));
        }
        const auto type = id_at(collection, "type");
        const bool known_shape = type == Variant::ARRAY || type == Variant::DICTIONARY;
        const String label = type == Variant::DICTIONARY ? "Dictionary" : type == Variant::ARRAY ? "Array" : "Collection";
        Dictionary collection_input = known_shape ? named_pin(collection, "array", label) : pin("array", label, Variant::ARRAY);
        collection_input.erase("default");
        collection_input.erase("iteration_output");
        collection_input.erase("accept_rewire");
        collection_input["iteration_collection"] = true;
        collection_input["iteration_shape_known"] = known_shape;
        return Array::make(exec("in", "In"), collection_input);
    }
    if (kind == "break" || kind == "continue") {
        return output ? Array() : action_pins(false);
    }
    if (kind == "do_once" || kind == "do_n" || kind == "flip_flop" || kind == "gate") {
        if (output) {
            return kind == "flip_flop" ? Array::make(exec("a", "A"), exec("b", "B"), pin("is_a", "Is A", Variant::BOOL)) :
                    action_pins(true);
        }
        Array result = action_pins(false);
        if (kind == "gate") {
            result.append_array(Array::make(exec("open", "Open"), exec("close", "Close"), exec("toggle", "Toggle"),
                    defaulted(pin("start_closed", "Start Closed", Variant::BOOL), true)));
        } else {
            result.append(exec("reset", "Reset"));
            if (kind == "do_once") {
                result.append(defaulted(pin("start_closed", "Start Closed", Variant::BOOL), false));
            } else if (kind == "do_n") {
                result.append(defaulted(pin("count", "Count", Variant::INT), 1));
            }
        }
        return result;
    }
    if (kind == "sequence" || kind == "switch") {
        if (!output) {
            return kind == "sequence" ? action_pins(false) :
                    Array::make(exec("in", "In"), named_pin(node.get("pin", Variant()), "value", "Value"));
        }
        Array result;
        if (kind == "sequence") {
            const int64_t count = std::clamp(static_cast<int64_t>(node.get("count", 2)), int64_t(1), int64_t(64));
            for (int64_t index = 0; index < count; ++index) {
                result.append(exec(numbered("then_", index), numbered("Then ", index + 1)));
            }
        } else {
            const Array cases = array_at(node, "cases");
            for (int64_t index = 0; index < std::min(cases.size(), int64_t(64)); ++index) {
                result.append(exec(numbered("case_", index), String(cases[index].stringify())));
            }
            result.append(exec("default", "Default"));
        }
        return result;
    }
    if (kind == "switch_enum") {
        if (!output) {
            return Array::make(exec("in", "In"), named_pin(node.get("pin", Variant()), "value", "Selection"));
        }
        const Array cases = array_at(node, "cases");
        Array result;
        for (int64_t index = 0; index < std::min(cases.size(), max_enum_switch_cases); ++index) {
            const Variant item = cases[index];
            if (item.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary entry = item;
            const String id = text_at(entry, "id");
            if (id.begins_with("case_")) {
                result.append(exec(id, text_at(entry, "label")));
            }
        }
        result.append(exec("default", "Default"));
        return result;
    }
    if (kind == "operator") {
        const String operation = node.get("operator", String());
        const bool logic = operation == "and" || operation == "or" || operation == "not";
        const bool comparison = operation == "equal" || operation == "not_equal" || operation == "less" ||
                operation == "less_equal" || operation == "greater" || operation == "greater_equal";
        if (output) {
            return Array::make(logic || comparison ? pin("result", "Result", Variant::BOOL) :
                    named_pin(node.get("pin", Variant()), "result", "Result"));
        }
        Array result = Array::make(logic ? pin("a", "A", Variant::BOOL) : named_pin(node.get("pin", Variant()), "a", "A"));
        if (operation != "not" && operation != "negate") {
            result.append(logic ? pin("b", "B", Variant::BOOL) : named_pin(node.get("pin", Variant()), "b", "B"));
        }
        return result;
    }
    if (kind == "literal" && node.has("global_constant")) {
        return output ? Array::make(pin("result", "Value", Variant::INT)) : Array();
    }
    if (kind == "literal" || kind == "select") {
        if (output) {
            return Array::make(named_pin(node.get("pin", Variant()), "result", "Result"));
        }
        if (kind == "literal") {
            return Array::make(named_pin(node.get("pin", Variant()), "value", "Value"));
        }
        return Array::make(defaulted(pin("condition", "Condition", Variant::BOOL), false),
                named_pin(node.get("pin", Variant()), "true", "True"),
                named_pin(node.get("pin", Variant()), "false", "False"));
    }
    return {};
}

std::map<int64_t, GraphPinLayout> graph_pin_presentation(const Dictionary &root, const Dictionary &graph,
        const StringName &base_class) {
    std::map<int64_t, GraphPinLayout> result;
    std::map<int64_t, Dictionary> nodes;
    for (const Variant &entry : array_at(graph, "nodes")) {
        if (entry.get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = entry;
        const int64_t id = id_at(node, "id");
        if (id <= 0 || !nodes.try_emplace(id, node).second) continue;
        GraphPinLayout &layout = result[id];
        layout.contract_inputs = graph_pins(root, graph, node, false, base_class);
        layout.contract_outputs = graph_pins(root, graph, node, true, base_class);
    }
    GraphTypeResolver resolver(graph, nodes, result);
    for (const auto &[id, node] : nodes) {
        GraphPinLayout &layout = result.at(id);
        for (const Dictionary &descriptor : layout.contract_inputs) {
            layout.inputs.append(resolver.presentation(node, descriptor, false));
        }
        for (const Dictionary &descriptor : layout.contract_outputs) {
            layout.outputs.append(resolver.presentation(node, descriptor, true));
        }
    }
    return result;
}

bool can_disable_graph_node(const Dictionary &root, const Dictionary &graph, const Dictionary &node,
        const StringName &base_class) {
    for (const bool output : {false, true}) {
        const StringName expected = output ? "then" : "in";
        bool found = false;
        for (const Dictionary &pin : graph_pins(root, graph, node, output, base_class)) {
            if (!pin.get("exec", false)) {
                continue;
            }
            if (found || StringName(pin.get("id", StringName())) != expected) {
                return false;
            }
            found = true;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

String validate_enum_switch(const Dictionary &node) {
    const String qualified = text_at(node, "enum_type");
    const Variant pin_record = node.get("pin", Variant());
    if (qualified.get_slice_count(".") != 2 || !qualified.get_slice(".", 0).is_valid_identifier() ||
            !qualified.get_slice(".", 1).is_valid_identifier() || pin_record.get_type() != Variant::DICTIONARY) {
        return "The enum switch is missing its enum type or selection pin.";
    }
    const Dictionary selection = pin_record;
    if (id_at(selection, "type") != Variant::INT || text_at(selection, "enum_type") != qualified ||
            (selection.has("default") && selection["default"].get_type() != Variant::INT)) {
        return "The selection pin must use the declared enum type.";
    }
    const Array cases = array_at(node, "cases");
    if (cases.is_empty() || cases.size() > max_enum_switch_cases) {
        return "The enum switch has a missing or oversized case list.";
    }
    std::set<String> ids;
    std::set<int64_t> values;
    for (const Variant &item : cases) {
        if (item.get_type() != Variant::DICTIONARY) {
            return "An enum switch case record is invalid.";
        }
        const Dictionary entry = item;
        const String id = text_at(entry, "id");
        const Variant value = entry.get("value", Variant());
        if (!id.begins_with("case_") || !id.substr(5).is_valid_identifier() || text_at(entry, "label").is_empty() ||
                value.get_type() != Variant::INT || !ids.insert(id).second || !values.insert(value).second) {
            return "Enum switch cases need unique symbol IDs, labels, and integer values.";
        }
    }
    return {};
}

Dictionary connect_graph_pins(const Dictionary &graph, int64_t from, const StringName &from_pin,
        int64_t to, const StringName &to_pin, bool execution, const Dictionary &root, const StringName &base_class) {
    if (from <= 0 || to <= 0 || from == to || from_pin.is_empty() || to_pin.is_empty()) {
        return {};
    }
    Array retained;
    const Array nodes = array_at(graph, "nodes");
    const Dictionary output = lookup_pin(root, graph, find_graph_record(nodes, from), true, from_pin, base_class);
    const Dictionary target_node = find_graph_record(nodes, to);
    const Dictionary input = lookup_pin(root, graph, target_node, false, to_pin, base_class);
    if (output.is_empty() || input.is_empty() || static_cast<bool>(output.get("exec", false)) != execution ||
            static_cast<bool>(input.get("exec", false)) != execution || (!execution && !pins_compatible(output, input))) {
        return {};
    }
    std::map<int64_t, std::vector<int64_t>> outgoing;
    for (const Variant &value : array_at(graph, "connections")) {
        if (value.get_type() != Variant::DICTIONARY) {
            return {};
        }
        const Dictionary connection = value;
        const int64_t source = id_at(connection, "from");
        const int64_t target = id_at(connection, "to");
        const auto source_pin = StringName(connection.get("from_pin", StringName()));
        const auto target_pin = StringName(connection.get("to_pin", StringName()));
        if (source == from && target == to && source_pin == from_pin && target_pin == to_pin) {
            return graph;
        }
        if ((!execution && target == to && target_pin == to_pin) ||
            (execution && source == from && source_pin == from_pin)) {
            continue;
        }
        if (execution && lookup_pin(root, graph, find_graph_record(nodes, source), true, source_pin, base_class).get("exec", false)) {
            outgoing[source].push_back(target);
        }
        retained.append(connection);
    }
    if (execution) {
        std::set<int64_t> visited;
        std::vector<int64_t> pending{to};
        while (!pending.empty()) {
            const int64_t cursor = pending.back();
            pending.pop_back();
            if (cursor == from) {
                return {};
            }
            if (visited.insert(cursor).second) {
                const auto adjacent = outgoing.find(cursor);
                if (adjacent != outgoing.end()) {
                    pending.insert(pending.end(), adjacent->second.begin(), adjacent->second.end());
                }
            }
        }
    }
    retained.append(wire(from, from_pin, to, to_pin));
    Dictionary result = graph.duplicate();
    result["connections"] = retained;
    if (!execution && input.get("dictionary_terminal", false)) {
        Dictionary updated_target = target_node.duplicate();
        set_dictionary_constructor_wire_type(updated_target, to_pin, output);
        if (updated_target != target_node) {
            Array updated_nodes = nodes.duplicate();
            for (int64_t index = 0; index < updated_nodes.size(); ++index) {
                const Variant record = updated_nodes[index];
                if (record.get_type() == Variant::DICTIONARY && id_at(record, "id") == to) {
                    updated_nodes[index] = updated_target;
                    break;
                }
            }
            result["nodes"] = updated_nodes;
        }
    }
    return result;
}

Dictionary prune_incompatible_loop_wires(const Dictionary &root, const Dictionary &graph, const StringName &base_class) {
    std::map<int64_t, Dictionary> nodes;
    std::set<int64_t> dependents;
    for (const Variant &entry : array_at(graph, "nodes")) {
        if (entry.get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = entry;
        const int64_t id = id_at(node, "id");
        nodes.emplace(id, node);
        const String kind = text_at(node, "kind");
        if (kind == "foreach" || follows_loop_output(node) || collection_operation_edits(collection_operation(StringName(kind))) ||
                (kind == "make_dictionary" && node.has("dictionary_types"))) {
            dependents.insert(id);
        }
    }
    if (dependents.empty()) {
        return graph;
    }
    Dictionary result = graph;
    Array connections = array_at(result, "connections");
    // A removed wire can change a downstream collection type. Repeat until no dependent wire is invalid.
    for (;;) {
        Array kept;
        for (const Variant &entry : connections) {
            if (entry.get_type() != Variant::DICTIONARY) {
                kept.append(entry);
                continue;
            }
            const Dictionary connection = entry;
            const int64_t from = id_at(connection, "from");
            const int64_t to = id_at(connection, "to");
            const StringName from_pin = connection.get("from_pin", StringName());
            const StringName to_pin = connection.get("to_pin", StringName());
            const auto producing = nodes.find(from);
            const auto receiving = nodes.find(to);
            const bool inferred_output = dependents.count(from) && (from_pin == StringName("item") ||
                    from_pin == StringName("key") || from_pin == StringName("value") || from_pin == StringName("result"));
            if ((inferred_output || (dependents.count(to) && to_pin == StringName("array"))) &&
                    producing != nodes.end() && receiving != nodes.end()) {
                const Dictionary output = lookup_pin(root, result, producing->second, true, from_pin, base_class);
                const Dictionary input = lookup_pin(root, result, receiving->second, false, to_pin, base_class);
                if (output.is_empty() || input.is_empty() || !pins_compatible(output, input)) {
                    continue;
                }
            }
            kept.append(entry);
        }
        if (kept.size() == connections.size()) {
            return result;
        }
        result = result.duplicate();
        result["connections"] = kept;
        connections = kept;
    }
}

Dictionary collapse_graph_selection(const Dictionary &root, int64_t graph_id, const PackedInt64Array &selected,
        bool macro, const StringName &base_class, String &error) {
    error = String();
    const Dictionary source_graph = graph_body(root, graph_id);
    const Array nodes = array_at(source_graph, "nodes");
    const Array edges = array_at(source_graph, "connections");
    std::set<int64_t> explicit_selection;
    for (int64_t id : selected) {
        explicit_selection.insert(id);
    }
    if (explicit_selection.empty() || source_graph.is_empty()) {
        error = "Select at least one graph node.";
        return {};
    }
    const std::set<int64_t> selection = include_exclusive_sources(root, source_graph, base_class, explicit_selection);

    Array remaining_nodes;
    Array extracted_nodes;
    int64_t highest_id = 0;
    Vector2 top_left;
    bool first = true;
    Vector2 call_position;
    bool first_explicit = true;
    bool pure = true;
    bool has_loop = false;
    bool has_loop_control = false;
    for (const Variant &item : nodes) {
        if (item.get_type() != Variant::DICTIONARY) {
            error = "The graph contains an invalid node record.";
            return {};
        }
        const Dictionary node = item;
        const int64_t id = id_at(node, "id");
        highest_id = std::max(highest_id, id);
        if (selection.count(id) == 0) {
            remaining_nodes.append(node);
            continue;
        }
        if (is_extraction_boundary(node)) {
            error = "Select ordinary graph nodes without event entries, graph boundaries, or local variable references.";
            return {};
        }
        const String kind = node.get("kind", String());
        const Vector2 position = node.get("position", Vector2());
        top_left = first ? position : top_left.min(position);
        first = false;
        if (explicit_selection.count(id) != 0) {
            call_position = first_explicit ? position : call_position.min(position);
            first_explicit = false;
        }
        extracted_nodes.append(node);
        has_loop |= kind == "for" || kind == "foreach" || kind == "while";
        has_loop_control |= kind == "break" || kind == "continue";
        for (bool output : {false, true}) {
            for (const Variant &value : graph_pins(root, source_graph, node, output, base_class)) {
                if (Dictionary(value).get("exec", false)) {
                    pure = false;
                }
            }
        }
    }
    const int64_t definition_id = next_definition_id(root);
    if (has_loop_control && !has_loop) {
        error = "Select the enclosing loop along with its Break or Continue nodes.";
        return {};
    }
    if (extracted_nodes.size() != static_cast<int64_t>(selection.size()) || definition_id == 0 ||
        highest_id > std::numeric_limits<int64_t>::max() - 3) {
        error = "The selection contains missing nodes or the graph has no available identifiers.";
        return {};
    }
    const int64_t call_id = highest_id + 1;
    const int64_t entry_id = highest_id + 2;
    const int64_t return_id = highest_id + 3;

    Array remaining_edges;
    Array extracted_edges;
    Array inputs;
    Array outputs;
    using Endpoint = std::pair<int64_t, StringName>;
    std::map<Endpoint, StringName> incoming_ports;
    std::map<Endpoint, StringName> outgoing_ports;
    std::set<Endpoint> function_entries;
    std::set<Endpoint> function_destinations;
    std::set<Endpoint> function_exits;
    for (const Variant &item : edges) {
        if (item.get_type() != Variant::DICTIONARY) {
            error = "The graph contains an invalid wire.";
            return {};
        }
        const Dictionary edge = item;
        const Endpoint from{id_at(edge, "from"), edge.get("from_pin", StringName())};
        const Endpoint to{id_at(edge, "to"), edge.get("to_pin", StringName())};
        const bool source_inside = selection.count(from.first) != 0;
        const bool target_inside = selection.count(to.first) != 0;
        if (source_inside == target_inside) {
            (source_inside ? extracted_edges : remaining_edges).append(edge);
            continue;
        }
        const bool incoming = target_inside;
        const Endpoint &inside = incoming ? to : from;
        const Dictionary inside_node = find_graph_record(nodes, inside.first);
        Dictionary descriptor = lookup_pin(root, source_graph, inside_node, !incoming, inside.second, base_class);
        if (descriptor.is_empty()) {
            error = "Repair the invalid connection on the selection before collapsing it.";
            return {};
        }
        const bool execution = descriptor.get("exec", false);
        if (execution && !macro) {
            if (incoming) {
                if (function_entries.insert(to).second) {
                    extracted_edges.append(wire(entry_id, "then", to.first, to.second));
                }
                remaining_edges.append(wire(from.first, from.second, call_id, "in"));
            } else {
                if (function_exits.insert(from).second) {
                    extracted_edges.append(wire(from.first, from.second, return_id, "in"));
                }
                if (function_destinations.insert(to).second) {
                    remaining_edges.append(wire(call_id, "then", to.first, to.second));
                }
            }
            continue;
        }
        Array &boundary = incoming ? inputs : outputs;
        auto &ports = incoming ? incoming_ports : outgoing_ports;
        const auto known_port = ports.find(inside);
        const StringName port = known_port == ports.end() ?
                StringName(numbered(incoming ? "input_" : "output_", boundary.size() + 1)) : known_port->second;
        if (known_port == ports.end()) {
            descriptor = descriptor.duplicate();
            descriptor["id"] = port;
            descriptor.erase("default");
            boundary.append(descriptor);
            ports.emplace(inside, port);
            extracted_edges.append(incoming ? wire(entry_id, port, to.first, to.second) :
                    wire(from.first, from.second, return_id, port));
        }
        remaining_edges.append(incoming ? wire(from.first, from.second, call_id, port) :
                wire(call_id, port, to.first, to.second));
    }
    if (function_entries.size() > 1 || function_destinations.size() > 1) {
        error = "A function needs one execution entry and one continuation. Use Collapse to Macro for multiple paths.";
        return {};
    }
    int64_t execution_outputs = 0;
    for (const Variant &value : outputs) {
        if (Dictionary(value).get("exec", false)) {
            ++execution_outputs;
        }
    }
    if (macro && execution_outputs > 1 && execution_outputs < outputs.size()) {
        error = "A macro with multiple exits must define data outputs on every path. Collapse the execution paths first, then add shared data pins.";
        return {};
    }
    // Wires left in the caller must not require the new call to enter itself or supply its own argument.
    std::map<int64_t, std::vector<int64_t>> execution_paths;
    std::map<int64_t, std::vector<int64_t>> data_paths;
    for (const Variant &item : remaining_edges) {
        const Dictionary edge = item;
        const int64_t from = id_at(edge, "from");
        const int64_t to = id_at(edge, "to");
        const StringName port = edge.get("from_pin", StringName());
        bool execution = false;
        if (from == call_id) {
            execution = !macro && port == StringName("then");
            for (const Variant &value : outputs) {
                const Dictionary descriptor = value;
                if (StringName(descriptor["id"]) == port) {
                    execution = descriptor.get("exec", false);
                    break;
                }
            }
        } else {
            execution = lookup_pin(root, source_graph, find_graph_record(nodes, from), true, port, base_class).get("exec", false);
        }
        auto &paths = execution ? execution_paths : data_paths;
        paths[from].push_back(to);
    }
    if (graph_path_returns_to(call_id, execution_paths)) {
        error = "Select all actions between the selected execution paths before collapsing.";
        return {};
    }
    if (graph_path_returns_to(call_id, data_paths)) {
        error = "Include the shared nodes that connect the selected data outputs back to the selected inputs.";
        return {};
    }

    bool has_entry = !function_entries.empty();
    bool has_exit = !function_exits.empty();
    for (const Variant &item : inputs) {
        has_entry |= static_cast<bool>(Dictionary(item).get("exec", false));
    }
    for (const Variant &item : outputs) {
        has_exit |= static_cast<bool>(Dictionary(item).get("exec", false));
    }
    if (!has_entry) {
        std::set<int64_t> targets;
        for (const Variant &item : extracted_edges) {
            const Dictionary edge = item;
            const int64_t destination = id_at(edge, "to");
            const Dictionary target_pin = lookup_pin(root, source_graph, find_graph_record(nodes, destination),
                    false, edge.get("to_pin", StringName()), base_class);
            if (target_pin.get("exec", false)) {
                targets.insert(destination);
            }
        }
        Endpoint first_action;
        for (const Variant &item : extracted_nodes) {
            const int64_t id = id_at(item, "id");
            if (targets.count(id) != 0) {
                continue;
            }
            for (const Variant &value : graph_pins(root, source_graph, item, false, base_class)) {
                const Dictionary input = value;
                if (!input.get("exec", false)) {
                    continue;
                }
                if (first_action.first != 0) {
                    error = "Connect the selected actions to one entry before collapsing them.";
                    return {};
                }
                first_action = {id, input["id"]};
            }
        }
        if (first_action.first > 0) {
            extracted_edges.append(wire(entry_id, "then", first_action.first, first_action.second));
        } else {
            extracted_edges.append(wire(entry_id, "then", return_id, "in"));
        }
    }
    if (!has_exit || !macro) {
        std::set<Endpoint> connected;
        for (const Variant &item : extracted_edges) {
            const Dictionary edge = item;
            connected.emplace(id_at(edge, "from"), edge.get("from_pin", StringName()));
        }
        for (const Variant &item : extracted_nodes) {
            const int64_t id = id_at(item, "id");
            for (const Variant &value : graph_pins(root, source_graph, item, true, base_class)) {
                const Dictionary output = value;
                const StringName port = output["id"];
                if (output.get("exec", false) && connected.count({id, port}) == 0) {
                    if (has_exit) {
                        error = "A function cannot merge an unconnected execution path into an existing continuation. Use Collapse to Macro.";
                        return {};
                    }
                    extracted_edges.append(wire(id, port, return_id, "in"));
                }
            }
        }
    }

    float far_right = 0.0f;
    for (int64_t index = 0; index < extracted_nodes.size(); ++index) {
        Dictionary node = Dictionary(extracted_nodes[index]).duplicate();
        const Vector2 position = Vector2(node.get("position", Vector2())) - top_left;
        node["position"] = position;
        far_right = std::max(far_right, static_cast<float>(position.x));
        extracted_nodes[index] = node;
    }
    extracted_nodes.append(boundary_node(entry_id, macro ? "macro_entry" : "function_entry", definition_id, Vector2(-240, 0)));
    extracted_nodes.append(boundary_node(return_id, "return", definition_id, Vector2(far_right + 280, 0)));
    remaining_nodes.append(boundary_node(call_id, macro ? "call_macro" : "call_function", definition_id, call_position));
    Dictionary definition;
    definition["id"] = definition_id;
    String definition_name = numbered(macro ? "macro_" : "function_", definition_id);
    auto name_in_use = [&](const String &name) {
        for (const char *key : {"functions", "macros", "events"}) {
            for (const Variant &value : array_at(root, key)) {
                if (value.get_type() == Variant::DICTIONARY && String(Dictionary(value).get("name", String())) == name) {
                    return true;
                }
            }
        }
        return false;
    };
    for (int64_t suffix = 2; name_in_use(definition_name); ++suffix) {
        definition_name = numbered(macro ? "macro_" : "function_", definition_id) + "_" + String::num_int64(suffix);
    }
    definition["name"] = definition_name;
    definition["inputs"] = inputs;
    definition["outputs"] = outputs;
    if (pure) {
        definition["pure"] = true;
    }
    Dictionary body;
    body["nodes"] = extracted_nodes;
    body["connections"] = extracted_edges;
    body["locals"] = Array();
    definition["graph"] = body;
    Dictionary outer = source_graph.duplicate();
    outer["nodes"] = remaining_nodes;
    outer["connections"] = remaining_edges;
    Dictionary result = replace_graph_body(root, graph_id, outer);
    const char *key = macro ? "macros" : "functions";
    Array definitions = array_at(result, key).duplicate();
    definitions.append(definition);
    result[key] = definitions;
    return result;
}

} // namespace crimson::visual_scripting
