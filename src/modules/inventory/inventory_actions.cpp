// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_actions.hpp"
#include "inventory_callbacks.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace crimson::inventory {
namespace {

const Vector2i NO_FIT(-1, -1);
using Grid = Ref<SpatialInventory>;

class BusyScope {
    // Reentrancy guard shared by synchronous inventory actions.
    bool &busy;

public:
    // Marks the owner busy for the lifetime of this scope.
    explicit BusyScope(bool &flag) : busy(flag) { busy = true; }
    // Clears the owner's busy guard when the scope exits.
    ~BusyScope() { busy = false; }
};

// Builds a dictionary from named fields.
Dictionary fields(std::initializer_list<std::pair<Variant, Variant>> entries) {
    Dictionary dictionary;
    for (const auto &[key, value] : entries) {
        dictionary[key] = value;
    }
    return dictionary;
}

// Packages an action outcome for a source item.
Dictionary result_for(const StringName &id, Error error, const String &reason) {
    return fields({{"id", id}, {"error", error}, {"reason", reason}, {"moved", 0}, {"remaining", 0},
            {"completed", false}, {"selection", Dictionary()}, {"changes", Array()}});
}

// Reads the Godot error code carried by an action result.
Error read_error(const Dictionary &result) {
    return static_cast<Error>(static_cast<int64_t>(result["error"]));
}

// Converts a grid error to a placement explanation.
String placement_reason(Error error) {
    switch (error) {
        case ERR_PARAMETER_RANGE_ERROR: return "Item extends past the grid.";
        case ERR_ALREADY_IN_USE: return "The destination footprint is occupied.";
        case ERR_ALREADY_EXISTS: return "That instance already exists in this grid.";
        case ERR_BUSY: return "An affected placement changed before the action.";
        default: return String("Placement failed: ") + UtilityFunctions::error_string(error);
    }
}

// Builds a result row identifying an inventory and item.
Dictionary selection(const Grid &inventory, const StringName &id) {
    return fields({{"inventory", inventory}, {"id", id}});
}

// Records an item whose stack display needs refreshing.
void mark_refresh(Dictionary &refreshes, const Grid &inventory, const StringName &id) {
    if (!refreshes.has(inventory)) {
        refreshes[inventory] = Dictionary();
    }
    Dictionary ids = refreshes[inventory];
    ids[id] = true;
}

// Flattens tracked refreshes into rows for callers.
TypedArray<Dictionary> refresh_rows(const Dictionary &refreshes) {
    TypedArray<Dictionary> rows;
    const Array inventories = refreshes.keys();
    for (int64_t i = 0; i < inventories.size(); ++i) {
        const Dictionary ids = refreshes[inventories[i]];
        const Array ordered_ids = ids.keys();
        for (int64_t j = 0; j < ordered_ids.size(); ++j) {
            rows.append(selection(inventories[i], ordered_ids[j]));
        }
    }
    return rows;
}

// Builds the host policy context for a proposed item drop.
Dictionary drop_context(const Dictionary &placement, const Grid &source, const Grid &destination,
        const Vector2i &origin, bool rotated, const StringName &action, const StringName &swap_id = {}) {
    return fields({{"id", placement["id"]}, {"footprint", placement["footprint"]}, {"rotated", rotated},
            {"source", source}, {"destination", destination}, {"origin", origin}, {"swap_id", swap_id},
            {"action", action}});
}

// Computes the occupied rectangle from footprint and rotation.
Rect2i placement_rect(Vector2i footprint, const Vector2i &origin, bool rotated) {
    if (rotated) {
        std::swap(footprint.x, footprint.y);
    }
    return Rect2i(origin, footprint);
}

// Builds a before-and-after placement row for a grid commit.
Dictionary edit_row(const Grid &inventory, const StringName &id, const Dictionary &before,
        const Dictionary &after) {
    return fields({{"inventory", inventory}, {"id", id}, {"before", before}, {"after", after}});
}

} // namespace

void SpatialInventoryActions::_bind_methods() {
    ClassDB::bind_method(D_METHOD("quick_transfer", "source", "id", "destinations"),
            &SpatialInventoryActions::quick_transfer, DEFVAL(Array()));
    ClassDB::bind_method(D_METHOD("quick_transfer_matching", "source", "id", "destinations"),
            &SpatialInventoryActions::quick_transfer_matching, DEFVAL(Array()));
    ClassDB::bind_method(D_METHOD("split_stack", "source", "id", "amount", "new_id", "destination", "origin", "rotated"),
            &SpatialInventoryActions::split_stack, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("move_item", "source", "id", "destination", "origin", "rotated", "swap_id",
            "expected_source", "expected_swap"), &SpatialInventoryActions::move_item,
            DEFVAL(false), DEFVAL(StringName()), DEFVAL(Dictionary()), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("merge_stacks", "source", "id", "destination", "target_id", "limit"),
            &SpatialInventoryActions::merge_stacks, DEFVAL(0));
    ClassDB::bind_static_method("SpatialInventoryActions", D_METHOD("find_items", "inventory", "predicate"),
            &SpatialInventoryActions::find_items, DEFVAL(Callable()));

    ClassDB::bind_method(D_METHOD("set_stack_adapter", "adapter"), &SpatialInventoryActions::set_stack_adapter);
    ClassDB::bind_method(D_METHOD("get_stack_adapter"), &SpatialInventoryActions::get_stack_adapter);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stack_adapter", PROPERTY_HINT_RESOURCE_TYPE, "SpatialInventoryStackAdapter"),
            "set_stack_adapter", "get_stack_adapter");
    ClassDB::bind_method(D_METHOD("set_pickup_policy", "policy"), &SpatialInventoryActions::set_pickup_policy);
    ClassDB::bind_method(D_METHOD("get_pickup_policy"), &SpatialInventoryActions::get_pickup_policy);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "pickup_policy"), "set_pickup_policy", "get_pickup_policy");
    ClassDB::bind_method(D_METHOD("set_drop_policy", "policy"), &SpatialInventoryActions::set_drop_policy);
    ClassDB::bind_method(D_METHOD("get_drop_policy"), &SpatialInventoryActions::get_drop_policy);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "drop_policy"), "set_drop_policy", "get_drop_policy");
    ClassDB::bind_method(D_METHOD("set_destination_policy", "policy"), &SpatialInventoryActions::set_destination_policy);
    ClassDB::bind_method(D_METHOD("get_destination_policy"), &SpatialInventoryActions::get_destination_policy);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "destination_policy"), "set_destination_policy", "get_destination_policy");
    ClassDB::bind_method(D_METHOD("set_destination_provider", "provider"), &SpatialInventoryActions::set_destination_provider);
    ClassDB::bind_method(D_METHOD("get_destination_provider"), &SpatialInventoryActions::get_destination_provider);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "destination_provider"), "set_destination_provider", "get_destination_provider");
    ClassDB::bind_method(D_METHOD("set_operation_guard", "guard"), &SpatialInventoryActions::set_operation_guard);
    ClassDB::bind_method(D_METHOD("get_operation_guard"), &SpatialInventoryActions::get_operation_guard);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "operation_guard"), "set_operation_guard", "get_operation_guard");
    ClassDB::bind_method(D_METHOD("set_allow_rotation", "enabled"), &SpatialInventoryActions::set_allow_rotation);
    ClassDB::bind_method(D_METHOD("get_allow_rotation"), &SpatialInventoryActions::get_allow_rotation);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_rotation"), "set_allow_rotation", "get_allow_rotation");
}

void SpatialInventoryActions::set_stack_adapter(const Adapter &adapter) { stack_adapter = adapter; }
Ref<SpatialInventoryStackAdapter> SpatialInventoryActions::get_stack_adapter() const { return stack_adapter; }
void SpatialInventoryActions::set_pickup_policy(const Callable &policy) { pickup_policy = policy; }
Callable SpatialInventoryActions::get_pickup_policy() const { return pickup_policy; }
void SpatialInventoryActions::set_drop_policy(const Callable &policy) { drop_policy = policy; }
Callable SpatialInventoryActions::get_drop_policy() const { return drop_policy; }
void SpatialInventoryActions::set_destination_policy(const Callable &policy) { destination_policy = policy; }
Callable SpatialInventoryActions::get_destination_policy() const { return destination_policy; }
void SpatialInventoryActions::set_destination_provider(const Callable &provider) { destination_provider = provider; }
Callable SpatialInventoryActions::get_destination_provider() const { return destination_provider; }
void SpatialInventoryActions::set_operation_guard(const Callable &guard) { operation_guard = guard; }
Callable SpatialInventoryActions::get_operation_guard() const { return operation_guard; }
void SpatialInventoryActions::set_allow_rotation(bool enabled) { allow_rotation = enabled; }
bool SpatialInventoryActions::get_allow_rotation() const { return allow_rotation; }

Dictionary SpatialInventoryActions::quick_transfer(const Grid &source, const StringName &id,
        const Array &destinations) {
    if (busy) {
        return result_for(id, ERR_BUSY, "An inventory action is already running.");
    }
    if (source.is_null() || id.is_empty()) {
        return result_for(id, ERR_INVALID_PARAMETER, "A source inventory and instance ID are required.");
    }
    const Dictionary placement = source->get_placement(id);
    if (placement.is_empty()) {
        return result_for(id, ERR_DOES_NOT_EXIST, "The source item does not exist.");
    }

    const BusyScope scope(busy);
    const Adapter adapter = stack_adapter;
    const StringName key = adapter.is_valid() ? adapter->get_stack_key(id) : StringName();
    ReceiverCache cache;
    return transfer_one(source, placement, destinations, adapter, key, cache);
}

Dictionary SpatialInventoryActions::quick_transfer_matching(const Grid &source, const StringName &id,
        const Array &destinations) {
    const auto batch_result = [](Error error, const String &reason) {
        return fields({{"error", error}, {"reason", reason}, {"moved", 0}, {"remaining", 0}, {"completed", 0},
                {"transfers", Array()}, {"selection", Dictionary()}, {"changes", Array()}});
    };
    if (busy) {
        return batch_result(ERR_BUSY, "An inventory action is already running.");
    }
    if (source.is_null() || id.is_empty()) {
        return batch_result(ERR_INVALID_PARAMETER, "A source inventory and instance ID are required.");
    }
    const Dictionary selected = source->get_placement(id);
    if (selected.is_empty()) {
        return batch_result(ERR_DOES_NOT_EXIST, "The source item does not exist.");
    }

    const BusyScope scope(busy);
    const Adapter adapter = stack_adapter;
    const StringName key = adapter.is_valid() ? adapter->get_stack_key(id) : StringName();
    if (key.is_empty()) {
        return batch_result(ERR_UNAVAILABLE, "The selected item has no stack description.");
    }
    Array candidates = Array::make(selected);
    const Array placements = source->get_placements();
    for (int64_t i = 0; i < placements.size(); ++i) {
        const Dictionary placement = placements[i];
        const StringName candidate = placement["id"];
        if (candidate != id && adapter->get_stack_key(candidate) == key) {
            candidates.append(placement);
        }
    }

    ReceiverCache cache;
    Dictionary refreshes;
    Array transfers;
    Dictionary chosen;
    Status status;
    int64_t moved = 0;
    int64_t remaining = 0;
    int64_t completed = 0;
    int64_t affected = 0;
    for (int64_t i = 0; i < candidates.size(); ++i) {
        const Dictionary transfer = transfer_one(source, candidates[i], destinations, adapter, key, cache);
        transfers.append(transfer);
        const int64_t transferred = transfer["moved"];
        moved += transferred;
        remaining += static_cast<int64_t>(transfer["remaining"]);
        status = {read_error(transfer), transfer["reason"]};
        if (transferred > 0) {
            ++affected;
            completed += static_cast<bool>(transfer["completed"]) ? 1 : 0;
            chosen = transfer["selection"];
        }
        const Array changes = transfer["changes"];
        for (int64_t j = 0; j < changes.size(); ++j) {
            const Dictionary row = changes[j];
            mark_refresh(refreshes, row["inventory"], row["id"]);
        }
    }
    if (affected > 0) {
        status = {OK, String("Transferred from {0} matching stacks. {1} stacks stayed in the source.")
                              .format(Array::make(affected, candidates.size() - completed))};
    }
    return fields({{"error", status.error}, {"reason", status.reason}, {"moved", moved}, {"remaining", remaining},
            {"completed", completed}, {"transfers", transfers}, {"selection", chosen}, {"changes", refresh_rows(refreshes)}});
}

Dictionary SpatialInventoryActions::transfer_one(const Grid &source, const Dictionary &placement,
        const Array &destinations, const Adapter &adapter, const StringName &key, ReceiverCache &cache) {
    const StringName id = placement["id"];
    Dictionary result = result_for(id, OK, "");
    Dictionary refreshes;
    if (!matches_source(source, placement) || !matches_stack(adapter, id, key)) {
        return end_transfer(result, source, ERR_BUSY, "The source item changed before transfer.", refreshes);
    }
    String denial = pickup_denial(source, placement);
    if (!denial.is_empty()) {
        return end_transfer(result, source, ERR_UNAUTHORIZED, denial, refreshes);
    }

    std::vector<Grid> models;
    const Status routing = transfer_destinations(source, placement, destinations, models);
    if (routing.error != OK) {
        return end_transfer(result, source, routing.error, routing.reason, refreshes);
    }
    denial = routing.reason;
    if (!matches_source(source, placement)) {
        return end_transfer(result, source, ERR_BUSY, "The source item changed before transfer.", refreshes);
    }

    if (adapter.is_valid() && adapter->get_apply_changes().is_valid() && !key.is_empty()) {
        for (const Grid &destination : models) {
            if (!destination->get_placement(id).is_empty()) {
                continue;
            }
            const Status merged = merge_receivers(source, placement, destination, adapter, key, cache, result, refreshes);
            if (!merged.reason.is_empty()) {
                denial = merged.reason;
            }
            if (static_cast<bool>(result["completed"]) || merged.error != OK) {
                return end_transfer(result, source, merged.error, denial, refreshes);
            }
        }
    }

    for (const Grid &destination : models) {
        if (!destination->get_placement(id).is_empty()) {
            continue;
        }
        const Position position = find_position(source, destination, placement);
        if (position.origin == NO_FIT) {
            if (!position.denial.is_empty()) {
                denial = position.denial;
            }
            continue;
        }
        if (!matches_source(source, placement, destination) || !matches_stack(adapter, id, key)) {
            return end_transfer(result, source, ERR_BUSY, "The source or destination changed before transfer.", refreshes);
        }
        const int64_t amount = placed_units(source, id);
        const uint64_t before_revision = destination->get_placement_revision();
        const Error error = source->try_transfer(id, destination, position.origin, position.rotated);
        if (error != OK) {
            return end_transfer(result, source, error, placement_reason(error), refreshes);
        }
        auto cached = cache.find(destination);
        if (cached != cache.end() && cached->second.placement_revision == before_revision &&
                destination->get_placement_revision() == before_revision + 1) {
            Receivers &receivers = cached->second;
            receivers.ids.append(id);
            receivers.placement_revision = before_revision + 1;
            if (adapter.is_valid() && receivers.empty_capacity_revision == adapter->get_stack_records_revision() &&
                    adapter->get_merge_capacity(id, key) > 0) {
                receivers.empty_capacity_revision = -1;
            }
        }
        result["moved"] = static_cast<int64_t>(result["moved"]) + amount;
        result["completed"] = true;
        result["selection"] = selection(destination, id);
        return end_transfer(result, source, OK, "Item transferred.", refreshes);
    }

    if (!matches_source(source, placement)) {
        return end_transfer(result, source, ERR_BUSY, "The source item changed before transfer.", refreshes);
    }
    if (!denial.is_empty()) {
        return end_transfer(result, source, ERR_UNAUTHORIZED, denial, refreshes);
    }
    const String reason = static_cast<int64_t>(result["moved"]) > 0 ? "The rest stayed in the source." :
            "No destination has room for this item.";
    return end_transfer(result, source, ERR_CANT_CREATE, reason, refreshes);
}

SpatialInventoryActions::Status SpatialInventoryActions::transfer_destinations(const Grid &source,
        const Dictionary &placement, const Array &destinations, std::vector<Grid> &models) const {
    Array ordered = destinations;
    if (destinations.is_empty() && destination_provider.is_valid()) {
        const Variant supplied = destination_provider.call(drop_context(placement, source, {},
                placement["origin"], placement["rotated"], "transfer"));
        if (supplied.get_type() != Variant::ARRAY) {
            return {ERR_INVALID_PARAMETER, "Quick transfer targets must return an Array."};
        }
        ordered = supplied;
    }

    Dictionary seen;
    seen[source] = true;
    String denial;
    for (int64_t i = 0; i < ordered.size(); ++i) {
        const Grid destination = ordered[i];
        if (destination.is_null() || seen.has(destination)) {
            continue;
        }
        seen[destination] = true;
        if (!guard_allows(source, destination)) {
            continue;
        }
        if (destination_policy.is_valid()) {
            const Variant decision = destination_policy.call(drop_context(placement, source, destination,
                    placement["origin"], placement["rotated"], "transfer"));
            if (decision.get_type() != Variant::STRING) {
                denial = "Destination policy must return a String.";
                continue;
            }
            const String reason = decision;
            if (!reason.is_empty()) {
                denial = reason;
                continue;
            }
        }
        models.push_back(destination);
    }
    return {OK, denial};
}

SpatialInventoryActions::Status SpatialInventoryActions::merge_receivers(const Grid &source,
        const Dictionary &placement, const Grid &destination, const Adapter &adapter,
        const StringName &key, ReceiverCache &cache, Dictionary &result, Dictionary &refreshes) {
    const uint64_t placement_revision = destination->get_placement_revision();
    auto cached = cache.find(destination);
    if (cached == cache.end() || cached->second.placement_revision != placement_revision) {
        Receivers entry{placement_revision, -1, {}};
        const Array placements = destination->get_placements();
        for (int64_t i = 0; i < placements.size(); ++i) {
            const Dictionary target = placements[i];
            entry.ids.append(target["id"]);
        }
        cached = cache.insert_or_assign(destination, std::move(entry)).first;
    }
    Receivers &receivers = cached->second;
    const int64_t records_revision = adapter->get_stack_records_revision();
    if (receivers.empty_capacity_revision == records_revision) {
        return {};
    }

    const StringName id = placement["id"];
    bool has_capacity = false;
    String denial;
    for (int64_t i = 0; i < receivers.ids.size(); ++i) {
        const StringName target_id = receivers.ids[i];
        if (adapter->get_merge_capacity(target_id, key) == 0) {
            continue;
        }
        has_capacity = true;
        const Dictionary target = destination->get_placement(target_id);
        if (target.is_empty()) {
            continue;
        }
        const Dictionary plan = adapter->plan_merge(id, target_id);
        if (read_error(plan) != OK) {
            return {ERR_BUSY, "The source or destination stack changed before transfer."};
        }
        const Array changes = plan["changes"];
        const Dictionary first_change = changes[0];
        const Dictionary before = first_change["before"];
        if (StringName(before["key"]) != key) {
            return {ERR_BUSY, "The source or destination stack changed before transfer."};
        }
        if (drop_policy.is_valid()) {
            Dictionary context = drop_context(placement, source, destination, target["origin"],
                    placement["rotated"], "merge");
            context["target_id"] = target_id;
            context["quantity"] = plan["moved"];
            const String reason = drop_denial(context);
            if (!reason.is_empty()) {
                denial = reason;
                continue;
            }
        }

        const bool consumed = static_cast<int64_t>(plan["remaining"]) == 0;
        const Error error = commit_placed_merge(adapter, plan, source, placement, destination, target);
        if (error != OK) {
            return {error, String("Could not transfer into the matching stack (") +
                    UtilityFunctions::error_string(error) + ")."};
        }
        result["moved"] = static_cast<int64_t>(result["moved"]) + static_cast<int64_t>(plan["moved"]);
        mark_refresh(refreshes, destination, target_id);
        if (consumed) {
            result["completed"] = true;
            result["selection"] = selection(destination, target_id);
            return {OK, "Stack transferred into matching stacks."};
        }
        mark_refresh(refreshes, source, id);
    }
    if (!has_capacity) {
        receivers.empty_capacity_revision = records_revision;
    }
    return {OK, denial};
}

SpatialInventoryActions::Position SpatialInventoryActions::find_position(const Grid &source,
        const Grid &destination, const Dictionary &placement) const {
    const Vector2i footprint = placement["footprint"];
    const bool initial_rotation = placement["rotated"];
    const int orientation_count = allow_rotation && (footprint.x != footprint.y || drop_policy.is_valid()) ? 2 : 1;
    String denial;
    for (int orientation = 0; orientation < orientation_count; ++orientation) {
        const bool rotated = orientation == 0 ? initial_rotation : !initial_rotation;
        Vector2i origin = destination->find_first_fit(footprint, rotated);
        while (origin != NO_FIT) {
            denial = drop_denial(drop_context(placement, source, destination, origin, rotated, "place"));
            if (denial.is_empty()) {
                return {origin, rotated, {}};
            }
            origin = destination->find_first_fit(footprint, rotated, origin + Vector2i(1, 0));
        }
    }
    return {NO_FIT, false, denial};
}

Dictionary SpatialInventoryActions::split_stack(const Grid &source, const StringName &id, int64_t amount,
        const StringName &new_id, const Grid &destination, const Vector2i &origin, bool rotated) {
    if (busy) {
        return result_for(id, ERR_BUSY, "An inventory action is already running.");
    }
    if (source.is_null() || destination.is_null() || id.is_empty()) {
        return result_for(id, ERR_INVALID_PARAMETER, "Source, destination and instance ID are required.");
    }
    const Dictionary placement = source->get_placement(id);
    if (placement.is_empty()) {
        return result_for(id, ERR_DOES_NOT_EXIST, "The source item does not exist.");
    }
    const Adapter adapter = stack_adapter;
    if (adapter.is_null()) {
        return result_for(id, ERR_UNAVAILABLE, "A stack adapter is required.");
    }

    const BusyScope scope(busy);
    String denial = pickup_denial(source, placement);
    if (!denial.is_empty()) {
        return result_for(id, ERR_UNAUTHORIZED, denial);
    }
    const Vector2i footprint = placement["footprint"];
    const Error placement_error = destination->check_placement(footprint, origin, rotated);
    if (placement_error != OK) {
        return result_for(id, placement_error, placement_reason(placement_error));
    }
    if (!source->get_placement(new_id).is_empty() || !destination->get_placement(new_id).is_empty()) {
        return result_for(id, ERR_ALREADY_EXISTS, placement_reason(ERR_ALREADY_EXISTS));
    }
    const Dictionary plan = adapter->plan_split(id, new_id, amount);
    const Error plan_error = read_error(plan);
    if (plan_error != OK) {
        return result_for(id, plan_error, String("Could not split the stack: ") + UtilityFunctions::error_string(plan_error));
    }

    if (drop_policy.is_valid()) {
        Dictionary context = drop_context(placement, source, destination, origin, rotated, "split");
        context["new_id"] = new_id;
        context["quantity"] = amount;
        denial = drop_denial(context);
        if (!denial.is_empty()) {
            return result_for(id, ERR_UNAUTHORIZED, denial);
        }
    }
    if (stack_adapter != adapter || !matches_source(source, placement, destination)) {
        return result_for(id, ERR_BUSY, "The source or destination changed before splitting.");
    }
    const Callable commit_placement = inventory_callback([&]() -> Error {
        if (stack_adapter != adapter || !matches_source(source, placement, destination)) {
            return ERR_BUSY;
        }
        if (!source->get_placement(new_id).is_empty()) {
            return ERR_ALREADY_EXISTS;
        }
        return destination->try_place(new_id, footprint, origin, rotated);
    });
    const Error error = adapter->commit_detached_split(plan, commit_placement);
    if (error != OK) {
        return result_for(id, error, String("Could not split the stack: ") + UtilityFunctions::error_string(error));
    }

    Dictionary result = result_for(id, OK, String("Split off ") + String::num_int64(amount) + ".");
    result["moved"] = amount;
    result["remaining"] = placed_units(source, id);
    result["completed"] = true;
    result["selection"] = selection(destination, new_id);
    result["changes"] = Array::make(selection(source, id));
    return result;
}

Dictionary SpatialInventoryActions::move_item(const Grid &source, const StringName &id, const Grid &destination,
        const Vector2i &origin, bool rotated, const StringName &swap_id, const Dictionary &expected_source,
        const Dictionary &expected_swap) {
    if (busy) {
        return result_for(id, ERR_BUSY, "An inventory action is already running.");
    }
    if (source.is_null() || destination.is_null() || id.is_empty() || id == swap_id ||
            (swap_id.is_empty() && !expected_swap.is_empty())) {
        return result_for(id, ERR_INVALID_PARAMETER, "Invalid move or swap request.");
    }
    const Dictionary before = source->get_placement(id);
    if (!expected_source.is_empty() && before != expected_source) {
        return result_for(id, ERR_BUSY, placement_reason(ERR_BUSY));
    }
    if (before.is_empty()) {
        return result_for(id, ERR_DOES_NOT_EXIST, "The source item does not exist.");
    }

    const BusyScope scope(busy);
    Error error = source->check_transfer(id, destination, origin, rotated);
    const StringName action = swap_id.is_empty() ? StringName("place") : StringName("swap");
    Dictionary blocker;
    if (!swap_id.is_empty()) {
        if (error == OK) {
            return result_for(id, ERR_BUSY, "The expected swap target no longer overlaps the destination.");
        }
        if (error == ERR_ALREADY_IN_USE) {
            blocker = destination->get_placement(swap_id);
            if (blocker.is_empty() || (!expected_swap.is_empty() && blocker != expected_swap) ||
                    !placement_rect(before["footprint"], origin, rotated).intersects(
                            placement_rect(blocker["footprint"], blocker["origin"], blocker["rotated"]))) {
                return result_for(id, ERR_BUSY, "The expected swap target changed.");
            }
            error = OK;
        }
    }
    if (error != OK) {
        return result_for(id, error, placement_reason(error));
    }

    String denial = placement_denial(source, before, destination, origin, rotated, action, swap_id);
    if (denial.is_empty() && !blocker.is_empty()) {
        denial = placement_denial(destination, blocker, source, before["origin"], blocker["rotated"], action, id);
    }
    if (!denial.is_empty()) {
        return result_for(id, ERR_UNAUTHORIZED, denial);
    }
    const int64_t units = placed_units(source, id);
    if (!matches_source(source, before, destination)) {
        return result_for(id, ERR_BUSY, placement_reason(ERR_BUSY));
    }

    if (blocker.is_empty()) {
        error = source->try_transfer(id, destination, origin, rotated);
    } else if (matches_source(destination, blocker, source)) {
        Dictionary after = before.duplicate();
        after["origin"] = origin;
        after["rotated"] = rotated;
        Dictionary returned = blocker.duplicate();
        returned["origin"] = before["origin"];
        if (source == destination) {
            error = SpatialInventory::apply_placement_changes(Array::make(
                    edit_row(source, id, before, after), edit_row(source, swap_id, blocker, returned)));
        } else {
            error = SpatialInventory::apply_placement_changes(Array::make(
                    edit_row(source, id, before, {}), edit_row(destination, id, {}, after),
                    edit_row(destination, swap_id, blocker, {}), edit_row(source, swap_id, {}, returned)));
        }
    } else {
        error = ERR_BUSY;
    }
    if (error != OK) {
        return result_for(id, error, placement_reason(error));
    }

    Dictionary result = result_for(id, OK, blocker.is_empty() ? "Item moved." : "Items swapped.");
    result["moved"] = units;
    result["completed"] = true;
    result["selection"] = selection(destination, id);
    return result;
}

Dictionary SpatialInventoryActions::merge_stacks(const Grid &source, const StringName &id,
        const Grid &destination, const StringName &target_id, int64_t limit) {
    if (busy) {
        return result_for(id, ERR_BUSY, "An inventory action is already running.");
    }
    if (source.is_null() || destination.is_null() || id.is_empty() || target_id.is_empty() || id == target_id) {
        return result_for(id, ERR_INVALID_PARAMETER, "Invalid stack merge request.");
    }
    const Dictionary placement = source->get_placement(id);
    const Dictionary target = destination->get_placement(target_id);
    if (placement.is_empty() || target.is_empty()) {
        return result_for(id, ERR_DOES_NOT_EXIST, "Both stack placements are required.");
    }
    const Adapter adapter = stack_adapter;
    if (adapter.is_null()) {
        return result_for(id, ERR_UNAVAILABLE, "A stack adapter is required.");
    }

    const BusyScope scope(busy);
    String denial = pickup_denial(source, placement);
    if (!denial.is_empty()) {
        return result_for(id, ERR_UNAUTHORIZED, denial);
    }
    const Dictionary plan = adapter->plan_merge(id, target_id, limit);
    const Error plan_error = read_error(plan);
    if (plan_error != OK) {
        return result_for(id, plan_error, String("Could not merge the stacks: ") + UtilityFunctions::error_string(plan_error) + ".");
    }
    if (drop_policy.is_valid()) {
        Dictionary context = drop_context(placement, source, destination, target["origin"], placement["rotated"], "merge");
        context["target_id"] = target_id;
        context["quantity"] = plan["moved"];
        denial = drop_denial(context);
        if (!denial.is_empty()) {
            return result_for(id, ERR_UNAUTHORIZED, denial);
        }
    }
    const Error error = commit_placed_merge(adapter, plan, source, placement, destination, target);
    if (error != OK) {
        return result_for(id, error, String("Could not merge the stacks: ") + UtilityFunctions::error_string(error) + ".");
    }
    const int64_t moved = plan["moved"];
    const bool completed = static_cast<int64_t>(plan["remaining"]) == 0;
    Dictionary result = result_for(id, OK, String("Merged ") + String::num_int64(moved) + ".");
    result["moved"] = moved;
    result["remaining"] = plan["remaining"];
    result["completed"] = completed;
    result["selection"] = selection(destination, target_id);
    Array changes = Array::make(selection(destination, target_id));
    if (!completed) {
        changes.append(selection(source, id));
    }
    result["changes"] = changes;
    return result;
}

Error SpatialInventoryActions::commit_placed_merge(const Adapter &adapter, const Dictionary &plan,
        const Grid &source, const Dictionary &placement, const Grid &destination, const Dictionary &target) {
    const StringName id = placement["id"];
    const StringName target_id = target["id"];
    const bool consumed = static_cast<int64_t>(plan["remaining"]) == 0;
    const Callable commit_placement = inventory_callback([&]() -> Error {
        if (stack_adapter != adapter || !matches_source(source, placement, destination) ||
                destination->get_placement(target_id) != target ||
                (source != destination && (!destination->get_placement(id).is_empty() ||
                        !source->get_placement(target_id).is_empty()))) {
            return ERR_BUSY;
        }
        return consumed ? source->remove(id) : OK;
    });
    return adapter->commit_merge(plan, commit_placement);
}

TypedArray<StringName> SpatialInventoryActions::find_items(const Grid &inventory, const Callable &predicate) {
    TypedArray<StringName> ids;
    if (inventory.is_null()) {
        return ids;
    }
    const Array placements = inventory->get_placements();
    std::vector<StringName> matched_ids;
    for (int64_t i = 0; i < placements.size(); ++i) {
        const Dictionary placement = placements[i];
        if (predicate.is_valid()) {
            const Variant matched = predicate.call(placement);
            if (matched.get_type() != Variant::BOOL || !static_cast<bool>(matched)) {
                continue;
            }
        }
        matched_ids.push_back(placement["id"]);
    }
    std::sort(matched_ids.begin(), matched_ids.end(), [](const StringName &left, const StringName &right) {
        return String(left) < String(right);
    });
    for (const StringName &id : matched_ids) {
        ids.append(id);
    }
    return ids;
}

bool SpatialInventoryActions::matches_source(const Grid &source, const Dictionary &placement,
        const Grid &destination) const {
    return guard_allows(source, destination) && source->get_placement(placement["id"]) == placement;
}

bool SpatialInventoryActions::guard_allows(const Grid &source, const Grid &destination) const {
    if (!operation_guard.is_valid()) {
        return true;
    }
    const Variant allowed = operation_guard.call(source, destination);
    return allowed.get_type() == Variant::BOOL && static_cast<bool>(allowed);
}

bool SpatialInventoryActions::matches_stack(const Adapter &adapter, const StringName &id, const StringName &key) const {
    return key.is_empty() || (stack_adapter == adapter && adapter->get_stack_key(id) == key);
}

int64_t SpatialInventoryActions::placed_units(const Grid &source, const StringName &id) const {
    if (source->get_placement(id).is_empty()) {
        return 0;
    }
    const Adapter adapter = stack_adapter;
    if (adapter.is_valid()) {
        const Dictionary limits = adapter->get_split_limits(id);
        if (read_error(limits) == OK) {
            return limits["quantity"];
        }
    }
    return 1;
}

Dictionary SpatialInventoryActions::end_transfer(Dictionary result, const Grid &source, Error error,
        const String &reason, const Dictionary &refreshes) const {
    const StringName id = result["id"];
    const int64_t remaining = placed_units(source, id);
    result["remaining"] = remaining;
    result["changes"] = refresh_rows(refreshes);
    const int64_t moved = result["moved"];
    if (moved > 0 && !static_cast<bool>(result["completed"])) {
        result["error"] = OK;
        result["reason"] = String("Transferred ") + String::num_int64(moved) + ". " + reason;
        if (remaining > 0) {
            result["selection"] = selection(source, id);
        }
    } else {
        result["error"] = error;
        result["reason"] = reason;
    }
    return result;
}

String SpatialInventoryActions::placement_denial(const Grid &source, const Dictionary &placement,
        const Grid &destination, const Vector2i &origin, bool rotated, const StringName &action,
        const StringName &swap_id) const {
    const String denial = pickup_denial(source, placement);
    if (!denial.is_empty() || !drop_policy.is_valid()) {
        return denial;
    }
    return drop_denial(drop_context(placement, source, destination, origin, rotated, action, swap_id));
}

String SpatialInventoryActions::pickup_denial(const Grid &source, const Dictionary &placement) const {
    if (!pickup_policy.is_valid()) {
        return {};
    }
    const Variant decision = pickup_policy.call(fields({{"id", placement["id"]}, {"footprint", placement["footprint"]},
            {"rotated", placement["rotated"]}, {"source", source}, {"origin", placement["origin"]}}));
    return decision.get_type() == Variant::STRING ? String(decision) : String("Pickup policy must return a String.");
}

String SpatialInventoryActions::drop_denial(const Dictionary &context) const {
    if (!drop_policy.is_valid()) {
        return {};
    }
    const Variant decision = drop_policy.call(context);
    return decision.get_type() == Variant::STRING ? String(decision) : String("Drop policy must return a String.");
}

} // namespace crimson::inventory
