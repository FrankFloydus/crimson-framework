// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "inventory_wallet.hpp"
#include "spatial_inventory.h"

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace crimson::inventory {

class SpatialInventoryTrade : public godot::RefCounted {
    GDCLASS(SpatialInventoryTrade, godot::RefCounted)

public:
    enum Phase { IDLE, REQUESTED, ACTIVE };

    // Sentinel origin that selects the first available cell.
    static constexpr godot::Vector2i FIRST_FIT{-1, -1};
    // Version of the trade snapshot layout.
    static constexpr int SNAPSHOT_VERSION = 2;
    // Upper bound on revisions accepted from saved trades.
    static constexpr int64_t MAX_SNAPSHOT_REVISION = 0x3fffffffffffffff;
    // Limit on distinct currency IDs in an offer.
    static constexpr int64_t MAX_CURRENCIES_PER_OFFER = 32;
    // Largest supported nonnegative currency balance.
    static constexpr int64_t MAX_BALANCE = INT64_MAX;
    // Maximum source allocations accepted for a return.
    static constexpr int64_t MAX_RETURN_SOURCES = 64;

    // Returns the current trade phase.
    int64_t get_phase() const { return phase_; }
    // Returns the actor who opened the trade invitation.
    int64_t get_initiator() const { return initiator_; }
    // Returns the actor who received the trade invitation.
    int64_t get_receiver() const { return receiver_; }
    // Returns the current committed model revision.
    int64_t get_revision() const { return revision_; }

    // Returns the wallet used by the trade.
    godot::Ref<SpatialInventoryWallet> get_wallet() const { return wallet_; }
    // Sets the wallet used by the trade.
    void set_wallet(const godot::Ref<SpatialInventoryWallet> &wallet);
    // Returns the host callback for stack metadata and quantities.
    godot::Callable get_describe_stack() const { return describe_stack_; }
    // Sets the host callback for stack metadata and quantities.
    void set_describe_stack(const godot::Callable &callback) {
        if (!busy_) {
            describe_stack_ = callback;
        }
    }
    // Returns the host callback that allocates new stack IDs.
    godot::Callable get_allocate_stack_id() const { return allocate_stack_id_; }
    // Sets the host callback that allocates new stack IDs.
    void set_allocate_stack_id(const godot::Callable &callback) {
        if (!busy_) {
            allocate_stack_id_ = callback;
        }
    }
    // Returns the host callback that applies stack edits.
    godot::Callable get_apply_stack_changes() const { return apply_stack_changes_; }
    // Sets the host callback that applies stack edits.
    void set_apply_stack_changes(const godot::Callable &callback) {
        if (!busy_) {
            apply_stack_changes_ = callback;
        }
    }
    // Returns the host eligibility rule for participants.
    godot::Callable get_participant_eligibility() const { return participant_eligibility_; }
    // Sets the host eligibility rule for participants.
    void set_participant_eligibility(const godot::Callable &callback) {
        if (!busy_) {
            participant_eligibility_ = callback;
        }
    }
    // Returns the host eligibility rule for offered items.
    godot::Callable get_item_eligibility() const { return item_eligibility_; }
    // Sets the host eligibility rule for offered items.
    void set_item_eligibility(const godot::Callable &callback) {
        if (!busy_) {
            item_eligibility_ = callback;
        }
    }

    // Associates a trade actor with their backpack, offer, and recovery grids.
    godot::Error register_participant(int64_t actor, godot::Ref<godot::SpatialInventory> backpack, godot::Vector2i size,
                                      godot::Ref<godot::SpatialInventory> offer = {},
                                      godot::Ref<godot::SpatialInventory> recovery = {});
    // Returns the live projection of the actor's offered items.
    godot::Ref<godot::SpatialInventory> get_offer(int64_t actor);
    // Returns the actor's recovery grid for returned items.
    godot::Ref<godot::SpatialInventory> get_recovery(int64_t actor);
    // Reports whether a participant has accepted the current offer.
    bool is_locked(int64_t actor);
    // Checks whether an item is reserved by the participant's offer.
    bool is_reserved(int64_t actor, godot::StringName id);
    // Returns the IDs of source items reserved by this actor.
    godot::TypedArray<godot::StringName> get_reserved_source_ids(int64_t actor);
    // Returns the stack record associated with the offered placement.
    godot::Dictionary get_offer_stack(int64_t actor, godot::StringName offer_id);
    // Returns the currency amounts the actor has offered.
    godot::Dictionary get_currency_offers(int64_t actor);
    // Sets the amount offered for the actor and currency.
    godot::Dictionary set_currency_offer(int64_t actor, godot::StringName currency, int64_t amount);
    // Returns a versioned snapshot of the model's committed state.
    godot::Dictionary export_snapshot();
    // Validates a snapshot before replacing the model's committed state.
    godot::Error import_snapshot(godot::Dictionary snapshot);
    // Creates a trade invitation from the initiating actor.
    godot::Dictionary request(int64_t actor);
    // Accepts the invitation and moves the trade to its active phase.
    godot::Dictionary accept(int64_t actor);
    // Cancels the current trade and restores or recovers offered items.
    godot::Dictionary cancel(int64_t actor);
    // Adds an item to the offer grid and reserves its source.
    godot::Dictionary offer_item(int64_t actor, godot::StringName id, godot::Vector2i origin = FIRST_FIT,
                                 godot::Variant rotated = {});
    // Offers part of a stack with an optional target offer stack.
    godot::Dictionary offer_quantity(int64_t actor, godot::StringName id, int64_t amount, godot::Vector2i origin,
                                     godot::Variant rotated = {}, godot::StringName target_id = {});
    // Stages and adds a batch of items to the actor's offer.
    godot::Dictionary offer_items(int64_t actor, godot::TypedArray<godot::StringName> ids);
    // Changes the position or rotation of an item in the offer grid.
    godot::Dictionary move_offer(int64_t actor, godot::StringName id, godot::Vector2i origin, bool rotated);
    // Returns one offered item or stack to the actor.
    godot::Dictionary withdraw(int64_t actor, godot::StringName id, godot::Vector2i origin = FIRST_FIT,
                               godot::Variant rotated = {}, godot::StringName target_id = {});
    // Merges compatible offer stacks while preserving their source allocations.
    godot::Dictionary merge_offer_stacks(int64_t actor, godot::StringName source_id, godot::StringName target_id,
                                         int64_t limit = 0);
    // Returns a batch of offered items to the actor.
    godot::Dictionary withdraw_items(int64_t actor, godot::TypedArray<godot::StringName> ids);
    // Sets the actor's acceptance lock and settles the trade if both actors accept.
    godot::Dictionary set_lock(int64_t actor, bool locked);

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

private:
    // Current invitation or active-trade phase.
    Phase phase_ = IDLE;
    // Actor who requested the trade.
    int64_t initiator_ = 0;
    // Actor who received the invitation.
    int64_t receiver_ = 0;
    // Monotonic revision of committed trade changes.
    int64_t revision_ = 0;
    // Guards trade state against nested changes during settlement and callbacks.
    bool busy_ = false;
    // Wallet used to inspect and settle offered currencies.
    godot::Ref<SpatialInventoryWallet> wallet_;
    // Host callback that reads stack metadata and quantities.
    godot::Callable describe_stack_;
    // Host callback that allocates an ID for a split stack.
    godot::Callable allocate_stack_id_;
    // Host callback that applies expected and proposed stack records.
    godot::Callable apply_stack_changes_;
    // Host policy for whether the actors can trade.
    godot::Callable participant_eligibility_;
    // Host policy for whether a specific item can be traded.
    godot::Callable item_eligibility_;

    // Participants' owned inventory grids by actor ID.
    godot::Dictionary backpacks_;
    // Offer projection grids by actor ID.
    godot::Dictionary offers_;
    // Recovery grids for items that cannot return to their backpacks.
    godot::Dictionary recoveries_;
    // Offered item and stack records by actor ID.
    godot::Dictionary reservations_;
    // Counts tracking source items reserved in offers.
    godot::Dictionary reserved_sources_;
    // Counts tracking original sources of offered quantities.
    godot::Dictionary provenance_sources_;
    // Proposed currency amounts by actor and currency ID.
    godot::Dictionary currency_offers_;
    // Acceptance lock for each participant.
    godot::Dictionary locks_;

    // Prepares and commits the offer of one item or quantity.
    godot::Dictionary offer_single(int64_t actor, godot::StringName id, int64_t amount, godot::Vector2i origin,
                                   godot::Variant rotated, godot::StringName target_id);
    // Adds a source quantity to an existing offer stack.
    godot::Dictionary add_quantity_to_offer(int64_t actor, godot::StringName id, godot::Dictionary source,
                                            godot::Dictionary stack, int64_t amount, godot::Vector2i origin,
                                            godot::Variant rotated, godot::StringName target_id);
    // Plans and settles the exchange after both participants lock their offers.
    godot::Dictionary complete_trade();
    // Builds expected and proposed wallet balances for both participants.
    godot::Dictionary plan_currency_exchange();
    // Describes one proposed currency movement between actors.
    static godot::Dictionary currency_transfer(int64_t from_actor, int64_t to_actor, godot::StringName currency,
                                               int64_t amount);
    // Plans removal of items leaving the participant's backpack.
    godot::Dictionary plan_outgoing(int64_t actor, godot::TypedArray<godot::Dictionary> changes);
    // Plans where withdrawn items and quantities can return.
    godot::Dictionary plan_returns(int64_t actor, godot::Array ids, bool allow_recovery,
                                   godot::Vector2i origin = FIRST_FIT, godot::Variant rotated = {},
                                   godot::StringName target_id = {});
    // Merges returning units into a compatible stack in the staged plan.
    int64_t plan_return_merge(int64_t actor, godot::Dictionary state, godot::StringName id, godot::Dictionary stack,
                              int64_t amount, bool require_all);
    // Finds a free origin for a return in the detached grid.
    static godot::Dictionary find_return_cell(godot::Ref<godot::SpatialInventory> shadow, godot::Dictionary entry,
                                              bool rotated);
    // Chooses the instance ID to use when returning the offered item.
    godot::StringName return_id(godot::Dictionary entry, godot::StringName escrow_id);
    // Plans receiving the other actor's offered items.
    bool plan_incoming(godot::Dictionary source, godot::Dictionary destination, godot::Dictionary plan);
    // Indexes compatible receiving stacks for exchange settlement.
    godot::Dictionary settlement_receivers(godot::Ref<godot::SpatialInventory> shadow);
    // Plans the quantity that can merge into a destination stack.
    int64_t plan_merge_incoming(godot::Dictionary source, godot::Dictionary destination, godot::Dictionary entry,
                                godot::Dictionary group, godot::Dictionary plan);
    // Stages placement of an incoming item in the receiving backpack.
    static bool place_incoming(godot::Ref<godot::SpatialInventory> backpack, godot::Ref<godot::SpatialInventory> shadow,
                               godot::Dictionary offered, godot::TypedArray<godot::Dictionary> changes);
    // Describes a proposed item or quantity transfer for the settlement result.
    static godot::Dictionary settlement_transfer(godot::Dictionary source, godot::Dictionary destination,
                                                 godot::StringName source_id, godot::StringName destination_id,
                                                 int64_t quantity);
    // Builds the trade snapshot from current offers, reservations, and phase.
    godot::Dictionary capture_snapshot();
    // Validates a saved trade and prepares its placement and record changes.
    godot::Dictionary plan_snapshot(godot::Dictionary snapshot);
    // Validates the version and top-level fields of a trade snapshot.
    bool valid_snapshot_header(godot::Dictionary snapshot);
    // Validates and normalizes a saved currency offer.
    static godot::Variant snapshot_currencies(godot::Variant raw);
    // Checks saved item expectations against current backpack state.
    bool check_snapshot_backpacks(godot::TypedArray<godot::Dictionary> changes, godot::StringName id, int64_t owner,
                                  godot::Dictionary expected);
    // Validates and normalizes an offer record from a snapshot.
    static godot::Dictionary snapshot_offer(godot::Variant raw);
    // Validates a saved placement record and normalizes its fields.
    static godot::Dictionary snapshot_placement(godot::Variant raw);
    // Validates stack metadata stored in a trade snapshot.
    static godot::Variant snapshot_stack(godot::Variant raw);
    // Commits a placement and reservation edit for one offered item.
    godot::Dictionary edit(int64_t actor, godot::StringName id, godot::Dictionary reservation,
                           godot::TypedArray<godot::Dictionary> changes, godot::String reason,
                           godot::TypedArray<godot::Dictionary> stack_changes = {},
                           godot::TypedArray<godot::Dictionary> backing_records = {});
    // Commits a batch of offer, reservation, and stack record edits.
    godot::Dictionary edit_offers(int64_t actor, godot::Dictionary edits, godot::TypedArray<godot::Dictionary> changes,
                                  godot::String reason, godot::TypedArray<godot::Dictionary> stack_changes = {},
                                  godot::TypedArray<godot::Dictionary> backing_records = {});
    // Sets the offer reservations and their source allocation indexes.
    void set_reservations(int64_t actor, godot::Dictionary records, godot::Dictionary edits);
    // Adds or subtracts source usage recorded by an offer reservation.
    static void adjust_source_counts(godot::Dictionary sources, godot::Dictionary reservation, int64_t delta);
    // Applies exchange or return changes and reports the completed outcome.
    godot::Dictionary settle(godot::TypedArray<godot::Dictionary> changes, bool completed,
                             godot::TypedArray<godot::Dictionary> stack_changes = {},
                             godot::TypedArray<godot::Dictionary> backing_records = {},
                             godot::TypedArray<godot::Dictionary> transfers = {}, godot::Dictionary currency_plan = {});
    // Routes prepared edits through the placement, stack, and currency commit checks.
    godot::Error apply(godot::TypedArray<godot::Dictionary> changes,
                       godot::TypedArray<godot::Dictionary> stack_changes = {},
                       godot::TypedArray<godot::Dictionary> currency_changes = {});
    // Combines checked wallet changes with the item commit.
    godot::Error apply_wallet_checked(godot::TypedArray<godot::Dictionary> changes,
                                      godot::TypedArray<godot::Dictionary> stack_changes,
                                      godot::TypedArray<godot::Dictionary> currency_changes);
    // Combines checked placement changes with optional host stack edits.
    godot::Error apply_checked(godot::TypedArray<godot::Dictionary> changes,
                               godot::TypedArray<godot::Dictionary> stack_changes);
    // Reports a failed settlement and resets participant locks.
    godot::Dictionary failed_exchange(godot::Error error, godot::String reason);
    // Validates that the caller is a registered participant.
    godot::Dictionary check_actor(int64_t actor);
    // Validates that the participant can edit the active offer.
    godot::Dictionary check_edit(int64_t actor);
    // Returns the other registered actor in the trade.
    int64_t other_actor(int64_t actor);
    // Evaluates the host's trade participant eligibility rule.
    godot::String participant_refusal(godot::StringName stage, int64_t actor, int64_t other);
    // Evaluates the host's eligibility rule for the proposed item and quantity.
    godot::String item_refusal(godot::StringName stage, int64_t actor, int64_t other, godot::Dictionary placement,
                               godot::Dictionary stack, int64_t amount);
    // Interprets the host callback's eligibility result.
    godot::String eligibility_refusal(godot::Callable policy, godot::Dictionary context);
    // Reads a current currency balance through the configured wallet.
    int64_t read_balance(int64_t actor, godot::StringName currency);
    // Returns the reservation associated with the offer item.
    godot::Dictionary get_reservation(int64_t actor, godot::StringName offer_id);
    // Checks that the host supplied the callbacks required to edit quantities.
    bool can_edit_quantities();
    // Obtains and validates the current metadata for a stack.
    godot::Variant read_stack(godot::StringName id);
    // Validates and normalizes a stack metadata record.
    static godot::Variant normalize_stack(godot::Variant raw);
    // Asks the host for an unused instance ID for a new stack.
    godot::StringName new_stack_id(godot::StringName source_id);
    // Checks whether an ID is absent from the trade's tracked grids.
    bool id_is_unused(godot::StringName id);
    // Checks whether all recovery inventories have been emptied.
    bool recoveries_are_empty();
    // Checks that an offered item still agrees with its reservation.
    bool matches_reservation(int64_t actor, godot::StringName id, godot::Dictionary entry);
    // Checks whether the given grid contains the item.
    static bool has_placement(godot::Ref<godot::SpatialInventory> inventory, godot::StringName id);
    // Builds the source reservation and its projected offer placement.
    static godot::Dictionary make_reservation(godot::StringName id, godot::Dictionary source, godot::Dictionary stack,
                                              int64_t quantity, godot::Dictionary placement);
    // Builds an offer record including stack quantities and source allocations.
    static godot::Dictionary offer_record(godot::Dictionary placement, godot::Dictionary stack,
                                          godot::Dictionary sources, godot::StringName whole_id = {});
    // Adds a source allocation to a stack's provenance.
    static void add_allocation(godot::Dictionary sources, godot::StringName id, godot::Dictionary allocation);
    // Copies stack metadata with a replacement quantity.
    static godot::Dictionary with_quantity(godot::Dictionary stack, int64_t quantity);
    // Stages the removal or quantity reduction of a source stack.
    static void stage_debit(godot::Ref<godot::SpatialInventory> backpack, godot::StringName id,
                            godot::Dictionary placement, godot::Dictionary stack, int64_t amount,
                            godot::TypedArray<godot::Dictionary> changes,
                            godot::TypedArray<godot::Dictionary> stack_changes,
                            godot::TypedArray<godot::Dictionary> backing_records);
    // Builds a before-and-after edit for a host stack record.
    static godot::Dictionary stack_row(godot::StringName id, godot::Dictionary before, godot::Dictionary after,
                                       godot::StringName source_id = {});
    // Copies a placement with a replacement item ID.
    static godot::Dictionary with_id(godot::Dictionary placement, godot::StringName id);
    // Copies a placement at a new origin and orientation.
    static godot::Dictionary at(godot::Dictionary placement, godot::Vector2i origin, bool rotated);
    // Resolves and validates the proposed placement in an offer grid.
    static godot::Dictionary offer_placement(godot::Ref<godot::SpatialInventory> offer, godot::Dictionary source,
                                             godot::Vector2i origin, godot::Variant rotated);
    // Checks whether the requested batch fits the source inventory's item count.
    static bool valid_bulk_size(godot::Ref<godot::SpatialInventory> source, int64_t count);
    // Builds a before-and-after row for the grid change batch.
    static godot::Dictionary placement_row(godot::Ref<godot::SpatialInventory> inventory, godot::StringName id,
                                           godot::Dictionary before, godot::Dictionary after);
    // Returns only placement rows that would change the grid.
    static godot::TypedArray<godot::Dictionary> changed_rows(godot::TypedArray<godot::Dictionary> changes);
    // Converts a grid error to a placement explanation.
    static godot::String placement_reason(godot::Error error);
    // Builds a trade outcome with its error, changes, and completion flag.
    static godot::Dictionary make_result(godot::Error error, godot::String reason,
                                         godot::TypedArray<godot::Dictionary> changes = {}, bool completed = false);
};

} // namespace crimson::inventory

VARIANT_ENUM_CAST(crimson::inventory::SpatialInventoryTrade::Phase);
