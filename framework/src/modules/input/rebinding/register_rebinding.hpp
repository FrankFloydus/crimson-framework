// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::rebinding {

// Register local rebinding types after the Input module's scene bindings.
void register_types(godot::ModuleInitializationLevel level);

} // namespace crimson::rebinding
