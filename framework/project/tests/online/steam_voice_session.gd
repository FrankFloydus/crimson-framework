# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises authority voice routing on two opted-in Steam test accounts.
extends SceneTree

## Time allowed for manual host and guest coordination.
const LIVE_TIMEOUT_MS := 90000
## Local only synthetic payload. The policy fixture always denies playback.
const VALIDATION_BYTE := 1


## Run after Steam can attach to the current demo's persistent Game.
func _initialize() -> void:
	_run.call_deferred()


## Use a live host or guest, with either synthetic policy frames or deliberate capture.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if (arguments.size() not in [3, 4] or arguments[0] not in ["voice-policy", "voice"]
			or arguments[1] not in ["host", "guest"] or (arguments[1] == "guest" and arguments.size() != 4)):
		push_error("Use voice-policy|voice host <namespace> or voice-policy|voice guest <namespace> <lobby-id>.")
		quit(2)
		return
	if arguments[0] == "voice" and OS.get_environment("CRIMSON_STEAM_VOICE_ALLOW_MICROPHONE") != "1":
		push_error("Set CRIMSON_STEAM_VOICE_ALLOW_MICROPHONE=1 for deliberate microphone capture.")
		quit(2)
		return
	var provider := SteamProvider.configure().for_app(480).with_lobby_namespace(arguments[2])
	if provider == null:
		quit(2)
		return
	var scene := load("res://demo/main.tscn") as PackedScene
	var demo := scene.instantiate()
	var game := demo.get_node(^"Game") as GameRuntime
	game.definition = game.definition.with_provider(provider.friends_only())
	root.add_child(demo)
	var operation := game.host_online() if arguments[1] == "host" else game.join_online(arguments[3])
	var started: Dictionary = await operation.wait()
	var passed := false
	if started.get("ok", false) and game.voice() != null:
		if arguments[1] == "host":
			print("CRIMSON_STEAM_VOICE_LOBBY=%s" % game.get_online_listing())
		if arguments[0] == "voice-policy":
			passed = await _policy(game, arguments[1] == "host")
		else:
			passed = await _capture(game)
	game.leave()
	demo.free()
	if passed:
		print("CRIMSON_STEAM_VOICE_SESSION_PASSED %s %s" % [arguments[0], arguments[1]])
	else:
		push_error("Steam GameVoice test did not verify the selected outcome.")
	quit(0 if passed else 1)


## Observe bounded authority validation without passing synthetic data to the Steam decoder.
func _policy(game: GameRuntime, hosting: bool) -> bool:
	var voice := game.voice()
	var epoch := game.get_world_epoch()
	var validation_frame := PackedByteArray([VALIDATION_BYTE])
	if hosting:
		var selected: Array[int] = []
		var observed_audio: Array[int] = []
		voice.frame_received.connect(func(peer: int, _pcm: SteamVoicePcm) -> void: observed_audio.append(peer))
		voice.set_recipient_policy(func(sender: int, recipient: int, channel: int) -> bool:
			if recipient == 1 and channel == GameVoice.GROUP:
				selected.append(sender)
			return false)
		voice.call("_voice_submit", epoch, 1, GameVoice.GROUP, validation_frame)
		var deadline := Time.get_ticks_msec() + LIVE_TIMEOUT_MS
		while Time.get_ticks_msec() < deadline and selected.is_empty():
			await process_frame
		await create_timer(0.75).timeout
		if selected.size() != 1 or not observed_audio.is_empty():
			return false
		var session := game.get_node(^"Session") as GameSession
		var identity := session.get_peer_identity(selected[0])
		return identity != null and identity.provider == &"steam" and game.get_player(selected[0]) != null

	await create_timer(0.15).timeout
	var oversized := PackedByteArray()
	oversized.resize(1201)
	voice.rpc_id(1, &"_voice_submit", epoch - 1, 1, GameVoice.GROUP, validation_frame)
	voice.rpc_id(1, &"_voice_submit", epoch, 1, GameVoice.GROUP, oversized)
	voice.rpc_id(1, &"_voice_submit", epoch, 1, 99, validation_frame)
	voice.rpc_id(1, &"_voice_submit", epoch, 1, GameVoice.GROUP, validation_frame)
	await create_timer(0.10).timeout
	voice.rpc_id(1, &"_voice_submit", epoch, 1, GameVoice.GROUP, validation_frame)
	await create_timer(1.25).timeout
	return true


## Capture deliberate speech, observe decoded remote audio, then verify local mute.
func _capture(game: GameRuntime) -> bool:
	var voice := game.voice()
	var received: Array[int] = []
	voice.frame_received.connect(func(peer: int, pcm: SteamVoicePcm) -> void:
		if not pcm.samples.is_empty() and pcm.samples.size() % 2 == 0 and pcm.sample_rate_hz >= 11025:
			received.append(peer))
	if game.is_authority():
		var session := game.get_node(^"Session") as GameSession
		var guest_deadline := Time.get_ticks_msec() + LIVE_TIMEOUT_MS
		while session.get_connected_peer_ids().is_empty() and Time.get_ticks_msec() < guest_deadline:
			await process_frame
	await create_timer(0.5).timeout
	if voice.start_talking() != OK:
		return false
	var standalone := SteamServices.for_game(game).voice().capture()
	if standalone.start() != ERR_BUSY or standalone.read_frame().error != ERR_UNAVAILABLE:
		standalone.stop()
		voice.stop_talking()
		return false
	print("Speak into the configured Steam microphone. The fixture discards decoded audio after checking it.")
	var deadline := Time.get_ticks_msec() + 10000
	while received.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	if received.is_empty():
		voice.stop_talking()
		return false
	var peer := received[0]
	var muted := voice.set_muted(peer) == OK
	var count := received.size()
	await create_timer(0.75).timeout
	var passed := muted and received.size() == count
	passed = voice.stop_talking() == OK and passed
	return passed
