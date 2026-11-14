// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_mover.hpp"

#include "mover.hpp"
#include "mover_component.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::mover {

void register_types(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }

    GDREGISTER_CLASS(MotionRule);
    GDREGISTER_ABSTRACT_CLASS(Locomotion);
    GDREGISTER_ABSTRACT_CLASS(Mover);
    GDREGISTER_CLASS(MoverComponent);
    GDREGISTER_INTERNAL_CLASS(MoverDriver);
}

void unregister_types(godot::ModuleInitializationLevel) {
    // The binding library unregisters classes during extension shutdown.
}

} // namespace crimson::mover
