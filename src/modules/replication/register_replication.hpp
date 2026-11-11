// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::replication {

// Register the replication node at scene initialization.
void register_types(godot::ModuleInitializationLevel level);
// Finish replication module shutdown when Godot unloads the extension.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::replication
