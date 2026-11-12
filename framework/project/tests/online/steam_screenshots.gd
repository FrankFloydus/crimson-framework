# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises typed screenshot drafts without writing to the Spacewar screenshot library.

const SPACEWAR := 480

var _failures := 0
var _saved: Array[SteamScreenshot] = []


## Run after the scene tree can attach a client.
func _initialize() -> void:
	_run.call_deferred()


## Validate unavailable paths before testing the signed-in facade.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if not args.is_empty() and args[0] == "graphical":
		await _run_graphical(args)
		return
	if args.size() != 1 or args[0] not in ["missing", "sdk-off", "spacewar"]:
		push_error("Choose missing, sdk-off, spacewar, or graphical <authorized non-480 AppID>.")
		quit(2)
		return
	var mode := args[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.screenshots() == null, "Screenshots is absent before client startup.")
	await _check_detached(SteamScreenshots.new())
	var started := steam.start(SPACEWAR)
	match mode:
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.screenshots() == null,
				"Missing Steam runtime keeps the screenshot service unavailable.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.screenshots() == null,
				"SDK-off registers typed screenshot classes without starting the service.")
		"spacewar":
			_check(started == OK, "Initialize Spacewar. %s" % steam.get_initialization_error())
			if started == OK:
				await _check_live(steam)
	steam.free()
	_finish(mode)


## Reject detached and expired service handles consistently.
func _check_detached(screenshots: SteamScreenshots) -> void:
	_check(not screenshots.is_available() and screenshots.capture() == ERR_UNAVAILABLE,
		"A detached screenshot scope cannot capture.")
	var result: SteamScreenshotResult = await screenshots.from_image(null).at("Fixture").submit()
	_check(not result.ok and result.error == ERR_UNAVAILABLE and result.value == null,
		"A detached draft settles immediately with a typed unavailable result.")


## Exercise live interface, input bounds, branch semantics, and retained invalidation without a write.
func _check_live(steam: SteamClient) -> void:
	var screenshots := steam.screenshots()
	_check(screenshots != null and screenshots.is_available(), "Spacewar exposes the screenshot interface.")
	if screenshots == null or not screenshots.is_available():
		return
	var empty: SteamScreenshotResult = await screenshots.from_image(null).submit()
	_check(not empty.ok and empty.error == ERR_INVALID_PARAMETER and empty.value == null,
		"A live screenshot draft rejects missing image data before a native write.")
	var image := Image.create_empty(64, 64, true, Image.FORMAT_RGBA8)
	image.fill(Color.CORNFLOWER_BLUE)
	var base := screenshots.from_image(image)
	_check(image.get_format() == Image.FORMAT_RGBA8 and image.has_mipmaps(),
		"Draft creation leaves the source image and mipmaps intact.")
	var branch := base.at("Fixture").tag("not-a-steam-id")
	var other := base.at("é".repeat(128))
	_check(branch != base and other != base and other != branch, "Draft edits are independent branches.")
	var retained := branch.send()
	var branch_result: SteamScreenshotResult = await retained.wait()
	_check(retained.is_finished() and retained.result() == branch_result and
		branch_result.error == ERR_INVALID_PARAMETER and retained.cancel() == ERR_DOES_NOT_EXIST,
		"Invalid retained submission has one settled typed result.")
	var utf8_result: SteamScreenshotResult = await other.submit()
	var item_result: SteamScreenshotResult = await base.tag_item("18446744073709551616").submit()
	_check(utf8_result.error == ERR_INVALID_PARAMETER and item_result.error == ERR_INVALID_PARAMETER,
		"Reject overlong UTF-8 locations and overflowing Workshop IDs before saving.")
	steam.stop()
	await _check_detached(screenshots)
	var stale: SteamScreenshotResult = await base.submit()
	_check(stale.error == ERR_UNAVAILABLE, "A retained photo draft cannot write after its original scope stops.")
	_check(steam.start(SPACEWAR) == OK and steam.screenshots() != screenshots,
		"Restart supplies a different screenshot scope.")


## Render a synthetic frame and submit both save paths on an explicitly authorized test AppID.
func _run_graphical(args: PackedStringArray) -> void:
	if args.size() < 2 or not args[1].is_valid_int() or int(args[1]) <= 0 or int(args[1]) == SPACEWAR or \
			OS.get_environment("CRIMSON_STEAM_SCREENSHOTS_ALLOW_SAVE") != "1" or DisplayServer.get_name() == "headless":
		push_error("Graphical verification needs a rendered non-480 app and CRIMSON_STEAM_SCREENSHOTS_ALLOW_SAVE=1.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.screenshots() == null or not steam.screenshots().is_available():
		push_error("Steam screenshots are unavailable in the configured app.")
		quit(2)
		return
	var scene := ColorRect.new()
	scene.color = Color.CORNFLOWER_BLUE
	root.add_child(scene)
	scene.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var screenshots := steam.screenshots()
	screenshots.screenshot_ready.connect(_record_saved)
	var overlay := steam.overlay()
	var deadline := Time.get_ticks_msec() + 10000
	while overlay != null and not overlay.is_ready() and Time.get_ticks_msec() < deadline:
		await process_frame
	_check(screenshots.capture() == OK, "Steam accepted a request to capture the rendered test frame.")
	deadline = Time.get_ticks_msec() + 12000
	while _saved.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	_check(not _saved.is_empty(), "Observe a confirmed ordinary Steam screenshot save.")
	var image := Image.create_empty(320, 200, false, Image.FORMAT_RGB8)
	image.fill(Color.MIDNIGHT_BLUE)
	var draft := screenshots.from_image(image).at("Crimson screenshot fixture")
	var target := OS.get_environment("CRIMSON_STEAM_SCREENSHOTS_TAG_STEAM_ID")
	var item := OS.get_environment("CRIMSON_STEAM_SCREENSHOTS_TAG_ITEM_ID")
	if not target.is_empty():
		draft = draft.tag(target)
	if not item.is_empty():
		draft = draft.tag_item(item)
	var photo: SteamScreenshotResult = await draft.submit()
	_check(photo.ok and photo.value != null and photo.value.annotations_accepted and
		photo.value.annotation_errors.is_read_only(), "Steam confirmed the exact annotated save and accepted its metadata.")
	await process_frame
	_check(photo.value != null and _saved.has(photo.value), "The ready signal reports the same saved snapshot.")
	_finish("graphical")


## Record a save notification without logging its native handle or user data.
func _record_saved(screenshot: SteamScreenshot) -> void:
	_saved.append(screenshot)


## Accumulate independent verification failures.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)


## Print the marker consumed by the isolated harness.
func _finish(mode: String) -> void:
	if _failures == 0:
		print("CRIMSON_STEAM_SCREENSHOTS_PASSED %s" % mode)
	quit(int(_failures != 0))
