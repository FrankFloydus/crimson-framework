# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises Steam Messages authorization and opt-in two-account transport.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480
## Only app messages on these two fixture channels are allowed.
const RELIABLE_CHANNEL := &"crimson.fixture.reliable"
const TRANSIENT_CHANNEL := &"crimson.fixture.transient"
## Fixed non-account fixture data.
const RELIABLE_DATA := "crimson-fixture-ready"
const TRANSIENT_DATA := "crimson-fixture-sample"


## Start after the root can attach the Steam scope.
func _initialize() -> void:
	_run.call_deferred()


## The default modes send nothing to another Steam account.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.is_empty():
		quit(2)
		return
	var mode := arguments[0]
	var passed := false
	match mode:
		"spacewar", "missing", "sdk-off":
			if arguments.size() == 1:
				passed = _read_only(mode)
		"standalone", "game":
			if OS.get_environment("CRIMSON_STEAM_MESSAGES_ALLOW_LIVE") != "1":
				push_error("Set CRIMSON_STEAM_MESSAGES_ALLOW_LIVE=1 to opt into two-account Steam traffic.")
				quit(2)
				return
			if mode == "standalone" and arguments.size() == 2:
				passed = await _standalone(arguments[1])
			elif mode == "game" and arguments.size() >= 3:
				passed = await _game(arguments)
	if passed:
		print("CRIMSON_STEAM_MESSAGES_PASSED %s" % mode)
	else:
		push_error("Steam Messages fixture did not verify the selected behavior.")
	quit(0 if passed else 1)


## Check inactive, standalone-only and self-authorization boundaries without a network send.
func _read_only(mode: String) -> bool:
	var detached := SteamMessages.new()
	var channels: Array[StringName] = [RELIABLE_CHANNEL, TRANSIENT_CHANNEL]
	var passed := detached.for_peer(1).error == ERR_UNAVAILABLE
	passed = passed and detached.authorize("", channels).error == ERR_UNAVAILABLE
	var steam := SteamClient.boot(root, SPACEWAR)
	if mode == "spacewar":
		var messages := steam.messages()
		passed = passed and steam.is_ready() and messages != null
		if messages != null:
			passed = passed and messages.for_peer(1).error == ERR_UNCONFIGURED
			passed = passed and messages.authorize("", channels).error == ERR_INVALID_PARAMETER
			var self_binding := messages.authorize(steam.get_local_steam_id(), channels)
			passed = passed and self_binding.error == ERR_INVALID_PARAMETER
			steam.stop()
			passed = passed and messages.for_peer(1).error == ERR_UNAVAILABLE
	else:
		passed = passed and not steam.is_ready() and steam.messages() == null
	steam.free()
	return passed


## Authorize only the explicitly named test account in a standalone client.
func _standalone(account: String) -> bool:
	var steam := SteamClient.boot(root, SPACEWAR)
	if not steam.is_ready():
		steam.free()
		return false
	var channels: Array[StringName] = [RELIABLE_CHANNEL, TRANSIENT_CHANNEL]
	var authorization := steam.messages().authorize(account, channels)
	if not authorization.ok:
		steam.free()
		return false
	var sibling := SteamClient.boot(root, SPACEWAR)
	var exclusive := sibling.is_ready() and sibling.messages().authorize(account, channels).error == ERR_BUSY
	sibling.free()
	var passed := exclusive and (await _exchange(authorization.value))
	if passed:
		await create_timer(2.0).timeout
		var rebound := steam.messages().authorize(account, channels)
		passed = rebound.ok and rebound.value != authorization.value
		if passed:
			passed = await _exchange(rebound.value, true, "rebound")
	steam.free()
	return passed


## Bind Game messages through the current demo's verified Steam session.
func _game(arguments: PackedStringArray) -> bool:
	var host := arguments[1] == "host"
	if (not host and arguments[1] != "guest") or arguments.size() != (3 if host else 4):
		return false
	var channels: Array[StringName] = [RELIABLE_CHANNEL, TRANSIENT_CHANNEL]
	var provider := SteamProvider.configure().for_app(SPACEWAR).with_lobby_namespace(arguments[2])
	if provider == null:
		return false
	var scene := load("res://demo/main.tscn") as PackedScene
	var demo := scene.instantiate()
	var game := demo.get_node(^"Game") as GameRuntime
	game.definition = game.definition.with_provider(provider.friends_only().with_message_channels(channels))
	root.add_child(demo)
	var operation := game.host_online() if host else game.join_online(arguments[3])
	var started: Dictionary = await operation.wait()
	if not started.get("ok", false):
		demo.free()
		return false
	if host:
		print("CRIMSON_STEAM_MESSAGES_LOBBY=%s" % game.get_online_listing())
	var steam := SteamServices.for_game(game)
	var messages := steam.messages()
	var denied := messages.authorize(steam.get_local_steam_id(), channels).error == ERR_UNAUTHORIZED
	var session := game.get_node(^"Session") as GameSession
	var deadline := Time.get_ticks_msec() + 90000
	var peer: SteamMessagePeer
	while Time.get_ticks_msec() < deadline and peer == null:
		for id: int in session.get_connected_peer_ids():
			var result := messages.for_peer(id)
			if result.ok:
				peer = result.value
				break
		await create_timer(0.10).timeout
	var passed := denied and peer != null and (await _exchange(peer, false))
	game.leave()
	await process_frame
	if peer != null:
		passed = passed and peer.send(RELIABLE_CHANNEL, RELIABLE_DATA.to_utf8_buffer()) == ERR_UNAVAILABLE
	demo.free()
	return passed


## Observe both explicit delivery modes without accepting an unconfigured channel.
func _exchange(peer: SteamMessagePeer, close_at_end := true, generation := "initial") -> bool:
	var observed := {"reliable": false, "unreliable": false, "foreign_generation": false}
	var reliable_text := "%s:%s" % [RELIABLE_DATA, generation]
	var transient_text := "%s:%s" % [TRANSIENT_DATA, generation]
	peer.received.connect(func(channel: StringName, bytes: PackedByteArray, delivery: int) -> void:
		var text := bytes.get_string_from_utf8()
		if channel == RELIABLE_CHANNEL and text == reliable_text and delivery == SteamDelivery.RELIABLE:
			observed.reliable = true
		elif channel == TRANSIENT_CHANNEL and text == transient_text and delivery == SteamDelivery.UNRELIABLE:
			observed.unreliable = true
		elif channel in [RELIABLE_CHANNEL, TRANSIENT_CHANNEL]:
			observed.foreign_generation = true)
	var reliable := reliable_text.to_utf8_buffer()
	var transient := transient_text.to_utf8_buffer()
	var denied := peer.send(&"crimson.fixture.denied", reliable) == ERR_UNAUTHORIZED
	var deadline := Time.get_ticks_msec() + 25000
	while Time.get_ticks_msec() < deadline and not (observed.reliable and observed.unreliable):
		peer.send(RELIABLE_CHANNEL, reliable)
		peer.send(TRANSIENT_CHANNEL, transient, SteamDelivery.UNRELIABLE)
		await create_timer(0.2).timeout
	var passed: bool = denied and observed.reliable and observed.unreliable and not observed.foreign_generation
	if passed:
		for retry in range(3):
			peer.send(RELIABLE_CHANNEL, reliable)
			peer.send(TRANSIENT_CHANNEL, transient, SteamDelivery.UNRELIABLE)
			await create_timer(0.2).timeout
	if close_at_end:
		passed = passed and not observed.foreign_generation
		passed = peer.close() == OK and passed
		passed = peer.send(RELIABLE_CHANNEL, reliable) == ERR_UNAVAILABLE and passed
	return passed
