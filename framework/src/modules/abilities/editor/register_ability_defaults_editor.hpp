// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/core/class_db.hpp>

#ifdef DEBUG_ENABLED
#include <godot_cpp/classes/script.hpp>

namespace godot {
class Control;
}
#endif

namespace crimson::abilities {

/// Install or remove the native Ability Defaults editor for this editor session.
void register_ability_defaults_editor(godot::ModuleInitializationLevel level);
void unregister_ability_defaults_editor(godot::ModuleInitializationLevel level);

#ifdef DEBUG_ENABLED
/// Report whether the selected script is based on GameplayAbility.
bool is_ability_defaults_script(const godot::Ref<godot::Script> &script);
/// Require a standalone project script with an assigned Godot resource UID.
bool is_saved_ability_defaults_script(const godot::Ref<godot::Script> &script);
/// Open the same editor used by the Inspector and Visual Script toolbar.
void open_ability_defaults_editor(const godot::Ref<godot::Script> &script, godot::Control *anchor);
#endif

} // namespace crimson::abilities
