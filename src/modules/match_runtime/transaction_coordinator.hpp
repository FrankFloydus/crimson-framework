// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <thread>
#include <vector>

namespace crimson::match_runtime {

// Copy bounded, acyclic runtime DTO data without retaining mutable container aliases.
bool copy_runtime_dto(const godot::Dictionary &value, godot::Dictionary &output);

class CrimsonTransactionCoordinator;

// A coordinator-owned transaction or prepared restore that can be submitted once.
class CrimsonTransaction : public godot::RefCounted {
    GDCLASS(CrimsonTransaction, godot::RefCounted)

public:
    // Copy an operation into the plan with the caller's expected store revision.
    godot::Error add_change(const godot::String &store_id, const godot::Dictionary &operation,
            int64_t expected_revision);

protected:
    // Expose the plan builder API to Godot.
    static void _bind_methods();

private:
    friend class CrimsonTransactionCoordinator;

    // A detached operation for one store in the order supplied by the caller.
    struct Change {
        // Stable ID of the store to modify.
        godot::String store_id;
        // Detached DTO passed to the owning store for preparation.
        godot::Dictionary operation;
        // Committed store revision observed by the caller.
        int64_t expected_revision;
    };

    // Coordinator instance ID used to reject foreign plans without extending its lifetime.
    uint64_t owner_id = 0;
    // Thread on which the owning coordinator permits plan mutations.
    std::thread::id owner_thread;
    // True once the owning coordinator has accepted this plan for evaluation.
    bool consumed = false;
    // DTO values counted across the context and all queued operations.
    std::size_t dto_nodes = 0;
    // String characters counted across the context and all queued operations.
    std::size_t dto_characters = 0;
    // Detached authority context shared with each participant through its own copy.
    godot::Dictionary context;
    // Changes appended by the caller before submission.
    std::vector<Change> changes;
    // Base revision of a prepared restore, or -1 for a regular transaction.
    int64_t restore_base_revision = -1;
    // Validator count used to invalidate restore plans if the registry grows.
    std::size_t restore_validator_count = 0;
    // Detached candidates for every registered store in a prepared restore.
    godot::Dictionary restore_states;
};

// Owns authoritative DTO stores and publishes complete transactions on its owner thread.
class CrimsonTransactionCoordinator : public godot::RefCounted {
    GDCLASS(CrimsonTransactionCoordinator, godot::RefCounted)

public:
    // Register a store and its preparation callback using a detached initial state.
    // Restore checks use validate_restore(candidate, context), or prepare(candidate, [], context) when omitted.
    godot::Error register_store(const godot::String &store_id, const godot::Dictionary &initial_state,
            const godot::Callable &prepare, const godot::Callable &validate_restore = {});
    // Register a validator that checks prepared state when any watched store changes.
    godot::Error register_validator(const godot::String &validator_id, const godot::PackedStringArray &watched_stores,
            const godot::Callable &validator);
    // Return a detached copy of a registered store's last committed state.
    godot::Dictionary read_store(const godot::String &store_id) const;
    // Read one detached value without copying the rest of the store.
    godot::Variant read_store_value(const godot::String &store_id, const godot::String &key) const;
    // Return a store's committed revision or -1 when the store is unavailable.
    int64_t get_store_revision(const godot::String &store_id) const;
    // Return the current coordinator revision or -1 from another thread.
    int64_t get_revision() const;
    // Create a new plan for a detached authority context.
    godot::Ref<CrimsonTransaction> begin(const godot::Dictionary &context = {});
    // Prepare, validate, and install an owned plan before notifying observers.
    godot::Dictionary commit(const godot::Ref<CrimsonTransaction> &transaction);

    // Capture a bounded, detached stores map at one committed revision.
    godot::Dictionary capture_snapshot() const;
    // Validate the exact registered stores and return a detached, single-use candidate.
    godot::Dictionary prepare_restore(const godot::Dictionary &snapshot, const godot::Dictionary &context = {});
    // Publish every store at one revision and optionally hold observers until scene activation completes.
    godot::Dictionary publish_restore(const godot::Ref<CrimsonTransaction> &candidate, bool defer_notification = false);
    // Notify observers of a deferred restore and release its mutation gate after they return.
    godot::Error notify_restore_published(int64_t published_revision);

    // Check plan ownership for native subsystems before they add operations.
    bool owns_transaction(const godot::Ref<CrimsonTransaction> &transaction) const;
    // Register a native subsystem's store and validator as one setup operation.
    godot::Error register_store_with_validator(const godot::String &store_id,
            const godot::Dictionary &initial_state, const godot::Callable &prepare, const godot::String &validator_id,
            const godot::PackedStringArray &watched_stores, const godot::Callable &validator);

protected:
    // Expose the coordinator API and committed signal to Godot.
    static void _bind_methods();

private:
    // Keeps one domain's published state and preparation policy together.
    struct Store {
        // Detached authoritative state visible to committed reads.
        godot::Dictionary state;
        // Callback that stages a candidate from the current state and queued operations.
        godot::Callable prepare;
        // Optional schema check for a detached restore candidate.
        godot::Callable validate_restore;
        // Number of successful transactions that have touched this store.
        int64_t revision = 0;
    };

    // A cross-store invariant that runs after every affected store has prepared.
    struct Validator {
        // Store IDs whose changes trigger validation.
        godot::PackedStringArray watched_stores;
        // Callback that approves or rejects the detached candidate set.
        godot::Callable validate;
    };

    // Authoritative stores indexed by stable ID.
    std::map<godot::String, Store> stores;
    // Registered validators indexed by stable ID for deterministic evaluation.
    std::map<godot::String, Validator> validators;
    // Creating thread, which owns all mutable coordinator state.
    const std::thread::id owner_thread = std::this_thread::get_id();
    // Number of successful transactions published by this coordinator.
    int64_t revision = 0;
    // Holds the mutation gate through preparation, validation, installation, and notification.
    bool committing = false;
    // Published restore revision waiting for its surrounding transition, or -1 when none is pending.
    int64_t pending_restore_revision = -1;
    // Store IDs to announce when the deferred restore notification is released.
    godot::PackedStringArray pending_restore_stores;

    // Check whether the current caller is on the coordinator's owner thread.
    bool is_owner_thread() const;
    // Check whether a transaction callback or a deferred restore owns the mutation gate.
    bool mutation_locked() const;
    // Check whether a validator ID has already been registered.
    bool has_validator(const godot::String &validator_id) const;
    // Build a consistent result for callers using the current committed revision.
    godot::Dictionary result(bool ok, const godot::String &code, const godot::String &message) const;
    // Check every validator affected by the prepared store map and return an error message on failure.
    godot::String validate_prepared_states(const godot::Dictionary &prepared, const godot::Dictionary &context) const;
};

} // namespace crimson::match_runtime
