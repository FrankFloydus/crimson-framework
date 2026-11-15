// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/core/class_db.hpp>

namespace crimson::visual_scripting {

/// Register the visual script graph tools at editor initialization.
void register_visual_script_editor(godot::ModuleInitializationLevel level);

/// Remove the visual script graph tools before editor shutdown.
void unregister_visual_script_editor(godot::ModuleInitializationLevel level);

}
