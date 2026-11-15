// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_value_nodes.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

namespace crimson::visual_scripting {
namespace {

using namespace godot;
using Type = Variant::Type;

/// List the writable components first and any additional read-only graph outputs after them.
struct Components {
    std::array<const char *, 5> fields; ///< Native Godot field names in graph pin order.
    size_t make_count; ///< Number of writable fields exposed by Make.
};

/// Describe the public fields used by each supported Godot value.
Components fields_for(Type type) {
    switch (type) {
        case Variant::VECTOR2:
        case Variant::VECTOR2I: return {{"x", "y"}, 2};
        case Variant::VECTOR3:
        case Variant::VECTOR3I:
        case Variant::BASIS: return {{"x", "y", "z"}, 3};
        case Variant::VECTOR4:
        case Variant::VECTOR4I:
        case Variant::PROJECTION:
        case Variant::QUATERNION: return {{"x", "y", "z", "w"}, 4};
        case Variant::RECT2:
        case Variant::RECT2I:
        case Variant::AABB: return {{"position", "size", "end"}, 2};
        case Variant::PLANE: return {{"normal", "d", "x", "y", "z"}, 2};
        case Variant::COLOR: return {{"r", "g", "b", "a"}, 4};
        case Variant::TRANSFORM2D: return {{"x", "y", "origin"}, 3};
        case Variant::TRANSFORM3D: return {{"basis", "origin"}, 2};
        default: return {};
    }
}

/// Give a native field a short visible pin label.
Dictionary value_pin(const StringName &id, Type type) {
    Dictionary descriptor;
    descriptor["id"] = id;
    descriptor["label"] = String(id).capitalize();
    descriptor["type"] = static_cast<int64_t>(type);
    return descriptor;
}

} // namespace

const std::array<Type, 16> &supported_visual_value_types() {
    static constexpr std::array<Type, 16> types{
        Variant::VECTOR2, Variant::VECTOR2I, Variant::RECT2, Variant::RECT2I, Variant::VECTOR3, Variant::VECTOR3I,
        Variant::TRANSFORM2D, Variant::VECTOR4, Variant::VECTOR4I, Variant::PLANE, Variant::QUATERNION, Variant::AABB,
        Variant::BASIS, Variant::TRANSFORM3D, Variant::PROJECTION, Variant::COLOR,
    };
    return types;
}

Type visual_value_type(const Dictionary &node) {
    const Variant saved_type = node.get("value_type", Variant());
    if (saved_type.get_type() != Variant::INT) return Variant::VARIANT_MAX;
    const int64_t value = saved_type;
    const auto &types = supported_visual_value_types();
    const auto found = std::find(types.begin(), types.end(), value);
    return found == types.end() ? Variant::VARIANT_MAX : *found;
}

Array visual_value_pins(Type type, bool make, bool output) {
    const Components components = fields_for(type);
    if (components.make_count == 0) return {};

    if (make == output) return Array::make(value_pin("value", type));

    const Variant example = default_visual_value(type);
    Array pins;
    const size_t end = make ? components.make_count : components.fields.size();
    for (size_t i = 0; i < end && components.fields[i]; ++i) {
        const StringName field(components.fields[i]);
        bool valid = false;
        const Variant initial = example.get_named(field, valid);
        if (!valid) return {};
        Dictionary descriptor = value_pin(field, initial.get_type());
        if (make) descriptor["default"] = initial;
        pins.append(descriptor);
    }
    return pins;
}

bool write_visual_value_component(Variant &value, const StringName &field, const Variant &component) {
    bool valid = false;
    value.set_named(field, component, valid);
    return valid;
}

Variant default_visual_value(Type type) {
    return UtilityFunctions::type_convert(Variant(), type);
}

} // namespace crimson::visual_scripting
