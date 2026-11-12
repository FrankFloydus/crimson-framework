# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Checks standalone Steam Parties reads and explicitly selected two-account beacons.
extends SceneTree

## Valve's shared Steamworks development AppID.
const SPACEWAR := 480
## Bound the live account handoff and reservation join observation.
const LIVE_TIMEOUT_MS := 90000


## Start after the root can attach a Steam client or Game.
func _initialize() -> void:
	_run.call_deferred()


## Select a read-only fixture by default, or an explicitly opted-in two-account test.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.is_empty() or arguments[0] not in ["spacewar", "missing", "sdk-off", "host", "guest"]:
		push_error("Choose a valid Steam Parties fixture mode.")
		quit(2)
		return
	var mode := arguments[0]
	var passed := false
	if mode in ["host", "guest"]:
		if (arguments.size() != 3 or arguments[1].is_empty() or arguments[2].is_empty()
				or OS.get_environment("CRIMSON_STEAM_PARTIES_ALLOW_LIVE") != "1"):
			push_error("Provide an explicit Steam location or beacon, namespace, and live Parties opt-in.")
			quit(2)
			return
		passed = await _live(mode, arguments[1], arguments[2])
	elif arguments.size() == 1:
		passed = await _read_only(mode)
	if passed:
		print("CRIMSON_STEAM_PARTIES_PASSED %s" % mode)
	else:
		push_error("Steam Parties fixture did not verify the selected outcome.")
	quit(0 if passed else 1)


## Read detached types, visible capabilities, and retired scope behavior without reserving a seat.
func _read_only(mode: String) -> bool:
	var detached := SteamParties.new()
	var rejected: SteamListingResult = await detached.reserve(null)
	var passed := detached.locations().error == ERR_UNAVAILABLE and detached.beacons().error == ERR_UNAVAILABLE
	passed = passed and rejected.error == ERR_UNAVAILABLE
	var steam := SteamClient.boot(root, SPACEWAR)
	if mode == "spacewar":
		var parties := steam.parties()
		passed = passed and steam.is_ready() and parties != null
		if parties != null:
			var locations := parties.locations()
			var beacons := parties.beacons()
			passed = passed and (locations.ok or locations.error in [ERR_UNAVAILABLE, FAILED])
			passed = passed and (beacons.ok or beacons.error == ERR_UNAVAILABLE)
			if locations.ok:
				passed = passed and locations.value.is_read_only() and locations.value.size() <= 128
			if beacons.ok:
				passed = passed and beacons.value.is_read_only() and beacons.value.size() <= 128
			print("CRIMSON_STEAM_PARTIES_CAPABILITY locations=%d beacons=%d" % [locations.error, beacons.error])
			steam.stop()
			passed = passed and parties.beacons().error == ERR_UNAVAILABLE
	else:
		passed = passed and not steam.is_ready() and steam.parties() == null
	steam.free()
	return passed


## Use the current demo's normal Steam provider and scene-loading path.
func _live(mode: String, selected_id: String, game_namespace: String) -> bool:
	var scene := load("res://demo/main.tscn") as PackedScene
	var provider := SteamProvider.configure().for_app(SPACEWAR).with_lobby_namespace(game_namespace)
	if scene == null or provider == null:
		return false
	var demo := scene.instantiate()
	var game := demo.get_node(^"Game") as GameRuntime
	game.definition = game.definition.with_provider(provider.friends_only())
	root.add_child(demo)
	var steam := SteamServices.for_game(game)
	if steam == null or not steam.is_ready() or steam.parties() == null:
		demo.free()
		return false
	var passed := false
	if mode == "host":
		passed = await _host(game, steam, selected_id)
	else:
		passed = await _guest(game, steam.parties(), selected_id)
	game.leave()
	demo.free()
	return passed


## Publish only the opted-in location and verify the exact arriving reservation.
func _host(game: GameRuntime, steam: SteamScope, selected_id: String) -> bool:
	var locations := steam.parties().locations()
	if not locations.ok:
		return false
	var location: SteamPartyLocation
	for candidate: SteamPartyLocation in locations.value:
		if candidate.id == selected_id:
			location = candidate
	if location == null or not (await game.host_online().wait()).get("ok", false):
		return false
	var listing := steam.lobbies().current().listing()
	var created: SteamPartyBeaconResult = await steam.parties().publish(listing, location, 1)
	if not created.ok:
		return false
	var beacon := created.value
	var reservations: Array[SteamPartyReservation] = []
	beacon.reservation_received.connect(func(reservation: SteamPartyReservation) -> void:
		reservations.append(reservation))
	print("CRIMSON_STEAM_PARTIES_BEACON=%s" % beacon.id)
	var session := game.get_node(^"Session") as GameSession
	var deadline := Time.get_ticks_msec() + LIVE_TIMEOUT_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if reservations.is_empty():
			continue
		for peer: int in session.get_connected_peer_ids():
			var identity := session.get_peer_identity(peer)
			if identity != null and identity.subject == reservations[0].steam_id and game.get_player(peer) != null:
				await create_timer(0.3).timeout
				var already_completed := beacon.complete(reservations[0]) == ERR_DOES_NOT_EXIST
				return beacon.close() == OK and already_completed
	beacon.close()
	return false


## Reserve only the selected host's beacon and confirm ordinary world-ready admission.
func _guest(game: GameRuntime, parties: SteamParties, selected_id: String) -> bool:
	var deadline := Time.get_ticks_msec() + LIVE_TIMEOUT_MS
	while Time.get_ticks_msec() < deadline:
		var beacons := parties.beacons()
		if beacons.ok:
			for beacon: SteamPartyListing in beacons.value:
				if beacon.id == selected_id:
					var reserved: SteamListingResult = await parties.reserve(beacon)
					if not reserved.ok:
						return false
					var joined: Dictionary = await game.join_listing(reserved.value).wait()
					var ready: bool = joined.get("ok", false) and game.get_local_player() != null
					if ready:
						await create_timer(2.0).timeout
					return ready
		await create_timer(0.25).timeout
	return false
