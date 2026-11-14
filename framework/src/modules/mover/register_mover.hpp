// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::mover {

// Register standalone Mover types during Godot scene initialization.
void register_types(godot::ModuleInitializationLevel level);
// Finish Mover shutdown when Godot unloads the extension.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::mover
