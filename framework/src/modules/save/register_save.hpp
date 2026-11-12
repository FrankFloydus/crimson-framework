// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::save {

// Register the native save classes at the scene initialization level.
void initialize(godot::ModuleInitializationLevel level);
// Provide the save module shutdown hook at GDExtension teardown.
void uninitialize(godot::ModuleInitializationLevel level);

} // namespace crimson::save
