// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_timeline.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Maximum UTF-8 bytes for a submitted title, description, or tooltip.
constexpr int64_t maximum_text_bytes = 1024;
// Maximum UTF-8 bytes for an icon name.
constexpr int64_t maximum_icon_bytes = 128;
// Keep abandoned open ranges bounded until the scope stops.
constexpr size_t maximum_open_ranges = 64;
// The local deadline for receiving Steam's recording lookup result.
constexpr uint64_t recording_query_timeout_msec = 15000;

static_assert(int(SteamTimeline::PLAYING) == int(k_ETimelineGameMode_Playing));
static_assert(int(SteamTimeline::STAGING) == int(k_ETimelineGameMode_Staging));
static_assert(int(SteamTimeline::MENUS) == int(k_ETimelineGameMode_Menus));
static_assert(int(SteamTimeline::LOADING_SCREEN) == int(k_ETimelineGameMode_LoadingScreen));
static_assert(int(SteamTimeline::NONE) == int(k_ETimelineEventClipPriority_None));
static_assert(int(SteamTimeline::STANDARD) == int(k_ETimelineEventClipPriority_Standard));
static_assert(int(SteamTimeline::FEATURED) == int(k_ETimelineEventClipPriority_Featured));

// Resolve Steam's optional Timeline interface without enabling other capabilities.
ISteamTimeline *get_timeline(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamTimeline_v004)>(
                                       "SteamAPI_SteamTimeline_v004")
                             : nullptr;
    return get ? get() : nullptr;
}

// Retain the exact issuing facade while invoking a recording callback or cancellation.
Ref<SteamTimeline> find_timeline(ObjectID owner) {
    return Ref<SteamTimeline>(Object::cast_to<SteamTimeline>(ObjectDB::get_instance(owner)));
}

// Accept only finite offsets representable by Steam's float arguments.
bool valid_offset(double seconds) {
    return std::isfinite(seconds) && std::abs(seconds) <= (std::numeric_limits<float>::max)();
}

// Reject overlong text and embedded nulls before passing data to Steam.
bool valid_text(const CharString &text, int64_t limit, bool allow_empty = false) {
    const int64_t length = text.length();
    return (allow_empty || length > 0) && length <= limit &&
            (length == 0 || std::memchr(text.get_data(), 0, static_cast<size_t>(length)) == nullptr);
}

// Match the bounded NUL-terminated phase ID field returned by Steam.
bool valid_phase_id(const CharString &id) {
    if (!valid_text(id, k_cchMaxPhaseIDLength - 1)) {
        return false;
    }
    for (int64_t index = 0; index < id.length(); ++index) {
        const auto byte = static_cast<unsigned char>(id[index]);
        if (byte < 0x20 || byte == 0x7f) {
            return false;
        }
    }
    return true;
}

// Query the Steam overlay after confirming that its interface and symbol exist.
Error overlay_ready(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto ready = runtime->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
            "SteamAPI_ISteamUtils_IsOverlayEnabled");
    if (!ready || !runtime->get_utils()) {
        return ERR_UNAVAILABLE;
    }
    return ready(runtime->get_utils()) ? OK : ERR_BUSY;
}

// Own the UTF-8 arguments for the duration of a native event call.
struct EventText {
    // Nonempty localized display title.
    CharString title;
    // Optional localized description.
    CharString description;
    // Built-in or configured Steam icon name.
    CharString icon;

    // Steam's display order after validating the input range.
    int64_t priority;
    // Clip suggestion policy, validated before narrowing to the SDK enum.
    int64_t clip;

    // Snapshot the caller's options before passing their text to Steam.
    EventText(const String &event_title, const Ref<TimelineMarkerPresentation> &presentation) :
            title(event_title.utf8()),
            description(presentation.is_valid() ? presentation->get_description().utf8() : CharString()),
            icon(presentation.is_valid() ? presentation->get_icon().utf8() : String("steam_marker").utf8()),
            priority(presentation.is_valid() ? presentation->get_priority() : 0),
            clip(presentation.is_valid() ? presentation->get_clip() : SteamTimeline::NONE) {}

    // Verify all text and numeric values common to event edits.
    bool is_valid() const {
        return valid_text(title, maximum_text_bytes) && valid_text(description, maximum_text_bytes, true) &&
                valid_text(icon, maximum_icon_bytes) && priority >= 0 && priority <= k_unMaxTimelinePriority &&
                clip >= SteamTimeline::NONE && clip <= SteamTimeline::FEATURED;
    }
};

} // namespace
#endif

void TimelineMarkerPresentation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_description", "value"), &TimelineMarkerPresentation::set_description);
    ClassDB::bind_method(D_METHOD("get_description"), &TimelineMarkerPresentation::get_description);
    ClassDB::bind_method(D_METHOD("set_icon", "value"), &TimelineMarkerPresentation::set_icon);
    ClassDB::bind_method(D_METHOD("get_icon"), &TimelineMarkerPresentation::get_icon);
    ClassDB::bind_method(D_METHOD("set_priority", "value"), &TimelineMarkerPresentation::set_priority);
    ClassDB::bind_method(D_METHOD("get_priority"), &TimelineMarkerPresentation::get_priority);
    ClassDB::bind_method(D_METHOD("set_clip", "value"), &TimelineMarkerPresentation::set_clip);
    ClassDB::bind_method(D_METHOD("get_clip"), &TimelineMarkerPresentation::get_clip);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "description"), "set_description", "get_description");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "icon"), "set_icon", "get_icon");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "0,1000,1"),
            "set_priority", "get_priority");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "clip", PROPERTY_HINT_ENUM, "None:1,Standard:2,Featured:3"),
            "set_clip", "get_clip");
}

void TimelineMarkerPresentation::set_description(const String &value) {
    description = value;
}

String TimelineMarkerPresentation::get_description() const {
    return description;
}

void TimelineMarkerPresentation::set_icon(const String &value) {
    icon = value;
}

String TimelineMarkerPresentation::get_icon() const {
    return icon;
}

void TimelineMarkerPresentation::set_priority(int64_t value) {
    priority = value;
}

int64_t TimelineMarkerPresentation::get_priority() const {
    return priority;
}

void TimelineMarkerPresentation::set_clip(int64_t value) {
    clip = value;
}

int64_t TimelineMarkerPresentation::get_clip() const {
    return clip;
}

void TimelineMarkerOptions::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_offset_seconds", "value"), &TimelineMarkerOptions::set_offset_seconds);
    ClassDB::bind_method(D_METHOD("get_offset_seconds"), &TimelineMarkerOptions::get_offset_seconds);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_seconds"), "set_offset_seconds", "get_offset_seconds");
}

void TimelineMarkerOptions::set_offset_seconds(double value) {
    offset_seconds = value;
}

double TimelineMarkerOptions::get_offset_seconds() const {
    return offset_seconds;
}

void SteamTimelineEvent::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &SteamTimelineEvent::is_valid);
    ClassDB::bind_method(D_METHOD("is_open"), &SteamTimelineEvent::is_open);
}

bool SteamTimelineEvent::is_valid() const {
    const auto active = scope.lock();
    return handle != 0 && active && active->active;
}

bool SteamTimelineEvent::is_open() const {
    return open && is_valid();
}

void SteamTimelineEventResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamTimelineEventResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamTimelineEvent",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamTimelineEvent> SteamTimelineEventResult::get_value() const {
    return value;
}

Ref<SteamTimelineEventResult> SteamTimelineEventResult::create(Error error, const Ref<SteamTimelineEvent> &event) {
    Ref<SteamTimelineEventResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = event;
    return result;
}

void SteamTimelineOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamTimelineOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamTimelineOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamTimelineOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamTimelineOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamBoolResult")));
}

bool SteamTimelineOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamBoolResult> SteamTimelineOperation::result() const {
    return outcome;
}

Variant SteamTimelineOperation::wait() {
    return is_finished() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamTimelineOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto timeline = call != 0 ? find_timeline(owner) : Ref<SteamTimeline>();
    if (timeline.is_valid()) {
        return timeline->finish_query(call, ERR_SKIP);
    }
#endif
    return ERR_UNAVAILABLE;
}

void SteamTimelineOperation::finish(Error result, bool exists) {
    if (is_finished()) {
        return;
    }
    const Ref<SteamTimelineOperation> self(this);
    owner = ObjectID();
    call = 0;
    outcome = SteamBoolResult::create(result, exists);
    emit_signal("completed", outcome);
}

void TimelinePhaseRecording::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_recording_ms"), &TimelinePhaseRecording::get_recording_ms);
    ClassDB::bind_method(D_METHOD("get_longest_clip_ms"), &TimelinePhaseRecording::get_longest_clip_ms);
    ClassDB::bind_method(D_METHOD("get_clip_count"), &TimelinePhaseRecording::get_clip_count);
    ClassDB::bind_method(D_METHOD("get_screenshot_count"), &TimelinePhaseRecording::get_screenshot_count);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "recording_ms", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_recording_ms");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "longest_clip_ms", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_longest_clip_ms");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "clip_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_clip_count");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "screenshot_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_screenshot_count");
}

int64_t TimelinePhaseRecording::get_recording_ms() const {
    return recording_ms;
}

int64_t TimelinePhaseRecording::get_longest_clip_ms() const {
    return longest_clip_ms;
}

int64_t TimelinePhaseRecording::get_clip_count() const {
    return clip_count;
}

int64_t TimelinePhaseRecording::get_screenshot_count() const {
    return screenshot_count;
}

void SteamTimelinePhaseResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamTimelinePhaseResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "TimelinePhaseRecording",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<TimelinePhaseRecording> SteamTimelinePhaseResult::get_value() const {
    return value;
}

Ref<SteamTimelinePhaseResult> SteamTimelinePhaseResult::create(
        Error error, const Ref<TimelinePhaseRecording> &recording) {
    Ref<SteamTimelinePhaseResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = recording;
    return result;
}

void SteamTimelinePhaseOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamTimelinePhaseResult")));
}

void SteamTimeline::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamTimeline::is_available);
    ClassDB::bind_method(D_METHOD("set_game_mode", "mode"), &SteamTimeline::set_game_mode);
    ClassDB::bind_method(D_METHOD("set_tooltip", "description", "offset_seconds"),
            &SteamTimeline::set_tooltip, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("clear_tooltip", "offset_seconds"),
            &SteamTimeline::clear_tooltip, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("mark", "title", "options"), &SteamTimeline::mark, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("mark_range", "title", "duration_seconds", "options"),
            &SteamTimeline::mark_range, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("start_range", "title", "options"), &SteamTimeline::start_range, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("update_range", "event", "title", "presentation"),
            &SteamTimeline::update_range, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("end_range", "event", "offset_seconds"), &SteamTimeline::end_range, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("remove_event", "event"), &SteamTimeline::remove_event);
    ClassDB::bind_method(D_METHOD("recording_exists", "event"), &SteamTimeline::recording_exists);
    ClassDB::bind_method(D_METHOD("recording_operation", "event"), &SteamTimeline::recording_operation);
    ClassDB::bind_method(D_METHOD("open_in_overlay", "event"), &SteamTimeline::open_in_overlay);
    ClassDB::bind_method(D_METHOD("start_phase", "phase_id"), &SteamTimeline::start_phase);
    ClassDB::bind_method(D_METHOD("set_phase_attribute", "group", "value", "priority"),
            &SteamTimeline::set_phase_attribute, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("add_phase_tag", "name", "icon", "group", "priority"),
            &SteamTimeline::add_phase_tag, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("end_phase"), &SteamTimeline::end_phase);
    ClassDB::bind_method(D_METHOD("phase_recording", "phase_id"), &SteamTimeline::phase_recording);
    ClassDB::bind_method(D_METHOD("open_phase_in_overlay", "phase_id"), &SteamTimeline::open_phase_in_overlay);

    BIND_ENUM_CONSTANT(PLAYING);
    BIND_ENUM_CONSTANT(STAGING);
    BIND_ENUM_CONSTANT(MENUS);
    BIND_ENUM_CONSTANT(LOADING_SCREEN);
    BIND_ENUM_CONSTANT(NONE);
    BIND_ENUM_CONSTANT(STANDARD);
    BIND_ENUM_CONSTANT(FEATURED);
}

void SteamTimeline::configure(const std::shared_ptr<SteamRuntime> &steam_runtime,
        const std::shared_ptr<SteamClientScope> &client_scope) {
    runtime = steam_runtime;
    scope = client_scope;
}

std::shared_ptr<SteamRuntime> SteamTimeline::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && scope && scope->active) {
        return runtime.lock();
    }
#endif
    return {};
}

bool SteamTimeline::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return get_timeline(active) != nullptr;
#else
    return false;
#endif
}

Error SteamTimeline::set_game_mode(GameMode mode) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        if (mode < PLAYING || mode > LOADING_SCREEN) {
            return ERR_INVALID_PARAMETER;
        }
        const auto set = active->symbol<decltype(&SteamAPI_ISteamTimeline_SetTimelineGameMode)>(
                "SteamAPI_ISteamTimeline_SetTimelineGameMode");
        if (set) {
            set(timeline, static_cast<ETimelineGameMode>(mode));
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::set_tooltip(const String &description, double offset_seconds) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        const CharString text = description.utf8();
        if (!valid_text(text, maximum_text_bytes) || !valid_offset(offset_seconds)) {
            return ERR_INVALID_PARAMETER;
        }
        const auto set = active->symbol<decltype(&SteamAPI_ISteamTimeline_SetTimelineTooltip)>(
                "SteamAPI_ISteamTimeline_SetTimelineTooltip");
        if (set) {
            set(timeline, text.get_data(), static_cast<float>(offset_seconds));
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::clear_tooltip(double offset_seconds) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        if (!valid_offset(offset_seconds)) {
            return ERR_INVALID_PARAMETER;
        }
        const auto clear = active->symbol<decltype(&SteamAPI_ISteamTimeline_ClearTimelineTooltip)>(
                "SteamAPI_ISteamTimeline_ClearTimelineTooltip");
        if (clear) {
            clear(timeline, static_cast<float>(offset_seconds));
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Ref<SteamTimelineEventResult> SteamTimeline::mark(const String &title, const Ref<TimelineMarkerOptions> &options) {
    return submit_event(EventKind::Instant, title, 0.0, options);
}

Ref<SteamTimelineEventResult> SteamTimeline::mark_range(
        const String &title, double duration_seconds, const Ref<TimelineMarkerOptions> &options) {
    return submit_event(EventKind::FixedRange, title, duration_seconds, options);
}

Ref<SteamTimelineEventResult> SteamTimeline::start_range(
        const String &title, const Ref<TimelineMarkerOptions> &options) {
    return submit_event(EventKind::OpenRange, title, 0.0, options);
}

Ref<SteamTimelineEventResult> SteamTimeline::submit_event(
        EventKind kind, const String &title, double duration_seconds, const Ref<TimelineMarkerOptions> &options) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (!timeline) {
        return SteamTimelineEventResult::create(ERR_UNAVAILABLE);
    }

    const bool open = kind == EventKind::OpenRange;
    const double offset_seconds = options.is_valid() ? options->get_offset_seconds() : 0.0;
    const EventText text(title, options);
    if (!text.is_valid() || !valid_offset(offset_seconds) ||
            (kind == EventKind::FixedRange &&
                    (!std::isfinite(duration_seconds) || duration_seconds <= 0 ||
                            duration_seconds > k_flMaxTimelineEventDuration ||
                            static_cast<float>(duration_seconds) == 0))) {
        return SteamTimelineEventResult::create(ERR_INVALID_PARAMETER);
    }
    if (open && open_ranges.size() >= maximum_open_ranges) {
        return SteamTimelineEventResult::create(ERR_BUSY);
    }

    const auto weight = static_cast<uint32_t>(text.priority);
    const auto offset = static_cast<float>(offset_seconds);
    const auto suggestion = static_cast<ETimelineEventClipPriority>(text.clip);
    uint64_t handle = 0;
    switch (kind) {
        case EventKind::Instant: {
            const auto add = active->symbol<decltype(&SteamAPI_ISteamTimeline_AddInstantaneousTimelineEvent)>(
                    "SteamAPI_ISteamTimeline_AddInstantaneousTimelineEvent");
            if (!add) {
                return SteamTimelineEventResult::create(ERR_UNAVAILABLE);
            }
            handle = add(timeline, text.title.get_data(), text.description.get_data(), text.icon.get_data(),
                    weight, offset, suggestion);
        } break;
        case EventKind::FixedRange: {
            const auto add = active->symbol<decltype(&SteamAPI_ISteamTimeline_AddRangeTimelineEvent)>(
                    "SteamAPI_ISteamTimeline_AddRangeTimelineEvent");
            if (!add) {
                return SteamTimelineEventResult::create(ERR_UNAVAILABLE);
            }
            handle = add(timeline, text.title.get_data(), text.description.get_data(), text.icon.get_data(),
                    weight, offset, static_cast<float>(duration_seconds), suggestion);
        } break;
        case EventKind::OpenRange: {
            const auto start = active->symbol<decltype(&SteamAPI_ISteamTimeline_StartRangeTimelineEvent)>(
                    "SteamAPI_ISteamTimeline_StartRangeTimelineEvent");
            if (!start) {
                return SteamTimelineEventResult::create(ERR_UNAVAILABLE);
            }
            handle = start(timeline, text.title.get_data(), text.description.get_data(), text.icon.get_data(),
                    weight, offset, suggestion);
        } break;
    }
    if (handle == 0) {
        return SteamTimelineEventResult::create(FAILED);
    }

    Ref<SteamTimelineEvent> event;
    event.instantiate();
    event->owner = ObjectID(get_instance_id());
    event->scope = scope;
    event->handle = handle;
    event->open = open;
    if (open) {
        open_ranges.emplace(handle, event);
    }
    return SteamTimelineEventResult::create(OK, event);
#else
    return SteamTimelineEventResult::create(ERR_UNAVAILABLE);
#endif
}

bool SteamTimeline::owns_event(const Ref<SteamTimelineEvent> &event) const {
    return event.is_valid() && event->owner == ObjectID(get_instance_id()) && event->is_valid();
}

Error SteamTimeline::update_range(const Ref<SteamTimelineEvent> &event, const String &title,
        const Ref<TimelineMarkerPresentation> &presentation) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        const EventText text(title, presentation);
        if (!owns_event(event) || !event->open || !text.is_valid()) {
            return ERR_INVALID_PARAMETER;
        }
        const auto update = active->symbol<decltype(&SteamAPI_ISteamTimeline_UpdateRangeTimelineEvent)>(
                "SteamAPI_ISteamTimeline_UpdateRangeTimelineEvent");
        if (update) {
            update(timeline, event->handle, text.title.get_data(), text.description.get_data(), text.icon.get_data(),
                    static_cast<uint32_t>(text.priority), static_cast<ETimelineEventClipPriority>(text.clip));
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::end_range(const Ref<SteamTimelineEvent> &event, double offset_seconds) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        if (!owns_event(event) || !event->open || !valid_offset(offset_seconds)) {
            return ERR_INVALID_PARAMETER;
        }
        const auto end = active->symbol<decltype(&SteamAPI_ISteamTimeline_EndRangeTimelineEvent)>(
                "SteamAPI_ISteamTimeline_EndRangeTimelineEvent");
        if (end) {
            end(timeline, event->handle, static_cast<float>(offset_seconds));
            event->open = false;
            open_ranges.erase(event->handle);
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::remove_event(const Ref<SteamTimelineEvent> &event) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        if (!owns_event(event)) {
            return ERR_INVALID_PARAMETER;
        }
        const auto remove = active->symbol<decltype(&SteamAPI_ISteamTimeline_RemoveTimelineEvent)>(
                "SteamAPI_ISteamTimeline_RemoveTimelineEvent");
        if (remove) {
            remove(timeline, event->handle);
            open_ranges.erase(event->handle);
            event->handle = 0;
            event->open = false;
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Variant SteamTimeline::recording_exists(const Ref<SteamTimelineEvent> &event) {
    return recording_operation(event)->wait();
}

Ref<SteamTimelineOperation> SteamTimeline::recording_operation(const Ref<SteamTimelineEvent> &event) {
    Ref<SteamTimelineOperation> operation;
    operation.instantiate();
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        if (!owns_event(event) || event->open) {
            operation->finish(ERR_INVALID_PARAMETER);
            return operation;
        }
        const auto query = active->symbol<decltype(&SteamAPI_ISteamTimeline_DoesEventRecordingExist)>(
                "SteamAPI_ISteamTimeline_DoesEventRecordingExist");
        if (query) {
            if (!active->can_track_call()) {
                operation->finish(ERR_BUSY);
                return operation;
            }
            const uint64_t event_handle = event->handle;
            const uint64_t call = query(timeline, event_handle);
            if (watch_recording(active, call, SteamTimelineEventRecordingExists_t::k_iCallback,
                        sizeof(SteamTimelineEventRecordingExists_t), [operation, event_handle](Error status,
                                const void *record) {
                            SteamTimelineEventRecordingExists_t answer{};
                            if (status == OK) {
                                std::memcpy(&answer, record, sizeof(answer));
                                if (answer.m_ulEventID != event_handle) {
                                    status = FAILED;
                                }
                            }
                            operation->finish(status, answer.m_bRecordingExists);
                        })) {
                operation->owner = ObjectID(get_instance_id());
                operation->call = call;
            } else {
                operation->finish(FAILED);
            }
            return operation;
        }
    }
#endif
    operation->finish(ERR_UNAVAILABLE);
    return operation;
}

Error SteamTimeline::open_in_overlay(const Ref<SteamTimelineEvent> &event) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        if (!owns_event(event) || event->open) {
            return ERR_INVALID_PARAMETER;
        }
        const auto open = active->symbol<decltype(&SteamAPI_ISteamTimeline_OpenOverlayToTimelineEvent)>(
                "SteamAPI_ISteamTimeline_OpenOverlayToTimelineEvent");
        if (open) {
            const Error ready = overlay_ready(active);
            if (ready != OK) {
                return ready;
            }
            open(timeline, event->handle);
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::start_phase(const String &phase_id) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (!timeline) {
        return ERR_UNAVAILABLE;
    }
    const CharString id = phase_id.utf8();
    if (!valid_phase_id(id)) {
        return ERR_INVALID_PARAMETER;
    }
    if (active->timeline_phase_owner != 0) {
        return ERR_BUSY;
    }
    const auto start = active->symbol<decltype(&SteamAPI_ISteamTimeline_StartGamePhase)>(
            "SteamAPI_ISteamTimeline_StartGamePhase");
    const auto set_id = active->symbol<decltype(&SteamAPI_ISteamTimeline_SetGamePhaseID)>(
            "SteamAPI_ISteamTimeline_SetGamePhaseID");
    const auto end = active->symbol<decltype(&SteamAPI_ISteamTimeline_EndGamePhase)>(
            "SteamAPI_ISteamTimeline_EndGamePhase");
    if (start && set_id && end) {
        active->timeline_phase_owner = get_instance_id();
        start(timeline);
        set_id(timeline, id.get_data());
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::set_phase_attribute(const String &group, const String &value, int64_t priority) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (!timeline) {
        return ERR_UNAVAILABLE;
    }
    if (active->timeline_phase_owner != get_instance_id()) {
        return ERR_UNCONFIGURED;
    }
    const CharString attribute_group = group.utf8();
    const CharString attribute_value = value.utf8();
    if (!valid_text(attribute_group, maximum_text_bytes) || !valid_text(attribute_value, maximum_text_bytes) ||
            priority < 0 || priority > k_unMaxTimelinePriority) {
        return ERR_INVALID_PARAMETER;
    }
    const auto set = active->symbol<decltype(&SteamAPI_ISteamTimeline_SetGamePhaseAttribute)>(
            "SteamAPI_ISteamTimeline_SetGamePhaseAttribute");
    if (set) {
        set(timeline, attribute_group.get_data(), attribute_value.get_data(), static_cast<uint32_t>(priority));
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::add_phase_tag(const String &name, const String &icon, const String &group, int64_t priority) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (!timeline) {
        return ERR_UNAVAILABLE;
    }
    if (active->timeline_phase_owner != get_instance_id()) {
        return ERR_UNCONFIGURED;
    }
    const CharString tag_name = name.utf8();
    const CharString tag_icon = icon.utf8();
    const CharString tag_group = group.utf8();
    if (!valid_text(tag_name, maximum_text_bytes) || !valid_text(tag_icon, maximum_icon_bytes) ||
            !valid_text(tag_group, maximum_text_bytes, true) || priority < 0 || priority > k_unMaxTimelinePriority) {
        return ERR_INVALID_PARAMETER;
    }
    const auto add = active->symbol<decltype(&SteamAPI_ISteamTimeline_AddGamePhaseTag)>(
            "SteamAPI_ISteamTimeline_AddGamePhaseTag");
    if (add) {
        add(timeline, tag_name.get_data(), tag_icon.get_data(), tag_group.get_data(), static_cast<uint32_t>(priority));
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamTimeline::end_phase() {
    return retire_phase(active_runtime());
}

Error SteamTimeline::retire_phase(const std::shared_ptr<SteamRuntime> &active) {
#ifdef CRIMSON_WITH_STEAM
    ISteamTimeline *timeline = get_timeline(active);
    if (!timeline) {
        return ERR_UNAVAILABLE;
    }
    if (active->timeline_phase_owner != get_instance_id()) {
        return ERR_UNCONFIGURED;
    }
    const auto end = active->symbol<decltype(&SteamAPI_ISteamTimeline_EndGamePhase)>(
            "SteamAPI_ISteamTimeline_EndGamePhase");
    active->timeline_phase_owner = 0;
    if (end) {
        end(timeline);
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Variant SteamTimeline::phase_recording(const String &phase_id) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        const CharString id = phase_id.utf8();
        if (!valid_phase_id(id)) {
            return SteamTimelinePhaseResult::create(ERR_INVALID_PARAMETER);
        }
        const auto query = active->symbol<decltype(&SteamAPI_ISteamTimeline_DoesGamePhaseRecordingExist)>(
                "SteamAPI_ISteamTimeline_DoesGamePhaseRecordingExist");
        if (query) {
            if (!active->can_track_call()) {
                return SteamTimelinePhaseResult::create(ERR_BUSY);
            }
            Ref<SteamTimelinePhaseOperation> operation;
            operation.instantiate();
            const uint64_t call = query(timeline, id.get_data());
            if (watch_recording(active, call, SteamTimelineGamePhaseRecordingExists_t::k_iCallback,
                        sizeof(SteamTimelineGamePhaseRecordingExists_t), [operation, id](Error status,
                                const void *record) {
                            Ref<TimelinePhaseRecording> footage;
                            if (status == OK) {
                                SteamTimelineGamePhaseRecordingExists_t answer{};
                                std::memcpy(&answer, record, sizeof(answer));
                                const size_t length = static_cast<size_t>(id.length());
                                constexpr auto max_milliseconds = static_cast<uint64_t>(
                                        (std::numeric_limits<int64_t>::max)());
                                if (std::memcmp(id.get_data(), answer.m_rgchPhaseID, length) != 0 ||
                                        answer.m_rgchPhaseID[length] != '\0' ||
                                        answer.m_ulRecordingMS > max_milliseconds ||
                                        answer.m_ulLongestClipMS > max_milliseconds) {
                                    status = FAILED;
                                } else {
                                    footage.instantiate();
                                    footage->recording_ms = static_cast<int64_t>(answer.m_ulRecordingMS);
                                    footage->longest_clip_ms = static_cast<int64_t>(answer.m_ulLongestClipMS);
                                    footage->clip_count = answer.m_unClipCount;
                                    footage->screenshot_count = answer.m_unScreenshotCount;
                                }
                            }
                            operation->emit_signal("completed", SteamTimelinePhaseResult::create(status, footage));
                        })) {
                return Signal(operation.ptr(), "completed");
            }
            return SteamTimelinePhaseResult::create(FAILED);
        }
    }
#endif
    return SteamTimelinePhaseResult::create(ERR_UNAVAILABLE);
}

Error SteamTimeline::open_phase_in_overlay(const String &phase_id) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamTimeline *timeline = get_timeline(active);
    if (timeline) {
        const CharString id = phase_id.utf8();
        if (!valid_phase_id(id)) {
            return ERR_INVALID_PARAMETER;
        }
        const auto open = active->symbol<decltype(&SteamAPI_ISteamTimeline_OpenOverlayToGamePhase)>(
                "SteamAPI_ISteamTimeline_OpenOverlayToGamePhase");
        if (open) {
            const Error ready = overlay_ready(active);
            if (ready != OK) {
                return ready;
            }
            open(timeline, id.get_data());
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

bool SteamTimeline::watch_recording(const std::shared_ptr<SteamRuntime> &active, uint64_t call, int callback_id,
        size_t size, std::function<void(Error, const void *)> complete) {
#ifdef CRIMSON_WITH_STEAM
    const ObjectID self(get_instance_id());
    if (active->watch_call(call, callback_id, size, [self, call](const void *record) {
                if (const auto facade = find_timeline(self); facade.is_valid()) {
                    const bool live = facade->scope && facade->scope->active;
                    facade->finish_query(call, !live ? ERR_SKIP : (record ? OK : FAILED), record);
                }
            })) {
        pending_queries.emplace(call, PendingQuery{std::move(complete),
                Time::get_singleton()->get_ticks_msec() + recording_query_timeout_msec});
        return true;
    }
#endif
    return false;
}

Error SteamTimeline::finish_query(uint64_t call, Error error, const void *record) {
#ifdef CRIMSON_WITH_STEAM
    auto pending = pending_queries.extract(call);
    if (!pending.empty()) {
        if (const auto active = runtime.lock()) {
            active->unwatch_call(call);
        }
        if ((error == OK || error == FAILED) &&
                Time::get_singleton()->get_ticks_msec() >= pending.mapped().deadline_msec) {
            error = ERR_TIMEOUT;
            record = nullptr;
        }
        pending.mapped().complete(error, record);
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

void SteamTimeline::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (pending_queries.empty() || !scope || !scope->active) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<uint64_t> expired;
    for (const auto &[call, query] : pending_queries) {
        if (now >= query.deadline_msec) {
            expired.push_back(call);
        }
    }
    for (const uint64_t call : expired) {
        finish_query(call, ERR_TIMEOUT);
    }
#endif
}

void SteamTimeline::close() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    retire_phase(active);
    runtime.reset();
    scope.reset();
    const auto ranges = std::exchange(open_ranges, {});
    const auto queries = std::exchange(pending_queries, {});

    if (ISteamTimeline *timeline = get_timeline(active)) {
        const auto remove = active->symbol<decltype(&SteamAPI_ISteamTimeline_RemoveTimelineEvent)>(
                "SteamAPI_ISteamTimeline_RemoveTimelineEvent");
        if (remove) {
            for (const auto &[handle, event] : ranges) {
                remove(timeline, handle);
            }
        }
    }
    if (active) {
        for (const auto &[call, query] : queries) {
            active->unwatch_call(call);
        }
    }
    for (const auto &[call, query] : queries) {
        query.complete(ERR_SKIP, nullptr);
    }
#endif
}

} // namespace crimson::steam
