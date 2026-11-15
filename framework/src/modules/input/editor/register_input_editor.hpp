// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/godot.hpp>

namespace crimson::input {

class InputLayout;
class InputActionMap;

// Show the editor-owned floating panel, optionally assigning a selected layout to a scene or profile.
void open_input_actions(const godot::Ref<InputLayout> &layout, godot::Object *assignment_owner = nullptr);

// Show the native editor for one independently saved action map.
void open_input_map(const godot::Ref<InputActionMap> &map);

// Install the authoring tools when Godot loads the extension in the editor.
void register_input_editor(godot::ModuleInitializationLevel level);

// Remove the editor plugin before the extension's editor classes are retired.
void unregister_input_editor(godot::ModuleInitializationLevel level);

} // namespace crimson::input
