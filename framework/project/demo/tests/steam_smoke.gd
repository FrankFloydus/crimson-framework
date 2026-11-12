# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Opt-in Spacewar smoke. Host and guest require separate signed-in Steam accounts.
extends SceneTree

const DEFAULT_NAMESPACE := "crimson-spacewar-smoke"
const OVERLAY_STARTUP_OPTION := "--crimson-steam-overlay=480"
const INVITE_TARGET_ENV := "CRIMSON_STEAM_INVITE_STEAM_ID"
const OVERLAY_WAIT_MS := 10000
const WAIT_SECONDS := 180.0


## Confirms that a guest reached the authority's mounted world before either side leaves.
class ReadyProof extends Node:
	var game: CrimsonGame
	var session: CrimsonGameSession
	var ready_guest := 0
	var acknowledged := false

	@rpc("any_peer", "call_remote", "reliable", 0)
	func guest_ready(epoch: int) -> void:
		var sender := multiplayer.get_remote_sender_id()
		if ready_guest == 0 and game.is_authority() and game.get_phase() == CrimsonGame.RUNNING \
				and sender > 1 and epoch == game.get_world_epoch() \
				and session.get_peer_identity(sender) != null and game.get_player(sender) != null:
			ready_guest = sender

	@rpc("authority", "call_remote", "reliable", 0)
	func host_acknowledged(epoch: int) -> void:
		acknowledged = multiplayer.get_remote_sender_id() == 1 and not game.is_authority() \
				and game.get_phase() == CrimsonGame.RUNNING and epoch == game.get_world_epoch()


var _provider: CrimsonSteamProvider
var _game: CrimsonGame
var _session: CrimsonGameSession
var _proof: ReadyProof
var _previous_peer: MultiplayerPeer
var _invited_listing := ""


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	var overlay_option := arguments.find(OVERLAY_STARTUP_OPTION)
	if overlay_option >= 0:
		arguments.remove_at(overlay_option)
	if arguments.is_empty() or arguments[0] not in ["runtime", "listen", "host", "guest", "guest-invite"]:
		_usage()
		return
	var role := arguments[0]
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
	_provider = CrimsonSteamProvider.new()
	_provider.app_id = 480
	_provider.lobby_namespace = lobby_namespace
	if lobby_namespace.begins_with("--") or _provider.lobby_namespace != lobby_namespace:
		_fail("The namespace must contain 1–64 ASCII letters, digits, underscores, or hyphens.", 2)
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
	if role == "guest-invite":
		_provider.invite_received.connect(_on_invite_received)
	var scene := load(String(ProjectSettings.get_setting("application/run/main_scene"))) as PackedScene
	var demo := scene.instantiate()
	_game = demo.get_node(^"Game") as CrimsonGame
	_game.definition = _game.definition.with_provider(_provider)
	_proof = ReadyProof.new()
	_proof.name = "SteamSmokeProof"
	_proof.game = _game
	_game.add_child(_proof)
	root.add_child(demo)
	_session = _game.get_node_or_null(^"Session") as CrimsonGameSession
	if _session == null or not _provider.is_ready() or _provider.get_local_steam_id().is_empty():
		_fail("Spacewar could not attach the native provider. Check the signed-in Steam client, AppID 480, and SDK DLL.")
		return
	if invite_from_env and invitee == _provider.get_local_steam_id():
		_fail("The selected guest must use a different Steam account.", 2)
		return
	if not _rejects_inactive_invites():
		_fail("The Steam provider accepted an invalid recipient or an invitation without a lobby.")
		return
	_proof.session = _session
	_previous_peer = _session.get_multiplayer().multiplayer_peer
	match role:
		"listen", "host":
			if not await _start_host():
				return
			if role == "host" and not await _wait_for_guest(invite_overlay, invitee):
				return
		"guest", "guest-invite":
			if role == "guest-invite":
				print("CRIMSON_STEAM_WAITING_FOR_INVITE")
				while _invited_listing.is_empty():
					await process_frame
			var listing_id := arguments[1] if role == "guest" else _invited_listing
			if not await _join_and_confirm(listing_id):
				return
	if role != "runtime" and not _leave_cleanly():
		_fail("Spacewar teardown left a world, listing, or multiplayer peer attached.")
		return
	if role == "listen" and not _rejects_inactive_invites():
		_fail("Spacewar did not retire its invitation scope when leaving the lobby.")
		return
	print("CRIMSON_STEAM_PASSED %s" % role)
	quit(0)


func _start_host() -> bool:
	var started: Dictionary = await _game.host_online().wait()
	if not started.get("ok", false):
		_fail("Spacewar host failed with %s." % started.get("code", "Unknown"))
		return false
	if not _has_world() or not _game.is_authority() or _session.get_local_peer_id() != 1 \
			or not _session.is_accepting_connections() or not _valid_listing(_game.get_online_listing()):
		_fail("The Spacewar host did not mount the demo world, spawn its actor, and publish an open lobby.")
		return false
	return true


func _wait_for_guest(invite_overlay: bool, invitee: String) -> bool:
	var world := _game.get_world()
	var actor := _game.get_local_player()
	var epoch := _game.get_world_epoch()
	print("CRIMSON_STEAM_LOBBY_ID=", _game.get_online_listing())
	if invite_overlay and not await _request_invite_overlay():
		return false
	if not invitee.is_empty():
		var submitted := _provider.invite_player(invitee)
		invitee = ""
		if submitted != OK:
			_fail("Spacewar could not submit the private lobby invitation (error %d)." % submitted)
			return false
		print("CRIMSON_STEAM_INVITE_SUBMITTED")
	while _proof.ready_guest == 0 and _game.get_phase() == CrimsonGame.RUNNING:
		await process_frame
	var guest_id := _proof.ready_guest
	if guest_id <= 1 or not _verified_actor(guest_id) \
			or _session.get_connected_peer_ids() != PackedInt32Array([guest_id]) \
			or _game.get_world() != world or _game.get_local_player() != actor or _game.get_world_epoch() != epoch:
		_fail("The Spacewar guest did not join the running host world with a verified identity and remote actor.")
		return false
	if _proof.rpc_id(guest_id, &"host_acknowledged", epoch) != OK:
		_fail("Spacewar could not acknowledge the guest's ready world.")
		return false
	while _game.get_phase() == CrimsonGame.RUNNING and (_session.get_connected_peer_ids().has(guest_id) \
			or _session.get_peer_identity(guest_id) != null or _game.get_player(guest_id) != null):
		await process_frame
	if not _has_world() or _game.get_world() != world or _game.get_local_player() != actor \
			or _game.get_world_epoch() != epoch or not _session.get_connected_peer_ids().is_empty():
		_fail("Spacewar did not retire the departed guest while preserving the host world and actor.")
		return false
	return true


func _request_invite_overlay() -> bool:
	var deadline := Time.get_ticks_msec() + OVERLAY_WAIT_MS
	var result := _provider.open_invite_overlay()
	while result == ERR_BUSY and Time.get_ticks_msec() < deadline:
		await create_timer(0.125).timeout
		result = _provider.open_invite_overlay()
	if result == OK:
		print("CRIMSON_STEAM_OVERLAY_REQUESTED")
		return true
	print("CRIMSON_STEAM_OVERLAY_UNAVAILABLE")
	_fail("The Steam overlay was unavailable. Use the explicit direct invite option for this launch.")
	return false


func _join_and_confirm(listing_id: String) -> bool:
	var joined: Dictionary = await _game.join_online(listing_id).wait()
	if not joined.get("ok", false):
		_fail("Spacewar guest failed with %s. Check the private invite and matching namespace." % joined.get("code", "Unknown"))
		return false
	var local_id := _session.get_local_peer_id()
	if not _has_world() or _game.is_authority() or local_id <= 1 \
			or _game.get_player(local_id) != _game.get_local_player() or not _verified_actor(1):
		_fail("The Spacewar guest did not mount the demo world with a local actor and verified authority actor.")
		return false
	if _proof.rpc_id(1, &"guest_ready", _game.get_world_epoch()) != OK:
		_fail("Spacewar could not report the guest's ready world to the host.")
		return false
	while not _proof.acknowledged and _game.get_phase() == CrimsonGame.RUNNING:
		await process_frame
	if not _proof.acknowledged or not _has_world():
		_fail("Spacewar did not confirm the same mounted world on both peers.")
		return false
	return true


func _has_world() -> bool:
	var world := _game.get_world()
	var actor := _game.get_local_player()
	var loader := _game.get_node_or_null(^"SceneLoader") as CrimsonSceneLoadManager
	return _game.get_phase() == CrimsonGame.RUNNING and _game.get_world_id() == _game.definition.starting_world \
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
	return _game.leave() == OK and _game.get_phase() == CrimsonGame.IDLE and _session.get_state() == CrimsonGameSession.IDLE \
			and _game.get_online_listing().is_empty() and _game.get_world() == null and _game.get_local_player() == null \
			and _session.get_connected_peer_ids().is_empty() and _session.get_multiplayer().multiplayer_peer == _previous_peer \
			and peer != null and peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED


func _rejects_inactive_invites() -> bool:
	var local_account := _provider.get_local_steam_id()
	var test_account := "76561197960265729" if local_account != "76561197960265729" else "76561197960265730"
	return _provider.invite_player("invalid") == ERR_INVALID_PARAMETER \
			and _provider.invite_player(local_account) == ERR_INVALID_PARAMETER \
			and _provider.invite_player(test_account) == ERR_UNCONFIGURED


func _on_invite_received(listing_id: String) -> void:
	if _invited_listing.is_empty() and _valid_listing(listing_id):
		_invited_listing = listing_id


func _valid_listing(listing_id: String) -> bool:
	return listing_id.length() <= 20 and listing_id.is_valid_int() and listing_id != "0" \
			and not listing_id.begins_with("+") and not listing_id.begins_with("-")


func _usage() -> void:
	_fail("Use runtime [NAMESPACE], listen [NAMESPACE], host [NAMESPACE] [--invite|--invite-from-env], guest LOBBY_ID [NAMESPACE], or guest-invite [NAMESPACE].", 2)


func _fail(message: String, exit_code := 1) -> void:
	if is_instance_valid(_game):
		_game.leave()
	push_error(message)
	quit(exit_code)
