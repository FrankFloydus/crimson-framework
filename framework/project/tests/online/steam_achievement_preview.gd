# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Unlocks one published test achievement after a deliberate press.
extends SceneTree

## Valve's shared sample AppID.
const SPACEWAR := 480
## Valve's client-writable sample achievement allowed in the Spacewar lane.
const SPACEWAR_ACHIEVEMENT := "ACH_WIN_ONE_GAME"
## Native Steam EResult value for an accepted persistence request.
const STEAM_OK := 1

## Exact Steamworks API name selected at launch.
var _api_name: StringName
## Stats scoped to the client node under the fixture window.
var _stats: SteamStats
## Overlay scoped to the same client and app as the selected achievement.
var _overlay: SteamOverlay
## Fixture feedback for readiness, errors, and completion.
var _status: Label
## The only input that can start an unlock.
var _unlock: Button
## Remains available throughout the fixture and receives focus after submission.
var _close: Button
## First app-wide persistence result observed after the single submission, or -1 while pending.
var _store_result := -1
## Whether Steam reported a complete unlock for the selected achievement.
var _achievement_stored := false
## Determines the fixture exit code when the user closes the window.
var _verified := false


## Run once the root can display controls and own the Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Require a rendered, explicitly authorized launch and wait for the locked achievement and overlay.
func _run() -> void:
	var arguments := PackedStringArray()
	for argument in OS.get_cmdline_user_args():
		if not argument.begins_with("--crimson-steam-overlay="):
			arguments.append(argument)
	var app_id := arguments[0].to_int() if arguments.size() == 2 else 0
	if (app_id < 1 or app_id > 4294967295 or str(app_id) != arguments[0]
			or arguments[1].is_empty() or arguments[1].to_utf8_buffer().size() >= 128):
		push_error("Pass the test AppID and its published achievement API name.")
		quit(2)
		return
	if app_id == SPACEWAR and arguments[1] != SPACEWAR_ACHIEVEMENT:
		push_error("The Spacewar achievement preview accepts only ACH_WIN_ONE_GAME.")
		quit(2)
		return
	if OS.get_environment("CRIMSON_STEAM_STATS_ALLOW_WRITES") != "1" or DisplayServer.get_name() == "headless":
		push_error("Open a rendered window and set CRIMSON_STEAM_STATS_ALLOW_WRITES=1 to enable the test.")
		quit(2)
		return

	_api_name = StringName(arguments[1])
	_build_screen(app_id)
	var steam := SteamClient.boot(root, app_id)
	if steam == null or not steam.is_ready():
		_fail("Steam could not start for this AppID. %s" % (steam.get_initialization_error() if steam != null else ""))
		return
	_stats = steam.stats()
	_overlay = steam.overlay()
	if _stats == null or _overlay == null:
		_fail("Steam Stats or the Steam Overlay is unavailable for this launch.")
		return

	var deadline := Time.get_ticks_msec() + 20_000
	var achievement := _stats.achievement(_api_name)
	while Time.get_ticks_msec() < deadline and (achievement == null or not _overlay.is_ready()):
		if achievement != null and achievement.unlocked:
			break
		await create_timer(0.1).timeout
		achievement = _stats.achievement(_api_name)
	if achievement == null:
		_fail("Steam could not read this achievement. Check its API name and publish its Steamworks settings.")
		return
	if achievement.unlocked:
		_status.text = "Already unlocked for this Steam account. A new notification requires a locked achievement."
		print("CRIMSON_STEAM_ACHIEVEMENT_PREVIEW_ALREADY_UNLOCKED")
		return
	if not _overlay.is_ready():
		_fail("The Steam Overlay did not become ready. Enable it for the test app and relaunch with the overlay bootstrap argument.")
		return

	var app_name := "Spacewar" if app_id == SPACEWAR else "this app"
	_status.text = "Unlock %s in %s on the signed-in Steam account. Watch for the Steam notification." % [achievement.display_name, app_name]
	_unlock.disabled = false
	_unlock.grab_focus()
	print("CRIMSON_STEAM_ACHIEVEMENT_PREVIEW_READY")


## Build a responsive fixture with explicit directional focus and keyboard or controller Cancel.
func _build_screen(app_id: int) -> void:
	auto_accept_quit = false
	root.title = "Steam Achievement Test · App %d" % app_id
	root.close_requested.connect(_exit)
	_bind_controller_action(&"ui_accept", JOY_BUTTON_A)
	_bind_controller_action(&"ui_cancel", JOY_BUTTON_B)
	var margins := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margins.add_theme_constant_override("margin_" + side, 32)
	root.add_child(margins)
	margins.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var panel := VBoxContainer.new()
	panel.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	panel.add_theme_constant_override("separation", 16)
	margins.add_child(panel)

	var title := Label.new()
	title.text = String(_api_name)
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	title.add_theme_font_size_override("font_size", 28)
	panel.add_child(title)
	_status = Label.new()
	_status.text = "Waiting for Steam to load the achievement and overlay."
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 20)
	panel.add_child(_status)

	var actions := HBoxContainer.new()
	actions.add_theme_constant_override("separation", 12)
	panel.add_child(actions)
	_unlock = Button.new()
	_unlock.text = "Unlock achievement"
	_unlock.disabled = true
	_unlock.pressed.connect(_submit.bind(app_id))
	actions.add_child(_unlock)
	_close = Button.new()
	_close.text = "Close"
	_close.pressed.connect(_exit)
	actions.add_child(_close)
	for button in [_unlock, _close]:
		button.custom_minimum_size.y = 44
		button.add_theme_font_size_override("font_size", 20)
		button.gui_input.connect(_on_action_input)
	_unlock.focus_neighbor_right = _unlock.get_path_to(_close)
	_close.focus_neighbor_left = _close.get_path_to(_unlock)
	_close.grab_focus()


## Preserve controller navigation when the fixture runs in a minimal staging project.
func _bind_controller_action(action: StringName, button: JoyButton) -> void:
	var binding := InputEventJoypadButton.new()
	binding.button_index = button
	binding.device = -1
	if not InputMap.action_has_event(action, binding):
		InputMap.action_add_event(action, binding)


## Handle Cancel on the focused action and keep fixture input out of the overlay.
func _on_action_input(event: InputEvent) -> void:
	if _overlay != null and _overlay.is_active():
		root.set_input_as_handled()
		return
	if event.is_action_pressed(&"ui_cancel"):
		root.set_input_as_handled()
		_exit()


## Submit once, await Steam's callbacks, then confirm the updated typed snapshot.
func _submit(app_id: int) -> void:
	_unlock.disabled = true
	_close.grab_focus()
	var achievement := _stats.achievement(_api_name)
	if achievement == null or achievement.unlocked:
		_fail("Steam could not verify a locked achievement. Relaunch with a locked test achievement.")
		return
	if not _overlay.is_ready():
		_fail("Steam Overlay became unavailable. Relaunch the test with Steam Overlay enabled.")
		return

	_status.text = "Waiting for Steam to confirm the unlock."
	_stats.stats_stored.connect(_on_stats_stored, CONNECT_ONE_SHOT)
	_stats.achievement_stored.connect(_on_achievement_stored)
	var submitted := _stats.edit().unlock(String(_api_name)).submit()
	if not submitted.ok or submitted.value == null or submitted.value.applied != 1:
		_fail("Steam did not accept the unlock submission (%s). Check that the achievement allows client writes." % error_string(submitted.error))
		return

	var deadline := Time.get_ticks_msec() + 20_000
	while (Time.get_ticks_msec() < deadline
			and (_store_result == -1 or (_store_result == STEAM_OK and not _achievement_stored))):
		await process_frame
	if _store_result != STEAM_OK or not _achievement_stored:
		_fail("Steam did not confirm the unlock (store result %d). Check your Steam account before testing again." % _store_result)
		return
	achievement = _stats.achievement(_api_name)
	if achievement == null or not achievement.unlocked:
		_fail("Steam reported the unlock, but the refreshed achievement could not confirm it.")
		return

	_verified = true
	_status.text = "Steam confirmed the unlock. Check the Steam notification or open Achievements from the overlay."
	print("CRIMSON_STEAM_ACHIEVEMENT_PREVIEW_PASSED %s" % ("spacewar" if app_id == SPACEWAR else "app"))


## Record Steam's app-wide acknowledgment for this isolated fixture's submission.
func _on_stats_stored(result: int) -> void:
	_store_result = result


## Count only a complete unlock for the exact requested achievement.
func _on_achievement_stored(name: String, current: int, maximum: int) -> void:
	if name == String(_api_name) and current == 0 and maximum == 0:
		_achievement_stored = true


## Keep failures readable until the user closes the fixture.
func _fail(message: String) -> void:
	_status.text = message
	push_error(message)


## Let the user leave after observing Steam and keep the exit code tied to verification.
func _exit() -> void:
	quit(0 if _verified else 1)
