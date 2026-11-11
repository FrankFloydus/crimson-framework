// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/match_runtime/transaction_coordinator.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <optional>
#include <thread>

namespace crimson::integration {

class CrimsonMatchCheckpoint;

// Opaque detached state owned by one prepared SaveCoordinator operation.
class CrimsonMatchCheckpointCandidate : public godot::RefCounted {
    GDCLASS(CrimsonMatchCheckpointCandidate, godot::RefCounted)

protected:
    // Register the internal type without exposing its mutable fields.
    static void _bind_methods();

private:
    friend class CrimsonMatchCheckpoint;

    // Identity of the bridge that created this candidate.
    uint64_t checkpoint_id = 0;
    // Bridge context version observed before staging.
    uint64_t context_version = 0;
    // Stable world ID resolved by Game Flow before activation.
    godot::String world_id;
    // Coordinator-owned prepared restore token, consumed on publication.
    godot::Ref<match_runtime::CrimsonTransaction> restore;
};

// Adapts authoritative match stores to one SaveCoordinator host and participant.
class CrimsonMatchCheckpoint : public godot::RefCounted {
    GDCLASS(CrimsonMatchCheckpoint, godot::RefCounted)

public:
    // Bind the current store registry and capture world on the owning thread.
    void set_context(const godot::Ref<match_runtime::CrimsonTransactionCoordinator> &coordinator,
            const godot::String &world_id);

    // Return the committed world identity and detached store DTOs.
    godot::Dictionary save_capture() const;
    // Create an empty, bridge-owned candidate for this load.
    godot::Dictionary save_create_candidate() const;
    // Validate the persisted match DTO and prepare the coordinator's detached restore.
    bool save_stage(const godot::Variant &dto, const godot::Variant &candidate) const;
    // Return the staged world ID while the candidate is owned and ready to publish.
    godot::String get_candidate_world(const godot::Variant &candidate) const;
    // Install the validated stores and defer notification until the world has mounted.
    bool save_publish(const godot::Variant &candidate);
    // Notify store observers after mounting the corresponding world.
    godot::Error notify_publication();
    // Drop a pending notification when recovery retires the entire owning match.
    void discard_publication();

protected:
    // Expose callback names consumed by SaveCoordinator and the native host API.
    static void _bind_methods();

private:
    // Identifies the exact coordinator commit awaiting observer notification.
    struct DeferredPublication {
        // Coordinator that published the restore, even if the capture context changes.
        godot::Ref<match_runtime::CrimsonTransactionCoordinator> coordinator;
        // Revision of the deferred restore publication.
        int64_t revision;
    };

    // Creating thread, which owns context changes and all checkpoint callbacks.
    const std::thread::id owner_thread = std::this_thread::get_id();
    // Store coordinator for the current match.
    godot::Ref<match_runtime::CrimsonTransactionCoordinator> stores;
    // Stable ID of the mounted world captured by the participant.
    godot::String active_world;
    // Advances whenever the store coordinator or mounted capture world changes.
    uint64_t context_version = 0;
    // Retains a deferred publication until Game Flow flushes it after mount.
    std::optional<DeferredPublication> deferred_publication;

    // Check thread affinity before accessing the bridge context.
    bool on_owner_thread() const;
    // Accept only a detached candidate belonging to the current bridge context.
    godot::Ref<CrimsonMatchCheckpointCandidate> owned_candidate(const godot::Variant &candidate) const;
};

} // namespace crimson::integration
