// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "inventory_stack_adapter.hpp"
#include "spatial_inventory.h"

#include <map>

#include <godot_cpp/variant/typed_array.hpp>

namespace crimson::inventory {

class SpatialInventoryActions : public godot::RefCounted {
    GDCLASS(SpatialInventoryActions, godot::RefCounted)

public:
    // Moves or merges an item into available destinations in order.
    godot::Dictionary quick_transfer(const godot::Ref<godot::SpatialInventory> &source,
            const godot::StringName &id, const godot::Array &destinations = {});
    // Transfers items sharing the selected stack key into the destinations.
    godot::Dictionary quick_transfer_matching(const godot::Ref<godot::SpatialInventory> &source,
            const godot::StringName &id, const godot::Array &destinations = {});
    // Splits a quantity into a new stack and places the result.
    godot::Dictionary split_stack(const godot::Ref<godot::SpatialInventory> &source,
            const godot::StringName &id, int64_t amount, const godot::StringName &new_id,
            const godot::Ref<godot::SpatialInventory> &destination, const godot::Vector2i &origin,
            bool rotated = false);
    // Moves an item or swaps it with the expected destination item.
    godot::Dictionary move_item(const godot::Ref<godot::SpatialInventory> &source,
            const godot::StringName &id, const godot::Ref<godot::SpatialInventory> &destination,
            const godot::Vector2i &origin, bool rotated = false, const godot::StringName &swap_id = {},
            const godot::Dictionary &expected_source = {}, const godot::Dictionary &expected_swap = {});
    // Transfers compatible quantities between two placed stacks.
    godot::Dictionary merge_stacks(const godot::Ref<godot::SpatialInventory> &source,
            const godot::StringName &id, const godot::Ref<godot::SpatialInventory> &destination,
            const godot::StringName &target_id, int64_t limit = 0);
    // Returns IDs whose placements satisfy the optional predicate.
    static godot::TypedArray<godot::StringName> find_items(const godot::Ref<godot::SpatialInventory> &inventory,
            const godot::Callable &predicate = {});

    // Sets the adapter for host-owned stack records.
    void set_stack_adapter(const godot::Ref<SpatialInventoryStackAdapter> &adapter);
    // Returns the adapter for host-owned stack records.
    godot::Ref<SpatialInventoryStackAdapter> get_stack_adapter() const;
    // Sets the host callback that can deny lifting a source item.
    void set_pickup_policy(const godot::Callable &policy);
    // Returns the host callback that can deny lifting a source item.
    godot::Callable get_pickup_policy() const;
    // Sets the host callback that can deny a proposed destination placement.
    void set_drop_policy(const godot::Callable &policy);
    // Returns the host callback that can deny a proposed destination placement.
    godot::Callable get_drop_policy() const;
    // Sets the callback that filters quick-transfer destinations.
    void set_destination_policy(const godot::Callable &policy);
    // Returns the callback that filters quick-transfer destinations.
    godot::Callable get_destination_policy() const;
    // Sets the callback that supplies quick-transfer destinations.
    void set_destination_provider(const godot::Callable &provider);
    // Returns the callback that supplies quick-transfer destinations.
    godot::Callable get_destination_provider() const;
    // Sets the host guard for source and destination operations.
    void set_operation_guard(const godot::Callable &guard);
    // Returns the host guard for source and destination operations.
    godot::Callable get_operation_guard() const;
    // Sets whether automatic placement may rotate an item.
    void set_allow_rotation(bool enabled);
    // Returns whether automatic placement may rotate an item.
    bool get_allow_rotation() const;

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

private:
    using Grid = godot::Ref<godot::SpatialInventory>;
    using Adapter = godot::Ref<SpatialInventoryStackAdapter>;

    struct Receivers {
        // Placement revision used to detect stale plans.
        uint64_t placement_revision;
        // Stack revision when this receiver last had no free capacity.
        int64_t empty_capacity_revision = -1;
        // Item IDs eligible to receive transferred units.
        godot::TypedArray<godot::StringName> ids;
    };
    using ReceiverCache = std::map<Grid, Receivers>;

    struct Status {
        // Godot error code for the operation.
        godot::Error error = godot::OK;
        // Human-readable explanation for rejecting the operation.
        godot::String reason;
    };

    struct Position {
        // Top-left cell of the placement.
        godot::Vector2i origin{-1, -1};
        // Whether the placement swaps its footprint axes.
        bool rotated = false;
        // Reason a candidate placement was denied.
        godot::String denial;
    };

    // Adapter for reading and committing host-owned stack records.
    Adapter stack_adapter;
    // Host callback that can deny lifting a source item.
    godot::Callable pickup_policy;
    // Host callback that can deny a destination placement.
    godot::Callable drop_policy;
    // Host callback that filters quick-transfer destinations.
    godot::Callable destination_policy;
    // Host callback that resolves available quick-transfer destinations.
    godot::Callable destination_provider;
    // Host callback that can reject an operation against current models.
    godot::Callable operation_guard;
    // Whether automatic searches may rotate a footprint.
    bool allow_rotation = true;
    // Reentrancy guard shared by synchronous inventory actions.
    bool busy = false;

    // Transfers one source item using the available destination grids.
    godot::Dictionary transfer_one(const Grid &source, const godot::Dictionary &placement,
            const godot::Array &destinations, const Adapter &adapter, const godot::StringName &key,
            ReceiverCache &cache);
    // Resolves and filters eligible destination inventories.
    Status transfer_destinations(const Grid &source, const godot::Dictionary &placement,
            const godot::Array &destinations, std::vector<Grid> &models) const;
    // Merges into compatible receiver stacks and collects refresh details.
    Status merge_receivers(const Grid &source, const godot::Dictionary &placement, const Grid &destination,
            const Adapter &adapter, const godot::StringName &key, ReceiverCache &cache,
            godot::Dictionary &result, godot::Dictionary &refreshes);
    // Finds an allowed free position for the proposed item placement.
    Position find_position(const Grid &source, const Grid &destination, const godot::Dictionary &placement) const;
    // Commits stack edits with the expected source and target placements.
    Status commit_placed_merge(const Adapter &adapter, const godot::Dictionary &plan,
            const Grid &source, const godot::Dictionary &placement,
            const Grid &destination, const godot::Dictionary &target);
    // Checks that the source placement still matches the planned action.
    Status check_source(const Grid &source, const godot::Dictionary &placement, const Grid &destination = {}) const;
    // Checks that the stack key still matches the expected key.
    bool matches_stack(const Adapter &adapter, const godot::StringName &id, const godot::StringName &key) const;
    // Checks whether the host operation guard allows the source and destination.
    Status check_guard(const Grid &source, const Grid &destination) const;
    // Returns the current quantity represented by a placed item.
    int64_t placed_units(const Grid &source, const godot::StringName &id) const;
    // Finalizes transfer reporting with updated source and refresh information.
    godot::Dictionary end_transfer(godot::Dictionary result, const Grid &source, godot::Error error,
            const godot::String &reason, const godot::Dictionary &refreshes) const;
    // Checks the host policy for the proposed placement.
    godot::String placement_denial(const Grid &source, const godot::Dictionary &placement, const Grid &destination,
            const godot::Vector2i &origin, bool rotated, const godot::StringName &action, const godot::StringName &swap_id) const;
    // Checks whether the host allows the source item to be lifted.
    godot::String pickup_denial(const Grid &source, const godot::Dictionary &placement) const;
    // Checks whether the host allows the proposed destination.
    godot::String drop_denial(const godot::Dictionary &context) const;
};

} // namespace crimson::inventory
