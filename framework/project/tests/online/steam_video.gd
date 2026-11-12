# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Checks Steam Video status and requests for an AppID without a video entitlement.
extends SceneTree

## Valve's shared development AppID, which is a game.
const SPACEWAR := 480


## Run the fixture after the root can attach a Steam client node.
func _initialize() -> void:
	_run.call_deferred()


## Check typed negative outcomes and the local broadcast status.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if not arguments.is_empty() and arguments[0] in ["entitled", "graphical"]:
		await _check_entitled(arguments)
		return
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose a valid Steam Video fixture mode.")
		quit(2)
		return

	var mode := arguments[0]
	var steam := SteamClient.boot(root, SPACEWAR)
	var detached := SteamVideo.new()
	var invalid: SteamStringResult = await detached.stream_url(0)
	var unavailable: SteamStringResult = await detached.projection_settings(SPACEWAR)
	var passed := invalid.error == ERR_INVALID_PARAMETER and unavailable.error == ERR_UNAVAILABLE
	passed = passed and detached.broadcast_status().error == ERR_UNAVAILABLE
	if mode == "spacewar":
		passed = passed and steam.is_ready() and steam.video() != null
		if steam.video() != null:
			passed = (await _check_spacewar(steam)) and passed
	else:
		passed = passed and not steam.is_ready() and steam.video() == null
	steam.free()
	if passed:
		print("CRIMSON_STEAM_VIDEO_PASSED %s" % mode)
	else:
		push_error("Steam Video status, lookup, or scope verification failed.")
	quit(0 if passed else 1)


## Query an entitled test video without printing its URL or projection data.
func _check_entitled(args: PackedStringArray) -> void:
	if args.size() < 3 or not args[1].is_valid_int() or int(args[1]) <= 0 or not args[2].is_valid_int() or int(args[2]) <= 0:
		push_error("Use entitled or graphical <running AppID> <entitled video AppID> [projection].")
		quit(2)
		return
	if args[0] == "graphical" and (DisplayServer.get_name() == "headless" or
			OS.get_environment("CRIMSON_STEAM_VIDEO_ALLOW_BROADCAST") != "1"):
		push_error("The graphical private-broadcast lane requires CRIMSON_STEAM_VIDEO_ALLOW_BROADCAST=1.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.video() == null:
		push_error("The Steam Video test scope is unavailable.")
		quit(2)
		return
	var video := steam.video()
	var url: SteamStringResult = await video.stream_url(int(args[2]))
	var passed := url.ok and (url.value.begins_with("https://") or url.value.begins_with("http://"))
	if "projection" in args:
		var projection: SteamStringResult = await video.projection_settings(int(args[2]))
		passed = passed and projection.ok and not projection.value.is_empty()
	if args[0] == "graphical":
		passed = (await _check_broadcast(video)) and passed
	steam.free()
	if passed:
		print("CRIMSON_STEAM_VIDEO_PASSED %s" % args[0])
	else:
		push_error("Entitled Steam Video or private-broadcast verification failed.")
	quit(int(not passed))


## Observe caller-controlled private broadcast callbacks and the active status snapshot.
func _check_broadcast(video: SteamVideo) -> bool:
	var started: Array[bool] = []
	var stopped: Array[int] = []
	video.broadcast_started.connect(func() -> void: started.append(true))
	video.broadcast_stopped.connect(func(reason: int) -> void: stopped.append(reason))
	var status := Label.new()
	status.text = "Start the controlled private Steam broadcast from your test viewer."
	root.add_child(status)
	var deadline := Time.get_ticks_msec() + 45000
	while started.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	var active := video.broadcast_status()
	var observed_active := not started.is_empty() and active.ok and active.value != null and \
		active.value.uploading and active.value.viewers >= 0
	status.text = "Stop the private broadcast from Steam."
	deadline = Time.get_ticks_msec() + 45000
	while stopped.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	var idle := video.broadcast_status()
	return observed_active and not stopped.is_empty() and idle.ok and idle.value != null and not idle.value.uploading


## Test sibling observations of the same native lookup and a different callback family.
func _check_spacewar(steam: SteamClient) -> bool:
	var video := steam.video()
	var status := video.broadcast_status()
	var passed := status.error == ERR_UNAVAILABLE or (status.ok and status.value.viewers >= 0)
	var sibling := SteamClient.boot(root, SPACEWAR)
	if not sibling.is_ready() or sibling.video() == null:
		sibling.free()
		return false

	var url_a: Array[SteamStringResult] = []
	var url_b: Array[SteamStringResult] = []
	var projection: Array[SteamStringResult] = []
	_observe(video.stream_url(SPACEWAR), url_a)
	_observe(sibling.video().stream_url(SPACEWAR), url_b)
	_observe(video.projection_settings(SPACEWAR), projection)
	var deadline := Time.get_ticks_msec() + 18000
	while (url_a.is_empty() or url_b.is_empty() or projection.is_empty()) and Time.get_ticks_msec() < deadline:
		await process_frame

	passed = passed and not url_a.is_empty() and not url_b.is_empty() and not projection.is_empty()
	if passed:
		passed = not url_a[0].ok and url_a[0].error == url_b[0].error and not projection[0].ok
		print("CRIMSON_STEAM_VIDEO_STATUS broadcast=%d url=%d projection=%d" %
			[status.error, url_a[0].error, projection[0].error])
	steam.stop()
	var retired: SteamStringResult = await video.stream_url(SPACEWAR)
	passed = passed and retired.error == ERR_UNAVAILABLE and sibling.is_ready()
	sibling.free()
	return passed


## Observe a direct result or an asynchronous typed signal immediately.
func _observe(pending: Variant, destination: Array[SteamStringResult]) -> void:
	if pending is Signal:
		var completion: Signal = pending
		completion.connect(func(result: SteamStringResult) -> void: destination.append(result), CONNECT_ONE_SHOT)
	else:
		destination.append(pending)
