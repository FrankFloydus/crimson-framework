// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "input_control_labels.hpp"

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/os.hpp>
#include <iterator>

namespace crimson::input::editor_labels {
using namespace godot;

namespace {

constexpr const char *pad_buttons[] = {
    "South Button (A / Cross)", "East Button (B / Circle)", "West Button (X / Square)",
    "North Button (Y / Triangle)", "Back / Select", "Guide", "Start / Menu", "Left Stick Click",
    "Right Stick Click", "Left Bumper", "Right Bumper", "D-pad Up", "D-pad Down", "D-pad Left",
    "D-pad Right", "Miscellaneous Button", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4",
    "Touchpad", "Miscellaneous Button 2", "Miscellaneous Button 3", "Miscellaneous Button 4",
    "Miscellaneous Button 5", "Miscellaneous Button 6",
};
constexpr const char *mouse_buttons[] = {
    "Left Mouse Button", "Right Mouse Button", "Middle Mouse Button", "Wheel Up", "Wheel Down",
    "Wheel Left", "Wheel Right", "Mouse Button 4", "Mouse Button 5",
};
constexpr const char *pad_axes[][2] = {
    {"Left Stick Left", "Left Stick Right"}, {"Left Stick Up", "Left Stick Down"},
    {"Right Stick Left", "Right Stick Right"}, {"Right Stick Up", "Right Stick Down"},
    {"Left Trigger Release", "Left Trigger"}, {"Right Trigger Release", "Right Trigger"},
};
static_assert(std::size(pad_buttons) == JOY_BUTTON_SDL_MAX);
static_assert(std::size(pad_axes) == JOY_AXIS_SDL_MAX);
static_assert(std::size(mouse_buttons) == MOUSE_BUTTON_XBUTTON2 - MOUSE_BUTTON_LEFT + 1);

String with_modifiers(const String &name, const Ref<InputEventWithModifiers> &event) {
    const auto mask = static_cast<Key>(static_cast<int64_t>(event->get_modifiers_mask()));
    const String modifiers = OS::get_singleton()->get_keycode_string(mask);
    return modifiers.is_empty() ? name : modifiers + String("+") + name;
}

String with_gamepad_device(const String &name, int device) {
    return device < 0 ? name : name + String(" (Gamepad ") + String::num_int64(device) + ")";
}

}

String key_name(Key key) {
    return key == KEY_SPACE ? String("Space Bar") : OS::get_singleton()->get_keycode_string(key);
}

String mouse_button_name(MouseButton button) {
    return button >= MOUSE_BUTTON_LEFT && button <= MOUSE_BUTTON_XBUTTON2 ?
            String(mouse_buttons[button - MOUSE_BUTTON_LEFT]) : "Mouse Button " + String::num_int64(button);
}

String gamepad_button_name(JoyButton button) {
    return button >= 0 && button < JOY_BUTTON_SDL_MAX ?
            String(pad_buttons[button]) : "Gamepad Button " + String::num_int64(button);
}

String gamepad_axis_name(JoyAxis axis, bool negative) {
    return axis >= 0 && axis < JOY_AXIS_SDL_MAX ?
            String(pad_axes[axis][negative ? 0 : 1]) : "Gamepad Axis " + String::num_int64(axis) + (negative ? "-" : "+");
}

String control_name(const Ref<InputEvent> &event) {
    if (event.is_null()) {
        return "Unassigned";
    }
    if (const Ref<InputEventKey> key = event; key.is_valid()) {
        Key code = key->get_physical_keycode();
        if (code == KEY_NONE) {
            code = key->get_keycode() != KEY_NONE ? key->get_keycode() : key->get_key_label();
        }
        const String name = key_name(code);
        return name.is_empty() ? String("Unassigned") : with_modifiers(name, key);
    }
    if (const Ref<InputEventMouseButton> mouse = event; mouse.is_valid()) {
        return with_modifiers(mouse_button_name(mouse->get_button_index()), mouse);
    }
    if (const Ref<InputEventJoypadButton> pad = event; pad.is_valid()) {
        return with_gamepad_device(gamepad_button_name(pad->get_button_index()), pad->get_device());
    }
    if (const Ref<InputEventJoypadMotion> axis = event; axis.is_valid()) {
        return with_gamepad_device(gamepad_axis_name(axis->get_axis(), axis->get_axis_value() < 0), axis->get_device());
    }
    if (Ref<InputEventMouseMotion>(event).is_valid()) {
        return "Mouse Motion";
    }
    return event->get_class();
}

}

#endif
