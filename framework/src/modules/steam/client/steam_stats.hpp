// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <memory>
#include <variant>
#include <vector>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// A signed integer stat read with a separate outcome for unavailable data.
class SteamIntResult : public SteamResult {
    GDCLASS(SteamIntResult, SteamResult)

public:
    // Return the cached signed integer when the read succeeded.
    int64_t get_value() const;

protected:
    // Bind the read-only integer payload.
    static void _bind_methods();

private:
    friend class SteamStats;
    friend class SteamServerPlayerStats;

    // Construct a terminal read result with zero payload on failure.
    static godot::Ref<SteamIntResult> create(godot::Error error, int64_t value);

    // The cached integer value, meaningful when the result is OK.
    int64_t value = 0;
};

// A floating-point stat read with a separate outcome for unavailable data.
class SteamFloatResult : public SteamResult {
    GDCLASS(SteamFloatResult, SteamResult)

public:
    // Return the cached floating-point value when the read succeeded.
    double get_value() const;

protected:
    // Bind the read-only float payload.
    static void _bind_methods();

private:
    friend class SteamStats;
    friend class SteamServerPlayerStats;

    // Construct a terminal read result with zero payload on failure.
    static godot::Ref<SteamFloatResult> create(godot::Error error, double value);

    // The cached floating-point value, meaningful when the result is OK.
    double value = 0.0;
};

// Detached Steam achievement metadata and unlock state for the signed-in player.
class SteamAchievement : public godot::RefCounted {
    GDCLASS(SteamAchievement, godot::RefCounted)

public:
    // Return the stable published API name used for lookups and unlock requests.
    godot::StringName get_api_name() const;
    // Return Steam's localized display name, falling back to the published API name.
    godot::String get_display_name() const;
    // Return Steam's localized description.
    godot::String get_description() const;
    // Report whether the app marks this achievement as hidden.
    bool is_hidden() const;
    // Report whether the signed-in player unlocked this achievement.
    bool is_unlocked() const;
    // Return the unlock time in Unix seconds, or zero when no timestamp is available.
    int64_t get_unlocked_at_unix() const;

protected:
    // Bind the read-only snapshot properties.
    static void _bind_methods();

private:
    friend class SteamStats;

    // The app's stable achievement identifier.
    godot::StringName api_name;
    // The localized name copied from Steam or the API identifier if omitted.
    godot::String display_name;
    // The localized description copied from Steam or an empty string if omitted.
    godot::String description;
    // Whether the app hides the achievement before unlock.
    bool hidden = false;
    // Whether the player had unlocked the achievement at snapshot time.
    bool unlocked = false;
    // Unlock time in Unix seconds, or zero when unavailable.
    int64_t unlocked_at_unix = 0;
};

// Counts draft edits accepted into the Steam cache before the submission returned.
class SteamStatsSubmission : public godot::RefCounted {
    GDCLASS(SteamStatsSubmission, godot::RefCounted)

public:
    // Return how many edits Steam accepted into its local cache.
    int64_t get_applied() const;
    // Return the zero-based rejected edit index, or -1 when no individual edit failed.
    int64_t get_failed_at() const;

protected:
    // Bind the read-only submission counters.
    static void _bind_methods();

private:
    friend class SteamStatsSubmissionResult;

    // Number of edits accepted by the local Steam setters.
    int64_t applied = 0;
    // Index of the rejected edit, or -1 for an outcome without an edit-level failure.
    int64_t failed_at = -1;
};

// Local stats submission outcome and edit counts, including partial failures.
class SteamStatsSubmissionResult : public SteamResult {
    GDCLASS(SteamStatsSubmissionResult, SteamResult)

public:
    // Return the counters associated with the draft outcome, including on failure.
    godot::Ref<SteamStatsSubmission> get_value() const;

protected:
    // Bind the read-only submission payload.
    static void _bind_methods();

private:
    friend class SteamStatsEdit;

    // Construct a terminal result with counts and a local submission outcome.
    static godot::Ref<SteamStatsSubmissionResult> create(godot::Error error, int64_t applied = 0,
            int64_t failed_at = -1, const godot::String &message = {});

    // Edit counters retained for any outcome produced by submit().
    godot::Ref<SteamStatsSubmission> value;
};

// Holds a branchable group of cache edits for one Steam stats service.
class SteamStatsEdit : public godot::RefCounted {
    GDCLASS(SteamStatsEdit, godot::RefCounted)

public:
    // Return a branch that sets a signed 32-bit integer stat.
    godot::Ref<SteamStatsEdit> set_int(const godot::String &name, int64_t value) const;
    // Return a branch that sets a finite single-precision stat.
    godot::Ref<SteamStatsEdit> set_float(const godot::String &name, double value) const;
    // Return a branch that adds a finite count and positive duration in seconds to an average-rate stat.
    godot::Ref<SteamStatsEdit> update_average_rate(const godot::String &name, double count, double seconds) const;
    // Return a branch that unlocks the named achievement.
    godot::Ref<SteamStatsEdit> unlock(const godot::String &name) const;
    // Apply prevalidated cache edits and request persistence once, returning local edit counts.
    godot::Ref<SteamStatsSubmissionResult> submit() const;

protected:
    // Register the draft modifiers and explicit submission action.
    static void _bind_methods();

private:
    friend class SteamStats;

    // A count accumulated over one positive interval for a Steam average-rate stat.
    struct AverageRate {
        // Count accumulated since the last update.
        double count;
        // Duration of the interval in seconds.
        double seconds;
    };

    // One ordered update to the Steam stats cache.
    struct Change {
        // Published API name of the affected stat or achievement.
        godot::String name;
        // Numeric stat update, or monostate to unlock the achievement.
        std::variant<int64_t, double, AverageRate, std::monostate> value;
    };

    // Return an independent copy with the supplied change appended.
    godot::Ref<SteamStatsEdit> append(Change change) const;

    // Weak identity of the service that created this draft.
    godot::ObjectID stats_id;
    // Changes are submitted in the order in which they were added.
    std::vector<Change> changes;
};

// Reads and updates the signed-in account's published Steam stat definitions.
class SteamStats : public godot::RefCounted {
    GDCLASS(SteamStats, godot::RefCounted)

public:
    // Subscribe to results for this client scope on the main thread.
    void start_scope(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Invalidate the service and remove its app-wide callback subscriptions.
    void close();
    // Report whether this scope has access to the Steam User Stats interface.
    bool is_available() const;
    // Begin an empty draft without modifying the Steam cache.
    godot::Ref<SteamStatsEdit> edit() const;
    // Read the named cached integer and report why the read failed when unavailable.
    godot::Ref<SteamIntResult> stat_int(const godot::String &name) const;
    // Read the named cached float and report why the read failed when unavailable.
    godot::Ref<SteamFloatResult> stat_float(const godot::String &name) const;
    // Set a signed 32-bit integer in Steam's local stats cache.
    godot::Error set_stat_int(const godot::String &name, int64_t value);
    // Set a finite single-precision value in Steam's local stats cache.
    godot::Error set_stat_float(const godot::String &name, double value);
    // Add a finite count and positive session duration in seconds to an average-rate stat.
    godot::Error update_average_rate(const godot::String &name, double count, double seconds);
    // Return typed snapshots of published achievements readable from the Steam cache.
    godot::TypedArray<SteamAchievement> achievements() const;
    // Return the cached achievement snapshot, or null if the name or cache is unavailable.
    godot::Ref<SteamAchievement> achievement(const godot::StringName &api_name) const;
    // Mark an achievement unlocked in the local cache.
    godot::Error unlock_achievement(const godot::String &name);
    // Reset an achievement in the local cache for a controlled app or test account.
    godot::Error clear_achievement(const godot::String &name);
    // Request Steam's progress popup without changing the cached unlock state.
    godot::Error show_progress(const godot::String &name, int64_t current, int64_t maximum);
    // Submit changed stats and achievements. Backend results are app-wide signals.
    godot::Error store();

protected:
    // Register stats methods and the two app-wide Steam result signals.
    static void _bind_methods();

private:
    friend class SteamStatsEdit;

    // Resolves native cache readers for one scoped achievement query.
    struct AchievementReader;

    // Acquire the runtime on its owning thread if the service is still active.
    std::shared_ptr<SteamRuntime> active_runtime() const;

    // Observe the client runtime without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Owner liveness is invalidated before any service emits its cancellation events.
    std::shared_ptr<SteamClientScope> client_scope;
    // Runtime observer for the app-wide UserStatsStored event.
    uint64_t stored_subscription = 0;
    // Runtime observer for the app-wide UserAchievementStored event.
    uint64_t achievement_subscription = 0;
};

} // namespace crimson::steam
