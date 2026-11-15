// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/editor_debugger_plugin.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <map>
#include <set>

namespace crimson::visual_scripting {

class VisualScriptWindow;

/// Adapt Godot's remote debug sessions to resource-scoped visual graph breakpoints.
class VisualScriptEditorDebugger final : public godot::EditorDebuggerPlugin {
    GDCLASS(VisualScriptEditorDebugger, godot::EditorDebuggerPlugin)

    friend class VisualScriptWindow;

    /// Graph editor owned by the editor plugin; valid for the lifetime of this adapter.
    VisualScriptWindow *window = nullptr;
    /// Project-local sets of positive virtual source lines, keyed by resource path.
    godot::Dictionary breakpoints;
    /// Project-local watch identities keyed by script, separate from runtime values and graph assets.
    godot::Dictionary watches;
    /// Most recent node location received from each active debugger session.
    std::map<int32_t, godot::Dictionary> hits;
    /// Active graph hit currently shown in the editor.
    int32_t displayed_session = -1;
    /// Active Godot game sessions, tracked separately from their current breakpoint.
    std::set<int32_t> running_sessions;
    /// Sessions waiting for Godot to acknowledge a Continue or Step command.
    std::set<int32_t> pending_sessions;
    /// Pauses whose native stack inspection is in progress or complete.
    std::set<int32_t> inspection_ready;

    /// Present a pause after Godot confirms that the game entered the native debugger.
    void session_broke(bool can_debug, int32_t session_id);
    /// Populate the native breakpoint list when a new process joins this debugger session.
    void session_started(int32_t session_id);
    /// Remove game state when a session continues or stops.
    void session_released(int32_t session_id);
    /// Remove a disconnected game from the run status.
    void session_stopped(int32_t session_id);
    /// Update the graph toolbar using the displayed session rather than another running game.
    void refresh_run_state();
    /// Save breakpoints separately from game assets and editor Undo history.
    void save_breakpoints() const;
    /// Publish watch addresses without writing any observed game values.
    void save_watches() const;

protected:
    /// This editor integration does not add public script bindings.
    static void _bind_methods() {}

public:
    /// Read this editor project's saved breakpoints and attach its graph window.
    void attach(VisualScriptWindow *graph_window);
    /// Obtain the graph debugger attached to this editor plugin.
    static VisualScriptEditorDebugger *get_active();
    /// Retire the adapter before the Godot editor frees the graph window.
    void detach();
    /// Format the breakpoints passed to a new run by Godot's EditorPlugin.
    godot::PackedStringArray launch_breakpoints() const;
    /// Check if this script and virtual line has an editor breakpoint.
    bool has_breakpoint(const godot::String &path, int32_t line) const;
    /// Update editor-owned breakpoint state and propagate it to each running session.
    void set_breakpoint(const godot::String &path, int32_t line, bool enabled);
    /// Remove breakpoints whose authored node was deleted so its ID cannot be picked up by a new node.
    void prune_breakpoints(const godot::String &path, const godot::Dictionary &document);
    /// Return this script's saved watches.
    godot::Array watch_records(const godot::String &path) const;
    /// Add or remove a pin watch by saved script, graph, node, direction and port ID.
    void set_watch(const godot::Dictionary &pin, bool enabled);
    /// Drop watches on deleted nodes and removed ports before an ID can be reused.
    void prune_watches(const godot::String &path, const godot::Dictionary &document, const godot::StringName &base);
    /// Resume or step the main thread that owns the displayed visual pause.
    void command(const godot::StringName &action);
    /// Preserve Godot's breakpoints panel commands, whose callback lines are zero based.
    void _breakpoint_set_in_tree(const godot::Ref<godot::Script> &script, int32_t line, bool enabled) override;
    /// Clear the same points if the native debugger removes all breakpoints.
    void _breakpoints_cleared_in_tree() override;
    /// Route custom graph hit messages from Godot's debugger.
    bool _has_capture(const godot::String &prefix) const override;
    /// Record the latest hit for the correct running game.
    bool _capture(const godot::String &message, const godot::Array &data, int32_t session_id) override;
    /// Listen for the debugger's pause, continue, and stop lifecycle.
    void _setup_session(int32_t session_id) override;
};

} // namespace crimson::visual_scripting

#endif
