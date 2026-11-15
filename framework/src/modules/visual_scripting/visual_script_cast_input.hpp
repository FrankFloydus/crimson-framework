// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>

namespace crimson::visual_scripting {

/// A native Godot Input Map query resolved while the graph compiles.
enum class InputPollOperation : uint8_t {
    Invalid, Pressed, JustPressed, JustReleased, Strength, Axis, Vector,
};

/// Describe the action and data sockets of a cast to the node's authored Object type.
godot::Array cast_pins(const godot::Dictionary &node, bool output);

/// Check that a cast's native class and optional external script agree.
godot::String validate_cast_node(const godot::Dictionary &node);

/// Expose all available Input Map queries as graph node records.
godot::Array find_input_poll_nodes();

/// Decode the stable operation name stored in an Input Map query node.
InputPollOperation input_poll_operation(const godot::StringName &operation);

/// Describe the action names, optional settings, and result of the selected query.
godot::Array input_poll_pins(InputPollOperation operation, bool output);

/// Sample Godot's Input state, returning the type's neutral value for an unavailable action.
godot::Variant read_input_poll(InputPollOperation operation, const godot::Variant *const *arguments);

} // namespace crimson::visual_scripting
