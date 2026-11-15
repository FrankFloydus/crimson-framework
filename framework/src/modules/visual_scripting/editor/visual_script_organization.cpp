// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_organization.hpp"

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Read a saved list without coercing other Variant types.
Array read_records(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Read a stable ID without coercing other numeric types.
int64_t read_id(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::INT ? static_cast<int64_t>(value) : 0;
}

/// Index the positive IDs of nodes that are still present in this graph body.
std::set<int64_t> live_nodes(const Dictionary &graph) {
    std::set<int64_t> ids;
    for (const Variant &entry : read_records(graph, "nodes")) {
        if (entry.get_type() == Variant::DICTIONARY) {
            const int64_t id = read_id(entry, "id");
            if (id > 0) {
                ids.insert(id);
            }
        }
    }
    return ids;
}

/// Keep valid unique IDs in their original order.
Array keep_members(const PackedInt64Array &members, const std::set<int64_t> &available) {
    std::set<int64_t> seen;
    Array result;
    for (const int64_t id : members) {
        if (available.count(id) != 0 && seen.insert(id).second) {
            result.append(id);
        }
    }
    return result;
}

/// Reuse an unchanged graph or attach a replacement comment list to a shallow copy.
Dictionary with_comments(const Dictionary &graph, const Array &comments) {
    if (graph_comments(graph) == comments) {
        return graph;
    }
    Dictionary result = graph.duplicate();
    if (comments.is_empty()) {
        result.erase("comments");
    } else {
        result["comments"] = comments;
    }
    return result;
}

/// Keep invalid dimensions out of the editor's graph geometry.
bool valid_bounds(const Rect2 &bounds) {
    return bounds.is_finite() && bounds.size.x > 0 && bounds.size.y > 0;
}

} // namespace

PackedInt64Array select_graph_dependencies(const Dictionary &graph, const PackedInt64Array &selected, bool upstream) {
    const auto available = live_nodes(graph);
    std::map<int64_t, std::vector<int64_t>> neighbors;
    for (const Variant &entry : read_records(graph, "connections")) {
        if (entry.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary wire = entry;
        const int64_t from = read_id(wire, upstream ? "to" : "from");
        const int64_t to = read_id(wire, upstream ? "from" : "to");
        if (available.count(from) != 0 && available.count(to) != 0) {
            neighbors[from].push_back(to);
        }
    }

    std::set<int64_t> visited;
    PackedInt64Array result;
    for (const int64_t id : selected) {
        if (available.count(id) != 0 && visited.insert(id).second) {
            result.append(id);
        }
    }
    for (int64_t index = 0; index < result.size(); ++index) {
        const auto adjacent = neighbors.find(result[index]);
        if (adjacent == neighbors.end()) {
            continue;
        }
        for (const int64_t id : adjacent->second) {
            if (visited.insert(id).second) {
                result.append(id);
            }
        }
    }
    return result;
}

Array graph_comments(const Dictionary &graph) {
    return read_records(graph, "comments");
}

PackedInt64Array graph_comment_members(const Dictionary &comment) {
    PackedInt64Array members;
    for (const Variant &entry : read_records(comment, "members")) {
        if (entry.get_type() == Variant::INT && static_cast<int64_t>(entry) > 0) {
            members.append(static_cast<int64_t>(entry));
        }
    }
    return members;
}

Dictionary add_graph_comment(const Dictionary &graph, const PackedInt64Array &members, const Rect2 &bounds) {
    if (!valid_bounds(bounds)) {
        return graph;
    }
    Array comments = graph_comments(graph).duplicate();
    int64_t largest = 0;
    for (const Variant &entry : comments) {
        if (entry.get_type() == Variant::DICTIONARY) {
            largest = std::max(largest, read_id(entry, "id"));
        }
    }
    if (largest == std::numeric_limits<int64_t>::max()) {
        return graph;
    }

    std::set<int64_t> available = live_nodes(graph);
    const Array retained = keep_members(members, available);
    for (const Variant &entry : retained) {
        available.erase(static_cast<int64_t>(entry));
    }
    if (!retained.is_empty()) {
        for (int64_t index = 0; index < comments.size(); ++index) {
            if (comments[index].get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary previous = comments[index];
            const Array remaining = keep_members(graph_comment_members(previous), available);
            if (remaining != read_records(previous, "members")) {
                Dictionary edited = previous.duplicate();
                edited["members"] = remaining;
                comments[index] = edited;
            }
        }
    }
    Dictionary comment;
    comment["id"] = largest + 1;
    comment["title"] = "Comment";
    comment["position"] = bounds.position;
    comment["size"] = bounds.size;
    comment["members"] = retained;
    comments.append(comment);
    return with_comments(graph, comments);
}

Dictionary update_graph_comment(const Dictionary &graph, int64_t id, const String &title, const Rect2 &bounds) {
    if (id <= 0 || !valid_bounds(bounds)) {
        return graph;
    }
    Array comments = graph_comments(graph).duplicate();
    for (int64_t index = 0; index < comments.size(); ++index) {
        if (comments[index].get_type() != Variant::DICTIONARY || read_id(comments[index], "id") != id) {
            continue;
        }
        const Dictionary previous = comments[index];
        Dictionary comment = previous.duplicate();
        comment["title"] = title.strip_edges().is_empty() ? String("Comment") : title;
        comment["position"] = bounds.position;
        comment["size"] = bounds.size;
        comments[index] = comment;
        return with_comments(graph, comments);
    }
    return graph;
}

Dictionary remove_graph_comments(const Dictionary &graph, const PackedInt64Array &ids) {
    std::set<int64_t> removed;
    for (const int64_t id : ids) {
        if (id > 0) {
            removed.insert(id);
        }
    }
    if (removed.empty()) {
        return graph;
    }
    Array retained;
    for (const Variant &entry : graph_comments(graph)) {
        if (entry.get_type() != Variant::DICTIONARY || removed.count(read_id(entry, "id")) == 0) {
            retained.append(entry);
        }
    }
    return with_comments(graph, retained);
}

Dictionary prune_graph_comment_members(const Dictionary &graph) {
    const Array comments = graph_comments(graph);
    if (comments.is_empty()) {
        return graph;
    }
    auto available = live_nodes(graph);
    Array retained = comments.duplicate();
    for (int64_t index = 0; index < comments.size(); ++index) {
        if (comments[index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary comment = comments[index];
        const Array remaining = keep_members(graph_comment_members(comment), available);
        for (const Variant &member : remaining) {
            available.erase(static_cast<int64_t>(member));
        }
        if (remaining != read_records(comment, "members")) {
            Dictionary edited = comment.duplicate();
            edited["members"] = remaining;
            retained[index] = edited;
        }
    }
    return with_comments(graph, retained);
}

} // namespace crimson::visual_scripting

#endif
