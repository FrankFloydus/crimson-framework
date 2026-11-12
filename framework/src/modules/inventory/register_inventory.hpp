// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::inventory {

// Registers the native inventory types at scene initialization.
void register_types(godot::ModuleInitializationLevel level);
// Provides the inventory shutdown hook for the binding library.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::inventory
