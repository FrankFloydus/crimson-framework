// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "spatial_inventory.h"

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <vector>

namespace crimson::inventory {

class SpatialInventoryEquipment : public godot::RefCounted {
    GDCLASS(SpatialInventoryEquipment, godot::RefCounted)

    // Sentinel cell requesting an automatic first-fit search.
    static const godot::Vector2i first_fit;
    // Upper bound for slots in one equipment configuration.
    static constexpr int max_slots = 64;
    // Version of the equipment snapshot layout.
    static constexpr int snapshot_version = 1;

    // Host callback that lists slots an item must occupy.
    godot::Callable slot_requirements;
    // Host callback that can deny an equipment move.
    godot::Callable move_policy;
    // Inventory from which items equip and to which they return.
    godot::Ref<godot::SpatialInventory> backpack;
    // Equipment slot IDs in the configured display order.
    godot::TypedArray<godot::StringName> slot_ids;
    // Position index of each configured equipment slot.
    godot::Dictionary slot_positions;
    // Equipment item ID currently occupying each slot.
    godot::Dictionary occupants;
    // Equipped item records indexed by item ID.
    godot::Dictionary items;
    // Revision recorded to detect changes between preparation and commit.
    int64_t revision = 0;
    // Guards state changes against reentrant callbacks.
    bool busy = false;

    // Plans an equipment action and optionally commits the validated result.
    godot::Dictionary run(const godot::StringName &action, const godot::StringName &id,
            const godot::StringName &slot, const godot::Vector2i &origin, const godot::Variant &rotated,
            bool commit, const godot::Dictionary &expected);
    // Plans the incoming equipment item and displaced items.
    godot::Dictionary plan_equip(godot::Dictionary &result) const;
    // Plans the backpack destination for a removed equipment item.
    void plan_unequip(godot::Dictionary &result, godot::Vector2i origin, const godot::Variant &rotated) const;
    // Publishes the validated backpack and equipment changes.
    void commit_plan(godot::Dictionary &result, const godot::Dictionary &incoming);
    // Clears an equipped item and all slots it occupies.
    void remove_equipped(const godot::StringName &id);
    // Validates and restores equipped records from a snapshot.
    godot::Error restore_items(const godot::Array &records);
    // Finds a backpack return cell for an equipment item.
    godot::Vector2i find_return(const godot::Dictionary &item, const godot::Dictionary &source,
            const std::vector<godot::Rect2i> &reserved) const;
    // Finds a legal backpack origin outside reserved rectangles.
    godot::Vector2i find_unreserved(const godot::Dictionary &item, const godot::Vector2i &extent,
            const std::vector<godot::Rect2i> &reserved) const;
    // Orders displaced equipment so return planning is deterministic.
    bool displacement_order(const godot::StringName &first, const godot::StringName &second) const;
    // Returns the validated requirements for equipping the item in the requested slot.
    godot::Dictionary get_requirements(const godot::StringName &id, const godot::StringName &slot) const;
    // Validates and normalizes the slots returned by the host.
    godot::TypedArray<godot::StringName> normalized_slots(const godot::Array &raw,
            const godot::StringName &requested) const;
    // Invokes the host's permission rule for an equipment move.
    godot::String check_move_policy(const godot::StringName &action, const godot::Dictionary &item,
            const godot::Dictionary &source, const godot::Dictionary &destination, bool displaced) const;
    // Initializes the data returned by an equipment action.
    godot::Dictionary new_result(const godot::StringName &action, const godot::StringName &id,
            const godot::StringName &slot) const;
    // Builds a before-and-after row for the grid change batch.
    godot::Dictionary placement_row(const godot::StringName &id, const godot::Dictionary &before,
            const godot::Dictionary &after) const;

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Sets the host rule that defines the required equipment slots.
    void set_slot_requirements(const godot::Callable &rules);
    // Returns the host rule that defines the required equipment slots.
    godot::Callable get_slot_requirements() const;
    // Sets the host rule for moving equipment.
    void set_move_policy(const godot::Callable &policy);
    // Returns the host rule for moving equipment.
    godot::Callable get_move_policy() const;
    // Returns the current committed model revision.
    int64_t get_revision() const;
    // Returns the inventory used by the equipment model.
    godot::Ref<godot::SpatialInventory> get_backpack() const;
    // Sets the backpack and configures the equipment slots.
    godot::Error configure(const godot::Ref<godot::SpatialInventory> &source, const godot::Array &slots);
    // Returns the configured equipment slots in display order.
    godot::TypedArray<godot::StringName> get_slot_ids() const;
    // Returns the item occupying the requested equipment slot.
    godot::StringName get_slot_item(const godot::StringName &slot) const;
    // Returns the equipped record for the requested item.
    godot::Dictionary get_equipped(const godot::StringName &id) const;
    // Returns all equipped records.
    godot::TypedArray<godot::Dictionary> get_equipped_items() const;
    // Plans equipping the selected item without applying changes.
    godot::Dictionary preview_equip(const godot::StringName &id, const godot::StringName &slot);
    // Equips an item and places any displaced items in the backpack.
    godot::Dictionary equip(const godot::StringName &id, const godot::StringName &slot,
            const godot::Dictionary &expected = {});
    // Plans a backpack return without removing the equipped item.
    godot::Dictionary preview_unequip(const godot::StringName &slot, const godot::Vector2i &origin = first_fit,
            const godot::Variant &rotated = {});
    // Returns an equipped item to an available backpack position.
    godot::Dictionary unequip(const godot::StringName &slot, const godot::Vector2i &origin = first_fit,
            const godot::Variant &rotated = {}, const godot::Dictionary &expected = {});
    // Returns a versioned snapshot of the model's committed state.
    godot::Dictionary export_snapshot() const;
    // Validates a snapshot before replacing the model's committed state.
    godot::Error import_snapshot(const godot::Dictionary &snapshot);
};

} // namespace crimson::inventory
