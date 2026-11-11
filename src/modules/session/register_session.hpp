// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::session {

// Register game session types at Godot scene initialization.
void register_types(godot::ModuleInitializationLevel level);
// Finish game session shutdown when Godot unloads the extension.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::session
