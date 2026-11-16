// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <array>

namespace crimson::abilities {

// Internal project setting containing UID-keyed ability defaults.
inline constexpr char ability_defaults_setting[] = "crimson/gameplay_abilities/defaults";

// Persisted tag fields in their presentation and validation order.
inline constexpr std::array<const char *, 10> ability_default_tag_keys = {
    "ability_tags",
    "activation_required_tags",
    "activation_blocked_tags",
    "activation_owned_tags",
    "block_abilities_with_tags",
    "cancel_abilities_with_tags",
    "source_required_tags",
    "source_blocked_tags",
    "target_required_tags",
    "target_blocked_tags",
};

// Registers the hidden setting with its empty versioned schema.
void initialize_ability_defaults_setting();

// Loads only this script's defaults. Missing identities and entries give an empty dictionary.
bool load_ability_defaults(const godot::Ref<godot::Script> &script, godot::Dictionary &tags, godot::String &error);

// Returns the current UID for a saved script file, or an empty string when no source identity can be established.
godot::String get_ability_defaults_authoring_uid(const godot::Ref<godot::Script> &script);

// Saves defaults for the UID the editor opened and rolls back the setting on a write failure.
godot::Error save_ability_defaults(const godot::Ref<godot::Script> &script, const godot::Dictionary &tags,
        const godot::String &expected_uid, godot::String &error);

} // namespace crimson::abilities
