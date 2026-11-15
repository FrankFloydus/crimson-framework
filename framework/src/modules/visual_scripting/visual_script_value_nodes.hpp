// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <array>

namespace crimson::visual_scripting {

/// Godot value types whose native fields can be assembled and split in the graph.
const std::array<godot::Variant::Type, 16> &supported_visual_value_types();

/// Decode the stable saved type for a Make or Break node, or return VARIANT_MAX.
godot::Variant::Type visual_value_type(const godot::Dictionary &node);

/// Produce matching typed component pins and native defaults for either operation.
godot::Array visual_value_pins(godot::Variant::Type type, bool make, bool output);

/// Set one native field on the new value and report when Godot rejects it.
bool write_visual_value_component(godot::Variant &value, const godot::StringName &field, const godot::Variant &component);

/// Start Make from the same initial value as the corresponding empty Godot constructor.
godot::Variant default_visual_value(godot::Variant::Type type);

} // namespace crimson::visual_scripting
