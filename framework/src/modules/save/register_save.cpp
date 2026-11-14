// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_save.hpp"

#include "save_coordinator.hpp"
#include "save_presentation.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::save {

void initialize(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        godot::ClassDB::register_class<checkpoint::SaveCoordinator>(true);
        godot::ClassDB::register_class<SavePresentation>();
    }
}

void uninitialize(godot::ModuleInitializationLevel) {}

} // namespace crimson::save
