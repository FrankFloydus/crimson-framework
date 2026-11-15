// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace godot {
class ScriptLanguage;
}

namespace crimson::visual_scripting {

/// Register the script classes, language, and file format when scenes initialize.
void register_visual_scripting(godot::ModuleInitializationLevel level);

/// Retire the script language and file format when scenes shut down.
void unregister_visual_scripting(godot::ModuleInitializationLevel level);

/// Get the registered language while the scene module is active.
godot::ScriptLanguage *visual_script_language();

} // namespace crimson::visual_scripting
