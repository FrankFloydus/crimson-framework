// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace crimson::steam {

class SteamFloatResult;
class SteamIntResult;
class SteamServerRuntime;
class SteamServerStatsEdit;
struct SteamServerProgressionState;

// Outcome of loading or storing one admitted player's Steam GameServer stats.
class SteamServerStatsResult : public SteamResult {
    GDCLASS(SteamServerStatsResult, SteamResult)

public:
    // Number of SDK setters accepted into the cache during this submission.
    int64_t get_applied() const;
    // Index of the rejected edit, or -1 when failure was outside an individual edit.
    int64_t get_failed_at() const;
    // Native EResult for the last observed load or store, or zero if no native result arrived.
    int64_t get_steam_result() const;
    // Report whether Steam accepted a native store request before observation ended.
    bool was_store_submitted() const;

protected:
    // Bind the immutable submission outcome.
    static void _bind_methods();

private:
    friend struct SteamServerProgressionState;
    friend class SteamServerStatsOperation;

    // Create a terminal outcome with the known native effects and callback evidence.
    static godot::Ref<SteamServerStatsResult> create(godot::Error error, int64_t applied,
            int64_t failed_at, int64_t steam_result, bool store_submitted);

    // Accepted setters in this operation's edit batch.
    int64_t applied = 0;
    // Rejected edit index, when available.
    int64_t failed_at = -1;
    // Native completion status, if Steam supplied one.
    int64_t steam_result = 0;
    // The native StoreUserStats action was submitted.
    bool store_submitted = false;
};

// Retained observation of one dedicated-server stats operation.
class SteamServerStatsOperation : public godot::RefCounted {
    GDCLASS(SteamServerStatsOperation, godot::RefCounted)

public:
    // Report whether this observation settled.
    bool is_finished() const;
    // Return the immutable outcome, or null while pending.
    godot::Ref<SteamServerStatsResult> result() const;
    // Return the terminal result or the completion signal to await.
    godot::Variant wait();
    // End observation locally. An issued Steam store can still complete.
    godot::Error cancel();

protected:
    // Register the operation methods and typed completion signal.
    static void _bind_methods();

private:
    friend struct SteamServerProgressionState;
    friend class SteamServerPlayerStats;
    friend class SteamServerStatsEdit;

    // Create an operation that has already settled with a local error.
    static godot::Ref<SteamServerStatsOperation> settled(godot::Error error, int64_t failed_at = -1);
    // Settle this observer once and notify after all terminal state is recorded.
    void finish(const godot::Ref<SteamServerStatsResult> &result);

    // Terminal outcome, null while awaiting Steam.
    godot::Ref<SteamServerStatsResult> outcome;
    // Scene-thread function that can release this exact local observer.
    std::function<godot::Error()> cancel_observation;
};

// Read and write published Steam stats for one exact dedicated-server admission.
class SteamServerPlayerStats : public godot::RefCounted {
    GDCLASS(SteamServerPlayerStats, godot::RefCounted)

public:
    // Check this admission and the availability of the dedicated SDK interface.
    bool is_available() const;
    // Report whether Steam has loaded the cache for this exact admission.
    bool is_loaded() const;
    // Load the Steam cache without submitting any edits.
    godot::Ref<SteamServerStatsOperation> load() const;
    // Read a signed integer stat from this admission's loaded cache.
    godot::Ref<SteamIntResult> stat_int(const godot::StringName &name) const;
    // Read a finite float stat from this admission's loaded cache.
    godot::Ref<SteamFloatResult> stat_float(const godot::StringName &name) const;
    // Read whether Steam's loaded cache marks the named achievement as unlocked.
    godot::Ref<SteamBoolResult> achievement(const godot::StringName &name) const;
    // Create a draft whose setters run only when it is submitted.
    godot::Ref<SteamServerStatsEdit> edit();
    // Retry storing previously accepted cache changes without replaying any setter.
    godot::Ref<SteamServerStatsOperation> store() const;

protected:
    // Bind cache reads, draft creation and explicit persistence.
    static void _bind_methods();

private:
    friend class SteamServerProgression;
    friend class SteamServerStatsEdit;
    friend struct SteamServerProgressionState;

    // Weak progression owner on the exact dedicated Steam runtime.
    std::weak_ptr<SteamServerProgressionState> state;
    // Steam account taken from the certified transport and admitted Session identity.
    uint64_t account = 0;
    // Exact SessionIdentity lifetime, which changes when the peer is readmitted.
    godot::ObjectID identity;
    // Recheck the same connection's present admission without retaining its Game.
    std::function<godot::ObjectID()> resolve_admission;
};

// Branchable edits to one dedicated player's Steam cache.
class SteamServerStatsEdit : public godot::RefCounted {
    GDCLASS(SteamServerStatsEdit, godot::RefCounted)

public:
    // Return a branch with one absolute signed 32-bit stat value staged.
    godot::Ref<SteamServerStatsEdit> set_int(const godot::StringName &name, int64_t value) const;
    // Return a branch with one finite Steam float value staged.
    godot::Ref<SteamServerStatsEdit> set_float(const godot::StringName &name, double value) const;
    // Return a branch with one count over a positive duration in seconds staged.
    godot::Ref<SteamServerStatsEdit> update_average_rate(const godot::StringName &name, double count,
            double seconds) const;
    // Return a branch that unlocks a published Game Server or Official GS achievement.
    godot::Ref<SteamServerStatsEdit> unlock(const godot::StringName &name) const;
    // Return a branch that clears a published server-set achievement.
    godot::Ref<SteamServerStatsEdit> clear_achievement(const godot::StringName &name) const;
    // Load if needed, apply this draft once, and await Steam's store result.
    godot::Ref<SteamServerStatsOperation> submit();

protected:
    // Bind branchable setters and the submission action.
    static void _bind_methods();

private:
    friend class SteamServerPlayerStats;
    friend struct SteamServerProgressionState;

    // Native action represented by a staged change.
    enum class Kind { INTEGER, FLOAT, AVERAGE_RATE, UNLOCK, CLEAR };

    // A validated edit ready to be applied to the loaded cache.
    struct Change {
        // Select the native setter and its associated value fields.
        Kind kind;
        // Published Steamworks API name.
        godot::StringName name;
        // Absolute signed value for integer stats.
        int64_t integer = 0;
        // Absolute float or accumulated average-rate count.
        double scalar = 0.0;
        // Elapsed seconds for an average-rate update.
        double seconds = 0.0;
    };

    // Retain the scoped handle without retaining its Game or Steam runtime.
    godot::Ref<SteamServerPlayerStats> player;
    // Independent ordered edit list with a fixed upper bound.
    std::vector<Change> changes;
    // Index of the first invalid or over-limit edit, or -1 while valid.
    int64_t invalid_edit = -1;
    // Repeated submission of this exact draft returns the original observation.
    godot::Ref<SteamServerStatsOperation> submission;

    // Copy this draft's contents and stage one bounded, locally validated edit.
    godot::Ref<SteamServerStatsEdit> append(Change change, bool valid_value) const;
};

// Owns progression observers and drains issued calls for the lifetime of one server pipe.
class SteamServerProgression {
public:
    // Start a progression owner for the dedicated runtime without issuing Steam calls.
    explicit SteamServerProgression(const std::shared_ptr<SteamServerRuntime> &runtime);
    // Retire account observations before releasing the dedicated runtime.
    ~SteamServerProgression();

    // Bind a fresh public handle to the exact admitted Steam account.
    godot::Ref<SteamServerPlayerStats> for_peer(uint64_t account, godot::ObjectID identity,
            std::function<godot::ObjectID()> resolve_admission) const;
    // Settle departed and timed-out observations, keeping issued stores fenced until drained.
    void tick();

private:
    // Native call state, shared only while a scene-thread callback is delivering an outcome.
    std::shared_ptr<SteamServerProgressionState> state;
};

} // namespace crimson::steam
