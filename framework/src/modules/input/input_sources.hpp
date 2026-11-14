// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "controls.hpp"

namespace crimson::input {

// Check an authored or replacement event for its destination action type.
bool valid_binding_event(const godot::Ref<godot::InputEvent> &event, InputAction::ValueKind kind);
// Copy a physical source so authored defaults and detached snapshots stay independent.
godot::Ref<godot::InputEvent> copy_binding_event(const godot::Ref<godot::InputEvent> &event);
// Return the magnitude an incoming event should contribute, or -1 when it targets another control.
float binding_strength(const godot::Ref<godot::InputEvent> &binding, const godot::Ref<godot::InputEvent> &event, bool exact);
// Report whether a raw event belongs to the gamepad device class.
bool is_gamepad(const godot::Ref<godot::InputEvent> &event);
// Return X (0) or Y (1) for an axis of this stick, or -1 for another source.
int stick_component(InputBinding::Stick stick, godot::JoyAxis axis);
// Report whether a signed gamepad event claims either axis of a whole-stick source.
bool stick_overlaps(InputBinding::Stick stick, const godot::Ref<godot::InputEvent> &event);
// Report whether two events claim the same physical source, accounting for wildcards.
bool bindings_overlap(const godot::Ref<godot::InputEvent> &first, const godot::Ref<godot::InputEvent> &second);

} // namespace crimson::input
