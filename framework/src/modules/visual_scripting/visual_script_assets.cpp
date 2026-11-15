// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_graph.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <set>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// The stable identity and fallback location for one project asset.
struct AssetReference {
    /// Canonical project path to use when the UID cannot be resolved.
    String path;
    /// Stable Godot resource identity, or empty for assets without one.
    String uid;
};

/// Accept saved, standalone project assets that can be included in an export.
bool is_project_asset_path(const String &path) {
    if (!path.begins_with("res://") || path.get_file().is_empty() || path != path.simplify_path()) {
        return false;
    }
    const String relative = path.trim_prefix("res://");
    return relative != ".." && !relative.begins_with("../") && !relative.contains(":");
}

/// Check that a typed pin accepts a native resource class.
bool is_resource_class(const StringName &class_name) {
    return !class_name.is_empty() && ClassDB::class_exists(class_name) && ClassDB::is_parent_class(class_name, "Resource");
}

/// Decode an external asset reference without coercing other graph literal types.
AssetReference read_asset_reference(const Variant &value) {
    if (value.get_type() != Variant::DICTIONARY) {
        return {};
    }
    const Dictionary encoded = value;
    const Variant path_value = encoded.get("path", Variant());
    const Variant uid_value = encoded.get("uid", String());
    if (path_value.get_type() != Variant::STRING || uid_value.get_type() != Variant::STRING) {
        return {};
    }
    const String path = path_value;
    if (!is_project_asset_path(path)) {
        return {};
    }
    return { path, uid_value };
}

/// Look up the current location of an asset whose UID has been registered by Godot.
String registered_asset_path(const String &uid) {
    if (uid.is_empty()) {
        return {};
    }
    const auto *uids = ResourceUID::get_singleton();
    const int64_t id = uids->text_to_id(uid);
    if (id == ResourceUID::INVALID_ID || !uids->has_id(id)) {
        return {};
    }
    const String path = uids->get_id_path(id);
    return is_project_asset_path(path) ? path : String();
}

/// Capture the UID of an existing project asset without assigning a new identity.
Dictionary encode_asset_path(const String &path, const String &known_uid = String()) {
    const int64_t id = ResourceLoader::get_singleton()->get_resource_uid(path);
    Dictionary reference;
    reference["path"] = path;
    reference["uid"] = id == ResourceUID::INVALID_ID ? known_uid : ResourceUID::get_singleton()->id_to_text(id);
    return reference;
}

/// Match external references only against typed Resource pins.
String resource_pin_class(const Dictionary &pin) {
    const Variant type = pin.get("type", Variant());
    const Variant class_name = pin.get("class", Variant());
    if (type.get_type() != Variant::INT || static_cast<int64_t>(type) != Variant::OBJECT ||
        (class_name.get_type() != Variant::STRING && class_name.get_type() != Variant::STRING_NAME) ||
        !is_resource_class(class_name)) {
        return {};
    }
    return class_name;
}

/// Get the descriptor of the collection slot that can hold external resources.
Dictionary resource_collection_side(const Dictionary &pin) {
    const int64_t type = pin.get("type", Variant::NIL);
    if (type != Variant::ARRAY && type != Variant::DICTIONARY) {
        return {};
    }
    const Variant side = pin.get(type == Variant::ARRAY ? "element" : "value", Variant());
    return side.get_type() == Variant::DICTIONARY && !resource_pin_class(side).is_empty() ? Dictionary(side) : Dictionary();
}

/// Visit resource-valued elements while leaving ordinary Dictionary data untouched.
template <typename Visitor>
bool visit_resource_collection(const Dictionary &pin, const Variant &stored, const Visitor &visit) {
    const int64_t type = pin.get("type", Variant::NIL);
    const auto resource_class = resource_pin_class(resource_collection_side(pin));
    if (type != stored.get_type() || resource_class.is_empty()) return false;
    if (type == Variant::ARRAY) {
        const Array entries = stored;
        for (int64_t index = 0; index < entries.size(); ++index) {
            if (!visit(Variant(index), entries[index], resource_class)) return false;
        }
    } else {
        const Dictionary entries = stored;
        for (const Variant &key : entries.keys()) {
            if (!visit(key, entries[key], resource_class)) return false;
        }
    }
    return true;
}

/// Visit one asset literal under its storage key.
template <typename Visitor>
void visit_resource_value(Dictionary storage, const Variant &key, const Dictionary &pin, const Visitor &visit) {
    const String asset_class = resource_pin_class(pin);
    const Variant stored = storage.get(key, Variant());
    if (asset_class.is_empty()) {
        visit_resource_collection(pin, stored, [&](const Variant &index, const Variant &entry, const String &expected) {
            const AssetReference asset = read_asset_reference(entry);
            if (!asset.path.is_empty()) {
                Dictionary literal;
                literal["entry"] = entry;
                visit(literal, Variant("entry"), expected, asset);
                if (literal["entry"] != entry) {
                    if (stored.get_type() == Variant::ARRAY) {
                        Array array = stored;
                        array[int64_t(index)] = literal["entry"];
                    } else {
                        Dictionary map = stored;
                        map[index] = literal["entry"];
                    }
                }
            }
            return true;
        });
        return;
    }
    const AssetReference asset = read_asset_reference(stored);
    if (!asset.path.is_empty()) {
        visit(storage, key, asset_class, asset);
    }
}

/// Visit typed resource inputs and scoped variable defaults in a graph body.
template <typename Visitor>
void visit_resource_body(const Dictionary &root, const Dictionary &graph, const Visitor &visit) {
    const Variant graph_nodes = graph.get("nodes", Variant());
    if (graph_nodes.get_type() != Variant::ARRAY) {
        return;
    }
    const Array nodes = graph_nodes;
    for (int64_t node_index = 0; node_index < nodes.size(); ++node_index) {
        if (nodes[node_index].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary node = nodes[node_index];
        const Variant encoded_values = node.get("values", Variant());
        if (encoded_values.get_type() != Variant::DICTIONARY) {
            continue;
        }
        Dictionary values = encoded_values;
        if (StringName(node.get("kind", StringName())) == StringName("preload_resource")) {
            visit_resource_value(values, StringName("asset"), preload_asset_pin(node), visit);
        }
        for (const Variant &descriptor : graph_pins(root, graph, node, false, "Node")) {
            if (descriptor.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary pin = descriptor;
            const Variant pin_id = pin.get("id", Variant());
            if (pin_id.get_type() == Variant::STRING || pin_id.get_type() == Variant::STRING_NAME) {
                visit_resource_value(values, pin_id, pin, visit);
            }
        }
    }
    for (const char *scope : {"variables", "locals"}) {
        const Variant declarations = graph.get(scope, Variant());
        if (declarations.get_type() != Variant::ARRAY) {
            continue;
        }
        for (const Variant &record : Array(declarations)) {
            if (record.get_type() == Variant::DICTIONARY) {
                const Dictionary variable = record;
                const Variant pin = variable.get("pin", Variant());
                if (pin.get_type() == Variant::DICTIONARY) {
                    visit_resource_value(variable, "value", pin, visit);
                }
            }
        }
    }
}

/// Visit the event graph and reusable bodies so extraction preserves asset dependencies.
template <typename Visitor>
void visit_resource_inputs(const Dictionary &root, const Visitor &visit) {
    visit_resource_body(root, root, visit);
    for (const char *key : {"functions", "macros", "events"}) {
        const Variant declarations = root.get(key, Variant());
        if (declarations.get_type() != Variant::ARRAY) {
            continue;
        }
        for (const Variant &record : Array(declarations)) {
            if (record.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary definition = record;
            const Variant body = definition.get("graph", Variant());
            if (body.get_type() == Variant::DICTIONARY) {
                Dictionary active = Dictionary(body).duplicate();
                active["definition"] = definition.get("id", 0);
                visit_resource_body(root, active, visit);
            }
            const Variant inputs = definition.get("inputs", Variant());
            if (inputs.get_type() == Variant::ARRAY) {
                for (const Variant &record_pin : Array(inputs)) {
                    if (record_pin.get_type() == Variant::DICTIONARY) {
                        const Dictionary pin = record_pin;
                        visit_resource_value(pin, "default", pin, visit);
                    }
                }
            }
        }
    }
}

/// Visit script identities on graph records and pin descriptors without reading user literals as metadata.
template <typename Visitor>
void visit_script_paths(Dictionary record, const Visitor &visit) {
    const Variant stored_path = record.get("script_path", Variant());
    if (stored_path.get_type() == Variant::STRING && is_project_asset_path(stored_path)) {
        const String updated = visit(String(stored_path));
        if (!updated.is_empty() && updated != stored_path) {
            record["script_path"] = updated;
        }
    }
    for (const char *key : {"graph", "member", "pin", "output", "element", "key", "value_type"}) {
        const Variant child = record.get(key, Variant());
        if (child.get_type() == Variant::DICTIONARY) {
            visit_script_paths(Dictionary(child), visit);
        }
    }
    if (int64_t(record.get("type", Variant::NIL)) == Variant::DICTIONARY) {
        const Variant value_pin = record.get("value", Variant());
        if (value_pin.get_type() == Variant::DICTIONARY) {
            visit_script_paths(Dictionary(value_pin), visit);
        }
    }
    const Variant dictionary_types = record.get("dictionary_types", Variant());
    if (dictionary_types.get_type() == Variant::DICTIONARY) {
        const Dictionary types = dictionary_types;
        for (const char *lane : {"key", "value"}) {
            const Variant terminal = types.get(lane, Variant());
            if (terminal.get_type() == Variant::DICTIONARY) visit_script_paths(Dictionary(terminal), visit);
        }
    }
    for (const char *key : {"nodes", "variables", "locals", "functions", "macros", "events", "inputs", "outputs"}) {
        const Variant children = record.get(key, Variant());
        if (children.get_type() == Variant::ARRAY) {
            for (const Variant &child : Array(children)) {
                if (child.get_type() == Variant::DICTIONARY) {
                    visit_script_paths(Dictionary(child), visit);
                }
            }
        }
    }
}

} // namespace

bool is_supported_asset(const Ref<Resource> &resource) {
    if (resource.is_null() || resource->get_class() != "AudioStream") {
        return resource.is_valid();
    }
    for (Ref<Script> script = resource->get_script(); script.is_valid(); script = script->get_base_script()) {
        if (script->has_script_method("_instantiate_playback")) {
            return true;
        }
    }
    return false;
}

Dictionary make_asset_reference(const Ref<Resource> &resource) {
    return is_supported_asset(resource) ? make_preload_reference(resource) : Dictionary();
}

Dictionary make_preload_reference(const Ref<Resource> &resource) {
    if (resource.is_null() || resource->is_built_in() || !is_project_asset_path(resource->get_path())) {
        return {};
    }
    return encode_asset_path(resource->get_path());
}

String asset_reference_path(const Variant &value) {
    return read_asset_reference(value).path;
}

String resolved_asset_reference_path(const Variant &value) {
    const AssetReference asset = read_asset_reference(value);
    const String registered = registered_asset_path(asset.uid);
    return registered.is_empty() || (registered != asset.path && !ResourceLoader::get_singleton()->exists(registered)) ?
            asset.path : registered;
}

Dictionary preload_asset_pin(const Dictionary &node) {
    Dictionary descriptor;
    descriptor["id"] = StringName("asset");
    descriptor["label"] = "Resource";
    descriptor["type"] = int64_t(Variant::OBJECT);
    descriptor["class"] = node.get("class", StringName("Resource"));
    return descriptor;
}

Dictionary preload_output_pin(const Dictionary &node) {
    Dictionary descriptor = preload_asset_pin(node);
    const StringName constraint = descriptor["class"];
    const StringName asset_class = node.get("asset_class", StringName());
    if (is_resource_class(asset_class) && is_resource_class(constraint) && ClassDB::is_parent_class(asset_class, constraint)) {
        descriptor["class"] = asset_class;
    }
    descriptor["id"] = StringName("value");
    return descriptor;
}

Ref<Resource> load_asset_reference(const Variant &value, const StringName &expected_class) {
    const Ref<Resource> resource = preload_asset_reference(value, expected_class);
    return is_supported_asset(resource) ? resource : Ref<Resource>();
}

Ref<Resource> preload_asset_reference(const Variant &value, const StringName &expected_class) {
    return load_resource_path(resolved_asset_reference_path(value), expected_class);
}

Ref<Resource> load_resource_path(const String &path, const StringName &expected_class) {
    auto *loader = ResourceLoader::get_singleton();
    if (path.is_empty() || !is_resource_class(expected_class)) {
        return {};
    }
    const String resolved = path.begins_with("uid://") ? registered_asset_path(path) : path;
    if (resolved.is_empty() || !loader->exists(resolved)) return {};
    const Ref<Resource> resource = loader->load(resolved);
    return resource.is_valid() && resource->is_class(expected_class) ? resource : Ref<Resource>();
}

bool resource_collection_pin(const Dictionary &pin) {
    return !resource_collection_side(pin).is_empty();
}

bool valid_resource_collection_storage(const Dictionary &pin, const Variant &value) {
    return visit_resource_collection(pin, value, [&](const Variant &key, const Variant &entry, const String &) {
        const Variant key_pin = pin.get("key", Variant());
        return (value.get_type() == Variant::ARRAY || key_pin.get_type() != Variant::DICTIONARY ||
                        pin_accepts_value(Dictionary(key_pin), key)) &&
                (entry.get_type() == Variant::NIL ||
                        (entry.get_type() == Variant::OBJECT && !entry.get_validated_object()) ||
                        !read_asset_reference(entry).path.is_empty());
    });
}

bool load_resource_collection(const Dictionary &pin, const Variant &stored, Variant &loaded) {
    if (pin_accepts_value(pin, stored)) {
        loaded = stored;
        return true;
    }
    Array array;
    Dictionary map;
    const bool loaded_all = visit_resource_collection(pin, stored, [&](const Variant &key, const Variant &entry, const String &expected) {
        Variant materialized;
        if (entry.get_type() != Variant::NIL && !(entry.get_type() == Variant::OBJECT && !entry.get_validated_object())) {
            const Ref<Resource> asset = load_asset_reference(entry, expected);
            if (asset.is_null()) return false;
            materialized = asset;
        }
        if (stored.get_type() == Variant::ARRAY) {
            array.append(materialized);
        } else {
            map[key] = materialized;
        }
        return true;
    });
    return loaded_all && resolve_collection_value(pin, stored.get_type() == Variant::ARRAY ? Variant(array) : Variant(map), loaded);
}

PackedStringArray graph_asset_dependencies(const Dictionary &graph, bool add_types) {
    PackedStringArray dependencies;
    std::set<String> seen;
    visit_resource_inputs(graph, [&](const Dictionary &, const Variant &, const String &asset_class, const AssetReference &asset) {
        const bool has_registered_uid = !registered_asset_path(asset.uid).is_empty();
        String dependency = has_registered_uid ? asset.uid : asset.path;
        if (add_types || has_registered_uid) {
            dependency += "::";
            if (add_types) {
                dependency += asset_class;
            }
        }
        if (has_registered_uid) {
            dependency += String("::") + asset.path;
        }
        if (seen.insert(dependency).second) {
            dependencies.append(dependency);
        }
    });
    visit_script_paths(graph, [&](const String &path) -> String {
        const String dependency = add_types ? path + String("::Script") : path;
        if (seen.insert(dependency).second) {
            dependencies.append(dependency);
        }
        return {};
    });
    return dependencies;
}

bool rename_graph_assets(const Dictionary &graph, const Dictionary &renames) {
    bool changed = false;
    visit_resource_inputs(graph, [&](Dictionary &values, const Variant &pin_id, const String &, const AssetReference &asset) {
        const String current_path = registered_asset_path(asset.uid);
        Variant replacement = renames.get(current_path, Variant());
        if (replacement.get_type() == Variant::NIL) {
            replacement = renames.get(asset.path, Variant());
        }
        if (replacement.get_type() == Variant::NIL && !asset.uid.is_empty()) {
            replacement = renames.get(asset.uid, Variant());
        }
        if (replacement.get_type() != Variant::STRING) {
            return;
        }
        const String new_path = replacement;
        if (new_path != asset.path && is_project_asset_path(new_path)) {
            const String known_uid = current_path == new_path ? asset.uid : String();
            values[pin_id] = encode_asset_path(new_path, known_uid);
            changed = true;
        }
    });
    visit_script_paths(graph, [&](const String &old_path) -> String {
        const Variant replacement = renames.get(old_path, Variant());
        if (replacement.get_type() == Variant::STRING && replacement != old_path && is_project_asset_path(replacement)) {
            changed = true;
            return replacement;
        }
        return {};
    });
    return changed;
}

} // namespace crimson::visual_scripting
