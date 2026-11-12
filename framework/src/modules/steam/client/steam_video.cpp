// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_video.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <cstring>
#include <limits>
#include <map>
#include <utility>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Maximum native keys and waiters retained by one process Video interface.
constexpr size_t maximum_requests = 32;
constexpr size_t maximum_observers_per_request = 32;
// Observer deadline. Steam can still deliver a result after this deadline.
constexpr uint64_t request_timeout_msec = 15000;
// Start at Valve's documented typical projection buffer size, with a hard cap.
constexpr int32_t typical_projection_bytes = 48000;
constexpr int32_t maximum_projection_bytes = 256 * 1024;

// Resolve the optional Steam Video interface for the active process.
ISteamVideo *get_video(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamVideo_v007)>("SteamAPI_SteamVideo_v007") : nullptr;
    return get ? get() : nullptr;
}

// Retain the exact facade during a copied broadcast event.
Ref<SteamVideo> video_service(ObjectID owner) {
    return Ref<SteamVideo>(Object::cast_to<SteamVideo>(ObjectDB::get_instance(owner)));
}

// Preserve actionable Steam errors without including AppIDs or returned content in the diagnostic.
Error video_error(EResult result) {
    switch (result) {
        case k_EResultOK: return OK;
        case k_EResultInvalidParam: return ERR_INVALID_PARAMETER;
        case k_EResultFileNotFound: return ERR_FILE_NOT_FOUND;
        case k_EResultAccessDenied:
        case k_EResultBlocked: return ERR_UNAUTHORIZED;
        case k_EResultTimeout: return ERR_TIMEOUT;
        case k_EResultNoConnection:
        case k_EResultNotLoggedOn: return ERR_CONNECTION_ERROR;
        case k_EResultServiceUnavailable: return ERR_UNAVAILABLE;
        default: return FAILED;
    }
}

// Copy only a bounded, nonempty SDK text field containing a terminator.
bool read_text(const char *data, size_t capacity, String &value) {
    const auto *end = static_cast<const char *>(std::memchr(data, 0, capacity));
    return end && end != data && value.parse_utf8(data, end - data) == OK;
}

// Read the SDK's single-use projection payload once and retain the resulting String.
Ref<SteamStringResult> read_projection(const std::shared_ptr<SteamRuntime> &runtime, AppId_t app_id) {
    ISteamVideo *video = get_video(runtime);
    const auto read = video ? runtime->symbol<decltype(&SteamAPI_ISteamVideo_GetOPFStringForApp)>(
                                      "SteamAPI_ISteamVideo_GetOPFStringForApp")
                            : nullptr;
    if (!read) {
        return SteamStringResult::create(ERR_UNAVAILABLE);
    }

    std::vector<char> buffer(typical_projection_bytes, '\0');
    int32_t size = typical_projection_bytes;
    bool received = read(video, app_id, buffer.data(), &size);
    if (!received && size > typical_projection_bytes && size <= maximum_projection_bytes) {
        buffer.assign(static_cast<size_t>(size), '\0');
        received = read(video, app_id, buffer.data(), &size);
    }
    if (!received || size <= 0 || static_cast<size_t>(size) > buffer.size()) {
        return SteamStringResult::create(FAILED, {}, "Steam returned invalid projection settings.");
    }
    if (buffer[static_cast<size_t>(size - 1)] == '\0') {
        --size;
    }
    String content;
    if (size == 0 || std::memchr(buffer.data(), 0, static_cast<size_t>(size)) ||
            content.parse_utf8(buffer.data(), size) != OK) {
        return SteamStringResult::create(FAILED, {}, "Steam returned invalid projection settings.");
    }
    return SteamStringResult::create(OK, content);
}

} // namespace

// Correlates Video callbacks that identify an AppID and callback family instead of a call handle.
class SteamVideoRequests {
public:
    // Observe one AppID and family, issuing the native call only for a new key.
    Variant lookup(const std::shared_ptr<SteamRuntime> &runtime, ISteamVideo *video,
            AppId_t app_id, bool projection, ObjectID owner, const std::shared_ptr<SteamClientScope> &scope);
    // Copy and distribute a URL callback for the exact pending video AppID.
    void receive_url(const void *data, size_t size);
    // Consume and distribute the single-use projection payload for the pending video AppID.
    void receive_projection(const std::shared_ptr<SteamRuntime> &runtime, const void *data, size_t size);
    // Retire timed-out observers or observers whose scope explicitly stopped.
    void retire_observers(uint64_t now, ObjectID owner = {});

private:
    // Steam callback family and queried video AppID.
    using Key = std::pair<bool, AppId_t>;

    // One awaiting script and its exact issuing facade.
    struct Observer {
        // Facade that owns cancellation for this observer.
        ObjectID owner;
        // Exact generation that was live when the request started.
        std::weak_ptr<SteamClientScope> scope;
        // Held until the callback or a local terminal condition.
        Ref<SteamVideoOperation> operation;
        // Monotonic deadline for the local observer.
        uint64_t deadline;
    };

    // Move observers out before emitting a completion that can reenter the service.
    std::vector<Observer> finish(const Key &key);
    // Resolve retired or late observers without exposing private video payloads.
    static void deliver(std::vector<Observer> observers, const Ref<SteamStringResult> &result);

    // Empty vectors retain keys while their native result still needs to drain.
    std::map<Key, std::vector<Observer>> pending;
};

Variant SteamVideoRequests::lookup(const std::shared_ptr<SteamRuntime> &runtime, ISteamVideo *video,
        AppId_t app_id, bool projection, ObjectID owner, const std::shared_ptr<SteamClientScope> &scope) {
    const Key key{projection, app_id};
    auto found = pending.find(key);
    const bool new_request = found == pending.end();
    if (new_request ? pending.size() >= maximum_requests :
                      found->second.empty() || found->second.size() >= maximum_observers_per_request) {
        return SteamStringResult::create(ERR_BUSY);
    }
    const auto request = runtime->symbol<decltype(&SteamAPI_ISteamVideo_GetVideoURL)>(
            projection ? "SteamAPI_ISteamVideo_GetOPFSettings" : "SteamAPI_ISteamVideo_GetVideoURL");
    if (!request) {
        return SteamStringResult::create(ERR_UNAVAILABLE);
    }

    Ref<SteamVideoOperation> operation;
    operation.instantiate();
    pending[key].push_back(Observer{owner, scope, operation,
            Time::get_singleton()->get_ticks_msec() + request_timeout_msec});
    if (new_request) {
        request(video, app_id);
    }
    return Signal(operation.ptr(), "completed");
}

std::vector<SteamVideoRequests::Observer> SteamVideoRequests::finish(const Key &key) {
    auto found = pending.extract(key);
    return found.empty() ? std::vector<Observer>() : std::move(found.mapped());
}

void SteamVideoRequests::deliver(std::vector<Observer> observers, const Ref<SteamStringResult> &result) {
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    for (const auto &observer : observers) {
        const auto scope = observer.scope.lock();
        if (!scope || !scope->active) {
            observer.operation->emit_signal("completed", SteamStringResult::create(ERR_SKIP));
        } else if (now >= observer.deadline) {
            observer.operation->emit_signal("completed", SteamStringResult::create(ERR_TIMEOUT));
        } else {
            observer.operation->emit_signal("completed", result);
        }
    }
}

void SteamVideoRequests::receive_url(const void *data, size_t size) {
    if (!data || size != sizeof(GetVideoURLResult_t)) {
        return;
    }
    GetVideoURLResult_t event{};
    std::memcpy(&event, data, sizeof(event));
    auto observers = finish({false, event.m_unVideoAppID});
    if (observers.empty()) {
        return;
    }
    Error status = video_error(event.m_eResult);
    String url;
    if (status == OK && !read_text(event.m_rgchURL, sizeof(event.m_rgchURL), url)) {
        status = FAILED;
    }
    deliver(std::move(observers), SteamStringResult::create(status, url));
}

void SteamVideoRequests::receive_projection(const std::shared_ptr<SteamRuntime> &runtime,
        const void *data, size_t size) {
    if (!data || size != sizeof(GetOPFSettingsResult_t)) {
        return;
    }
    GetOPFSettingsResult_t event{};
    std::memcpy(&event, data, sizeof(event));
    const Key key{true, event.m_unVideoAppID};
    if (pending.find(key) == pending.end()) {
        return;
    }
    auto observers = finish(key);
    const Error status = video_error(event.m_eResult);
    const auto result = status == OK ? read_projection(runtime, event.m_unVideoAppID)
                                    : SteamStringResult::create(status);
    deliver(std::move(observers), result);
}

void SteamVideoRequests::retire_observers(uint64_t now, ObjectID owner) {
    std::vector<Ref<SteamVideoOperation>> removed;
    for (auto &[key, observers] : pending) {
        for (auto observer = observers.begin(); observer != observers.end();) {
            if (owner != ObjectID() ? observer->owner == owner : now >= observer->deadline) {
                removed.push_back(std::move(observer->operation));
                observer = observers.erase(observer);
            } else {
                ++observer;
            }
        }
    }
    if (!removed.empty()) {
        const auto result = SteamStringResult::create(owner != ObjectID() ? ERR_SKIP : ERR_TIMEOUT);
        for (const auto &operation : removed) {
            operation->emit_signal("completed", result);
        }
    }
}
#endif

void SteamBroadcastStatus::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_uploading"), &SteamBroadcastStatus::is_uploading);
    ClassDB::bind_method(D_METHOD("get_viewers"), &SteamBroadcastStatus::get_viewers);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "uploading", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_uploading");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "viewers", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_viewers");
}

bool SteamBroadcastStatus::is_uploading() const {
    return uploading;
}

int64_t SteamBroadcastStatus::get_viewers() const {
    return viewers;
}

void SteamBroadcastStatusResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamBroadcastStatusResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamBroadcastStatus",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamBroadcastStatus> SteamBroadcastStatusResult::get_value() const {
    return value;
}

Ref<SteamBroadcastStatusResult> SteamBroadcastStatusResult::create(
        Error error, const Ref<SteamBroadcastStatus> &status) {
    Ref<SteamBroadcastStatusResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = status;
    return result;
}

void SteamVideoOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamStringResult")));
}

void SteamVideo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("stream_url", "video_app_id"), &SteamVideo::stream_url);
    ClassDB::bind_method(D_METHOD("projection_settings", "video_app_id"), &SteamVideo::projection_settings);
    ClassDB::bind_method(D_METHOD("broadcast_status"), &SteamVideo::broadcast_status);
    ADD_SIGNAL(MethodInfo("broadcast_started"));
    ADD_SIGNAL(MethodInfo("broadcast_stopped", PropertyInfo(Variant::INT, "reason")));
}

void SteamVideo::configure(
        const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime) {
    runtime = active;
    scope = lifetime;
#ifdef CRIMSON_WITH_STEAM
    if (get_video(active)) {
        const ObjectID self(get_instance_id());
        started_subscription = active->subscribe(BroadcastUploadStart_t::k_iCallback,
                [self](const void *data, size_t size) {
                    const auto service = video_service(self);
                    if (service.is_valid() && service->active_runtime() && data && size == sizeof(BroadcastUploadStart_t)) {
                        service->emit_signal("broadcast_started");
                    }
                });
        stopped_subscription = active->subscribe(BroadcastUploadStop_t::k_iCallback,
                [self](const void *data, size_t size) {
                    const auto service = video_service(self);
                    if (!service.is_valid() || !service->active_runtime() || !data ||
                            size != sizeof(BroadcastUploadStop_t)) {
                        return;
                    }
                    BroadcastUploadStop_t event{};
                    std::memcpy(&event, data, sizeof(event));
                    service->emit_signal("broadcast_stopped", static_cast<int64_t>(event.m_eResult));
                });
    }
#endif
}

void SteamVideo::close() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    runtime.reset();
    scope.reset();
    if (active) {
        active->unsubscribe(started_subscription);
        active->unsubscribe(stopped_subscription);
        if (active->video_requests) {
            active->video_requests->retire_observers(0, ObjectID(get_instance_id()));
        }
    }
#endif
}

void SteamVideo::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active && active->video_requests) {
        active->video_requests->retire_observers(Time::get_singleton()->get_ticks_msec());
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamVideo::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto lifetime = scope.lock();
        if (lifetime && lifetime->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

Variant SteamVideo::stream_url(int64_t video_app_id) {
    return lookup(video_app_id, false);
}

Variant SteamVideo::projection_settings(int64_t video_app_id) {
    return lookup(video_app_id, true);
}

Variant SteamVideo::lookup(int64_t video_app_id, bool projection) {
    if (video_app_id <= 0 || static_cast<uint64_t>(video_app_id) > UINT32_MAX) {
        return SteamStringResult::create(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamVideo *video = get_video(active);
    if (!video) {
        return SteamStringResult::create(ERR_UNAVAILABLE);
    }
    if (!active->video_requests) {
        const std::weak_ptr<SteamRuntime> weak_runtime(active);
        const uint64_t url = active->subscribe(GetVideoURLResult_t::k_iCallback,
                [weak_runtime](const void *data, size_t size) {
                    if (const auto live = weak_runtime.lock(); live && live->video_requests) {
                        live->video_requests->receive_url(data, size);
                    }
                });
        const uint64_t opf = active->subscribe(GetOPFSettingsResult_t::k_iCallback,
                [weak_runtime](const void *data, size_t size) {
                    if (const auto live = weak_runtime.lock(); live && live->video_requests) {
                        live->video_requests->receive_projection(live, data, size);
                    }
                });
        if (!url || !opf) {
            active->unsubscribe(url);
            active->unsubscribe(opf);
            return SteamStringResult::create(ERR_BUSY);
        }
        active->video_requests = std::make_shared<SteamVideoRequests>();
    }
    return active->video_requests->lookup(active, video, static_cast<AppId_t>(video_app_id), projection,
            ObjectID(get_instance_id()), scope.lock());
#else
    return SteamStringResult::create(ERR_UNAVAILABLE);
#endif
}

Ref<SteamBroadcastStatusResult> SteamVideo::broadcast_status() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamVideo *video = get_video(active)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamVideo_IsBroadcasting)>(
                "SteamAPI_ISteamVideo_IsBroadcasting");
        if (read) {
            int viewers = 0;
            const bool uploading = read(video, &viewers);
            if (uploading && viewers < 0) {
                return SteamBroadcastStatusResult::create(FAILED);
            }
            Ref<SteamBroadcastStatus> value;
            value.instantiate();
            value->uploading = uploading;
            value->viewers = uploading ? viewers : 0;
            return SteamBroadcastStatusResult::create(OK, value);
        }
    }
#endif
    return SteamBroadcastStatusResult::create(ERR_UNAVAILABLE);
}

} // namespace crimson::steam
