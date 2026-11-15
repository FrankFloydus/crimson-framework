// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::visual_scripting {

/// Give a script-local enum a stable qualified identity independent of its visible name.
godot::StringName authored_enum_identity(int64_t id);

/// Resolve the enum's document-local ID, returning zero for reflected Godot enums.
int64_t authored_enum_id(const godot::StringName &qualified_enum);

/// Find the authored display name for one enum, returning an empty string when unavailable.
godot::String authored_enum_label(const godot::Dictionary &root, const godot::StringName &qualified_enum);

/// Read member records with stable IDs, labels, and integer values.
godot::Array authored_enum_entries(const godot::Dictionary &root, const godot::StringName &qualified_enum);

/// List authored enum choices with qualified IDs and display names.
godot::Array authored_enum_options(const godot::Dictionary &root);

/// Create a Switch snapshot that uses the existing enum runtime and stable authored case IDs.
godot::Dictionary make_authored_enum_switch(const godot::Dictionary &root, const godot::StringName &qualified_enum);

/// Refresh switch snapshots from one enum declaration without changing existing branch identities.
godot::Dictionary refresh_authored_enum_switches(const godot::Dictionary &root, const godot::StringName &qualified_enum);

} // namespace crimson::visual_scripting

#endif
