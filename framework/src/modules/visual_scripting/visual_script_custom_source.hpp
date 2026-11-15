// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace crimson::visual_scripting {

/// Select executable GDScript or an analyzer-compatible document with the same declarations.
enum class CustomScriptSourcePurpose { RUNTIME, ANALYSIS };

/// One GDScript document used by both the runtime and Godot's editor language server.
struct CustomScriptSource {
    /// Generated GDScript, including the typed graph context and the authored body.
    godot::String text;
    /// Zero-based line on which the executable body starts in the generated document.
    int body_line = 0;
    /// Standard number of Unicode code points prepended to an authored line.
    int body_indent = 4;
    /// Actual per-line prefix widths, including zero inside continued string literals.
    godot::PackedInt32Array body_columns;
    /// Absolute generated line for each authored row, including rows moved to helper declarations.
    godot::PackedInt32Array body_lines;
    /// Whether the invocation supplies an absence mask before the ordinary input arguments.
    bool uses_input_defaults = false;
    /// Details when the pin declaration cannot generate a valid signature.
    godot::String error;
    /// Notes for graph members whose GDScript name differs from their declared name.
    godot::PackedStringArray aliases;
};

/// Check whether an authored pin may use a name without colliding with the wrapper or GDScript.
bool custom_script_identifier_available(const godot::String &name);

/// Merge source-declared pins with the node's manual pins without modifying the supplied node.
/// An invalid directive returns an empty dictionary and a diagnostic with the authored line number.
godot::Dictionary reconcile_custom_script_pins(const godot::Dictionary &node, godot::String &error);

/// Generate one typed GDScript body, retaining stable graph declaration IDs in its callbacks.
CustomScriptSource build_custom_script_source(const godot::Dictionary &document, const godot::Dictionary &graph_body,
        const godot::Dictionary &node, const godot::String &owner_base, CustomScriptSourcePurpose purpose = CustomScriptSourcePurpose::RUNTIME);

} // namespace crimson::visual_scripting
