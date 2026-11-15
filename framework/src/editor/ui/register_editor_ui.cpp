// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_editor_ui.hpp"

#ifdef DEBUG_ENABLED
#include "editor_controls.hpp"
#include "editor_compositions.hpp"
#include "editor_graph.hpp"
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#endif

namespace crimson::editor_ui {

#ifdef DEBUG_ENABLED

/// Register the catalog window before creating the editor plug-in.
void register_editor_catalog();
/// Install the catalog menu in the Godot editor.
void install_editor_catalog();
/// Remove the catalog menu on editor shutdown.
void remove_editor_catalog();

#endif

void register_editor_ui(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        register_editor_controls();
        register_editor_compositions();
        register_editor_graph();
        register_editor_catalog();
        install_editor_catalog();
    }
#else
    (void)level;
#endif
}

void unregister_editor_ui(godot::ModuleInitializationLevel level) {
#ifdef DEBUG_ENABLED
    if (level == godot::MODULE_INITIALIZATION_LEVEL_EDITOR) {
        remove_editor_catalog();
    }
#else
    (void)level;
#endif
}

}
