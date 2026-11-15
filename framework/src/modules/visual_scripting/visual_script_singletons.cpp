// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_builtin_data.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>

#include <set>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Store only names accepted by Godot's Autoload panel.
constexpr char autoload_prefix[] = "autoload/";

/// Use the registered script identity without creating an instance of user code.
void describe_script_pin(Dictionary &pin, const Ref<Script> &script) {
    const String path = external_script_path(script);
    if (path.is_empty()) return;
    const StringName global_name = script->get_global_name();
    pin["script_path"] = path;
    pin["script_name"] = global_name.is_empty() ? path.get_file().get_basename() : String(global_name);
}

/// Treat editor classes as unavailable in the running game.
bool game_class(const StringName &type) {
    if (!ClassDB::class_exists(type)) return false;
    const auto api = ClassDBSingleton::get_singleton()->class_get_api_type(type);
    return api == ClassDBSingleton::API_CORE || api == ClassDBSingleton::API_EXTENSION;
}

/// Read the authored root of a PackedScene without instantiating it or running script callbacks.
void describe_scene_pin(Dictionary &pin, const Ref<PackedScene> &scene) {
    // SceneState is a stock Godot class outside Crimson's compiled application bindings.
    Ref<RefCounted> state = scene->call("get_state");
    bool found_script = false;
    for (int depth = 0; state.is_valid() && depth < 32; ++depth) {
        const int count = state->call("get_node_count");
        if (count == 0) break;
        const StringName type = state->call("get_node_type", 0);
        if (game_class(type)) pin["class"] = type;
        if (!found_script) {
            const int properties = state->call("get_node_property_count", 0);
            for (int index = 0; index < properties; ++index) {
                if (StringName(state->call("get_node_property_name", 0, index)) == StringName("script")) {
                    const Ref<Script> script = state->call("get_node_property_value", 0, index);
                    describe_script_pin(pin, script);
                    found_script = true;
                    break;
                }
            }
        }
        if (game_class(type)) break;
        state = state->call("get_base_scene_state");
    }
}

/// Describe an Autoload from its project setting without adding it to the editor tree.
Dictionary autoload_pin(const String &path) {
    Dictionary pin;
    pin["class"] = StringName("Node");
    const Ref<Resource> resource = ResourceLoader::get_singleton()->load(path);
    if (const Ref<Script> script = resource; script.is_valid()) {
        const StringName type = script->get_instance_base_type();
        if (game_class(type) && ClassDB::is_parent_class(type, "Node")) pin["class"] = type;
        describe_script_pin(pin, script);
    } else if (const Ref<PackedScene> scene = resource; scene.is_valid()) {
        describe_scene_pin(pin, scene);
    }
    return pin;
}

/// Form a graph card with a named reference and its authoring-time output type.
Dictionary singleton_record(const StringName &name, const StringName &source, const Dictionary &pin) {
    Dictionary record;
    record["kind"] = StringName("singleton");
    record["singleton"] = name;
    record["source"] = source;
    record["pin"] = pin;
    return record;
}

} // namespace

Array find_visual_script_singletons() {
    Array result;
    auto *settings = ProjectSettings::get_singleton();
    std::set<StringName> autoloads;
    for (const Dictionary &setting : settings->get_property_list()) {
        const String key = setting.get("name", String());
        if (!key.begins_with(autoload_prefix)) continue;
        const String name = key.trim_prefix(autoload_prefix);
        if (!name.is_valid_identifier()) continue;
        const String path = String(settings->get_setting_with_override(key)).trim_prefix("*");
        if (!ResourceLoader::get_singleton()->exists(path)) continue;
        autoloads.insert(StringName(name));
        result.append(singleton_record(name, "autoload", autoload_pin(path)));
    }

    auto *engine = Engine::get_singleton();
    std::set<StringName> stock_names;
    for (size_t index = 0; index < builtin_singletons_count; ++index) {
        const BuiltinSingleton &info = builtin_singletons[index];
        const StringName name(info.name);
        const StringName type(info.type);
        stock_names.insert(name);
        if (autoloads.count(name) || !game_class(type) || !engine->has_singleton(name)) continue;
        Dictionary pin;
        pin["class"] = type;
        result.append(singleton_record(name, "engine", pin));
    }
    PackedStringArray names = engine->get_singleton_list();
    names.sort();
    for (const String &name : names) {
        const StringName identity = name;
        if (autoloads.count(identity) || stock_names.count(identity) || !engine->has_singleton(identity)) continue;
        Object *object = engine->get_singleton(identity);
        if (!object) continue;
        const StringName type = object->get_class();
        if (!game_class(type)) continue;
        Dictionary pin;
        pin["class"] = type;
        describe_attached_script(pin, object);
        result.append(singleton_record(identity, "engine", pin));
    }
    return result;
}

Object *resolve_visual_script_singleton(Object *owner, const StringName &name, bool autoload) {
    if (autoload) {
        auto *node = Object::cast_to<Node>(owner);
        if (!node || !node->is_inside_tree()) return nullptr;
        Window *root = node->get_tree()->get_root();
        const String key = String(autoload_prefix) + name;
        return root && ProjectSettings::get_singleton()->has_setting(key) ? root->get_node_or_null(NodePath(name)) : nullptr;
    }
    auto *engine = Engine::get_singleton();
    return engine->has_singleton(name) ? engine->get_singleton(name) : nullptr;
}

} // namespace crimson::visual_scripting
