// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace crimson::attributes {

// Project setting shared by authoring, code generation, and runtime.
inline constexpr char attribute_registry_setting[] = "crimson/gameplay_attributes/sets";

// Controls which recipients may see an attribute in a committed character view.
enum class AttributeVisibility {
    AUTHORITY, // Kept in the authority store.
    OWNER, // Projected to the character's admitted owner.
    OBSERVERS, // Projected to the owner and positively authorized observers.
};

// One validated attribute inside an authored set.
struct AttributeDefinition {
    std::string id; // Stable local identifier within its set.
    std::string label; // Optional presentation text.
    AttributeVisibility visibility = AttributeVisibility::AUTHORITY; // Replication policy.
};

// A group of validated definitions sharing the same first ID segment.
struct AttributeSetDefinition {
    std::string id; // Project-wide set identifier.
    std::string label; // Optional presentation text.
    std::vector<AttributeDefinition> attributes; // Definitions in authoring order.
};

// Detached compiled registry. Runtime owners keep it fixed for their lifetime.
struct AttributeRegistry {
    std::vector<AttributeSetDefinition> sets; // Validated sets in authoring order.

    // Borrows the exact definition for a qualified ID, or returns null.
    const AttributeDefinition *find(std::string_view qualified_id) const;
};

// Returns a detached copy of the current project setting.
godot::Dictionary project_attribute_registry();
// Validates a versioned project dictionary and replaces the result only on success.
bool parse_attribute_registry(const godot::Dictionary &data, AttributeRegistry &result, godot::String &error);
// Compiles the current project setting for a new runtime owner.
bool load_project_attribute_registry(AttributeRegistry &result, godot::String &error);
// Saves the registry metadata in the editor and rolls back the live setting if saving fails.
godot::Error save_project_attribute_registry(const godot::Dictionary &data, godot::String &error);
// Installs the default and hides the backing dictionary from the generic settings editor.
void initialize_attribute_registry_setting();
// Builds independent zero values keyed by qualified String IDs.
bool make_attribute_values(const AttributeRegistry &registry, const godot::PackedStringArray &set_ids,
        godot::Dictionary &out, godot::String &error);
// Computes a finite base value without publishing it.
godot::Error evaluate_attribute_base(double previous, double operand, bool add, double &result);

} // namespace crimson::attributes
