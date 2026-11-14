// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_scene_load.hpp"

#include "scene_load_manager.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::scene_load {

void register_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_VIRTUAL_CLASS(SceneLoadManager);
    }
}

void unregister_types(godot::ModuleInitializationLevel) {}

} // namespace crimson::scene_load
