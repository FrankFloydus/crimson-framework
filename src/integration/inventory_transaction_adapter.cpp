// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_transaction_adapter.hpp"

#include "modules/inventory/spatial_inventory.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <limits>

using namespace godot;

namespace crimson::integration {
namespace {

// Encodes a grid vector as two portable integer components.
Array encode_cell(const Vector2i &cell) {
    return Array::make(cell.x, cell.y);
}

// Validates and decodes two integers within the native vector range.
bool decode_cell(const Variant &value, Vector2i &cell) {
    if (value.get_type() != Variant::ARRAY) {
        return false;
    }
    const Array components = value;
    if (components.size() != 2 || components[0].get_type() != Variant::INT ||
            components[1].get_type() != Variant::INT) {
        return false;
    }
    const int64_t x = components[0];
    const int64_t y = components[1];
    constexpr int64_t lower = std::numeric_limits<int32_t>::min();
    constexpr int64_t upper = std::numeric_limits<int32_t>::max();
    if (x < lower || x > upper || y < lower || y > upper) {
        return false;
    }
    cell = Vector2i(static_cast<int32_t>(x), static_cast<int32_t>(y));
    return true;
}

// Converts a validated native grid snapshot to portable store data.
Dictionary encode_snapshot(const Dictionary &native_snapshot) {
    Dictionary snapshot;
    snapshot["version"] = native_snapshot["version"];
    snapshot["grid_size"] = encode_cell(native_snapshot["grid_size"]);
    const Array native_rows = native_snapshot["placements"];
    Array rows;
    for (int64_t index = 0; index < native_rows.size(); ++index) {
        const Dictionary native_row = native_rows[index];
        Dictionary row;
        const StringName native_id = native_row["id"];
        row["id"] = String(native_id);
        row["footprint"] = encode_cell(native_row["footprint"]);
        row["origin"] = encode_cell(native_row["origin"]);
        row["rotated"] = native_row["rotated"];
        rows.push_back(row);
    }
    snapshot["placements"] = rows;
    return snapshot;
}

// Validates portable fields and reconstructs the native snapshot shape.
Dictionary decode_snapshot(const Dictionary &snapshot) {
    Vector2i size;
    const Variant version = snapshot.get("version", Variant());
    const Variant placements = snapshot.get("placements", Variant());
    if (version.get_type() != Variant::INT || placements.get_type() != Variant::ARRAY ||
            !decode_cell(snapshot.get("grid_size", Variant()), size)) {
        return {};
    }

    const Array rows = placements;
    Array native_rows;
    for (int64_t index = 0; index < rows.size(); ++index) {
        if (rows[index].get_type() != Variant::DICTIONARY) {
            return {};
        }
        const Dictionary row = rows[index];
        const Variant id = row.get("id", Variant());
        const Variant rotated = row.get("rotated", Variant());
        Vector2i footprint;
        Vector2i origin;
        if (id.get_type() != Variant::STRING || rotated.get_type() != Variant::BOOL ||
                !decode_cell(row.get("footprint", Variant()), footprint) ||
                !decode_cell(row.get("origin", Variant()), origin)) {
            return {};
        }
        Dictionary native_row;
        native_row["id"] = StringName(String(id));
        native_row["footprint"] = footprint;
        native_row["origin"] = origin;
        native_row["rotated"] = rotated;
        native_rows.push_back(native_row);
    }

    Dictionary native_snapshot;
    native_snapshot["version"] = version;
    native_snapshot["grid_size"] = size;
    native_snapshot["placements"] = native_rows;
    return native_snapshot;
}

// Validates and applies one requested placement edit to a detached grid.
Error apply_operation(SpatialInventory &inventory, const Dictionary &operation) {
    const Variant kind_value = operation.get("kind", Variant());
    const Variant item_value = operation.get("item_id", Variant());
    if (kind_value.get_type() != Variant::STRING || item_value.get_type() != Variant::STRING) {
        return ERR_INVALID_PARAMETER;
    }
    const String kind = kind_value;
    const StringName item_id{ String(item_value) };
    if (item_id.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    if (kind == "remove") {
        return inventory.remove(item_id);
    }

    Vector2i origin;
    const Variant rotated = operation.get("rotated", Variant());
    if (rotated.get_type() != Variant::BOOL || !decode_cell(operation.get("origin", Variant()), origin)) {
        return ERR_INVALID_PARAMETER;
    }
    if (kind == "move") {
        return inventory.try_move(item_id, origin, bool(rotated));
    }
    Vector2i footprint;
    if (kind == "place" && decode_cell(operation.get("footprint", Variant()), footprint)) {
        return inventory.try_place(item_id, footprint, origin, bool(rotated));
    }
    return ERR_INVALID_PARAMETER;
}

// Builds a rejection response for the coordinator's prepare callback.
Dictionary rejected(const String &message) {
    Dictionary result;
    result["ok"] = false;
    result["message"] = message;
    return result;
}

// Stages an entire operation batch in a detached inventory and returns the resulting store snapshot.
Dictionary prepare_inventory(const Dictionary &current, const Array &operations, const Dictionary &) {
    Ref<SpatialInventory> candidate;
    candidate.instantiate();
    if (candidate->import_snapshot(decode_snapshot(current)) != OK) {
        return rejected("The inventory store could not be loaded.");
    }
    for (int64_t index = 0; index < operations.size(); ++index) {
        if (operations[index].get_type() != Variant::DICTIONARY) {
            return rejected("An inventory operation is invalid.");
        }
        const Error result = apply_operation(*candidate.ptr(), operations[index]);
        if (result != OK) {
            return rejected("Inventory operation " + String::num_int64(index + 1) +
                    " failed with Godot error " + String::num_int64(result) + ".");
        }
    }
    Dictionary result;
    result["ok"] = true;
    result["state"] = encode_snapshot(candidate->export_snapshot());
    return result;
}

// Builds a portable placement edit for a target cell and rotation.
Dictionary position_operation(const char *kind, const String &item_id, const Vector2i &origin, bool rotated) {
    Dictionary operation;
    operation["kind"] = kind;
    operation["item_id"] = item_id;
    operation["origin"] = encode_cell(origin);
    operation["rotated"] = rotated;
    return operation;
}

} // namespace

void CrimsonInventoryTransactionAdapter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "coordinator", "store_id", "initial_snapshot"),
            &CrimsonInventoryTransactionAdapter::configure);
    ClassDB::bind_method(D_METHOD("get_snapshot"), &CrimsonInventoryTransactionAdapter::get_snapshot);
    ClassDB::bind_method(D_METHOD("queue_place", "transaction", "item_id", "footprint", "origin", "rotated"),
            &CrimsonInventoryTransactionAdapter::queue_place, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("queue_move", "transaction", "item_id", "origin", "rotated"),
            &CrimsonInventoryTransactionAdapter::queue_move, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("queue_remove", "transaction", "item_id"),
            &CrimsonInventoryTransactionAdapter::queue_remove);
}

Error CrimsonInventoryTransactionAdapter::configure(const Ref<match_runtime::CrimsonTransactionCoordinator> &p_coordinator,
        const String &p_store_id, const Dictionary &initial_snapshot) {
    if (coordinator.is_valid()) {
        return ERR_ALREADY_IN_USE;
    }
    if (p_coordinator.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    Ref<SpatialInventory> inventory;
    inventory.instantiate();
    const Error validation = inventory->import_snapshot(initial_snapshot);
    if (validation != OK) {
        return validation;
    }
    const Error registered = p_coordinator->register_store(p_store_id, encode_snapshot(inventory->export_snapshot()),
            callable_mp_static(&prepare_inventory));
    if (registered == OK) {
        coordinator = p_coordinator;
        store_id = p_store_id;
    }
    return registered;
}

Dictionary CrimsonInventoryTransactionAdapter::get_snapshot() const {
    return coordinator.is_valid() ? decode_snapshot(coordinator->read_store(store_id)) : Dictionary();
}

Error CrimsonInventoryTransactionAdapter::queue_operation(const Ref<match_runtime::CrimsonTransaction> &transaction,
        const Dictionary &operation) const {
    if (coordinator.is_null()) {
        return ERR_UNCONFIGURED;
    }
    if (!coordinator->owns_transaction(transaction)) {
        return ERR_INVALID_PARAMETER;
    }
    return transaction->add_change(store_id, operation, coordinator->get_store_revision(store_id));
}

Error CrimsonInventoryTransactionAdapter::queue_place(const Ref<match_runtime::CrimsonTransaction> &transaction,
        const String &item_id, const Vector2i &footprint, const Vector2i &origin, bool rotated) const {
    Dictionary operation = position_operation("place", item_id, origin, rotated);
    operation["footprint"] = encode_cell(footprint);
    return queue_operation(transaction, operation);
}

Error CrimsonInventoryTransactionAdapter::queue_move(const Ref<match_runtime::CrimsonTransaction> &transaction,
        const String &item_id, const Vector2i &origin, bool rotated) const {
    return queue_operation(transaction, position_operation("move", item_id, origin, rotated));
}

Error CrimsonInventoryTransactionAdapter::queue_remove(const Ref<match_runtime::CrimsonTransaction> &transaction,
        const String &item_id) const {
    Dictionary operation;
    operation["kind"] = "remove";
    operation["item_id"] = item_id;
    return queue_operation(transaction, operation);
}

} // namespace crimson::integration
