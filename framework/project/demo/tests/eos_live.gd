# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises the real EOS provider against a private deployment and Dev Auth accounts.
extends SceneTree

const DEFINITION := preload("res://demo/session/transfer_hall.tres")
const SETTINGS := [
	"CRIMSON_EOS_PRODUCT_ID",
	"CRIMSON_EOS_SANDBOX_ID",
	"CRIMSON_EOS_DEPLOYMENT_ID",
	"CRIMSON_EOS_CLIENT_ID",
	"CRIMSON_EOS_CLIENT_SECRET",
]


## Acknowledges guest gameplay readiness over the same EOS peer as the match.
class ReadyProof extends Node:
	## The host only records readiness from an admitted, spawned guest.
	var game: CrimsonGame
	## Remote connection that reached its ready world and sent the proof RPC.
	var ready_guest := 0
	## Whether the guest received the authority's reliable confirmation.
	var acknowledged := false

	@rpc("any_peer", "call_remote", "reliable", 0)
	func guest_ready() -> void:
		var sender := multiplayer.get_remote_sender_id()
		if game.is_authority() and sender > 1 and game.get_player(sender) != null:
			ready_guest = sender

	@rpc("authority", "call_remote", "reliable", 0)
	func host_acknowledged() -> void:
		acknowledged = multiplayer.get_remote_sender_id() == 1


var _game: CrimsonGame
var _session: CrimsonGameSession
var _provider: CrimsonEOSProvider
var _proof: ReadyProof
var _force_relay := false


## Defer startup until the root can own the provider poller and game.
func _initialize() -> void:
	_run.call_deferred()


## Read local configuration without copying its values into diagnostics.
func _run() -> void:
	var args := Array(OS.get_cmdline_user_args())
	_force_relay = args.has("--force-relay")
	args.erase("--force-relay")
	var hosting := args == ["--host"]
	var joining: bool = args.size() == 2 and args[0] == "--join" and not String(args[1]).is_empty()
	if not hosting and not joining:
		_fail("Use --host or --join LOBBY_ID, optionally followed by --force-relay.", 2)
		return
	var credential_key := "CRIMSON_EOS_HOST_CREDENTIAL" if hosting else "CRIMSON_EOS_GUEST_CREDENTIAL"
	for name: String in SETTINGS + [credential_key]:
		if OS.get_environment(name).is_empty():
			_fail("Missing environment variable %s." % name, 2)
			return
	var address := OS.get_environment("CRIMSON_EOS_DEV_AUTH_ADDRESS")
	if address.is_empty():
		address = "127.0.0.1:6547"
	create_timer(180.0).timeout.connect(func() -> void: _fail("EOS live fixture timed out."))

	_provider = CrimsonEOSProvider.configure(
		OS.get_environment(SETTINGS[0]),
		OS.get_environment(SETTINGS[1]),
		OS.get_environment(SETTINGS[2]),
		OS.get_environment(SETTINGS[3]),
		OS.get_environment(SETTINGS[4]),
		_force_relay,
	)
	var login: Dictionary = await _provider.login_developer(root, address, OS.get_environment(credential_key)).wait()
	if not login.get("ok", false) or not _provider.is_ready():
		_fail("EOS developer sign-in failed with %s." % login.get("code", "Unknown"))
		return

	_game = DEFINITION.with_provider(_provider).create()
	_proof = ReadyProof.new()
	_proof.name = "EOSLiveProof"
	_proof.game = _game
	_game.add_child(_proof)
	root.add_child(_game)
	_session = _game.get_node_or_null(^"Session") as CrimsonGameSession
	if _session == null:
		_fail("The EOS provider did not initialize a game session.")
		return
	if hosting:
		await _run_host()
	else:
		await _run_guest(String(args[1]))


## Publish the actual running Transfer Hall, then verify an authenticated guest and seat cleanup.
func _run_host() -> void:
	var started: Dictionary = await _game.host_online().wait()
	if not started.get("ok", false) or not _has_world() or not _game.is_authority():
		_fail("EOS host startup failed with %s." % started.get("code", "Unknown"))
		return
	var listing := _game.get_online_listing()
	if listing.is_empty():
		_fail("The ready EOS host did not publish a lobby ID.")
		return
	print("CRIMSON_EOS_LOBBY_ID=", listing)
	while _proof.ready_guest == 0:
		await process_frame
	var peer_id := _proof.ready_guest
	if not await _verified_peer(peer_id):
		_fail("The ready guest lacked a matching EOS identity or required relay path.")
		return
	if _proof.rpc_id(peer_id, &"host_acknowledged") != OK:
		_fail("Could not acknowledge the ready EOS guest.")
		return
	while _session.get_connected_peer_ids().has(peer_id):
		await process_frame
	if _session.get_peer_identity(peer_id) != null or _game.get_player(peer_id) != null:
		_fail("Leaving did not clear the EOS guest's identity and actor.")
		return
	_game.leave()
	print("CRIMSON_EOS_HOST_PASSED")
	quit()


## Join by EOS lobby ID and send the proof RPC only after scene and baseline readiness.
func _run_guest(listing: String) -> void:
	var joined: Dictionary = await _game.join_online(listing).wait()
	if not joined.get("ok", false) or not _has_world() or _game.is_authority():
		_fail("EOS guest startup failed with %s." % joined.get("code", "Unknown"))
		return
	if not await _verified_peer(1):
		_fail("The host lacked a matching EOS identity or required relay path.")
		return
	if _proof.rpc_id(1, &"guest_ready") != OK:
		_fail("Could not report EOS guest readiness.")
		return
	while not _proof.acknowledged:
		await process_frame
	_game.leave()
	print("CRIMSON_EOS_GUEST_PASSED")
	quit()


## Require the demo world and the local actor produced by the normal game facade.
func _has_world() -> bool:
	return _game.get_phase() == CrimsonGame.RUNNING and _game.get_world() != null \
			and _game.get_local_player() != null


## Compare verified session identity with the actual P2P endpoint without logging either PUID.
func _verified_peer(peer_id: int) -> bool:
	var identity := _session.get_peer_identity(peer_id)
	var peer := _session.get_multiplayer().multiplayer_peer
	if identity == null or identity.get_provider() != &"eos" or peer == null \
			or not peer.has_method("get_remote_product_user_id") or not peer.has_method("get_peer_path"):
		return false
	var actual := String(peer.call("get_remote_product_user_id", peer_id))
	var path := StringName(peer.call("get_peer_path", peer_id))
	var deadline := Time.get_ticks_msec() + 5000
	while path == &"unknown" and Time.get_ticks_msec() < deadline:
		await process_frame
		if _session.get_multiplayer().multiplayer_peer != peer:
			return false
		path = StringName(peer.call("get_peer_path", peer_id))
	print("CRIMSON_EOS_PEER_PATH=", path)
	return not actual.is_empty() and actual != _provider.get_product_user_id() \
			and identity.get_subject() == actual and path in [&"direct", &"relay"] \
			and (not _force_relay or path == &"relay")


## End the fixture with a bounded, credential-free failure report.
func _fail(message: String, exit_code := 1) -> void:
	if is_instance_valid(_game):
		_game.leave()
	push_error(message)
	quit(exit_code)
