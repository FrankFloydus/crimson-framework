// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <nlohmann/json.hpp>

namespace crimson::steam::publisher_codec {

// Preserve full uint64 values rather than routing Steam IDs through a Godot float.
using Json = nlohmann::json;

// Parse a bounded native reply without logging vendor diagnostics.
Json parse_body(const godot::PackedByteArray &body);
// Return a property only when the input is an object containing the requested key.
const Json *member(const Json *object, const char *name);
// Decode a canonical unsigned decimal string or an exact JSON uint64.
bool unsigned_value(const Json &node, uint64_t &output);
// Report whether an ID is a valid public individual Steam account.
bool individual_id(uint64_t id);
// Read a verified account ID, allowing an omitted or zero optional license owner.
bool steam_id(const Json *node, bool required, godot::String &output);

} // namespace crimson::steam::publisher_codec
