// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "input_sources.hpp"

#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>

#include <algorithm>
#include <cmath>

namespace crimson::input {
using namespace godot;

bool valid_binding_event(const Ref<InputEvent> &event, InputAction::ValueKind kind) {
    if (event.is_null()) {
        return true;
    }
    if (kind == InputAction::MOTION) {
        return Object::cast_to<InputEventMouseMotion>(event.ptr()) != nullptr;
    }
    if (const auto *key = Object::cast_to<InputEventKey>(event.ptr())) {
        return key->get_physical_keycode() != KEY_NONE || key->get_keycode() != KEY_NONE || key->get_key_label() != KEY_NONE;
    }
    if (const auto *button = Object::cast_to<InputEventMouseButton>(event.ptr())) {
        return button->get_button_index() >= MOUSE_BUTTON_LEFT && button->get_button_index() <= MOUSE_BUTTON_XBUTTON2;
    }
    if (const auto *button = Object::cast_to<InputEventJoypadButton>(event.ptr())) {
        return button->get_button_index() >= 0 && button->get_button_index() < JOY_BUTTON_MAX;
    }
    if (const auto *motion = Object::cast_to<InputEventJoypadMotion>(event.ptr())) {
        return motion->get_axis() >= 0 && motion->get_axis() < JOY_AXIS_MAX &&
                std::isfinite(motion->get_axis_value()) && std::abs(motion->get_axis_value()) > 0.0f;
    }
    return false;
}

Ref<InputEvent> copy_binding_event(const Ref<InputEvent> &event) {
    return event.is_valid() ? Ref<InputEvent>(event->duplicate()) : Ref<InputEvent>();
}

float binding_strength(const Ref<InputEvent> &binding, const Ref<InputEvent> &event, bool exact) {
    if (binding.is_null() || event.is_null()) {
        return -1.0f;
    }
    if (const auto *axis = Object::cast_to<InputEventJoypadMotion>(binding.ptr())) {
        const auto *source = Object::cast_to<InputEventJoypadMotion>(event.ptr());
        if (!source || axis->get_axis() != source->get_axis() ||
                (axis->get_device() >= 0 && axis->get_device() != source->get_device())) {
            return -1.0f;
        }
        const float value = source->get_axis_value();
        return std::isfinite(value) && (value * axis->get_axis_value() > 0.0f) ? std::min(1.0f, std::abs(value)) : 0.0f;
    }
    if (const auto *button = Object::cast_to<InputEventJoypadButton>(binding.ptr())) {
        const auto *source = Object::cast_to<InputEventJoypadButton>(event.ptr());
        if (!source || button->get_button_index() != source->get_button_index() ||
                (button->get_device() >= 0 && button->get_device() != source->get_device())) {
            return -1.0f;
        }
        return source->is_pressed() ? 1.0f : 0.0f;
    }
    return binding->is_match(event, exact) ? (event->is_pressed() ? 1.0f : 0.0f) : -1.0f;
}

bool is_gamepad(const Ref<InputEvent> &event) {
    return event.is_valid() && (Object::cast_to<InputEventJoypadButton>(event.ptr()) ||
            Object::cast_to<InputEventJoypadMotion>(event.ptr()));
}

int stick_component(InputBinding::Stick stick, JoyAxis axis) {
    switch (stick) {
        case InputBinding::LEFT_STICK:
            return axis == JOY_AXIS_LEFT_X ? 0 : axis == JOY_AXIS_LEFT_Y ? 1 : -1;
        case InputBinding::RIGHT_STICK:
            return axis == JOY_AXIS_RIGHT_X ? 0 : axis == JOY_AXIS_RIGHT_Y ? 1 : -1;
        default:
            return -1;
    }
}

bool stick_overlaps(InputBinding::Stick stick, const Ref<InputEvent> &event) {
    const auto *axis = Object::cast_to<InputEventJoypadMotion>(event.ptr());
    return axis && stick_component(stick, axis->get_axis()) >= 0;
}

bool bindings_overlap(const Ref<InputEvent> &first, const Ref<InputEvent> &second) {
    if (first.is_null() || second.is_null()) {
        return false;
    }
    if (is_gamepad(first) && is_gamepad(second) && first->get_device() >= 0 && second->get_device() >= 0 &&
            first->get_device() != second->get_device()) {
        return false;
    }
    if (const auto *a = Object::cast_to<InputEventJoypadMotion>(first.ptr())) {
        const auto *b = Object::cast_to<InputEventJoypadMotion>(second.ptr());
        return b && a->get_axis() == b->get_axis() && a->get_axis_value() * b->get_axis_value() > 0.0f;
    }
    return first->is_match(second, true) || second->is_match(first, true);
}

} // namespace crimson::input
