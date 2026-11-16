// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/attributes/attribute_registry.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>
#include <vector>

namespace crimson::attributes {

/// Complete game-owned header prepared for publication.
struct GeneratedAttributeHeader {
    /// File name within the selected project directory.
    std::string filename;
    /// Deterministic C++17 source with a generated-file marker.
    std::string source;
};

/// Validate the C++ symbols that Save and Generate derive from authored IDs.
bool validate_attribute_header_symbols(const AttributeRegistry &registry, std::string &error);

/// Generate one header per set and replace the output only when all symbols are valid.
bool build_attribute_headers(const AttributeRegistry &registry, std::vector<GeneratedAttributeHeader> &headers,
        std::string &error);

/// Stage generated headers inside the project and publish each as a complete file.
godot::Error generate_attribute_headers(const AttributeRegistry &registry, const godot::String &directory,
        godot::String &error);

} // namespace crimson::attributes
