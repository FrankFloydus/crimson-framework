// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "attribute_registry.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace crimson::attributes {
namespace {

using namespace godot;

// Builds the canonical empty registry used for a project without definitions.
Dictionary empty_registry() {
    Dictionary settings;
    settings["version"] = 1;
    settings["sets"] = Array();
    return settings;
}

// Preserves String bytes when passing authored text to the native registry.
std::string utf8(const String &text) {
    const CharString bytes = text.utf8();
    return std::string(bytes.get_data(), static_cast<size_t>(bytes.length()));
}

// Recognizes a lowercase ASCII identifier suitable for generated symbols.
bool valid_segment(std::string_view id) {
    if (id.empty() || id.size() > 48 || id.front() < 'a' || id.front() > 'z' || id.back() == '_') {
        return false;
    }
    bool after_underscore = false;
    for (const char c : id) {
        if (c == '_') {
            if (after_underscore) return false;
        } else if ((c < 'a' || c > 'z') && (c < '0' || c > '9')) {
            return false;
        }
        after_underscore = c == '_';
    }
    return true;
}

// Validates one object's ID and optional display label.
bool read_identity(const Dictionary &data, std::string &id, std::string &label, String &error) {
    const Variant raw_id = data.get("id", Variant());
    if (raw_id.get_type() != Variant::STRING || !valid_segment(id = utf8(String(raw_id)))) {
        error = "IDs must start with a lowercase letter and use up to 48 lowercase letters, digits, and single underscores.";
        return false;
    }
    const Variant raw_label = data.get("label", String());
    if (raw_label.get_type() != Variant::STRING) {
        error = "Display labels must be text.";
        return false;
    }
    label = utf8(String(raw_label));
    return true;
}

// Compiles one row after its set has been identified.
bool read_attribute(const Dictionary &data, AttributeDefinition &attribute, String &error) {
    if (!read_identity(data, attribute.id, attribute.label, error)) return false;
    const Variant raw_visibility = data.get("visibility", String("authority"));
    if (raw_visibility.get_type() != Variant::STRING) {
        error = "Visibility must be authority, owner, or observers.";
        return false;
    }
    const String visibility = raw_visibility;
    if (visibility == "owner") {
        attribute.visibility = AttributeVisibility::OWNER;
    } else if (visibility == "observers") {
        attribute.visibility = AttributeVisibility::OBSERVERS;
    } else if (visibility != "authority") {
        error = "Visibility must be authority, owner, or observers.";
        return false;
    }
    return true;
}

// Compiles one set and checks its local ID namespace.
bool read_set(const Dictionary &data, AttributeSetDefinition &set, String &error) {
    if (!read_identity(data, set.id, set.label, error)) return false;
    const Variant raw_rows = data.get("attributes", Variant());
    if (raw_rows.get_type() != Variant::ARRAY) {
        error = "Attributes must be a list.";
        return false;
    }
    std::set<std::string> ids;
    for (const Variant &row : Array(raw_rows)) {
        AttributeDefinition attribute;
        if (row.get_type() != Variant::DICTIONARY) {
            error = "Each attribute must be a definition.";
            return false;
        }
        if (!read_attribute(row, attribute, error)) {
            if (!attribute.id.empty()) error = String(attribute.id.c_str()) + String(": ") + error;
            return false;
        }
        if (!ids.insert(attribute.id).second) {
            error = String("Duplicate attribute ID: ") + String(attribute.id.c_str());
            return false;
        }
        set.attributes.push_back(std::move(attribute));
    }
    return true;
}

} // namespace

using namespace godot;

const AttributeDefinition *AttributeRegistry::find(std::string_view qualified_id) const {
    const size_t separator = qualified_id.find('.');
    if (separator == std::string_view::npos) return nullptr;
    const auto set = std::find_if(sets.begin(), sets.end(), [set_id = qualified_id.substr(0, separator)](
                                          const AttributeSetDefinition &entry) { return entry.id == set_id; });
    if (set == sets.end()) return nullptr;
    const auto attribute = std::find_if(set->attributes.begin(), set->attributes.end(),
            [local_id = qualified_id.substr(separator + 1)](const AttributeDefinition &entry) { return entry.id == local_id; });
    return attribute == set->attributes.end() ? nullptr : &*attribute;
}

Dictionary project_attribute_registry() {
    const Variant stored = ProjectSettings::get_singleton()->get_setting(attribute_registry_setting);
    return stored.get_type() == Variant::DICTIONARY ? Dictionary(stored).duplicate(true) : Dictionary();
}

bool parse_attribute_registry(const Dictionary &data, AttributeRegistry &result, String &error) {
    error = String();
    const Variant version = data.get("version", Variant());
    const Variant rows = data.get("sets", Variant());
    if (version.get_type() != Variant::INT || int64_t(version) != 1 || rows.get_type() != Variant::ARRAY) {
        error = "The Gameplay Attributes registry requires version 1 and a list of sets.";
        return false;
    }
    AttributeRegistry next;
    std::set<std::string> ids;
    for (const Variant &row : Array(rows)) {
        AttributeSetDefinition set;
        if (row.get_type() != Variant::DICTIONARY) {
            error = "Each attribute set must be a definition.";
            return false;
        }
        if (!read_set(row, set, error)) {
            if (!set.id.empty()) error = String(set.id.c_str()) + String(": ") + error;
            return false;
        }
        if (!ids.insert(set.id).second) {
            error = String("Duplicate set ID: ") + String(set.id.c_str());
            return false;
        }
        next.sets.push_back(std::move(set));
    }
    result = std::move(next);
    return true;
}

bool load_project_attribute_registry(AttributeRegistry &result, String &error) {
    return parse_attribute_registry(project_attribute_registry(), result, error);
}

Error save_project_attribute_registry(const Dictionary &data, String &error) {
    if (!Engine::get_singleton()->is_editor_hint()) {
        error = "Open the project in the Godot editor to save the attribute registry.";
        return ERR_UNAUTHORIZED;
    }
    AttributeRegistry compiled;
    if (!parse_attribute_registry(data, compiled, error)) return ERR_INVALID_DATA;
    auto *settings = ProjectSettings::get_singleton();
    const Variant previous = settings->get_setting(attribute_registry_setting);
    Dictionary metadata = data.duplicate(true);
    for (const Variant &set : Array(metadata["sets"])) {
        for (const Variant &row : Array(Dictionary(set)["attributes"])) {
            Dictionary attribute = row;
            attribute.erase("initial");
            attribute.erase("minimum");
            attribute.erase("maximum");
        }
    }
    if (metadata == previous) return OK;
    settings->set_setting(attribute_registry_setting, metadata);
    const Error status = settings->save();
    if (status != OK) {
        settings->set_setting(attribute_registry_setting, previous);
        error = "Godot could not save project.godot. Check that the file is writable.";
    }
    return status;
}

void initialize_attribute_registry_setting() {
    auto *settings = ProjectSettings::get_singleton();
    const Dictionary defaults = empty_registry();
    if (!settings->has_setting(attribute_registry_setting)) {
        settings->set_setting(attribute_registry_setting, defaults);
    }
    settings->set_initial_value(attribute_registry_setting, defaults);
    settings->set_as_internal(attribute_registry_setting, true);
}

bool make_attribute_values(const AttributeRegistry &registry, const PackedStringArray &set_ids,
        Dictionary &out, String &error) {
    Dictionary next;
    std::set<std::string> selected;
    for (const String &set_id : set_ids) {
        const std::string native_id = utf8(set_id);
        const auto set = std::find_if(registry.sets.begin(), registry.sets.end(),
                [&native_id](const AttributeSetDefinition &entry) { return entry.id == native_id; });
        if (set == registry.sets.end() || !selected.insert(native_id).second) {
            error = String("Unknown or duplicate Gameplay Attribute set: ") + set_id;
            return false;
        }
        for (const AttributeDefinition &attribute : set->attributes) {
            const std::string qualified = set->id + '.' + attribute.id;
            next[String(qualified.c_str())] = 0.0;
        }
    }
    out = next;
    error = String();
    return true;
}

Error evaluate_attribute_base(double previous, double operand, bool add, double &result) {
    if (!std::isfinite(previous) || !std::isfinite(operand)) return ERR_INVALID_PARAMETER;
    const double next = add ? previous + operand : operand;
    if (!std::isfinite(next)) return ERR_INVALID_PARAMETER;
    result = next;
    return OK;
}

} // namespace crimson::attributes
