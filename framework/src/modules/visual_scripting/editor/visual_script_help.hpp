// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::visual_scripting {

/// Return compact reference prose for a method on the class that declares or inherits it.
godot::String visual_script_method_help(const godot::StringName &owner, const godot::StringName &method);

/// Return compact reference prose for a native signal, including inherited signals.
godot::String visual_script_signal_help(const godot::StringName &owner, const godot::StringName &signal);

/// Discard indexed Godot values before the editor extension shuts down.
void clear_visual_script_help();

}

#endif
