// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/dictionary.hpp>

namespace crimson::inventory::detail {

// Adds one named field and recurses over the remaining field pairs.
template<class Value, class... Fields>
void write_model_fields(godot::Dictionary &dictionary, const char *name, const Value &value, const Fields &...fields) {
    dictionary[name] = value;
    if constexpr (sizeof...(fields) != 0) {
        write_model_fields(dictionary, fields...);
    }
}

// Builds a Godot dictionary from name-and-value pairs.
template<class... Fields>
godot::Dictionary make_model_dto(const Fields &...fields) {
    static_assert(sizeof...(fields) % 2 == 0);
    godot::Dictionary dictionary;
    write_model_fields(dictionary, fields...);
    return dictionary;
}

} // namespace crimson::inventory::detail
