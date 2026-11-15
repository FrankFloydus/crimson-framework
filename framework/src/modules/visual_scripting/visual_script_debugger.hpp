// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {
class EngineDebugger;
}

namespace crimson::visual_scripting {

/// Values from a single paused graph frame, valid only while Godot handles the break.
struct VisualDebugFrame {
    /// Real resource path, also used by Godot to match breakpoints.
    godot::String script;
    /// Graph definition name, or Event Graph.
    godot::String title;
    /// Name shown on the paused card.
    godot::String node_title;
    /// Node that owns the live script instance.
    godot::String instance;
    /// Short display name of the scene instance.
    godot::String instance_label;
    /// Graph definition ID; zero identifies the Event Graph.
    int64_t graph = 0;
    /// Graph-local ID of the node about to run.
    int64_t node = 0;
    /// Resolved input pins (or the event/entry arguments) about to be consumed.
    godot::Dictionary inputs;
    /// Input argument rows with their source and the paused card identity.
    godot::Array input_rows;
    /// The failure of an unresolved input, separate from the captured values.
    godot::String input_error;
    /// Actual upstream and previously published outputs shown in the graph sidebar.
    godot::Array computed;
    /// Local variables owned by the suspended invocation.
    godot::Dictionary locals;
    /// Variables shared by the invocations on this instance.
    godot::Dictionary members;
    /// The actual graph call chain, nearest caller first, for the in-graph stack view.
    godot::Array callers;
    /// Captured input and output pins across the active invocations.
    godot::Array pins;
    /// Active graph IDs, nearest invocation first, for watch availability.
    godot::Array active_graphs;
    /// Graphs whose pin rows exceeded the capture or transport limit.
    godot::Array limited_graphs;
};

/// Encode a graph address in the positive signed line numbers Godot accepts; zero is unavailable.
int32_t visual_breakpoint_line(int64_t graph, int64_t node);

/// Recover the authored graph definition and node ID from a positive virtual line.
bool visual_breakpoint_address(int32_t line, int64_t &graph, int64_t &node);

/// Return the Godot debugger for a live game on the main thread, or null outside that context.
godot::EngineDebugger *visual_debugger();

/// Register and retire the live-update debugger namespace with the graph language.
void initialize_visual_debugger();
void shutdown_visual_debugger();

/// Keep Godot's native step depth in sync with nested visual graph invocations.
class VisualDebugScope {
    /// Native debugger, retained by Godot for the process.
    godot::EngineDebugger *debugger;
    /// Depth contribution, negative when following a macro exit into the caller.
    int32_t offset;

public:
    /// Enter an invocation while a native Step Over or Step Out is in progress.
    explicit VisualDebugScope(int32_t offset = 1);
    /// Leave the invocation, including one where stepping began after entry.
    ~VisualDebugScope();
};

/// Poll Godot and check the explicit breakpoint or the native step state at this address.
bool visual_debug_should_break(godot::EngineDebugger &debugger, const godot::String &script, int32_t line);

/// Format one bounded and transport-safe row without dereferencing arbitrary game properties.
godot::Dictionary visual_debug_row(const godot::String &name, const godot::Variant &value);

/// Send the address to the graph editor and let Godot suspend the game at this frame.
void visual_debug_break(godot::EngineDebugger &debugger, const VisualDebugFrame &frame);

/// Read the snapshot of the frame that is currently suspended by Godot.
const VisualDebugFrame *paused_visual_debug_frame(int32_t level = 0);

/// Acknowledge that the native editor has requested the first frame's stack variables.
void visual_debug_inspection_ready(int32_t level);

/// Unpack a named set of values into Godot's ScriptLanguageExtension debugger format.
godot::Dictionary visual_debug_values(const godot::String &key, const godot::Dictionary &values);

} // namespace crimson::visual_scripting

#endif
