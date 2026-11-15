// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace crimson::visual_scripting {

/// Return a canonical external Script path that can be stored in a graph.
godot::String external_script_path(const godot::Ref<godot::Script> &script);

/// Load a Script for compiler reflection or creation of a script-typed collection.
godot::Ref<godot::Script> load_script_type(const godot::String &path);

/// List named project Script types as Object pin descriptors for the editor's type picker.
godot::Array global_script_type_pins();

/// Resolve one globally named Script type or return an empty descriptor when unavailable.
godot::Dictionary global_script_type_pin(const godot::StringName &name);

/// Return the registered global name for an exact nominal pin path, if the language exposes one.
godot::StringName global_script_pin_name(const godot::Dictionary &pin);

/// Check whether a serialized path can identify a standalone project Script.
bool valid_script_path(const godot::String &path);

/// Add a saved, attached Script identity and a display name to a scene reference.
void describe_attached_script(godot::Dictionary &record, godot::Object *object);

/// Describe construction of a standalone GDScript backed by a Node or RefCounted.
godot::Dictionary describe_script_constructor(const godot::Ref<godot::Script> &script);

/// Verify a saved constructor against the current script, base type, and arguments.
bool matches_script_constructor(const godot::Dictionary &record, const godot::Ref<godot::Script> &script);

/// Check an actual Script and its base chain without loading resources during dispatch.
bool script_derives_from(const godot::Ref<godot::Script> &actual, const godot::String &expected_path);

/// Check the actual attached Script and its base chain against a saved type.
bool object_has_script_type(const godot::Object *object, const godot::String &expected_path);

/// Check a wire between two nominal Script types.
bool script_paths_compatible(const godot::String &source_path, const godot::String &target_path);

/// List public instance methods and declared instance properties of a saved Script.
godot::Array find_script_members(const godot::String &script_path);

/// List public declared signals of a saved Script as graph event records.
godot::Array find_script_signals(const godot::String &script_path);

/// Validate a graph's saved Script operation against the current Script and its signature.
godot::String validate_script_member(const godot::StringName &kind, const godot::Dictionary &member);

/// Validate a graph's saved Script signal against the current Script and its signature.
godot::String validate_script_signal(const godot::Dictionary &record);

} // namespace crimson::visual_scripting
