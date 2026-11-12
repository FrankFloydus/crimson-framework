// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_equipment.hpp"

#include "inventory_model_dto.hpp"

#include <algorithm>

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::inventory {
namespace {

// Tests whether a Variant can represent a StringName identifier.
bool is_name(const Variant &value) {
    return (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) &&
            !StringName(value).is_empty();
}

// Computes the footprint in its placed orientation.
Vector2i extent(const Dictionary &item) {
    const Vector2i footprint = item["footprint"];
    return bool(item["rotated"]) ? Vector2i(footprint.y, footprint.x) : footprint;
}

// Builds an equipped item record and its occupied slots.
Dictionary equipped_item(const StringName &id, const Vector2i &footprint, bool rotated,
        const StringName &slot, const TypedArray<StringName> &slots) {
    return detail::make_model_dto("id", id, "footprint", footprint, "rotated", rotated, "slot", slot, "slots", slots);
}

// Builds a placement record for an item returning to the backpack.
Dictionary placed_item(const Dictionary &item, const Vector2i &origin, bool rotated) {
    return detail::make_model_dto("id", item["id"], "footprint", item["footprint"], "origin", origin, "rotated", rotated);
}

// Adds an error and reason to an equipment operation result.
Dictionary fail(Dictionary &result, Error error, const String &reason) {
    result["error"] = error;
    result["reason"] = reason;
    result["changes"] = TypedArray<Dictionary>();
    return result;
}

// Checks that a fresh equipment plan matches the caller's preview.
bool matches_preview(const Dictionary &expected, const Dictionary &current) {
    for (const char *key : {"error", "action", "id", "slot", "slots", "displaced", "changes",
            "equipment_revision", "backpack_revision", "committed"}) {
        if (!expected.has(key) || expected[key] != current[key]) {
            return false;
        }
    }
    return true;
}

// Finds the next candidate column beyond blocking reservations.
int reserved_end_x(const Rect2i &candidate, const std::vector<Rect2i> &reserved) {
    int end_x = candidate.position.x;
    for (const Rect2i &other : reserved) {
        if (candidate.intersects(other)) {
            end_x = std::max(end_x, other.get_end().x);
        }
    }
    return end_x;
}

} // namespace

const Vector2i SpatialInventoryEquipment::first_fit{-1, -1};

void SpatialInventoryEquipment::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_slot_requirements", "rules"), &SpatialInventoryEquipment::set_slot_requirements);
    ClassDB::bind_method(D_METHOD("get_slot_requirements"), &SpatialInventoryEquipment::get_slot_requirements);
    ClassDB::bind_method(D_METHOD("set_move_policy", "policy"), &SpatialInventoryEquipment::set_move_policy);
    ClassDB::bind_method(D_METHOD("get_move_policy"), &SpatialInventoryEquipment::get_move_policy);
    ClassDB::bind_method(D_METHOD("get_revision"), &SpatialInventoryEquipment::get_revision);
    ClassDB::bind_method(D_METHOD("get_backpack"), &SpatialInventoryEquipment::get_backpack);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "slot_requirements"), "set_slot_requirements", "get_slot_requirements");
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "move_policy"), "set_move_policy", "get_move_policy");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "revision", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_INSTANCE_STATE), "", "get_revision");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "backpack", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_INSTANCE_STATE,
            "SpatialInventory"), "", "get_backpack");
    ClassDB::bind_method(D_METHOD("configure", "source", "slot_ids"), &SpatialInventoryEquipment::configure);
    ClassDB::bind_method(D_METHOD("get_slot_ids"), &SpatialInventoryEquipment::get_slot_ids);
    ClassDB::bind_method(D_METHOD("get_slot_item", "slot"), &SpatialInventoryEquipment::get_slot_item);
    ClassDB::bind_method(D_METHOD("get_equipped", "id"), &SpatialInventoryEquipment::get_equipped);
    ClassDB::bind_method(D_METHOD("get_equipped_items"), &SpatialInventoryEquipment::get_equipped_items);
    ClassDB::bind_method(D_METHOD("preview_equip", "id", "slot"), &SpatialInventoryEquipment::preview_equip);
    ClassDB::bind_method(D_METHOD("equip", "id", "slot", "expected"), &SpatialInventoryEquipment::equip, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("preview_unequip", "slot", "origin", "rotated"), &SpatialInventoryEquipment::preview_unequip,
            DEFVAL(first_fit), DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("unequip", "slot", "origin", "rotated", "expected"), &SpatialInventoryEquipment::unequip,
            DEFVAL(first_fit), DEFVAL(Variant()), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("export_snapshot"), &SpatialInventoryEquipment::export_snapshot);
    ClassDB::bind_method(D_METHOD("import_snapshot", "snapshot"), &SpatialInventoryEquipment::import_snapshot);
    ADD_SIGNAL(MethodInfo("changed"));
}

void SpatialInventoryEquipment::set_slot_requirements(const Callable &rules) { slot_requirements = rules; }
Callable SpatialInventoryEquipment::get_slot_requirements() const { return slot_requirements; }
void SpatialInventoryEquipment::set_move_policy(const Callable &policy) { move_policy = policy; }
Callable SpatialInventoryEquipment::get_move_policy() const { return move_policy; }
int64_t SpatialInventoryEquipment::get_revision() const { return revision; }
Ref<SpatialInventory> SpatialInventoryEquipment::get_backpack() const { return backpack; }

Error SpatialInventoryEquipment::configure(const Ref<SpatialInventory> &source, const Array &slots) {
    if (busy) {
        return ERR_BUSY;
    }
    if (source.is_null() || source->get_grid_size() == Vector2i() || slots.is_empty() || slots.size() > max_slots) {
        return ERR_INVALID_PARAMETER;
    }
    TypedArray<StringName> names;
    Dictionary positions;
    for (int64_t index = 0; index < slots.size(); ++index) {
        const Variant &value = slots[index];
        if (!is_name(value)) {
            return ERR_INVALID_PARAMETER;
        }
        const StringName slot = value;
        if (positions.has(slot)) {
            return ERR_INVALID_PARAMETER;
        }
        positions[slot] = index;
        names.append(slot);
    }
    if (backpack == source && slot_ids == names) {
        return OK;
    }
    if (!items.is_empty()) {
        return ERR_BUSY;
    }
    backpack = source;
    slot_ids = names;
    slot_positions = positions;
    busy = true;
    ++revision;
    emit_signal("changed");
    busy = false;
    return OK;
}

TypedArray<StringName> SpatialInventoryEquipment::get_slot_ids() const { return slot_ids.duplicate(); }

StringName SpatialInventoryEquipment::get_slot_item(const StringName &slot) const {
    return occupants.get(slot, StringName());
}

Dictionary SpatialInventoryEquipment::get_equipped(const StringName &id) const {
    const Dictionary item = items.get(id, Dictionary());
    return item.duplicate(true);
}

TypedArray<Dictionary> SpatialInventoryEquipment::get_equipped_items() const {
    TypedArray<Dictionary> result;
    for (int64_t index = 0; index < slot_ids.size(); ++index) {
        const StringName slot = slot_ids[index];
        const StringName id = get_slot_item(slot);
        if (!id.is_empty()) {
            const Dictionary item = items[id];
            if (StringName(item["slot"]) == slot) {
                result.append(item.duplicate(true));
            }
        }
    }
    return result;
}

Dictionary SpatialInventoryEquipment::preview_equip(const StringName &id, const StringName &slot) {
    return run("equip", id, slot, first_fit, {}, false, {});
}

Dictionary SpatialInventoryEquipment::equip(const StringName &id, const StringName &slot, const Dictionary &expected) {
    return run("equip", id, slot, first_fit, {}, true, expected);
}

Dictionary SpatialInventoryEquipment::preview_unequip(const StringName &slot, const Vector2i &origin, const Variant &rotated) {
    return run("unequip", get_slot_item(slot), slot, origin, rotated, false, {});
}

Dictionary SpatialInventoryEquipment::unequip(const StringName &slot, const Vector2i &origin,
        const Variant &rotated, const Dictionary &expected) {
    return run("unequip", get_slot_item(slot), slot, origin, rotated, true, expected);
}

Dictionary SpatialInventoryEquipment::export_snapshot() const {
    return detail::make_model_dto("version", snapshot_version, "items", get_equipped_items());
}

Error SpatialInventoryEquipment::import_snapshot(const Dictionary &snapshot) {
    if (busy || !items.is_empty()) {
        return ERR_BUSY;
    }
    if (backpack.is_null() || backpack->get_grid_size() == Vector2i()) {
        return ERR_UNCONFIGURED;
    }
    const Variant version = snapshot.get("version", {});
    const Variant raw_records = snapshot.get("items", {});
    if (version.get_type() != Variant::INT || int64_t(version) != snapshot_version ||
            raw_records.get_type() != Variant::ARRAY || Array(raw_records).size() > slot_ids.size()) {
        return ERR_INVALID_DATA;
    }
    busy = true;
    const Error error = restore_items(raw_records);
    busy = false;
    return error;
}

Dictionary SpatialInventoryEquipment::run(const StringName &action, const StringName &id, const StringName &slot,
        const Vector2i &origin, const Variant &rotated, bool commit, const Dictionary &expected) {
    Dictionary result = new_result(action, id, slot);
    if (busy) {
        return fail(result, ERR_BUSY, "An equipment action is already running.");
    }
    if (backpack.is_null() || backpack->get_grid_size() == Vector2i()) {
        return fail(result, ERR_UNCONFIGURED, "Configure equipment and its backpack first.");
    }
    if (!slot_positions.has(slot) || (rotated.get_type() != Variant::NIL && rotated.get_type() != Variant::BOOL)) {
        return fail(result, ERR_INVALID_PARAMETER, "Choose a configured equipment slot and a valid rotation.");
    }
    if (id.is_empty()) {
        return fail(result, ERR_DOES_NOT_EXIST, "Choose an item to move.");
    }
    busy = true;
    Dictionary incoming;
    if (action == StringName("equip")) {
        incoming = plan_equip(result);
    } else {
        plan_unequip(result, origin, rotated);
    }
    if (backpack->get_placement_revision() != uint64_t(result["backpack_revision"])) {
        fail(result, ERR_BUSY, "The backpack changed during equipment validation.");
    }
    if (commit && int64_t(result["error"]) == OK) {
        if (expected.is_empty() || matches_preview(expected, result)) {
            commit_plan(result, incoming);
        } else {
            fail(result, ERR_BUSY, "The equipment preview changed. Review the new placement and try again.");
        }
    }
    busy = false;
    return result;
}

Dictionary SpatialInventoryEquipment::plan_equip(Dictionary &result) const {
    const StringName id = result["id"];
    const Dictionary source = backpack->get_placement(id);
    if (source.is_empty()) {
        fail(result, ERR_DOES_NOT_EXIST, "The item is outside the backpack.");
        return {};
    }
    if (items.has(id)) {
        fail(result, ERR_ALREADY_EXISTS, "The equipment endpoint already owns this item.");
        return {};
    }
    const StringName slot = result["slot"];
    const Dictionary requirements = get_requirements(id, slot);
    const Error rules_error = Error(int64_t(requirements["error"]));
    if (rules_error != OK) {
        fail(result, rules_error, requirements["reason"]);
        return {};
    }
    const TypedArray<StringName> slots = requirements["slots"];
    const Dictionary incoming = equipped_item(id, source["footprint"], source["rotated"], slot, slots);
    String denial = check_move_policy("equip", incoming, source, incoming, false);
    if (!denial.is_empty()) {
        fail(result, ERR_UNAUTHORIZED, denial);
        return {};
    }

    std::vector<StringName> displaced;
    Dictionary seen;
    for (int64_t index = 0; index < slots.size(); ++index) {
        const StringName occupant = get_slot_item(slots[index]);
        if (!occupant.is_empty() && !seen.has(occupant)) {
            seen[occupant] = true;
            displaced.push_back(occupant);
        }
    }
    std::stable_sort(displaced.begin(), displaced.end(), [this](const StringName &first, const StringName &second) {
        return displacement_order(first, second);
    });
    TypedArray<StringName> displaced_ids;
    for (const StringName &occupant : displaced) {
        displaced_ids.append(occupant);
    }
    result["slots"] = slots.duplicate();
    result["displaced"] = displaced_ids;

    TypedArray<Dictionary> changes;
    changes.append(placement_row(id, source, {}));
    std::vector<Rect2i> reserved;
    for (const StringName &occupant : displaced) {
        if (!backpack->get_placement(occupant).is_empty()) {
            fail(result, ERR_ALREADY_EXISTS, "A displaced item already exists in the backpack.");
            return {};
        }
        const Dictionary equipped = items[occupant];
        const Vector2i origin = find_return(equipped, source, reserved);
        if (origin == first_fit) {
            fail(result, ERR_CANT_CREATE, "The backpack has no room for all displaced equipment.");
            return {};
        }
        const Dictionary placement = placed_item(equipped, origin, equipped["rotated"]);
        denial = check_move_policy("unequip", equipped, equipped, placement, true);
        if (!denial.is_empty()) {
            fail(result, ERR_UNAUTHORIZED, denial);
            return {};
        }
        reserved.emplace_back(origin, extent(equipped));
        changes.append(placement_row(occupant, {}, placement));
    }
    result["changes"] = changes;
    return incoming;
}

void SpatialInventoryEquipment::plan_unequip(Dictionary &result, Vector2i origin, const Variant &rotated) const {
    const StringName id = result["id"];
    if (!backpack->get_placement(id).is_empty()) {
        fail(result, ERR_ALREADY_EXISTS, "The backpack already owns this equipped item.");
        return;
    }
    const Dictionary equipped = items[id];
    const bool rotation = rotated.get_type() == Variant::NIL ? bool(equipped["rotated"]) : bool(rotated);
    const Vector2i footprint = equipped["footprint"];
    if (origin == first_fit) {
        origin = backpack->find_first_fit(footprint, rotation);
        if (origin == first_fit) {
            fail(result, ERR_CANT_CREATE, "The backpack has no room for this equipment.");
            return;
        }
    } else {
        const Error error = backpack->check_placement(footprint, origin, rotation);
        if (error != OK) {
            fail(result, error, "The equipped item does not fit at that backpack cell.");
            return;
        }
    }
    const Dictionary placement = placed_item(equipped, origin, rotation);
    const String denial = check_move_policy("unequip", equipped, equipped, placement, false);
    if (!denial.is_empty()) {
        fail(result, ERR_UNAUTHORIZED, denial);
        return;
    }
    TypedArray<Dictionary> changes;
    changes.append(placement_row(id, {}, placement));
    result["changes"] = changes;
    result["slots"] = Array(equipped["slots"]).duplicate();
}

void SpatialInventoryEquipment::commit_plan(Dictionary &result, const Dictionary &incoming) {
    const Dictionary old_items = items;
    const Dictionary old_occupants = occupants;
    items = old_items.duplicate();
    occupants = old_occupants.duplicate();
    if (incoming.is_empty()) {
        remove_equipped(result["id"]);
    } else {
        const Array displaced = result["displaced"];
        for (int64_t index = 0; index < displaced.size(); ++index) {
            remove_equipped(displaced[index]);
        }
        const StringName id = incoming["id"];
        items[id] = incoming;
        const Array slots = incoming["slots"];
        for (int64_t index = 0; index < slots.size(); ++index) {
            occupants[slots[index]] = id;
        }
    }
    ++revision;
    // Backpack listeners observe the equipment state from the same transaction.
    const Error error = SpatialInventory::apply_placement_changes(result["changes"]);
    if (error != OK) {
        items = old_items;
        occupants = old_occupants;
        --revision;
        fail(result, error, "The backpack changed before equipment could commit.");
        return;
    }
    result["committed"] = true;
    emit_signal("changed");
}

void SpatialInventoryEquipment::remove_equipped(const StringName &id) {
    const Dictionary item = items[id];
    const Array slots = item["slots"];
    for (int64_t index = 0; index < slots.size(); ++index) {
        occupants.erase(slots[index]);
    }
    items.erase(id);
}

Error SpatialInventoryEquipment::restore_items(const Array &records) {
    const uint64_t backpack_revision = backpack->get_placement_revision();
    Dictionary restored;
    Dictionary restored_occupants;
    for (int64_t index = 0; index < records.size(); ++index) {
        const Variant &value = records[index];
        if (value.get_type() != Variant::DICTIONARY) {
            return ERR_INVALID_DATA;
        }
        const Dictionary record = value;
        const Variant raw_id = record.get("id", {});
        const Variant raw_slot = record.get("slot", {});
        const Variant raw_footprint = record.get("footprint", {});
        const Variant rotated = record.get("rotated", {});
        const Variant raw_slots = record.get("slots", {});
        if (!is_name(raw_id) || !is_name(raw_slot) || raw_footprint.get_type() != Variant::VECTOR2I ||
                rotated.get_type() != Variant::BOOL || raw_slots.get_type() != Variant::ARRAY) {
            return ERR_INVALID_DATA;
        }
        const Vector2i footprint = raw_footprint;
        if (footprint.x <= 0 || footprint.y <= 0) {
            return ERR_INVALID_DATA;
        }
        const StringName id = raw_id;
        const StringName slot = raw_slot;
        if (restored.has(id) || !backpack->get_placement(id).is_empty()) {
            return ERR_ALREADY_EXISTS;
        }
        const TypedArray<StringName> slots = normalized_slots(raw_slots, slot);
        if (slots.is_empty()) {
            return ERR_INVALID_DATA;
        }
        const Dictionary requirements = get_requirements(id, slot);
        const Error rules_error = Error(int64_t(requirements["error"]));
        if (rules_error != OK) {
            return rules_error;
        }
        if (Array(requirements["slots"]) != slots) {
            return ERR_INVALID_DATA;
        }
        for (int64_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
            const StringName occupied = slots[slot_index];
            if (restored_occupants.has(occupied)) {
                return ERR_ALREADY_EXISTS;
            }
            restored_occupants[occupied] = id;
        }
        restored[id] = equipped_item(id, footprint, rotated, slot, slots);
    }
    if (backpack->get_placement_revision() != backpack_revision) {
        return ERR_BUSY;
    }
    if (!restored.is_empty()) {
        items = restored;
        occupants = restored_occupants;
        ++revision;
        emit_signal("changed");
    }
    return OK;
}

Vector2i SpatialInventoryEquipment::find_return(const Dictionary &item, const Dictionary &source,
        const std::vector<Rect2i> &reserved) const {
    const Vector2i preferred = source["origin"];
    const Vector2i item_extent = extent(item);
    const Vector2i size = backpack->get_grid_size();
    if (item_extent.x > size.x || item_extent.y > size.y) {
        return first_fit;
    }
    const StringName source_id = source["id"];
    const Vector2i footprint = item["footprint"];
    const bool rotated = item["rotated"];
    if (reserved_end_x(Rect2i(preferred, item_extent), reserved) == preferred.x &&
            backpack->get_swap_target(footprint, preferred, rotated) == source_id) {
        return preferred;
    }
    const Vector2i native_fit = find_unreserved(item, item_extent, reserved);
    // Only positions overlapping the vacated item need checking beyond the grid's normal scan.
    const Vector2i one(1, 1);
    const Vector2i first = (preferred - item_extent + one).max(Vector2i());
    const Vector2i last = (preferred + extent(source) - one).min(size - item_extent);
    for (int y = first.y; y <= last.y; ++y) {
        if (native_fit != first_fit && y > native_fit.y) {
            break;
        }
        const int end_x = y == native_fit.y ? std::min(last.x, native_fit.x - 1) : last.x;
        for (int x = first.x; x <= end_x;) {
            const Vector2i origin(x, y);
            const int skip_x = reserved_end_x(Rect2i(origin, item_extent), reserved);
            if (skip_x == x && backpack->get_swap_target(footprint, origin, rotated) == source_id) {
                return origin;
            }
            x = std::max(x + 1, skip_x);
        }
    }
    return native_fit;
}

Vector2i SpatialInventoryEquipment::find_unreserved(const Dictionary &item, const Vector2i &item_extent,
        const std::vector<Rect2i> &reserved) const {
    Vector2i start;
    const Vector2i footprint = item["footprint"];
    const bool rotated = item["rotated"];
    for (;;) {
        const Vector2i fit = backpack->find_first_fit(footprint, rotated, start);
        if (fit == first_fit) {
            return fit;
        }
        const int skip_x = reserved_end_x(Rect2i(fit, item_extent), reserved);
        if (skip_x == fit.x) {
            return fit;
        }
        start = Vector2i(skip_x, fit.y);
    }
}

bool SpatialInventoryEquipment::displacement_order(const StringName &first_id, const StringName &second_id) const {
    const Dictionary first = items[first_id];
    const Dictionary second = items[second_id];
    const Vector2i first_size = first["footprint"];
    const Vector2i second_size = second["footprint"];
    const int64_t first_area = int64_t(first_size.x) * first_size.y;
    const int64_t second_area = int64_t(second_size.x) * second_size.y;
    if (first_area != second_area) {
        return first_area > second_area;
    }
    return int64_t(slot_positions[first["slot"]]) < int64_t(slot_positions[second["slot"]]);
}

Dictionary SpatialInventoryEquipment::get_requirements(const StringName &id, const StringName &slot) const {
    Variant value = Array::make(slot);
    if (!slot_requirements.is_null()) {
        if (!slot_requirements.is_valid()) {
            return detail::make_model_dto("error", ERR_UNAUTHORIZED, "reason", "The equipment rules are unavailable.");
        }
        value = slot_requirements.call(id, slot);
    }
    if (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) {
        const String reason = value;
        return detail::make_model_dto("error", ERR_UNAUTHORIZED, "reason", reason.is_empty() ? String("This item cannot use the slot.") : reason);
    }
    if (value.get_type() != Variant::ARRAY) {
        return detail::make_model_dto("error", ERR_INVALID_DATA, "reason", "Equipment rules must return named slots or a denial reason.");
    }
    const TypedArray<StringName> slots = normalized_slots(value, slot);
    if (slots.is_empty()) {
        return detail::make_model_dto("error", ERR_INVALID_DATA, "reason", "Equipment rules must include the chosen slot and unique configured slots.");
    }
    return detail::make_model_dto("error", OK, "slots", slots);
}

TypedArray<StringName> SpatialInventoryEquipment::normalized_slots(const Array &raw, const StringName &requested) const {
    if (raw.is_empty() || raw.size() > slot_ids.size()) {
        return {};
    }
    Dictionary seen;
    for (int64_t index = 0; index < raw.size(); ++index) {
        const Variant &value = raw[index];
        if (!is_name(value)) {
            return {};
        }
        const StringName slot = value;
        if (!slot_positions.has(slot) || seen.has(slot)) {
            return {};
        }
        seen[slot] = true;
    }
    if (!seen.has(requested)) {
        return {};
    }
    TypedArray<StringName> slots;
    for (int64_t index = 0; index < slot_ids.size(); ++index) {
        if (seen.has(slot_ids[index])) {
            slots.append(slot_ids[index]);
        }
    }
    return slots;
}

String SpatialInventoryEquipment::check_move_policy(const StringName &action, const Dictionary &item,
        const Dictionary &source, const Dictionary &destination, bool displaced) const {
    if (move_policy.is_null()) {
        return {};
    }
    if (!move_policy.is_valid()) {
        return "The equipment movement policy is unavailable.";
    }
    const Dictionary context = detail::make_model_dto(
            "action", action, "id", item["id"], "slot", item["slot"], "slots", Array(item["slots"]).duplicate(),
            "backpack", backpack, "placement", source.duplicate(true),
            "destination_placement", destination.duplicate(true), "displaced", displaced);
    const Variant value = move_policy.call(context);
    return value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME ?
            String(value) : String("Equipment movement policy must return a String.");
}

Dictionary SpatialInventoryEquipment::new_result(const StringName &action, const StringName &id, const StringName &slot) const {
    const int64_t backpack_revision = backpack.is_valid() ? int64_t(backpack->get_placement_revision()) : -1;
    return detail::make_model_dto(
            "error", OK, "reason", "", "action", action, "id", id, "slot", slot, "committed", false,
            "slots", TypedArray<StringName>(), "displaced", TypedArray<StringName>(), "changes", TypedArray<Dictionary>(),
            "equipment_revision", revision, "backpack_revision", backpack_revision);
}

Dictionary SpatialInventoryEquipment::placement_row(const StringName &id, const Dictionary &before, const Dictionary &after) const {
    return detail::make_model_dto("inventory", backpack, "id", id, "before", before, "after", after);
}

} // namespace crimson::inventory
