// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_device.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object_id.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"
#endif

namespace crimson::steam {
using namespace godot;

void SteamDevice::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamDevice::is_available);
    ClassDB::bind_method(D_METHOD("hardware"), &SteamDevice::hardware);
    ClassDB::bind_method(D_METHOD("is_steam_deck"), &SteamDevice::is_steam_deck);
    ClassDB::bind_method(D_METHOD("is_proton"), &SteamDevice::is_proton);
    ClassDB::bind_method(D_METHOD("recommended_preset"), &SteamDevice::recommended_preset);
    ClassDB::bind_method(D_METHOD("is_running_in_vr"), &SteamDevice::is_running_in_vr);
    ClassDB::bind_method(D_METHOD("is_headset_streaming_enabled"), &SteamDevice::is_headset_streaming_enabled);
    ClassDB::bind_method(D_METHOD("open_vr_dashboard"), &SteamDevice::open_vr_dashboard);

    BIND_ENUM_CONSTANT(HARDWARE_UNKNOWN);
    BIND_ENUM_CONSTANT(HARDWARE_NONE);
    BIND_ENUM_CONSTANT(HARDWARE_STEAM_DECK);
    BIND_ENUM_CONSTANT(HARDWARE_STEAM_MACHINE);
    BIND_ENUM_CONSTANT(HARDWARE_STEAM_FRAME);
    BIND_ENUM_CONSTANT(PRESET_UNKNOWN);
    BIND_ENUM_CONSTANT(PRESET_NONE);
    BIND_ENUM_CONSTANT(PRESET_LOW);
    BIND_ENUM_CONSTANT(PRESET_MEDIUM);
    BIND_ENUM_CONSTANT(PRESET_HIGH);
    BIND_ENUM_CONSTANT(PRESET_MAX);
    BIND_ENUM_CONSTANT(PRESET_STEAM_DECK);
    BIND_ENUM_CONSTANT(PRESET_STEAM_MACHINE);
    BIND_ENUM_CONSTANT(PRESET_STEAM_FRAME);

    _bind_keyboard_methods();
}

void SteamDevice::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
    runtime = active;
    client_scope = scope;
    keyboard.attach(ObjectID(get_instance_id()), active, scope);
}

void SteamDevice::detach() {
    client_scope.reset();
    runtime.reset();
    keyboard.detach();
}

std::shared_ptr<SteamRuntime> SteamDevice::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto scope = client_scope.lock();
        if (scope && scope->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

bool SteamDevice::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return active && active->get_utils();
#else
    return false;
#endif
}

SteamDevice::Hardware SteamDevice::hardware() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        const auto family = detail::steam_hardware(*active);
        return family == SteamUtils::HARDWARE_OTHER ? HARDWARE_UNKNOWN : static_cast<Hardware>(family);
    }
#endif
    return HARDWARE_UNKNOWN;
}

bool SteamDevice::is_steam_deck() const {
    return hardware() == HARDWARE_STEAM_DECK;
}

bool SteamDevice::is_proton() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return active && detail::steam_is_proton(*active);
#else
    return false;
#endif
}

SteamDevice::Preset SteamDevice::recommended_preset() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active) {
        const auto preset = detail::steam_device_preset(*active);
        return preset == SteamUtils::CONFIG_OTHER ? PRESET_UNKNOWN : static_cast<Preset>(preset);
    }
#endif
    return PRESET_UNKNOWN;
}

bool SteamDevice::is_running_in_vr() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return active && detail::steam_is_in_vr(*active);
#else
    return false;
#endif
}

bool SteamDevice::is_headset_streaming_enabled() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return active && detail::steam_headset_streaming(*active);
#else
    return false;
#endif
}

Error SteamDevice::open_vr_dashboard() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = active_runtime()) {
        return detail::steam_open_vr_dashboard(*active);
    }
#endif
    return ERR_UNAVAILABLE;
}

} // namespace crimson::steam
