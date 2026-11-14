// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

#ifdef DEBUG_ENABLED
namespace godot {
class EditorProperty;
}
#endif

namespace crimson::mover {

#ifdef DEBUG_ENABLED
// Attach the same Controller Style help to the stock dropdown on a component or profile.
void apply_controller_style_help(godot::EditorProperty &row);
#endif

// Register the Mover property help plugin for the editor.
void register_mover_editor(godot::ModuleInitializationLevel level);

// Remove the Mover property help plugin during editor shutdown.
void unregister_mover_editor(godot::ModuleInitializationLevel level);

} // namespace crimson::mover
