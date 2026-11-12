// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_sorter.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_set.hpp>

#include <algorithm>
#include <vector>

namespace crimson::inventory {

using namespace godot;

namespace {

constexpr Vector2i NO_FIT(-1, -1);

struct SortItem {
    // Stable identifier for the item instance.
    String id;
    // Number of cells in the item's unrotated footprint.
    int area;
    // Largest footprint dimension used to break sorting ties.
    int longest_side;
    // Item geometry and position recorded for this operation.
    Dictionary placement;
};

struct PackedLayout {
    // Godot error code for the operation.
    Error error;
    // Inventory grid referenced by this record.
    Ref<SpatialInventory> inventory;
    // Staged before-and-after placement rows.
    Array changes;
};

// Builds a result describing a rejected sort plan.
Dictionary error_result(Error error) {
    Dictionary result;
    result["error"] = error;
    return result;
}

// Indexes placements by item ID for baseline comparisons.
Dictionary index_placements(const Array &placements) {
    Dictionary indexed;
    for (int64_t index = 0; index < placements.size(); ++index) {
        const Dictionary placement = placements[index];
        indexed[placement["id"]] = placement;
    }
    return indexed;
}

// Checks whether the inventory still matches the preview baseline.
bool matches_baseline(const Ref<SpatialInventory> &inventory, Vector2i size, uint64_t revision, const Dictionary &baseline) {
    return inventory->get_placement_revision() == revision ||
            (inventory->get_grid_size() == size && index_placements(inventory->get_placements()) == baseline);
}

// Returns unpinned items in the sorter's deterministic packing order.
std::vector<SortItem> ordered_movable_items(const Dictionary &baseline, const HashSet<StringName> &pinned) {
    std::vector<SortItem> movable;
    movable.reserve(baseline.size() - pinned.size());
    const Array ids = baseline.keys();
    for (int64_t index = 0; index < ids.size(); ++index) {
        const StringName id = ids[index];
        if (pinned.has(id)) {
            continue;
        }
        const Dictionary placement = baseline[id];
        const Vector2i size = placement["footprint"];
        movable.push_back({String(id), size.x * size.y, std::max(size.x, size.y), placement});
    }
    std::sort(movable.begin(), movable.end(), [](const SortItem &left, const SortItem &right) {
        if (left.area != right.area) {
            return left.area > right.area;
        }
        if (left.longest_side != right.longest_side) {
            return left.longest_side > right.longest_side;
        }
        return left.id < right.id;
    });
    return movable;
}

// Checks host policy for a candidate sorted placement.
bool policy_allows(const Callable &policy, const Ref<SpatialInventory> &inventory, const StringName &id,
        Vector2i footprint, Vector2i origin, bool rotated) {
    Dictionary context;
    context["id"] = id;
    context["footprint"] = footprint;
    context["origin"] = origin;
    context["rotated"] = rotated;
    context["source"] = inventory;
    context["destination"] = inventory;
    context["swap_id"] = StringName();
    context["action"] = StringName("place");
    const Variant result = policy.call(context);
    return result.get_type() == Variant::STRING && String(result).is_empty();
}

// Checks host policy for each proposed placement change.
bool policy_allows_changes(const Callable &policy, const Array &changes) {
    if (!policy.is_valid()) {
        return true;
    }
    for (int64_t index = 0; index < changes.size(); ++index) {
        const Dictionary row = changes[index];
        const Dictionary after = row["after"];
        if (!policy_allows(policy, Ref<SpatialInventory>(row["inventory"]), after["id"], after["footprint"],
                    after["origin"], after["rotated"])) {
            return false;
        }
    }
    return true;
}

// Searches for a valid origin for one candidate orientation.
Vector2i find_origin(const Ref<SpatialInventory> &staged, const Ref<SpatialInventory> &inventory,
        const StringName &id, Vector2i footprint, bool rotated, const Callable &policy, Dictionary &first_fit_starts) {
    if (!policy.is_valid()) {
        const Vector2i extent = rotated ? Vector2i(footprint.y, footprint.x) : footprint;
        const Vector2i start = first_fit_starts.get(extent, Vector2i());
        if (start == NO_FIT) {
            return NO_FIT;
        }
        // Packing only adds blockers, so an earlier origin cannot become available.
        const Vector2i first = staged->find_first_fit(footprint, rotated, start);
        first_fit_starts[extent] = first;
        return first;
    }

    Vector2i candidate = staged->find_first_fit(footprint, rotated);
    while (candidate != NO_FIT) {
        if (policy_allows(policy, inventory, id, footprint, candidate, rotated)) {
            return candidate;
        }
        candidate = staged->find_first_fit(footprint, rotated, candidate + Vector2i(1, 0));
    }
    return NO_FIT;
}

// Compares cells in row-major order.
bool earlier_cell(Vector2i left, Vector2i right) {
    return left.y < right.y || (left.y == right.y && left.x < right.x);
}

// Chooses a free placement from the candidate orientations.
Dictionary choose_placement(const Ref<SpatialInventory> &staged, const Ref<SpatialInventory> &inventory,
        const Dictionary &placement, bool allow_rotation, const Callable &policy, Dictionary &first_fit_starts,
        bool prefer_current_orientation) {
    const StringName id = placement["id"];
    const Vector2i footprint = placement["footprint"];
    bool rotated = placement["rotated"];
    Vector2i origin = find_origin(staged, inventory, id, footprint, rotated, policy, first_fit_starts);
    if (allow_rotation && footprint.x != footprint.y && origin != Vector2i() &&
            (origin == NO_FIT || !prefer_current_orientation)) {
        const Vector2i alternate = find_origin(staged, inventory, id, footprint, !rotated, policy, first_fit_starts);
        if (alternate != NO_FIT && (origin == NO_FIT || earlier_cell(alternate, origin))) {
            origin = alternate;
            rotated = !rotated;
        }
    }
    if (origin == NO_FIT) {
        return {};
    }
    Dictionary proposed;
    proposed["id"] = id;
    proposed["footprint"] = footprint;
    proposed["origin"] = origin;
    proposed["rotated"] = rotated;
    return proposed;
}

// Builds a before-and-after placement edit.
Dictionary placement_change(const Ref<SpatialInventory> &inventory, const Variant &id,
        const Variant &before, const Variant &after) {
    Dictionary row;
    row["inventory"] = inventory;
    row["id"] = id;
    row["before"] = before;
    row["after"] = after;
    return row;
}

// Packs movable items around pinned placements in a detached layout.
PackedLayout pack_layout(const Ref<SpatialInventory> &inventory, Vector2i size, const Dictionary &baseline,
        const HashSet<StringName> &pinned, const std::vector<SortItem> &movable,
        bool allow_rotation, const Callable &policy, bool prefer_current_orientation) {
    Ref<SpatialInventory> staged;
    staged.instantiate();
    const Error configured = staged->configure(size);
    if (configured != OK) {
        return {configured, {}, {}};
    }
    for (const StringName &id : pinned) {
        const Dictionary placement = baseline[id];
        const Error placed = staged->try_place(id, placement["footprint"], placement["origin"], placement["rotated"]);
        if (placed != OK) {
            return {placed, {}, {}};
        }
    }

    Dictionary first_fit_starts;
    Array changes;
    for (const SortItem &item : movable) {
        const Dictionary &before = item.placement;
        const Dictionary after = choose_placement(staged, inventory, before, allow_rotation, policy,
                first_fit_starts, prefer_current_orientation);
        if (after.is_empty()) {
            return {ERR_CANT_CREATE, {}, {}};
        }
        const Error placed = staged->try_place(after["id"], after["footprint"], after["origin"], after["rotated"]);
        if (placed != OK) {
            return {placed, {}, {}};
        }
        if (before != after) {
            changes.push_back(placement_change(inventory, before["id"], before, after));
        }
    }
    return {OK, staged, changes};
}

} // namespace

Dictionary SpatialInventorySorter::preview(const Ref<SpatialInventory> &inventory, const Array &pinned_ids,
        bool allow_rotation, const Callable &policy) {
    if (busy) {
        return error_result(ERR_BUSY);
    }
    busy = true;
    plan.reset();
    Dictionary result = build_preview(inventory, pinned_ids, allow_rotation, policy);
    busy = false;
    return result;
}

Dictionary SpatialInventorySorter::build_preview(const Ref<SpatialInventory> &inventory, const Array &pinned_ids,
        bool allow_rotation, const Callable &policy) {
    if (inventory.is_null()) {
        return error_result(ERR_INVALID_PARAMETER);
    }
    const Vector2i size = inventory->get_grid_size();
    if (size == Vector2i()) {
        return error_result(ERR_UNCONFIGURED);
    }
    const uint64_t revision = inventory->get_placement_revision();
    const Dictionary baseline = index_placements(inventory->get_placements());
    HashSet<StringName> pinned;
    for (int64_t index = 0; index < pinned_ids.size(); ++index) {
        const Variant raw_id = pinned_ids[index];
        if (raw_id.get_type() != Variant::STRING && raw_id.get_type() != Variant::STRING_NAME) {
            return error_result(ERR_INVALID_PARAMETER);
        }
        const StringName id = raw_id;
        if (!baseline.has(id)) {
            return error_result(ERR_DOES_NOT_EXIST);
        }
        pinned.insert(id);
    }

    const auto movable = ordered_movable_items(baseline, pinned);
    PackedLayout packed = pack_layout(inventory, size, baseline, pinned, movable, allow_rotation, policy, false);
    if (packed.error == ERR_CANT_CREATE && allow_rotation) {
        packed = pack_layout(inventory, size, baseline, pinned, movable, true, policy, true);
    }
    if (packed.error != OK) {
        return error_result(packed.error);
    }
    if (!matches_baseline(inventory, size, revision, baseline)) {
        return error_result(ERR_BUSY);
    }

    plan = Plan{inventory, size, inventory->get_placement_revision(), baseline, packed.changes, policy};
    Dictionary result = error_result(OK);
    result["grid_size"] = size;
    result["placements"] = packed.inventory->get_placements();
    result["changed_count"] = packed.changes.size();
    return result;
}

bool SpatialInventorySorter::has_preview() const {
    return plan.has_value();
}

void SpatialInventorySorter::cancel_preview() {
    if (!busy) {
        plan.reset();
    }
}

Error SpatialInventorySorter::apply_preview() {
    if (busy) {
        return ERR_BUSY;
    }
    if (!plan) {
        return ERR_DOES_NOT_EXIST;
    }
    busy = true;
    const Error result = apply_plan();
    busy = false;
    return result;
}

Error SpatialInventorySorter::apply_plan() {
    const auto &inventory = plan->inventory;
    const uint64_t revision = inventory->get_placement_revision();
    if (!matches_baseline(inventory, plan->size, plan->revision, plan->baseline)) {
        return ERR_BUSY;
    }
    const Array &changes = plan->changes;
    if (!policy_allows_changes(plan->policy, changes)) {
        return ERR_UNAUTHORIZED;
    }
    if (!matches_baseline(inventory, plan->size, revision, plan->baseline)) {
        return ERR_BUSY;
    }

    const Error result = SpatialInventory::apply_placement_changes(changes);
    if (result != OK) {
        return result;
    }
    if (!changes.is_empty()) {
        undo_changes.clear();
        for (int64_t index = 0; index < changes.size(); ++index) {
            const Dictionary row = changes[index];
            undo_changes.push_back(placement_change(inventory, row["id"], row["after"], row["before"]));
        }
    }
    plan.reset();
    return OK;
}

bool SpatialInventorySorter::can_undo() const {
    return !undo_changes.is_empty();
}

Error SpatialInventorySorter::undo(const Callable &policy) {
    if (busy) {
        return ERR_BUSY;
    }
    if (undo_changes.is_empty()) {
        return ERR_DOES_NOT_EXIST;
    }
    busy = true;
    const Error result = policy_allows_changes(policy, undo_changes) ?
            SpatialInventory::apply_placement_changes(undo_changes) : ERR_UNAUTHORIZED;
    if (result == OK) {
        undo_changes.clear();
        plan.reset();
    }
    busy = false;
    return result;
}

void SpatialInventorySorter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("preview", "inventory", "pinned_ids", "allow_rotation", "placement_policy"),
            &SpatialInventorySorter::preview, DEFVAL(Array()), DEFVAL(true), DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("has_preview"), &SpatialInventorySorter::has_preview);
    ClassDB::bind_method(D_METHOD("cancel_preview"), &SpatialInventorySorter::cancel_preview);
    ClassDB::bind_method(D_METHOD("apply_preview"), &SpatialInventorySorter::apply_preview);
    ClassDB::bind_method(D_METHOD("can_undo"), &SpatialInventorySorter::can_undo);
    ClassDB::bind_method(D_METHOD("undo", "placement_policy"), &SpatialInventorySorter::undo, DEFVAL(Callable()));
}

} // namespace crimson::inventory
