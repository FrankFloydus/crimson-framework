// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace crimson::visual_scripting {

/// Check that a resource can be used in graph inputs. Bare AudioStream resources lack a playback implementation.
bool is_supported_asset(const godot::Ref<godot::Resource> &resource);

/// Store a supported external resource by UID and fallback path, or return an empty dictionary.
godot::Dictionary make_asset_reference(const godot::Ref<godot::Resource> &resource);

/// Store a standalone project resource selected for Preload, including script resources.
godot::Dictionary make_preload_reference(const godot::Ref<godot::Resource> &resource);

/// Read the stored fallback path, even when the saved resource is missing.
godot::String asset_reference_path(const godot::Variant &value);

/// Prefer the asset's registered UID path, falling back to its saved project path.
godot::String resolved_asset_reference_path(const godot::Variant &value);

/// Load a supported graph input asset of the expected native Resource class, or return null.
godot::Ref<godot::Resource> load_asset_reference(const godot::Variant &value, const godot::StringName &expected_class);

/// Prepare a constant project asset using its saved UID and fallback path.
godot::Ref<godot::Resource> preload_asset_reference(const godot::Variant &value, const godot::StringName &expected_class);

/// Load a resource synchronously on the executing path, checking its declared output type.
godot::Ref<godot::Resource> load_resource_path(const godot::String &path, const godot::StringName &expected_class);

/// Describe the saved asset field shared by the Preload editor and dependency scanner.
godot::Dictionary preload_asset_pin(const godot::Dictionary &node);

/// Describe the output refined to the selected resource's native type when known.
godot::Dictionary preload_output_pin(const godot::Dictionary &node);

/// Identify a collection with a Resource element or value descriptor.
bool resource_collection_pin(const godot::Dictionary &pin);

/// Check serialized resource entries without loading assets into the editor model.
bool valid_resource_collection_storage(const godot::Dictionary &pin, const godot::Variant &value);

/// Load the saved resources into a class-constrained Godot collection.
bool load_resource_collection(const godot::Dictionary &pin, const godot::Variant &stored, godot::Variant &loaded);

/// Identify pins that can persist catalog names in place of Resource references.
enum class GameplayTagLiteralType { NONE, TAG, CONTAINER, TAG_ARRAY };

/// Read exactly the native tag or collection type declared by the pin.
GameplayTagLiteralType gameplay_tag_literal_type(const godot::Dictionary &pin);

/// Recognize name-backed defaults while leaving normal resource references on the asset path.
bool is_gameplay_tag_literal(const godot::Dictionary &pin, const godot::Variant &stored);

/// Build a detached runtime value from a tag default using the registered Gameplay Tags factories.
bool resolve_gameplay_tag_literal(const godot::Dictionary &pin, const godot::Variant &stored, godot::Variant &loaded);

/// Report external pin assets in the format Godot expects for dependency editing and selective export.
godot::PackedStringArray graph_asset_dependencies(const godot::Dictionary &graph, bool add_types);

/// Apply Godot's path remaps to external pin assets and report whether the graph changed.
bool rename_graph_assets(const godot::Dictionary &graph, const godot::Dictionary &renames);

} // namespace crimson::visual_scripting
