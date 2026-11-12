// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_replication.hpp"

#include "replication.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::replication {

void register_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(StateReplication);
    }
}

void unregister_types(godot::ModuleInitializationLevel level) {
    // godot-cpp unregisters the class at extension shutdown.
}

} // namespace crimson::replication
