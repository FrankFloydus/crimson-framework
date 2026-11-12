// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_music.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <cmath>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <cstring>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Resolve Steam Music only when the running client exposes the SDK interface.
ISteamMusic *get_music(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamMusic_v001)>("SteamAPI_SteamMusic_v001") : nullptr;
    return get ? get() : nullptr;
}

// Return the exact service while dispatching a copied SDK event.
Ref<SteamMusic> music_service(ObjectID owner) {
    return Ref<SteamMusic>(Object::cast_to<SteamMusic>(ObjectDB::get_instance(owner)));
}

} // namespace
#endif

void SteamMusicStatus::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_enabled"), &SteamMusicStatus::is_enabled);
    ClassDB::bind_method(D_METHOD("get_playback"), &SteamMusicStatus::get_playback);
    ClassDB::bind_method(D_METHOD("get_volume"), &SteamMusicStatus::get_volume);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "playback", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_playback");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_volume");
    BIND_ENUM_CONSTANT(UNKNOWN);
    BIND_ENUM_CONSTANT(PLAYING);
    BIND_ENUM_CONSTANT(PAUSED);
    BIND_ENUM_CONSTANT(IDLE);
}

bool SteamMusicStatus::is_enabled() const {
    return enabled;
}

SteamMusicStatus::Playback SteamMusicStatus::get_playback() const {
    return playback;
}

double SteamMusicStatus::get_volume() const {
    return volume;
}

void SteamMusicStatusResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamMusicStatusResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamMusicStatus",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamMusicStatus> SteamMusicStatusResult::get_value() const {
    return value;
}

Ref<SteamMusicStatusResult> SteamMusicStatusResult::create(Error error, const Ref<SteamMusicStatus> &status) {
    Ref<SteamMusicStatusResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = status;
    return result;
}

void SteamMusic::_bind_methods() {
    ClassDB::bind_method(D_METHOD("status"), &SteamMusic::status);
    ClassDB::bind_method(D_METHOD("play"), &SteamMusic::play);
    ClassDB::bind_method(D_METHOD("pause"), &SteamMusic::pause);
    ClassDB::bind_method(D_METHOD("next"), &SteamMusic::next);
    ClassDB::bind_method(D_METHOD("previous"), &SteamMusic::previous);
    ClassDB::bind_method(D_METHOD("set_volume", "level"), &SteamMusic::set_volume);
    ADD_SIGNAL(MethodInfo("playback_changed"));
    ADD_SIGNAL(MethodInfo("volume_changed", PropertyInfo(Variant::FLOAT, "level")));
}

void SteamMusic::configure(
        const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime) {
    runtime = active;
    scope = lifetime;
#ifdef CRIMSON_WITH_STEAM
    if (get_music(active)) {
        const ObjectID self(get_instance_id());
        playback_subscription = active->subscribe(PlaybackStatusHasChanged_t::k_iCallback,
                [self](const void *, size_t size) {
                    const auto service = music_service(self);
                    if (service.is_valid() && (size == 0 || size == sizeof(PlaybackStatusHasChanged_t)) &&
                            service->active_runtime()) {
                        service->emit_signal("playback_changed");
                    }
                });
        volume_subscription = active->subscribe(VolumeHasChanged_t::k_iCallback,
                [self](const void *data, size_t size) {
                    const auto service = music_service(self);
                    if (!service.is_valid() || !service->active_runtime() || !data ||
                            size != sizeof(VolumeHasChanged_t)) {
                        return;
                    }
                    VolumeHasChanged_t event{};
                    std::memcpy(&event, data, sizeof(event));
                    if (std::isfinite(event.m_flNewVolume) && event.m_flNewVolume >= 0 && event.m_flNewVolume <= 1) {
                        service->emit_signal("volume_changed", event.m_flNewVolume);
                    }
                });
    }
#endif
}

void SteamMusic::close() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = runtime.lock()) {
        active->unsubscribe(playback_subscription);
        active->unsubscribe(volume_subscription);
    }
#endif
    runtime.reset();
    scope.reset();
}

std::shared_ptr<SteamRuntime> SteamMusic::active_runtime() const {
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

Ref<SteamMusicStatusResult> SteamMusic::status() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamMusic *music = get_music(active)) {
        const auto enabled = active->symbol<decltype(&SteamAPI_ISteamMusic_BIsEnabled)>("SteamAPI_ISteamMusic_BIsEnabled");
        const auto playback = active->symbol<decltype(&SteamAPI_ISteamMusic_GetPlaybackStatus)>(
                "SteamAPI_ISteamMusic_GetPlaybackStatus");
        const auto volume = active->symbol<decltype(&SteamAPI_ISteamMusic_GetVolume)>("SteamAPI_ISteamMusic_GetVolume");
        if (enabled && playback && volume) {
            Ref<SteamMusicStatus> value;
            value.instantiate();
            value->enabled = enabled(music);
            if (value->enabled) {
                switch (playback(music)) {
                    case AudioPlayback_Playing: value->playback = SteamMusicStatus::PLAYING; break;
                    case AudioPlayback_Paused: value->playback = SteamMusicStatus::PAUSED; break;
                    case AudioPlayback_Idle: value->playback = SteamMusicStatus::IDLE; break;
                    default: break;
                }
                const double level = volume(music);
                if (!std::isfinite(level) || level < 0 || level > 1) {
                    return SteamMusicStatusResult::create(FAILED);
                }
                value->volume = level;
            }
            return SteamMusicStatusResult::create(OK, value);
        }
    }
#endif
    return SteamMusicStatusResult::create(ERR_UNAVAILABLE);
}

Error SteamMusic::play() {
    return control("SteamAPI_ISteamMusic_Play");
}

Error SteamMusic::pause() {
    return control("SteamAPI_ISteamMusic_Pause");
}

Error SteamMusic::next() {
    return control("SteamAPI_ISteamMusic_PlayNext");
}

Error SteamMusic::previous() {
    return control("SteamAPI_ISteamMusic_PlayPrevious");
}

Error SteamMusic::control(const char *symbol) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamMusic *music = get_music(active)) {
        const auto enabled = active->symbol<decltype(&SteamAPI_ISteamMusic_BIsEnabled)>("SteamAPI_ISteamMusic_BIsEnabled");
        const auto action = active->symbol<decltype(&SteamAPI_ISteamMusic_Play)>(symbol);
        if (enabled && action && enabled(music)) {
            action(music);
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamMusic::set_volume(double level) {
    if (!std::isfinite(level) || level < 0 || level > 1) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (ISteamMusic *music = get_music(active)) {
        const auto enabled = active->symbol<decltype(&SteamAPI_ISteamMusic_BIsEnabled)>("SteamAPI_ISteamMusic_BIsEnabled");
        const auto set = active->symbol<decltype(&SteamAPI_ISteamMusic_SetVolume)>("SteamAPI_ISteamMusic_SetVolume");
        if (enabled && set && enabled(music)) {
            set(music, static_cast<float>(level));
            return OK;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

} // namespace crimson::steam
