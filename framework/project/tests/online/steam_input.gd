# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies detached Input and read-only lease behavior without requiring a physical controller.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480

## Number of failed assertions in this run.
var _failures := 0


## Defer setup until the tree can own the standalone client.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the selected SDK configuration and finish with a fixture status.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if not args.is_empty() and args[0] == "graphical":
		await _check_graphical(args)
		return
	if args.size() != 1 or args[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose the Steam Input fixture mode spacewar, missing, or sdk-off.")
		quit(2)
		return

	var detached := SteamInput.new()
	_check(detached.start() == ERR_UNAVAILABLE, "A detached service cannot start the Steam Input interface.")
	detached.stop()
	_check(detached.controllers().is_read_only() and detached.controllers().is_empty(),
		"A detached service supplies an empty read-only controller snapshot.")
	_check_retired_controller(SteamController.new())

	var mode := args[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	var started := steam.start(SPACEWAR)
	match mode:
		"spacewar":
			if started == OK:
				await _check_live(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.input() == null,
				"A missing runtime leaves Input detached.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.input() == null,
				"An SDK-free build keeps the typed Input API and has no active scope.")

	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_INPUT_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Exercise real actions, glyphs, and a deliberately saved test binding on the supplied controller manifest.
func _check_graphical(args: PackedStringArray) -> void:
	if args.size() < 6 or not args[1].is_valid_int() or int(args[1]) <= 0 or not args[2].is_absolute_path() or \
			DisplayServer.get_name() == "headless" or OS.get_environment("CRIMSON_STEAM_INPUT_ALLOW_REBIND") != "1":
		push_error("Use graphical <AppID> <absolute loose manifest> <set> <digital> <analog> [layer] with CRIMSON_STEAM_INPUT_ALLOW_REBIND=1.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.input() == null or steam.input().start(args[2]) != OK:
		push_error("Steam could not start Input with the supplied manifest.")
		quit(2)
		return
	var input := steam.input()
	var status := Label.new()
	root.add_child(status)
	status.text = "Connect the test controller."
	var deadline := Time.get_ticks_msec() + 20000
	while input.controllers().is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	var controllers := input.controllers()
	if controllers.is_empty():
		push_error("No Steam Input controller was detected.")
		quit(1)
		return
	var controller := controllers[0]
	_check(controller.use_action_set(StringName(args[3])) == OK, "Select the supplied action set.")
	var layer := StringName(args[6]) if args.size() >= 7 and not args[6].begins_with("--") else StringName()
	if not layer.is_empty():
		_check(controller.activate_layer(layer) == OK and controller.deactivate_layer(layer) == OK,
			"Activate and remove the supplied action-set layer.")
	var digital := StringName(args[4])
	var analog := StringName(args[5])
	status.text = "Press %s and move %s on the selected test controller." % [digital, analog]
	var pressed := false
	var moved := false
	deadline = Time.get_ticks_msec() + 30000
	while controller.is_connected() and not (pressed and moved) and Time.get_ticks_msec() < deadline:
		var button := controller.digital(digital)
		var axis := controller.analog(analog)
		pressed = pressed or (button.ok and button.value.active and button.value.pressed)
		moved = moved or (axis.ok and axis.value.active and axis.value.value.length() > 0.15)
		await process_frame
	_check(pressed and moved, "Observe a real digital press and analog input from the supplied actions.")
	var glyph := controller.glyph(digital)
	var glyphs := controller.glyphs(digital)
	_check(glyph.ok and glyph.value != null and glyph.value.get_width() > 0 and
		glyphs.ok and not glyphs.value.is_empty() and glyphs.value.is_read_only(), "Load Steam binding glyph textures.")
	if glyph.ok and glyph.value != null:
		var icon := TextureRect.new()
		icon.texture = glyph.value
		icon.position = Vector2(16, 48)
		root.add_child(icon)
	var changed: Array[SteamController] = []
	input.bindings_changed.connect(func(selected: SteamController) -> void: changed.append(selected))
	status.text = "Save a binding change in the Steam panel, then close it."
	var overlay := steam.overlay()
	deadline = Time.get_ticks_msec() + 10000
	while overlay != null and not overlay.is_ready() and Time.get_ticks_msec() < deadline:
		await process_frame
	changed.clear()
	if controller.open_binding_panel() == OK:
		deadline = Time.get_ticks_msec() + 45000
		while controller not in changed and Time.get_ticks_msec() < deadline:
			await process_frame
		_check(controller in changed, "Observe the saved test binding change for the same controller.")
	else:
		_check(false, "Open the Steam controller rebinding panel.")
	input.stop()
	_check_retired_controller(controller)
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_INPUT_PASSED graphical")
	quit(int(_failures != 0))


## Verify one native lease, independent views, and local scope invalidation with the default manifest.
func _check_live(steam: SteamClient) -> void:
	var input := steam.input()
	if input == null:
		_check(false, "The signed client exposes Input.")
		return
	_check(input.controllers().is_empty(), "Steam Input does not start automatically.")
	_check(input.start("res://steam_input_manifest.vdf") == ERR_INVALID_PARAMETER,
		"A packed resource path cannot stand in for an absolute loose manifest.")
	var absent_path := ProjectSettings.globalize_path("user://absent_steam_input_manifest.vdf")
	_check(input.start(absent_path) == ERR_FILE_NOT_FOUND, "A missing explicit manifest fails before SDK startup.")
	var initialized := input.start()
	if initialized == ERR_UNAVAILABLE:
		print("CRIMSON_STEAM_INPUT_UNAVAILABLE_HEADLESS")
		return
	_check(initialized == OK, "The default Steam Input lease starts.")
	if initialized != OK:
		return
	_check(input.start() == OK, "Starting the same lease twice is idempotent.")
	_check(input.start(absent_path) == ERR_BUSY, "A conflicting explicit manifest cannot replace the live lease.")

	var sibling := SteamClient.new()
	root.add_child(sibling)
	if sibling.start(SPACEWAR) != OK:
		_check(false, "A sibling client can share the process Steam runtime.")
		sibling.free()
		return
	var sibling_input := sibling.input()
	if sibling_input == null or sibling_input.start() != OK:
		_check(false, "A default sibling scope shares the already-started native input lease.")
		sibling.free()
		return
	await process_frame
	await process_frame
	var first_views := input.controllers()
	var sibling_views := sibling_input.controllers()
	_check(first_views.is_read_only() and sibling_views.is_read_only() and first_views.size() <= 16,
		"The controller snapshots are typed, bounded, and read-only.")
	_check(first_views.size() == sibling_views.size(), "The two scopes observe the same hardware snapshot.")
	for view in first_views:
		_check(view.is_connected() and view not in sibling_views,
			"Each client scope publishes its own live view of a connected device.")
		var absent := view.digital(&"__crimson_fixture_missing_action")
		_check(absent.error in [ERR_DOES_NOT_EXIST, ERR_UNAVAILABLE],
			"An unknown Steam Input action returns a typed error.")
	print("CRIMSON_STEAM_INPUT_CONTROLLERS count=%d" % first_views.size())

	input.stop()
	input.stop()
	_check(input.controllers().is_empty(), "Repeated local stop invalidates the first scope's views.")
	for view in first_views:
		_check_retired_controller(view)
	for view in sibling_views:
		_check(view.is_connected(), "The second scope retains the live native input lease.")
	sibling.stop()
	_check(sibling_input.controllers().is_empty() and sibling_input.start() == ERR_UNAVAILABLE,
		"Client stop retires the sibling Input service and its controller snapshot.")
	for view in sibling_views:
		_check_retired_controller(view)
	_check(input.start() == OK, "The remaining client can reacquire Input after the final lease stops.")
	input.stop()
	sibling.free()


## Check retired view results without submitting a controller command to Steam.
func _check_retired_controller(view: SteamController) -> void:
	_check(not view.is_connected() and view.type() == SteamController.TYPE_UNKNOWN,
		"A detached controller cannot identify a live device.")
	var digital := view.digital(&"reload")
	var analog := view.analog(&"move")
	var glyph := view.glyph(&"reload")
	var glyphs := view.glyphs(&"reload")
	_check(digital.error == ERR_UNAVAILABLE and digital.value == null and
		analog.error == ERR_UNAVAILABLE and analog.value == null and
		glyph.error == ERR_UNAVAILABLE and glyph.value == null and
		glyphs.error == ERR_UNAVAILABLE and glyphs.value.is_empty() and glyphs.value.is_read_only(),
		"A retired controller reports typed unavailable action and glyph results.")


## Report a fixture assertion without logging opaque controller handles.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
