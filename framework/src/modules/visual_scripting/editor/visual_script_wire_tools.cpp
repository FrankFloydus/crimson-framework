// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_wire_tools.hpp"

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"

#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Identify one directed socket by its node and stable pin ID.
struct Socket {
    /// The persisted node ID.
    int64_t node = 0;
    /// The persisted pin ID, independent of its visible index.
    StringName pin;
    /// Match the node and pin together.
    bool operator==(const Socket &other) const { return node == other.node && pin == other.pin; }
};

/// Store two endpoints in the same order as a serialized connection.
struct Wire {
    /// The data or execution output.
    Socket from;
    /// The matching input.
    Socket to;
    /// Compare wire identity without depending on Dictionary key order.
    bool operator==(const Wire &other) const { return from == other.from && to == other.to; }
};

/// Read a stable node ID from one node or connection record.
int64_t record_id(const Dictionary &record, const char *field) {
    return record.get(field, int64_t(0));
}

/// Read one connection, accepting the saved String and StringName pin forms.
Wire read_wire(const Dictionary &record) {
    return {{record_id(record, "from"), record.get("from_pin", StringName())},
            {record_id(record, "to"), record.get("to_pin", StringName())}};
}

/// Replace the connection list without mutating the caller's body.
Dictionary with_links(const Dictionary &graph, const Array &connections) {
    Dictionary changed = graph.duplicate();
    changed["connections"] = connections;
    return changed;
}

/// Filter removed nodes without modifying shared declaration or graph records.
Array without_nodes(const Dictionary &graph, const std::set<int64_t> &removed) {
    Array remaining;
    for (const Dictionary &node : Array(graph.get("nodes", Array()))) {
        if (removed.count(record_id(node, "id")) == 0) {
            remaining.append(node);
        }
    }
    return remaining;
}

/// Collect graph-scoped node records once for pin validation of a wire edit.
std::map<int64_t, Dictionary> index_nodes(const Dictionary &graph) {
    std::map<int64_t, Dictionary> nodes;
    for (const Dictionary &node : Array(graph.get("nodes", Array()))) {
        nodes.emplace(record_id(node, "id"), node);
    }
    return nodes;
}

/// Resolve a named pin using the same descriptors as runtime and editor rendering.
Dictionary describe_pin(const Dictionary &root, const Dictionary &graph, const Dictionary &node,
        const StringName &pin, bool output, const StringName &base_class) {
    if (!node.is_empty()) {
        for (const Dictionary &descriptor : graph_pins(root, graph, node, output, base_class)) {
            if (StringName(descriptor.get("id", StringName())) == pin) {
                return descriptor;
            }
        }
    }
    return {};
}

/// Read the same execution marker used to project typed pins onto the canvas.
bool is_exec(const Dictionary &pin) {
    return pin.get("exec", false);
}

/// Accept matching execution sockets or compatible typed data sockets.
bool compatible(const Dictionary &source, const Dictionary &target) {
    return !source.is_empty() && !target.is_empty() && is_exec(source) == is_exec(target) &&
            (is_exec(source) || pins_compatible(source, target));
}

/// Determine whether an edge would compete for this socket's single connection.
bool occupies_socket(const Wire &wire, const Socket &socket, bool output, bool execution) {
    if (output) {
        return execution && wire.from == socket;
    }
    return !execution && wire.to == socket;
}

/// Find whether a named socket already has a wire the edit must retain.
bool socket_occupied(const Array &connections, const Socket &socket, bool output, bool execution) {
    for (const Dictionary &record : connections) {
        if (occupies_socket(read_wire(record), socket, output, execution)) {
            return true;
        }
    }
    return false;
}

/// Count all execution sockets on one side, including unwired alternatives.
int count_exec(const Array &pins) {
    int count = 0;
    for (const Dictionary &pin : pins) {
        if (is_exec(pin)) {
            ++count;
        }
    }
    return count;
}

/// Add a compatible edge with the common cycle check and without replacing any other link.
Dictionary append_wire(const Dictionary &root, const Dictionary &graph, const std::map<int64_t, Dictionary> &nodes,
        const Wire &wire, const StringName &base_class) {
    const auto from = nodes.find(wire.from.node);
    const auto to = nodes.find(wire.to.node);
    if (from == nodes.end() || to == nodes.end() || wire.from.node == wire.to.node) {
        return {};
    }
    const Dictionary source = describe_pin(root, graph, from->second, wire.from.pin, true, base_class);
    const Dictionary target = describe_pin(root, graph, to->second, wire.to.pin, false, base_class);
    const bool execution = is_exec(source);
    if (!compatible(source, target)) {
        return {};
    }
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        const Wire existing = read_wire(record);
        if (!(existing == wire) && (occupies_socket(existing, wire.from, true, execution) ||
                occupies_socket(existing, wire.to, false, execution))) {
            return {};
        }
    }
    return connect_graph_pins(graph, wire.from.node, wire.from.pin, wire.to.node, wire.to.pin, execution, root, base_class);
}

/// Cut a single entry-to-exit execution path and join its endpoints. Shake Off may ignore unused pins.
Dictionary reroute_execution(const Dictionary &root, int64_t graph_id, const PackedInt64Array &selected,
        bool remove_nodes, const StringName &base_class, bool linked_execution_only = false) {
    const Dictionary graph = graph_body(root, graph_id);
    if (graph.is_empty() || selected.is_empty()) {
        return {};
    }
    const std::set<int64_t> selection(selected.ptr(), selected.ptr() + selected.size());
    auto nodes = index_nodes(graph);
    for (const int64_t id : selection) {
        const auto found = nodes.find(id);
        if (found == nodes.end() || (!linked_execution_only &&
                (count_exec(graph_pins(root, graph, found->second, false, base_class)) != 1 ||
                        count_exec(graph_pins(root, graph, found->second, true, base_class)) != 1))) {
            return {};
        }
    }

    Wire entry;
    Wire exit;
    std::map<int64_t, int64_t> path;
    std::set<int64_t> receivers;
    Array retained;
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        const Wire wire = read_wire(record);
        const bool source_selected = selection.count(wire.from.node) != 0;
        const bool target_selected = selection.count(wire.to.node) != 0;
        if (!source_selected && !target_selected) {
            retained.append(record);
            continue;
        }
        const auto source = nodes.find(wire.from.node);
        if (source == nodes.end()) {
            return {};
        }
        const Dictionary pin = describe_pin(root, graph, source->second, wire.from.pin, true, base_class);
        if (pin.is_empty()) {
            return {};
        }
        if (is_exec(pin)) {
            if ((source_selected && !path.emplace(wire.from.node, wire.to.node).second) ||
                    (target_selected && !receivers.insert(wire.to.node).second)) {
                return {};
            }
            if (!source_selected || !target_selected) {
                Wire &boundary = target_selected ? entry : exit;
                if (boundary.from.node != 0) {
                    return {};
                }
                boundary = wire;
                continue;
            }
        } else if (source_selected && !target_selected) {
            // The downstream graph would stop receiving this executed node's value.
            return {};
        }
        if (!remove_nodes) {
            retained.append(record);
        }
    }
    if (entry.from.node <= 0 || exit.to.node <= 0 || path.size() != selection.size() || receivers.size() != selection.size()) {
        return {};
    }
    int64_t next = entry.to.node;
    for (size_t remaining = selection.size(); remaining > 0; --remaining) {
        const auto found = path.find(next);
        if (found == path.end()) {
            return {};
        }
        next = found->second;
    }
    if (next != exit.to.node) {
        return {};
    }

    Dictionary result = with_links(graph, retained);
    if (remove_nodes) {
        result["nodes"] = without_nodes(graph, selection);
    }
    return append_wire(root, result, nodes, {entry.from, exit.to}, base_class);
}

/// Cut one pure data path and connect its external producer to all compatible consumers.
Dictionary reroute_data(const Dictionary &root, int64_t graph_id, const PackedInt64Array &selected,
        bool remove_nodes, const StringName &base_class) {
    const Dictionary graph = graph_body(root, graph_id);
    if (graph.is_empty() || selected.is_empty()) {
        return {};
    }
    const std::set<int64_t> selection(selected.ptr(), selected.ptr() + selected.size());
    const auto nodes = index_nodes(graph);
    for (const int64_t id : selection) {
        const auto found = nodes.find(id);
        if (found == nodes.end() || count_exec(graph_pins(root, graph, found->second, false, base_class)) != 0 ||
                count_exec(graph_pins(root, graph, found->second, true, base_class)) != 0) {
            return {};
        }
    }

    std::optional<Wire> entry;
    Socket exit;
    std::vector<Socket> consumers;
    std::map<int64_t, int64_t> path;
    std::set<int64_t> receivers;
    Array retained;
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        const Wire wire = read_wire(record);
        const bool source_selected = selection.count(wire.from.node) != 0;
        const bool target_selected = selection.count(wire.to.node) != 0;
        if (!source_selected && !target_selected) {
            retained.append(record);
        } else if (!source_selected) {
            if (entry || !receivers.insert(wire.to.node).second) {
                return {};
            }
            entry = wire;
        } else if (!target_selected) {
            if (exit.node != 0 && !(exit == wire.from)) {
                return {};
            }
            exit = wire.from;
            if (std::find(consumers.begin(), consumers.end(), wire.to) == consumers.end()) {
                consumers.push_back(wire.to);
            }
        } else {
            if (!path.emplace(wire.from.node, wire.to.node).second || !receivers.insert(wire.to.node).second) {
                return {};
            }
            if (!remove_nodes) {
                retained.append(record);
            }
        }
    }
    if (!entry || consumers.empty() || path.size() + 1 != selection.size() || receivers.size() != selection.size()) {
        return {};
    }
    int64_t next = entry->to.node;
    for (size_t remaining = path.size(); remaining > 0; --remaining) {
        const auto found = path.find(next);
        if (found == path.end()) {
            return {};
        }
        next = found->second;
    }
    if (next != exit.node) {
        return {};
    }

    Dictionary result = with_links(graph, retained);
    if (remove_nodes) {
        result["nodes"] = without_nodes(graph, selection);
    }
    for (const Socket &consumer : consumers) {
        result = append_wire(root, result, nodes, {entry->from, consumer}, base_class);
        if (result.is_empty()) {
            return {};
        }
    }
    return result;
}

} // namespace

Dictionary break_pin_links(const Dictionary &graph, int64_t node_id, const StringName &pin, bool output) {
    const Socket endpoint{node_id, pin};
    if (node_id <= 0 || pin.is_empty()) {
        return {};
    }
    const Array connections = graph.get("connections", Array());
    Array retained;
    for (const Dictionary &record : connections) {
        const Wire wire = read_wire(record);
        if (!((output ? wire.from : wire.to) == endpoint)) {
            retained.append(record);
        }
    }
    return retained.size() == connections.size() ? Dictionary() : with_links(graph, retained);
}

Dictionary break_node_links(const Dictionary &graph, const PackedInt64Array &selected) {
    if (selected.is_empty()) {
        return {};
    }
    const std::set<int64_t> selection(selected.ptr(), selected.ptr() + selected.size());
    const Array connections = graph.get("connections", Array());
    Array retained;
    for (const Dictionary &record : connections) {
        const Wire wire = read_wire(record);
        if (selection.count(wire.from.node) == 0 && selection.count(wire.to.node) == 0) {
            retained.append(record);
        }
    }
    return retained.size() == connections.size() ? Dictionary() : with_links(graph, retained);
}

Dictionary reset_pin_literal(const Dictionary &graph, int64_t node_id, const StringName &pin) {
    if (node_id <= 0 || pin.is_empty()) {
        return {};
    }
    const Socket endpoint{node_id, pin};
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        if (read_wire(record).to == endpoint) {
            return {};
        }
    }
    const Array nodes = graph.get("nodes", Array());
    for (int64_t index = 0; index < nodes.size(); ++index) {
        const Dictionary original = nodes[index];
        if (record_id(original, "id") != node_id) {
            continue;
        }
        Dictionary values = Dictionary(original.get("values", Dictionary())).duplicate();
        bool reset = values.erase(pin);
        reset = values.erase(String(pin)) || reset;
        Dictionary changed = original.duplicate();
        if (String(original.get("kind", String())) == "print" && pin == StringName("message")) {
            reset = changed.erase("message") || reset;
        } else if (String(original.get("kind", String())) == "literal" && pin == StringName("value")) {
            reset = changed.erase("value") || reset;
        }
        if (!reset) {
            return {};
        }
        if (values.is_empty()) {
            changed.erase("values");
        } else {
            changed["values"] = values;
        }
        Array updated = nodes.duplicate();
        updated[index] = changed;
        Dictionary result = graph.duplicate();
        result["nodes"] = updated;
        return result;
    }
    return {};
}

Dictionary delete_and_reconnect_execution(const Dictionary &root, int64_t graph_id,
        const PackedInt64Array &selected, const StringName &base_class) {
    return reroute_execution(root, graph_id, selected, true, base_class);
}

Dictionary bypass_execution(const Dictionary &root, int64_t graph_id,
        const PackedInt64Array &selected, const StringName &base_class) {
    return reroute_execution(root, graph_id, selected, false, base_class);
}

Dictionary bypass_graph_selection(const Dictionary &root, int64_t graph_id,
        const PackedInt64Array &selected, bool remove_nodes, const StringName &base_class, bool linked_execution_only) {
    Dictionary graph = graph_body(root, graph_id);
    if (selected.is_empty() || graph.is_empty()) {
        return {};
    }
    // Only selected nodes linked to one another can belong to the same bypass.
    std::map<int64_t, std::vector<int64_t>> neighbours;
    std::set<int64_t> remaining(selected.ptr(), selected.ptr() + selected.size());
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        const Wire wire = read_wire(record);
        if (remaining.count(wire.from.node) != 0 && remaining.count(wire.to.node) != 0) {
            neighbours[wire.from.node].push_back(wire.to.node);
            neighbours[wire.to.node].push_back(wire.from.node);
        }
    }
    Dictionary document = root;
    while (!remaining.empty()) {
        PackedInt64Array component;
        component.append(*remaining.begin());
        remaining.erase(remaining.begin());
        for (int64_t index = 0; index < component.size(); ++index) {
            for (const int64_t other : neighbours[component[index]]) {
                if (remaining.erase(other) != 0) {
                    component.append(other);
                }
            }
        }
        Dictionary changed = reroute_execution(document, graph_id, component, remove_nodes, base_class, linked_execution_only);
        if (changed.is_empty()) {
            changed = reroute_data(document, graph_id, component, remove_nodes, base_class);
        }
        if (changed.is_empty()) {
            return {};
        }
        graph = changed;
        if (!remaining.empty()) {
            document = replace_graph_body(root, graph_id, graph);
        }
    }
    return graph;
}

Dictionary dissolve_reroute(const Dictionary &root, int64_t graph_id, int64_t node_id, const StringName &base_class) {
    const Dictionary graph = graph_body(root, graph_id);
    auto nodes = index_nodes(graph);
    const auto route = nodes.find(node_id);
    if (node_id <= 0 || route == nodes.end() || String(route->second.get("kind", String())) != "reroute") {
        return {};
    }
    const bool execution = String(route->second.get("mode", String())) == "exec";
    Socket producer;
    std::vector<Socket> consumers;
    Array retained;
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        const Wire wire = read_wire(record);
        if (wire.to.node == node_id) {
            if (producer.node != 0 || wire.to.pin != StringName(execution ? "in" : "value")) {
                return {};
            }
            producer = wire.from;
        } else if (wire.from.node == node_id) {
            if (wire.from.pin != StringName(execution ? "then" : "value")) {
                return {};
            }
            consumers.push_back(wire.to);
        } else {
            retained.append(record);
        }
    }
    if (producer.node <= 0 || consumers.empty() || (execution && consumers.size() != 1)) {
        return {};
    }
    nodes.erase(route);
    Array remaining;
    for (const Dictionary &node : Array(graph.get("nodes", Array()))) {
        if (record_id(node, "id") != node_id) {
            remaining.append(node);
        }
    }
    Dictionary result = with_links(graph, retained);
    result["nodes"] = remaining;
    for (const Socket &consumer : consumers) {
        result = append_wire(root, result, nodes, {producer, consumer}, base_class);
        if (result.is_empty()) {
            return {};
        }
    }
    return result;
}

Dictionary exchange_pin_links(const Dictionary &root, int64_t graph_id,
        int64_t first_node, const StringName &first_pin, int64_t second_node, const StringName &second_pin,
        bool output, const StringName &base_class) {
    const Socket first{first_node, first_pin};
    const Socket second{second_node, second_pin};
    if (first == second || first_node <= 0 || second_node <= 0 || first_pin.is_empty() || second_pin.is_empty()) {
        return {};
    }
    const Dictionary graph = graph_body(root, graph_id);
    const auto nodes = index_nodes(graph);
    const auto first_record = nodes.find(first_node);
    const auto second_record = nodes.find(second_node);
    if (first_record == nodes.end() || second_record == nodes.end()) {
        return {};
    }
    const Dictionary first_descriptor = describe_pin(root, graph, first_record->second, first_pin, output, base_class);
    const Dictionary second_descriptor = describe_pin(root, graph, second_record->second, second_pin, output, base_class);
    if (first_descriptor.is_empty() || second_descriptor.is_empty() || is_exec(first_descriptor) != is_exec(second_descriptor)) {
        return {};
    }
    Array retained;
    std::vector<Wire> replacements;
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        Wire wire = read_wire(record);
        Socket &endpoint = output ? wire.from : wire.to;
        if (endpoint == first) {
            endpoint = second;
        } else if (endpoint == second) {
            endpoint = first;
        } else {
            retained.append(record);
            continue;
        }
        replacements.push_back(wire);
    }
    if (replacements.empty()) {
        return {};
    }
    Dictionary result = with_links(graph, retained);
    for (const Wire &wire : replacements) {
        result = append_wire(root, result, nodes, wire, base_class);
        if (result.is_empty()) {
            return {};
        }
    }
    return result;
}

Dictionary splice_node_into_wire(const Dictionary &root, int64_t graph_id, const Dictionary &wire,
        int64_t node_id, const StringName &input, const StringName &output, const StringName &base_class) {
    const Wire original = read_wire(wire);
    const Socket in{node_id, input};
    const Socket out{node_id, output};
    if (node_id <= 0 || node_id == original.from.node || node_id == original.to.node || input.is_empty() || output.is_empty()) {
        return {};
    }
    const Dictionary graph = graph_body(root, graph_id);
    const auto nodes = index_nodes(graph);
    const auto inserted = nodes.find(node_id);
    if (inserted == nodes.end()) {
        return {};
    }
    const Dictionary input_descriptor = describe_pin(root, graph, inserted->second, input, false, base_class);
    const Dictionary output_descriptor = describe_pin(root, graph, inserted->second, output, true, base_class);
    if (input_descriptor.is_empty() || output_descriptor.is_empty() || is_exec(input_descriptor) != is_exec(output_descriptor)) {
        return {};
    }
    bool found = false;
    Array retained;
    for (const Dictionary &record : Array(graph.get("connections", Array()))) {
        const Wire connection = read_wire(record);
        if (connection == original) {
            found = true;
        } else if (connection.to == in) {
            return {};
        } else {
            retained.append(record);
        }
    }
    if (!found) {
        return {};
    }
    Dictionary result = append_wire(root, with_links(graph, retained), nodes, {original.from, in}, base_class);
    return result.is_empty() ? Dictionary() : append_wire(root, result, nodes, {out, original.to}, base_class);
}

Dictionary find_splice_pins(const Dictionary &root, int64_t graph_id, const Dictionary &wire,
        const Dictionary &node, const StringName &base_class) {
    const Dictionary graph = graph_body(root, graph_id);
    const Array records = graph.get("nodes", Array());
    const Wire original = read_wire(wire);
    const int64_t node_id = record_id(node, "id");
    if (node_id != 0 && (node_id == original.from.node || node_id == original.to.node)) {
        return {};
    }
    const Dictionary source = describe_pin(root, graph, find_graph_record(records, original.from.node), original.from.pin, true, base_class);
    const Dictionary target = describe_pin(root, graph, find_graph_record(records, original.to.node), original.to.pin, false, base_class);
    if (!compatible(source, target)) {
        return {};
    }
    const Array connections = graph.get("connections", Array());
    const bool execution = is_exec(source);
    StringName input_id;
    StringName output_id;
    // graph_pins uses the same top-to-bottom ordering as the node card.
    for (const Dictionary &input : graph_pins(root, graph, node, false, base_class)) {
        const StringName candidate = input.get("id", StringName());
        if (candidate.is_empty() || !compatible(source, input) ||
                (node_id > 0 && socket_occupied(connections, {node_id, candidate}, false, false))) {
            continue;
        }
        input_id = candidate;
        break;
    }
    for (const Dictionary &output : graph_pins(root, graph, node, true, base_class)) {
        const StringName candidate = output.get("id", StringName());
        if (candidate.is_empty() || !compatible(output, target) ||
                (node_id > 0 && socket_occupied(connections, {node_id, candidate}, true, execution))) {
            continue;
        }
        output_id = candidate;
        break;
    }
    if (input_id.is_empty() || output_id.is_empty()) {
        return {};
    }
    Dictionary selected;
    selected["input"] = input_id;
    selected["output"] = output_id;
    return selected;
}

Dictionary connect_selected_graph_nodes(const Dictionary &root, int64_t graph_id,
        const PackedInt64Array &selected, const StringName &base_class) {
    const Dictionary graph = graph_body(root, graph_id);
    const auto nodes = index_nodes(graph);
    std::set<int64_t> unique;
    std::vector<std::tuple<float, float, int64_t>> ordered;
    for (const int64_t id : selected) {
        const auto found = nodes.find(id);
        if (found == nodes.end() || !unique.insert(id).second) {
            continue;
        }
        const Vector2 position = found->second.get("position", Vector2());
        ordered.emplace_back(position.x, position.y, id);
    }
    if (ordered.size() < 2) {
        return {};
    }
    std::sort(ordered.begin(), ordered.end());
    Dictionary result = graph;
    for (size_t index = 1; index < ordered.size(); ++index) {
        const int64_t from = std::get<2>(ordered[index - 1]);
        const int64_t to = std::get<2>(ordered[index]);
        const Array connections = result.get("connections", Array());
        const Array outputs = graph_pins(root, graph, nodes.at(from), true, base_class);
        const Array inputs = graph_pins(root, graph, nodes.at(to), false, base_class);
        for (const bool execution : {true, false}) {
            std::optional<Wire> candidate;
            bool ambiguous = false;
            for (const Dictionary &source : outputs) {
                const Socket source_socket{from, source.get("id", StringName())};
                if (is_exec(source) != execution || socket_occupied(connections, source_socket, true, execution)) {
                    continue;
                }
                for (const Dictionary &target : inputs) {
                    const Socket target_socket{to, target.get("id", StringName())};
                    if (compatible(source, target) && !socket_occupied(connections, target_socket, false, execution)) {
                        if (candidate) {
                            ambiguous = true;
                            break;
                        }
                        candidate = Wire{source_socket, target_socket};
                    }
                }
                if (ambiguous) {
                    break;
                }
            }
            if (candidate) {
                if (!ambiguous) {
                    const Dictionary joined = append_wire(root, result, nodes, *candidate, base_class);
                    if (!joined.is_empty()) {
                        result = joined;
                    }
                }
                break;
            }
        }
    }
    return result == graph ? Dictionary() : result;
}

} // namespace crimson::visual_scripting

#endif
