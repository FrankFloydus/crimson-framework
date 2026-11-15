// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace crimson::visual_scripting {

/// Encode selected nodes and their internal wires for the same open script and graph.
godot::String encode_graph_clipboard(const godot::Dictionary &document, int64_t graph_id, uint64_t script_instance_id,
        const godot::PackedInt64Array &selection, godot::String &error);

/// Recognize a browser clipboard containing a whole Visual Script document.
bool is_visual_script_document_clipboard(const godot::String &clipboard_text);

/// Decode and validate a standalone clipboard document for the destination's native base.
godot::Dictionary decode_visual_script_document(const godot::String &clipboard_text,
        const godot::StringName &base_class, godot::String &error);

/// Describe named declaration conflicts in the order the caller supplies their choices.
godot::Array visual_script_document_conflicts(const godot::Dictionary &destination, const godot::Dictionary &source,
        const godot::StringName &base_class, godot::String &error, const godot::Callable &export_check = {});

/// Stage an entire Event Graph insertion with all declaration choices resolved. Neither input is modified.
godot::Dictionary merge_visual_script_document(const godot::Dictionary &destination, const godot::Dictionary &source,
        const godot::StringName &base_class, const godot::Vector2 &anchor, const godot::PackedStringArray &choices,
        godot::PackedInt64Array &created, godot::String &error, int64_t *converted_lifecycle = nullptr,
        const godot::Callable &export_check = {});

/// Paste a validated selection with its upper-left position at anchor. Return the new document and IDs.
godot::Dictionary paste_graph_clipboard(const godot::Dictionary &document, int64_t graph_id, uint64_t script_instance_id,
        const godot::String &clipboard_text, const godot::Vector2 &anchor, godot::PackedInt64Array &created,
        godot::String &error, const godot::StringName &base_class = "Node");

/// Remove the selection and its attached wires after the caller has copied it successfully.
godot::Dictionary cut_graph_selection(const godot::Dictionary &document, int64_t graph_id,
        const godot::PackedInt64Array &selection, godot::String &error);

/// Clone the selection at an offset. Optionally reuse external data sources without touching their existing wires.
godot::Dictionary duplicate_graph_selection(const godot::Dictionary &document, int64_t graph_id,
        const godot::PackedInt64Array &selection, const godot::Vector2 &offset, bool keep_external_data_inputs,
        godot::PackedInt64Array &created, godot::String &error, const godot::StringName &base_class = "Node");

} // namespace crimson::visual_scripting

#endif
