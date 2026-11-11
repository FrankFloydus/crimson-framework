// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_containers.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::inventory {
namespace {

// Tests whether a Variant can represent a StringName identifier.
bool is_name(const Variant &value) {
    return (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) &&
            !StringName(value).is_empty();
}

// Checks that a placement record agrees with its change-row ID.
bool matches_change_id(const Dictionary &placement, const StringName &id) {
    const Variant actual = placement.get("id", {});
    return placement.is_empty() || (is_name(actual) && StringName(actual) == id);
}

} // namespace

void SpatialInventoryContainers::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_host_placement_policy", "policy"), &SpatialInventoryContainers::set_host_placement_policy);
    ClassDB::bind_method(D_METHOD("get_host_placement_policy"), &SpatialInventoryContainers::get_host_placement_policy);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "host_placement_policy"), "set_host_placement_policy", "get_host_placement_policy");
    ClassDB::bind_method(D_METHOD("register_inventory", "inventory", "container_id"), &SpatialInventoryContainers::register_inventory, DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("unregister_inventory", "inventory"), &SpatialInventoryContainers::unregister_inventory);
    ClassDB::bind_method(D_METHOD("get_contents", "container_id"), &SpatialInventoryContainers::get_contents);
    ClassDB::bind_method(D_METHOD("get_owner_inventory", "item_id"), &SpatialInventoryContainers::get_owner_inventory);
    ClassDB::bind_method(D_METHOD("placement_policy", "context"), &SpatialInventoryContainers::placement_policy);
    ClassDB::bind_method(D_METHOD("validate_changes", "changes"), &SpatialInventoryContainers::validate_changes);
}

void SpatialInventoryContainers::set_host_placement_policy(const Callable &policy) { host_placement_policy = policy; }
Callable SpatialInventoryContainers::get_host_placement_policy() const { return host_placement_policy; }

Error SpatialInventoryContainers::register_inventory(const Ref<SpatialInventory> &inventory, const StringName &container_id) {
    if (inventory.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    if (container_by_inventory.has(inventory) || contents_by_id.has(container_id)) {
        return ERR_ALREADY_EXISTS;
    }
    if (!container_by_inventory.is_empty()) {
        const Array placements = inventory->get_placements();
        for (int64_t index = 0; index < placements.size(); ++index) {
            const Dictionary placement = placements[index];
            if (has_owner_elsewhere(placement["id"])) {
                return ERR_ALREADY_EXISTS;
            }
        }
    }

    container_by_inventory[inventory] = container_id;
    if (container_id.is_empty()) {
        return OK;
    }
    contents_by_id[container_id] = inventory;
    const Owner owner = find_owner(container_id);
    Error error = OK;
    if (owner.duplicate) {
        error = ERR_ALREADY_EXISTS;
    } else if (owner.inventory.is_valid() && !cycle_reason(container_id, owner.inventory).is_empty()) {
        error = ERR_INVALID_DATA;
    }
    if (error != OK) {
        unregister_inventory(inventory);
    }
    return error;
}

Error SpatialInventoryContainers::unregister_inventory(const Ref<SpatialInventory> &inventory) {
    if (inventory.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    if (!container_by_inventory.has(inventory)) {
        return ERR_DOES_NOT_EXIST;
    }
    const StringName id = container_by_inventory[inventory];
    container_by_inventory.erase(inventory);
    if (!id.is_empty()) {
        contents_by_id.erase(id);
    }
    return OK;
}

Ref<SpatialInventory> SpatialInventoryContainers::get_contents(const StringName &container_id) const {
    return contents_by_id.get(container_id, {});
}

Ref<SpatialInventory> SpatialInventoryContainers::get_owner_inventory(const StringName &id) const {
    return find_owner(id).inventory;
}

String SpatialInventoryContainers::placement_policy(const Dictionary &context) const {
    const Variant raw_id = context.get("id", {});
    if (!is_name(raw_id)) {
        return "A valid item ID is required.";
    }
    const StringName id = raw_id;
    const Variant raw_source = context.get("source", {});
    const Ref<SpatialInventory> source = raw_source;
    const Ref<SpatialInventory> destination = context.get("destination", {});
    if (destination.is_null() || !container_by_inventory.has(destination) ||
            (raw_source.get_type() != Variant::NIL && (source.is_null() || !container_by_inventory.has(source)))) {
        return "The proposed inventory is outside the container registry.";
    }
    if (has_owner_elsewhere(id, source)) {
        return "The item ID already belongs to another inventory.";
    }

    const Variant raw_action = context.get("action", StringName("place"));
    if (!is_name(raw_action)) {
        return "A valid placement action is required.";
    }
    const StringName action = raw_action;
    if (action == StringName("merge")) {
        const Variant raw_target = context.get("target_id", {});
        if (!is_name(raw_target)) {
            return "A valid merge target ID is required.";
        }
        const StringName target = raw_target;
        if (contents_by_id.has(id) || contents_by_id.has(target)) {
            return "Container items cannot be merged.";
        }
        if (has_owner_elsewhere(target, destination)) {
            return "The target ID already belongs to another inventory.";
        }
    } else if (action == StringName("split")) {
        const Variant raw_new_id = context.get("new_id", StringName());
        if (raw_new_id.get_type() != Variant::STRING && raw_new_id.get_type() != Variant::STRING_NAME) {
            return "A valid split ID is required.";
        }
        const StringName new_id = raw_new_id;
        if (contents_by_id.has(id)) {
            return "Container items cannot be split.";
        }
        if (!new_id.is_empty() && (contents_by_id.has(new_id) || has_owner_elsewhere(new_id))) {
            return "The new item ID is already in use.";
        }
    } else if (action == StringName("swap")) {
        const Variant raw_swap = context.get("swap_id", {});
        if (!is_name(raw_swap)) {
            return "A valid swap target ID is required.";
        }
        if (has_owner_elsewhere(StringName(raw_swap), destination)) {
            return "The swap target ID already belongs to another inventory.";
        }
    }

    if (contents_by_id.has(id)) {
        const String reason = cycle_reason(id, destination);
        if (!reason.is_empty()) {
            return reason;
        }
    }
    if (host_placement_policy.is_valid()) {
        const Variant reason = host_placement_policy.call(context);
        return reason.get_type() == Variant::STRING ? String(reason) : String("Container placement policy must return a String.");
    }
    return {};
}

String SpatialInventoryContainers::validate_changes(const Array &changes) const {
    Dictionary overrides;
    for (int64_t index = 0; index < changes.size(); ++index) {
        const Variant &raw_row = changes[index];
        if (raw_row.get_type() != Variant::DICTIONARY) {
            return "Invalid container placement change.";
        }
        const Dictionary row = raw_row;
        const Ref<SpatialInventory> inventory = row.get("inventory", {});
        const Variant raw_id = row.get("id", {});
        const Variant before = row.get("before", {});
        const Variant after = row.get("after", {});
        if (inventory.is_null() || !container_by_inventory.has(inventory) || !is_name(raw_id) ||
                before.get_type() != Variant::DICTIONARY || after.get_type() != Variant::DICTIONARY) {
            return "Invalid container placement change.";
        }
        const StringName id = raw_id;
        if (!matches_change_id(before, id) || !matches_change_id(after, id)) {
            return "A placement change has a mismatched item ID.";
        }
        Dictionary owner_overrides = overrides.get(id, Dictionary());
        if (owner_overrides.has(inventory)) {
            return "The same inventory and item ID appear twice in the batch.";
        }
        owner_overrides[inventory] = !Dictionary(after).is_empty();
        overrides[id] = owner_overrides;
    }

    const Array ids = overrides.keys();
    for (int64_t index = 0; index < ids.size(); ++index) {
        const StringName id = ids[index];
        const Owner owner = find_owner(id, overrides);
        if (owner.duplicate) {
            return "The batch would place the same item ID in multiple inventories.";
        }
        if (contents_by_id.has(id) && owner.inventory.is_valid()) {
            const String reason = cycle_reason(id, owner.inventory, overrides);
            if (!reason.is_empty()) {
                return reason;
            }
        }
    }
    return {};
}

String SpatialInventoryContainers::cycle_reason(const StringName &id, Ref<SpatialInventory> current, const Dictionary &overrides) const {
    Dictionary visited;
    while (current.is_valid()) {
        const StringName container = container_by_inventory.get(current, StringName());
        if (container.is_empty()) {
            return {};
        }
        if (container == id) {
            return "A container cannot enter its own storage or a descendant container.";
        }
        if (visited.has(current)) {
            return "The destination already belongs to a container cycle.";
        }
        visited[current] = true;
        const Owner owner = find_owner(container, overrides);
        if (owner.duplicate) {
            return "The destination's container ID belongs to multiple inventories.";
        }
        current = owner.inventory;
    }
    return {};
}

SpatialInventoryContainers::Owner SpatialInventoryContainers::find_owner(const StringName &id, const Dictionary &overrides) const {
    Owner owner;
    const Dictionary changes = overrides.get(id, Dictionary());
    const Array inventories = container_by_inventory.keys();
    for (int64_t index = 0; index < inventories.size(); ++index) {
        const Ref<SpatialInventory> grid = inventories[index];
        const bool present = changes.has(grid) ? bool(changes[grid]) : !grid->get_placement(id).is_empty();
        if (!present) {
            continue;
        }
        if (owner.inventory.is_valid()) {
            return {{}, true};
        }
        owner.inventory = grid;
    }
    return owner;
}

bool SpatialInventoryContainers::has_owner_elsewhere(const StringName &id, const Ref<SpatialInventory> &allowed) const {
    const Array inventories = container_by_inventory.keys();
    for (int64_t index = 0; index < inventories.size(); ++index) {
        const Ref<SpatialInventory> grid = inventories[index];
        if (grid != allowed && !grid->get_placement(id).is_empty()) {
            return true;
        }
    }
    return false;
}

} // namespace crimson::inventory
