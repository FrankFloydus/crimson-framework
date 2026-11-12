// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>

namespace crimson::steam {

class SteamRuntime;

// Reads Steam client, overlay, and hardware state from a started client scope.
class SteamUtils : public godot::RefCounted {
    GDCLASS(SteamUtils, godot::RefCounted)

public:
    // Device identity reported by Steam for diagnostics.
    enum HardwareType {
        HARDWARE_UNAVAILABLE = -1, // The client or query is unavailable.
        HARDWARE_NONE = 0, // The process is not running on a Steam hardware device.
        HARDWARE_STEAM_DECK, // Steam Deck hardware.
        HARDWARE_STEAM_MACHINE, // Steam Machine hardware.
        HARDWARE_STEAM_FRAME, // Steam Frame hardware.
        HARDWARE_OTHER, // A hardware type introduced by a newer Steam client.
    };

    // Suggested starting preset reported by Steam for this game and device.
    enum HardwareDefaultConfig {
        CONFIG_UNAVAILABLE = -1, // The client or query is unavailable.
        CONFIG_NONE = 0, // Steam has no preset suggestion.
        CONFIG_LOW, // A general low preset.
        CONFIG_MEDIUM, // A general medium preset.
        CONFIG_HIGH, // A general high preset.
        CONFIG_MAX, // A general maximum preset.
        CONFIG_STEAM_DECK, // A preset tuned for Steam Deck.
        CONFIG_STEAM_MACHINE, // A preset tuned for Steam Machine.
        CONFIG_STEAM_FRAME, // A preset tuned for Steam Frame.
        CONFIG_OTHER, // A preset introduced by a newer Steam client.
    };

    // Report whether this scope still has a Steam Utils interface on the main thread.
    bool is_available() const;
    // Report whether the overlay is available to the current process at this instant.
    bool is_overlay_ready() const;
    // Return the Steam client's interface language, or an empty string if unavailable.
    godot::String ui_language() const;
    // Return the device identity or HARDWARE_UNAVAILABLE if the query is unavailable.
    HardwareType hardware_type() const;
    // Return the suggested preset or CONFIG_UNAVAILABLE if the query is unavailable.
    HardwareDefaultConfig hardware_default_config() const;
    // Report whether the game process is running under Proton.
    bool is_running_under_proton() const;
    // Report whether the Steam client itself is running in VR mode.
    bool is_running_in_vr() const;

protected:
    // Register the typed Utils facade and its enum constants with Godot.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Observe this scope's runtime without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;

    // Forget the observed runtime when the owning client stops.
    void detach();
};

#ifdef CRIMSON_WITH_STEAM
namespace detail {

// Return the hardware family from a runtime already held on the main thread.
SteamUtils::HardwareType steam_hardware(const SteamRuntime &runtime);
// Return the Steam preset from a runtime already held on the main thread.
SteamUtils::HardwareDefaultConfig steam_device_preset(const SteamRuntime &runtime);
// Report whether Steam says the current process is running under Proton.
bool steam_is_proton(const SteamRuntime &runtime);
// Report whether Steam itself is running in VR mode.
bool steam_is_in_vr(const SteamRuntime &runtime);
// Report Steam's current VR headset streaming state when available.
bool steam_headset_streaming(const SteamRuntime &runtime);
// Request Steam's dashboard after checking that Steam VR mode and the export are available.
godot::Error steam_open_vr_dashboard(const SteamRuntime &runtime);

} // namespace detail
#endif

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamUtils::HardwareType);
VARIANT_ENUM_CAST(crimson::steam::SteamUtils::HardwareDefaultConfig);
