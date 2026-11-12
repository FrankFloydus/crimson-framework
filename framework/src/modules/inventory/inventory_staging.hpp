// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "inventory_actions.hpp"
#include "spatial_inventory.h"

namespace crimson::inventory {

class SpatialInventoryStaging : public godot::RefCounted {
    GDCLASS(SpatialInventoryStaging, godot::RefCounted)

    // Host callback that can deny lifting a source item.
    godot::Callable pickup_policy;
    // Host callback that can deny a destination placement.
    godot::Callable drop_policy;
    // Host callback that can deny restoring a batch of staged placements.
    godot::Callable batch_policy;
    // Inventory grid referenced by this record.
    godot::Ref<godot::SpatialInventory> inventory;
    // Inventory action model used to apply staging transfers.
    godot::Ref<SpatialInventoryActions> actions;
    // Original inventory and placement for each staged item.
    godot::Dictionary origins;
    // Explanation from the most recent staging result.
    godot::String last_reason;
    // Guards state changes against reentrant callbacks.
    bool busy = false;

    // Configures the transfer model with the current staging policies.
    void prepare_actions();
    // Validates the complete restoration plan before committing it.
    godot::Error restore_checked();
    // Checks whether a staged item can return to its destination.
    godot::String restore_denial(const godot::Dictionary &placement, const godot::Dictionary &destination_row) const;
    // Stores the current staging outcome and returns its error code.
    godot::Error report(godot::Error error, const godot::String &reason = {});
    // Checks that a staged item is still absent from the destination.
    bool original_unoccupied(const godot::Ref<godot::SpatialInventory> &source,
            const godot::Ref<godot::SpatialInventory> &destination,
            const godot::Ref<godot::SpatialInventory> &original, const godot::StringName &id);

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Creates the internal staging grid and action model.
    SpatialInventoryStaging();
    // Sets the host callback that can deny lifting a source item.
    void set_pickup_policy(const godot::Callable &policy);
    // Returns the host callback that can deny lifting a source item.
    godot::Callable get_pickup_policy() const;
    // Sets the host callback that can deny a proposed destination placement.
    void set_drop_policy(const godot::Callable &policy);
    // Returns the host callback that can deny a proposed destination placement.
    godot::Callable get_drop_policy() const;
    // Sets the host policy for restoring multiple staged placements.
    void set_batch_policy(const godot::Callable &policy);
    // Returns the host policy for restoring multiple staged placements.
    godot::Callable get_batch_policy() const;
    // Returns the staging model's internal grid.
    godot::Ref<godot::SpatialInventory> get_inventory() const;
    // Returns the explanation for the most recent staging outcome.
    godot::String get_last_reason() const;
    // Sets the size of the internal staging grid.
    godot::Error configure(const godot::Vector2i &size);
    // Moves an item into the staging grid and records its original placement.
    godot::Error stage_from(const godot::Ref<godot::SpatialInventory> &source,
            const godot::StringName &id, const godot::Vector2i &origin, bool rotated = false);
    // Moves a staged item into the requested destination.
    godot::Error place_item(const godot::StringName &id, const godot::Ref<godot::SpatialInventory> &destination,
            const godot::Vector2i &origin, bool rotated = false);
    // Attempts to return every staged item to its original inventory.
    godot::Error restore_all();
    // Reports whether the staging grid contains any items.
    bool is_empty() const;
    // Closes an empty staging grid or attempts restoration first.
    godot::Error try_close(bool restore = false);
};

} // namespace crimson::inventory
