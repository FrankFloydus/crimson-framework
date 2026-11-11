// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::match_runtime {

// Register match runtime types at Godot scene initialization.
void register_types(godot::ModuleInitializationLevel level);

} // namespace crimson::match_runtime
