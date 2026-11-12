// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
class SteamTimeline;

// Shared display properties for a new or updated Timeline marker.
class TimelineMarkerPresentation : public godot::RefCounted {
    GDCLASS(TimelineMarkerPresentation, godot::RefCounted)

public:
    // Set the localized description.
    void set_description(const godot::String &value);
    // Return the optional localized description.
    godot::String get_description() const;
    // Select a built-in Steam icon or an icon configured by the game.
    void set_icon(const godot::String &value);
    // Return the selected icon name.
    godot::String get_icon() const;
    // Set the visual priority, validated when the marker is submitted.
    void set_priority(int64_t value);
    // Return the visual priority from 0 through 1000 for valid submissions.
    int64_t get_priority() const;
    // Choose whether Steam may suggest a clip of this event.
    void set_clip(int64_t value);
    // Return the SteamTimeline clip priority.
    int64_t get_clip() const;

protected:
    // Register the configurable display properties with Godot.
    static void _bind_methods();

private:
    // Display text below the marker title.
    godot::String description;
    // Built-in marker icon unless overridden by the caller.
    godot::String icon = "steam_marker";
    // Marker priority, with the lowest priority as the default.
    int64_t priority = 0;
    // SteamTimeline::NONE until the caller opts into a suggestion.
    int64_t clip = 1;
};

// Adds a relative start time to the marker display options.
class TimelineMarkerOptions : public TimelineMarkerPresentation {
    GDCLASS(TimelineMarkerOptions, TimelineMarkerPresentation)

public:
    // Set the start relative to the moment the event is submitted.
    void set_offset_seconds(double value);
    // Return the relative start offset in seconds.
    double get_offset_seconds() const;

protected:
    // Register the relative start offset with Godot.
    static void _bind_methods();

private:
    // Negative values place a marker in the past.
    double offset_seconds = 0.0;
};

// Owns the native handle of one event created by a Timeline facade.
class SteamTimelineEvent : public godot::RefCounted {
    GDCLASS(SteamTimelineEvent, godot::RefCounted)

public:
    // Report whether this event still belongs to a running scope and has not been removed.
    bool is_valid() const;
    // Report whether this event is a range awaiting its end time.
    bool is_open() const;

protected:
    // Register the event's state queries with Godot.
    static void _bind_methods();

private:
    friend class SteamTimeline;

    // Exact facade that submitted the event.
    godot::ObjectID owner;
    // Scope lifetime shared with other Steam services for the same client start.
    std::weak_ptr<SteamClientScope> scope;
    // Full native TimelineEventHandle_t. Zero is reserved for an invalid event.
    uint64_t handle = 0;
    // Whether Steam is still waiting for the end of this range.
    bool open = false;
};

// Reports whether Steam accepted a new Timeline event.
class SteamTimelineEventResult : public SteamResult {
    GDCLASS(SteamTimelineEventResult, SteamResult)

public:
    // Return the submitted event on success.
    godot::Ref<SteamTimelineEvent> get_value() const;

protected:
    // Register the read-only event payload.
    static void _bind_methods();

private:
    friend class SteamTimeline;
    // Create a submission result without exposing a native SDK handle.
    static godot::Ref<SteamTimelineEventResult> create(godot::Error error,
            const godot::Ref<SteamTimelineEvent> &event = {});
    // Script-facing event owned by this result.
    godot::Ref<SteamTimelineEvent> value;
};

// Observes one Steam query for video associated with an event.
class SteamTimelineOperation : public godot::RefCounted {
    GDCLASS(SteamTimelineOperation, godot::RefCounted)

public:
    // Report whether the query reached a terminal result.
    bool is_finished() const;
    // Return the typed answer, or null while the query is pending.
    godot::Ref<SteamBoolResult> result() const;
    // Return this result immediately if complete, or its completion signal.
    godot::Variant wait();
    // Stop observing the query and settle it with ERR_SKIP.
    godot::Error cancel();

protected:
    // Register the operation's methods and signal with Godot.
    static void _bind_methods();

private:
    friend class SteamTimeline;

    // Settle exactly once after the native observation has been retired.
    void finish(godot::Error result, bool exists = false);

    // Facade responsible for cancelling the pending call.
    godot::ObjectID owner;
    // Exact SteamAPICall_t associated with this pending result.
    uint64_t call = 0;
    // Settled answer, also emitted through completed.
    godot::Ref<SteamBoolResult> outcome;
};

// Immutable footage and capture counts for a single game phase.
class TimelinePhaseRecording : public godot::RefCounted {
    GDCLASS(TimelinePhaseRecording, godot::RefCounted)

public:
    // Return the total available background recording in milliseconds.
    int64_t get_recording_ms() const;
    // Return the duration of the longest saved clip in milliseconds.
    int64_t get_longest_clip_ms() const;
    // Return the number of saved clips.
    int64_t get_clip_count() const;
    // Return the number of screenshots linked to the phase.
    int64_t get_screenshot_count() const;

protected:
    // Register the read-only footage and capture counts.
    static void _bind_methods();

private:
    friend class SteamTimeline;
    // Length of the background recording in milliseconds.
    int64_t recording_ms = 0;
    // Length of the longest clip in milliseconds.
    int64_t longest_clip_ms = 0;
    // Number of saved clips.
    int64_t clip_count = 0;
    // Number of saved screenshots.
    int64_t screenshot_count = 0;
};

// Reports the recording available for a game-owned phase ID.
class SteamTimelinePhaseResult : public SteamResult {
    GDCLASS(SteamTimelinePhaseResult, SteamResult)

public:
    // Return the phase footage snapshot on success.
    godot::Ref<TimelinePhaseRecording> get_value() const;

protected:
    // Register the read-only phase footage payload.
    static void _bind_methods();

private:
    friend class SteamTimeline;
    // Create a settled query result.
    static godot::Ref<SteamTimelinePhaseResult> create(godot::Error error,
            const godot::Ref<TimelinePhaseRecording> &recording = {});
    // Phase footage reported by Steam, including valid all-zero snapshots.
    godot::Ref<TimelinePhaseRecording> value;
};

// Carries the typed phase answer for the immediate await API.
class SteamTimelinePhaseOperation : public godot::RefCounted {
    GDCLASS(SteamTimelinePhaseOperation, godot::RefCounted)

protected:
    // Register the completion signal with its typed result.
    static void _bind_methods();
};

// Submits local game-state and event markers to Steam Game Recording.
class SteamTimeline : public godot::RefCounted {
    GDCLASS(SteamTimeline, godot::RefCounted)

public:
    // Steam's visual distinction between types of game activity.
    enum GameMode {
        PLAYING = 1,
        STAGING,
        MENUS,
        LOADING_SCREEN,
    };

    // Whether Steam may offer the marked event as a suggested clip.
    enum ClipPriority {
        NONE = 1,
        STANDARD,
        FEATURED,
    };

    // Report whether this scope exposes the Steam Timeline interface.
    bool is_available() const;
    // Change the process-wide Timeline game mode.
    godot::Error set_game_mode(GameMode mode);
    // Set the process-wide Timeline state description at a relative time.
    godot::Error set_tooltip(const godot::String &description, double offset_seconds = 0.0);
    // Clear the process-wide Timeline state description at a relative time.
    godot::Error clear_tooltip(double offset_seconds = 0.0);
    // Mark one instant with optional display and timing settings.
    godot::Ref<SteamTimelineEventResult> mark(const godot::String &title,
            const godot::Ref<TimelineMarkerOptions> &options = {});
    // Mark a known positive duration in seconds.
    godot::Ref<SteamTimelineEventResult> mark_range(const godot::String &title, double duration_seconds,
            const godot::Ref<TimelineMarkerOptions> &options = {});
    // Begin a range and return an event that the caller must end or remove.
    godot::Ref<SteamTimelineEventResult> start_range(const godot::String &title,
            const godot::Ref<TimelineMarkerOptions> &options = {});
    // Replace the display details of an open range owned by this scope.
    godot::Error update_range(const godot::Ref<SteamTimelineEvent> &event, const godot::String &title,
            const godot::Ref<TimelineMarkerPresentation> &presentation = {});
    // End an open range so its marker can be displayed by Steam.
    godot::Error end_range(const godot::Ref<SteamTimelineEvent> &event, double offset_seconds = 0.0);
    // Remove a submitted event or discard an open range.
    godot::Error remove_event(const godot::Ref<SteamTimelineEvent> &event);
    // Check whether Steam has video for a completed event owned by this scope.
    godot::Variant recording_exists(const godot::Ref<SteamTimelineEvent> &event);
    // Return an awaitable and cancellable observation for this event's footage.
    godot::Ref<SteamTimelineOperation> recording_operation(const godot::Ref<SteamTimelineEvent> &event);
    // Ask an enabled Steam overlay to open the completed event's recording.
    godot::Error open_in_overlay(const godot::Ref<SteamTimelineEvent> &event);
    // Start the only active phase in this client process with a stable game ID.
    godot::Error start_phase(const godot::String &phase_id);
    // Replace the display text for an attribute group in this scope's active phase.
    godot::Error set_phase_attribute(const godot::String &group, const godot::String &value, int64_t priority = 0);
    // Add a localized tag to this scope's active phase.
    godot::Error add_phase_tag(const godot::String &name, const godot::String &icon,
            const godot::String &group, int64_t priority = 0);
    // Close the active phase owned by this scope.
    godot::Error end_phase();
    // Await a typed recording snapshot for a stable phase ID.
    godot::Variant phase_recording(const godot::String &phase_id);
    // Ask an enabled Steam overlay to open a previously recorded phase.
    godot::Error open_phase_in_overlay(const godot::String &phase_id);

protected:
    // Register the Timeline facade with Godot.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamTimelineOperation;

    // Select the native submission method while sharing validation.
    enum class EventKind { Instant, FixedRange, OpenRange };

    // Native observation for one recording query.
    struct PendingQuery {
        // Exact callback interpreter retaining its own script-facing observation.
        std::function<void(godot::Error, const void *)> complete;
        // Monotonic deadline after which the query settles locally.
        uint64_t deadline_msec = 0;
    };

    // Bind this facade to a fresh client start.
    void configure(const std::shared_ptr<SteamRuntime> &steam_runtime,
            const std::shared_ptr<SteamClientScope> &client_scope);
    // Retire unfinished ranges and unwatch all queries before signalling cancellation.
    void close();
    // Expire any queries that have reached their local deadline.
    void tick();
    // Acquire a usable runtime while the main-thread client scope remains active.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Confirm ownership and validity before accepting an event handle.
    bool owns_event(const godot::Ref<SteamTimelineEvent> &event) const;
    // Validate common event arguments and submit the appropriate SDK call.
    godot::Ref<SteamTimelineEventResult> submit_event(EventKind kind, const godot::String &title,
            double duration_seconds, const godot::Ref<TimelineMarkerOptions> &options);
    // Retain a recording observer for one exact callback payload and deadline.
    bool watch_recording(const std::shared_ptr<SteamRuntime> &active, uint64_t call, int callback_id,
            size_t size, std::function<void(godot::Error, const void *)> complete);
    // Remove a native observation and settle its operation.
    godot::Error finish_query(uint64_t call, godot::Error error, const void *record = nullptr);
    // Release a phase owned by this facade, including during client teardown.
    godot::Error retire_phase(const std::shared_ptr<SteamRuntime> &active);

    // Process Steam runtime, retained by the owning client.
    std::weak_ptr<SteamRuntime> runtime;
    // Shared gate that becomes inactive before any service emits cancellation.
    std::shared_ptr<SteamClientScope> scope;
    // Unfinished ranges kept until the caller ends them or the scope closes.
    std::map<uint64_t, godot::Ref<SteamTimelineEvent>> open_ranges;
    // Observed native recording calls keyed by the exact SteamAPICall_t.
    std::map<uint64_t, PendingQuery> pending_queries;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamTimeline::GameMode);
VARIANT_ENUM_CAST(crimson::steam::SteamTimeline::ClipPriority);
