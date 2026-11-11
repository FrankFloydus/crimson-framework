// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>
#include <memory>

namespace checkpoint {

class Codec;
class Store;

// Main-thread Godot node that coordinates checkpoint capture, I/O, and restore.
class SaveCoordinator : public godot::Node {
    GDCLASS(SaveCoordinator, godot::Node)

public:
    // Allocate coordinator state for a newly created node.
    SaveCoordinator();
    // Release coordinator state and shut down its worker.
    ~SaveCoordinator() override;

    // Enable processing so queued worker events reach the main thread.
    void _ready() override;
    // Cancel and discard an active prepared restore before leaving the tree.
    void _exit_tree() override;
    // Deliver worker events and dispatch queued requests on the main thread.
    void _process(double delta) override;

    // Configure the application ID and checkpoint storage root before I/O starts.
    bool configure(const godot::String &app_id, const godot::String &root);
    // Set the scope identity and schema version before schema freeze.
    bool configure_scope(const godot::String &scope_id, int64_t version);
    // Bind the object that creates and publishes restore candidates.
    bool set_host(godot::Object *host);
    // Declare a participant schema before binding its live adapter.
    bool declare_participant(const godot::String &id, int64_t version, bool required);
    // Bind a live adapter to a previously declared participant.
    bool bind_participant(const godot::String &id, godot::Object *adapter);
    // Declare a participant schema and bind its adapter in one call.
    bool register_participant(const godot::String &id, int64_t version, godot::Object *adapter, bool required);
    // Register one version upgrade step for a participant payload.
    bool register_migration(const godot::String &id, int64_t from, const godot::Callable &migration);
    // Register one upgrade step for the participant collection of a scope.
    bool register_scope_migration(int64_t from, const godot::Callable &migration);

    // Queue a manual checkpoint for capture at the next host safe point.
    int64_t request_save(const godot::String &profile, const godot::String &slot, const godot::Dictionary &metadata);
    // Queue an autosave and supersede a matching uncaptured autosave.
    int64_t request_autosave(const godot::String &profile, const godot::String &slot, const godot::Dictionary &metadata);
    // Queue a restore that publishes automatically after staging succeeds.
    int64_t request_load(const godot::String &profile, const godot::String &slot,
            const godot::String &sequence, const godot::String &generation_id);
    // Queue a restore that pauses after staging for explicit activation.
    int64_t request_prepare_load(const godot::String &profile, const godot::String &slot,
            const godot::String &sequence, const godot::String &generation_id);
    // Inspect one page of committed generations without restoring gameplay state.
    int64_t request_list(const godot::String &profile, const godot::String &slot,
            const godot::String &before_sequence, const godot::String &before_id, int64_t limit);
    // Inspect the generation matching a complete slot reference.
    int64_t request_inspect_generation(const godot::String &profile, const godot::String &slot,
            const godot::String &sequence, const godot::String &generation_id);
    // Return the detached candidate for the active prepared load ticket.
    godot::Variant get_prepared_candidate(int64_t ticket) const;
    // Validate and publish the prepared candidate on the main thread.
    bool activate_prepared_load(int64_t ticket);
    // Validate and discard an exact slot head to permit a subsequent scope upgrade.
    bool validate_prepared_load_for_save(int64_t ticket);
    // Install available services from a trusted provider DLL before I/O starts.
    bool load_native_provider(const godot::String &path, const godot::String &configuration);
    // Capture the first pending save at a host-defined revision.
    bool capture_pending(const godot::String &revision);
    // Cancel a queued or active ticket before its commit boundary.
    godot::String cancel(int64_t ticket);
    // Return the error code from the most recent rejected request.
    godot::String get_last_admission_error() const;
    // Check whether requests and undelivered notifications are drained.
    bool is_idle() const;
    // Retire pending work from the current world when replacing it outside restore.
    bool invalidate_world();

    // Native integrations may replace the default services before the first request.
    bool set_services(std::shared_ptr<Store> store, std::shared_ptr<Codec> codec);

protected:
    // Expose the coordinator methods and signals to Godot.
    static void _bind_methods();

private:
    struct Impl;
    // Private request, binding, and worker state owned by the Godot node.
    std::unique_ptr<Impl> impl;

    // Validate and enqueue a save intent with its capture policy.
    int64_t queue_save(const godot::String &profile, const godot::String &slot,
            const godot::Dictionary &metadata, bool autosave);
    // Validate and enqueue a load with its generation selection and activation mode.
    int64_t queue_load(const godot::String &profile, const godot::String &slot,
            const godot::String &sequence, const godot::String &generation_id, bool prepare_only);
};

} // namespace checkpoint
