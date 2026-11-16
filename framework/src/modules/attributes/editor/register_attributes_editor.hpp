// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/godot.hpp>

namespace crimson::attributes {

/// Install the Gameplay Attributes Project Settings tab in the Godot editor.
void register_attributes_editor(godot::ModuleInitializationLevel level);

/// Remove the Gameplay Attributes tab as the Godot editor shuts down.
void unregister_attributes_editor(godot::ModuleInitializationLevel level);

} // namespace crimson::attributes
