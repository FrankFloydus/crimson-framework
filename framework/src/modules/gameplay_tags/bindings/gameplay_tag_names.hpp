// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "gameplay_tag_resources.hpp"

namespace crimson::gameplay_tags::bindings {

// Project setting containing explicit catalog names.
inline constexpr char definitions_setting[] = "crimson/gameplay_tags/definitions";
// Project setting mapping retired canonical names directly to current paths.
inline constexpr char redirects_setting[] = "crimson/gameplay_tags/redirects";

// Parses a Godot name as a validated native path.
TagValue parse_tag_name(const godot::StringName &name);
// Converts a canonical native tag to its Godot name.
godot::StringName canonical_tag_name(const TagValue &tag);
// Resolves a canonical name using a snapshot of the flattened redirect table.
TagValue resolve_tag_name(TagValue tag, const godot::Dictionary &redirects);
// Parses a serialized name and applies the current project's rename redirects.
TagValue read_tag_name(const godot::StringName &name);
// Wraps native tags in a typed Godot array while preserving order.
godot::TypedArray<GameplayTag> wrap_tags(const std::vector<TagValue> &tags);

} // namespace crimson::gameplay_tags::bindings
