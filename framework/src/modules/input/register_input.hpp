// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/core/class_db.hpp>

namespace crimson::input {

// Register authored input resources, the facade, and its private scene router.
void register_types(godot::ModuleInitializationLevel level);

} // namespace crimson::input
