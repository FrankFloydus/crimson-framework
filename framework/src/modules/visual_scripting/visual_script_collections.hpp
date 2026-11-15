// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace crimson::visual_scripting {

/// Maximum authored entries in a collection constructor.
constexpr int64_t visual_script_max_collection_size = 32;

/// Collection operations supported by the graph's shared pin schema and runtime.
enum class CollectionOperation {
    NONE, MAKE_ARRAY, MAKE_DICTIONARY, GET_ARRAY_ITEM, GET_DICTIONARY_VALUE,
    SET_ARRAY_ITEM, APPEND_ARRAY_ITEM, REMOVE_ARRAY_ITEM, SET_DICTIONARY_VALUE, ERASE_DICTIONARY_KEY,
};

/// Resolve a saved node kind as a collection operation.
CollectionOperation collection_operation(const godot::StringName &kind);

/// Return whether the operation emits execution and edits a copy.
bool collection_operation_edits(CollectionOperation operation);

/// Return the container type or NIL when the kind is not a collection operation.
godot::Variant::Type collection_type(CollectionOperation operation);

/// Return the stable input that supplies a container, or an empty name for a constructor.
godot::StringName collection_source_input(CollectionOperation operation);

/// Return whether a constructor arity is missing or in its supported range.
bool valid_collection_arity(const godot::Dictionary &node);

/// Describe the pins in constructor or operation order for the inferred source type.
godot::Array collection_operation_pins(CollectionOperation operation, const godot::Dictionary &node,
        bool output, const godot::Dictionary &source);

/// Identify an existing item, key, or value input on a collection constructor.
bool is_collection_constructor_input(const godot::Dictionary &node, const godot::StringName &pin);

/// Copy a pin's native terminal identity without its graph-specific fields.
godot::Dictionary collection_terminal_type(const godot::Dictionary &pin);

/// Describe an authored value's native terminal type, or return empty for an unset literal.
godot::Dictionary collection_literal_type(const godot::Variant &literal);

/// Infer safe native terminal types for a legacy Make Dictionary node when adding an entry.
godot::Dictionary infer_legacy_dictionary_types(const godot::Dictionary &document, const godot::Dictionary &graph,
        const godot::Dictionary &node, const godot::StringName &base);

/// Establish an unresolved Make Dictionary terminal or keep its existing type.
void set_dictionary_constructor_wire_type(godot::Dictionary &node, const godot::StringName &pin, const godot::Dictionary &source);

/// Establish or validate a Make Dictionary terminal before saving a literal.
bool set_dictionary_constructor_literal_type(godot::Dictionary &node, const godot::StringName &pin, const godot::Variant &value);

/// Append an entry, or remove the selected item or pair and remap its surviving inputs and wires.
bool resize_collection_constructor(godot::Dictionary &graph, int64_t node_id, const godot::StringName &remove_pin,
        const godot::Dictionary &document, const godot::StringName &base);

/// Run a collection operation and provide its named outputs without changing the input container.
godot::String evaluate_collection_operation(CollectionOperation operation, const godot::Array &inputs,
        godot::Dictionary &outputs, const godot::Dictionary *dictionary_template = nullptr);

} // namespace crimson::visual_scripting
