# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises detached and live Overlay scopes and optionally observes a real graphical pane.

const SPACEWAR := 480
const APP_ID_MAX := 4294967295

var _failures := 0
var _original_overlay: SteamOverlay
var _replacement_overlay: SteamOverlay
var _original_events: Array[bool] = []
var _replacement_events: Array[bool] = []


## Run after the root window is available and this tree can attach a client.
func _initialize() -> void:
	_run.call_deferred()


## Keep the headless checks independent of the opt-in graphical actions.
func _run() -> void:
	var args := PackedStringArray()
	for arg in OS.get_cmdline_user_args():
		if not arg.begins_with("--crimson-steam-overlay="):
			args.append(arg)
	if args.size() >= 1 and args[0] == "graphical":
		await _run_graphical(args)
		return
	if args.size() != 1 or args[0] not in ["missing", "sdk-off", "spacewar"]:
		push_error("Choose missing, sdk-off, spacewar, or graphical <AppID> <pane> [target].")
		quit(2)
		return

	var mode := args[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.overlay() == null, "A client exposes no overlay before starting.")
	_check(_is_detached(SteamOverlay.new()), "An unattached overlay rejects every open action.")
	var started := steam.start(SPACEWAR)
	match mode:
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.overlay() == null,
				"An SDK-enabled client exposes no overlay without the Steam runtime.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.overlay() == null,
				"The SDK-off build registers the type and keeps the service unavailable.")
		"spacewar":
			if started == OK:
				_check_live_scope(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
	steam.free()
	_finish(mode)


## Exercise input rejection, a shared runtime, and replacement of the exact client scope.
func _check_live_scope(steam: SteamClient) -> void:
	var overlay := steam.overlay()
	_check(overlay != null, "The live Steam scope exposes its overlay.")
	if overlay == null:
		return
	_check(overlay.open_store(-1) == ERR_INVALID_PARAMETER and
		overlay.open_store(APP_ID_MAX + 1) == ERR_INVALID_PARAMETER, "Reject invalid store AppIDs.")
	for invalid_id in ["", "+76561197960265729", "18446744073709551616", "1", "1 2"]:
		_check(overlay.open_profile(invalid_id) == ERR_INVALID_PARAMETER and
			overlay.open_trade(invalid_id) == ERR_INVALID_PARAMETER, "Reject malformed or non-individual SteamIDs.")
	for invalid_url in ["steam://open/friends", "https:///missing-host", "https://:443/path",
			"https://user@example.com", "https://example.com:65536", "https://example.com/a b",
			"https://example.com/" + "a".repeat(2048)]:
		_check(overlay.open_web(invalid_url) == ERR_INVALID_PARAMETER, "Reject invalid overlay browser URLs.")
	var ready := overlay.is_ready()
	print("CRIMSON_STEAM_OVERLAY_STATE ready=%s active=%s" % [str(ready), str(overlay.is_active())])
	if not ready:
		_check(overlay.open_achievements() == ERR_BUSY and overlay.open_store() == ERR_BUSY and
			overlay.open_profile(steam.get_local_steam_id()) == ERR_BUSY and
			overlay.open_web("https://store.steampowered.com/") == ERR_BUSY,
			"Valid requests report an overlay that has yet to become accessible.")

	var sibling := SteamClient.boot(root, SPACEWAR)
	_check(sibling != null and sibling.is_ready() and sibling.overlay() != null,
		"A sibling scope has an independent facade for the process overlay.")
	steam.stop()
	_check(steam.overlay() == null and _is_detached(overlay),
		"Stopping the owner invalidates its retained facade even while the process runtime lives.")
	_check(steam.start() == OK and steam.overlay() != null and steam.overlay() != overlay and _is_detached(overlay),
		"Restart supplies a fresh facade and cannot revive the old one.")
	if sibling != null:
		sibling.free()


## Request one authorized pane and observe open and close callbacks in a rendered window.
func _run_graphical(args: PackedStringArray) -> void:
	if (args.size() < 3 or args.size() > 4 or not args[1].is_valid_int() or
			args[1].to_int() < 1 or args[1].to_int() > APP_ID_MAX or DisplayServer.get_name() == "headless"):
		_check(false, "Graphical verification requires a rendered window and positive AppID.")
		_finish("graphical")
		return
	var action := args[2]
	var target := args[3] if args.size() == 4 else ""
	if (action not in ["achievements", "store", "profile", "web", "trade"] or
			(action == "achievements" and not target.is_empty()) or
			(action in ["web", "trade"] and target.is_empty())):
		_check(false, "Choose a supported pane and supply the required target.")
		_finish("graphical")
		return
	var app_id := args[1].to_int()
	var instruction := Label.new()
	instruction.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	instruction.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	instruction.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	instruction.add_theme_font_size_override("font_size", 22)
	root.add_child(instruction)
	instruction.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	instruction.text = "Waiting for the Steam overlay"
	var steam := SteamClient.boot(root, app_id)
	_original_overlay = steam.overlay() if steam != null else null
	if _original_overlay == null:
		_check(false, "Steam startup failed. %s" % (steam.get_initialization_error() if steam != null else "No client."))
		_finish("graphical")
		return
	_original_overlay.activation_changed.connect(_on_original_activation)
	var ready := await _wait_until(func() -> bool: return _original_overlay.is_ready(), 20_000)
	if not ready:
		_check(false, "Steam did not make the overlay accessible within 20 seconds.")
		_finish("graphical")
		return

	instruction.text = "Check the Steam %s pane. Close it with the Steam overlay shortcut." % action
	var issued := _open_requested_pane(steam, action, target)
	if issued != OK:
		_check(false, "Opening the selected pane returned %s." % error_string(issued))
		_finish("graphical")
		return
	print("CRIMSON_STEAM_OVERLAY_REQUEST_ISSUED %s" % action)
	var opened := await _wait_until(func() -> bool: return _original_events.has(true), 20_000)
	if not opened:
		_check(false, "Steam did not report activation for the issued request.")
		_finish("graphical")
		return

	# A new consumer reads the open state even after the original client stops.
	var sibling := SteamClient.boot(root, app_id)
	_replacement_overlay = sibling.overlay() if sibling != null else null
	if _replacement_overlay == null:
		_check(false, "A second overlay scope could not start.")
		_finish("graphical")
		return
	_check(_replacement_overlay.is_active(), "A new scope sees the cached active state.")
	_replacement_overlay.activation_changed.connect(_on_replacement_activation)
	steam.stop()
	_check(_is_detached(_original_overlay) and _replacement_overlay.is_active(),
		"Stopping the original scope leaves the shared activation state intact.")
	var closed := await _wait_until(func() -> bool: return _replacement_events.has(false), 90_000)
	_check(closed and not _replacement_overlay.is_active(), "Steam reported the overlay close on the live scope.")
	_check(_original_events == [true] and _replacement_events == [false],
		"Each live scope received its transition once and the stopped scope received no later signals.")
	_finish("graphical")


## Map an explicitly selected fixture action to one overlay request.
func _open_requested_pane(steam: SteamClient, action: String, target: String) -> Error:
	match action:
		"achievements":
			return _original_overlay.open_achievements()
		"store":
			if target.is_empty():
				return _original_overlay.open_store()
			return _original_overlay.open_store(target.to_int()) if target.is_valid_int() else ERR_INVALID_PARAMETER
		"profile":
			return _original_overlay.open_profile(steam.get_local_steam_id() if target.is_empty() else target)
		"web":
			return _original_overlay.open_web(target)
		"trade":
			return _original_overlay.open_trade(target)
	return ERR_INVALID_PARAMETER


## Confirm the observer sees the process state updated before the callback.
func _on_original_activation(active: bool) -> void:
	_check(_original_overlay.is_active() == active, "The active state is published before the signal.")
	_original_events.append(active)
	print("CRIMSON_STEAM_OVERLAY_ACTIVATION %s" % str(active))


## Record the close after ownership of observation has moved to the sibling.
func _on_replacement_activation(active: bool) -> void:
	_check(_replacement_overlay.is_active() == active, "The replacement scope observes the published state.")
	_replacement_events.append(active)
	print("CRIMSON_STEAM_OVERLAY_ACTIVATION %s" % str(active))


## Wait for a native callback or a deadline without pausing the main-thread Steam pump.
func _wait_until(condition: Callable, timeout_msec: int) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_msec
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return condition.call()


## Check that all methods on a stale or unattached facade return unavailable defaults.
func _is_detached(overlay: SteamOverlay) -> bool:
	return (not overlay.is_ready() and not overlay.is_active() and
		overlay.open_achievements() == ERR_UNAVAILABLE and overlay.open_store() == ERR_UNAVAILABLE and
		overlay.open_profile("") == ERR_UNAVAILABLE and overlay.open_web("") == ERR_UNAVAILABLE and
		overlay.open_trade("") == ERR_UNAVAILABLE)


## Preserve the test's exact success marker and a failing process exit on an assertion.
func _finish(mode: String) -> void:
	if _failures == 0:
		print("CRIMSON_STEAM_OVERLAY_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Record a failing assertion without logging an account identifier or a web target.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
