// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/visual_scripting/visual_script_collections.hpp"
#include "modules/visual_scripting/visual_script_assets.hpp"
#include "modules/visual_scripting/visual_script_members.hpp"
#include "modules/visual_scripting/visual_script_script_members.hpp"
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/object_id.hpp>

namespace crimson::visual_scripting {
namespace {

using namespace godot;

/// Identify operations with a variable number of authored entries.
bool is_constructor(CollectionOperation operation) {
    return operation == CollectionOperation::MAKE_ARRAY || operation == CollectionOperation::MAKE_DICTIONARY;
}

/// Find the saved terminal lane of a Make Dictionary input.
const char *dictionary_lane(const StringName &input) {
    const String name = input;
    if (name.begins_with("key_")) return "key";
    if (name.begins_with("value_")) return "value";
    return nullptr;
}

/// Store the type that the first concrete input established for this lane.
void establish_dictionary_lane(Dictionary &node, Dictionary types, const char *lane, const Dictionary &terminal) {
    if (int64_t(terminal.get("type", Variant::NIL)) == Variant::NIL) return;
    types = types.duplicate();
    types[lane] = terminal;
    node["dictionary_types"] = types;
}

/// A legacy Variant lane can lock after a later edit resolves its ambiguous or mixed entries.
bool is_legacy_variant_lane(const Dictionary &types, const char *lane) {
    const Variant value = types.get(lane, Variant());
    return value.get_type() == Variant::DICTIONARY && int64_t(Dictionary(value).get("type", -1)) == Variant::NIL;
}

/// Retain locked types, filling in legacy lanes only when no inconsistent entry or default remains.
void adopt_dictionary_types_for_added_pin(Dictionary &node, const Dictionary &root, const Dictionary &graph,
        const StringName &base) {
    const Variant saved = node.get("dictionary_types", Variant());
    if (!node.has("dictionary_types")) {
        node["dictionary_types"] = infer_legacy_dictionary_types(root, graph, node, base);
    } else if (saved.get_type() == Variant::DICTIONARY) {
        Dictionary types = saved;
        const bool key = is_legacy_variant_lane(types, "key");
        const bool value = is_legacy_variant_lane(types, "value");
        if (!key && !value) return;
        const Dictionary inferred = infer_legacy_dictionary_types(root, graph, node, base);
        types = types.duplicate();
        for (const char *lane : {"key", "value"}) {
            if (!is_legacy_variant_lane(types, lane)) continue;
            if (inferred.has(lane)) types[lane] = inferred[lane];
            else types.erase(lane);
        }
        node["dictionary_types"] = types;
    }
}

/// Find the input index using the same identifiers that the compiler uses.
int64_t constructor_input_index(const Array &pins, const StringName &pin) {
    for (int64_t index = 0; index < pins.size(); ++index) {
        if (StringName(Dictionary(pins[index])["id"]) == pin) return index;
    }
    return -1;
}

/// Map the selected entry to empty IDs and shift each subsequent entry one row up.
Dictionary removed_entry_remap(const Array &pins, int64_t index, int64_t stride) {
    Dictionary remap;
    const int64_t first = index - index % stride;
    for (int64_t input = first; input < pins.size(); ++input) {
        remap[Dictionary(pins[input])["id"]] =
                input < first + stride ? Variant() : Dictionary(pins[input - stride])["id"];
    }
    return remap;
}

/// Carry stored defaults to the same entries after their numbered input IDs change.
Dictionary remap_constructor_defaults(const Dictionary &values, const Dictionary &remap) {
    Dictionary result = values.duplicate();
    const Array pins = remap.keys();
    for (const Variant &pin : pins) {
        result.erase(pin);
    }
    for (const Variant &pin : pins) {
        const Variant replacement = remap[pin];
        if (replacement.get_type() != Variant::NIL && values.has(pin)) result[replacement] = values[pin];
    }
    return result;
}

/// Drop incoming wires to removed pins and keep later wires attached to their original entries.
Array remap_constructor_wires(const Array &wires, int64_t node_id, const Dictionary &remap) {
    Array result;
    for (const Variant &entry : wires) {
        if (entry.get_type() == Variant::DICTIONARY) {
            const Dictionary wire = entry;
            const Variant pin = wire.get("to_pin", Variant());
            const Variant target = wire.get("to", Variant());
            if (target.get_type() == Variant::INT && int64_t(target) == node_id && remap.has(pin)) {
                const Variant replacement = remap[pin];
                if (replacement.get_type() != Variant::NIL) {
                    Dictionary changed = wire.duplicate();
                    changed["to_pin"] = replacement;
                    result.append(changed);
                }
                continue;
            }
        }
        result.append(entry);
    }
    return result;
}

/// Describe a collection input or output and keep the inferred collection's terminal metadata.
Dictionary value_pin(const StringName &id, const String &label, Variant::Type type, const Dictionary &inferred = {}) {
    const Variant inferred_type = inferred.get("type", Variant::NIL);
    Dictionary result = int64_t(inferred_type) == type ? inferred.duplicate() : Dictionary();
    result["id"] = id;
    result["label"] = label;
    result["type"] = int64_t(type);
    result.erase("exec");
    result.erase("default");
    result.erase("iteration_collection");
    result.erase("iteration_output");
    return result;
}

/// Add one input with a known fallback, keeping new cards executable as they are authored.
void append_input(Array &pins, const StringName &id, const String &label, Variant::Type type, const Variant &fallback) {
    Dictionary descriptor = value_pin(id, label, type);
    descriptor["default"] = fallback;
    pins.append(descriptor);
}

/// Use the saved type for every pin on one lane of a schema-bearing dictionary.
void append_dictionary_input(Array &pins, const StringName &id, const String &label, const char *lane, const Dictionary &types) {
    const Variant saved = types.get(lane, Variant());
    const Dictionary terminal = saved.get_type() == Variant::DICTIONARY ? Dictionary(saved) : Dictionary();
    const auto type = static_cast<Variant::Type>(int64_t(terminal.get("type", Variant::NIL)));
    Dictionary descriptor = value_pin(id, label, type, terminal);
    if (!types.has(lane) || type != Variant::NIL) {
        descriptor["dictionary_terminal"] = true;
        if (StringName(lane) == StringName("key")) descriptor["dictionary_key"] = true;
    }
    if (type == Variant::NIL) descriptor["default"] = Variant();
    pins.append(descriptor);
}

/// Add collection terminals without exposing an editable input for the execution wire.
void append_execution(Array &pins, bool output) {
    Dictionary descriptor = value_pin(output ? "then" : "in", output ? "Then" : "In", Variant::NIL);
    descriptor["exec"] = true;
    pins.append(descriptor);
}

/// Use stable value keys and reject reference keys or non-values before they reach Godot's hash table.
bool dictionary_key(const Variant &key) {
    Dictionary descriptor;
    descriptor["type"] = int64_t(key.get_type());
    return key.get_type() != Variant::NIL && collection_key_type_allowed(descriptor);
}

/// Validate a native typed Array or Dictionary terminal without attempting a mutation that may log errors.
bool accepts_terminal(const Variant &value, int64_t type, const StringName &native_class, const Variant &script_value) {
    if (type == Variant::NIL) return true;
    if (type != Variant::OBJECT) return value.get_type() == type;
    if (value.get_type() == Variant::NIL) return true;
    if (value.get_type() != Variant::OBJECT) return false;
    const Object *object = value.get_validated_object();
    if (!object) {
        const ObjectID identity = value;
        return identity == ObjectID();
    }
    if (!native_class.is_empty() && !object->is_class(native_class)) return false;
    const Ref<Script> expected = script_value;
    if (expected.is_null()) return true;
    const Ref<Script> actual = object->get_script();
    return actual.is_valid() && (actual == expected || script_derives_from(actual, expected->get_path()));
}

/// Return the old container unchanged on a failed edit and a separate copy on success.
void edit_array(CollectionOperation operation, const Array &inputs, Dictionary &outputs) {
    const Array source = inputs[0];
    const bool appending = operation == CollectionOperation::APPEND_ARRAY_ITEM;
    const bool removing = operation == CollectionOperation::REMOVE_ARRAY_ITEM;
    const int64_t index = appending ? source.size() : int64_t(inputs[1]);
    const bool valid_index = appending || (index >= 0 && index < source.size());
    const bool valid_value = removing || accepts_terminal(inputs[appending ? 1 : 2], source.get_typed_builtin(),
            source.get_typed_class_name(), source.get_typed_script());
    const bool succeeds = valid_index && valid_value;
    if (succeeds) {
        Array result = source.duplicate();
        if (removing) result.remove_at(index);
        else if (appending) result.push_back(inputs[1]);
        else result[index] = inputs[2];
        outputs[StringName("result")] = result;
    } else {
        outputs[StringName("result")] = source;
    }
    outputs[StringName("succeeded")] = succeeds;
}

/// Validate an authored key against the graph's stable-key rules and the source Dictionary type.
bool map_accepts_key(const Dictionary &map, const Variant &key) {
    return dictionary_key(key) && accepts_terminal(key, map.get_typed_key_builtin(),
            map.get_typed_key_class_name(), map.get_typed_key_script());
}

/// Apply a Dictionary edit to a copy only after verifying that the key and value are valid.
void edit_dictionary(CollectionOperation operation, const Array &inputs, Dictionary &outputs) {
    const Dictionary source = inputs[0];
    const Variant key = inputs[1];
    const bool removing = operation == CollectionOperation::ERASE_DICTIONARY_KEY;
    const bool valid_key = map_accepts_key(source, key);
    const bool succeeds = valid_key && (removing ? source.has(key) : accepts_terminal(inputs[2],
            source.get_typed_value_builtin(), source.get_typed_value_class_name(), source.get_typed_value_script()));
    if (succeeds) {
        Dictionary result = source.duplicate();
        if (removing) result.erase(key);
        else result[key] = inputs[2];
        outputs[StringName("result")] = result;
    } else {
        outputs[StringName("result")] = source;
    }
    outputs[StringName("succeeded")] = succeeds;
}

} // namespace

CollectionOperation collection_operation(const StringName &kind) {
    using Operation = CollectionOperation;
    static const std::pair<StringName, Operation> operations[] = {
        {"make_array", Operation::MAKE_ARRAY}, {"make_dictionary", Operation::MAKE_DICTIONARY},
        {"get_array_item", Operation::GET_ARRAY_ITEM}, {"get_dictionary_value", Operation::GET_DICTIONARY_VALUE},
        {"set_array_item", Operation::SET_ARRAY_ITEM}, {"append_array_item", Operation::APPEND_ARRAY_ITEM},
        {"remove_array_item", Operation::REMOVE_ARRAY_ITEM}, {"set_dictionary_value", Operation::SET_DICTIONARY_VALUE},
        {"erase_dictionary_key", Operation::ERASE_DICTIONARY_KEY},
    };
    for (const auto &[name, operation] : operations) {
        if (name == kind) return operation;
    }
    return Operation::NONE;
}

bool collection_operation_edits(CollectionOperation operation) {
    return operation >= CollectionOperation::SET_ARRAY_ITEM;
}

Variant::Type collection_type(CollectionOperation operation) {
    using Operation = CollectionOperation;
    switch (operation) {
        case Operation::MAKE_ARRAY: case Operation::GET_ARRAY_ITEM: case Operation::SET_ARRAY_ITEM:
        case Operation::APPEND_ARRAY_ITEM: case Operation::REMOVE_ARRAY_ITEM: return Variant::ARRAY;
        case Operation::MAKE_DICTIONARY: case Operation::GET_DICTIONARY_VALUE:
        case Operation::SET_DICTIONARY_VALUE: case Operation::ERASE_DICTIONARY_KEY: return Variant::DICTIONARY;
        default: return Variant::NIL;
    }
}

StringName collection_source_input(CollectionOperation operation) {
    if (operation <= CollectionOperation::MAKE_DICTIONARY) return {};
    return collection_type(operation) == Variant::ARRAY ? StringName("array") : StringName("dictionary");
}

bool valid_collection_arity(const Dictionary &node) {
    const Variant count = node.get("collection_size", 1);
    return count.get_type() == Variant::INT && int64_t(count) >= 0 && int64_t(count) <= visual_script_max_collection_size;
}

Array collection_operation_pins(CollectionOperation operation, const Dictionary &node, bool output, const Dictionary &source) {
    using Operation = CollectionOperation;
    const Variant::Type type = collection_type(operation);
    const bool array = type == Variant::ARRAY;
    const bool constructor = is_constructor(operation);
    const bool edit = collection_operation_edits(operation);
    const Variant saved_types = operation == Operation::MAKE_DICTIONARY ? node.get("dictionary_types", Variant()) : Variant();
    const bool typed_dictionary = saved_types.get_type() == Variant::DICTIONARY;
    const Dictionary types = typed_dictionary ? Dictionary(saved_types) : Dictionary();
    Array pins;
    if (edit) append_execution(pins, output);
    if (output) {
        if (constructor) {
            Dictionary result = value_pin("value", array ? "Array" : "Dictionary", type);
            for (const char *lane : {"key", "value"}) {
                if (types.has(lane)) result[lane] = types[lane];
            }
            pins.append(result);
        } else if (edit) {
            pins.append(value_pin("result", "Result", type, source));
            pins.append(value_pin("succeeded", "Succeeded", Variant::BOOL));
        } else {
            pins.append(value_pin("value", "Value", Variant::NIL));
            pins.append(value_pin("found", "Found", Variant::BOOL));
        }
    } else if (constructor) {
        const int64_t count = valid_collection_arity(node) ? int64_t(node.get("collection_size", 1)) : 0;
        for (int64_t item = 1; item <= count; ++item) {
            const String number = String::num_int64(item);
            if (typed_dictionary) {
                append_dictionary_input(pins, StringName("key_" + number), "Key " + number, "key", types);
                append_dictionary_input(pins, StringName("value_" + number), "Value " + number, "value", types);
            } else {
                if (!array) append_input(pins, StringName("key_" + number), "Key " + number, Variant::NIL, Variant());
                append_input(pins, StringName((array ? String("item_") : String("value_")) + number),
                        (array ? String("Item ") : String("Value ")) + number, Variant::NIL, Variant());
            }
        }
    } else {
        const Variant empty = array ? Variant(Array()) : Variant(Dictionary());
        append_input(pins, array ? "array" : "dictionary", array ? "Array" : "Dictionary", type, empty);
        if (array && operation != Operation::APPEND_ARRAY_ITEM) append_input(pins, "index", "Index", Variant::INT, 0);
        if (!array) append_input(pins, "key", "Key", Variant::NIL, Variant());
        if (operation == Operation::SET_ARRAY_ITEM || operation == Operation::APPEND_ARRAY_ITEM ||
                operation == Operation::SET_DICTIONARY_VALUE) append_input(pins, "value", "Value", Variant::NIL, Variant());
    }
    return pins;
}

bool is_collection_constructor_input(const Dictionary &node, const StringName &pin) {
    const auto operation = collection_operation(node.get("kind", StringName()));
    return is_constructor(operation) && constructor_input_index(collection_operation_pins(operation, node, false, {}), pin) >= 0;
}

Dictionary collection_terminal_type(const Dictionary &pin) {
    Dictionary result;
    result["type"] = pin.get("type", int64_t(Variant::NIL));
    for (const char *field : {"class", "enum_type", "script_path", "script_name"}) {
        const Variant identity = pin.get(field, Variant());
        if ((identity.get_type() == Variant::STRING || identity.get_type() == Variant::STRING_NAME) &&
                !String(identity).is_empty()) {
            result[field] = String(identity);
        }
    }
    if (int64_t(result["type"]) == Variant::OBJECT && !result.has("class")) result["class"] = "Object";
    return result;
}

Dictionary collection_literal_type(const Variant &literal) {
    const auto type = literal.get_type();
    if (type == Variant::NIL) return {};
    Dictionary result;
    result["type"] = int64_t(type);
    if (type == Variant::OBJECT) {
        const Object *object = literal.get_validated_object();
        result["class"] = object ? object->get_class() : String("Object");
    }
    return result;
}

void set_dictionary_constructor_wire_type(Dictionary &node, const StringName &pin, const Dictionary &source) {
    const char *lane = dictionary_lane(pin);
    const Variant saved = node.get("dictionary_types", Variant());
    if (!lane || saved.get_type() != Variant::DICTIONARY) return;
    const Dictionary types = saved;
    if (!types.has(lane)) establish_dictionary_lane(node, types, lane, collection_terminal_type(source));
}

bool set_dictionary_constructor_literal_type(Dictionary &node, const StringName &pin, const Variant &value) {
    const char *lane = dictionary_lane(pin);
    const Variant saved = node.get("dictionary_types", Variant());
    if (!lane || saved.get_type() != Variant::DICTIONARY) return true;
    const Dictionary types = saved;
    const bool key = StringName(lane) == StringName("key");
    if (types.has(lane)) {
        const Variant saved_terminal = types[lane];
        if (saved_terminal.get_type() != Variant::DICTIONARY) return false;
        const Dictionary terminal = saved_terminal;
        const int64_t expected = terminal.get("type", Variant::NIL);
        if (expected == Variant::NIL) return true;
        if (expected == Variant::OBJECT && !asset_reference_path(value).is_empty()) return true;
        return (int64_t(value.get_type()) == expected || (expected == Variant::OBJECT && value.get_type() == Variant::NIL)) &&
                pin_accepts_value(terminal, value);
    }
    const Dictionary terminal = collection_literal_type(value);
    if (terminal.is_empty()) return true;
    if (key && !collection_key_type_allowed(terminal)) return false;
    establish_dictionary_lane(node, types, lane, terminal);
    return true;
}

bool resize_collection_constructor(Dictionary &graph, int64_t node_id, const StringName &remove_pin,
        const Dictionary &document, const StringName &base) {
    const Variant stored_nodes = graph.get("nodes", Variant());
    if (node_id <= 0 || stored_nodes.get_type() != Variant::ARRAY) return false;
    const Array nodes = stored_nodes;
    for (int64_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].get_type() != Variant::DICTIONARY) continue;
        const Dictionary node = nodes[index];
        const Variant identity = node.get("id", Variant());
        if (identity.get_type() != Variant::INT || int64_t(identity) != node_id) continue;
        const auto operation = collection_operation(node.get("kind", StringName()));
        if (!is_constructor(operation) || !valid_collection_arity(node)) return false;
        const int64_t count = node.get("collection_size", 1);
        Dictionary updated_graph = graph.duplicate();
        Dictionary next = node.duplicate();
        if (remove_pin.is_empty()) {
            if (count >= visual_script_max_collection_size) return false;
            if (operation == CollectionOperation::MAKE_DICTIONARY) adopt_dictionary_types_for_added_pin(next, document, graph, base);
            next["collection_size"] = count + 1;
        } else {
            const Array pins = collection_operation_pins(operation, node, false, {});
            const int64_t input_index = constructor_input_index(pins, remove_pin);
            if (input_index < 0) return false;
            const Dictionary remap = removed_entry_remap(pins, input_index, operation == CollectionOperation::MAKE_ARRAY ? 1 : 2);
            const Variant values = node.get("values", Variant());
            if (values.get_type() == Variant::DICTIONARY) next["values"] = remap_constructor_defaults(values, remap);
            const Variant wires = graph.get("connections", Variant());
            if (wires.get_type() == Variant::ARRAY) updated_graph["connections"] = remap_constructor_wires(wires, node_id, remap);
            next["collection_size"] = count - 1;
        }
        Array updated_nodes = nodes.duplicate();
        updated_nodes[index] = next;
        updated_graph["nodes"] = updated_nodes;
        graph = updated_graph;
        return true;
    }
    return false;
}

String evaluate_collection_operation(CollectionOperation operation, const Array &inputs, Dictionary &outputs,
        const Dictionary *dictionary_template) {
    using Operation = CollectionOperation;
    switch (operation) {
        case Operation::MAKE_ARRAY:
            outputs[StringName("value")] = inputs;
            return {};
        case Operation::MAKE_DICTIONARY: {
            Dictionary result = dictionary_template ? dictionary_template->duplicate() : Dictionary();
            const int64_t key_type = dictionary_template ? result.get_typed_key_builtin() : Variant::NIL;
            const int64_t value_type = dictionary_template ? result.get_typed_value_builtin() : Variant::NIL;
            for (int64_t entry = 0; entry < inputs.size(); entry += 2) {
                const Variant key = inputs[entry];
                if (key.get_type() == Variant::NIL) continue;
                if (key_type != Variant::NIL && key.get_type() != key_type) {
                    return "A Make Dictionary key does not match its type.";
                }
                if (!dictionary_key(key)) continue;
                const Variant value = inputs[entry + 1];
                if (value_type != Variant::NIL && !accepts_terminal(value, value_type,
                        result.get_typed_value_class_name(), result.get_typed_value_script())) {
                    return "A Make Dictionary value does not match its type.";
                }
                result[key] = value;
            }
            outputs[StringName("value")] = result;
            return {};
        }
        case Operation::GET_ARRAY_ITEM: {
            const Array source = inputs[0];
            const int64_t index = inputs[1];
            const bool found = index >= 0 && index < source.size();
            outputs[StringName("value")] = found ? source[index] : Variant();
            outputs[StringName("found")] = found;
            return {};
        }
        case Operation::GET_DICTIONARY_VALUE: {
            const Dictionary source = inputs[0];
            const Variant key = inputs[1];
            const bool found = map_accepts_key(source, key) && source.has(key);
            outputs[StringName("value")] = found ? source.get(key, Variant()) : Variant();
            outputs[StringName("found")] = found;
            return {};
        }
        case Operation::SET_ARRAY_ITEM: case Operation::APPEND_ARRAY_ITEM: case Operation::REMOVE_ARRAY_ITEM:
            edit_array(operation, inputs, outputs);
            return {};
        case Operation::SET_DICTIONARY_VALUE: case Operation::ERASE_DICTIONARY_KEY:
            edit_dictionary(operation, inputs, outputs);
            return {};
        default: return "This collection operation is unavailable.";
    }
}

} // namespace crimson::visual_scripting
