// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "transaction_coordinator.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <thread>

namespace crimson::match_runtime {

// Owns the match's one-primary-character assignment rules.
class PlayerPossession : public godot::RefCounted {
    GDCLASS(PlayerPossession, godot::RefCounted)

public:
    // Registers possession state and its cross-store validator with the match coordinator.
    godot::Error configure(const godot::Ref<TransactionCoordinator> &coordinator,
            const godot::String &players_store, const godot::String &characters_store,
            const godot::Callable &eligibility = {});

    // Reads the committed primary character ID, or an empty ID when the player has none.
    godot::String get_primary(const godot::String &player_id) const;

    // Reads the committed primary controller ID, or an empty ID when the character has none.
    godot::String get_controller(const godot::String &character_id) const;

    // Reads the player's assignment revision in the active world epoch, or zero for a missing assignment.
    int64_t get_revision(const godot::String &player_id) const;

    // Queues a switch or release alongside other changes in the same match transaction.
    godot::Error queue_primary_change(const godot::Ref<GameTransaction> &transaction,
            const godot::String &player_id, const godot::String &character_id, int64_t expected_revision);

    // Commits a single switch or release and returns the transaction outcome.
    godot::Dictionary try_set_primary(const godot::String &player_id, const godot::String &character_id,
            int64_t expected_revision);

protected:
    // Exposes the possession API, its coordinator callbacks, and the assignment signal.
    static void _bind_methods();

private:
    // Limits mutation, reads, and callbacks to the thread that owns the service.
    const std::thread::id owner_thread = std::this_thread::get_id();

    // Keeps the match's transaction coordinator alive while possession is configured.
    godot::Ref<TransactionCoordinator> coordinator;

    // Identifies the store containing game-defined player records.
    godot::String players_store;

    // Identifies the store containing game-defined character records.
    godot::String characters_store;

    // Applies optional game rules to a proposed switch or release.
    godot::Callable eligibility;

    // Remembers the preceding committed assignments for change notification.
    godot::Dictionary last_published;

    // Prevents duplicate notification for the same published store revision.
    int64_t last_store_revision = 0;

    // Checks whether the caller can access this service's live fields.
    bool on_owner_thread() const;

    // Validates operations and prepares a detached candidate assignment store.
    godot::Dictionary prepare_store(const godot::Dictionary &current, const godot::Array &operations,
            const godot::Dictionary &context);

    // Checks the final candidate records after all affected stores have prepared.
    godot::Dictionary validate_references(const godot::Dictionary &prepared, const godot::Dictionary &context) const;

    // Emits primary changes after the coordinator has published every store.
    void on_committed(int64_t revision, const godot::PackedStringArray &stores);
};

} // namespace crimson::match_runtime
