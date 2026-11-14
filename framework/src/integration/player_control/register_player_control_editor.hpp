// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::integration {

// Register the PlayerControlProfile action pickers in the Godot editor.
void register_player_control_editor(godot::ModuleInitializationLevel level);

// Remove the profile inspector before Godot retires the extension's editor classes.
void unregister_player_control_editor(godot::ModuleInitializationLevel level);

} // namespace crimson::integration
