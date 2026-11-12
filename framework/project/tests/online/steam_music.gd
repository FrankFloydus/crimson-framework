# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Checks the client music capability without changing playback or volume.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480


## Run the fixture after the root can attach a Steam client node.
func _initialize() -> void:
	_run.call_deferred()


## Check the typed read-only status and unavailable states.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if not arguments.is_empty() and arguments[0] == "controls":
		await _check_controls(arguments)
		return
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose a valid Steam Music fixture mode.")
		quit(2)
		return

	var mode := arguments[0]
	var steam := SteamClient.boot(root, SPACEWAR)
	var detached := SteamMusic.new()
	var passed := detached.status().error == ERR_UNAVAILABLE and detached.play() == ERR_UNAVAILABLE
	passed = passed and detached.set_volume(INF) == ERR_INVALID_PARAMETER
	if mode == "spacewar":
		var service := steam.music()
		passed = passed and steam.is_ready() and service != null
		if service != null:
			var status := service.status()
			passed = passed and (status.ok or status.error == ERR_UNAVAILABLE)
			if status.ok:
				passed = passed and status.value != null and status.value.volume >= 0.0 and status.value.volume <= 1.0
				print("CRIMSON_STEAM_MUSIC_STATUS enabled=%s" % status.value.enabled)
				if not status.value.enabled:
					passed = passed and service.pause() == ERR_UNAVAILABLE
			else:
				print("CRIMSON_STEAM_MUSIC_STATUS interface_unavailable")
			steam.stop()
			passed = passed and service.status().error == ERR_UNAVAILABLE
	else:
		passed = passed and not steam.is_ready() and steam.music() == null
	steam.free()
	if passed:
		print("CRIMSON_STEAM_MUSIC_PASSED %s" % mode)
	else:
		push_error("Steam Music status or scope verification failed.")
	quit(0 if passed else 1)


## Exercise the operator's dedicated soundtrack queue and restore its volume and playback state.
func _check_controls(args: PackedStringArray) -> void:
	if args.size() < 2 or not args[1].is_valid_int() or int(args[1]) <= 0 or \
			OS.get_environment("CRIMSON_STEAM_MUSIC_ALLOW_CONTROLS") != "1":
		push_error("Use controls <AppID> with a dedicated active soundtrack queue and CRIMSON_STEAM_MUSIC_ALLOW_CONTROLS=1.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.music() == null:
		push_error("The test Steam music service is unavailable.")
		quit(2)
		return
	var music := steam.music()
	var before := music.status()
	if not before.ok or before.value == null or not before.value.enabled or \
			before.value.playback not in [SteamMusicStatus.PLAYING, SteamMusicStatus.PAUSED]:
		push_error("Steam Music requires an enabled, active test soundtrack queue in playing or paused state.")
		quit(2)
		return
	var playback_events: Array[bool] = []
	var volume_events: Array[float] = []
	music.playback_changed.connect(func() -> void: playback_events.append(true))
	music.volume_changed.connect(func(level: float) -> void: volume_events.append(level))
	var volume := 0.35 if before.value.volume > 0.5 else 0.65
	var paused := music.pause() == OK and await _wait_for_playback(music, SteamMusicStatus.PAUSED)
	var playing := music.play() == OK and await _wait_for_playback(music, SteamMusicStatus.PLAYING)
	var adjusted := music.set_volume(volume) == OK and await _wait_for_volume(music, volume)
	var next_track := music.next() == OK
	await create_timer(0.5).timeout
	var previous_track := music.previous() == OK
	var restored_volume := music.set_volume(before.value.volume) == OK and await _wait_for_volume(music, before.value.volume)
	var restored_playback := music.play() if before.value.playback == SteamMusicStatus.PLAYING else music.pause()
	var restored := restored_playback == OK and await _wait_for_playback(music, before.value.playback)
	var passed := paused and playing and adjusted and next_track and previous_track and restored_volume and restored and \
		not playback_events.is_empty() and not volume_events.is_empty()
	steam.free()
	if passed:
		print("CRIMSON_STEAM_MUSIC_PASSED controls")
	else:
		push_error("Steam Music control acknowledgement, observed state, or restoration check failed.")
	quit(int(not passed))


## Wait for the actual client playback status after a Steam control request.
func _wait_for_playback(music: SteamMusic, desired: SteamMusicStatus.Playback) -> bool:
	var deadline := Time.get_ticks_msec() + 5000
	while Time.get_ticks_msec() < deadline:
		var result := music.status()
		if result.ok and result.value != null and result.value.playback == desired:
			return true
		await process_frame
	return false


## Wait for the actual normalized Steam client volume after a request.
func _wait_for_volume(music: SteamMusic, desired: float) -> bool:
	var deadline := Time.get_ticks_msec() + 5000
	while Time.get_ticks_msec() < deadline:
		var result := music.status()
		if result.ok and result.value != null and absf(result.value.volume - desired) < 0.02:
			return true
		await process_frame
	return false
