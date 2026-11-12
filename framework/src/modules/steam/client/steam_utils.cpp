// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_utils.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <optional>
#include <type_traits>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Resolve and run a value-only flat SDK query while holding the runtime on the main thread.
template <typename Query>
auto read_utils_value(const SteamRuntime &runtime, const char *name)
        -> std::optional<std::invoke_result_t<Query, ISteamUtils *>> {
    using Value = std::invoke_result_t<Query, ISteamUtils *>;
    static_assert(!std::is_pointer_v<Value>);
    ISteamUtils *utils = runtime.get_utils();
    const auto query = utils ? runtime.symbol<Query>(name) : nullptr;
    return query ? std::optional<Value>(query(utils)) : std::nullopt;
}

} // namespace

namespace detail {

SteamUtils::HardwareType steam_hardware(const SteamRuntime &runtime) {
    const auto value = read_utils_value<decltype(&SteamAPI_ISteamUtils_IsRunningOnSteamHardware)>(
            runtime, "SteamAPI_ISteamUtils_IsRunningOnSteamHardware");
    if (!value) {
        return SteamUtils::HARDWARE_UNAVAILABLE;
    }
    switch (*value) {
        case k_ESteamHardwareTypeNone: return SteamUtils::HARDWARE_NONE;
        case k_ESteamHardwareTypeSteamDeck: return SteamUtils::HARDWARE_STEAM_DECK;
        case k_ESteamHardwareTypeSteamMachine: return SteamUtils::HARDWARE_STEAM_MACHINE;
        case k_ESteamHardwareTypeSteamFrame: return SteamUtils::HARDWARE_STEAM_FRAME;
        default: return SteamUtils::HARDWARE_OTHER;
    }
}

SteamUtils::HardwareDefaultConfig steam_device_preset(const SteamRuntime &runtime) {
    const auto value = read_utils_value<decltype(&SteamAPI_ISteamUtils_GetSteamHardwareDefaultConfig)>(
            runtime, "SteamAPI_ISteamUtils_GetSteamHardwareDefaultConfig");
    if (!value) {
        return SteamUtils::CONFIG_UNAVAILABLE;
    }
    switch (*value) {
        case k_ESteamHardwareDefaultConfigNone: return SteamUtils::CONFIG_NONE;
        case k_ESteamHardwareDefaultConfigLow: return SteamUtils::CONFIG_LOW;
        case k_ESteamHardwareDefaultConfigMedium: return SteamUtils::CONFIG_MEDIUM;
        case k_ESteamHardwareDefaultConfigHigh: return SteamUtils::CONFIG_HIGH;
        case k_ESteamHardwareDefaultConfigMax: return SteamUtils::CONFIG_MAX;
        case k_ESteamHardwareDefaultConfigSteamDeck: return SteamUtils::CONFIG_STEAM_DECK;
        case k_ESteamHardwareDefaultConfigSteamMachine: return SteamUtils::CONFIG_STEAM_MACHINE;
        case k_ESteamHardwareDefaultConfigSteamFrame: return SteamUtils::CONFIG_STEAM_FRAME;
        default: return SteamUtils::CONFIG_OTHER;
    }
}

bool steam_is_proton(const SteamRuntime &runtime) {
    return read_utils_value<decltype(&SteamAPI_ISteamUtils_IsRunningUnderProton)>(
            runtime, "SteamAPI_ISteamUtils_IsRunningUnderProton").value_or(false);
}

bool steam_is_in_vr(const SteamRuntime &runtime) {
    return read_utils_value<decltype(&SteamAPI_ISteamUtils_IsSteamRunningInVR)>(
            runtime, "SteamAPI_ISteamUtils_IsSteamRunningInVR").value_or(false);
}

bool steam_headset_streaming(const SteamRuntime &runtime) {
    return read_utils_value<decltype(&SteamAPI_ISteamUtils_IsVRHeadsetStreamingEnabled)>(
            runtime, "SteamAPI_ISteamUtils_IsVRHeadsetStreamingEnabled").value_or(false);
}

Error steam_open_vr_dashboard(const SteamRuntime &runtime) {
    ISteamUtils *utils = runtime.get_utils();
    const auto open = utils ? runtime.symbol<decltype(&SteamAPI_ISteamUtils_StartVRDashboard)>(
            "SteamAPI_ISteamUtils_StartVRDashboard") : nullptr;
    if (!open || !steam_is_in_vr(runtime)) {
        return ERR_UNAVAILABLE;
    }
    open(utils);
    return OK;
}

} // namespace detail
#endif

void SteamUtils::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamUtils::is_available);
    ClassDB::bind_method(D_METHOD("is_overlay_ready"), &SteamUtils::is_overlay_ready);
    ClassDB::bind_method(D_METHOD("ui_language"), &SteamUtils::ui_language);
    ClassDB::bind_method(D_METHOD("hardware_type"), &SteamUtils::hardware_type);
    ClassDB::bind_method(D_METHOD("hardware_default_config"), &SteamUtils::hardware_default_config);
    ClassDB::bind_method(D_METHOD("is_running_under_proton"), &SteamUtils::is_running_under_proton);
    ClassDB::bind_method(D_METHOD("is_running_in_vr"), &SteamUtils::is_running_in_vr);

    BIND_ENUM_CONSTANT(HARDWARE_UNAVAILABLE);
    BIND_ENUM_CONSTANT(HARDWARE_NONE);
    BIND_ENUM_CONSTANT(HARDWARE_STEAM_DECK);
    BIND_ENUM_CONSTANT(HARDWARE_STEAM_MACHINE);
    BIND_ENUM_CONSTANT(HARDWARE_STEAM_FRAME);
    BIND_ENUM_CONSTANT(HARDWARE_OTHER);
    BIND_ENUM_CONSTANT(CONFIG_UNAVAILABLE);
    BIND_ENUM_CONSTANT(CONFIG_NONE);
    BIND_ENUM_CONSTANT(CONFIG_LOW);
    BIND_ENUM_CONSTANT(CONFIG_MEDIUM);
    BIND_ENUM_CONSTANT(CONFIG_HIGH);
    BIND_ENUM_CONSTANT(CONFIG_MAX);
    BIND_ENUM_CONSTANT(CONFIG_STEAM_DECK);
    BIND_ENUM_CONSTANT(CONFIG_STEAM_MACHINE);
    BIND_ENUM_CONSTANT(CONFIG_STEAM_FRAME);
    BIND_ENUM_CONSTANT(CONFIG_OTHER);
}

bool SteamUtils::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    return active && active->is_healthy() && active->get_utils();
#else
    return false;
#endif
}

bool SteamUtils::is_overlay_ready() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    return active && active->is_healthy() && read_utils_value<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
            *active, "SteamAPI_ISteamUtils_IsOverlayEnabled").value_or(false);
#else
    return false;
#endif
}

String SteamUtils::ui_language() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    if (ISteamUtils *utils = active && active->is_healthy() ? active->get_utils() : nullptr) {
        const auto query = active->symbol<decltype(&SteamAPI_ISteamUtils_GetSteamUILanguage)>(
                "SteamAPI_ISteamUtils_GetSteamUILanguage");
        const char *language = query ? query(utils) : nullptr;
        return language ? String::utf8(language) : String();
    }
#endif
    return {};
}

SteamUtils::HardwareType SteamUtils::hardware_type() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    if (active && active->is_healthy()) {
        return detail::steam_hardware(*active);
    }
#endif
    return HARDWARE_UNAVAILABLE;
}

SteamUtils::HardwareDefaultConfig SteamUtils::hardware_default_config() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    if (active && active->is_healthy()) {
        return detail::steam_device_preset(*active);
    }
#endif
    return CONFIG_UNAVAILABLE;
}

bool SteamUtils::is_running_under_proton() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    return active && active->is_healthy() && detail::steam_is_proton(*active);
#else
    return false;
#endif
}

bool SteamUtils::is_running_in_vr() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    return active && active->is_healthy() && detail::steam_is_in_vr(*active);
#else
    return false;
#endif
}

void SteamUtils::detach() {
    runtime.reset();
}

} // namespace crimson::steam
