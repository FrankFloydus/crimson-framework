// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_staging.hpp"

#include "inventory_model_dto.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace crimson::inventory {
namespace {

// Builds a before-and-after placement edit.
Dictionary placement_change(const Variant &grid, const Variant &id, const Dictionary &before, const Dictionary &after) {
    return detail::make_model_dto("inventory", grid, "id", id, "before", before, "after", after);
}

} // namespace

SpatialInventoryStaging::SpatialInventoryStaging() {
    inventory.instantiate();
    actions.instantiate();
}

void SpatialInventoryStaging::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_pickup_policy", "policy"), &SpatialInventoryStaging::set_pickup_policy);
    ClassDB::bind_method(D_METHOD("get_pickup_policy"), &SpatialInventoryStaging::get_pickup_policy);
    ClassDB::bind_method(D_METHOD("set_drop_policy", "policy"), &SpatialInventoryStaging::set_drop_policy);
    ClassDB::bind_method(D_METHOD("get_drop_policy"), &SpatialInventoryStaging::get_drop_policy);
    ClassDB::bind_method(D_METHOD("set_batch_policy", "policy"), &SpatialInventoryStaging::set_batch_policy);
    ClassDB::bind_method(D_METHOD("get_batch_policy"), &SpatialInventoryStaging::get_batch_policy);
    ClassDB::bind_method(D_METHOD("get_inventory"), &SpatialInventoryStaging::get_inventory);
    ClassDB::bind_method(D_METHOD("get_last_reason"), &SpatialInventoryStaging::get_last_reason);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "pickup_policy"), "set_pickup_policy", "get_pickup_policy");
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "drop_policy"), "set_drop_policy", "get_drop_policy");
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "batch_policy"), "set_batch_policy", "get_batch_policy");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "inventory", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_INSTANCE_STATE,
            "SpatialInventory"), "", "get_inventory");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "last_reason", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_INSTANCE_STATE), "", "get_last_reason");
    ClassDB::bind_method(D_METHOD("configure", "size"), &SpatialInventoryStaging::configure);
    ClassDB::bind_method(D_METHOD("stage_from", "source", "id", "origin", "rotated"), &SpatialInventoryStaging::stage_from, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("place_item", "id", "destination", "origin", "rotated"), &SpatialInventoryStaging::place_item, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("restore_all"), &SpatialInventoryStaging::restore_all);
    ClassDB::bind_method(D_METHOD("is_empty"), &SpatialInventoryStaging::is_empty);
    ClassDB::bind_method(D_METHOD("try_close", "restore"), &SpatialInventoryStaging::try_close, DEFVAL(false));
}

void SpatialInventoryStaging::set_pickup_policy(const Callable &policy) { pickup_policy = policy; }
Callable SpatialInventoryStaging::get_pickup_policy() const { return pickup_policy; }
void SpatialInventoryStaging::set_drop_policy(const Callable &policy) { drop_policy = policy; }
Callable SpatialInventoryStaging::get_drop_policy() const { return drop_policy; }
void SpatialInventoryStaging::set_batch_policy(const Callable &policy) { batch_policy = policy; }
Callable SpatialInventoryStaging::get_batch_policy() const { return batch_policy; }
Ref<SpatialInventory> SpatialInventoryStaging::get_inventory() const { return inventory; }
String SpatialInventoryStaging::get_last_reason() const { return last_reason; }

Error SpatialInventoryStaging::configure(const Vector2i &size) {
    if (busy) {
        return report(ERR_BUSY, "A staging action is already running.");
    }
    if (!origins.is_empty() && inventory->get_grid_size() != size) {
        return report(ERR_BUSY, "Settle the staged items before resizing the tray.");
    }
    busy = true;
    const Error error = inventory->configure(size);
    busy = false;
    return report(error);
}

Error SpatialInventoryStaging::stage_from(const Ref<SpatialInventory> &source,
        const StringName &id, const Vector2i &origin, bool rotated) {
    if (busy) {
        return report(ERR_BUSY, "A staging action is already running.");
    }
    if (source.is_null() || source == inventory || id.is_empty()) {
        return report(ERR_INVALID_PARAMETER, "Choose an item from another inventory.");
    }
    if (origins.has(id)) {
        return report(ERR_ALREADY_EXISTS, "The tray already tracks this item.");
    }
    const Dictionary before = source->get_placement(id);
    if (before.is_empty()) {
        return report(ERR_DOES_NOT_EXIST, "The source item does not exist.");
    }
    busy = true;
    prepare_actions();
    const Dictionary result = actions->move_item(source, id, inventory, origin, rotated, {}, before);
    const Error error = Error(int64_t(result["error"]));
    if (error == OK) {
        origins[id] = detail::make_model_dto("inventory", source, "placement", before);
    }
    busy = false;
    return report(error, result["reason"]);
}

Error SpatialInventoryStaging::place_item(const StringName &id, const Ref<SpatialInventory> &destination,
        const Vector2i &origin, bool rotated) {
    if (busy) {
        return report(ERR_BUSY, "A staging action is already running.");
    }
    if (destination.is_null() || destination == inventory || id.is_empty()) {
        return report(ERR_INVALID_PARAMETER, "Choose a destination outside the staging tray.");
    }
    if (!origins.has(id)) {
        return report(ERR_DOES_NOT_EXIST, "This item has no recorded staging origin.");
    }
    const Dictionary record = origins[id];
    const Ref<SpatialInventory> original = record["inventory"];
    if (!original->get_placement(id).is_empty()) {
        return report(ERR_BUSY, "The original inventory already owns this item.");
    }

    busy = true;
    prepare_actions();
    actions->set_operation_guard(callable_mp(this, &SpatialInventoryStaging::original_unoccupied).bind(original, id));
    const Dictionary result = actions->move_item(inventory, id, destination, origin, rotated);
    actions->set_operation_guard({});
    const Error error = Error(int64_t(result["error"]));
    if (error == OK) {
        origins.erase(id);
    }
    busy = false;
    return report(error, result["reason"]);
}

bool SpatialInventoryStaging::original_unoccupied(const Ref<SpatialInventory> &, const Ref<SpatialInventory> &,
        const Ref<SpatialInventory> &original, const StringName &id) {
    return original->get_placement(id).is_empty();
}

Error SpatialInventoryStaging::restore_all() {
    if (busy) {
        return report(ERR_BUSY, "A staging action is already running.");
    }
    busy = true;
    const Error result = restore_checked();
    busy = false;
    return result;
}

bool SpatialInventoryStaging::is_empty() const {
    return origins.is_empty() && inventory->get_placements().is_empty();
}

Error SpatialInventoryStaging::try_close(bool restore) {
    if (busy) {
        return report(ERR_BUSY, "A staging action is already running.");
    }
    if (is_empty()) {
        return report(OK);
    }
    if (!restore) {
        return report(ERR_BUSY, "Place or restore the staged items before closing.");
    }
    const Error error = restore_all();
    if (error == OK && !is_empty()) {
        return report(ERR_BUSY, "The tray still contains items.");
    }
    return error;
}

Error SpatialInventoryStaging::restore_checked() {
    const Array staged = inventory->get_placements();
    if (staged.size() != origins.size()) {
        return report(ERR_BUSY, "The tray no longer matches its recorded items.");
    }
    if (staged.is_empty()) {
        return report(OK);
    }

    TypedArray<Dictionary> changes;
    for (int64_t index = 0; index < staged.size(); ++index) {
        const Dictionary placement = staged[index];
        const StringName id = placement["id"];
        if (!origins.has(id)) {
            return report(ERR_BUSY, "An item in the tray has no recorded origin.");
        }
        const Dictionary record = origins[id];
        const Dictionary original = record["placement"];
        if (placement["footprint"] != original["footprint"]) {
            return report(ERR_BUSY, "A staged item's footprint changed.");
        }
        changes.append(placement_change(inventory, id, placement, {}));
        changes.append(placement_change(record["inventory"], id, {}, original));
    }
    for (int64_t index = 0; index < changes.size(); index += 2) {
        const Dictionary source_row = changes[index];
        const String denial = restore_denial(source_row["before"], changes[index + 1]);
        if (!denial.is_empty()) {
            return report(ERR_UNAUTHORIZED, denial);
        }
    }
    if (batch_policy.is_valid()) {
        const Variant decision = batch_policy.call(changes.duplicate(true));
        if (decision.get_type() != Variant::STRING) {
            return report(ERR_UNAUTHORIZED, "Batch policy must return a String.");
        }
        if (!String(decision).is_empty()) {
            return report(ERR_UNAUTHORIZED, decision);
        }
    }

    const Error error = SpatialInventory::apply_placement_changes(changes);
    if (error == OK) {
        origins.clear();
    } else if (error == ERR_ALREADY_IN_USE) {
        return report(error, "An original placement is occupied.");
    } else if (error == ERR_BUSY) {
        return report(error, "A staged placement or its ownership changed.");
    }
    return report(error);
}

String SpatialInventoryStaging::restore_denial(const Dictionary &placement, const Dictionary &destination_row) const {
    if (pickup_policy.is_valid()) {
        const Variant pickup = pickup_policy.call(detail::make_model_dto(
                "id", placement["id"], "footprint", placement["footprint"], "rotated", placement["rotated"],
                "source", inventory, "origin", placement["origin"]));
        if (pickup.get_type() != Variant::STRING) {
            return "Pickup policy must return a String.";
        }
        if (!String(pickup).is_empty()) {
            return pickup;
        }
    }
    if (drop_policy.is_valid()) {
        const Dictionary original = destination_row["after"];
        const Variant drop = drop_policy.call(detail::make_model_dto(
                "id", original["id"], "footprint", original["footprint"], "rotated", original["rotated"],
                "source", inventory, "destination", destination_row["inventory"], "origin", original["origin"],
                "swap_id", StringName(), "action", StringName("place")));
        return drop.get_type() == Variant::STRING ? String(drop) : String("Drop policy must return a String.");
    }
    return {};
}

void SpatialInventoryStaging::prepare_actions() {
    actions->set_pickup_policy(pickup_policy);
    actions->set_drop_policy(drop_policy);
}

Error SpatialInventoryStaging::report(Error error, const String &reason) {
    last_reason = error == OK ? String() : (reason.is_empty() ? UtilityFunctions::error_string(error) : reason);
    return error;
}

} // namespace crimson::inventory
