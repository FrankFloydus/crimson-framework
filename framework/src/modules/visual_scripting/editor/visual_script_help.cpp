// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_help.hpp"

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <optional>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// The editor loads descriptions on demand and resets this before the API is unavailable.
std::optional<Dictionary> index;
/// Location of the stock Godot and Crimson reference excerpt index in the installed addon.
constexpr char index_path[] = "res://addons/crimson-framework/editor/visual_script_help.json";

/// Load the addon's bundled reference prose once, without involving the editor's Help dock.
const Dictionary &help_index() {
    if (!index) {
        index.emplace();
        const Ref<FileAccess> file = FileAccess::open(index_path, FileAccess::READ);
        if (file.is_valid()) {
            const Variant parsed = JSON::parse_string(file->get_as_text());
            if (parsed.get_type() == Variant::DICTIONARY) {
                const Dictionary document = parsed;
                const Variant descriptions = document.get("descriptions", Variant());
                if (descriptions.get_type() == Variant::DICTIONARY) {
                    *index = descriptions;
                }
            }
        }
    }
    return *index;
}

/// Look up Godot value types and global functions directly, and walk parents for native object classes.
String lookup(const char *kind, StringName owner, const StringName &name) {
    const Dictionary &descriptions = help_index();
    const String suffix = String("/") + kind + String("/") + String(name);
    while (!owner.is_empty()) {
        const String key = String(owner) + suffix;
        if (descriptions.has(key)) {
            return descriptions[key];
        }
        if (!ClassDB::class_exists(owner)) {
            break;
        }
        owner = ClassDB::get_parent_class(owner);
    }
    return "No description available.";
}

}

godot::String visual_script_method_help(const godot::StringName &owner, const godot::StringName &method) {
    return lookup("method", owner, method);
}

godot::String visual_script_signal_help(const godot::StringName &owner, const godot::StringName &signal) {
    return lookup("signal", owner, signal);
}

void clear_visual_script_help() {
    index.reset();
}

}

#endif
