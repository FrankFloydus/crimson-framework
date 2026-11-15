// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/variant.hpp>

#include <cstddef>
#include <cstdint>

namespace crimson::visual_scripting {

/// A declared fixed argument and its optional engine-default literal.
struct BuiltinArgument {
    const char *name; ///< The argument's name in Godot.
    const char *type; ///< The API spelling, including Variant for an arbitrary value.
    const char *default_literal; ///< Godot's serialized literal, or null when required.
};

/// Metadata shared by the method picker, graph validation, and utility dispatch.
struct BuiltinMethod {
    const char *owner; ///< Godot's type name, or @GlobalScope for a utility.
    const char *name; ///< The method name, unique for this owner.
    const char *result; ///< The return type name, or an empty string for void.
    const BuiltinArgument *arguments; ///< Argument definitions in declaration order.
    size_t count; ///< Number of fixed arguments required by the signature.
    bool read_only; ///< The engine declares this receiver method const.
    bool is_static; ///< The function requires no receiver.
    bool variadic; ///< The call allows additional Variant arguments.
    uint32_t hash; ///< Engine signature hash for GDExtension utility lookup.
    godot::Variant (*call_utility)(const godot::Variant *const *); ///< Typed wrapper for a fixed utility call.
};

extern const BuiltinMethod builtin_methods[]; ///< Catalog sorted by owner, then method name.
extern const size_t builtin_methods_count; ///< Number of catalog entries.

/// A named integer from the pinned Godot @GlobalScope catalogue.
struct GlobalConstant {
    const char *name; ///< Exact Godot symbol, such as KEY_ESCAPE.
    const char *enumeration; ///< Declaring global enum, or empty for a standalone constant.
    int64_t value; ///< Value published by this Godot API revision.
    bool bitfield; ///< Whether the declaring group represents bit flags.
};

extern const GlobalConstant global_constants[]; ///< Global symbols in exact-name order.
extern const size_t global_constants_count; ///< Number of global symbols.

/// The native type of a stock singleton when it is registered with Godot.
struct BuiltinSingleton {
    const char *name; ///< Exact name in Engine's registry.
    const char *type; ///< Native class exposed to graph code.
};

extern const BuiltinSingleton builtin_singletons[]; ///< Complete stock API singleton catalogue.
extern const size_t builtin_singletons_count; ///< Number of stock singleton descriptors.

} // namespace crimson::visual_scripting
