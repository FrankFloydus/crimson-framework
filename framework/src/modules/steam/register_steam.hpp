// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::steam {

// Register standalone Steam bindings and handle opted-in pre-render startup.
void register_types(godot::ModuleInitializationLevel level);

// Release the optional pre-render runtime lease at server-level shutdown.
void unregister_types(godot::ModuleInitializationLevel level);

} // namespace crimson::steam
