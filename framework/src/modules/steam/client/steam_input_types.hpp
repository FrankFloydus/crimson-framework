// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace crimson::steam {

class SteamController;

// A digital action's state in the current Steam Input frame.
class SteamDigitalInput : public godot::RefCounted {
    GDCLASS(SteamDigitalInput, godot::RefCounted)

public:
    // Return whether this action has an active binding in the current frame.
    bool is_active() const;
    // Return whether an active action is currently pressed.
    bool is_pressed() const;

protected:
    // Bind the read-only digital action properties.
    static void _bind_methods();

private:
    friend class SteamController;

    // Whether the current action binding is active.
    bool active = false;
    // Whether the active binding is pressed.
    bool pressed = false;
};

// An analog action's values and source mode in the current Steam Input frame.
class SteamAnalogInput : public godot::RefCounted {
    GDCLASS(SteamAnalogInput, godot::RefCounted)

public:
    // Interpret the vector as reported by the current action's configured input mode.
    enum Mode {
        MODE_UNKNOWN = -1, // Steam reported an unrecognized input source mode.
        MODE_NONE = 0, // No source mode is active.
        MODE_DPAD = 1, // Directional pad input.
        MODE_BUTTONS = 2, // Button input.
        MODE_FOUR_BUTTONS = 3, // Four-button input.
        MODE_ABSOLUTE_MOUSE = 4, // Absolute pointer position.
        MODE_RELATIVE_MOUSE = 5, // Relative pointer motion.
        MODE_JOYSTICK_MOVE = 6, // Joystick movement axes.
        MODE_JOYSTICK_MOUSE = 7, // Joystick mouse emulation.
        MODE_JOYSTICK_CAMERA = 8, // Joystick camera control.
        MODE_SCROLL_WHEEL = 9, // Scroll wheel input.
        MODE_TRIGGER = 10, // Trigger input.
        MODE_TOUCH_MENU = 11, // Touch menu input.
        MODE_MOUSE_JOYSTICK = 12, // Mouse joystick emulation.
        MODE_MOUSE_REGION = 13, // Mouse region input.
        MODE_RADIAL_MENU = 14, // Radial menu input.
        MODE_SINGLE_BUTTON = 15, // Single-button input.
        MODE_SWITCHES = 16, // Switch input.
    };

    // Return whether this action has an active binding in the current frame.
    bool is_active() const;
    // Return the Steam-reported values in units appropriate for this mode.
    godot::Vector2 get_value() const;
    // Return the input source mode, or MODE_UNKNOWN for a future SDK mode.
    Mode get_mode() const;

protected:
    // Bind the read-only analog action properties and source mode enum.
    static void _bind_methods();

private:
    friend class SteamController;

    // Whether the current action binding is active.
    bool active = false;
    // Values from the current Steam Input frame.
    godot::Vector2 value;
    // Source mode used to interpret the vector.
    Mode mode = MODE_NONE;
};

// The current digital state, or null when the action could not be read.
class SteamDigitalInputResult : public SteamResult {
    GDCLASS(SteamDigitalInputResult, SteamResult)

public:
    // Return the current digital state, or null when the action could not be read.
    godot::Ref<SteamDigitalInput> get_value() const;

protected:
    // Register the typed payload property.
    static void _bind_methods();

private:
    friend class SteamController;

    // Build the result before returning it to the caller.
    static godot::Ref<SteamDigitalInputResult> create(godot::Error error, godot::Ref<SteamDigitalInput> payload = {});

    // The current digital state, or null when the action could not be read.
    godot::Ref<SteamDigitalInput> value;
};

// The current analog state, or null when the action could not be read.
class SteamAnalogInputResult : public SteamResult {
    GDCLASS(SteamAnalogInputResult, SteamResult)

public:
    // Return the current analog state, or null when the action could not be read.
    godot::Ref<SteamAnalogInput> get_value() const;

protected:
    // Register the typed payload property.
    static void _bind_methods();

private:
    friend class SteamController;

    // Build the result before returning it to the caller.
    static godot::Ref<SteamAnalogInputResult> create(godot::Error error, godot::Ref<SteamAnalogInput> payload = {});

    // The current analog state, or null when the action could not be read.
    godot::Ref<SteamAnalogInput> value;
};

// The loaded texture, or null if the operation failed.
class SteamTextureResult : public SteamResult {
    GDCLASS(SteamTextureResult, SteamResult)

public:
    // Return the loaded texture, or null if the operation failed.
    godot::Ref<godot::Texture2D> get_value() const;

protected:
    // Register the typed payload property.
    static void _bind_methods();

private:
    friend class SteamController;

    // Build the result before returning it to the caller.
    static godot::Ref<SteamTextureResult> create(godot::Error error, godot::Ref<godot::Texture2D> payload = {});

    // The loaded texture, or null if the operation failed.
    godot::Ref<godot::Texture2D> value;
};

// The read-only textures in Steam's reported origin order, empty if the operation failed.
class SteamTexturesResult : public SteamResult {
    GDCLASS(SteamTexturesResult, SteamResult)

public:
    // Initialize an empty read-only array for a newly created result.
    SteamTexturesResult();
    // Return the read-only textures in Steam's reported origin order, empty if the operation failed.
    godot::TypedArray<godot::Texture2D> get_value() const;

protected:
    // Register the typed payload property.
    static void _bind_methods();

private:
    friend class SteamController;

    // Build the result before returning it to the caller.
    static godot::Ref<SteamTexturesResult> create(godot::Error error, godot::TypedArray<godot::Texture2D> payload = {});

    // The read-only textures in Steam's reported origin order, empty if the operation failed.
    godot::TypedArray<godot::Texture2D> value;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamAnalogInput::Mode);
