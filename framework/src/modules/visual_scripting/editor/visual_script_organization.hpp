// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>

namespace crimson::visual_scripting {

/// Include each selected node and every reachable node in the requested wire direction.
godot::PackedInt64Array select_graph_dependencies(const godot::Dictionary &graph,
        const godot::PackedInt64Array &selected, bool upstream);

/// Read the graph body's saved comment frames, which are independent from executable nodes.
godot::Array graph_comments(const godot::Dictionary &graph);

/// Read the stable node IDs grouped under a comment frame.
godot::PackedInt64Array graph_comment_members(const godot::Dictionary &comment);

/// Add a comment frame around the supplied bounds and transfer its members from previous frames.
godot::Dictionary add_graph_comment(const godot::Dictionary &graph, const godot::PackedInt64Array &members,
        const godot::Rect2 &bounds);

/// Edit a frame's title and graph-space bounds without changing its members.
godot::Dictionary update_graph_comment(const godot::Dictionary &graph, int64_t id,
        const godot::String &title, const godot::Rect2 &bounds);

/// Remove the given comment frames while retaining their member nodes.
godot::Dictionary remove_graph_comments(const godot::Dictionary &graph, const godot::PackedInt64Array &ids);

/// Clear references to removed nodes before a future edit can reuse their IDs.
godot::Dictionary prune_graph_comment_members(const godot::Dictionary &graph);

} // namespace crimson::visual_scripting

#endif
