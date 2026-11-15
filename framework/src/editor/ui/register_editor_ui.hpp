// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::editor_ui {

/// Register the editor-only components and their preview tool.
void register_editor_ui(godot::ModuleInitializationLevel level);
/// Release the editor-only catalog during extension shutdown.
void unregister_editor_ui(godot::ModuleInitializationLevel level);

} // namespace crimson::editor_ui
