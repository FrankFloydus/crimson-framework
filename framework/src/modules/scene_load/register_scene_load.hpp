// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::scene_load {

// Register scene loading types at Godot scene initialization.
void register_types(godot::ModuleInitializationLevel level);
// Finish scene loading shutdown when Godot unloads the extension.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::scene_load
