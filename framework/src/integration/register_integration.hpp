// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::integration {

// Register composition and provider classes after the standalone modules.
void register_types(godot::ModuleInitializationLevel level);

// Release process-level provider resources before standalone module teardown.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::integration
