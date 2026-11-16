// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/attributes/attribute_codegen.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

using crimson::attributes::AttributeRegistry;
using crimson::attributes::GeneratedAttributeHeader;
using crimson::attributes::build_attribute_headers;
using crimson::attributes::validate_attribute_header_symbols;

/// Report a fixture failure even if the executable was built with assertions disabled.
void check(bool condition, std::string_view description) {
    if (!condition) {
        std::cerr << "Gameplay Attributes generator: " << description << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

/// Exercise deterministic generation and optionally write its actual output for a compiler check.
int main(int argc, char **argv) {
    AttributeRegistry registry{
        {
            { "vitals", "Vitals", { { "stamina", "Stamina" }, { "health", "Health" } } },
            { "vehicle", "Vehicle", { { "health", "Durability" }, { "set", "Drive Mode" } } },
        },
    };
    std::vector<GeneratedAttributeHeader> headers;
    std::string error;
    check(validate_attribute_header_symbols(registry, error) && error.empty(), "valid symbols can be saved");
    check(build_attribute_headers(registry, headers, error) && error.empty() && headers.size() == 2,
            "valid declarations generate one header per set");
    check(headers[0].filename == "vehicle_attributes.gen.hpp" &&
                    headers[1].filename == "vitals_attributes.gen.hpp",
            "headers are ordered by set ID");
    check(headers[0].source.find("get_set(godot::Object &attributes)") != std::string::npos &&
                    headers[0].source.find("find_set(godot::Object &component)") != std::string::npos,
            "live set lookup preserves the accessor for an attribute named set");
    const std::string original = headers[1].source;
    check(original.find("vitals.health") != std::string::npos, "qualified ID is preserved");
    check(original.find("get_health(") != std::string::npos &&
                    original.find("get_health_base(") != std::string::npos &&
                    original.find("set_health_base(") != std::string::npos &&
                    original.find("add_health_base(") != std::string::npos &&
                    original.find("bind_health_changed(") != std::string::npos,
            "named reads, writes and change subscription are emitted");
    check(original.find("vitals.health") < original.find("vitals.stamina"), "members are ordered by ID");
    check(original.find("class Set {") != std::string::npos &&
                    original.find("component.call(\"get_attribute_set\", godot::StringName(\"vitals\"))") != std::string::npos &&
                    original.find("get_health() const") != std::string::npos &&
                    original.find("set_health_base(double value) const") != std::string::npos &&
                    original.find("bind_health_changed(const godot::Callable &callback) const") != std::string::npos &&
                    original.find("detail::read(attributes.ptr(), \"health\", false)") != std::string::npos,
            "typed live set uses its set ID for lookup and local IDs for access");

    registry.sets.front().label = "Combat Statistics";
    registry.sets.front().attributes.back().label = "Hit Points";
    std::reverse(registry.sets.front().attributes.begin(), registry.sets.front().attributes.end());
    std::reverse(registry.sets.begin(), registry.sets.end());
    check(build_attribute_headers(registry, headers, error) && headers[1].source == original,
            "authoring order and presentation labels do not change generated code");

    registry.sets.back().attributes.push_back({ "health_base", "Health Base" });
    check(!validate_attribute_header_symbols(registry, error) && error.find("get_health_base") != std::string::npos &&
                    error.find("vitals.health") != std::string::npos &&
                    error.find("vitals.health_base") != std::string::npos,
            "save preflight identifies both owners of the collision");
    const std::string collision = error;
    check(!build_attribute_headers(registry, headers, error) && error == collision && headers[1].source == original,
            "generation uses the same validation and preserves the existing output plan");
    registry.sets.back().attributes.pop_back();
    registry.sets.push_back(registry.sets.back());
    check(!build_attribute_headers(registry, headers, error) && error.find("Duplicate attribute set ID") != std::string::npos,
            "duplicate set IDs are rejected");

    if (argc == 2) {
        const auto destination = std::filesystem::u8path(argv[1]);
        for (const auto &header : headers) {
            std::ofstream file(destination / header.filename, std::ios::binary);
            file.write(header.source.data(), static_cast<std::streamsize>(header.source.size()));
            file.close();
            check(static_cast<bool>(file), "write the compiler fixture header");
        }
    }
    std::cout << "CRIMSON_GAMEPLAY_ATTRIBUTES_CODEGEN_PASSED\n";
}
