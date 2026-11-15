// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_debug_navigation.hpp"

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/editor/visual_script_window.hpp"
#include "modules/visual_scripting/visual_script_debugger.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"

#include <godot_cpp/classes/editor_debugger_session.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// One active editor adapter per Godot process.
VisualScriptEditorDebugger *active_debugger = nullptr;
/// Store personal breakpoints with the local editor project, never in graph resources.
constexpr char settings_section[] = "crimson_visual_scripting";
/// Metadata key for the graph breakpoint table.
constexpr char settings_key[] = "breakpoints";
/// Local debugger preferences hold addresses only.
constexpr char watch_key[] = "watches";

/// Compare stable addresses irrespective of display name or tag.
bool same_watch(const Dictionary &first, const Dictionary &second) {
    return int64_t(first.get("graph", -1)) == int64_t(second.get("graph", -1)) &&
            int64_t(first.get("node", 0)) == int64_t(second.get("node", 0)) &&
            bool(first.get("output", false)) == bool(second.get("output", false)) &&
            StringName(first.get("pin", StringName())) == StringName(second.get("pin", StringName()));
}

/// Send an update only to a running game; launch points are supplied separately by Godot.
void update_session(const Ref<EditorDebuggerSession> &session, const String &path, int32_t line, bool enabled) {
    if (session.is_valid() && session->is_active()) {
        session->send_message("crimson_visual:breakpoint", Array::make(path, line, enabled));
    }
}

} // namespace

void VisualScriptEditorDebugger::attach(VisualScriptWindow *graph_window) {
    window = graph_window;
    active_debugger = this;
    const Ref<EditorSettings> settings = EditorInterface::get_singleton()->get_editor_settings();
    if (settings.is_valid()) {
        const Variant stored = settings->get_project_metadata(settings_section, settings_key, Dictionary());
        if (stored.get_type() == Variant::DICTIONARY) {
            breakpoints = stored;
        }
        const Variant watched = settings->get_project_metadata(settings_section, watch_key, Dictionary());
        if (watched.get_type() == Variant::DICTIONARY) {
            watches = watched;
        }
    }
}

VisualScriptEditorDebugger *VisualScriptEditorDebugger::get_active() {
    return active_debugger;
}

void VisualScriptEditorDebugger::detach() {
    window = nullptr;
    active_debugger = nullptr;
    hits.clear();
    running_sessions.clear();
    displayed_session = -1;
    pending_sessions.clear();
    inspection_ready.clear();
}

void VisualScriptEditorDebugger::save_breakpoints() const {
    const Ref<EditorSettings> settings = EditorInterface::get_singleton()->get_editor_settings();
    if (settings.is_valid()) {
        settings->set_project_metadata(settings_section, settings_key, breakpoints);
    }
}

void VisualScriptEditorDebugger::save_watches() const {
    const Ref<EditorSettings> settings = EditorInterface::get_singleton()->get_editor_settings();
    if (settings.is_valid()) {
        settings->set_project_metadata(settings_section, watch_key, watches);
    }
}

Array VisualScriptEditorDebugger::watch_records(const String &path) const {
    const Variant found = watches.get(path, Array());
    return found.get_type() == Variant::ARRAY ? Array(found) : Array();
}

void VisualScriptEditorDebugger::set_watch(const Dictionary &pin, bool enabled) {
    const String path = pin.get("script", String());
    if (!path.begins_with("res://") || int64_t(pin.get("node", 0)) <= 0 ||
            int64_t(pin.get("graph", -1)) < 0 || StringName(pin.get("pin", StringName())).is_empty()) {
        return;
    }
    const Array previous = watch_records(path);
    Array updated;
    bool found = false;
    for (const Variant &entry : previous) {
        if (entry.get_type() != Variant::DICTIONARY) continue;
        const Dictionary row = entry;
        if (same_watch(row, pin)) {
            if (enabled) return;
            found = true;
        } else {
            updated.append(row);
        }
    }
    if (enabled) {
        if (updated.size() >= 128) {
            if (window) window->show_problem("This script already has 128 watched pins. Remove a watch to add another.");
            return;
        }
        Dictionary address;
        for (const char *field : {"script", "graph", "node", "output", "pin", "name", "title", "type"}) {
            if (pin.has(field)) address[field] = pin[field];
        }
        updated.append(address);
    } else if (!found) {
        return;
    }
    if (updated.is_empty()) watches.erase(path);
    else watches[path] = updated;
    save_watches();
}

void VisualScriptEditorDebugger::prune_watches(const String &path, const Dictionary &document, const StringName &base) {
    const Array previous = watch_records(path);
    if (previous.is_empty()) return;
    Array retained;
    for (const Variant &entry : previous) {
        if (entry.get_type() != Variant::DICTIONARY) continue;
        const Dictionary watch = entry;
        const Dictionary graph = graph_body(document, watch.get("graph", 0));
        const Dictionary node = find_graph_record(graph.get("nodes", Array()), watch.get("node", 0));
        if (node.is_empty()) continue;
        for (const Dictionary &port : graph_pins(document, graph, node, watch.get("output", false), base)) {
            if (!bool(port.get("exec", false)) &&
                    StringName(port.get("id", StringName())) == StringName(watch.get("pin", StringName()))) {
                retained.append(watch);
                break;
            }
        }
    }
    if (retained.size() == previous.size()) return;
    if (retained.is_empty()) watches.erase(path);
    else watches[path] = retained;
    save_watches();
}

PackedStringArray VisualScriptEditorDebugger::launch_breakpoints() const {
    PackedStringArray result;
    for (const Variant &key : breakpoints.keys()) {
        const String path = key;
        for (int32_t line : PackedInt32Array(breakpoints[key])) {
            result.append(path + String(":") + String::num_int64(line));
        }
    }
    return result;
}

bool VisualScriptEditorDebugger::has_breakpoint(const String &path, int32_t line) const {
    return line > 0 && PackedInt32Array(breakpoints.get(path, PackedInt32Array())).has(line);
}

void VisualScriptEditorDebugger::set_breakpoint(const String &path, int32_t line, bool enabled) {
    if (!path.begins_with("res://") || path.contains(",") || path.contains("%20") ||
            line <= 0 || has_breakpoint(path, line) == enabled) {
        return;
    }
    PackedInt32Array points = breakpoints.get(path, PackedInt32Array());
    if (enabled) {
        points.append(line);
    } else {
        points.remove_at(points.find(line));
    }
    if (points.is_empty()) {
        breakpoints.erase(path);
    } else {
        breakpoints[path] = points;
    }
    save_breakpoints();
    for (const Variant &value : get_sessions()) {
        const Ref<EditorDebuggerSession> session = value;
        update_session(session, path, line, enabled);
    }
    if (window) {
        window->refresh_debug_markers();
    }
}

void VisualScriptEditorDebugger::prune_breakpoints(const String &path, const Dictionary &document) {
    const PackedInt32Array points = breakpoints.get(path, PackedInt32Array());
    for (int32_t line : points) {
        int64_t graph = 0;
        int64_t node = 0;
        if (!visual_breakpoint_address(line, graph, node) ||
                find_graph_record(graph_body(document, graph).get("nodes", Array()), node).is_empty()) {
            set_breakpoint(path, line, false);
        }
    }
}

void VisualScriptEditorDebugger::_breakpoint_set_in_tree(const Ref<Script> &resource, int32_t line, bool enabled) {
    if (resource.is_valid() && Object::cast_to<VisualScript>(resource.ptr())) {
        set_breakpoint(resource->get_path(), line + 1, enabled);
    }
}

void VisualScriptEditorDebugger::_breakpoints_cleared_in_tree() {
    const Dictionary previous = breakpoints.duplicate();
    breakpoints.clear();
    for (const Variant &key : previous.keys()) {
        const String path = key;
        for (const Variant &value : get_sessions()) {
            const Ref<EditorDebuggerSession> session = value;
            for (int32_t line : PackedInt32Array(previous[key])) {
                update_session(session, path, line, false);
            }
        }
    }
    save_breakpoints();
    if (window) {
        window->refresh_debug_markers();
    }
}

bool VisualScriptEditorDebugger::_has_capture(const String &prefix) const {
    return prefix == "crimson_visual";
}

bool VisualScriptEditorDebugger::_capture(const String &message, const Array &data, int32_t session_id) {
    if (message == "crimson_visual:resumed") {
        session_released(session_id);
        return true;
    }
    if (message == "crimson_visual:inspection_ready") {
        if (hits.count(session_id) != 0) {
            inspection_ready.insert(session_id);
            refresh_run_state();
        }
        return true;
    }
    if (message != "crimson_visual:hit" || data.size() != 1 || data[0].get_type() != Variant::DICTIONARY) {
        return false;
    }
    hits[session_id] = data[0];
    const Ref<EditorDebuggerSession> session = get_session(session_id);
    if (session.is_valid() && session->is_breaked()) {
        session_broke(true, session_id);
    }
    return true;
}

void VisualScriptEditorDebugger::_setup_session(int32_t session_id) {
    const Ref<EditorDebuggerSession> session = get_session(session_id);
    if (session.is_valid()) {
        session->connect("started", callable_mp(this, &VisualScriptEditorDebugger::session_started).bind(session_id));
        session->connect("breaked", callable_mp(this, &VisualScriptEditorDebugger::session_broke).bind(session_id));
        session->connect("continued", callable_mp(this, &VisualScriptEditorDebugger::session_released).bind(session_id));
        session->connect("stopped", callable_mp(this, &VisualScriptEditorDebugger::session_stopped).bind(session_id));
        if (session->is_active()) {
            session_started(session_id);
        }
    }
}

void VisualScriptEditorDebugger::session_started(int32_t session_id) {
    const Ref<EditorDebuggerSession> session = get_session(session_id);
    if (session.is_null()) {
        return;
    }
    running_sessions.insert(session_id);
    refresh_run_state();
    for (const Variant &key : breakpoints.keys()) {
        const String path = key;
        for (int32_t line : PackedInt32Array(breakpoints[key])) {
            update_session(session, path, line, true);
        }
    }
}

void VisualScriptEditorDebugger::session_broke(bool can_debug, int32_t session_id) {
    const auto current = hits.find(session_id);
    if (window && can_debug && current != hits.end()) {
        if (displayed_session == session_id && pending_sessions.count(session_id) == 0) {
            return;
        }
        pending_sessions.erase(session_id);
        displayed_session = session_id;
        window->debug_focus_hit(current->second);
        refresh_run_state();
    }
}

void VisualScriptEditorDebugger::session_released(int32_t session_id) {
    pending_sessions.erase(session_id);
    inspection_ready.erase(session_id);
    hits.erase(session_id);
    if (window && displayed_session == session_id) {
        displayed_session = -1;
        for (const auto &[other, hit] : hits) {
            const Ref<EditorDebuggerSession> session = get_session(other);
            if (session.is_valid() && session->is_breaked()) {
                displayed_session = other;
                window->debug_focus_hit(hit);
                refresh_run_state();
                return;
            }
        }
        window->debug_focus_hit(Dictionary());
    }
    refresh_run_state();
}

void VisualScriptEditorDebugger::session_stopped(int32_t session_id) {
    running_sessions.erase(session_id);
    session_released(session_id);
}

void VisualScriptEditorDebugger::refresh_run_state() {
    if (window) {
        const auto current = hits.find(displayed_session);
        const bool paused = current != hits.end() && pending_sessions.count(displayed_session) == 0;
        window->debug_set_run_state(!running_sessions.empty() || displayed_session >= 0, paused,
                paused && inspection_ready.count(displayed_session) != 0);
        if (paused) {
            window->debug_apply_snapshot(current->second);
        }
    }
}

void VisualScriptEditorDebugger::command(const StringName &action) {
    if (displayed_session < 0 || pending_sessions.count(displayed_session) != 0 ||
            inspection_ready.count(displayed_session) == 0) {
        return;
    }
    const Ref<EditorDebuggerSession> session = get_session(displayed_session);
    if (session.is_null() || !session->is_active() || !session->is_breaked()) {
        return;
    }
    const String native = action == StringName("continue") ? String("continue") :
            action == StringName("step_over") ? String("next") :
            action == StringName("step_into") ? String("step") :
            action == StringName("step_out") ? String("out") : String();
    if (!native.is_empty()) {
        // Visual checkpoints only run on the game main thread, the target of send_message.
        pending_sessions.insert(displayed_session);
        refresh_run_state();
        session->send_message(native, {});
    }
}

bool VisualScriptWindow::debug_has_breakpoint(int64_t node) const {
    const auto *debugger = VisualScriptEditorDebugger::get_active();
    return debugger && script.is_valid() && debugger->has_breakpoint(script->get_path(), visual_breakpoint_line(active_graph, node));
}

void VisualScriptWindow::debug_set_breakpoint(int64_t node, bool enabled) {
    auto *debugger = VisualScriptEditorDebugger::get_active();
    if (!debugger || script.is_null()) {
        return;
    }
    const String path = script->get_path();
    const int32_t line = visual_breakpoint_line(active_graph, node);
    if (line == 0 || !path.begins_with("res://") || path.contains(",") || path.contains("%20")) {
        show_problem("Cannot add a breakpoint at this address. Save the script with a project path without commas or %20.");
        return;
    }
    debugger->set_breakpoint(path, line, enabled);
}

bool VisualScriptWindow::debug_is_current(int64_t node) const {
    return script.is_valid() && String(debug_hit.get("script", String())) == script->get_path() &&
            static_cast<int64_t>(debug_hit.get("graph", -1)) == active_graph &&
            static_cast<int64_t>(debug_hit.get("node", 0)) == node;
}

void VisualScriptWindow::debug_command(const StringName &action) {
    if (auto *debugger = VisualScriptEditorDebugger::get_active()) {
        debugger->command(action);
    }
}

Array VisualScriptWindow::debug_watch_records(const String &path) const {
    const auto *debugger = VisualScriptEditorDebugger::get_active();
    return debugger ? debugger->watch_records(path) : Array();
}

void VisualScriptWindow::debug_watch_set(const Dictionary &address, bool enabled) {
    if (auto *debugger = VisualScriptEditorDebugger::get_active()) {
        debugger->set_watch(address, enabled);
        debugger->refresh_run_state();
    }
}

void VisualScriptWindow::debug_watch_erase_missing(const String &path, const Dictionary &document) {
    if (auto *debugger = VisualScriptEditorDebugger::get_active(); debugger && script.is_valid()) {
        debugger->prune_watches(path, document, script->get_graph_base());
    }
}

void VisualScriptWindow::debug_prune_breakpoints() {
    if (auto *debugger = VisualScriptEditorDebugger::get_active(); debugger && script.is_valid()) {
        debugger->prune_breakpoints(script->get_path(), script->get_graph());
    }
}

void VisualScriptWindow::debug_focus_hit(const Dictionary &hit) {
    debug_hit = hit;
    refresh_debug_markers();
    const String path = hit.get("script", String());
    if (path.is_empty() || (!unsaved_status().is_empty() && script->get_path() != path)) {
        return;
    }
    const Ref<VisualScript> resource = script.is_valid() && script->get_path() == path ? script :
            Ref<VisualScript>(ResourceLoader::get_singleton()->load(path, "VisualScript"));
    if (resource.is_null()) {
        return;
    }
    open(resource);
    const auto graph = static_cast<int64_t>(hit.get("graph", -1));
    const auto node = static_cast<int64_t>(hit.get("node", 0));
    const Dictionary body = graph_body(resource->get_graph(), graph);
    if (find_graph_record(body.get("nodes", Array()), node).is_empty()) {
        return;
    }
    GraphLocation location = graph_location();
    location.graph = graph;
    location.selection.clear();
    location.selection.append(node);
    restore_location(location);
    callable_mp(this, &VisualScriptWindow::frame_nodes).bind(true).call_deferred();
}

} // namespace crimson::visual_scripting

#endif
