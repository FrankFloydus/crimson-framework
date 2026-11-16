// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/abilities/ability_defaults.hpp"

#include "modules/gameplay_tags/bindings/gameplay_tag_names.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_uid.hpp>

#include <algorithm>
#include <set>

namespace crimson::abilities {
namespace {

using namespace godot;
namespace tag_names = gameplay_tags::bindings;

// Builds the versioned settings envelope from the current entry table.
Dictionary make_registry(const Dictionary &entries = Dictionary()) {
    Dictionary registry;
    registry["version"] = 1;
    registry["entries"] = entries;
    return registry;
}

// Checks the registry envelope without reading defaults for other scripts.
bool read_entries(const Variant &stored, Dictionary &entries, String &error) {
    if (stored.get_type() == Variant::DICTIONARY) {
        const Dictionary registry = stored;
        const Variant version = registry.get("version", Variant());
        const Variant rows = registry.get("entries", Variant());
        if (version.get_type() == Variant::INT && static_cast<int64_t>(version) == 1 &&
                rows.get_type() == Variant::DICTIONARY) {
            entries = rows;
            return true;
        }
    }
    error = "Ability Defaults requires a version 1 registry with a dictionary of entries.";
    return false;
}

// Recognizes standalone script paths and excludes built-in scripts attached to a scene or resource.
bool standalone_script_path(const String &path) {
    const String relative = path.trim_prefix("res://");
    const String extension = path.get_extension().to_lower();
    return path.begins_with("res://") && path == path.simplify_path() && !relative.begins_with("../") &&
            !relative.contains(":") && (extension == "gd" || extension == "crimsonscript");
}

// Recognizes a canonical Godot UID without treating malformed sidecars as an identity.
String read_sidecar_uid(const String &path) {
    const String raw = FileAccess::get_file_as_string(path + String(".uid")).strip_edges();
    if (!raw.begins_with("uid://")) {
        return {};
    }
    const auto *uids = ResourceUID::get_singleton();
    const int64_t id = uids->text_to_id(raw);
    return id != ResourceUID::INVALID_ID && uids->id_to_text(id) == raw ? raw : String();
}

// Resolves the current identity from a source sidecar or the export's packed UID index.
String script_uid(const String &path, bool authoring) {
    if (!standalone_script_path(path)) {
        return {};
    }
    const String physical_path = ProjectSettings::get_singleton()->globalize_path(path);
    const bool on_disk = physical_path.is_absolute_path() && FileAccess::file_exists(physical_path);
    if (on_disk) {
        // An old editor cache entry does not establish the identity of a replacement source file.
        return FileAccess::file_exists(path + String(".uid")) ? read_sidecar_uid(path) : String();
    }
    if (authoring) {
        return {};
    }
    const int64_t id = ResourceLoader::get_singleton()->get_resource_uid(path);
    return id != ResourceUID::INVALID_ID ? ResourceUID::get_singleton()->id_to_text(id) : String();
}

// Checks that every field belongs to the schema, then normalizes its explicit tag list.
bool normalize_tags(const Dictionary &input, Dictionary &normalized, String &error) {
    for (const Variant &key : input.keys()) {
        if (key.get_type() != Variant::STRING ||
                std::none_of(ability_default_tag_keys.begin(), ability_default_tag_keys.end(),
                        [&key](const char *field) { return String(key) == field; })) {
            error = String("Unknown Ability Defaults field: ") + String(key);
            return false;
        }
    }

    Dictionary result;
    for (const char *field : ability_default_tag_keys) {
        const Variant raw = input.get(field, PackedStringArray());
        if (raw.get_type() != Variant::PACKED_STRING_ARRAY) {
            error = String("Ability Defaults field must contain a list of tag names: ") + String(field);
            return false;
        }
        PackedStringArray names;
        std::set<std::string> seen;
        for (const String &name : PackedStringArray(raw)) {
            const auto tag = tag_names::read_tag_name(StringName(name));
            if (!tag.is_valid()) {
                error = String("Invalid Gameplay Tag in ") + String(field) + String(": ") + name;
                return false;
            }
            if (seen.insert(tag.name()).second) {
                names.append(String(tag.name().c_str()));
            }
        }
        if (!names.is_empty()) {
            result[field] = names;
        }
    }
    normalized = result;
    return true;
}

} // namespace

using namespace godot;

void initialize_ability_defaults_setting() {
    auto *settings = ProjectSettings::get_singleton();
    const Dictionary defaults = make_registry();
    if (!settings->has_setting(ability_defaults_setting)) {
        settings->set_setting(ability_defaults_setting, defaults);
    }
    settings->set_initial_value(ability_defaults_setting, defaults);
    settings->set_as_internal(ability_defaults_setting, true);
}

bool load_ability_defaults(const Ref<Script> &script, Dictionary &tags, String &error) {
    error = String();
    if (script.is_null()) {
        error = "Provide a Gameplay Ability script to load its defaults.";
        return false;
    }
    const String uid = script_uid(script->get_path(), false);
    if (uid.is_empty()) {
        tags = Dictionary();
        return true;
    }
    const auto *settings = ProjectSettings::get_singleton();
    Dictionary entries;
    if (!read_entries(settings->get_setting(ability_defaults_setting, make_registry()), entries, error)) {
        return false;
    }
    if (!entries.has(uid)) {
        tags = Dictionary();
        return true;
    }
    const Variant row = entries[uid];
    const Variant fields = row.get_type() == Variant::DICTIONARY ? Dictionary(row).get("tags", Variant()) : Variant();
    if (fields.get_type() != Variant::DICTIONARY) {
        error = String("Ability Defaults must contain a dictionary of tag fields for ") + uid + String(".");
        return false;
    }
    return normalize_tags(fields, tags, error);
}

String get_ability_defaults_authoring_uid(const Ref<Script> &script) {
    return script.is_valid() ? script_uid(script->get_path(), true) : String();
}

Error save_ability_defaults(const Ref<Script> &script, const Dictionary &tags, const String &expected_uid, String &error) {
    error = String();
    if (!Engine::get_singleton()->is_editor_hint()) {
        error = "Open the project in the Godot editor to save Ability Defaults.";
        return ERR_UNAUTHORIZED;
    }
    const String uid = get_ability_defaults_authoring_uid(script);
    if (uid != expected_uid) {
        error = "The ability script identity changed. Close and reopen Ability Defaults to edit the current script.";
        return ERR_INVALID_DATA;
    }
    if (uid.is_empty()) {
        error = "Save the ability as a standalone script and let Godot assign its Resource UID before saving defaults.";
        return ERR_UNAVAILABLE;
    }
    Dictionary fields;
    if (!normalize_tags(tags, fields, error)) {
        return ERR_INVALID_DATA;
    }

    auto *settings = ProjectSettings::get_singleton();
    const Variant previous = settings->get_setting(ability_defaults_setting, make_registry());
    Dictionary current_entries;
    if (!read_entries(previous, current_entries, error)) {
        return ERR_INVALID_DATA;
    }
    Dictionary entries = current_entries.duplicate();
    if (fields.is_empty()) {
        entries.erase(uid);
    } else {
        Dictionary row;
        row["path"] = script->get_path();
        row["tags"] = fields;
        entries[uid] = row;
    }
    const Dictionary updated = make_registry(entries);
    if (updated == previous) {
        return OK;
    }
    settings->set_setting(ability_defaults_setting, updated);
    const Error saved = settings->save();
    if (saved != OK) {
        settings->set_setting(ability_defaults_setting, previous);
        error = "Godot could not save project.godot. Check that the file is writable.";
    }
    return saved;
}

} // namespace crimson::abilities
