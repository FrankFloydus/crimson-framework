// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/editor/visual_script_authored_enums.hpp"

#ifdef DEBUG_ENABLED

#include "modules/visual_scripting/visual_script_graph.hpp"

#include <godot_cpp/variant/variant.hpp>

namespace crimson::visual_scripting {
namespace {
using namespace godot;

/// Reserve a qualified prefix that distinguishes local enum pins from ClassDB enums.
constexpr const char *enum_prefix = "GraphEnum.Enum_";

/// Read an optional array without coercing malformed data.
Array records(const Dictionary &record, const char *key) {
    const Variant value = record.get(key, Variant());
    return value.get_type() == Variant::ARRAY ? Array(value) : Array();
}

/// Find an authored enum record without consulting runtime native class metadata.
Dictionary find_enum(const Dictionary &root, const StringName &qualified_enum) {
    return find_graph_record(records(root, "enums"), authored_enum_id(qualified_enum));
}

} // namespace

StringName authored_enum_identity(int64_t id) {
    return id > 0 ? StringName(String(enum_prefix) + String::num_int64(id)) : StringName();
}

int64_t authored_enum_id(const StringName &qualified_enum) {
    const String qualified = qualified_enum;
    const String suffix = qualified.trim_prefix(enum_prefix);
    if (suffix == qualified || !suffix.is_valid_int()) {
        return 0;
    }
    const int64_t id = suffix.to_int();
    return qualified_enum == authored_enum_identity(id) ? id : 0;
}

String authored_enum_label(const Dictionary &root, const StringName &qualified_enum) {
    return find_enum(root, qualified_enum).get("name", String());
}

Array authored_enum_entries(const Dictionary &root, const StringName &qualified_enum) {
    return records(find_enum(root, qualified_enum), "entries");
}

Array authored_enum_options(const Dictionary &root) {
    Array result;
    for (const Variant &item : records(root, "enums")) {
        if (item.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary record = item;
        const StringName id = authored_enum_identity(record.get("id", 0));
        const String name = record.get("name", String());
        if (!id.is_empty() && !name.is_empty()) {
            Dictionary option;
            option["id"] = id;
            option["name"] = name;
            result.append(option);
        }
    }
    return result;
}

Dictionary make_authored_enum_switch(const Dictionary &root, const StringName &qualified_enum) {
    const Dictionary declaration = find_enum(root, qualified_enum);
    Array cases;
    for (const Variant &item : records(declaration, "entries")) {
        if (item.get_type() != Variant::DICTIONARY) {
            return {};
        }
        const Dictionary source = item;
        const int64_t id = source.get("id", 0);
        if (id <= 0) {
            return {};
        }
        Dictionary entry;
        entry["id"] = StringName("case_member_" + String::num_int64(id));
        entry["label"] = source.get("name", String());
        entry["value"] = source.get("value", Variant());
        cases.append(entry);
    }
    if (cases.is_empty()) {
        return {};
    }
    Dictionary selection;
    selection["id"] = StringName("value");
    selection["label"] = "Selection";
    selection["type"] = Variant::INT;
    selection["enum_type"] = qualified_enum;
    selection["default"] = Dictionary(cases[0]).get("value", Variant());
    Dictionary result;
    result["kind"] = "switch_enum";
    result["enum_type"] = qualified_enum;
    result["pin"] = selection;
    result["cases"] = cases;
    return validate_enum_switch(result).is_empty() ? result : Dictionary();
}

Dictionary refresh_authored_enum_switches(const Dictionary &root, const StringName &qualified_enum) {
    const Dictionary snapshot = make_authored_enum_switch(root, qualified_enum);
    if (snapshot.is_empty()) {
        return {};
    }
    PackedInt64Array graphs;
    graphs.append(0);
    for (const char *section : {"functions", "macros"}) {
        for (const Variant &item : records(root, section)) {
            if (item.get_type() == Variant::DICTIONARY) {
                const int64_t id = Dictionary(item).get("id", 0);
                if (id > 0) {
                    graphs.append(id);
                }
            }
        }
    }
    Dictionary updated = root;
    for (const int64_t graph_id : graphs) {
        Dictionary body = graph_body(updated, graph_id).duplicate();
        Array nodes = records(body, "nodes").duplicate();
        bool changed = false;
        for (int64_t index = 0; index < nodes.size(); ++index) {
            if (nodes[index].get_type() != Variant::DICTIONARY) {
                continue;
            }
            Dictionary node = Dictionary(nodes[index]).duplicate();
            if (String(node.get("kind", String())) != "switch_enum" ||
                    StringName(node.get("enum_type", StringName())) != qualified_enum) {
                continue;
            }
            node["pin"] = snapshot["pin"];
            node["cases"] = snapshot["cases"];
            nodes[index] = node;
            changed = true;
        }
        if (changed) {
            body["nodes"] = nodes;
            updated = replace_graph_body(updated, graph_id, body);
        }
    }
    return updated;
}

} // namespace crimson::visual_scripting

#endif
