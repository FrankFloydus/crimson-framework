// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_stack_adapter.hpp"

#include "inventory_callbacks.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <algorithm>
#include <limits>
#include <memory>

using namespace godot;

namespace crimson::inventory {
namespace {

// Builds a stack adapter result with the supplied error.
Dictionary status(Error error) {
    Dictionary result;
    result["error"] = error;
    return result;
}

// Converts a host callback result to a Godot error code.
Error error_from(const Variant &result) {
    if (result.get_type() != Variant::INT) {
        return ERR_INVALID_DATA;
    }
    const int64_t value = result;
    if (value < OK || value > ERR_PRINTER_ON_FIRE) {
        return ERR_INVALID_DATA;
    }
    return static_cast<Error>(value);
}

// Checks the shape and current revision of a stack operation plan.
bool valid_plan(const Dictionary &plan) {
    const Variant moved = plan.get("moved", Variant());
    return error_from(plan.get("error", Variant())) == OK &&
            plan.get("source_id", Variant()).get_type() == Variant::STRING_NAME &&
            plan.get("target_id", Variant()).get_type() == Variant::STRING_NAME &&
            moved.get_type() == Variant::INT && static_cast<int64_t>(moved) > 0;
}

// Copies stack metadata with a replacement quantity.
Dictionary with_quantity(const Dictionary &stack, int64_t quantity) {
    if (quantity == 0) {
        return {};
    }
    Dictionary result = stack.duplicate();
    result["quantity"] = quantity;
    return result;
}

// Builds a before-and-after row for host stack records.
Dictionary change(const StringName &id, const Dictionary &before, const Dictionary &after) {
    Dictionary row;
    row["id"] = id;
    row["before"] = before;
    row["after"] = after;
    return row;
}

// Builds a staged stack edit and its expected records.
Dictionary stack_plan(const StringName &source, const StringName &target, int64_t moved, int64_t remaining,
        const Dictionary &source_change, const Dictionary &target_change) {
    TypedArray<Dictionary> changes;
    changes.push_back(source_change);
    changes.push_back(target_change);
    Dictionary plan = status(OK);
    plan["source_id"] = source;
    plan["target_id"] = target;
    plan["moved"] = moved;
    plan["remaining"] = remaining;
    plan["changes"] = changes;
    return plan;
}

struct CommitGuard {
    // Marks the stack adapter busy during a commit.
    explicit CommitGuard(bool &flag) : committing(flag) { committing = true; }
    // Clears the stack adapter's busy guard.
    ~CommitGuard() { committing = false; }
    // Prevents reentrant calls while a host stack commit is active.
    bool &committing;
};

struct PlacementCommit {
    // Callback that applies the paired placement change.
    Callable placement;
    // Whether the one-shot placement callback has been invoked.
    bool called = false;
    // Result returned by the one-shot placement callback.
    Error result = ERR_INVALID_DATA;

    // Runs the paired grid commit once and stores its outcome.
    Error run() {
        if (called || !placement.is_valid()) {
            return ERR_BUSY;
        }
        called = true;
        result = error_from(placement.call());
        return result;
    }
};

} // namespace

void SpatialInventoryStackAdapter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_describe_stack", "callback"), &SpatialInventoryStackAdapter::set_describe_stack);
    ClassDB::bind_method(D_METHOD("get_describe_stack"), &SpatialInventoryStackAdapter::get_describe_stack);
    ClassDB::bind_method(D_METHOD("set_apply_changes", "callback"), &SpatialInventoryStackAdapter::set_apply_changes);
    ClassDB::bind_method(D_METHOD("get_apply_changes"), &SpatialInventoryStackAdapter::get_apply_changes);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "describe_stack"), "set_describe_stack", "get_describe_stack");
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "apply_changes"), "set_apply_changes", "get_apply_changes");
    ClassDB::bind_method(D_METHOD("invalidate_stack_records"), &SpatialInventoryStackAdapter::invalidate_stack_records);
    ClassDB::bind_method(D_METHOD("get_stack_records_revision"), &SpatialInventoryStackAdapter::get_stack_records_revision);
    ClassDB::bind_method(D_METHOD("plan_merge", "source_id", "target_id", "limit"),
            &SpatialInventoryStackAdapter::plan_merge, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("plan_split", "source_id", "new_id", "amount"), &SpatialInventoryStackAdapter::plan_split);
    ClassDB::bind_method(D_METHOD("get_split_limits", "id"), &SpatialInventoryStackAdapter::get_split_limits);
    ClassDB::bind_method(D_METHOD("get_stack_key", "id"), &SpatialInventoryStackAdapter::get_stack_key);
    ClassDB::bind_method(D_METHOD("get_merge_capacity", "id", "key"), &SpatialInventoryStackAdapter::get_merge_capacity);
    ClassDB::bind_method(D_METHOD("commit_merge", "plan", "commit_placement"), &SpatialInventoryStackAdapter::commit_merge,
            DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("commit_split", "plan", "destination", "footprint", "origin", "rotated"),
            &SpatialInventoryStackAdapter::commit_split, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("commit_detached_split", "plan", "claim_cursor"),
            &SpatialInventoryStackAdapter::commit_detached_split);
}

void SpatialInventoryStackAdapter::set_describe_stack(const Callable &callback) {
    describe_stack = callback;
}

Callable SpatialInventoryStackAdapter::get_describe_stack() const {
    return describe_stack;
}

void SpatialInventoryStackAdapter::set_apply_changes(const Callable &callback) {
    apply_changes = callback;
}

Callable SpatialInventoryStackAdapter::get_apply_changes() const {
    return apply_changes;
}

void SpatialInventoryStackAdapter::invalidate_stack_records() {
    records_revision = records_revision == (std::numeric_limits<int64_t>::max)() ? 0 : records_revision + 1;
}

int64_t SpatialInventoryStackAdapter::get_stack_records_revision() const {
    return records_revision;
}

Dictionary SpatialInventoryStackAdapter::read_stack(const StringName &id) const {
    // A host lookup can replace the adapter's configuration while it runs.
    const Callable describe = describe_stack;
    if (!describe.is_valid()) {
        return status(ERR_UNCONFIGURED);
    }
    const Variant raw = describe.call(id);
    if (raw.get_type() != Variant::DICTIONARY) {
        return status(ERR_INVALID_DATA);
    }
    const Dictionary description = raw;
    if (description.is_empty()) {
        return status(ERR_UNAVAILABLE);
    }
    const Variant key = description.get("key", Variant());
    const Variant quantity = description.get("quantity", Variant());
    const Variant capacity = description.get("max_quantity", Variant());
    if ((key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) ||
            quantity.get_type() != Variant::INT || capacity.get_type() != Variant::INT) {
        return status(ERR_INVALID_DATA);
    }
    const StringName normalized_key = key;
    if (normalized_key.is_empty() || static_cast<int64_t>(quantity) <= 0 ||
            static_cast<int64_t>(quantity) > static_cast<int64_t>(capacity)) {
        return status(ERR_INVALID_DATA);
    }
    Dictionary stack;
    stack["key"] = normalized_key;
    stack["quantity"] = quantity;
    stack["max_quantity"] = capacity;
    Dictionary result = status(OK);
    result["stack"] = stack;
    return result;
}

Dictionary SpatialInventoryStackAdapter::plan_merge(const StringName &source_id, const StringName &target_id,
        int64_t limit) const {
    if (!describe_stack.is_valid() || !apply_changes.is_valid()) {
        return status(ERR_UNCONFIGURED);
    }
    if (source_id.is_empty() || target_id.is_empty() || source_id == target_id || limit < 0) {
        return status(ERR_INVALID_PARAMETER);
    }
    const Dictionary source = read_stack(source_id);
    if (error_from(source["error"]) != OK) {
        return status(error_from(source["error"]));
    }
    const Dictionary target = read_stack(target_id);
    if (error_from(target["error"]) != OK) {
        return status(error_from(target["error"]));
    }
    const Dictionary source_before = source["stack"];
    const Dictionary target_before = target["stack"];
    if (source_before["key"] != target_before["key"]) {
        return status(ERR_UNAVAILABLE);
    }
    const int64_t target_quantity = target_before["quantity"];
    const int64_t capacity = static_cast<int64_t>(target_before["max_quantity"]) - target_quantity;
    if (capacity == 0) {
        return status(ERR_ALREADY_IN_USE);
    }
    const int64_t source_quantity = source_before["quantity"];
    int64_t moved = std::min(capacity, source_quantity);
    if (limit > 0) {
        moved = std::min(moved, limit);
    }
    const int64_t remaining = source_quantity - moved;
    return stack_plan(source_id, target_id, moved, remaining,
            change(source_id, source_before, with_quantity(source_before, remaining)),
            change(target_id, target_before, with_quantity(target_before, target_quantity + moved)));
}

Dictionary SpatialInventoryStackAdapter::get_split_limits(const StringName &id) const {
    if (!describe_stack.is_valid()) {
        return status(ERR_UNCONFIGURED);
    }
    if (id.is_empty()) {
        return status(ERR_INVALID_PARAMETER);
    }
    const Dictionary source = read_stack(id);
    if (error_from(source["error"]) != OK) {
        return status(error_from(source["error"]));
    }
    const Dictionary stack = source["stack"];
    const int64_t quantity = stack["quantity"];
    Dictionary result = status(OK);
    result["quantity"] = quantity;
    result["max_amount"] = quantity - 1;
    return result;
}

StringName SpatialInventoryStackAdapter::get_stack_key(const StringName &id) const {
    if (id.is_empty()) {
        return {};
    }
    const Dictionary result = read_stack(id);
    if (error_from(result["error"]) != OK) {
        return {};
    }
    const Dictionary stack = result["stack"];
    return stack["key"];
}

int64_t SpatialInventoryStackAdapter::get_merge_capacity(const StringName &id, const StringName &key) const {
    if (id.is_empty() || key.is_empty()) {
        return 0;
    }
    const Dictionary result = read_stack(id);
    if (error_from(result["error"]) != OK) {
        return 0;
    }
    const Dictionary stack = result["stack"];
    if (StringName(stack["key"]) != key) {
        return 0;
    }
    return static_cast<int64_t>(stack["max_quantity"]) - static_cast<int64_t>(stack["quantity"]);
}

Dictionary SpatialInventoryStackAdapter::plan_split(const StringName &source_id, const StringName &new_id,
        int64_t amount) const {
    if (!describe_stack.is_valid() || !apply_changes.is_valid()) {
        return status(ERR_UNCONFIGURED);
    }
    if (source_id.is_empty() || new_id.is_empty() || source_id == new_id || amount <= 0) {
        return status(ERR_INVALID_PARAMETER);
    }
    const Dictionary source = read_stack(source_id);
    if (error_from(source["error"]) != OK) {
        return status(error_from(source["error"]));
    }
    const Dictionary before = source["stack"];
    const int64_t quantity = before["quantity"];
    if (amount >= quantity) {
        return status(ERR_PARAMETER_RANGE_ERROR);
    }
    const Error target = error_from(read_stack(new_id)["error"]);
    if (target == OK) {
        return status(ERR_ALREADY_EXISTS);
    }
    if (target != ERR_UNAVAILABLE) {
        return status(target);
    }
    const int64_t remaining = quantity - amount;
    Dictionary created = change(new_id, {}, with_quantity(before, amount));
    created["source_id"] = source_id;
    return stack_plan(source_id, new_id, amount, remaining,
            change(source_id, before, with_quantity(before, remaining)), created);
}

Error SpatialInventoryStackAdapter::commit_merge(const Dictionary &plan, const Callable &commit_placement) {
    if (committing) {
        return ERR_BUSY;
    }
    if (!valid_plan(plan)) {
        return ERR_INVALID_PARAMETER;
    }
    const CommitGuard guard(committing);
    const Dictionary current = plan_merge(plan["source_id"], plan["target_id"], plan["moved"]);
    return apply_plan(plan, current, commit_placement);
}

Error SpatialInventoryStackAdapter::commit_split(const Dictionary &plan, const Ref<SpatialInventory> &destination,
        const Vector2i &footprint, const Vector2i &origin, bool rotated) {
    if (committing) {
        return ERR_BUSY;
    }
    if (!valid_plan(plan) || destination.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    const StringName id = plan["target_id"];
    if (!destination->get_placement(id).is_empty()) {
        return ERR_ALREADY_EXISTS;
    }
    const Error fit = destination->check_placement(footprint, origin, rotated);
    if (fit != OK) {
        return fit;
    }
    return commit_detached_split(plan, inventory_callback([destination, id, footprint, origin, rotated] {
        return destination->try_place(id, footprint, origin, rotated);
    }));
}

Error SpatialInventoryStackAdapter::commit_detached_split(const Dictionary &plan, const Callable &claim_cursor) {
    if (committing) {
        return ERR_BUSY;
    }
    if (!valid_plan(plan) || !claim_cursor.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    const CommitGuard guard(committing);
    const Dictionary current = plan_split(plan["source_id"], plan["target_id"], plan["moved"]);
    return apply_plan(plan, current, claim_cursor);
}

Error SpatialInventoryStackAdapter::apply_plan(const Dictionary &expected, const Dictionary &current,
        const Callable &placement) {
    const Callable apply = apply_changes;
    if (error_from(current["error"]) == ERR_UNCONFIGURED || !apply.is_valid()) {
        return ERR_UNCONFIGURED;
    }
    if (current != expected) {
        return ERR_BUSY;
    }
    if (!placement.is_valid()) {
        const Error result = error_from(apply.call(current["changes"], Callable()));
        invalidate_stack_records();
        return result;
    }

    // A host may retain the Callable. Expire it when the transaction returns.
    auto commit = std::make_shared<PlacementCommit>();
    commit->placement = placement;
    const Callable guarded = inventory_callback([commit] { return commit->run(); });
    const Error result = error_from(apply.call(current["changes"], guarded));
    commit->placement = Callable();
    invalidate_stack_records();
    return result == OK ? commit->result : result;
}

} // namespace crimson::inventory
