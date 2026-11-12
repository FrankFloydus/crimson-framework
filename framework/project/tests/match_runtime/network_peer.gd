# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree
## Runs as two independent processes to exercise actual ENet and server ingress.

# Loopback address for both processes.
const ADDRESS := "127.0.0.1"
# Maximum time to wait for a connection, command reply, or shutdown.
const TIMEOUT_MS := 10000
# Identity shared by commands in this fixture.
const MATCH_ID := "loopback-match"
# World generation for the fixture's command envelopes.
const WORLD_EPOCH := 1
# Stable player ID for the local controller.
const LOCAL_PLAYER := "host"
# Stable player ID assigned to the admitted client.
const REMOTE_PLAYER := "guest"
# Character claimed by both participants.
const HERO := "shared-hero"
# First character successfully claimed by the remote player.
const SCOUT := "scout"
# Character the remote player switches to.
const RESERVE := "reserve"

# Native session used for the offline run and the ENet run.
var _session: GameSession
# RPC endpoint at the same node path in both processes.
var _ingress: TestIngress
# Authority-side stores and transaction commits for the current run.
var _coordinator: TransactionCoordinator
# Authority-side primary character assignments.
var _possession: PlayerPossession
# Shared command path for local and remote players.
var _router: CommandRouter
# Ordered log of casts that reached the gameplay handler.
var _accepted_casts: Array = []
# Assertions that failed, including those checked in signal callbacks.
var _failures := 0


# Exchanges test commands and acknowledgments across the ENet connection.
class TestIngress extends Node:
	# Stable player IDs associated with admitted connection IDs.
	var bindings := PlayerBindings.new()
	# Authority-side callable for dispatching an identified player's command.
	var submit: Callable
	# Server replies keyed by the command sequence supplied by the client.
	var results: Dictionary = {}
	# Whether the server has opened the test's command gate.
	var client_can_start := false
	# Whether the admitted client has reported its assertions complete.
	var client_finished := false
	# Whether the authority has confirmed its own assertions.
	var host_confirmed := false

	# Allow remote commands after the authority completes its first claim.
	@rpc("authority", "call_remote", "reliable")
	func start_client() -> void:
		if multiplayer.get_remote_sender_id() == 1:
			client_can_start = true

	# Resolve the sender through trusted bindings and return the authority's result.
	@rpc("any_peer", "call_remote", "reliable")
	func send_intent(intent: Dictionary) -> void:
		if not multiplayer.is_server():
			return
		var sender := multiplayer.get_remote_sender_id()
		var player_id := bindings.get_player(sender)
		if player_id.is_empty():
			return
		var result: Dictionary = submit.call(player_id, intent)
		return_result.rpc_id(sender, intent.get("sequence", 0), result)

	# Record a command reply received from the server.
	@rpc("authority", "call_remote", "reliable")
	func return_result(sequence: int, result: Dictionary) -> void:
		if multiplayer.get_remote_sender_id() == 1:
			results[sequence] = result

	# Record test completion from the admitted participant.
	@rpc("any_peer", "call_remote", "reliable")
	func finish() -> void:
		if multiplayer.is_server() and not bindings.get_player(multiplayer.get_remote_sender_id()).is_empty():
			client_finished = true

	# Record the host's confirmation before closing the client.
	@rpc("authority", "call_remote", "reliable")
	func confirm() -> void:
		if multiplayer.get_remote_sender_id() == 1:
			host_confirmed = true


# Defer setup until the scene tree is ready to accept test nodes.
func _initialize() -> void:
	_run.call_deferred()


# Validate the process role and port, run the fixture, and return its exit status.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2 or arguments[0] not in ["host", "client"] or not arguments[1].is_valid_int():
		push_error("Usage: network_peer.gd -- host|client port")
		quit(1)
		return
	var port := arguments[1].to_int()
	if port < 1 or port > 65535:
		push_error("Expected a UDP port between 1 and 65535.")
		quit(1)
		return

	Engine.max_fps = 120
	var shell := Node.new()
	shell.name = "MatchRuntimeHarness"
	root.add_child(shell)

	_session = GameSession.new()
	_session.name = "Session"
	shell.add_child(_session)
	_ingress = TestIngress.new()
	_ingress.name = "Ingress"
	shell.add_child(_ingress)
	_session.peer_joined.connect(_on_peer_joined)
	_session.peer_left.connect(_on_peer_left)

	var passed: bool
	if arguments[0] == "host":
		passed = await _run_host(port)
	else:
		passed = await _run_client(port)

	_session.leave()
	passed = passed and _failures == 0
	if passed:
		print("CRIMSON_MATCH_NETWORK_PASSED")
	quit(0 if passed else 1)


# Initialize fresh stores, possession rules, and command handlers for one run.
func _configure_authority() -> bool:
	_accepted_casts.clear()
	_coordinator = TransactionCoordinator.new()
	_possession = PlayerPossession.new()
	_router = CommandRouter.new()
	if not _expect(_coordinator.register_store(
			"players", {LOCAL_PLAYER: {"name": "Host"}, REMOTE_PLAYER: {"name": "Guest"}}, _read_only) == OK,
			"Register player records."):
		return false
	if not _expect(_coordinator.register_store(
			"characters", {HERO: {"health": 10}, SCOUT: {"health": 20}, RESERVE: {"health": 30}}, _read_only) == OK,
			"Register character records."):
		return false
	if not _expect(_possession.configure(_coordinator, "players", "characters") == OK, "Configure possession."):
		return false
	if not _expect(_router.configure(MATCH_ID, WORLD_EPOCH, _possession) == OK, "Configure the command router."):
		return false
	if not _expect(_router.register_handler("possess", _possess) == OK
			and _router.register_handler("cast", _cast, true) == OK, "Register gameplay handlers."):
		return false
	_ingress.submit = _router.submit
	return _expect(_router.set_accepting_commands(true) == OK, "Open the command gate.")


# Reject mutations to the fixture's fixed player and character records.
func _read_only(_current: Dictionary, _operations: Array, _context: Dictionary) -> Dictionary:
	return {"ok": false, "message": "Fixture records are read-only."}


# Route a possession intent to the native possession service.
func _possess(player_id: String, intent: Dictionary) -> Dictionary:
	var payload: Dictionary = intent["payload"]
	return _possession.try_set_primary(player_id, payload["target"], payload["revision"])


# Record that a possession-gated cast reached the gameplay handler.
func _cast(player_id: String, intent: Dictionary) -> Dictionary:
	_accepted_casts.append([player_id, intent["character_id"]])
	return {"ok": true, "value": intent["character_id"]}


# Assign the fixture's stable remote player ID after admission.
func _on_peer_joined(peer_id: int) -> void:
	if _session.is_authority():
		_expect(_ingress.bindings.bind_peer(peer_id, REMOTE_PLAYER) > 0, "Bind the admitted peer to its stable player ID.")


# Remove the departing connection's binding using its current generation.
func _on_peer_left(peer_id: int) -> void:
	var generation := _ingress.bindings.get_binding_generation(peer_id)
	if generation > 0:
		_expect(_ingress.bindings.unbind_peer(peer_id, generation), "Release the disconnected peer binding.")


# Verify offline parity and inspect authoritative results from both ENet participants.
func _run_host(port: int) -> bool:
	if not _expect(_session.start_offline() == OK and _session.is_authority(), "Start the offline authority."):
		return false
	if not _configure_authority() or not _claim_and_cast_locally():
		return false
	if not _expect(_accepted_casts == [[LOCAL_PLAYER, HERO]], "Offline input reaches the gameplay handler."):
		return false
	_session.leave()

	if not _configure_authority():
		return false
	if not _expect(_session.host(port, 1, ADDRESS) == OK, "Listen on loopback UDP port %d." % port):
		return false
	print("CRIMSON_MATCH_HOST_READY")
	if not await _wait_for(func() -> bool: return _session.get_connected_peer_ids().size() == 1,
			"the remote participant"):
		return false
	# Keep the claim ordering deterministic while both participants are connected.
	if not _claim_and_cast_locally():
		return false
	var admitted_peer := _session.get_connected_peer_ids()[0]
	if not _expect(_ingress.rpc_id(admitted_peer, &"start_client") == OK, "Invite the client to submit commands."):
		return false

	if not await _wait_for(func() -> bool: return _ingress.client_finished, "the client's assertions"):
		return false
	var peers := _session.get_connected_peer_ids()
	if not _expect(peers.size() == 1, "One remote participant remains connected."):
		return false
	if not _expect(_possession.get_primary(LOCAL_PLAYER) == HERO
			and _possession.get_controller(HERO) == LOCAL_PLAYER, "The contested character has exactly one owner."):
		return false
	if not _expect(_possession.get_primary(REMOTE_PLAYER) == RESERVE
			and _possession.get_controller(SCOUT).is_empty(), "Switching releases the remote player's old character."):
		return false
	if not _expect(_accepted_casts == [[LOCAL_PLAYER, HERO], [REMOTE_PLAYER, SCOUT], [REMOTE_PLAYER, RESERVE]],
			"Only authorized local and remote casts reach the handler."):
		return false
	if not _expect(_ingress.rpc_id(peers[0], &"confirm") == OK, "Confirm the server assertions to the client."):
		return false
	# Keep polling until the client receives the confirmation and closes its connection.
	return await _wait_for(func() -> bool: return _session.get_connected_peer_ids().is_empty(), "the client to disconnect")


# Exercise the local player's claim and cast through the command router.
func _claim_and_cast_locally() -> bool:
	var claim := _router.submit(LOCAL_PLAYER, _claim_intent(1, HERO, 0))
	if not _expect(claim.get("ok", false), "The local player claims the shared character. %s" % str(claim)):
		return false
	var revision := _possession.get_revision(LOCAL_PLAYER)
	if not _expect(revision == 1, "The local possession starts at revision 1 after the first claim."):
		return false
	var cast := _router.submit(LOCAL_PLAYER, _cast_intent(2, HERO, revision))
	return _expect(cast.get("ok", false), "The local player uses the same cast command online and offline. %s" % str(cast))


# Verify remote contention, switching, stale targets, and replay rejection.
func _run_client(port: int) -> bool:
	if not _expect(_session.join(ADDRESS, port, 6.0) == OK, "Join the loopback host."):
		return false
	if not await _wait_for(func() -> bool: return _session.get_state() == GameSession.State.CLIENT,
			"the client session"):
		return false
	if not await _wait_for(func() -> bool: return _ingress.client_can_start, "the server's command gate"):
		return false

	var contested := await _submit_remote(_claim_intent(1, HERO, 0))
	if not _expect(not contested.is_empty() and not contested.get("ok", true),
			"A second participant cannot take the already possessed character. %s" % str(contested)):
		return false
	var claim := await _submit_remote(_claim_intent(2, SCOUT, 0))
	if not _expect(claim.get("ok", false), "The client claims an available character. %s" % str(claim)):
		return false
	var first_cast := await _submit_remote(_cast_intent(3, SCOUT, 1))
	if not _expect(first_cast.get("ok", false), "The client casts through the server router. %s" % str(first_cast)):
		return false
	var switched := await _submit_remote(_claim_intent(4, RESERVE, 1))
	if not _expect(switched.get("ok", false), "The client switches its primary character. %s" % str(switched)):
		return false
	var stale := await _submit_remote(_cast_intent(5, SCOUT, 1))
	if not _expect(not stale.get("ok", true) and stale.get("code", "") == "StalePossession",
			"The server rejects a delayed cast addressed to the previous character. %s" % str(stale)):
		return false
	var second_cast := await _submit_remote(_cast_intent(6, RESERVE, 2))
	if not _expect(second_cast.get("ok", false), "The new character accepts a fresh cast. %s" % str(second_cast)):
		return false
	var replay := await _submit_remote(_cast_intent(6, RESERVE, 2))
	if not _expect(not replay.get("ok", true) and replay.get("code", "") == "Replay",
			"The server rejects a repeated command sequence. %s" % str(replay)):
		return false
	if not _expect(_ingress.rpc_id(1, &"finish") == OK, "Report client completion."):
		return false
	return await _wait_for(func() -> bool: return _ingress.host_confirmed, "the server's confirmation")


# Send one intent and wait for the matching server reply.
func _submit_remote(intent: Dictionary) -> Dictionary:
	var sequence: int = intent["sequence"]
	_ingress.results.erase(sequence)
	if not _expect(_ingress.rpc_id(1, &"send_intent", intent) == OK, "Send remote command %d." % sequence):
		return {}
	if not await _wait_for(func() -> bool: return _ingress.results.has(sequence),
			"the response for command %d" % sequence):
		return {}
	return _ingress.results[sequence]


# Build a possession request for the target character and expected revision.
func _claim_intent(sequence: int, target: String, revision: int) -> Dictionary:
	return {
		"match_id": MATCH_ID,
		"world_epoch": WORLD_EPOCH,
		"sequence": sequence,
		"type": "possess",
		"payload": {"target": target, "revision": revision},
	}


# Build a cast addressed to a specific possession assignment.
func _cast_intent(sequence: int, character_id: String, revision: int) -> Dictionary:
	return {
		"match_id": MATCH_ID,
		"world_epoch": WORLD_EPOCH,
		"sequence": sequence,
		"type": "cast",
		"payload": {},
		"character_id": character_id,
		"possession_revision": revision,
	}


# Poll across scene-tree frames until a condition is true or the timeout expires.
func _wait_for(condition: Callable, description: String) -> bool:
	var deadline := Time.get_ticks_msec() + TIMEOUT_MS
	while not condition.call():
		if Time.get_ticks_msec() >= deadline:
			push_error("Timed out waiting for %s." % description)
			return false
		await process_frame
	return true


# Count and report a failed assertion.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		_failures += 1
		push_error(description)
	return condition
