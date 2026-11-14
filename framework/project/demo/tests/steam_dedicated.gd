# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree

const Fixture := preload("res://tests/fixtures/network_fixture.gd")
const APP_ID := 480
## Private-app opt-in for the only fixture that mutates Steam progression.
const PROGRESSION_WRITE := "CRIMSON_STEAM_SERVER_STATS_TEST_WRITE"
## Published server-write integer stat in the private test app.
const PROGRESSION_STAT := "CRIMSON_STEAM_SERVER_STATS_TEST_STAT"
## Absolute signed integer value to persist in the private test app.
const PROGRESSION_VALUE := "CRIMSON_STEAM_SERVER_STATS_TEST_VALUE"
## Published server-write achievement in the private test app.
const PROGRESSION_ACHIEVEMENT := "CRIMSON_STEAM_SERVER_STATS_TEST_ACHIEVEMENT"

## Send an intentionally invalid proof only to this fixture's own direct authority.
class InvalidTicket extends SessionAuthenticator:
	var session: GameSession
	var expected_server := ""

	func _make_proof(_peer_id: int, _authority: bool, challenge: PackedByteArray) -> PackedByteArray:
		var proof := challenge.duplicate()
		proof.append(0)
		return proof

	func _verify(request: SessionAuthRequest) -> void:
		var peer := session.get_multiplayer().multiplayer_peer
		if peer != null and peer.has_method("get_remote_steam_id") \
				and str(peer.call("get_remote_steam_id", 1)) == expected_server:
			request.accept(SessionIdentity.verified(&"steam", expected_server))
		else:
			request.reject()

## Reports any unexpected coordinator call when no Steam hosted deployment exists.
class UnprovisionedCoordinator extends SteamRelayCoordinator:
	var calls := 0

	func _register_server(request: SteamRelayRequest) -> void:
		calls += 1
		request.fail()

	func _withdraw_server(_listing_id: String) -> void:
		calls += 1

func _initialize() -> void:
	_run.call_deferred()

## Exercise the test-owned world with a separate, playerless SteamGameServer authority.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var role := args[0] if not args.is_empty() else "missing"
	var namespace_text := args[1] if args.size() > 1 else "crimson-dedicated-smoke"
	var launch_app := OS.get_environment("SteamAppId")
	var expected_app := launch_app.to_int() if launch_app.is_valid_int() else APP_ID
	if expected_app <= 0 or expected_app > 4294967295:
		_fail("Use a valid Steam AppID.")
		return
	var configuration := SteamProvider.configure().for_app(expected_app).with_lobby_namespace(namespace_text)
	if configuration == null:
		_fail("Use a valid Steam AppID and test namespace.")
		return
	Engine.max_fps = 120
	create_timer(50.0).timeout.connect(func() -> void: _fail("The dedicated Steam fixture timed out."))
	if role in ["guest", "browse-guest", "guest-reconnect", "relay-guest", "progression-guest"]:
		if args.size() != (4 if role == "relay-guest" else 3):
			_fail("Supply the exact dedicated server locator printed by the host.")
			return
		if role == "relay-guest":
			var coordinator := _load_coordinator(args[3])
			if coordinator == null:
				_fail("Supply a trusted SteamRelayCoordinator script or resource.")
				return
			configuration = configuration.with_relay_coordinator(coordinator)
		await _guest(configuration, args[2], role)
		return

	var game_port := args[2].to_int() if args.size() > 2 else 27015
	var query_port := args[3].to_int() if args.size() > 3 else 27016
	var defaults := SteamServer.configure("Crimson Dedicated Smoke", game_port, query_port)
	var version := "1.0.0.0" if expected_app == APP_ID else OS.get_environment("CRIMSON_STEAM_SERVER_VERSION")
	var description := "Spacewar" if expected_app == APP_ID else OS.get_environment("CRIMSON_STEAM_SERVER_DESCRIPTION")
	var install_dir := "Spacewar" if expected_app == APP_ID else OS.get_environment("CRIMSON_STEAM_SERVER_INSTALL_DIR")
	var options := defaults.with_product_metadata(version, description, install_dir).secure()
	if not options.is_valid() or options.get_product_version() != version \
			or options.get_game_description() != description or options.get_install_dir() != install_dir \
			or defaults.get_game_description() != "Crimson" \
			or defaults.with_product_metadata("1.0.0.0", "Spacewar", "../Spacewar").is_valid():
		_fail("Steam server metadata is invalid. For another AppID, supply CRIMSON_STEAM_SERVER_VERSION, CRIMSON_STEAM_SERVER_DESCRIPTION, and CRIMSON_STEAM_SERVER_INSTALL_DIR.")
		return
	if role == "lan-unavailable":
		options = options.advertise_on(SteamServer.LAN)
	var advertise := role == "internet-host"
	if advertise:
		options = options.advertise_on(SteamServer.INTERNET)
	if role == "relay-unavailable":
		options = options.via_sdr(&"fixture")
	var unprovisioned: UnprovisionedCoordinator
	if role == "relay-unprovisioned":
		if not OS.get_environment("SDR_LISTEN_PORT").is_empty():
			_fail("The unprovisioned fixture requires no Steam hosted UDP deployment.")
			return
		unprovisioned = UnprovisionedCoordinator.new()
		configuration = configuration.with_relay_coordinator(unprovisioned)
		options = options.via_sdr(&"fixture")
	if role == "relay-host":
		if args.size() < 6 or args.size() > 7:
			_fail("Supply the hosted relay profile and trusted coordinator resource.")
			return
		var coordinator := _load_coordinator(args[5])
		if coordinator == null:
			_fail("Supply a trusted SteamRelayCoordinator script or resource.")
			return
		var virtual_port := args[6].to_int() if args.size() == 7 else 0
		configuration = configuration.on_virtual_port(virtual_port)
		if configuration == null:
			_fail("Supply a valid Steam virtual port.")
			return
		configuration = configuration.with_relay_coordinator(coordinator)
		options = options.via_sdr(StringName(args[4]))
	var game := Fixture.definition().with_provider(configuration.dedicated(options)).boot(root)
	if game == null or SteamServices.server_for(game) == null or SteamServices.for_game(game) != null:
		_fail("The dedicated Game did not expose a distinct native server scope.")
		return
	var server := SteamServices.server_for(game)
	if role in ["missing", "sdk-off", "lan-unavailable", "relay-unavailable", "relay-unprovisioned"]:
		var unavailable: Dictionary = await game.start_dedicated_online().wait()
		if unavailable.get("code") != "OnlineUnavailable" or server.get_initialization_error().is_empty() \
				or server.status().ready or server.status().listed or game.get_phase() != GameRuntime.IDLE \
				or game.get_world() != null or (unprovisioned != null and unprovisioned.calls != 0):
			_fail("An unavailable dedicated Steam scope failed to report its startup diagnostic.")
			return
		print("CRIMSON_STEAM_DEDICATED_PASSED ", role)
		quit()
		return
	if role not in ["host", "host-only", "internet-host", "host-reconnect", "relay-host", "progression-host"]:
		_fail("Use a documented dedicated host, guest, or unavailability fixture role.")
		return
	if not server.get_initialization_error().is_empty():
		_fail(server.get_initialization_error())
		return
	var session := game.get_node(^"Session") as GameSession
	var selected_address := "" if role == "relay-host" else (args[4] if args.size() > 4 else "127.0.0.1")
	var started: Dictionary = await game.start_dedicated_online(selected_address).wait()
	var status := server.status()
	if not started.get("ok", false) or not status.ready or not status.secure or status.listed != advertise \
			or status.authenticated_players != 0 or status.world_id != Fixture.WORLD_ID \
			or game.get_world() == null or game.get_local_player() != null or game.get_player(1) != null \
			or not session.is_accepting_connections():
		_fail("The certified dedicated Game did not reach its expected playerless readiness. " + String(started.get("code", "")))
		return
	if role in ["host", "internet-host", "host-reconnect", "relay-host", "progression-host"]:
		print("CRIMSON_STEAM_DEDICATED_READY ", game.get_online_listing())
		var original_world := game.get_world()
		var expected_connections := 2 if role == "host-reconnect" else 1
		var verified_peers := PackedInt32Array()
		var retained_stats: SteamServerPlayerStats
		if role == "progression-host" and (server.stats(1) != null or server.stats(2) != null):
			_fail("The server exposed progression for an unadmitted or local peer.")
			return
		while verified_peers.size() < expected_connections or not session.get_connected_peer_ids().is_empty():
			for peer_id in session.get_connected_peer_ids():
				var identity := session.get_peer_identity(peer_id)
				if identity != null and identity.get_provider() == &"steam" and game.get_player(peer_id) != null:
					if server.status().authenticated_players != 1 or game.get_world() != original_world:
						_fail("The dedicated guest changed the live world or admission count.")
						return
					if not verified_peers.has(peer_id):
						verified_peers.append(peer_id)
						if role == "progression-host":
							retained_stats = server.stats(peer_id)
							if not await _verify_progression(retained_stats, expected_app):
								_fail("The dedicated progression check did not observe the expected scoped outcome.")
								return
			await process_frame
		if retained_stats != null and (retained_stats.is_available() or retained_stats.is_loaded() \
				or retained_stats.load().result().error != ERR_UNAUTHORIZED):
			_fail("A departing peer retained access to the dedicated stats cache.")
			return
	if game.leave() != OK or game.get_world() != null or game.get_phase() != GameRuntime.IDLE \
			or server.status().ready or server.status().listed or not game.get_online_listing().is_empty():
		_fail("The dedicated authority did not retire its world, discovery and admission.")
		return
	print("CRIMSON_STEAM_DEDICATED_PASSED ", role)
	quit()

## Confirm the guest verifies the exact dedicated Steam certificate before mounting and spawning.
func _guest(provider: SteamProvider, listing: String, role: String) -> void:
	var game := Fixture.definition().with_provider(provider).boot(root)
	if game == null:
		_fail("The Steam guest could not boot.")
		return
	var steam := SteamServices.for_game(game)
	if steam == null or not steam.is_ready():
		_fail("The dedicated guest requires a signed-in Steam client.")
		return
	var fields := listing.split(":")
	var relayed := role == "relay-guest"
	if fields.size() != (7 if relayed else 6) or fields[0] != ("h1" if relayed else "s1"):
		_fail("The dedicated locator did not have the expected scope.")
		return
	var expected_server := fields[4] if relayed else fields[3]
	var wrong_namespace := fields.duplicate()
	wrong_namespace[2] += "-denied"
	var denied: Dictionary = await game.join_online(":".join(wrong_namespace)).wait()
	if denied.get("code") != "ListingUnavailable" or game.get_world() != null or game.get_phase() != GameRuntime.IDLE:
		_fail("A mismatched dedicated namespace did not fail before mounting.")
		return
	var session := game.get_node(^"Session") as GameSession
	var selected: SteamListing
	if role == "browse-guest":
		var discovered := await _find_advertised(steam.server_browser(), listing)
		if discovered == null:
			_fail("Steam did not return or inspect the exact advertised dedicated server.")
			return
		selected = discovered
	var connections := 2 if role == "guest-reconnect" else 1
	if role == "guest-reconnect":
		if fields[4] != "2130706433":
			_fail("The invalid-ticket fixture requires its own loopback dedicated authority.")
			return
		var verified_auth := session.get_authenticator()
		var invalid_auth := InvalidTicket.new()
		invalid_auth.session = session
		invalid_auth.expected_server = expected_server
		if session.set_authenticator(invalid_auth) != OK:
			_fail("The fixture could not select its invalid proof.")
			return
		var invalid: Dictionary = await game.join_online(listing).wait()
		if invalid.get("code") != "JoinFailed" or game.get_world() != null \
				or session.set_authenticator(verified_auth) != OK:
			_fail("An invalid ticket admitted a guest or left the test authenticator active.")
			return
	var previous_identity: SessionIdentity
	for attempt in range(connections):
		var operation := game.join_listing(selected) if selected != null else game.join_online(listing)
		var joined: Dictionary = await operation.wait()
		var identity := session.get_peer_identity(1)
		if not joined.get("ok", false) or identity == null or identity == previous_identity \
				or identity.get_provider() != &"steam" or identity.get_subject() != expected_server \
				or game.get_world() == null or game.get_local_player() == null:
			_fail("The direct guest did not reach fresh verified world readiness. " + String(joined.get("code", "")))
			return
		var network := steam.networking()
		var route := network.peer_path(1) if network != null else null
		var expected_path := SteamConnectionPath.RELAY if relayed else SteamConnectionPath.DIRECT
		if route == null or not route.ok or route.value == null or route.value.kind != expected_path:
			_fail("Steam did not report the expected live transport route.")
			return
		print("CRIMSON_STEAM_DEDICATED_ROUTE ", "relay" if relayed else "direct")
		previous_identity = identity
		if role == "progression-guest":
			await create_timer(18.0).timeout
			if OS.get_environment(PROGRESSION_WRITE) == "1":
				var stats := steam.stats()
				var observed := stats.stat_int(OS.get_environment(PROGRESSION_STAT)) if stats != null else null
				var achievement := stats.achievement(StringName(OS.get_environment(PROGRESSION_ACHIEVEMENT))) if stats != null else null
				if steam.get_app_id() == APP_ID or observed == null or not observed.ok \
						or observed.value != OS.get_environment(PROGRESSION_VALUE).to_int() \
						or achievement == null or not achievement.unlocked:
					_fail("The private app's client did not observe the server-awarded progress.")
					return
		else:
			await create_timer(0.3).timeout
		if game.leave() != OK or game.get_phase() != GameRuntime.IDLE or session.get_peer_identity(1) != null:
			_fail("The Steam guest did not retire its exact authority identity.")
			return
	print("CRIMSON_STEAM_DEDICATED_PASSED ", role)
	quit()

## Read the native exact-account result, with writes enabled only for an explicitly configured private app.
func _verify_progression(stats: SteamServerPlayerStats, app_id: int) -> bool:
	if stats == null or not stats.is_available() or stats.is_loaded() \
			or stats.stat_int(&"NumGames").error != ERR_UNCONFIGURED:
		return false
	var write := OS.get_environment(PROGRESSION_WRITE) == "1"
	var name := OS.get_environment(PROGRESSION_STAT)
	var achievement_name := OS.get_environment(PROGRESSION_ACHIEVEMENT)
	var value := OS.get_environment(PROGRESSION_VALUE)
	if write and (app_id == APP_ID or name.is_empty() or achievement_name.is_empty() or not value.is_valid_int()):
		return false
	var operation := stats.load()
	if not operation.is_finished() and stats.load().result().error != ERR_BUSY:
		return false
	var loaded: SteamServerStatsResult = await operation.wait()
	if loaded.store_submitted or loaded.applied != 0:
		return false
	if not loaded.ok:
		if app_id == APP_ID and not write and loaded.steam_result > 1 and not stats.is_loaded():
			print("CRIMSON_STEAM_DEDICATED_PROGRESSION_NATIVE_REFUSED ", loaded.steam_result)
			return true
		return false
	if loaded.steam_result != 1 or not stats.is_loaded() \
			or stats.stat_int(&"CrimsonUndefinedDedicatedStat").error != ERR_DOES_NOT_EXIST:
		return false
	if not write:
		if app_id == APP_ID and (not stats.stat_int(&"NumGames").ok or not stats.achievement(&"ACH_WIN_ONE_GAME").ok):
			return false
		print("CRIMSON_STEAM_DEDICATED_PROGRESSION_READ")
		return true
	var draft := stats.edit().set_int(StringName(name), value.to_int()).unlock(StringName(achievement_name))
	var submission := draft.submit()
	if draft.submit() != submission:
		return false
	var stored: SteamServerStatsResult = await submission.wait()
	var after := stats.stat_int(StringName(name))
	var unlocked := stats.achievement(StringName(achievement_name))
	if not stored.ok or not stored.store_submitted or stored.applied != 2 or stored.steam_result != 1 \
			or not after.ok or after.value != value.to_int() or not unlocked.ok or not unlocked.value:
		return false
	print("CRIMSON_STEAM_DEDICATED_PROGRESSION_STORED")
	return true

## Load the developer's trusted hosted relay adapter from the staged project.
func _load_coordinator(path: String) -> SteamRelayCoordinator:
	if not path.begins_with("res://") or not ResourceLoader.exists(path):
		return null
	var resource := load(path)
	if resource is Script and resource.can_instantiate():
		return resource.new() as SteamRelayCoordinator
	return resource as SteamRelayCoordinator

## Read back the advertised server and validate a native inspection before choosing its listing.
func _find_advertised(browser: SteamServerBrowser, expected: String) -> SteamListing:
	for attempt in range(3):
		var found: SteamListingsResult = await browser.query().on_map(Fixture.WORLD_ID).fetch(10000)
		if found.ok:
			for listing in found.value:
				if listing.id == expected:
					var details: SteamServerDetailsResult = await browser.inspect(listing).wait()
					if details.ok and details.value.listing.id == expected:
						return details.value.listing
		if attempt < 2:
			await create_timer(0.5).timeout
	return null

## Finish with a generic diagnostic that does not include a user Steam ID or ticket.
func _fail(message: String) -> void:
	push_error(message)
	quit(1)
