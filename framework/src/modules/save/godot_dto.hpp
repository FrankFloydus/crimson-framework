// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "save_core.hpp"

#include <godot_cpp/variant/variant.hpp>

namespace checkpoint {

// The Godot-facing helpers run on the main thread. Json owns all converted data.
// Convert a Godot String to UTF-8 and reject embedded NUL bytes.
std::string utf8(const godot::String &value);
// Create a Godot String from validated UTF-8 data.
godot::String godot_string(const std::string &value);
// Convert a main-thread Godot value into a detached JSON snapshot.
Json to_json(const godot::Variant &value, const Limits &limits);

// Input must already satisfy the JSON codec's size and depth limits.
// Reconstruct Godot values from bounded and validated checkpoint JSON.
godot::Variant to_variant(const Json &value);

} // namespace checkpoint
