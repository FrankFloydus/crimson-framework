// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "spatial_inventory.h"

#include <godot_cpp/variant/callable.hpp>

namespace crimson::inventory {

// Grid placement and host-owned stack records commit through one synchronous callback.
class SpatialInventoryStackAdapter : public godot::RefCounted {
    GDCLASS(SpatialInventoryStackAdapter, godot::RefCounted)

    // Host callback that retrieves stack metadata and quantity.
    godot::Callable describe_stack;
    // Host callback that commits prepared stack records.
    godot::Callable apply_changes;
    // Prevents reentrant calls while a host stack commit is active.
    bool committing = false;
    // Version of the host stack records used to detect stale plans.
    int64_t records_revision = 0;

    // Obtains and validates the current metadata for a stack.
    godot::Dictionary read_stack(const godot::StringName &id) const;
    // Rechecks the stack plan and applies it through the host commit callback.
    godot::Error apply_plan(const godot::Dictionary &expected, const godot::Dictionary &current,
            const godot::Callable &placement);

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Sets the host callback for stack metadata and quantities.
    void set_describe_stack(const godot::Callable &callback);
    // Returns the host callback for stack metadata and quantities.
    godot::Callable get_describe_stack() const;
    // Sets the host callback used to commit stack records.
    void set_apply_changes(const godot::Callable &callback);
    // Returns the host callback used to commit stack records.
    godot::Callable get_apply_changes() const;
    // Advances the revision after the host changes stack records externally.
    void invalidate_stack_records();
    // Returns the current version of the host stack records.
    int64_t get_stack_records_revision() const;

    // Builds expected and proposed stack quantities for a merge.
    godot::Dictionary plan_merge(const godot::StringName &source_id, const godot::StringName &target_id,
            int64_t limit = 0) const;
    // Builds expected records for splitting a quantity into a new ID.
    godot::Dictionary plan_split(const godot::StringName &source_id, const godot::StringName &new_id,
            int64_t amount) const;
    // Returns the valid quantity range for splitting this stack.
    godot::Dictionary get_split_limits(const godot::StringName &id) const;
    // Returns the compatibility key for the current stack.
    godot::StringName get_stack_key(const godot::StringName &id) const;
    // Returns the available capacity for the supplied stack key.
    int64_t get_merge_capacity(const godot::StringName &id, const godot::StringName &key) const;

    // Commits a merge with the optional paired placement change.
    godot::Error commit_merge(const godot::Dictionary &plan, const godot::Callable &commit_placement = {});
    // Commits a split and places the new stack in the destination.
    godot::Error commit_split(const godot::Dictionary &plan, const godot::Ref<godot::SpatialInventory> &destination,
            const godot::Vector2i &footprint, const godot::Vector2i &origin, bool rotated = false);
    // Commits a split whose new stack is handed to the host's cursor model.
    godot::Error commit_detached_split(const godot::Dictionary &plan, const godot::Callable &claim_cursor);
};

} // namespace crimson::inventory
