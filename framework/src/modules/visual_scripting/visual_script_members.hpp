// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <memory>

namespace crimson::visual_scripting {

/// List authorable instance methods and readable/writable properties, including inherited members.
godot::Array find_native_members(const godot::StringName &target_class);

/// List stock Godot methods available for a built-in Variant type, including static methods.
godot::Array find_builtin_members(godot::Variant::Type type);

/// List stock Godot global utility functions with their native signatures.
godot::Array find_utility_members();

/// Decode and bounds-check the built-in receiver tag of a saved method.
godot::Variant::Type builtin_receiver_type(const godot::Dictionary &member);

/// Identify a built-in mutator that supplies the modified packed value as an output.
bool builtin_updates_target(const godot::Dictionary &member);

/// Validate a serialized built-in method or global utility against Godot's signatures.
godot::String validate_builtin_member(const godot::Dictionary &member);

struct BuiltinMethod;
struct GlobalConstant;

/// Find a Godot global constant by the exact symbol stored in a graph.
const GlobalConstant *find_global_constant(const godot::StringName &name);

/// Create a read-only literal node storing the engine symbol as its identity.
godot::Dictionary make_global_constant(const GlobalConstant &constant);

/// Discover the engine registry and project Autoloads with their available authoring types.
godot::Array find_visual_script_singletons();

/// Resolve a current singleton from the owner's scene tree or the engine registry.
godot::Object *resolve_visual_script_singleton(godot::Object *owner, const godot::StringName &name, bool autoload);

/// Resolve a validated global utility once when compiling a node.
const BuiltinMethod *find_utility_method(const godot::StringName &name);

/// Invoke a compiled utility with already validated arguments, including its variadic tail.
godot::String invoke_utility(const BuiltinMethod &method, const godot::StringName &name,
        const godot::Variant **arguments, size_t count, godot::Variant &result);

/// List static methods registered directly on the given runtime engine class.
godot::Array find_native_static_members(const godot::StringName &owner);

/// List typed signal event records for a target class and its ancestors.
godot::Array find_native_signals(const godot::StringName &target_class);

/// Snapshot a native class enum as an authorable switch, or return empty for unsupported types.
godot::Dictionary make_enum_switch(const godot::StringName &qualified_enum);

/// List public ClassDB enum names qualified by their declaring native class.
godot::PackedStringArray native_enum_types();

/// Return unique named integer choices from a native ClassDB enum.
godot::Array native_enum_entries(const godot::StringName &qualified_enum);

/// Validate a saved member descriptor against the class API available in this build.
godot::String validate_native_member(const godot::StringName &kind, const godot::Dictionary &member);

/// Check a wire between two declared data types, using Godot inheritance for objects.
bool pins_compatible(const godot::Dictionary &source_pin, const godot::Dictionary &target_pin);

/// Check a resolved data input or output against its declared type.
bool pin_accepts_value(const godot::Dictionary &pin, const godot::Variant &value);

/// Check whether a scalar descriptor is allowed as a typed Dictionary key.
bool collection_key_type_allowed(const godot::Dictionary &type);

/// Identify Array element or Dictionary key/value descriptors in a saved pin.
bool pin_has_collection_schema(const godot::Dictionary &pin);

/// Create an empty native collection with the pin's declared types, or return null for an invalid descriptor.
godot::Variant default_collection_value(const godot::Dictionary &pin);

/// Resolve saved or dynamically assigned collection values without mutating the source.
bool resolve_collection_value(const godot::Dictionary &pin, const godot::Variant &source, godot::Variant &resolved);

/// Verify a method receiver is a live Godot object that has not queued deletion.
bool is_live_method_receiver(const godot::Variant &value);

struct RuntimeCollectionType;

/// Type and optional object class resolved from a pin when compiling a graph.
struct RuntimePinType {
    /// Variant tag, or VARIANT_MAX for malformed descriptors.
    godot::Variant::Type type = godot::Variant::VARIANT_MAX;
    /// Required native class for object pins; empty for other tags.
    godot::StringName object_class;
    /// Compile collection fields only for pins that constrain element, key, or value types.
    std::shared_ptr<const RuntimeCollectionType> collection;
    /// Accept either collection kind on a For Each input whose source was not inferred.
    bool iteration_collection = false;
    /// Check the inferred container kind on a For Each input with a resolved source.
    bool iteration_shape_known = false;
    /// The saved Script required in addition to the native class on object pins.
    godot::String script_path;

    /// Decode a saved pin descriptor once.
    static RuntimePinType from_pin(const godot::Dictionary &pin);
    /// Match the value without coercing it, including liveness for object references.
    bool accepts(const godot::Variant &value) const;
};

} // namespace crimson::visual_scripting
