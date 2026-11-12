# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercise the boot menu through Godot's input pipeline and the configured native game.

var _failures: Array[String] = []


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var demo := (load("res://demo/main.tscn") as PackedScene).instantiate()
	root.add_child(demo)
	await process_frame
	await process_frame
	var game: CrimsonGame = demo.get_node(^"Game")
	var menu: Control = demo.get_node(^"MainMenu")
	var options: Button = menu.find_child("Options", true, false)
	var address: LineEdit = menu.get_node(^"%Address")
	var status: Label = menu.get_node(^"%Status")
	_expect(_focused(&"Solo"), "Single Player initially has focus")
	_expect(options.visible and options.disabled, "Options is visible and disabled")
	_click(options)
	_expect(game.get_phase() == CrimsonGame.IDLE and _focused(&"Solo"), "Clicking Options has no effect")

	_key(KEY_DOWN)
	_expect(_focused(&"OpenMultiplayer"), "Arrow key reaches Multiplayer")
	_key(KEY_ENTER)
	_expect(_focused(&"Host"), "Enter opens Multiplayer and focuses Host Game")
	var port: Label = menu.get_node(^"%Port")
	_expect(port.text.ends_with(str(game.definition.multiplayer_rules.port)), "Multiplayer displays the configured port")
	var definition := game.definition
	game.definition = null
	_key(KEY_ENTER)
	_expect(game.get_phase() == CrimsonGame.IDLE and not status.text.is_empty() and _focused(&"Host"), "A failed host operation keeps the menu open with feedback")
	game.definition = definition
	_key(KEY_ESCAPE)
	_expect(_focused(&"OpenMultiplayer"), "Escape returns to the previous focus")

	_click(menu.get_node(^"%OpenMultiplayer"))
	_joy(JOY_BUTTON_DPAD_DOWN)
	_expect(_focused(&"OpenJoin"), "D-pad reaches Join Game")
	_joy(JOY_BUTTON_A)
	_expect(_focused(&"Connect"), "Gamepad Confirm opens Join Game")
	_joy(JOY_BUTTON_B)
	_expect(_focused(&"OpenJoin"), "Gamepad Back restores Join Game focus")
	_joy(JOY_BUTTON_A)
	_joy(JOY_BUTTON_DPAD_UP)
	_expect(_focused(&"Address"), "The host address is reachable with directional focus")
	_joy(JOY_BUTTON_A)
	_expect(_focused(&"Address") and address.is_editing(), "Gamepad Confirm enters address editing")
	_joy(JOY_BUTTON_B)
	_expect(_focused(&"Address") and not address.is_editing(), "Gamepad Back first ends editing on the form")
	await process_frame
	await process_frame
	_click(address)
	_key(KEY_A, true)
	_key(KEY_BACKSPACE)
	_key(KEY_ENTER)
	_expect(game.get_phase() == CrimsonGame.IDLE and not status.text.is_empty(), "Submitting an empty address leaves the menu open with feedback")

	address.text = "127.0.0.1"
	_click(menu.get_node(^"%Connect"))
	_expect(game.get_phase() == CrimsonGame.TRANSITIONING and _focused(&"Cancel"), "Joining directs focus to Cancel")
	var connect: Button = menu.get_node(^"%Connect")
	_expect(connect.get_focus_mode_with_override() == Control.FOCUS_NONE and connect.get_mouse_filter_with_override() == Control.MOUSE_FILTER_IGNORE, "A pending join blocks input to the form")
	_joy(JOY_BUTTON_B)
	var restored_join := await _wait_for(func() -> bool: return game.get_phase() == CrimsonGame.IDLE and _focused(&"Connect"))
	_expect(restored_join and status.text.is_empty(), "Cancel restores the form without reporting an error")
	_joy(JOY_BUTTON_B)
	_joy(JOY_BUTTON_B)

	_key(KEY_UP)
	_key(KEY_ENTER)
	var started := await _wait_for(func() -> bool: return game.get_phase() == CrimsonGame.RUNNING)
	_expect(started and game.get_local_player() != null and not menu.visible, "Single Player spawns the local player and hides the menu")
	game.leave()
	var restored_solo := await _wait_for(func() -> bool: return menu.visible and _focused(&"Solo"))
	_expect(restored_solo, "Returning from a session restores menu focus")
	if _failures.is_empty():
		print("CRIMSON_MENU_PASS")
	else:
		for failure in _failures:
			push_error(failure)
	quit(int(not _failures.is_empty()))


func _wait_for(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + 5000
	while not condition.call():
		if Time.get_ticks_msec() >= deadline:
			return false
		await process_frame
	return true


func _expect(condition: bool, description: String) -> void:
	if not condition:
		_failures.append(description)


func _focused(name: StringName) -> bool:
	var focused := root.gui_get_focus_owner()
	return focused != null and focused.name == name


func _key(code: Key, with_control: bool = false) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.ctrl_pressed = with_control
		event.pressed = pressed
		root.push_input(event, true)


func _joy(button: JoyButton) -> void:
	for pressed in [true, false]:
		var event := InputEventJoypadButton.new()
		event.button_index = button
		event.pressed = pressed
		root.push_input(event, true)


func _click(control: Control) -> void:
	for pressed in [true, false]:
		var event := InputEventMouseButton.new()
		event.position = control.get_global_rect().get_center()
		event.global_position = event.position
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = pressed
		root.push_input(event, true)
