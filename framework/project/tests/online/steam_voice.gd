# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies the typed Steam voice surface without opening a microphone by default.
extends SceneTree

## Valve's shared Steamworks test AppID.
const SPACEWAR := 480


## Start after the root can attach a Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Check input limits, inactive capture, and exact scope shutdown.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off", "capture"]:
		push_error("Choose a valid Steam Voice fixture mode.")
		quit(2)
		return

	var mode := arguments[0]
	if mode == "capture" and OS.get_environment("CRIMSON_STEAM_VOICE_ALLOW_MICROPHONE") != "1":
		push_error("Set CRIMSON_STEAM_VOICE_ALLOW_MICROPHONE=1 to explicitly opt into this microphone test.")
		quit(2)
		return
	var steam := SteamClient.boot(root, SPACEWAR)
	var detached := SteamVoice.new()
	var bytes := PackedByteArray([1])
	var oversized := PackedByteArray()
	oversized.resize(8193)
	var passed := detached.capture() == null and detached.decode(bytes).error == ERR_UNAVAILABLE
	passed = passed and detached.decode(oversized).error == ERR_INVALID_PARAMETER
	passed = passed and detached.decode(bytes, 8000).error == ERR_INVALID_PARAMETER
	if mode in ["spacewar", "capture"]:
		passed = passed and steam.is_ready() and steam.voice() != null
		if steam.voice() != null:
			var voice := steam.voice()
			var capture := voice.capture()
			passed = passed and capture != null and capture.read_frame().error == ERR_UNAVAILABLE
			if mode == "capture" and passed:
				passed = await _capture(voice, capture)
			steam.stop()
			passed = passed and capture.start() == ERR_UNAVAILABLE and voice.decode(bytes).error == ERR_UNAVAILABLE
	else:
		passed = passed and not steam.is_ready() and steam.voice() == null
	steam.free()
	if passed:
		print("CRIMSON_STEAM_VOICE_PASSED %s" % mode)
	else:
		push_error("Steam Voice capability, bounds, or teardown verification failed.")
	quit(0 if passed else 1)


## Explicit test lane. Capture is held locally, decoded, and discarded without playback.
func _capture(voice: SteamVoice, capture: SteamVoiceCapture) -> bool:
	var sibling := SteamClient.boot(root, SPACEWAR)
	if not sibling.is_ready() or capture.start() != OK:
		sibling.free()
		return false
	var exclusive := sibling.voice().capture().start() == ERR_BUSY
	print("Speak into the configured Steam microphone for up to five seconds.")
	var deadline := Time.get_ticks_msec() + 5000
	var decoded := false
	while Time.get_ticks_msec() < deadline and not decoded:
		await process_frame
		var frame := capture.read_frame()
		if frame.ok:
			var pcm := voice.decode(frame.value.bytes)
			decoded = pcm.ok and not pcm.value.samples.is_empty() and pcm.value.samples.size() % 2 == 0
	var stopped := capture.stop() == OK
	sibling.free()
	return exclusive and decoded and stopped
