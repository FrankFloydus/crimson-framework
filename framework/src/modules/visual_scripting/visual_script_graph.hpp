// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <map>

namespace crimson::visual_scripting {

/// Find a declaration or node by its stable identifier.
godot::Dictionary find_graph_record(const godot::Array &records, int64_t id);

/// Allocate an ID shared across the function, macro and event declarations.
int64_t next_definition_id(const godot::Dictionary &root);

/// Return the root Event Graph or the body of a function or macro.
godot::Dictionary graph_body(const godot::Dictionary &root, int64_t graph_id);

/// Replace a graph body while retaining its declaration and siblings.
godot::Dictionary replace_graph_body(const godot::Dictionary &root, int64_t graph_id, const godot::Dictionary &body);

/// Return callback name, title, and typed data pins for each supported Godot lifecycle entry.
godot::Array lifecycle_events();

/// Return the authored display name of a Custom Script card, or its default title.
godot::String custom_script_title(const godot::Dictionary &node);

/// Return a node's authored and compiler-enforced pin descriptors.
godot::Array graph_pins(const godot::Dictionary &root, const godot::Dictionary &graph, const godot::Dictionary &node,
        bool output, const godot::StringName &base_class);

/// Resolved editor pin descriptions for one node, ordered like its contract pins.
struct GraphPinLayout {
    godot::Array inputs; ///< Input presentation, including known producer types on generic connections.
    godot::Array outputs; ///< Output presentation, including dependent collection and terminal hints.
    godot::Array contract_inputs; ///< Input descriptors used by wiring, literal editors, and compilation.
    godot::Array contract_outputs; ///< Compiler-enforced output descriptors in graph slot order.
};

/// Resolve editor pin hints together for one graph snapshot without changing its runtime contracts.
std::map<int64_t, GraphPinLayout> graph_pin_presentation(const godot::Dictionary &root, const godot::Dictionary &graph,
        const godot::StringName &base_class);

/// Check that bypassing the node can preserve one unambiguous execution path from In to Then.
bool can_disable_graph_node(const godot::Dictionary &root, const godot::Dictionary &graph, const godot::Dictionary &node,
        const godot::StringName &base_class);

/// Validate the saved enum identity, selection pin and named cases without querying the engine.
godot::String validate_enum_switch(const godot::Dictionary &node);

/// Make a wire, replacing conflicting connections as one mutation. An invalid request returns an empty dictionary.
godot::Dictionary connect_graph_pins(const godot::Dictionary &graph, int64_t from, const godot::StringName &from_pin,
        int64_t to, const godot::StringName &to_pin, bool execution, const godot::Dictionary &root,
        const godot::StringName &base_class);

/// Remove incompatible dependent wires after a collection producer or variable type changes.
godot::Dictionary prune_incompatible_loop_wires(const godot::Dictionary &root, const godot::Dictionary &graph,
        const godot::StringName &base_class);

/// Extract selected nodes and exclusive data sources into a new definition with typed boundary ports.
godot::Dictionary collapse_graph_selection(const godot::Dictionary &root, int64_t graph_id,
        const godot::PackedInt64Array &selected, bool macro, const godot::StringName &base_class, godot::String &error);

} // namespace crimson::visual_scripting
