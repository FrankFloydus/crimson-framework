// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef DEBUG_ENABLED

#include <godot_cpp/classes/input_event.hpp>

namespace crimson::input::editor_labels {

godot::String key_name(godot::Key key);
godot::String mouse_button_name(godot::MouseButton button);
godot::String gamepad_button_name(godot::JoyButton button);
godot::String gamepad_axis_name(godot::JoyAxis axis, bool negative);

// Describe an authored source in the same language as the editor that displays it.
godot::String control_name(const godot::Ref<godot::InputEvent> &event);

}

#endif
