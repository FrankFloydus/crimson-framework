// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/binder_common.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Snapshot of Steam's optional client music player.
class SteamMusicStatus : public godot::RefCounted {
    GDCLASS(SteamMusicStatus, godot::RefCounted)

public:
    // Playback states exposed by the music player.
    enum Playback {
        UNKNOWN, // Steam did not identify a playback state.
        PLAYING, // The player is currently playing.
        PAUSED, // The player is currently paused.
        IDLE, // The player is stopped.
    };

    // Return whether Steam has enabled its music player.
    bool is_enabled() const;
    // Return the observed playback state.
    Playback get_playback() const;
    // Return the normalized Steam player volume.
    double get_volume() const;

protected:
    // Register the immutable status fields and playback enum.
    static void _bind_methods();

private:
    friend class SteamMusic;

    // Whether the client enabled the music feature.
    bool enabled = false;
    // Playback at the time of the query.
    Playback playback = UNKNOWN;
    // Client music volume from 0 through 1 when enabled.
    double volume = 0.0;
};

// Outcome of reading Steam's optional music player.
class SteamMusicStatusResult : public SteamResult {
    GDCLASS(SteamMusicStatusResult, SteamResult)

public:
    // Return the player status on success.
    godot::Ref<SteamMusicStatus> get_value() const;

protected:
    // Register the immutable status payload.
    static void _bind_methods();

private:
    friend class SteamMusic;

    // Construct the terminal outcome for one status query.
    static godot::Ref<SteamMusicStatusResult> create(godot::Error error,
            const godot::Ref<SteamMusicStatus> &status = {});
    // Player state copied from the SDK.
    godot::Ref<SteamMusicStatus> value;
};

// Observes and controls the Steam client's music player on demand.
class SteamMusic : public godot::RefCounted {
    GDCLASS(SteamMusic, godot::RefCounted)

public:
    // Read whether the player is enabled and its current playback state.
    godot::Ref<SteamMusicStatusResult> status() const;
    // Request playback of the current Steam queue.
    godot::Error play();
    // Request a pause of the current Steam queue.
    godot::Error pause();
    // Request the next Steam track.
    godot::Error next();
    // Request the previous Steam track.
    godot::Error previous();
    // Set Steam music volume from 0 to 1.
    godot::Error set_volume(double level);

protected:
    // Register player controls and signals.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Attach the music service to one client start and observe playback events.
    void configure(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime);
    // Retire observations before callbacks can see this old scope again.
    void close();
    // Resolve the runtime only while this service's exact scope is active.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Submit a no-argument Steam music command after checking capability.
    godot::Error control(const char *symbol);

    // Runtime observed without retaining Steam after scope closure.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact client generation that owns this facade.
    std::weak_ptr<SteamClientScope> scope;
    // Subscription to playback status changes.
    uint64_t playback_subscription = 0;
    // Subscription to normalized volume changes.
    uint64_t volume_subscription = 0;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamMusicStatus::Playback);
