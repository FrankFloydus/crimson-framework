// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_debugger.hpp"

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/visual_script_language.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/engine_debugger.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// Graph-local IDs and definition IDs occupy separate portions of Godot's signed line number.
constexpr int64_t graph_stride = 65536;
/// Keep value payloads far below the engine debugger's packet limit.
constexpr int64_t value_limit = 192;
/// Bound each table even when a graph has many variables or inputs.
constexpr int64_t row_limit = 64;
/// Snapshot consumed synchronously by the native debug loop; only the game main thread writes it.
thread_local const VisualDebugFrame *paused_frame = nullptr;
/// Send one inspection acknowledgement per paused frame.
thread_local bool inspection_sent = false;

/// Update a graph breakpoint without the editor's text line list or changing the graph resource.
bool receive_breakpoint(const String &message, const Array &arguments) {
    if (message != "breakpoint" || arguments.size() != 3 || arguments[0].get_type() != Variant::STRING ||
            arguments[1].get_type() != Variant::INT || arguments[2].get_type() != Variant::BOOL) {
        return false;
    }
    const String path = arguments[0];
    const int64_t line = arguments[1];
    if (!path.begins_with("res://") || line <= 0 || line > 2147483647 || line % graph_stride == 0) {
        return false;
    }
    auto *debugger = EngineDebugger::get_singleton();
    if (debugger->is_active()) {
        if (static_cast<bool>(arguments[2])) {
            debugger->insert_breakpoint(static_cast<int32_t>(line), path);
        } else {
            debugger->remove_breakpoint(static_cast<int32_t>(line), path);
        }
    }
    return true;
}

/// Keep previews cheap and safe for containers, freed objects, and opaque Variant handles.
String preview_value(const Variant &value) {
    switch (value.get_type()) {
        case Variant::NIL: return "null";
        case Variant::STRING:
        case Variant::STRING_NAME: {
            const String contents = value;
            return contents.is_empty() ? String("\"\"") : contents;
        }
        case Variant::ARRAY: return String::num_int64(Array(value).size()) + " items";
        case Variant::DICTIONARY: return String::num_int64(Dictionary(value).size()) + " pairs";
        case Variant::OBJECT: {
            auto *object = ObjectDB::get_instance(value.operator ObjectID());
            if (!object) {
                return value.operator ObjectID() == ObjectID() ? String("null") : String("Freed object");
            }
            if (auto *node = Object::cast_to<Node>(object)) {
                const String name = node->get_name();
                const String type = node->get_class();
                return name.is_empty() || name.begins_with("@") ? type : name;
            }
            if (auto *resource = Object::cast_to<Resource>(object); resource && !resource->get_path().is_empty()) {
                return resource->get_path().get_file();
            }
            return String(object->get_class());
        }
        case Variant::CALLABLE: return "Callable";
        case Variant::SIGNAL: return "Signal";
        default:
            if (value.get_type() >= Variant::PACKED_BYTE_ARRAY) {
                return "Packed data";
            }
            return value.stringify();
    }
}

/// Clamp long names and values before sending anything to the editor.
String bounded(const String &value) {
    return value.length() > value_limit ? value.left(value_limit) + "..." : value;
}

/// Provide context that is useful on hover while keeping the primary value readable.
String value_detail(const Variant &value, const String &preview) {
    if (value.get_type() == Variant::OBJECT) {
        auto *object = ObjectDB::get_instance(value.operator ObjectID());
        if (auto *node = Object::cast_to<Node>(object)) {
            return node->is_inside_tree() ? String(node->get_class()) + String("\n") + String(node->get_path()) :
                    String(node->get_class()) + String(U" \u00B7 Not in scene tree");
        }
        if (auto *resource = Object::cast_to<Resource>(object); resource && !resource->get_path().is_empty()) {
            return resource->get_path();
        }
    } else if (value.get_type() == Variant::ARRAY || value.get_type() == Variant::DICTIONARY) {
        // Keep the preview shallow, even for self-referential collections.
        String detail;
        if (value.get_type() == Variant::ARRAY) {
            const Array entries = value;
            for (int64_t i = 0; i < entries.size() && i < 5; ++i) {
                detail += String::num_int64(i) + String(": ") + preview_value(entries[i]).left(32) + String("\n");
            }
        } else {
            const Dictionary entries = value;
            Variant iterator;
            bool valid = false;
            bool has_entry = value.iter_init(iterator, valid);
            for (int64_t index = 0; has_entry && valid && index < 5; ++index) {
                const Variant key = value.iter_get(iterator, valid);
                detail += preview_value(key).left(24) + String(": ") + preview_value(entries[key]).left(32) + String("\n");
                has_entry = value.iter_next(iterator, valid);
            }
        }
        return detail.strip_edges();
    }
    return preview;
}

/// Convert current named values to the graph inspector's display format.
Array inspect_rows(const Dictionary &values) {
    Array rows;
    for (const Variant &key : values.keys()) {
        if (rows.size() == row_limit) {
            rows.append(visual_debug_row("More values", values.size() - row_limit));
            break;
        }
        rows.append(visual_debug_row(String(key), values[key]));
    }
    return rows;
}

} // namespace

int32_t visual_breakpoint_line(int64_t graph, int64_t node) {
    return graph >= 0 && graph < 32768 && node > 0 && node < graph_stride ?
            static_cast<int32_t>(graph * graph_stride + node) : 0;
}

bool visual_breakpoint_address(int32_t line, int64_t &graph, int64_t &node) {
    graph = line / graph_stride;
    node = line % graph_stride;
    return line > 0 && node > 0;
}

EngineDebugger *visual_debugger() {
    auto *debugger = EngineDebugger::get_singleton();
    if (!debugger || !debugger->is_active() || paused_frame || Engine::get_singleton()->is_editor_hint()) {
        return nullptr;
    }
    auto *os = OS::get_singleton();
    return os->get_thread_caller_id() == os->get_main_thread_id() ? debugger : nullptr;
}

void initialize_visual_debugger() {
    if (auto *debugger = EngineDebugger::get_singleton(); debugger && debugger->is_active() &&
            !Engine::get_singleton()->is_editor_hint()) {
        debugger->register_message_capture("crimson_visual", callable_mp_static(receive_breakpoint));
    }
}

void shutdown_visual_debugger() {
    if (auto *debugger = EngineDebugger::get_singleton(); debugger && debugger->has_capture("crimson_visual")) {
        debugger->unregister_message_capture("crimson_visual");
    }
}

VisualDebugScope::VisualDebugScope(int32_t depth_offset) : debugger(visual_debugger()), offset(depth_offset) {
    if (debugger && debugger->get_lines_left() > 0 && debugger->get_depth() >= 0) {
        const int32_t depth = debugger->get_depth() + offset;
        debugger->set_depth(depth < 0 ? 0 : depth);
    }
}

VisualDebugScope::~VisualDebugScope() {
    if (debugger && debugger->get_lines_left() > 0 && debugger->get_depth() >= 0) {
        debugger->set_depth(debugger->get_depth() - offset);
    }
}

bool visual_debug_should_break(EngineDebugger &debugger, const String &script, int32_t line) {
    debugger.line_poll();
    if (line <= 0 || script.is_empty() || debugger.is_skipping_breakpoints()) {
        return false;
    }
    const bool stepping = debugger.get_lines_left() > 0 && debugger.get_depth() <= 0;
    if (stepping) {
        debugger.set_lines_left(debugger.get_lines_left() - 1);
    }
    return (stepping && debugger.get_lines_left() == 0) || debugger.is_breakpoint(line, script);
}

Dictionary visual_debug_row(const String &name, const Variant &value) {
    Dictionary row;
    row["name"] = bounded(name);
    row["type"] = Variant::get_type_name(value.get_type());
    const String display = preview_value(value);
    row["value"] = bounded(display.replace("\n", " ").replace("\r", ""));
    row["detail"] = bounded(value_detail(value, display));
    return row;
}

void visual_debug_break(EngineDebugger &debugger, const VisualDebugFrame &frame) {
    Dictionary hit;
    hit["script"] = frame.script;
    hit["graph"] = frame.graph;
    hit["node"] = frame.node;
    hit["instance"] = frame.instance;
    hit["instance_label"] = frame.instance_label;
    hit["title"] = frame.title;
    hit["node_title"] = frame.node_title;
    hit["inputs"] = frame.input_rows;
    hit["input_error"] = frame.input_error;
    hit["computed"] = frame.computed;
    hit["locals"] = inspect_rows(frame.locals);
    hit["members"] = inspect_rows(frame.members);
    hit["stack"] = frame.callers;
    hit["pins"] = frame.pins;
    hit["active_graphs"] = frame.active_graphs;
    hit["limited_graphs"] = frame.limited_graphs;
    debugger.send_message("crimson_visual:hit", Array::make(hit));
    paused_frame = &frame;
    inspection_sent = false;
    debugger.script_debug(visual_script_language(), true, false);
    paused_frame = nullptr;
    debugger.send_message("crimson_visual:resumed", {});
}

const VisualDebugFrame *paused_visual_debug_frame(int32_t level) {
    return level == 0 ? paused_frame : nullptr;
}

void visual_debug_inspection_ready(int32_t level) {
    if (level == 0 && paused_frame && !inspection_sent) {
        inspection_sent = true;
        EngineDebugger::get_singleton()->send_message("crimson_visual:inspection_ready", {});
    }
}

Dictionary visual_debug_values(const String &key, const Dictionary &values) {
    PackedStringArray names;
    Array entries;
    for (const Variant &entry : values.keys()) {
        names.append(String(entry));
        entries.append(values[entry]);
    }
    Dictionary result;
    result[key] = names;
    result["values"] = entries;
    return result;
}

} // namespace crimson::visual_scripting

#endif
