// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::visual_scripting {

/// Remove all wires at one endpoint, returning an empty dictionary when nothing changes.
godot::Dictionary break_pin_links(const godot::Dictionary &graph, int64_t node_id, const godot::StringName &pin, bool output);

/// Remove every wire incident to a selected node, retaining nodes and authored values.
godot::Dictionary break_node_links(const godot::Dictionary &graph, const godot::PackedInt64Array &selected);

/// Restore one unconnected input's generated default by clearing the authored override.
godot::Dictionary reset_pin_literal(const godot::Dictionary &graph, int64_t node_id, const godot::StringName &pin);

/// Delete one unambiguous execution chain and reconnect its outside execution endpoints.
godot::Dictionary delete_and_reconnect_execution(const godot::Dictionary &root, int64_t graph_id,
        const godot::PackedInt64Array &selected, const godot::StringName &base_class);

/// Bypass one unambiguous execution chain, preserving its nodes, internal wires, and data wires.
godot::Dictionary bypass_execution(const godot::Dictionary &root, int64_t graph_id,
        const godot::PackedInt64Array &selected, const godot::StringName &base_class);

/// Rejoin each independently valid chain in a selection, or leave all wires unchanged.
/// A shake may follow a single linked execution route through nodes with additional unused pins.
godot::Dictionary bypass_graph_selection(const godot::Dictionary &root, int64_t graph_id,
        const godot::PackedInt64Array &selected, bool remove_nodes, const godot::StringName &base_class,
        bool linked_execution_only = false);

/// Remove a reroute and restore its producer's wires to each compatible consumer.
godot::Dictionary dissolve_reroute(const godot::Dictionary &root, int64_t graph_id, int64_t node_id,
        const godot::StringName &base_class);

/// Swap wires on two same-direction pins if all resulting connections remain valid.
godot::Dictionary exchange_pin_links(const godot::Dictionary &root, int64_t graph_id,
        int64_t first_node, const godot::StringName &first_pin, int64_t second_node, const godot::StringName &second_pin,
        bool output, const godot::StringName &base_class);

/// Insert an existing node into one wire using its named input and output pins.
godot::Dictionary splice_node_into_wire(const godot::Dictionary &root, int64_t graph_id, const godot::Dictionary &wire,
        int64_t node_id, const godot::StringName &input, const godot::StringName &output, const godot::StringName &base_class);

/// Find the topmost available compatible input and output for a node placed on a wire.
godot::Dictionary find_splice_pins(const godot::Dictionary &root, int64_t graph_id, const godot::Dictionary &wire,
        const godot::Dictionary &node, const godot::StringName &base_class);

/// Connect selected nodes in canvas order where a single compatible wire can be chosen safely.
godot::Dictionary connect_selected_graph_nodes(const godot::Dictionary &root, int64_t graph_id,
        const godot::PackedInt64Array &selected, const godot::StringName &base_class);

} // namespace crimson::visual_scripting

#endif
