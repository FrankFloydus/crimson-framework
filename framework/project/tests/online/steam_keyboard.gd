# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Tests scoped Steam keyboard validation and offers an opt-in rendered hardware check.

const SPACEWAR := 480

var _failures := 0
var _floating_closed := false
var _finished := false


## Run after the scene tree can attach a client.
func _initialize() -> void:
	_run.call_deferred()


## Verify unavailable scopes and headless validation without opening a keyboard.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if not args.is_empty() and args[0] == "graphical":
		await _run_graphical(args)
		return
	if args.size() != 1 or args[0] not in ["missing", "sdk-off", "spacewar"]:
		push_error("Choose missing, sdk-off, spacewar, or graphical <AppID>.")
		quit(2)
		return
	var mode := args[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	await _check_detached(SteamDevice.new())
	var started := steam.start(SPACEWAR)
	match mode:
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.device() == null,
				"Missing Steam runtime keeps the keyboard service unavailable.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.device() == null,
				"SDK-off registers typed keyboard results without starting Steam.")
		"spacewar":
			_check(started == OK, "Initialize Spacewar. %s" % steam.get_initialization_error())
			if started == OK:
				await _check_live(steam)
	steam.free()
	_finish(mode)


## Ensure detached services fail without showing a keyboard or exposing input.
func _check_detached(device: SteamDevice) -> void:
	var result: SteamTextEntryResult = await device.enter_text("Fixture")
	_check(result.error == ERR_UNAVAILABLE and result.value == null and
		device.show_keyboard_for(null) == ERR_UNAVAILABLE and device.dismiss_keyboard() == ERR_UNAVAILABLE,
		"A detached keyboard scope returns typed unavailable outcomes.")


## Verify the signed-in service rejects invalid fields, bounds, and headless display.
func _check_live(steam: SteamClient) -> void:
	var device := steam.device()
	_check(device != null and device.is_available(), "The signed-in Steam scope exposes the device facade.")
	if device == null or not device.is_available():
		return
	var detached := LineEdit.new()
	_check(device.show_keyboard_for(detached) == ERR_INVALID_PARAMETER and device.dismiss_keyboard() == ERR_DOES_NOT_EXIST,
		"A detached field cannot open the keyboard, and there is no keyboard to dismiss.")
	detached.free()
	var oversize: SteamTextEntryResult = await device.enter_text("Fixture", "", 4097)
	var long_initial: SteamTextEntryResult = await device.enter_text("Fixture", "too long", 1)
	var unavailable: SteamTextEntryResult = await device.enter_text("Fixture", "sample")
	_check(oversize.error == ERR_INVALID_PARAMETER and long_initial.error == ERR_INVALID_PARAMETER,
		"Modal keyboard arguments are bounded before calling Steam.")
	_check(unavailable.error == ERR_UNAVAILABLE and unavailable.value == null,
		"A headless client cannot open the modal keyboard.")
	steam.stop()
	await _check_detached(device)
	_check(steam.start(SPACEWAR) == OK and steam.device() != device, "Restart supplies a different device scope.")


## Render a focused editable field and verify real OS keyboard placement and input on supported hardware.
func _run_graphical(args: PackedStringArray) -> void:
	if args.size() < 2 or not args[1].is_valid_int() or int(args[1]) <= 0 or DisplayServer.get_name() == "headless":
		push_error("Graphical verification needs a rendered Steam client and an AppID.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	var sibling := SteamClient.boot(root, int(args[1]))
	if steam == null or sibling == null or not steam.is_ready() or not sibling.is_ready():
		push_error("Steam is unavailable in the rendered app.")
		quit(2)
		return
	var box := VBoxContainer.new()
	box.position = Vector2(30, 30)
	root.add_child(box)
	var instructions := Label.new()
	instructions.text = "Use the Steam keyboard to enter the word transfer in the field, then close it."
	box.add_child(instructions)
	var field := LineEdit.new()
	field.custom_minimum_size.x = 320.0
	field.placeholder_text = "Type transfer"
	box.add_child(field)
	field.grab_focus()
	var device := steam.device()
	device.keyboard_dismissed.connect(_on_floating_closed)
	var deadline := Time.get_ticks_msec() + 10000
	var overlay := steam.overlay()
	while (not root.has_focus() or (overlay != null and not overlay.is_ready())) and Time.get_ticks_msec() < deadline:
		await process_frame
	if device.show_keyboard_for(field) != OK:
		_check(false, "Steam opened the floating keyboard beside the focused field.")
		_finish("graphical")
		return
	var busy: SteamTextEntryResult = await sibling.device().enter_text("This dialog must stay closed")
	_check(busy.error == ERR_BUSY and sibling.device().dismiss_keyboard() == ERR_BUSY,
		"A sibling cannot open or dismiss the current scope's keyboard.")
	deadline = Time.get_ticks_msec() + 45000
	while not _floating_closed and Time.get_ticks_msec() < deadline:
		await process_frame
	_check(_floating_closed and field.has_focus() and field.text.strip_edges() == "transfer",
		"The floating keyboard routed text into the focused field and reported dismissal.")
	if not _floating_closed:
		_finish("graphical")
		return
	instructions.text = "Submit the word transfer in the Steam modal keyboard."
	create_timer(45.0).timeout.connect(_modal_timeout.bind(steam))
	var entry: SteamTextEntryResult = await device.enter_text("Submit the word transfer", "", 16)
	if _finished:
		return
	_check(entry.ok and entry.value != null and entry.value.submitted and entry.value.text == "transfer",
		"The modal keyboard returns typed, copied input.")
	_finish("graphical")


## Record the owning floating keyboard's dismissal.
func _on_floating_closed() -> void:
	_floating_closed = true


## End the interactive test if the modal remains open past its fixture deadline.
func _modal_timeout(steam: SteamClient) -> void:
	if not _finished:
		_check(false, "The Steam modal keyboard settled within the interactive test window.")
		_finish("graphical")
		steam.stop()


## Accumulate independent verification failures.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)


## Print the marker consumed by the isolated harness.
func _finish(mode: String) -> void:
	_finished = true
	if _failures == 0:
		print("CRIMSON_STEAM_KEYBOARD_PASSED %s" % mode)
	quit(int(_failures != 0))
