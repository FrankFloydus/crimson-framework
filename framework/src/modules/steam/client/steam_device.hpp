// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_device_keyboard.hpp"
#include "modules/steam/client/steam_utils.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <memory>

namespace godot {
class Control;
}

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Reads Steam platform state and owns keyboard requests for one client scope.
class SteamDevice : public godot::RefCounted {
    GDCLASS(SteamDevice, godot::RefCounted)

public:
    // Device family reported by Steam.
    enum Hardware {
        HARDWARE_UNKNOWN = SteamUtils::HARDWARE_UNAVAILABLE, // The query or hardware code is unrecognized.
        HARDWARE_NONE = SteamUtils::HARDWARE_NONE, // Steam recognizes no specific Steam hardware.
        HARDWARE_STEAM_DECK = SteamUtils::HARDWARE_STEAM_DECK, // Steam Deck.
        HARDWARE_STEAM_MACHINE = SteamUtils::HARDWARE_STEAM_MACHINE, // Steam Machine.
        HARDWARE_STEAM_FRAME = SteamUtils::HARDWARE_STEAM_FRAME, // Steam Frame.
    };

    // Suggested initial configuration family for this app and device.
    enum Preset {
        PRESET_UNKNOWN = SteamUtils::CONFIG_UNAVAILABLE, // The query or preset code is unrecognized.
        PRESET_NONE = SteamUtils::CONFIG_NONE, // Steam has no preset suggestion.
        PRESET_LOW = SteamUtils::CONFIG_LOW, // A general low preset.
        PRESET_MEDIUM = SteamUtils::CONFIG_MEDIUM, // A general medium preset.
        PRESET_HIGH = SteamUtils::CONFIG_HIGH, // A general high preset.
        PRESET_MAX = SteamUtils::CONFIG_MAX, // A general maximum preset.
        PRESET_STEAM_DECK = SteamUtils::CONFIG_STEAM_DECK, // A preset tuned for Steam Deck.
        PRESET_STEAM_MACHINE = SteamUtils::CONFIG_STEAM_MACHINE, // A preset tuned for Steam Machine.
        PRESET_STEAM_FRAME = SteamUtils::CONFIG_STEAM_FRAME, // A preset tuned for Steam Frame.
    };

    // Keyboard layout requested for the active text control.
    enum FloatingKeyboardMode {
        SINGLE_LINE = 0, // Single-line text input.
        MULTILINE = 1, // Multi-line text input.
        EMAIL = 2, // Email-oriented text input.
        NUMERIC = 3, // Numeric text input.
    };

    // Report whether the exact scope can query Steam Utils on the main thread.
    bool is_available() const;
    // Return the current Steam hardware family or HARDWARE_UNKNOWN.
    Hardware hardware() const;
    // Report whether Steam identified the hardware as Steam Deck.
    bool is_steam_deck() const;
    // Report whether Steam says this game process is running under Proton.
    bool is_proton() const;
    // Return the app and device preset hint or PRESET_UNKNOWN.
    Preset recommended_preset() const;
    // Report whether Steam itself is running in VR mode.
    bool is_running_in_vr() const;
    // Report Steam's current headset streaming state.
    bool is_headset_streaming_enabled() const;
    // Request the Steam VR dashboard from a live VR session.
    godot::Error open_vr_dashboard();
    // Request the floating keyboard for a focused editor control.
    godot::Error show_keyboard_for(godot::Control *field, FloatingKeyboardMode mode = SINGLE_LINE);
    // Await a typed modal text entry result with optional masking and multi-line input.
    godot::Variant enter_text(const godot::String &prompt, const godot::String &initial = {},
            int64_t max_chars = 64, bool multiline = false, bool hidden = false);
    // Request dismissal of the keyboard owned by this device service.
    godot::Error dismiss_keyboard();

protected:
    // Register platform queries, dashboard actions, and text entry methods.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Register the keyboard API and dismissal signal from the keyboard implementation.
    static void _bind_keyboard_methods();
    // Associate this facade and its keyboard helper with the current start.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Retire platform queries before ending any keyboard observations.
    void detach();
    // Poll only keyboard state that requires main-thread follow-up.
    void tick();
    // Resolve Steam only while this exact client scope is active on the main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;

    // Process Steam runtime observed through this client scope.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact lifetime of this facade's client start.
    std::weak_ptr<SteamClientScope> client_scope;
    // Process-wide keyboard lease and typed modal result observation.
    SteamDeviceKeyboard keyboard;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamDevice::Hardware);
VARIANT_ENUM_CAST(crimson::steam::SteamDevice::Preset);
VARIANT_ENUM_CAST(crimson::steam::SteamDevice::FloatingKeyboardMode);
