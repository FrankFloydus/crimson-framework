// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_trade.hpp"

#include "inventory_callbacks.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <utility>

namespace crimson::inventory {

using namespace godot;

namespace {

// Creates a trade result or record from named values.
Dictionary make_dictionary(std::initializer_list<std::pair<Variant, Variant>> entries) {
    Dictionary result;
    for (const auto &[key, value] : entries) {
        result[key] = value;
    }
    return result;
}

// Creates a detached inventory model for trade planning.
Ref<SpatialInventory> new_grid() {
    Ref<SpatialInventory> grid;
    grid.instantiate();
    return grid;
}

// The host can retain the callable. Close the gate before leaving its synchronous callback.
struct CommitGate {
    // Whether the one-shot host commit callable can still run.
    bool open = true;
    // Whether the one-shot placement callback has been invoked.
    bool called = false;
    // Godot error code for the operation.
    Error error = ERR_INVALID_DATA;
};

// Wraps a synchronous commit in a one-shot callable.
template <typename Commit> Callable once(const std::shared_ptr<CommitGate> &gate, Commit commit) {
    return inventory_callback([gate, commit]() -> Error {
        if (!gate->open || gate->called) {
            return ERR_BUSY;
        }
        gate->called = true;
        gate->error = commit();
        return gate->error;
    });
}

// Closes a one-shot commit gate and returns its outcome.
Error finish_commit(CommitGate &gate, const Variant &result) {
    gate.open = false;
    if (gate.called) {
        return gate.error;
    }
    if (result.get_type() == Variant::INT && int64_t(result) != OK) {
        return static_cast<Error>(int64_t(result));
    }
    return ERR_INVALID_DATA;
}

// Orders offer placements by their grid cells.
bool compare_origins(Dictionary left, Dictionary right) {
    const Dictionary left_placement = left["placement"];
    const Dictionary right_placement = right["placement"];
    const Vector2i a = left_placement["origin"];
    const Vector2i b = right_placement["origin"];
    return a.y < b.y || (a.y == b.y && a.x < b.x);
}

} // namespace

void SpatialInventoryTrade::set_wallet(const Ref<SpatialInventoryWallet> &wallet) {
    if (!busy_ && currency_offers_.is_empty()) {
        wallet_ = wallet;
    }
}

void SpatialInventoryTrade::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_participant", "actor", "backpack", "size", "offer", "recovery"),
                         &SpatialInventoryTrade::register_participant, DEFVAL(Ref<SpatialInventory>()),
                         DEFVAL(Ref<SpatialInventory>()));
    ClassDB::bind_method(D_METHOD("get_offer", "actor"), &SpatialInventoryTrade::get_offer);
    ClassDB::bind_method(D_METHOD("get_recovery", "actor"), &SpatialInventoryTrade::get_recovery);
    ClassDB::bind_method(D_METHOD("is_locked", "actor"), &SpatialInventoryTrade::is_locked);
    ClassDB::bind_method(D_METHOD("is_reserved", "actor", "id"), &SpatialInventoryTrade::is_reserved);
    ClassDB::bind_method(D_METHOD("get_reserved_source_ids", "actor"), &SpatialInventoryTrade::get_reserved_source_ids);
    ClassDB::bind_method(D_METHOD("get_offer_stack", "actor", "offer_id"), &SpatialInventoryTrade::get_offer_stack);
    ClassDB::bind_method(D_METHOD("get_currency_offers", "actor"), &SpatialInventoryTrade::get_currency_offers);
    ClassDB::bind_method(D_METHOD("set_currency_offer", "actor", "currency", "amount"),
                         &SpatialInventoryTrade::set_currency_offer);
    ClassDB::bind_method(D_METHOD("export_snapshot"), &SpatialInventoryTrade::export_snapshot);
    ClassDB::bind_method(D_METHOD("import_snapshot", "snapshot"), &SpatialInventoryTrade::import_snapshot);
    ClassDB::bind_method(D_METHOD("request", "actor"), &SpatialInventoryTrade::request);
    ClassDB::bind_method(D_METHOD("accept", "actor"), &SpatialInventoryTrade::accept);
    ClassDB::bind_method(D_METHOD("cancel", "actor"), &SpatialInventoryTrade::cancel);
    ClassDB::bind_method(D_METHOD("offer_item", "actor", "id", "origin", "rotated"), &SpatialInventoryTrade::offer_item,
                         DEFVAL(FIRST_FIT), DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("offer_quantity", "actor", "id", "amount", "origin", "rotated", "target_id"),
                         &SpatialInventoryTrade::offer_quantity, DEFVAL(Variant()), DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("offer_items", "actor", "ids"), &SpatialInventoryTrade::offer_items);
    ClassDB::bind_method(D_METHOD("move_offer", "actor", "id", "origin", "rotated"),
                         &SpatialInventoryTrade::move_offer);
    ClassDB::bind_method(D_METHOD("withdraw", "actor", "id", "origin", "rotated", "target_id"),
                         &SpatialInventoryTrade::withdraw, DEFVAL(FIRST_FIT), DEFVAL(Variant()), DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("merge_offer_stacks", "actor", "source_id", "target_id", "limit"),
                         &SpatialInventoryTrade::merge_offer_stacks, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("withdraw_items", "actor", "ids"), &SpatialInventoryTrade::withdraw_items);
    ClassDB::bind_method(D_METHOD("set_lock", "actor", "locked"), &SpatialInventoryTrade::set_lock);

    ClassDB::bind_method(D_METHOD("get_phase"), &SpatialInventoryTrade::get_phase);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "phase"), "", "get_phase");
    ClassDB::bind_method(D_METHOD("get_initiator"), &SpatialInventoryTrade::get_initiator);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "initiator"), "", "get_initiator");
    ClassDB::bind_method(D_METHOD("get_receiver"), &SpatialInventoryTrade::get_receiver);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "receiver"), "", "get_receiver");
    ClassDB::bind_method(D_METHOD("get_revision"), &SpatialInventoryTrade::get_revision);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "revision"), "", "get_revision");
    ClassDB::bind_method(D_METHOD("get_describe_stack"), &SpatialInventoryTrade::get_describe_stack);
    ClassDB::bind_method(D_METHOD("set_describe_stack", "value"), &SpatialInventoryTrade::set_describe_stack);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "describe_stack"), "set_describe_stack", "get_describe_stack");
    ClassDB::bind_method(D_METHOD("get_allocate_stack_id"), &SpatialInventoryTrade::get_allocate_stack_id);
    ClassDB::bind_method(D_METHOD("set_allocate_stack_id", "value"), &SpatialInventoryTrade::set_allocate_stack_id);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "allocate_stack_id"), "set_allocate_stack_id",
                 "get_allocate_stack_id");
    ClassDB::bind_method(D_METHOD("get_apply_stack_changes"), &SpatialInventoryTrade::get_apply_stack_changes);
    ClassDB::bind_method(D_METHOD("set_apply_stack_changes", "value"), &SpatialInventoryTrade::set_apply_stack_changes);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "apply_stack_changes"), "set_apply_stack_changes",
                 "get_apply_stack_changes");
    ClassDB::bind_method(D_METHOD("get_participant_eligibility"), &SpatialInventoryTrade::get_participant_eligibility);
    ClassDB::bind_method(D_METHOD("set_participant_eligibility", "value"),
                         &SpatialInventoryTrade::set_participant_eligibility);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "participant_eligibility"), "set_participant_eligibility",
                 "get_participant_eligibility");
    ClassDB::bind_method(D_METHOD("get_item_eligibility"), &SpatialInventoryTrade::get_item_eligibility);
    ClassDB::bind_method(D_METHOD("set_item_eligibility", "value"), &SpatialInventoryTrade::set_item_eligibility);
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "item_eligibility"), "set_item_eligibility", "get_item_eligibility");
    ClassDB::bind_method(D_METHOD("get_wallet"), &SpatialInventoryTrade::get_wallet);
    ClassDB::bind_method(D_METHOD("set_wallet", "value"), &SpatialInventoryTrade::set_wallet);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "wallet", PROPERTY_HINT_RESOURCE_TYPE, "SpatialInventoryWallet"),
                 "set_wallet", "get_wallet");

    BIND_ENUM_CONSTANT(IDLE);
    BIND_ENUM_CONSTANT(REQUESTED);
    BIND_ENUM_CONSTANT(ACTIVE);
    BIND_CONSTANT(SNAPSHOT_VERSION);
    BIND_CONSTANT(MAX_BALANCE);
    BIND_CONSTANT(MAX_SNAPSHOT_REVISION);
    BIND_CONSTANT(MAX_CURRENCIES_PER_OFFER);
    BIND_CONSTANT(MAX_RETURN_SOURCES);
}

Error SpatialInventoryTrade::register_participant(int64_t actor, Ref<SpatialInventory> backpack, Vector2i size,
                                                  Ref<SpatialInventory> offer, Ref<SpatialInventory> recovery) {
    if (busy_ || phase_ != IDLE) {
        return ERR_BUSY;
    }
    if (actor <= 0 || backpack.is_null() || backpack->get_grid_size() == Vector2i()) {
        return ERR_INVALID_PARAMETER;
    }
    if (backpacks_.has(actor) || (!offer.is_null() && offer == recovery)) {
        return ERR_ALREADY_EXISTS;
    }
    Array registered = backpacks_.values() + offers_.values() + recoveries_.values();
    for (const Variant &raw_model : Array{backpack, offer, recovery}) {
        const Ref<SpatialInventory> model = Ref<SpatialInventory>(raw_model);
        if (!model.is_null() && registered.has(model)) {
            return ERR_ALREADY_EXISTS;
        }
        if (!model.is_null()) {
            registered.append(model);
        }
    }
    if (backpacks_.size() >= 2) {
        return ERR_ALREADY_IN_USE;
    }
    if (offer.is_null()) {
        offer = new_grid();
        Error error = offer->configure(size);
        if (error != OK) {
            return error;
        }
    } else if (offer->get_grid_size() != size || !offer->get_placements().is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    if (recovery.is_null()) {
        recovery = new_grid();
        Error error = recovery->configure(size);
        if (error != OK) {
            return error;
        }
    } else if (recovery->get_grid_size() != size) {
        return ERR_INVALID_PARAMETER;
    }
    if (size.x <= 0 || size.y <= 0) {
        return ERR_INVALID_PARAMETER;
    }
    backpacks_[actor] = backpack;
    offers_[actor] = offer;
    recoveries_[actor] = recovery;
    return OK;
}

// Bind this model to a view. Route writes through this trade, since the grid projects live items.
Ref<SpatialInventory> SpatialInventoryTrade::get_offer(int64_t actor) {
    return Ref<SpatialInventory>(offers_.get(actor, Variant()));
}

// The owner may claim returned items here. Reserve this grid for cancellation, with no game deposits.
Ref<SpatialInventory> SpatialInventoryTrade::get_recovery(int64_t actor) {
    return Ref<SpatialInventory>(recoveries_.get(actor, Variant()));
}

bool SpatialInventoryTrade::is_locked(int64_t actor) { return locks_.has(actor); }

// The host must protect reserved non-stack instances from unrelated movement or record changes.
bool SpatialInventoryTrade::is_reserved(int64_t actor, StringName id) {
    return reserved_sources_.has(actor) && Dictionary(reserved_sources_[actor]).has(id);
}

TypedArray<StringName> SpatialInventoryTrade::get_reserved_source_ids(int64_t actor) {
    TypedArray<StringName> ids;
    ids.assign(Dictionary(reserved_sources_.get(actor, Dictionary())).keys());
    return ids;
}

// Returns the escrow stack description. The offer ID also identifies its game-owned record.
Dictionary SpatialInventoryTrade::get_offer_stack(int64_t actor, StringName offer_id) {
    Dictionary reservation = get_reservation(actor, offer_id);
    if (reservation.is_empty() || Dictionary(reservation["stack"]).is_empty()) {
        return Dictionary();
    }
    Dictionary stack = Dictionary(reservation["stack"]).duplicate();
    Dictionary sources = Dictionary(reservation["sources"]);
    stack["source_id"] = (!sources.is_empty() ? StringName(sources.keys()[0]) : StringName());
    stack["record_id"] = offer_id;
    return stack;
}

// Read-only query. An offer promises to pay at settlement; it does not debit the wallet.
Dictionary SpatialInventoryTrade::get_currency_offers(int64_t actor) {
    return Dictionary(currency_offers_.get(actor, Dictionary())).duplicate();
}

Dictionary SpatialInventoryTrade::set_currency_offer(int64_t actor, StringName currency, int64_t amount) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (currency.is_empty() || amount < 0) {
        return make_result(ERR_INVALID_PARAMETER, "Choose a currency and a nonnegative amount.");
    }
    Dictionary offers = Dictionary(currency_offers_.get(actor, Dictionary()));
    int64_t previous = static_cast<int64_t>(offers.get(currency, 0));
    if (amount == previous) {
        return make_result(OK, "Currency offer unchanged.");
    }
    if (amount > 0) {
        if (wallet_.is_null()) {
            return make_result(ERR_UNCONFIGURED, "Configure a trade wallet first.");
        }
        String refusal = participant_refusal(StringName("offer"), actor, other_actor(actor));
        if (!refusal.is_empty()) {
            return make_result(ERR_UNAUTHORIZED, refusal);
        }
        if (amount > read_balance(actor, currency)) {
            return make_result(ERR_UNAUTHORIZED, "You do not have enough of that currency.");
        }
        if (previous == 0 && offers.size() >= MAX_CURRENCIES_PER_OFFER) {
            return make_result(ERR_PARAMETER_RANGE_ERROR, "The currency offer limit was reached.");
        }
        offers[currency] = amount;
        currency_offers_[actor] = offers;
    } else {
        offers.erase(currency);
        if (offers.is_empty()) {
            currency_offers_.erase(actor);
        }
    }
    locks_.clear();
    revision_ += 1;
    return make_result(OK, "Currency offer updated.");
}

// Capture committed trade state. The game saves its item records and backpacks alongside it.
Dictionary SpatialInventoryTrade::export_snapshot() {
    if (busy_) {
        return Dictionary();
    }
    busy_ = true;
    Dictionary snapshot = capture_snapshot();
    busy_ = false;
    return snapshot;
}

// Restore an idle, empty trade after the game has restored the backpacks and item records.

// Saved consent expires on restore. The registered offer and recovery models retain their identity.
Error SpatialInventoryTrade::import_snapshot(Dictionary snapshot) {
    if (busy_ || phase_ != IDLE || !reservations_.is_empty() || !recoveries_are_empty()) {
        return ERR_BUSY;
    }
    for (const Variant &raw_offer : offers_.values()) {
        const Ref<SpatialInventory> offer = Ref<SpatialInventory>(raw_offer);
        if (!offer->get_placements().is_empty()) {
            return ERR_BUSY;
        }
    }
    busy_ = true;
    Dictionary plan = plan_snapshot(snapshot);
    if (plan.is_empty()) {
        busy_ = false;
        return ERR_INVALID_DATA;
    }
    // Synchronous grid listeners should see the restored trade after the placement batch commits.
    int64_t previous_revision = revision_;
    phase_ = static_cast<Phase>(int64_t(plan["phase"]));
    initiator_ = static_cast<int64_t>(plan["initiator"]);
    receiver_ = static_cast<int64_t>(plan["receiver"]);
    revision_ = static_cast<int64_t>(plan["revision"]);
    currency_offers_ = Dictionary(plan["currency_offers"]);
    for (const Variant &raw_actor : Dictionary(plan["reservations"]).keys()) {
        const int64_t actor = static_cast<int64_t>(raw_actor);
        set_reservations(actor, Dictionary(), Dictionary(Dictionary(plan["reservations"])[actor]));
    }
    Error error = SpatialInventory::apply_placement_changes(plan["changes"]);
    if (error != OK) {
        phase_ = IDLE;
        initiator_ = 0;
        receiver_ = 0;
        revision_ = previous_revision;
        reservations_ = Dictionary();
        reserved_sources_ = Dictionary();
        provenance_sources_ = Dictionary();
        currency_offers_ = Dictionary();
    }
    busy_ = false;
    return error;
}

Dictionary SpatialInventoryTrade::request(int64_t actor) {
    Dictionary denied = check_actor(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (phase_ != IDLE) {
        return make_result(ERR_BUSY, "A trade request is already open.");
    }
    if (backpacks_.size() != 2) {
        return make_result(ERR_UNAVAILABLE, "A second participant is required.");
    }
    if (!recoveries_are_empty()) {
        return make_result(ERR_BUSY, "Claim trade returns before starting another trade.");
    }
    int64_t other = other_actor(actor);
    String refusal = participant_refusal(StringName("request"), actor, other);
    if (!refusal.is_empty()) {
        return make_result(ERR_UNAUTHORIZED, refusal);
    }
    initiator_ = actor;
    receiver_ = other;
    phase_ = REQUESTED;
    revision_ += 1;
    return make_result(OK, "Trade requested.");
}

Dictionary SpatialInventoryTrade::accept(int64_t actor) {
    Dictionary denied = check_actor(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (phase_ != REQUESTED || actor != receiver_) {
        return make_result(ERR_UNAUTHORIZED, "Only the receiver can accept the current request.");
    }
    if (!recoveries_are_empty()) {
        return make_result(ERR_BUSY, "Claim trade returns before opening the trade.");
    }
    String refusal = participant_refusal(StringName("accept"), actor, initiator_);
    if (!refusal.is_empty()) {
        return make_result(ERR_UNAUTHORIZED, refusal);
    }
    phase_ = ACTIVE;
    revision_ += 1;
    return make_result(OK, "Trade opened.");
}

Dictionary SpatialInventoryTrade::cancel(int64_t actor) {
    Dictionary denied = check_actor(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (phase_ == IDLE) {
        return make_result(ERR_UNAVAILABLE, "There is no open trade.");
    }
    TypedArray<Dictionary> changes;
    TypedArray<Dictionary> stack_changes;
    TypedArray<Dictionary> backing_records;
    for (const Variant &raw_owner : offers_.keys()) {
        const int64_t owner = static_cast<int64_t>(raw_owner);
        Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[owner]);
        Dictionary reserved = Dictionary(reservations_.get(owner, Dictionary()));
        if (offer->get_placements().size() != reserved.size()) {
            return make_result(ERR_BUSY, "An offer grid changed outside the trade.");
        }
        if (reserved.is_empty()) {
            continue;
        }
        Dictionary plan = plan_returns(owner, reserved.keys(), true);
        if (plan["error"] != Variant(OK)) {
            return make_result(static_cast<Error>(int64_t(plan["error"])), String(plan["reason"]));
        }
        changes.append_array(plan["changes"]);
        stack_changes.append_array(plan["stack_changes"]);
        backing_records.append_array(plan["backing_record_rows"]);
    }
    return settle(changes, false, stack_changes, backing_records);
}

// Add an entire placed item or stack to the offer at an exact cell or in the first available spot.
Dictionary SpatialInventoryTrade::offer_item(int64_t actor, StringName id, Vector2i origin, Variant rotated) {
    return offer_single(actor, id, 0, origin, rotated, StringName());
}

// Move the chosen units into escrow, leaving the unoffered remainder usable.
Dictionary SpatialInventoryTrade::offer_quantity(int64_t actor, StringName id, int64_t amount, Vector2i origin,
                                                 Variant rotated, StringName target_id) {
    if (amount <= 0) {
        return make_result(ERR_INVALID_PARAMETER, "Choose a positive stack amount.");
    }
    return offer_single(actor, id, amount, origin, rotated, target_id);
}

Dictionary SpatialInventoryTrade::offer_single(int64_t actor, StringName id, int64_t amount, Vector2i origin,
                                               Variant rotated, StringName target_id) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (rotated != Variant() && rotated.get_type() != Variant::BOOL) {
        return make_result(ERR_INVALID_PARAMETER, "Invalid offer rotation.");
    }
    if (is_reserved(actor, id)) {
        return make_result(ERR_ALREADY_EXISTS, "That item is already in your offer.");
    }
    Ref<SpatialInventory> backpack = Ref<SpatialInventory>(backpacks_[actor]);
    Dictionary source = backpack->get_placement(id);
    if (source.is_empty()) {
        return make_result(ERR_DOES_NOT_EXIST, "That item is outside your backpack.");
    }
    source.make_read_only();
    int64_t other = other_actor(actor);
    String refusal = participant_refusal(StringName("offer"), actor, other);
    if (!refusal.is_empty()) {
        return make_result(ERR_UNAUTHORIZED, refusal);
    }
    Ref<SpatialInventory> other_backpack = Ref<SpatialInventory>(backpacks_[other]);
    if (!other_backpack->get_placement(id).is_empty() || is_reserved(other, id)) {
        return make_result(ERR_ALREADY_EXISTS, "The item ID already exists for the other participant.");
    }
    Variant described = read_stack(id);
    if (described == Variant()) {
        return make_result(ERR_INVALID_DATA, "The source stack record is invalid.");
    }
    Dictionary stack = Dictionary(described);
    if (amount > 0 && (stack.is_empty() || amount > static_cast<int64_t>(stack["quantity"]))) {
        return make_result(ERR_INVALID_PARAMETER, "That amount is unavailable in the source stack.");
    }
    if (!stack.is_empty() && !can_edit_quantities()) {
        return make_result(ERR_UNCONFIGURED, "Configure the trade's stack hooks first.");
    }
    int64_t quantity = (amount > 0 ? amount : static_cast<int64_t>(stack.get("quantity", 0)));
    refusal = item_refusal(StringName("offer"), actor, other, source, stack, quantity);
    if (!refusal.is_empty()) {
        return make_result(ERR_UNAUTHORIZED, refusal);
    }
    if (!target_id.is_empty()) {
        return add_quantity_to_offer(actor, id, source, stack, amount, origin, rotated, target_id);
    }
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    Dictionary placement = offer_placement(offer, source, origin, rotated);
    if (placement.is_empty()) {
        return make_result(ERR_CANT_CREATE, "There is no space in your offer.");
    }
    StringName offer_id = id;
    if (!stack.is_empty()) {
        offer_id = new_stack_id(id);
        if (offer_id.is_empty()) {
            return make_result(ERR_CANT_CREATE, "Could not allocate a unique offer ID.");
        }
        placement = with_id(placement, offer_id);
    }
    Dictionary reservation = make_reservation(id, source, stack, quantity, placement);
    TypedArray<Dictionary> changes =
        TypedArray<Dictionary>(Array{placement_row(offer, offer_id, Dictionary(), placement)});
    TypedArray<Dictionary> stack_changes;
    TypedArray<Dictionary> backing_records;
    stage_debit(backpack, id, source, stack, quantity, changes, stack_changes, backing_records);
    if (!stack.is_empty()) {
        stack_changes.append(stack_row(offer_id, Dictionary(), Dictionary(reservation["stack"]), id));
    }
    return edit(actor, offer_id, reservation, changes, "Item offered.", stack_changes, backing_records);
}

Dictionary SpatialInventoryTrade::add_quantity_to_offer(int64_t actor, StringName id, Dictionary source,
                                                        Dictionary stack, int64_t amount, Vector2i origin,
                                                        Variant rotated, StringName target_id) {
    Dictionary target = get_reservation(actor, target_id);
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    bool use_rotation = static_cast<bool>((rotated == Variant() ? source["rotated"] : rotated));
    if (target.is_empty() || offer->get_swap_target(source["footprint"], origin, use_rotation) != target_id) {
        return make_result(ERR_BUSY, "The expected offer stack is no longer at that cell.");
    }
    Dictionary target_stack = Dictionary(target["stack"]);
    if (target_stack.get("key", Variant()) != stack["key"]) {
        return make_result(ERR_UNAVAILABLE, "Those offer stacks do not match.");
    }
    if (amount > static_cast<int64_t>(target_stack["max_quantity"]) - static_cast<int64_t>(target_stack["quantity"])) {
        return make_result(ERR_ALREADY_IN_USE, "That amount does not fit in the offer stack.");
    }
    if (!matches_reservation(actor, target_id, target)) {
        return make_result(ERR_BUSY, "The target offer stack changed.");
    }
    Dictionary combined = Dictionary(target["sources"]).duplicate();
    add_allocation(combined, id, make_dictionary({{"placement", source}, {"quantity", amount}}));
    Dictionary placement = Dictionary(target["offer"]);
    Dictionary updated = with_quantity(target_stack, static_cast<int64_t>(target_stack["quantity"]) + amount);
    TypedArray<Dictionary> changes =
        TypedArray<Dictionary>(Array{placement_row(offer, target_id, placement, placement)});
    TypedArray<Dictionary> stack_changes = TypedArray<Dictionary>(Array{stack_row(target_id, target_stack, updated)});
    TypedArray<Dictionary> backing_records;
    stage_debit(Ref<SpatialInventory>(backpacks_[actor]), id, source, stack, amount, changes, stack_changes,
                backing_records);
    return edit(actor, target_id, offer_record(placement, updated, combined), changes, "Stack quantity offered.",
                stack_changes, backing_records);
}

// Add whole instances in the caller's order. IDs that cannot be placed stay in the backpack.
Dictionary SpatialInventoryTrade::offer_items(int64_t actor, TypedArray<StringName> ids) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    Ref<SpatialInventory> backpack = Ref<SpatialInventory>(backpacks_[actor]);
    if (!valid_bulk_size(backpack, ids.size())) {
        return make_result(ERR_INVALID_PARAMETER, "Invalid number of items to offer.");
    }
    int64_t other = other_actor(actor);
    String refusal = participant_refusal(StringName("offer"), actor, other);
    if (!refusal.is_empty()) {
        return make_result(ERR_UNAUTHORIZED, refusal);
    }
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    Ref<SpatialInventory> shadow = new_grid();
    if (shadow->import_snapshot(offer->export_snapshot()) != OK) {
        return make_result(ERR_BUSY, "The offer changed before placement.");
    }
    Ref<SpatialInventory> other_backpack = Ref<SpatialInventory>(backpacks_[other]);
    Dictionary edits;
    Dictionary offered_sources;
    TypedArray<Dictionary> changes;
    TypedArray<Dictionary> stack_changes;
    TypedArray<Dictionary> backing_records;
    String first_item_refusal = "";
    for (const Variant &raw_id : ids) {
        const StringName id = StringName(raw_id);
        if (offered_sources.has(id) || is_reserved(actor, id) || is_reserved(other, id)) {
            continue;
        }
        Dictionary source = backpack->get_placement(id);
        if (source.is_empty() || !other_backpack->get_placement(id).is_empty()) {
            continue;
        }
        Variant described = read_stack(id);
        if (described == Variant()) {
            continue;
        }
        Dictionary stack = Dictionary(described);
        if (!stack.is_empty() && !can_edit_quantities()) {
            continue;
        }
        int64_t quantity = static_cast<int64_t>(stack.get("quantity", 0));
        refusal = item_refusal(StringName("offer"), actor, other, source, stack, quantity);
        if (!refusal.is_empty()) {
            if (first_item_refusal.is_empty()) {
                first_item_refusal = refusal;
            }
            continue;
        }
        Dictionary placement = offer_placement(shadow, source, FIRST_FIT, Variant());
        if (placement.is_empty()) {
            continue;
        }
        StringName offer_id = id;
        if (!stack.is_empty()) {
            offer_id = new_stack_id(id);
            if (offer_id.is_empty() || edits.has(offer_id)) {
                continue;
            }
            placement = with_id(placement, offer_id);
        }
        if (shadow->try_place(offer_id, placement["footprint"], placement["origin"], placement["rotated"]) != OK) {
            continue;
        }
        source.make_read_only();
        offered_sources[id] = true;
        Dictionary reservation = make_reservation(id, source, stack, quantity, placement);
        edits[offer_id] = reservation;
        stage_debit(backpack, id, source, stack, quantity, changes, stack_changes, backing_records);
        changes.append(placement_row(offer, offer_id, Dictionary(), placement));
        if (!stack.is_empty()) {
            stack_changes.append(stack_row(offer_id, Dictionary(), Dictionary(reservation["stack"]), id));
        }
    }
    if (edits.is_empty()) {
        if (!first_item_refusal.is_empty()) {
            return make_result(ERR_UNAUTHORIZED, first_item_refusal);
        }
        return make_result(ERR_CANT_CREATE, "None of the selected items fit in your offer.");
    }
    return edit_offers(actor, edits, changes, String("Items offered: ") + String::num_int64(edits.size()) + ".",
                       stack_changes, backing_records);
}

Dictionary SpatialInventoryTrade::move_offer(int64_t actor, StringName id, Vector2i origin, bool rotated) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    Dictionary reserved = get_reservation(actor, id);
    if (reserved.is_empty()) {
        return make_result(ERR_DOES_NOT_EXIST, "That item is outside your offer.");
    }
    Dictionary before = Dictionary(reserved["offer"]);
    Dictionary after = at(before, origin, rotated);
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    if (!matches_reservation(actor, id, reserved)) {
        return make_result(ERR_BUSY, "The item or its offer changed.");
    }
    if (before == after) {
        return make_result(OK, "Offer placement already matches.");
    }
    Dictionary updated = reserved.duplicate();
    updated["offer"] = after;
    return edit(actor, id, updated, TypedArray<Dictionary>(Array{placement_row(offer, id, before, after)}),
                "Offer rearranged.");
}

// Withdraw the projection. A specified target requires a full compatible merge at the exact drop cell.
Dictionary SpatialInventoryTrade::withdraw(int64_t actor, StringName id, Vector2i origin, Variant rotated,
                                           StringName target_id) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (rotated != Variant() && rotated.get_type() != Variant::BOOL) {
        return make_result(ERR_INVALID_PARAMETER, "Invalid return rotation.");
    }
    if (get_reservation(actor, id).is_empty()) {
        return make_result(ERR_DOES_NOT_EXIST, "That item is outside your offer.");
    }
    if (origin == FIRST_FIT && !target_id.is_empty()) {
        return make_result(ERR_INVALID_PARAMETER, "Choose an exact return cell for the target stack.");
    }
    Dictionary plan = plan_returns(actor, Array{id}, false, origin, rotated, target_id);
    if (plan["error"] != Variant(OK)) {
        return make_result(static_cast<Error>(int64_t(plan["error"])), String(plan["reason"]));
    }
    return edit(actor, id, Dictionary(), TypedArray<Dictionary>(plan["changes"]), "Item withdrawn.",
                TypedArray<Dictionary>(plan["stack_changes"]), TypedArray<Dictionary>(plan["backing_record_rows"]));
}

// Consolidate two owned escrow stacks, retaining the source projection when some units remain.
Dictionary SpatialInventoryTrade::merge_offer_stacks(int64_t actor, StringName source_id, StringName target_id,
                                                     int64_t limit) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (source_id == target_id || limit < 0) {
        return make_result(ERR_INVALID_PARAMETER, "Choose two different offer stacks.");
    }
    if (!can_edit_quantities()) {
        return make_result(ERR_UNCONFIGURED, "Configure the trade's stack hooks first.");
    }
    Dictionary source = get_reservation(actor, source_id);
    Dictionary target = get_reservation(actor, target_id);
    if (source.is_empty() || target.is_empty()) {
        return make_result(ERR_DOES_NOT_EXIST, "Both stacks must be in your offer.");
    }
    Dictionary source_stack = Dictionary(source["stack"]);
    Dictionary target_stack = Dictionary(target["stack"]);
    if (source_stack.is_empty() || source_stack.get("key", Variant()) != target_stack.get("key", Variant())) {
        return make_result(ERR_UNAVAILABLE, "Those offer stacks do not match.");
    }
    if (!matches_reservation(actor, source_id, source) || !matches_reservation(actor, target_id, target)) {
        return make_result(ERR_BUSY, "An offer stack changed.");
    }
    int64_t capacity =
        static_cast<int64_t>(target_stack["max_quantity"]) - static_cast<int64_t>(target_stack["quantity"]);
    if (capacity <= 0) {
        return make_result(ERR_ALREADY_IN_USE, "The target offer stack is full.");
    }
    int64_t moved = std::min<int64_t>(static_cast<int64_t>(source_stack["quantity"]), capacity);
    if (limit > 0) {
        moved = std::min<int64_t>(moved, limit);
    }
    Dictionary source_sources = Dictionary(source["sources"]).duplicate();
    Dictionary target_sources = Dictionary(target["sources"]).duplicate();
    int64_t remaining = moved;
    for (const Variant &raw_id : source_sources.keys()) {
        const StringName id = StringName(raw_id);
        Dictionary allocation = Dictionary(source_sources[id]);
        int64_t units = std::min<int64_t>(remaining, static_cast<int64_t>(allocation["quantity"]));
        add_allocation(target_sources, id,
                       make_dictionary({{"placement", allocation["placement"]}, {"quantity", units}}));
        if (units == static_cast<int64_t>(allocation["quantity"])) {
            source_sources.erase(id);
        } else {
            source_sources[id] = make_dictionary({{"placement", allocation["placement"]},
                                                  {"quantity", static_cast<int64_t>(allocation["quantity"]) - units}});
        }
        remaining -= units;
        if (remaining == 0) {
            break;
        }
    }
    Dictionary source_placement = Dictionary(source["offer"]);
    Dictionary target_placement = Dictionary(target["offer"]);
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    Dictionary next_source = with_quantity(source_stack, static_cast<int64_t>(source_stack["quantity"]) - moved);
    Dictionary next_target = with_quantity(target_stack, static_cast<int64_t>(target_stack["quantity"]) + moved);
    Dictionary edits = make_dictionary(
        {{source_id,
          (next_source.is_empty() ? Dictionary() : offer_record(source_placement, next_source, source_sources))},
         {target_id, offer_record(target_placement, next_target, target_sources)}});
    return edit_offers(
        actor, edits,
        TypedArray<Dictionary>(Array{placement_row(offer, source_id, source_placement,
                                                   (next_source.is_empty() ? Dictionary() : source_placement)),
                                     placement_row(offer, target_id, target_placement, target_placement)}),
        String("Merged ") + String::num_int64(moved) + " offered units.",
        TypedArray<Dictionary>(
            Array{stack_row(source_id, source_stack, next_source), stack_row(target_id, target_stack, next_target)}));
}

// Withdraw matching projections together. If any cannot fit, leave the entire set in escrow.
Dictionary SpatialInventoryTrade::withdraw_items(int64_t actor, TypedArray<StringName> ids) {
    Dictionary denied = check_edit(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    if (!valid_bulk_size(offer, ids.size())) {
        return make_result(ERR_INVALID_PARAMETER, "Invalid number of items to withdraw.");
    }
    Dictionary edits;
    for (const Variant &raw_id : ids) {
        const StringName id = StringName(raw_id);
        if (!get_reservation(actor, id).is_empty()) {
            edits[id] = Dictionary();
        }
    }
    if (edits.is_empty()) {
        return make_result(ERR_DOES_NOT_EXIST, "None of the selected items are in your offer.");
    }
    Dictionary plan = plan_returns(actor, edits.keys(), false);
    if (plan["error"] != Variant(OK)) {
        return make_result(static_cast<Error>(int64_t(plan["error"])), String(plan["reason"]));
    }
    return edit_offers(actor, edits, TypedArray<Dictionary>(plan["changes"]),
                       String("Items withdrawn: ") + String::num_int64(edits.size()) + ".",
                       TypedArray<Dictionary>(plan["stack_changes"]),
                       TypedArray<Dictionary>(plan["backing_record_rows"]));
}

Dictionary SpatialInventoryTrade::set_lock(int64_t actor, bool locked) {
    Dictionary denied = check_actor(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (phase_ != ACTIVE) {
        return make_result(ERR_UNAVAILABLE, "Accept a trade request first.");
    }
    if (!locked) {
        locks_.erase(actor);
        return make_result(OK, "Offer unlocked.");
    }
    if (reservations_.is_empty() && currency_offers_.is_empty()) {
        return make_result(ERR_INVALID_PARAMETER, "Offer an item or currency before locking the trade.");
    }
    if (locks_.has(actor)) {
        return make_result(OK, "Offer already locked.");
    }
    if (locks_.has(other_actor(actor))) {
        return complete_trade();
    }
    locks_[actor] = true;
    return make_result(OK, "Offer locked.");
}

Dictionary SpatialInventoryTrade::complete_trade() {
    String refusal = participant_refusal(StringName("settle"), initiator_, receiver_);
    if (refusal.is_empty()) {
        refusal = participant_refusal(StringName("settle"), receiver_, initiator_);
    }
    if (!refusal.is_empty()) {
        return failed_exchange(ERR_UNAUTHORIZED, refusal);
    }
    Dictionary currency_plan;
    if (!currency_offers_.is_empty()) {
        currency_plan = plan_currency_exchange();
        if (currency_plan["error"] != Variant(OK)) {
            return failed_exchange(static_cast<Error>(int64_t(currency_plan["error"])),
                                   String(currency_plan["reason"]));
        }
    }
    TypedArray<Dictionary> changes;
    TypedArray<Dictionary> stack_changes;
    TypedArray<Dictionary> backing_records;
    TypedArray<Dictionary> transfers;
    if (reservations_.is_empty()) {
        if (!(Ref<SpatialInventory>(offers_[initiator_])->get_placements().is_empty()) ||
            !(Ref<SpatialInventory>(offers_[receiver_])->get_placements().is_empty())) {
            return failed_exchange(ERR_BUSY, "An offer grid changed outside the trade.");
        }
    } else {
        Dictionary first = plan_outgoing(initiator_, changes);
        if (first.has("denial")) {
            return failed_exchange(ERR_UNAUTHORIZED, String(first["denial"]));
        }
        Dictionary second = plan_outgoing(receiver_, changes);
        if (second.has("denial")) {
            return failed_exchange(ERR_UNAUTHORIZED, String(second["denial"]));
        }
        if (first.is_empty() || second.is_empty()) {
            return failed_exchange(ERR_BUSY, "An escrow stack or reserved item changed.");
        }
        Dictionary plan = make_dictionary({{"changes", changes},
                                           {"stack_changes", stack_changes},
                                           {"backing_records", backing_records},
                                           {"transfers", transfers}});
        if (!plan_incoming(first, second, plan) || !plan_incoming(second, first, plan)) {
            return failed_exchange(ERR_CANT_CREATE, "An incoming item does not fit. Make room and lock again.");
        }
    }
    return settle(changes, true, stack_changes, backing_records, transfers, currency_plan);
}

Dictionary SpatialInventoryTrade::plan_currency_exchange() {
    TypedArray<Dictionary> changes;
    TypedArray<Dictionary> transfers;
    if (wallet_.is_null()) {
        return make_dictionary({{"error", ERR_UNCONFIGURED}, {"reason", "Configure a trade wallet first."}});
    }
    Dictionary first = Dictionary(currency_offers_.get(initiator_, Dictionary()));
    Dictionary second = Dictionary(currency_offers_.get(receiver_, Dictionary()));
    Dictionary currencies = first.duplicate();
    currencies.merge(second);
    for (const Variant &raw_currency : currencies.keys()) {
        const StringName currency = StringName(raw_currency);
        int64_t first_amount = static_cast<int64_t>(first.get(currency, 0));
        int64_t second_amount = static_cast<int64_t>(second.get(currency, 0));
        int64_t first_balance = read_balance(initiator_, currency);
        int64_t second_balance = read_balance(receiver_, currency);
        if (first_balance < first_amount || second_balance < second_amount) {
            return make_dictionary(
                {{"error", ERR_UNAUTHORIZED}, {"reason", "A participant can no longer afford their currency offer."}});
        }
        int64_t first_remainder = first_balance - first_amount;
        int64_t second_remainder = second_balance - second_amount;
        if (first_remainder > MAX_BALANCE - second_amount || second_remainder > MAX_BALANCE - first_amount) {
            return make_dictionary(
                {{"error", ERR_PARAMETER_RANGE_ERROR}, {"reason", "The receiving wallet cannot hold that amount."}});
        }
        changes.append(make_dictionary({{"actor", initiator_},
                                        {"currency", currency},
                                        {"before", first_balance},
                                        {"after", first_remainder + second_amount}}));
        changes.append(make_dictionary({{"actor", receiver_},
                                        {"currency", currency},
                                        {"before", second_balance},
                                        {"after", second_remainder + first_amount}}));
        if (first_amount > 0) {
            transfers.append(currency_transfer(initiator_, receiver_, currency, first_amount));
        }
        if (second_amount > 0) {
            transfers.append(currency_transfer(receiver_, initiator_, currency, second_amount));
        }
    }
    return make_dictionary({{"error", OK}, {"changes", changes}, {"transfers", transfers}});
}

Dictionary SpatialInventoryTrade::currency_transfer(int64_t from_actor, int64_t to_actor, StringName currency,
                                                    int64_t amount) {
    return make_dictionary(
        {{"from_actor", from_actor}, {"to_actor", to_actor}, {"currency", currency}, {"amount", amount}});
}

Dictionary SpatialInventoryTrade::plan_outgoing(int64_t actor, TypedArray<Dictionary> changes) {
    Ref<SpatialInventory> backpack = Ref<SpatialInventory>(backpacks_[actor]);
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    Dictionary reserved = Dictionary(reservations_.get(actor, Dictionary()));
    Array offered = offer->get_placements();
    if (offered.size() != reserved.size()) {
        return Dictionary();
    }
    Ref<SpatialInventory> shadow = new_grid();
    if (shadow->import_snapshot(backpack->export_snapshot()) != OK) {
        return Dictionary();
    }
    TypedArray<Dictionary> outgoing;
    bool has_stacks = false;
    int64_t other = other_actor(actor);
    for (const Variant &raw_placement : offered) {
        const Dictionary placement = Dictionary(raw_placement);
        StringName id = StringName(placement["id"]);
        Dictionary entry = Dictionary(reserved.get(id, Dictionary()));
        if (entry.is_empty() || !matches_reservation(actor, id, entry)) {
            return Dictionary();
        }
        Dictionary stack = Dictionary(entry["stack"]);
        Dictionary source =
            (stack.is_empty() ? Dictionary(Dictionary(Dictionary(entry["sources"])[id])["placement"]) : placement);
        String refusal = item_refusal(StringName("settle"), actor, other, source, stack,
                                      static_cast<int64_t>(stack.get("quantity", 0)));
        if (!refusal.is_empty()) {
            return make_dictionary({{"denial", refusal}});
        }
        if (stack.is_empty()) {
            if (shadow->remove(id) != OK) {
                return Dictionary();
            }
            changes.append(placement_row(backpack, id, source, Dictionary()));
        } else {
            has_stacks = true;
        }
        outgoing.append(entry);
        changes.append(placement_row(offer, id, placement, Dictionary()));
    }
    return make_dictionary({{"actor", actor},
                            {"backpack", backpack},
                            {"shadow", shadow},
                            {"outgoing", outgoing},
                            {"has_stacks", has_stacks}});
}

// Return planning shares one backpack snapshot for the entire operation, including bulk withdrawal.
Dictionary SpatialInventoryTrade::plan_returns(int64_t actor, Array ids, bool allow_recovery, Vector2i origin,
                                               Variant rotated, StringName target_id) {
    Ref<SpatialInventory> backpack = Ref<SpatialInventory>(backpacks_[actor]);
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    Ref<SpatialInventory> recovery = Ref<SpatialInventory>(recoveries_[actor]);
    Ref<SpatialInventory> shadow = new_grid();
    Dictionary state = make_dictionary(
        {{"backpack", backpack}, {"shadow", shadow}, {"records", Dictionary()}, {"checks", Dictionary()}});
    TypedArray<Dictionary> changes;
    if (shadow->import_snapshot(backpack->export_snapshot()) != OK) {
        return make_result(ERR_BUSY, "The backpack changed before the return.");
    }
    for (const Variant &raw_id : ids) {
        const StringName id = StringName(raw_id);
        Dictionary entry = get_reservation(actor, id);
        if (entry.is_empty() || Variant(offer->get_placement(id)) != entry["offer"]) {
            return make_result(ERR_BUSY, "An offer placement changed.");
        }
        Dictionary placement = Dictionary(entry["offer"]);
        Dictionary stack = Dictionary(entry["stack"]);
        bool use_rotation = static_cast<bool>((rotated == Variant() ? placement["rotated"] : rotated));
        changes.append(placement_row(offer, id, placement, Dictionary()));
        if (stack.is_empty()) {
            Dictionary source = Dictionary(Dictionary(Dictionary(entry["sources"])[id])["placement"]);
            if (!target_id.is_empty()) {
                return make_result(ERR_UNAVAILABLE, "Only matching stacks can merge.");
            }
            if (origin != FIRST_FIT) {
                if (!matches_reservation(actor, id, entry)) {
                    return make_result(ERR_BUSY, "The reserved item's placement changed.");
                }
                changes.append(placement_row(backpack, id, source, at(source, origin, use_rotation)));
            }
            // An automatic release removes only the projection. It cannot recreate a missing backing item.
            continue;
        }
        if (!matches_reservation(actor, id, entry)) {
            return make_result(ERR_BUSY, "An escrow stack changed.");
        }
        int64_t remaining = static_cast<int64_t>(stack["quantity"]);
        if (!target_id.is_empty()) {
            if (shadow->get_swap_target(placement["footprint"], origin, use_rotation) != target_id) {
                return make_result(ERR_BUSY, "The expected return stack is no longer at that cell.");
            }
            if (plan_return_merge(actor, state, target_id, stack, remaining, true) != remaining) {
                return make_result(ERR_UNAVAILABLE, "The offered units do not fit in that stack.");
            }
            remaining = 0;
        } else if (origin == FIRST_FIT) {
            for (const Variant &raw_source_id : Dictionary(entry["sources"]).keys()) {
                const StringName source_id = StringName(raw_source_id);
                Dictionary contribution = Dictionary(Dictionary(entry["sources"])[source_id]);
                remaining -= plan_return_merge(actor, state, source_id, stack,
                                               static_cast<int64_t>(contribution["quantity"]), false);
            }
        }
        if (remaining == 0) {
            Dictionary{state["records"]}[id] = stack_row(id, stack, Dictionary());
            continue;
        }
        Ref<SpatialInventory> destination = backpack;
        Vector2i return_origin = origin;
        if (origin == FIRST_FIT) {
            Dictionary fit = find_return_cell(shadow, entry, use_rotation);
            return_origin = Vector2i(fit["origin"]);
            use_rotation = static_cast<bool>(fit["rotated"]);
        }
        if (return_origin == FIRST_FIT) {
            if (!allow_recovery) {
                return make_result(ERR_CANT_CREATE, "Make room in your backpack before withdrawing.");
            }
            // The offer's non-overlapping geometry is also valid in the empty, same-size recovery.
            destination = recovery;
            return_origin = Vector2i(placement["origin"]);
            use_rotation = static_cast<bool>(placement["rotated"]);
        }
        StringName returned_id = return_id(entry, id);
        if (!shadow->get_placement(returned_id).is_empty()) {
            returned_id = id;
        }
        Dictionary returned = at(with_id(placement, returned_id), return_origin, use_rotation);
        if (destination == backpack &&
            shadow->try_place(returned_id, returned["footprint"], return_origin, use_rotation) != OK) {
            return make_result(ERR_CANT_CREATE, "The offered item does not fit in that cell.");
        }
        changes.append(placement_row(destination, returned_id, Dictionary(), returned));
        Dictionary remainder = with_quantity(stack, remaining);
        if (returned_id == id) {
            Dictionary{state["records"]}[id] = stack_row(id, stack, remainder);
        } else {
            Dictionary{state["records"]}[id] = stack_row(id, stack, Dictionary());
            Dictionary{state["records"]}[returned_id] = stack_row(returned_id, Dictionary(), remainder, id);
        }
    }
    TypedArray<Dictionary> stack_changes;
    stack_changes.assign(Dictionary(state["records"]).values());
    TypedArray<Dictionary> backing_records;
    for (const Variant &raw_checked_id : Dictionary(state["checks"]).keys()) {
        const StringName checked_id = StringName(raw_checked_id);
        Dictionary expected = Dictionary(Dictionary(state["checks"])[checked_id]);
        changes.append(placement_row(backpack, checked_id, expected, expected));
        backing_records.append(make_dictionary({{"inventory", backpack}, {"id", checked_id}}));
    }
    Dictionary result = make_result(OK, "", changes);
    result["stack_changes"] = stack_changes;
    result["backing_record_rows"] = backing_records;
    return result;
}

int64_t SpatialInventoryTrade::plan_return_merge(int64_t actor, Dictionary state, StringName id, Dictionary stack,
                                                 int64_t amount, bool require_all) {
    Ref<SpatialInventory> shadow = Ref<SpatialInventory>(state["shadow"]);
    if (is_reserved(actor, id) || shadow->get_placement(id).is_empty()) {
        return 0;
    }
    Dictionary records = Dictionary(state["records"]);
    Dictionary row = Dictionary(records.get(id, Dictionary()));
    Variant current = (row.is_empty() ? read_stack(id) : row["after"]);
    if (current == Variant() || Dictionary(current).get("key", Variant()) != stack["key"]) {
        return 0;
    }
    int64_t moved = std::min<int64_t>(amount, static_cast<int64_t>(Dictionary(current)["max_quantity"]) -
                                                  static_cast<int64_t>(Dictionary(current)["quantity"]));
    if (moved <= 0 || (require_all && moved != amount)) {
        return 0;
    }
    if (row.is_empty()) {
        Ref<SpatialInventory> backpack = Ref<SpatialInventory>(state["backpack"]);
        Dictionary{state["checks"]}[id] = backpack->get_placement(id);
        row = stack_row(id, Dictionary(current), Dictionary());
    }
    row["after"] = with_quantity(Dictionary(current), static_cast<int64_t>(Dictionary(current)["quantity"]) + moved);
    records[id] = row;
    return moved;
}

Dictionary SpatialInventoryTrade::find_return_cell(Ref<SpatialInventory> shadow, Dictionary entry, bool rotated) {
    Vector2i footprint = Vector2i(Dictionary(entry["offer"])["footprint"]);
    for (const Variant &raw_allocation : Dictionary(entry["sources"]).values()) {
        const Dictionary allocation = Dictionary(raw_allocation);
        Dictionary original = Dictionary(allocation["placement"]);
        if (shadow->check_placement(footprint, original["origin"], original["rotated"]) == OK) {
            return make_dictionary({{"origin", original["origin"]}, {"rotated", original["rotated"]}});
        }
    }
    Vector2i origin = shadow->find_first_fit(footprint, rotated);
    if (origin == FIRST_FIT && footprint.x != footprint.y) {
        rotated = !rotated;
        origin = shadow->find_first_fit(footprint, rotated);
    }
    return make_dictionary({{"origin", origin}, {"rotated", rotated}});
}

StringName SpatialInventoryTrade::return_id(Dictionary entry, StringName escrow_id) {
    StringName whole_id = StringName(entry["whole_id"]);
    return (!whole_id.is_empty() && id_is_unused(whole_id) ? whole_id : escrow_id);
}

bool SpatialInventoryTrade::plan_incoming(Dictionary source, Dictionary destination, Dictionary plan) {
    Ref<SpatialInventory> backpack = Ref<SpatialInventory>(destination["backpack"]);
    Ref<SpatialInventory> shadow = Ref<SpatialInventory>(destination["shadow"]);
    TypedArray<Dictionary> changes = TypedArray<Dictionary>(plan["changes"]);
    Dictionary receivers = (static_cast<bool>(source["has_stacks"]) ? settlement_receivers(shadow) : Dictionary());
    for (const Variant &raw_entry : Array(source["outgoing"])) {
        const Dictionary entry = Dictionary(raw_entry);
        Dictionary offered = Dictionary(entry["offer"]);
        StringName source_id = StringName(offered["id"]);
        Dictionary stack = Dictionary(entry["stack"]);
        if (stack.is_empty()) {
            if (!place_incoming(backpack, shadow, offered, changes)) {
                return false;
            }
            Array(plan["transfers"]).append(settlement_transfer(source, destination, source_id, source_id, 1));
            continue;
        }
        StringName key = StringName(stack["key"]);
        Dictionary group = Dictionary(receivers.get(key, make_dictionary({{"stacks", Array()}, {"next", 0}})));
        receivers[key] = group;
        int64_t remaining = plan_merge_incoming(source, destination, entry, group, plan);
        if (remaining == 0) {
            Array(plan["stack_changes"]).append(stack_row(source_id, stack, Dictionary()));
            continue;
        }
        StringName destination_id = return_id(entry, source_id);
        Dictionary placed = (destination_id != source_id ? with_id(offered, destination_id) : offered);
        if (!place_incoming(backpack, shadow, placed, changes)) {
            return false;
        }
        Dictionary delivered = with_quantity(stack, remaining);
        Dictionary record = stack_row(source_id, stack, delivered);
        if (destination_id != source_id) {
            Array(plan["stack_changes"]).append(stack_row(source_id, stack, Dictionary()));
            record = stack_row(destination_id, Dictionary(), delivered, source_id);
        }
        Array(plan["stack_changes"]).append(record);
        Array(plan["transfers"]).append(settlement_transfer(source, destination, source_id, destination_id, remaining));
        if (remaining < static_cast<int64_t>(delivered["max_quantity"])) {
            Array(group["stacks"])
                .append(make_dictionary({{"id", destination_id}, {"stack", delivered}, {"change", record}}));
        }
    }
    return true;
}

int64_t SpatialInventoryTrade::plan_merge_incoming(Dictionary source, Dictionary destination, Dictionary entry,
                                                   Dictionary group, Dictionary plan) {
    int64_t remaining = static_cast<int64_t>(Dictionary(entry["stack"])["quantity"]);
    StringName source_id = StringName(Dictionary(entry["offer"])["id"]);
    Array targets = Array(group["stacks"]);
    int64_t index = static_cast<int64_t>(group["next"]);
    while (remaining > 0 && index < targets.size()) {
        Dictionary target = Dictionary(targets[index]);
        StringName target_id = StringName(target["id"]);
        Dictionary stack = Dictionary(target["stack"]);
        int64_t capacity = static_cast<int64_t>(stack["max_quantity"]) - static_cast<int64_t>(stack["quantity"]);
        int64_t moved = std::min<int64_t>(remaining, capacity);
        Dictionary updated = with_quantity(stack, static_cast<int64_t>(stack["quantity"]) + moved);
        Dictionary change = Dictionary(target["change"]);
        if (change.is_empty()) {
            // Verify that a quantity-only receiver still occupies its original cells at commit.
            Dictionary placement = Dictionary(target["placement"]);
            Array(plan["changes"])
                .append(placement_row(Ref<SpatialInventory>(destination["backpack"]), target_id, placement, placement));
            Array(plan["changes"])
                .append(
                    placement_row(Ref<SpatialInventory>(source["backpack"]), target_id, Dictionary(), Dictionary()));
            Array(plan["backing_records"])
                .append(make_dictionary({{"inventory", destination["backpack"]}, {"id", target_id}}));
            change = stack_row(target_id, stack, updated);
            Array(plan["stack_changes"]).append(change);
            target["change"] = change;
        } else {
            change["after"] = updated;
        }
        target["stack"] = updated;
        Array(plan["transfers"]).append(settlement_transfer(source, destination, source_id, target_id, moved));
        remaining -= moved;
        if (moved == capacity) {
            index += 1;
        }
    }
    group["next"] = index;
    return remaining;
}

bool SpatialInventoryTrade::place_incoming(Ref<SpatialInventory> backpack, Ref<SpatialInventory> shadow,
                                           Dictionary offered, TypedArray<Dictionary> changes) {
    Vector2i footprint = Vector2i(offered["footprint"]);
    bool rotated = static_cast<bool>(offered["rotated"]);
    Vector2i origin = shadow->find_first_fit(footprint, rotated);
    if (origin == FIRST_FIT && footprint.x != footprint.y) {
        rotated = !rotated;
        origin = shadow->find_first_fit(footprint, rotated);
    }
    StringName id = StringName(offered["id"]);
    if (origin == FIRST_FIT || shadow->try_place(id, footprint, origin, rotated) != OK) {
        return false;
    }
    changes.append(placement_row(backpack, id, Dictionary(), at(offered, origin, rotated)));
    return true;
}

Dictionary SpatialInventoryTrade::settlement_transfer(Dictionary source, Dictionary destination, StringName source_id,
                                                      StringName destination_id, int64_t quantity) {
    return make_dictionary({{"from_actor", source["actor"]},
                            {"to_actor", destination["actor"]},
                            {"source_id", source_id},
                            {"destination_id", destination_id},
                            {"quantity", quantity}});
}

Dictionary SpatialInventoryTrade::capture_snapshot() {
    Dictionary participants;
    Dictionary return_ids;
    for (const Variant &raw_actor : backpacks_.keys()) {
        const int64_t actor = static_cast<int64_t>(raw_actor);
        Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
        Ref<SpatialInventory> recovery = Ref<SpatialInventory>(recoveries_[actor]);
        Vector2i size = offer->get_grid_size();
        Dictionary records = Dictionary(reservations_.get(actor, Dictionary()));
        if (offer->get_placements().size() != records.size() || recovery->get_grid_size() != size) {
            return Dictionary();
        }
        TypedArray<Dictionary> offers;
        for (const Variant &raw_id : records.keys()) {
            const StringName id = StringName(raw_id);
            Dictionary entry = Dictionary(records[id]);
            if ((!Dictionary(entry["stack"]).is_empty() && !can_edit_quantities()) ||
                !matches_reservation(actor, id, entry)) {
                return Dictionary();
            }
            offers.append(entry.duplicate(true));
        }
        TypedArray<Dictionary> returns;
        for (const Variant &raw_placement : recovery->get_placements()) {
            const Dictionary placement = Dictionary(raw_placement);
            StringName id = StringName(placement["id"]);
            Variant stack = read_stack(id);
            if ((phase_ != IDLE || !can_edit_quantities() || stack == Variant() || Dictionary(stack).is_empty() ||
                 return_ids.has(id))) {
                return Dictionary();
            }
            for (const Variant &raw_participant : backpacks_.keys()) {
                const int64_t participant = static_cast<int64_t>(raw_participant);
                if (has_placement(Ref<SpatialInventory>(backpacks_[participant]), id) ||
                    has_placement(Ref<SpatialInventory>(offers_[participant]), id)) {
                    return Dictionary();
                }
            }
            return_ids[id] = true;
            returns.append(make_dictionary({{"placement", placement}, {"stack", Dictionary(stack).duplicate()}}));
        }
        participants[actor] = make_dictionary(
            {{"size", size}, {"offers", offers}, {"returns", returns}, {"currencies", get_currency_offers(actor)}});
    }
    return make_dictionary({{"version", SNAPSHOT_VERSION},
                            {"phase", phase_},
                            {"initiator", initiator_},
                            {"receiver", receiver_},
                            {"revision", revision_},
                            {"locked", (!locks_.is_empty() ? static_cast<int64_t>(locks_.keys()[0]) : 0)},
                            {"participants", participants}});
}

Dictionary SpatialInventoryTrade::plan_snapshot(Dictionary snapshot) {
    if (!valid_snapshot_header(snapshot)) {
        return Dictionary();
    }
    int64_t phase_value = static_cast<int64_t>(snapshot["phase"]);
    int64_t locked = static_cast<int64_t>(snapshot["locked"]);
    Dictionary participants = Dictionary(snapshot["participants"]);
    TypedArray<Dictionary> changes;
    Dictionary reservations;
    Dictionary currency_offers;
    Dictionary plan =
        make_dictionary({{"phase", phase_value},
                         {"initiator", snapshot["initiator"]},
                         {"receiver", snapshot["receiver"]},
                         {"revision", std::max<int64_t>(revision_, static_cast<int64_t>(snapshot["revision"])) + 1},
                         {"reservations", reservations},
                         {"changes", changes},
                         {"currency_offers", currency_offers}});
    Dictionary placement_ids;
    Dictionary stack_source_ids;
    Dictionary whole_ids;
    int64_t offer_count = 0;
    bool has_currencies = snapshot["version"] == Variant(SNAPSHOT_VERSION);
    for (const Variant &actor : participants.keys()) {
        if (actor.get_type() != Variant::INT || !backpacks_.has(actor)) {
            return Dictionary();
        }
        Variant entry = participants[actor];
        Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
        Ref<SpatialInventory> recovery = Ref<SpatialInventory>(recoveries_[actor]);
        Vector2i size = offer->get_grid_size();
        if ((entry.get_type() != Variant::DICTIONARY || Dictionary(entry).size() != ((has_currencies ? 4 : 3)) ||
             Dictionary(entry).get("size", Variant()).get_type() != Variant::VECTOR2I ||
             Dictionary(entry)["size"] != Variant(size) || recovery->get_grid_size() != size)) {
            return Dictionary();
        }
        Variant offers = Dictionary(entry).get("offers", Variant());
        Variant returns = Dictionary(entry).get("returns", Variant());
        if ((offers.get_type() != Variant::ARRAY || returns.get_type() != Variant::ARRAY ||
             Array(offers).size() + Array(returns).size() > size.x * size.y ||
             (phase_value != ACTIVE && !Array(offers).is_empty()) ||
             (phase_value != IDLE && !Array(returns).is_empty()))) {
            return Dictionary();
        }
        if (has_currencies) {
            Variant currencies = snapshot_currencies(Dictionary(entry).get("currencies", Variant()));
            if (currencies == Variant()) {
                return Dictionary();
            }
            if (!Dictionary(currencies).is_empty()) {
                if (phase_value != ACTIVE || wallet_.is_null()) {
                    return Dictionary();
                }
                currency_offers[actor] = currencies;
            }
        }
        Dictionary actor_records;
        for (const Variant &raw : Array(offers)) {
            Dictionary record = snapshot_offer(raw);
            if (record.is_empty()) {
                return Dictionary();
            }
            Dictionary placement = Dictionary(record["offer"]);
            StringName id = StringName(placement["id"]);
            Dictionary stack = Dictionary(record["stack"]);
            bool gear = stack.is_empty();
            StringName whole_id = StringName(record["whole_id"]);
            if (placement_ids.has(id) || (!whole_id.is_empty() && whole_ids.has(whole_id))) {
                return Dictionary();
            }
            Variant live_stack = read_stack(id);
            if (live_stack.get_type() != Variant::DICTIONARY || live_stack != Variant(stack) ||
                (!gear && !can_edit_quantities())) {
                return Dictionary();
            }
            Dictionary expected =
                (gear ? Dictionary(Dictionary(Dictionary(record["sources"])[id])["placement"]) : Dictionary());
            if (!(check_snapshot_backpacks(changes, id, static_cast<int64_t>(actor), expected))) {
                return Dictionary();
            }
            if (!gear) {
                for (const Variant &raw_source_id : Dictionary(record["sources"]).keys()) {
                    const StringName source_id = StringName(raw_source_id);
                    stack_source_ids[source_id] = true;
                }
            }
            if (!whole_id.is_empty()) {
                whole_ids[whole_id] = true;
            }
            placement_ids[id] = !gear;
            changes.append(placement_row(offer, id, Dictionary(), placement));
            actor_records[id] = record;
        }
        for (const Variant &raw : Array(returns)) {
            if (raw.get_type() != Variant::DICTIONARY || Dictionary(raw).size() != 2) {
                return Dictionary();
            }
            Dictionary placement = snapshot_placement(Dictionary(raw).get("placement", Variant()));
            Variant stack = snapshot_stack(Dictionary(raw).get("stack", Variant()));
            if (placement.is_empty() || stack == Variant() || Dictionary(stack).is_empty() || !can_edit_quantities()) {
                return Dictionary();
            }
            StringName id = StringName(placement["id"]);
            Variant live_stack = read_stack(id);
            if ((placement_ids.has(id) || live_stack.get_type() != Variant::DICTIONARY || live_stack != stack ||
                 !(check_snapshot_backpacks(changes, id, static_cast<int64_t>(actor), Dictionary())))) {
                return Dictionary();
            }
            placement_ids[id] = true;
            changes.append(placement_row(recovery, id, Dictionary(), placement));
        }
        offer_count += actor_records.size();
        if (!actor_records.is_empty()) {
            reservations[actor] = actor_records;
        }
    }
    if (offer_count == 0 && currency_offers.is_empty() && locked != 0) {
        return Dictionary();
    }
    for (const Variant &raw_id : stack_source_ids.keys()) {
        const StringName id = StringName(raw_id);
        if (static_cast<bool>(placement_ids.get(id, false))) {
            return Dictionary();
        }
    }
    return plan;
}

bool SpatialInventoryTrade::valid_snapshot_header(Dictionary snapshot) {
    Variant version = snapshot.get("version", Variant());
    Variant phase_value = snapshot.get("phase", Variant());
    Variant initiator_value = snapshot.get("initiator", Variant());
    Variant receiver_value = snapshot.get("receiver", Variant());
    Variant saved_revision = snapshot.get("revision", Variant());
    Variant locked = snapshot.get("locked", Variant());
    Variant participants = snapshot.get("participants", Variant());
    if ((snapshot.size() != 7 || version.get_type() != Variant::INT ||
         (version != Variant(1) && version != Variant(SNAPSHOT_VERSION)) || phase_value.get_type() != Variant::INT ||
         initiator_value.get_type() != Variant::INT || receiver_value.get_type() != Variant::INT ||
         locked.get_type() != Variant::INT || saved_revision.get_type() != Variant::INT ||
         static_cast<int64_t>(saved_revision) < 0 ||
         std::max<int64_t>(revision_, static_cast<int64_t>(saved_revision)) >= MAX_SNAPSHOT_REVISION ||
         participants.get_type() != Variant::DICTIONARY || Dictionary(participants).size() != backpacks_.size())) {
        return false;
    }
    if (phase_value == Variant(IDLE)) {
        return initiator_value == Variant(0) && receiver_value == Variant(0) && locked == Variant(0);
    }
    if ((backpacks_.size() != 2 || initiator_value == receiver_value || !backpacks_.has(initiator_value) ||
         !backpacks_.has(receiver_value))) {
        return false;
    }
    if (phase_value == Variant(REQUESTED)) {
        return locked == Variant(0);
    }
    return phase_value == Variant(ACTIVE) &&
           (locked == Variant(0) || locked == initiator_value || locked == receiver_value);
}

Variant SpatialInventoryTrade::snapshot_currencies(Variant raw) {
    if (raw.get_type() != Variant::DICTIONARY || Dictionary(raw).size() > MAX_CURRENCIES_PER_OFFER) {
        return Variant();
    }
    Dictionary normalized;
    for (const Variant &key : Dictionary(raw).keys()) {
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) {
            return Variant();
        }
        StringName currency = StringName(key);
        Variant amount = Dictionary(raw)[key];
        if (currency.is_empty() || normalized.has(currency) || amount.get_type() != Variant::INT ||
            static_cast<int64_t>(amount) <= 0) {
            return Variant();
        }
        normalized[currency] = amount;
    }
    return normalized;
}

bool SpatialInventoryTrade::check_snapshot_backpacks(TypedArray<Dictionary> changes, StringName id, int64_t owner,
                                                     Dictionary expected) {
    for (const Variant &raw_actor : backpacks_.keys()) {
        const int64_t actor = static_cast<int64_t>(raw_actor);
        Ref<SpatialInventory> backpack = Ref<SpatialInventory>(backpacks_[actor]);
        Dictionary before = (actor == owner ? expected : Dictionary());
        if (backpack->get_placement(id) != before) {
            return false;
        }
        changes.append(placement_row(backpack, id, before, before));
    }
    return true;
}

Dictionary SpatialInventoryTrade::snapshot_offer(Variant raw) {
    if (raw.get_type() != Variant::DICTIONARY || Dictionary(raw).size() != 4) {
        return Dictionary();
    }
    Dictionary offer = snapshot_placement(Dictionary(raw).get("offer", Variant()));
    Variant stack = snapshot_stack(Dictionary(raw).get("stack", Variant()));
    Variant raw_sources = Dictionary(raw).get("sources", Variant());
    Variant raw_whole = Dictionary(raw).get("whole_id", Variant());
    if ((offer.is_empty() || stack == Variant() || raw_sources.get_type() != Variant::DICTIONARY ||
         Dictionary(raw_sources).size() > MAX_RETURN_SOURCES ||
         !((raw_whole.get_type() == Variant::STRING_NAME || raw_whole.get_type() == Variant::STRING)))) {
        return Dictionary();
    }
    StringName id = StringName(offer["id"]);
    StringName whole_id = StringName(raw_whole);
    bool gear = Dictionary(stack).is_empty();
    int64_t remaining = (gear ? 0 : static_cast<int64_t>(Dictionary(stack)["quantity"]));
    Dictionary sources;
    for (const Variant &key : Dictionary(raw_sources).keys()) {
        Variant entry = Dictionary(raw_sources)[key];
        if (!((key.get_type() == Variant::STRING_NAME || key.get_type() == Variant::STRING)) ||
            entry.get_type() != Variant::DICTIONARY || Dictionary(entry).size() != 2) {
            return Dictionary();
        }
        StringName source_id = StringName(key);
        Dictionary placement = snapshot_placement(Dictionary(entry).get("placement", Variant()));
        Variant amount = Dictionary(entry).get("quantity", Variant());
        if ((sources.has(source_id) || placement.is_empty() || placement["id"] != Variant(source_id) ||
             amount.get_type() != Variant::INT || static_cast<int64_t>(amount) < ((gear ? 0 : 1)) ||
             static_cast<int64_t>(amount) > remaining)) {
            return Dictionary();
        }
        sources[source_id] = make_dictionary({{"placement", placement}, {"quantity", amount}});
        remaining -= static_cast<int64_t>(amount);
    }
    if (gear) {
        if ((!whole_id.is_empty() || sources.size() != 1 || !sources.has(id) ||
             Dictionary(Dictionary(sources[id])["placement"])["footprint"] != offer["footprint"])) {
            return Dictionary();
        }
    } else if (!whole_id.is_empty()) {
        if ((sources.size() != 1 || !sources.has(whole_id) || remaining != 0 ||
             Dictionary(Dictionary(sources[whole_id])["placement"])["footprint"] != offer["footprint"])) {
            return Dictionary();
        }
    }
    return offer_record(offer, Dictionary(stack), sources, whole_id);
}

Dictionary SpatialInventoryTrade::snapshot_placement(Variant raw) {
    if (raw.get_type() != Variant::DICTIONARY || Dictionary(raw).size() != 4) {
        return Dictionary();
    }
    Variant id = Dictionary(raw).get("id", Variant());
    Variant footprint = Dictionary(raw).get("footprint", Variant());
    Variant origin = Dictionary(raw).get("origin", Variant());
    Variant rotated = Dictionary(raw).get("rotated", Variant());
    if (((id.get_type() != Variant::STRING_NAME && id.get_type() != Variant::STRING) || StringName(id).is_empty() ||
         footprint.get_type() != Variant::VECTOR2I || Vector2i(footprint).x <= 0 || Vector2i(footprint).y <= 0 ||
         origin.get_type() != Variant::VECTOR2I || Vector2i(origin).x < 0 || Vector2i(origin).y < 0 ||
         rotated.get_type() != Variant::BOOL)) {
        return Dictionary();
    }
    Dictionary placement =
        make_dictionary({{"id", StringName(id)}, {"footprint", footprint}, {"origin", origin}, {"rotated", rotated}});
    placement.make_read_only();
    return placement;
}

Variant SpatialInventoryTrade::snapshot_stack(Variant raw) {
    if (raw.get_type() != Variant::DICTIONARY || (!Dictionary(raw).is_empty() && Dictionary(raw).size() != 3)) {
        return Variant();
    }
    return normalize_stack(raw);
}

Dictionary SpatialInventoryTrade::edit(int64_t actor, StringName id, Dictionary reservation,
                                       TypedArray<Dictionary> changes, String reason,
                                       TypedArray<Dictionary> stack_changes, TypedArray<Dictionary> backing_records) {
    return edit_offers(actor, make_dictionary({{id, reservation}}), changes, reason, stack_changes, backing_records);
}

Dictionary SpatialInventoryTrade::edit_offers(int64_t actor, Dictionary edits, TypedArray<Dictionary> changes,
                                              String reason, TypedArray<Dictionary> stack_changes,
                                              TypedArray<Dictionary> backing_records) {
    Dictionary records = Dictionary(reservations_.get(actor, Dictionary()));
    Dictionary previous;
    for (const Variant &raw_id : edits.keys()) {
        const StringName id = StringName(raw_id);
        previous[id] = Dictionary(records.get(id, Dictionary()));
    }
    Dictionary previous_locks = locks_;
    locks_ = Dictionary();
    set_reservations(actor, records, edits);
    revision_ += 1;
    Error error = apply(changes, stack_changes);
    if (error != OK) {
        revision_ -= 1;
        locks_ = previous_locks;
        set_reservations(actor, records, previous);
        return make_result(error, placement_reason(error));
    }
    Dictionary result = make_result(OK, reason, changed_rows(changes));
    result["backing_record_rows"] = backing_records;
    for (const Variant &raw_id : edits.keys()) {
        const StringName id = StringName(raw_id);
        Dictionary after = Dictionary(edits[id]);
        if (!after.is_empty() && !Dictionary(after["stack"]).is_empty()) {
            Array(result["offer_record_ids"]).append(id);
        }
    }
    return result;
}

void SpatialInventoryTrade::set_reservations(int64_t actor, Dictionary records, Dictionary edits) {
    Dictionary sources = Dictionary(reserved_sources_.get(actor, Dictionary()));
    for (const Variant &raw_id : edits.keys()) {
        const StringName id = StringName(raw_id);
        Dictionary reservation = Dictionary(edits[id]);
        Dictionary previous = Dictionary(records.get(id, Dictionary()));
        adjust_source_counts(provenance_sources_, previous, -1);
        adjust_source_counts(provenance_sources_, reservation, 1);
        if (!previous.is_empty() && Dictionary(previous["stack"]).is_empty()) {
            adjust_source_counts(sources, previous, -1);
        }
        if (reservation.is_empty()) {
            records.erase(id);
        } else {
            records[id] = reservation;
            if (Dictionary(reservation["stack"]).is_empty()) {
                adjust_source_counts(sources, reservation, 1);
            }
        }
    }
    if (records.is_empty()) {
        reservations_.erase(actor);
    } else {
        reservations_[actor] = records;
    }
    if (sources.is_empty()) {
        reserved_sources_.erase(actor);
    } else {
        reserved_sources_[actor] = sources;
    }
}

void SpatialInventoryTrade::adjust_source_counts(Dictionary sources, Dictionary reservation, int64_t delta) {
    for (const Variant &raw_source_id : Dictionary(reservation.get("sources", Dictionary())).keys()) {
        const StringName source_id = StringName(raw_source_id);
        int64_t count = static_cast<int64_t>(sources.get(source_id, 0)) + delta;
        if (count == 0) {
            sources.erase(source_id);
        } else {
            sources[source_id] = count;
        }
    }
}

Dictionary SpatialInventoryTrade::settle(TypedArray<Dictionary> changes, bool completed,
                                         TypedArray<Dictionary> stack_changes, TypedArray<Dictionary> backing_records,
                                         TypedArray<Dictionary> transfers, Dictionary currency_plan) {
    Phase previous_phase = phase_;
    int64_t previous_initiator = initiator_;
    int64_t previous_receiver = receiver_;
    Dictionary previous_reservations = reservations_;
    Dictionary previous_sources = reserved_sources_;
    Dictionary previous_provenance = provenance_sources_;
    Dictionary previous_currency_offers = currency_offers_;
    Dictionary previous_locks = locks_;
    phase_ = IDLE;
    initiator_ = 0;
    receiver_ = 0;
    reservations_ = Dictionary();
    reserved_sources_ = Dictionary();
    provenance_sources_ = Dictionary();
    currency_offers_ = Dictionary();
    locks_ = Dictionary();
    revision_ += 1;
    TypedArray<Dictionary> currency_changes =
        TypedArray<Dictionary>(currency_plan.get("changes", TypedArray<Dictionary>()));
    Error error = apply(changes, stack_changes, currency_changes);
    if (error != OK) {
        phase_ = previous_phase;
        initiator_ = previous_initiator;
        receiver_ = previous_receiver;
        reservations_ = previous_reservations;
        reserved_sources_ = previous_sources;
        provenance_sources_ = previous_provenance;
        currency_offers_ = previous_currency_offers;
        revision_ -= 1;
        if (completed) {
            if (error == ERR_BUSY && !currency_changes.is_empty()) {
                return failed_exchange(error,
                                       "An item, offer, or wallet balance changed. Review the trade and lock again.");
            }
            return failed_exchange(error, placement_reason(error));
        }
        locks_ = previous_locks;
        return make_result(error, placement_reason(error));
    }
    Dictionary result =
        make_result(OK, (completed ? "Trade completed." : "Trade canceled."), changed_rows(changes), completed);
    result["backing_record_rows"] = backing_records;
    result["settlement_transfers"] = transfers;
    result["currency_transfers"] = currency_plan.get("transfers", Array());
    return result;
}

Error SpatialInventoryTrade::apply(TypedArray<Dictionary> changes, TypedArray<Dictionary> stack_changes,
                                   TypedArray<Dictionary> currency_changes) {
    busy_ = true;
    Error error = (currency_changes.is_empty() ? apply_checked(changes, stack_changes)
                                               : apply_wallet_checked(changes, stack_changes, currency_changes));
    busy_ = false;
    return error;
}

Dictionary SpatialInventoryTrade::failed_exchange(Error error, String reason) {
    locks_ = Dictionary();
    revision_ += 1;
    return make_result(error, reason);
}

Dictionary SpatialInventoryTrade::check_actor(int64_t actor) {
    if (busy_) {
        return make_result(ERR_BUSY, "A trade change is already committing.");
    }
    if (!backpacks_.has(actor)) {
        return make_result(ERR_UNAUTHORIZED, "That player is outside this trade.");
    }
    return Dictionary();
}

Dictionary SpatialInventoryTrade::check_edit(int64_t actor) {
    Dictionary denied = check_actor(actor);
    if (!denied.is_empty()) {
        return denied;
    }
    if (phase_ != ACTIVE) {
        return make_result(ERR_UNAVAILABLE, "Accept a trade request first.");
    }
    if (locks_.has(actor)) {
        return make_result(ERR_UNAUTHORIZED, "Unlock your offer before editing it.");
    }
    return Dictionary();
}

int64_t SpatialInventoryTrade::other_actor(int64_t actor) {
    for (const Variant &raw_participant : backpacks_.keys()) {
        const int64_t participant = static_cast<int64_t>(raw_participant);
        if (participant != actor) {
            return participant;
        }
    }
    return 0;
}

String SpatialInventoryTrade::participant_refusal(StringName stage, int64_t actor, int64_t other) {
    if (participant_eligibility_.is_null()) {
        return "";
    }
    return eligibility_refusal(participant_eligibility_,
                               make_dictionary({{"stage", stage}, {"actor", actor}, {"other_actor", other}}));
}

String SpatialInventoryTrade::item_refusal(StringName stage, int64_t actor, int64_t other, Dictionary placement,
                                           Dictionary stack, int64_t amount) {
    if (item_eligibility_.is_null()) {
        return "";
    }
    placement.make_read_only();
    stack.make_read_only();
    return eligibility_refusal(item_eligibility_, make_dictionary({{"stage", stage},
                                                                   {"actor", actor},
                                                                   {"other_actor", other},
                                                                   {"id", placement["id"]},
                                                                   {"placement", placement},
                                                                   {"stack", stack},
                                                                   {"quantity", (!stack.is_empty() ? amount : 1)}}));
}

String SpatialInventoryTrade::eligibility_refusal(Callable policy, Dictionary context) {
    if (!policy.is_valid()) {
        return "Trade eligibility policy is unavailable.";
    }
    context.make_read_only();
    bool was_busy = busy_;
    busy_ = true;
    Variant reason = policy.call(context);
    busy_ = was_busy;
    if (reason.get_type() != Variant::STRING && reason.get_type() != Variant::STRING_NAME) {
        return "Trade eligibility policy returned an invalid reason.";
    }
    return String(reason);
}

int64_t SpatialInventoryTrade::read_balance(int64_t actor, StringName currency) {
    bool was_busy = busy_;
    busy_ = true;
    const Variant value = wallet_->call("get_balance", actor, currency);
    const int64_t balance = value.get_type() == Variant::INT ? int64_t(value) : -1;
    busy_ = was_busy;
    return balance;
}

Dictionary SpatialInventoryTrade::get_reservation(int64_t actor, StringName offer_id) {
    return Dictionary(Dictionary(reservations_.get(actor, Dictionary())).get(offer_id, Dictionary()));
}

bool SpatialInventoryTrade::can_edit_quantities() {
    return describe_stack_.is_valid() && allocate_stack_id_.is_valid() && apply_stack_changes_.is_valid();
}

Variant SpatialInventoryTrade::read_stack(StringName id) {
    if (!describe_stack_.is_valid()) {
        return Dictionary();
    }
    bool was_busy = busy_;
    busy_ = true;
    Variant raw = describe_stack_.call(id);
    busy_ = was_busy;
    return normalize_stack(raw);
}

Variant SpatialInventoryTrade::normalize_stack(Variant raw) {
    if (raw.get_type() != Variant::DICTIONARY) {
        return Variant();
    }
    if (Dictionary(raw).is_empty()) {
        return Dictionary();
    }
    Variant key = Dictionary(raw).get("key", Variant());
    Variant quantity = Dictionary(raw).get("quantity", Variant());
    Variant maximum = Dictionary(raw).get("max_quantity", Variant());
    if (((key.get_type() != Variant::STRING_NAME && key.get_type() != Variant::STRING) || StringName(key).is_empty() ||
         quantity.get_type() != Variant::INT || maximum.get_type() != Variant::INT ||
         static_cast<int64_t>(quantity) <= 0 || static_cast<int64_t>(quantity) > static_cast<int64_t>(maximum))) {
        return Variant();
    }
    Dictionary stack = make_dictionary({{"key", StringName(key)}, {"quantity", quantity}, {"max_quantity", maximum}});
    stack.make_read_only();
    return stack;
}

StringName SpatialInventoryTrade::new_stack_id(StringName source_id) {
    bool was_busy = busy_;
    busy_ = true;
    Variant raw = allocate_stack_id_.call(source_id);
    busy_ = was_busy;
    if (raw.get_type() != Variant::STRING_NAME && raw.get_type() != Variant::STRING) {
        return StringName();
    }
    StringName id = StringName(raw);
    return (!id.is_empty() && !provenance_sources_.has(id) && id_is_unused(id) ? id : StringName());
}

bool SpatialInventoryTrade::id_is_unused(StringName id) {
    if (read_stack(id) != Variant(Dictionary())) {
        return false;
    }
    for (const Variant &raw_actor : backpacks_.keys()) {
        const int64_t actor = static_cast<int64_t>(raw_actor);
        if (has_placement(Ref<SpatialInventory>(backpacks_[actor]), id) ||
            has_placement(Ref<SpatialInventory>(offers_[actor]), id) ||
            has_placement(Ref<SpatialInventory>(recoveries_[actor]), id)) {
            return false;
        }
    }
    return true;
}

bool SpatialInventoryTrade::recoveries_are_empty() {
    for (const Variant &raw_recovery : recoveries_.values()) {
        const Ref<SpatialInventory> recovery = Ref<SpatialInventory>(raw_recovery);
        if (!recovery->get_placements().is_empty()) {
            return false;
        }
    }
    return true;
}

bool SpatialInventoryTrade::matches_reservation(int64_t actor, StringName id, Dictionary entry) {
    Ref<SpatialInventory> offer = Ref<SpatialInventory>(offers_[actor]);
    int64_t other = other_actor(actor);
    if ((Variant(offer->get_placement(id)) != entry["offer"] ||
         has_placement(Ref<SpatialInventory>(backpacks_[other]), id) ||
         has_placement(Ref<SpatialInventory>(offers_[other]), id) ||
         has_placement(Ref<SpatialInventory>(recoveries_[other]), id) ||
         has_placement(Ref<SpatialInventory>(recoveries_[actor]), id) || read_stack(id) != entry["stack"])) {
        return false;
    }
    Ref<SpatialInventory> backpack = Ref<SpatialInventory>(backpacks_[actor]);
    if (Dictionary(entry["stack"]).is_empty()) {
        return Variant(backpack->get_placement(id)) == Dictionary(Dictionary(entry["sources"])[id])["placement"];
    }
    return !has_placement(backpack, id);
}

bool SpatialInventoryTrade::has_placement(Ref<SpatialInventory> inventory, StringName id) {
    return !inventory->get_placement(id).is_empty();
}

Dictionary SpatialInventoryTrade::make_reservation(StringName id, Dictionary source, Dictionary stack, int64_t quantity,
                                                   Dictionary placement) {
    StringName whole_id =
        (!stack.is_empty() && quantity == static_cast<int64_t>(stack["quantity"]) ? id : StringName());
    return offer_record(placement, with_quantity(stack, quantity),
                        make_dictionary({{id, make_dictionary({{"placement", source}, {"quantity", quantity}})}}),
                        whole_id);
}

Dictionary SpatialInventoryTrade::offer_record(Dictionary placement, Dictionary stack, Dictionary sources,
                                               StringName whole_id) {
    return make_dictionary({{"offer", placement}, {"stack", stack}, {"sources", sources}, {"whole_id", whole_id}});
}

void SpatialInventoryTrade::add_allocation(Dictionary sources, StringName id, Dictionary allocation) {
    Dictionary previous = Dictionary(sources.get(id, Dictionary()));
    if (previous.is_empty()) {
        if (sources.size() < MAX_RETURN_SOURCES) {
            sources[id] = allocation;
        }
    } else {
        sources[id] = make_dictionary(
            {{"placement", previous["placement"]},
             {"quantity", static_cast<int64_t>(previous["quantity"]) + static_cast<int64_t>(allocation["quantity"])}});
    }
}

Dictionary SpatialInventoryTrade::with_quantity(Dictionary stack, int64_t quantity) {
    if (quantity <= 0) {
        return Dictionary();
    }
    Dictionary updated = stack.duplicate();
    updated["quantity"] = quantity;
    updated.make_read_only();
    return updated;
}

void SpatialInventoryTrade::stage_debit(Ref<SpatialInventory> backpack, StringName id, Dictionary placement,
                                        Dictionary stack, int64_t amount, TypedArray<Dictionary> changes,
                                        TypedArray<Dictionary> stack_changes, TypedArray<Dictionary> backing_records) {
    if (stack.is_empty()) {
        changes.append(placement_row(backpack, id, placement, placement));
        return;
    }
    Dictionary remainder = with_quantity(stack, static_cast<int64_t>(stack["quantity"]) - amount);
    changes.append(placement_row(backpack, id, placement, (remainder.is_empty() ? Dictionary() : placement)));
    stack_changes.append(stack_row(id, stack, remainder));
    if (!remainder.is_empty()) {
        backing_records.append(make_dictionary({{"inventory", backpack}, {"id", id}}));
    }
}

Dictionary SpatialInventoryTrade::stack_row(StringName id, Dictionary before, Dictionary after, StringName source_id) {
    Dictionary row = make_dictionary({{"id", id}, {"before", before}, {"after", after}});
    if (!source_id.is_empty()) {
        row["source_id"] = source_id;
    }
    return row;
}

Dictionary SpatialInventoryTrade::with_id(Dictionary placement, StringName id) {
    Dictionary updated = placement.duplicate();
    updated["id"] = id;
    updated.make_read_only();
    return updated;
}

Dictionary SpatialInventoryTrade::at(Dictionary placement, Vector2i origin, bool rotated) {
    Dictionary placed = make_dictionary(
        {{"id", placement["id"]}, {"footprint", placement["footprint"]}, {"origin", origin}, {"rotated", rotated}});
    placed.make_read_only();
    return placed;
}

Dictionary SpatialInventoryTrade::offer_placement(Ref<SpatialInventory> offer, Dictionary source, Vector2i origin,
                                                  Variant rotated) {
    Vector2i footprint = Vector2i(source["footprint"]);
    bool use_rotation = static_cast<bool>((rotated == Variant() ? source["rotated"] : rotated));
    if (origin == FIRST_FIT) {
        origin = offer->find_first_fit(footprint, use_rotation);
        if (origin == FIRST_FIT && rotated == Variant() && footprint.x != footprint.y) {
            use_rotation = !use_rotation;
            origin = offer->find_first_fit(footprint, use_rotation);
        }
        if (origin == FIRST_FIT) {
            return Dictionary();
        }
    }
    return at(source, origin, use_rotation);
}

bool SpatialInventoryTrade::valid_bulk_size(Ref<SpatialInventory> source, int64_t count) {
    Vector2i size = source->get_grid_size();
    return count > 0 && count <= size.x * size.y;
}

Dictionary SpatialInventoryTrade::placement_row(Ref<SpatialInventory> inventory, StringName id, Dictionary before,
                                                Dictionary after) {
    return make_dictionary({{"inventory", inventory}, {"id", id}, {"before", before}, {"after", after}});
}

TypedArray<Dictionary> SpatialInventoryTrade::changed_rows(TypedArray<Dictionary> changes) {
    TypedArray<Dictionary> changed;
    for (const Variant &raw_row : changes) {
        const Dictionary row = Dictionary(raw_row);
        if (row["before"] != row["after"]) {
            changed.append(row);
        }
    }
    return changed;
}

String SpatialInventoryTrade::placement_reason(Error error) {
    if (error == ERR_BUSY) {
        return "The item or its offer changed.";
    }
    if (error == ERR_ALREADY_IN_USE || error == ERR_PARAMETER_RANGE_ERROR) {
        return "The item does not fit in that cell.";
    }
    return UtilityFunctions::error_string(error);
}

Dictionary SpatialInventoryTrade::make_result(Error error, String reason, TypedArray<Dictionary> changes,
                                              bool completed) {
    return make_dictionary({{"error", error},
                            {"reason", reason},
                            {"changes", changes},
                            {"completed", completed},
                            {"offer_record_ids", Array()},
                            {"backing_record_rows", Array()},
                            {"settlement_transfers", Array()},
                            {"currency_transfers", Array()}});
}

Dictionary SpatialInventoryTrade::settlement_receivers(Ref<SpatialInventory> shadow) {
    Dictionary receivers;
    for (const Variant &raw_placement : shadow->get_placements()) {
        const Dictionary placement = raw_placement;
        const StringName id = placement["id"];
        const Variant raw_stack = read_stack(id);
        if (raw_stack.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary stack = raw_stack;
        if (stack.is_empty() || stack["quantity"] == stack["max_quantity"]) {
            continue;
        }
        const StringName key = stack["key"];
        const Dictionary group = receivers.get(key, make_dictionary({{"stacks", Array()}, {"next", 0}}));
        Array stacks = group["stacks"];
        stacks.append(
            make_dictionary({{"id", id}, {"placement", placement}, {"stack", stack}, {"change", Dictionary()}}));
        receivers[key] = group;
    }
    for (const Variant &raw_group : receivers.values()) {
        const Dictionary group = raw_group;
        Array stacks = group["stacks"];
        stacks.sort_custom(callable_mp_static(compare_origins));
    }
    return receivers;
}

Error SpatialInventoryTrade::apply_wallet_checked(TypedArray<Dictionary> changes, TypedArray<Dictionary> stack_changes,
                                                  TypedArray<Dictionary> currency_changes) {
    const auto gate = std::make_shared<CommitGate>();
    const auto commit =
        once(gate, [this, &changes, &stack_changes]() { return apply_checked(changes, stack_changes); });
    return finish_commit(*gate, wallet_->call("apply_balance_changes", currency_changes, commit));
}

Error SpatialInventoryTrade::apply_checked(TypedArray<Dictionary> changes, TypedArray<Dictionary> stack_changes) {
    if (stack_changes.is_empty()) {
        return SpatialInventory::apply_placement_changes(changes);
    }
    if (!apply_stack_changes_.is_valid()) {
        return ERR_UNCONFIGURED;
    }
    const auto gate = std::make_shared<CommitGate>();
    const auto commit = once(gate, [&changes]() { return SpatialInventory::apply_placement_changes(changes); });
    return finish_commit(*gate, apply_stack_changes_.call(stack_changes, commit));
}

} // namespace crimson::inventory
