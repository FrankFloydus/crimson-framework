// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::gameplay_tags {

/// Register the Gameplay Tags Inspector and shared picker in Godot's editor.
void register_gameplay_tags_editor(godot::ModuleInitializationLevel level);

/// Remove the Gameplay Tags Inspector and picker when the editor shuts down.
void unregister_gameplay_tags_editor(godot::ModuleInitializationLevel level);

} // namespace crimson::gameplay_tags
