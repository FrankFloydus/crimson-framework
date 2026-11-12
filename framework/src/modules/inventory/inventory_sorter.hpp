// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "spatial_inventory.h"

#include <godot_cpp/variant/callable.hpp>

#include <optional>

namespace crimson::inventory {

class SpatialInventorySorter : public godot::RefCounted {
    GDCLASS(SpatialInventorySorter, godot::RefCounted)

    struct Plan {
        // Inventory grid referenced by this record.
        godot::Ref<godot::SpatialInventory> inventory;
        // Inventory dimensions in cells.
        godot::Vector2i size;
        // Revision recorded to detect changes between preparation and commit.
        uint64_t revision;
        // Placements captured when the sort preview was built.
        godot::Dictionary baseline;
        // Staged before-and-after placement rows.
        godot::Array changes;
        // Host callback used to validate candidate placements.
        godot::Callable policy;
    };

    // Pending preview and the information needed to apply it.
    std::optional<Plan> plan;
    // Inverse placement rows retained after applying a sort.
    godot::Array undo_changes;
    // Guards state changes against reentrant callbacks.
    bool busy = false;

    // Builds a sort plan against the current inventory baseline.
    godot::Dictionary build_preview(const godot::Ref<godot::SpatialInventory> &inventory,
            const godot::Array &pinned_ids, bool allow_rotation, const godot::Callable &policy);
    // Commits a valid sort preview and records the inverse edits.
    godot::Error apply_plan();

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Builds a proposed arrangement without changing the inventory.
    godot::Dictionary preview(const godot::Ref<godot::SpatialInventory> &inventory,
            const godot::Array &pinned_ids = {}, bool allow_rotation = true, const godot::Callable &policy = {});
    // Returns whether an unapplied sort plan is available.
    bool has_preview() const;
    // Discards the pending sort plan.
    void cancel_preview();
    // Applies the pending sort plan if its baseline and policy still permit it.
    godot::Error apply_preview();
    // Returns whether a previous sort can be undone.
    bool can_undo() const;
    // Attempts to restore the placements recorded before the last sort.
    godot::Error undo(const godot::Callable &policy = {});
};

} // namespace crimson::inventory
