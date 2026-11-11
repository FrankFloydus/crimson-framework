// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "spatial_inventory.h"

#include <godot_cpp/variant/callable.hpp>

namespace crimson::inventory {

class SpatialInventoryContainers : public godot::RefCounted {
    GDCLASS(SpatialInventoryContainers, godot::RefCounted)

    struct Owner {
        // Inventory grid referenced by this record.
        godot::Ref<godot::SpatialInventory> inventory;
        // Whether more than one grid claims the same item.
        bool duplicate = false;
    };

    // Game rule consulted after container ownership checks.
    godot::Callable host_placement_policy;
    // Container item associated with each registered contents grid.
    godot::Dictionary container_by_inventory;
    // Registered contents grid indexed by container item ID.
    godot::Dictionary contents_by_id;

    // Finds the item's registered owner while accounting for staged overrides.
    Owner find_owner(const godot::StringName &id, const godot::Dictionary &overrides = {}) const;
    // Checks whether a different grid already claims the item.
    bool has_owner_elsewhere(const godot::StringName &id,
            const godot::Ref<godot::SpatialInventory> &allowed = {}) const;
    // Reports whether a proposed move would create a container cycle.
    godot::String cycle_reason(const godot::StringName &id, godot::Ref<godot::SpatialInventory> destination,
            const godot::Dictionary &overrides = {}) const;

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Sets the game rule used after container ownership validation.
    void set_host_placement_policy(const godot::Callable &policy);
    // Returns the game rule used after container ownership validation.
    godot::Callable get_host_placement_policy() const;
    // Registers a contents grid and its optional container item.
    godot::Error register_inventory(const godot::Ref<godot::SpatialInventory> &inventory,
            const godot::StringName &container_id = {});
    // Removes a grid from container ownership tracking.
    godot::Error unregister_inventory(const godot::Ref<godot::SpatialInventory> &inventory);
    // Returns the registered contents grid for the container item.
    godot::Ref<godot::SpatialInventory> get_contents(const godot::StringName &container_id) const;
    // Returns the unique registered grid owning the item.
    godot::Ref<godot::SpatialInventory> get_owner_inventory(const godot::StringName &id) const;
    // Validates a proposed placement against ownership, nesting, and host rules.
    godot::String placement_policy(const godot::Dictionary &context) const;
    // Validates ownership and nesting for the complete pending placement batch.
    godot::String validate_changes(const godot::Array &changes) const;
};

} // namespace crimson::inventory
