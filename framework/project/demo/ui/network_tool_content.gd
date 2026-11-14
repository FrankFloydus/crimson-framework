# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends VBoxContainer
## Routes window-local shortcuts after the selected control has handled its own input.

## Request that the owning debugger closes and gives input back to the game.
signal dismissed


func _unhandled_input(event: InputEvent) -> void:
	if not event.is_pressed() or event.is_echo():
		return
	var key: InputEventKey = event as InputEventKey
	var button: InputEventJoypadButton = event as InputEventJoypadButton
	if event.is_action_pressed(&"ui_cancel") or (key != null and (key.keycode == KEY_F8 or key.physical_keycode == KEY_F8)) \
			or (button != null and button.button_index == JOY_BUTTON_BACK):
		get_viewport().set_input_as_handled()
		dismissed.emit()
