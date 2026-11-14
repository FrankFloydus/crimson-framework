# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Opt-in Spacewar smoke. Two-peer roles require separate signed-in Steam accounts.
extends SceneTree

const Fixture := preload("res://tests/fixtures/network_fixture.gd")
const DEFAULT_NAMESPACE := "crimson-spacewar-smoke"
const OVERLAY_STARTUP_OPTION := "--crimson-steam-overlay=480"
const INVITE_TARGET_ENV := "CRIMSON_STEAM_INVITE_STEAM_ID"
const OVERLAY_WAIT_MS := 10000
const WAIT_SECONDS := 180.0


## Confirms that a guest reached the authority's mounted world before either side leaves.
class ReadyProof extends Node:
	var game: GameRuntime
	var session: GameSession
	var ready_guest := 0
	var acknowledged := false

	@rpc("any_peer", "call_remote", "reliable", 0)
	func guest_ready(epoch: int) -> void:
		var sender := multiplayer.get_remote_sender_id()
		if ready_guest == 0 and game.is_authority() and game.get_phase() == GameRuntime.RUNNING \
				and sender > 1 and epoch == game.get_world_epoch() \
				and session.get_peer_identity(sender) != null and game.get_player(sender) != null:
			ready_guest = sender

	@rpc("authority", "call_remote", "reliable", 0)
	func host_acknowledged(epoch: int) -> void:
		acknowledged = multiplayer.get_remote_sender_id() == 1 and not game.is_authority() \
				and game.get_phase() == GameRuntime.RUNNING and epoch == game.get_world_epoch()


var _provider: SteamProvider
var _game: GameRuntime
var _game_actions: Game
var _session: GameSession
var _proof: ReadyProof
var _previous_peer: MultiplayerPeer
var _invited: SteamInvite


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() >= 2 and arguments[0] == "launch-check":
		_verify_launch_invite(arguments[1])
		return
	var overlay_option := arguments.find(OVERLAY_STARTUP_OPTION)
	if overlay_option >= 0:
		arguments.remove_at(overlay_option)
	if arguments.is_empty() or arguments[0] not in [
		"runtime", "listen", "coexist", "host", "guest", "guest-invite", "guest-discover",
		"lobby-query", "scope-unavailable",
	]:
		_usage()
		return
	var role := arguments[0]
	var prefer_relay := arguments.has("--prefer-relay")
	if prefer_relay:
		arguments.remove_at(arguments.find("--prefer-relay"))
	var visibility := "private"
	var launch_lobby := ""
	for index in range(arguments.size() - 1, 0, -1):
		if arguments[index].begins_with("--visibility="):
			visibility = arguments[index].trim_prefix("--visibility=")
			arguments.remove_at(index)
		elif arguments[index] == "+connect_lobby" and index + 1 < arguments.size():
			launch_lobby = arguments[index + 1]
			arguments.remove_at(index + 1)
			arguments.remove_at(index)
	var invite_overlay := role == "host" and arguments[-1] == "--invite"
	var invite_from_env := role == "host" and arguments[-1] == "--invite-from-env"
	if invite_overlay or invite_from_env:
		arguments.remove_at(arguments.size() - 1)
	var expected := 2 if role == "guest" else 1
	if arguments.size() < expected or arguments.size() > expected + 1 \
			or (role == "guest" and not _valid_listing(arguments[1])):
		_usage()
		return
	var lobby_namespace := arguments[expected] if arguments.size() > expected else DEFAULT_NAMESPACE
	var configured := SteamProvider.configure().for_app(480)
	_provider = configured.with_lobby_namespace(lobby_namespace)
	if _provider == null:
		_fail("The namespace must contain 1–64 ASCII letters, digits, underscores, or hyphens.", 2)
		return
	match visibility:
		"private":
			pass
		"friends":
			_provider = _provider.friends_only()
		"public":
			_provider = _provider.public_lobbies()
		"invisible":
			_provider = _provider.invisible_lobby()
		_:
			_usage()
			return
	if prefer_relay:
		_provider = _provider.prefer_relay()
	if configured.lobby_visibility != SteamProvider.PRIVATE or configured.lobby_namespace != "crimson" \
			or configured.prefer_relay_routes or _provider.prefer_relay_routes != prefer_relay:
		_fail("Branching the Steam provider mutated its base defaults.")
		return
	if invite_overlay and DisplayServer.get_name() == "headless":
		_fail("Run the host with a window to request the Steam invite overlay.", 2)
		return
	var invitee := ""
	if invite_from_env:
		invitee = OS.get_environment(INVITE_TARGET_ENV)
		OS.unset_environment(INVITE_TARGET_ENV)
		if invitee.length() != 17 or not invitee.is_valid_int() or invitee.begins_with("+") \
				or invitee.begins_with("-") or invitee.to_int() <= 0:
			_fail("Enter the selected guest's 17-digit SteamID64 in the local host prompt.", 2)
			return

	Engine.max_fps = 120
	create_timer(WAIT_SECONDS).timeout.connect(func() -> void: _fail("The Spacewar fixture timed out."))
	if role == "coexist":
		if await _verify_coexistence():
			print("CRIMSON_STEAM_PASSED coexist")
			quit(0)
		return
	if _attach_fixture(role != "scope-unavailable") == null:
		return
	if role == "scope-unavailable":
		if await _verify_unavailable_scope():
			print("CRIMSON_STEAM_PASSED scope-unavailable")
			quit(0)
		return
	if not launch_lobby.is_empty():
		var pending := _game.invites().pending()
		if pending == null or pending.listing.id != launch_lobby or not pending.from_steam_id.is_empty() \
				or _game.get_phase() != GameRuntime.IDLE:
			_fail("Steam launch invite did not produce an idle, typed pending join request.")
			return
	if invite_from_env and invitee == _provider.get_local_steam_id():
		_fail("The selected guest must use a different Steam account.", 2)
		return
	if not _rejects_inactive_invites():
		_fail("The Steam provider accepted an invalid recipient or an invitation without a lobby.")
		return
	var presence := SteamServices.for_game(_game).friends()
	var previous_connect := presence.rich_presence("connect")
	match role:
		"listen", "host":
			var hosted: GameResult = await _game_actions.host()
			if not _confirm_host(hosted.ok, hosted.code):
				return
			if role == "listen" and not await _verify_join_presence(presence, previous_connect, visibility):
				return
			if role == "host" and not await _wait_for_guest(invite_overlay, invitee):
				return
		"guest", "guest-invite", "guest-discover":
			var joined_ok := false
			var joined_code := ""
			if role == "guest-invite":
				_game_actions.invite_changed.connect(_on_invite_received)
				_invited = _game_actions.pending_invite()
				print("CRIMSON_STEAM_WAITING_FOR_INVITE")
				while _invited == null:
					await process_frame
				var joined: GameResult = await _game_actions.accept_invite(_invited)
				joined_ok = joined.ok
				joined_code = joined.code
			elif role == "guest-discover":
				var joined: GameResult = await _game_actions.find_and_join()
				joined_ok = joined.ok
				joined_code = joined.code
			else:
				var joined: Dictionary = await _game.join_online(arguments[1]).wait()
				joined_ok = joined.get("ok", false)
				joined_code = joined.get("code", "Unknown")
			if not await _confirm_guest(joined_ok, joined_code):
				return
		"lobby-query":
			if not await _verify_lobby_query():
				return
	if role in ["listen", "host", "guest", "guest-invite", "guest-discover"] and not _leave_cleanly():
		_fail("Spacewar teardown left a world, listing, or multiplayer peer attached.")
		return
	if role == "listen" and not _rejects_inactive_invites():
		_fail("Spacewar did not retire its invitation scope when leaving the lobby.")
		return
	if role == "listen" and presence.rich_presence("connect") != previous_connect:
		_fail("Leaving the Game did not retire only the connect route it owned.")
		return
	print("CRIMSON_STEAM_PASSED %s" % role)
	quit(0)


## Attach the fixture's game with this process's Spacewar provider.
func _attach_fixture(require_steam := true) -> Node:
	var owner := Node.new()
	owner.name = &"SteamListenFixture"
	root.add_child(owner)
	_game_actions = GameConfig.from_definition(Fixture.definition().with_provider(_provider)).boot(owner)
	_game = owner.get_node(^"Game") as GameRuntime
	_proof = ReadyProof.new()
	_proof.name = "SteamSmokeProof"
	_proof.game = _game
	_game.add_child(_proof)
	_session = _game.get_node_or_null(^"Session") as GameSession
	if _session == null or (require_steam and (not _provider.is_ready() or _provider.get_local_steam_id().is_empty())):
		_fail("Spacewar could not attach the native provider. Check the signed-in Steam client, AppID 480, and SDK DLL.")
		return null
	_proof.session = _session
	_previous_peer = _session.get_multiplayer().multiplayer_peer
	return owner


## Confirm an SDK-free or failed start still exposes exact typed diagnostics.
func _verify_unavailable_scope() -> bool:
	var scope := SteamServices.for_game(_game)
	var invites := _game.invites()
	if scope == null or scope.is_ready() or scope.get_initialization_error().is_empty() \
			or scope.lobbies().is_available() or invites == null or invites.is_available() \
			or scope.apps() != null or scope.cloud() != null or scope.stats() != null \
			or invites.send("invalid") != ERR_UNAVAILABLE:
		_fail("The unavailable Steam attachment did not expose scoped diagnostics and disabled services.")
		return false
	var hosted: GameResult = await _game_actions.host()
	var discovery: GameResult = await _game_actions.find_and_join()
	if hosted.code != "OnlineUnavailable" or hosted.message != scope.get_initialization_error() \
			or discovery.code != "OnlineUnavailable" or _game.get_phase() != GameRuntime.IDLE \
			or _session.get_state() != GameSession.IDLE:
		_fail("Code-first Steam startup lost the exact diagnostic or fell back to a direct session.")
		return false
	return true


## Exercise detached validation and one native query lane against an empty test namespace.
func _verify_lobby_query() -> bool:
	var query := SteamServices.for_game(_game).lobbies().query()
	var invalid: SteamListingsResult = await query.limit(0).fetch()
	var typed_join: Dictionary = await _game.join_listing(OnlineListing.new()).wait()
	if invalid.error != ERR_INVALID_PARAMETER or typed_join.get("code") != "InvalidListing":
		_fail("The Steam query or typed game join accepted invalid input.")
		return false
	var pending := query.within(SteamLobbyQuery.WORLDWIDE).fetch_operation()
	var busy := query.fetch_operation()
	if pending.is_finished() or busy.result().error != ERR_BUSY or pending.cancel() != OK \
			or pending.result().error != ERR_SKIP:
		_fail("The shared Steam query lane did not reject overlap and settle cancellation.")
		return false
	var deadline := Time.get_ticks_msec() + 25000
	while Time.get_ticks_msec() < deadline:
		var attempt := query.fetch_operation()
		if attempt.is_finished() and attempt.result().error == ERR_BUSY:
			await create_timer(0.05).timeout
			continue
		var result: SteamListingsResult = await attempt.wait()
		if result.ok and result.value.is_empty():
			return await _verify_quick_join_cancel()
		break
	_fail("The Steam search did not drain, or its unique test namespace returned another game's lobby.")
	return false


## Exercise the code-first operation gate and retire a search without opening any world.
func _verify_quick_join_cancel() -> bool:
	var pending: Variant = _game_actions.find_and_join()
	var overlap: GameResult = await _game_actions.find_and_join()
	if typeof(pending) != TYPE_SIGNAL or overlap.code != "GameBusy":
		_fail("Quick join did not reserve the game's startup gate.")
		return false
	var settled: Array[GameResult] = []
	pending.connect(func(result: GameResult) -> void: settled.append(result), CONNECT_ONE_SHOT)
	var left: GameResult = await _game_actions.leave()
	if not left.ok or settled.size() != 1 or settled[0].code != "Cancelled" \
			or _game.get_phase() != GameRuntime.IDLE or _game.get_world() != null \
			or _session.get_state() != GameSession.IDLE:
		_fail("Leaving a quick join did not settle once and keep the game idle.")
		return false
	var deadline := Time.get_ticks_msec() + 25000
	while Time.get_ticks_msec() < deadline:
		var retried: GameResult = await _game_actions.find_and_join()
		if retried.code != "DiscoveryBusy":
			if retried.code == "NoMatches" and _game.get_phase() == GameRuntime.IDLE:
				return true
			break
		await create_timer(0.05).timeout
	_fail("The native Steam search did not drain, or quick join did not report an empty search.")
	return false


## Share the Steam API with a standalone client and retire the exact integrated lease.
func _verify_coexistence() -> bool:
	var client := SteamClient.boot(root, 480)
	if client == null or not client.is_ready():
		_fail("Spacewar could not initialize the standalone Steam client.")
		return false
	var demo := _attach_fixture()
	if demo == null:
		return false
	if client.get_local_steam_id() != _provider.get_local_steam_id():
		_fail("The standalone client and game provider did not share the signed-in Steam account.")
		return false
	var first_scope := SteamServices.for_game(_game)
	var first_cloud := first_scope.cloud() if first_scope != null else null
	var first_stats := first_scope.stats() if first_scope != null else null
	if first_cloud == null or first_stats == null or first_scope.apps() == null \
			or not first_cloud.is_available() or not first_stats.is_available() or first_cloud == client.cloud():
		_fail("The Game did not expose its own active app services through the shared Steam runtime.")
		return false
	var standalone_lobbies := client.lobbies()
	var social: SteamLobbyResult = await standalone_lobbies.create().wait()
	if not social.ok or social.value == null:
		_fail("The standalone scope could not reserve a private social lobby.")
		return false
	var game_attempt: Dictionary = await _game.host_online().wait()
	if game_attempt.get("code") != "ListingPublishFailed" or _game.get_phase() != GameRuntime.IDLE \
			or _session.get_state() != GameSession.IDLE or _game.get_world() != null \
			or _session.get_multiplayer().multiplayer_peer != _previous_peer \
			or standalone_lobbies.current() != social.value or social.value.leave() != OK:
		_fail("A Game publication replaced or released the standalone scope's exact lobby.")
		return false
	if not await _start_host():
		return false
	var game_lobbies := first_scope.lobbies()
	var hosted := game_lobbies.current()
	if hosted == null or hosted.listing().is_social() or hosted.leave() != ERR_UNAUTHORIZED \
			or hosted.set_joinable(false) != ERR_UNAUTHORIZED \
			or game_lobbies.create().result().error != ERR_UNAUTHORIZED \
			or standalone_lobbies.create().result().error != ERR_BUSY:
		_fail("The Game lobby did not retain its membership and admission ownership.")
		return false
	if not await _verify_shared_provider():
		return false
	var world := _game.get_world()
	var listing := _game.get_online_listing()
	var friends := client.friends()
	client.stop()
	await process_frame
	if client.is_ready() or friends.is_available() or not _provider.is_ready() or not _has_world() \
			or _game.get_world() != world or _game.get_online_listing() != listing or not _session.is_accepting_connections():
		_fail("Stopping the standalone client disrupted the published game or retained its client services.")
		return false
	if client.start(480) != OK or friends.is_available() or not _leave_cleanly():
		_fail("The client could not reacquire its own services while the game retired its listing.")
		return false
	if game_lobbies.current() != null or not hosted.members().is_empty() or standalone_lobbies.is_available():
		_fail("Retired lobby handles still exposed a live membership after their owner left.")
		return false

	var original_session := _session
	var retired_scope := SteamServices.for_game(_game)
	var retired_invites := _game.invites()
	var internal_client := original_session.get_node_or_null(^"SteamRuntime/SteamClient") as SteamClient
	if internal_client == null:
		_fail("The persistent Session did not own its internal Steam client.")
		return false
	var pending := retired_scope.lobbies().query().fetch_operation()
	if pending.is_finished():
		_fail("The Steam search did not open before the detach check.")
		return false
	var restart_attempts: Array[int] = []
	var cancelled_with_inactive_services: Array[bool] = []
	pending.completed.connect(func(result: SteamListingsResult) -> void:
		restart_attempts.append(internal_client.start(480))
		cancelled_with_inactive_services.append(result.error == ERR_SKIP and retired_scope.cloud() == null \
			and not first_cloud.is_available() and not first_stats.is_available()),
		CONNECT_ONE_SHOT)
	root.remove_child(demo)
	root.add_child(demo)
	await process_frame
	var reattached := SteamServices.for_game(_game)
	if not _provider.is_ready() or _game.get_node_or_null(^"Session") != original_session \
			or retired_scope.is_ready() or retired_scope.lobbies().is_available() or retired_invites.is_available() \
			or retired_scope.cloud() != null or retired_scope.stats() != null or reattached == null \
			or reattached == retired_scope or reattached.cloud() == null or reattached.stats() == null \
			or reattached.cloud() == first_cloud or reattached.stats() == first_stats \
			or restart_attempts.size() != 1 or restart_attempts[0] == OK \
			or cancelled_with_inactive_services.size() != 1 or not cancelled_with_inactive_services[0]:
		_fail("Reattaching the same game lost its Steam lease or persistent Session.")
		return false
	if not await _start_host():
		return false
	if not _leave_cleanly():
		_fail("The reattached game did not retire its Steam listing and transport.")
		return false

	var previous_authenticator := original_session.get_authenticator()
	demo.queue_free()
	await process_frame
	if not client.is_ready() or not client.friends().is_available() or _provider.is_ready() or previous_authenticator == null:
		_fail("Destroying the game did not retire only its lease while the authenticator was retained.")
		return false
	var replacement := _attach_fixture()
	if replacement == null:
		return false
	if _session.get_authenticator() == previous_authenticator:
		_fail("The replacement game reused the retired authenticator.")
		return false
	if not await _start_host():
		return false
	if not _leave_cleanly() or not _rejects_inactive_invites():
		_fail("The replacement game did not clean up its Steam transport and invitation scope.")
		return false
	replacement.queue_free()
	client.queue_free()
	await process_frame
	return true


## A second Game using the same Resource cannot replace the first attachment.
func _verify_shared_provider() -> bool:
	var owner := SteamServices.for_game(_game)
	var owner_invites := _game.invites()
	var owner_listing := _game.get_online_listing()
	var second := Node.new()
	second.name = &"SteamDuplicateFixture"
	root.add_child(second)
	GameConfig.from_definition(Fixture.definition().with_provider(_provider)).boot(second)
	var duplicate := second.get_node(^"Game") as GameRuntime
	var blocked := SteamServices.for_game(duplicate)
	var stable := blocked != null and blocked != owner and not blocked.is_ready() \
		and not blocked.get_initialization_error().is_empty() and blocked.cloud() == null \
		and not duplicate.invites().is_available() \
		and owner.is_ready() and owner_invites.is_available() and _game.get_online_listing() == owner_listing \
		and _provider.invite_player(owner.get_local_steam_id()) == ERR_BUSY
	second.queue_free()
	await process_frame
	if not stable or not _provider.is_ready() or owner_invites.send("invalid") != ERR_INVALID_PARAMETER:
		_fail("A shared SteamProvider Resource replaced or exposed the wrong Game attachment.")
		return false
	return true


func _start_host() -> bool:
	var started: Dictionary = await _game.host_online().wait()
	return _confirm_host(started.get("ok", false), started.get("code", "Unknown"))


## Verify the exact lobby after either public startup path settles.
func _confirm_host(ok: bool, code: String) -> bool:
	if not ok:
		_fail("Spacewar host failed with %s." % code)
		return false
	if not _has_world() or not _game.is_authority() or _session.get_local_peer_id() != 1 \
			or not _session.is_accepting_connections() or not _valid_listing(_game.get_online_listing()):
		_fail("The Spacewar host did not mount the fixture world, spawn its actor, and publish an open lobby.")
		return false
	var scope := SteamServices.for_game(_game)
	var lobby := scope.lobbies().current() if scope != null else null
	if lobby == null or lobby.listing().id != _game.get_online_listing() \
			or lobby.listing().world_id != _game.definition.starting_world:
		_fail("The exact Game scope did not snapshot its published Steam lobby.")
		return false
	return true


## Verify that a ready, visible game owns its exact join route and clears it when admission closes.
func _verify_join_presence(friends: SteamFriends, previous: String, visibility: String) -> bool:
	var available := visibility in ["friends", "public"]
	var enabled := available and previous.is_empty()
	var expected := "+connect_lobby " + _game.get_online_listing() if enabled else previous
	if not await _wait_for_presence(friends, expected):
		_fail("Steam did not report the expected join route for this visibility.")
		return false
	var steam := SteamServices.for_game(_game)
	var route := steam.lobbies().current().listing()
	var display := friends.rich_presence("steam_display")
	if steam.presence().clear_joinable() != OK or not await _wait_for_presence(friends, previous) \
			or friends.rich_presence("steam_display") != display:
		_fail("Explicit route clear removed foreign or display data, or republished the current lobby.")
		return false
	var expected_publish := OK if enabled else (ERR_BUSY if available else ERR_UNAUTHORIZED)
	if steam.presence().joinable(route) != expected_publish or not await _wait_for_presence(friends, expected):
		_fail("Explicit route publication did not respect lobby visibility or existing key ownership.")
		return false
	if enabled:
		if _session.set_accepting_connections(false) != OK or not await _wait_for_presence(friends, previous):
			_fail("Steam join presence remained live after Game admission closed.")
			return false
		if _session.set_accepting_connections(true) != OK or not await _wait_for_presence(friends, expected):
			_fail("Steam join presence did not return when Game admission reopened.")
			return false
	return true


## Read the startup invitation candidate without joining or mounting a world.
func _verify_launch_invite(expected: String) -> void:
	_provider = SteamProvider.configure().for_app(480)
	if _attach_fixture() == null:
		return
	var invite := _game.invites().pending()
	var matches := invite == null if expected == "none" else invite != null and invite.listing.id == expected
	if not matches or _game.get_phase() != GameRuntime.IDLE or _game.get_world() != null:
		_fail("Startup invite was dropped, misparsed, or started an automatic join.")
		return
	if invite != null:
		var stale := _game_actions.dismiss_invite(SteamInvite.new())
		var cleared: Array[SteamInvite] = []
		_game_actions.invite_changed.connect(func(value: SteamInvite) -> void: cleared.append(value))
		var dismissed := _game_actions.dismiss_invite(invite)
		var remaining := _game_actions.pending_invite()
		var repeated := _game_actions.dismiss_invite(invite)
		if stale.code != "InviteChanged" or not dismissed.ok or remaining != null \
				or cleared.size() != 1 or cleared[0] != null or repeated.code != "InviteChanged":
			_fail("Dismissal expected one cleared invite, got %s, %s, %s, %d, %s." % [
				stale.code, dismissed.code, remaining == null, cleared.size(), repeated.code,
			])
			return
	print("CRIMSON_STEAM_PASSED launch-check")
	quit(0)


## Allow the native frame pump to publish the exact route without printing it.
func _wait_for_presence(friends: SteamFriends, expected: String) -> bool:
	await process_frame
	var deadline := Time.get_ticks_msec() + 2500
	while friends.rich_presence("connect") != expected and Time.get_ticks_msec() < deadline:
		await process_frame
	return friends.rich_presence("connect") == expected


func _wait_for_guest(invite_overlay: bool, invitee: String) -> bool:
	var world := _game.get_world()
	var actor := _game.get_local_player()
	var epoch := _game.get_world_epoch()
	print("CRIMSON_STEAM_LOBBY_ID=", _game.get_online_listing())
	if invite_overlay and not await _request_invite_overlay():
		return false
	if not invitee.is_empty():
		var submitted := _game.invites().send(invitee)
		invitee = ""
		if submitted != OK:
			_fail("Spacewar could not submit the private lobby invitation (error %d)." % submitted)
			return false
		print("CRIMSON_STEAM_INVITE_SUBMITTED")
	while _proof.ready_guest == 0 and _game.get_phase() == GameRuntime.RUNNING:
		await process_frame
	var guest_id := _proof.ready_guest
	if guest_id <= 1 or not _verified_actor(guest_id) \
			or _session.get_connected_peer_ids() != PackedInt32Array([guest_id]) \
			or _game.get_world() != world or _game.get_local_player() != actor or _game.get_world_epoch() != epoch:
		_fail("The Spacewar guest did not join the running host world with a verified identity and remote actor.")
		return false
	if not _report_listen_route(guest_id):
		return false
	if _proof.rpc_id(guest_id, &"host_acknowledged", epoch) != OK:
		_fail("Spacewar could not acknowledge the guest's ready world.")
		return false
	while _game.get_phase() == GameRuntime.RUNNING and (_session.get_connected_peer_ids().has(guest_id) \
			or _session.get_peer_identity(guest_id) != null or _game.get_player(guest_id) != null):
		await process_frame
	if not _has_world() or _game.get_world() != world or _game.get_local_player() != actor \
			or _game.get_world_epoch() != epoch or not _session.get_connected_peer_ids().is_empty():
		_fail("Spacewar did not retire the departed guest while preserving the host world and actor.")
		return false
	return true


func _request_invite_overlay() -> bool:
	var deadline := Time.get_ticks_msec() + OVERLAY_WAIT_MS
	var result := _game_actions.invite_friends()
	while result.code == "OverlayBusy" and Time.get_ticks_msec() < deadline:
		await create_timer(0.125).timeout
		result = _game_actions.invite_friends()
	if result.ok:
		print("CRIMSON_STEAM_OVERLAY_REQUESTED")
		return true
	print("CRIMSON_STEAM_OVERLAY_UNAVAILABLE")
	_fail("The Steam overlay was unavailable. Use the explicit direct invite option for this launch.")
	return false


func _confirm_guest(ok: bool, code: String) -> bool:
	if not ok:
		_fail("Spacewar guest failed with %s. Check lobby availability and matching namespace." % code)
		return false
	var local_id := _session.get_local_peer_id()
	if not _has_world() or _game.is_authority() or local_id <= 1 \
			or _game.get_player(local_id) != _game.get_local_player() or not _verified_actor(1):
		_fail("The Spacewar guest did not mount the fixture world with a local actor and verified authority actor.")
		return false
	if _proof.rpc_id(1, &"guest_ready", _game.get_world_epoch()) != OK:
		_fail("Spacewar could not report the guest's ready world to the host.")
		return false
	while not _proof.acknowledged and _game.get_phase() == GameRuntime.RUNNING:
		await process_frame
	if not _proof.acknowledged or not _has_world():
		_fail("Spacewar did not confirm the same mounted world on both peers.")
		return false
	return _report_listen_route(1)


## Report Steam's measured route after authentication, without requiring a relay when direct is available.
func _report_listen_route(peer_id: int) -> bool:
	var path := SteamServices.for_game(_game).networking().peer_path(peer_id)
	if not path.ok or path.value == null:
		_fail("Steam did not return a path snapshot for the verified listen connection.")
		return false
	match path.value.kind:
		SteamConnectionPath.DIRECT:
			print("CRIMSON_STEAM_LISTEN_ROUTE direct")
		SteamConnectionPath.RELAY:
			print("CRIMSON_STEAM_LISTEN_ROUTE relay")
		_:
			print("CRIMSON_STEAM_LISTEN_ROUTE unknown")
	return true


func _has_world() -> bool:
	var world := _game.get_world()
	var actor := _game.get_local_player()
	var loader := _game.get_node_or_null(^"SceneLoader") as SceneLoadManager
	return _game.get_phase() == GameRuntime.RUNNING and _game.get_world_id() == _game.definition.starting_world \
			and _game.get_world_epoch() > 0 and world != null and actor != null \
			and world.is_ancestor_of(actor) and loader != null and loader.get_current_scene() == world


func _verified_actor(peer_id: int) -> bool:
	var world := _game.get_world()
	var actor := _game.get_player(peer_id)
	var identity := _session.get_peer_identity(peer_id)
	var peer := _session.get_multiplayer().multiplayer_peer
	if world == null or actor == null or actor == _game.get_local_player() or not world.is_ancestor_of(actor) \
			or identity == null or identity.get_provider() != &"steam" \
			or peer == null or not peer.has_method("get_remote_steam_id"):
		return false
	var actual := int(peer.call("get_remote_steam_id", peer_id))
	return actual > 0 and identity.get_subject() == str(actual) and identity.get_subject() != _provider.get_local_steam_id()


func _leave_cleanly() -> bool:
	var peer := _session.get_multiplayer().multiplayer_peer
	return _game.leave() == OK and _game.get_phase() == GameRuntime.IDLE and _session.get_state() == GameSession.IDLE \
			and _game.get_online_listing().is_empty() and _game.get_world() == null and _game.get_local_player() == null \
			and _session.get_connected_peer_ids().is_empty() and _session.get_multiplayer().multiplayer_peer == _previous_peer \
			and peer != null and peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED


func _rejects_inactive_invites() -> bool:
	var local_account := _provider.get_local_steam_id()
	var test_account := "76561197960265729" if local_account != "76561197960265729" else "76561197960265730"
	return _provider.invite_player("invalid") == ERR_INVALID_PARAMETER \
			and _provider.invite_player(local_account) == ERR_INVALID_PARAMETER \
			and _provider.invite_player(test_account) == ERR_UNCONFIGURED


func _on_invite_received(invite: SteamInvite) -> void:
	if _invited == null and invite.listing != null and _valid_listing(invite.listing.id):
		_invited = invite


func _valid_listing(listing_id: String) -> bool:
	return listing_id.length() <= 20 and listing_id.is_valid_int() and listing_id != "0" \
			and not listing_id.begins_with("+") and not listing_id.begins_with("-")


func _usage() -> void:
	_fail("Use runtime, listen, coexist, host, guest LOBBY_ID, guest-invite, guest-discover, lobby-query, or scope-unavailable with optional NAMESPACE, --visibility=private|friends|public|invisible, and --prefer-relay. Use launch-check EXPECTED_LOBBY_ID|none for startup argument checks.", 2)


func _fail(message: String, exit_code := 1) -> void:
	if is_instance_valid(_game):
		_game.leave()
	push_error(message)
	quit(exit_code)
