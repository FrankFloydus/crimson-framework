// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>

namespace godot {
class Button;
class Node;
}

namespace crimson::visual_scripting {

/// Distinguish a name argument from a typed tag or collection default.
enum class TagFieldKind { NONE, NAME, TAG, CONTAINER, ARRAY };

/// Use a tag picker for exact tag defaults and known catalog lookup arguments.
TagFieldKind tag_field_kind(const godot::Dictionary &pin, const godot::Dictionary &node = {});

/// Attach the shared Gameplay Tags picker and submit the new and previously displayed values.
godot::Button *tag_field(godot::Node *parent, TagFieldKind kind, const godot::Variant &stored,
        const std::function<bool(const godot::Variant &, const godot::Variant &)> &change);

/// Register the internal button during editor initialization.
void register_visual_script_tag_field();

} // namespace crimson::visual_scripting

#endif
