// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Snapshot of this Steam client's local broadcast.
class SteamBroadcastStatus : public godot::RefCounted {
    GDCLASS(SteamBroadcastStatus, godot::RefCounted)

public:
    // Report whether Steam is uploading a live broadcast.
    bool is_uploading() const;
    // Return the live viewer count, or zero when idle.
    int64_t get_viewers() const;

protected:
    // Register immutable broadcast status properties.
    static void _bind_methods();

private:
    friend class SteamVideo;

    // Whether Steam reported a live upload.
    bool uploading = false;
    // Number of viewers reported for the upload.
    int64_t viewers = 0;
};

// Outcome of reading the local broadcast.
class SteamBroadcastStatusResult : public SteamResult {
    GDCLASS(SteamBroadcastStatusResult, SteamResult)

public:
    // Return the copied status when successful.
    godot::Ref<SteamBroadcastStatus> get_value() const;

protected:
    // Register the immutable broadcast status payload.
    static void _bind_methods();

private:
    friend class SteamVideo;

    // Create the settled result for a status query.
    static godot::Ref<SteamBroadcastStatusResult> create(godot::Error error,
            const godot::Ref<SteamBroadcastStatus> &status = {});
    // Broadcast state copied from the SDK.
    godot::Ref<SteamBroadcastStatus> value;
};

// Holds the completion signal for a direct video lookup.
class SteamVideoOperation : public godot::RefCounted {
    GDCLASS(SteamVideoOperation, godot::RefCounted)

protected:
    // Register the typed lookup completion.
    static void _bind_methods();
};

// Reads Steam video descriptors and observes the client's broadcast status.
class SteamVideo : public godot::RefCounted {
    GDCLASS(SteamVideo, godot::RefCounted)

public:
    // Await a temporary streaming URL for the specified video AppID.
    godot::Variant stream_url(int64_t video_app_id);
    // Await the available 360-degree projection settings for the specified video AppID.
    godot::Variant projection_settings(int64_t video_app_id);
    // Read this Steam client's broadcast upload status.
    godot::Ref<SteamBroadcastStatusResult> broadcast_status() const;

protected:
    // Register typed status and direct await methods and broadcast signals.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Attach to one client start and its broadcast callbacks.
    void configure(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime);
    // Retire this facade and cancel its video observers before invoking script.
    void close();
    // Enforce local observation deadlines without releasing the native request key.
    void tick();
    // Resolve Steam only while this exact scope is alive on the main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Issue or join the lookup for one video AppID and callback family.
    godot::Variant lookup(int64_t video_app_id, bool projection);

    // Runtime observed without extending its life after the client stops.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact client generation that owns this facade.
    std::weak_ptr<SteamClientScope> scope;
    // Callback subscription for local broadcast startup.
    uint64_t started_subscription = 0;
    // Callback subscription for local broadcast shutdown.
    uint64_t stopped_subscription = 0;
};

} // namespace crimson::steam
