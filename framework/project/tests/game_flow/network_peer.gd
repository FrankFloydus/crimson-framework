# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends "res://tests/game_flow/game_flow_harness.gd"

# Loopback address for the separate Godot processes.
const ADDRESS := "127.0.0.1"

# Test-only synchronization between assertions in the separate processes.
var _probe: TransitionProbe
# Client peer admitted to the test lobby by the native session.
var _guest_peer := 0
# Number of baseline events the client has observed while ready.
var _baselines := 0
# Number of running delta events the client has observed.
var _deltas := 0


# Acknowledge assertions so the host can advance without relying on frame timing.
class TransitionProbe extends Node:
	# The admitted client allowed to report test milestones.
	var expected_peer := 0
	# Most recent sequential milestone verified by the client.
	var observed := 0
	# Whether the host has received the client's final observation.
	var confirmed := false

	# Record a milestone from the trusted test client in sequence.
	@rpc("any_peer", "call_remote", "reliable")
	func report(step: int) -> void:
		if multiplayer.is_server() and multiplayer.get_remote_sender_id() == expected_peer and step == observed + 1:
			observed = step

	# Let the client leave only after the server has received its last assertion.
	@rpc("authority", "call_remote", "reliable")
	func confirm() -> void:
		if multiplayer.get_remote_sender_id() == 1:
			confirmed = true


# Defer setup until the scene tree accepts persistent fixture nodes.
func _initialize() -> void:
	_run.call_deferred()


# Parse the role, run the fixture, and let the public facade tear down its session.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2 or arguments[0] not in ["host", "client"] or not arguments[1].is_valid_int():
		push_error("Usage: network_peer.gd -- host|client port")
		quit(1)
		return
	var port := arguments[1].to_int()
	if port < 1 or port > 65535 or not _configure_game(arguments[0] == "host"):
		quit(1)
		return
	_start_watchdog()
	_probe = TransitionProbe.new()
	_probe.name = &"TransitionProbe"
	_shell.add_child(_probe)
	_game.peer_joined.connect(_on_peer_joined)
	_game.projection_updated.connect(_on_projection_updated)
	var passed: bool
	if arguments[0] == "host":
		passed = await _run_host(port)
	else:
		passed = await _run_client(port)
	_game.leave()
	await process_frame
	if passed:
		print("CRIMSON_GAME_FLOW_NETWORK_PASSED")
	quit(0 if passed else 1)


# Bind the trusted roster and verify authority dispatch, capture, travel, and restore.
func _run_host(port: int) -> bool:
	if not _register_authority_store() or not _expect(_game.host(port, 4, ADDRESS) == OK,
		"Start the Game Flow authority on loopback UDP %d." % port):
		return false
	if not _expect(_game.is_authority() and _game.get_phase() == CrimsonGame.LOBBY,
		"Hosting enters the authority lobby before any world starts."):
		return false
	print("CRIMSON_GAME_FLOW_HOST_READY")
	if not await _wait_for(func() -> bool: return _guest_peer > 1, "Game Flow client admission"):
		return false
	_probe.expected_peer = _guest_peer
	if not _expect(_game.bind_player(1, "host") == OK and _game.bind_player(_guest_peer, "guest") == OK,
		"Bind the admitted peer and listen host to trusted stable player IDs."):
		return false
	var started: Dictionary = await _game.start_match().wait()
	if not _expect(started.get("ok", false) and _mounted_world(AMBER, "amber"), "Start and synchronize Amber: %s" % started):
		return false
	var amber_epoch := _game.get_world_epoch()
	if not await _wait_for(func() -> bool: return _probe.observed == 1, "client verifies its command's running delta"):
		return false
	if not _expect(_authority_score() == 17 and _dispatch_players == ["guest"],
		"The remote command ran once through the authority handler with the trusted guest identity."):
		return false
	var host_change: Dictionary = await _game.submit(COMMAND, {"score": 42}).wait()
	if not _expect(host_change.get("ok", false) and host_change.get("player") == "host" and _authority_score() == 42,
		"The listen host uses the same handler with the host identity: %s" % host_change):
		return false
	var saved: Dictionary = await _game.save(PROFILE, SLOT).wait()
	if not _expect(saved.get("ok", false), "Save the running authority's Amber world: %s" % saved):
		return false
	var traveled: Dictionary = await _game.travel_to(COBALT).wait()
	if not _expect(traveled.get("ok", false) and _mounted_world(COBALT, "cobalt") and _game.get_world_epoch() > amber_epoch,
		"Travel and synchronize Cobalt at a newer epoch: %s" % traveled):
		return false
	var cobalt_epoch := _game.get_world_epoch()
	if not await _wait_for(func() -> bool: return _probe.observed == 2, "client verifies the Cobalt baseline"):
		return false
	var changed: Dictionary = await _game.submit(COMMAND, {"score": 99}).wait()
	if not _expect(changed.get("ok", false) and _authority_score() == 99 and _dispatch_players == ["guest", "host", "host"],
		"Mutate Cobalt through the same registered handler: %s" % changed):
		return false
	if not await _wait_for(func() -> bool: return _probe.observed == 3, "client verifies the Cobalt running delta"):
		return false
	var restored: Dictionary = await _game.restore(PROFILE, SLOT).wait()
	if not _expect(restored.get("ok", false) and _mounted_world(AMBER, "amber") and _authority_score() == 42
			and _game.get_world_epoch() > cobalt_epoch,
		"Restore the prior Amber scene and score to authority at a third epoch: %s" % restored):
		return false
	if not await _wait_for(func() -> bool: return _probe.observed == 4, "client verifies the restored Amber baseline"):
		return false
	if not _expect(_probe.rpc_id(_guest_peer, &"confirm") == OK, "Acknowledge the client's final observation."):
		return false
	await create_timer(0.25).timeout
	return true


# Verify the client receives complete worlds and submits through the network authority.
func _run_client(port: int) -> bool:
	if not _expect(_game.join(ADDRESS, port, 6.0) == OK, "Join the Game Flow authority."):
		return false
	if not await _wait_for(func() -> bool: return _mounted_world(AMBER, "amber") and _baselines == 1, "initial Amber baseline"):
		return false
	if not _expect(not _game.is_authority() and _game.get_transactions() == null and _visible_score() == 0,
		"The ready client sees its initial projection and has no authority coordinator."):
		return false
	var amber_epoch := _game.get_world_epoch()
	var result: Dictionary = await _game.submit(COMMAND, {"score": 17, "player_id": "forged"}).wait()
	if not _expect(result.get("ok", false) and result.get("player") == "guest" and _dispatch_players.is_empty(),
		"The network command returned the trusted identity without dispatching on the client: %s" % result):
		return false
	if not await _wait_for(func() -> bool: return _visible_score() == 17 and _deltas > 0, "Amber running delta") \
		or not _report(1):
		return false
	if not await _wait_for(func() -> bool: return _mounted_world(COBALT, "cobalt") and _baselines == 2, "Cobalt baseline"):
		return false
	var cobalt_epoch := _game.get_world_epoch()
	if not _expect(cobalt_epoch > amber_epoch and _visible_score() == 42,
		"Cobalt becomes ready with the new epoch and current authority score.") or not _report(2):
		return false
	var cobalt_delta_count := _deltas
	if not await _wait_for(func() -> bool: return _visible_score() == 99 and _deltas > cobalt_delta_count, "Cobalt running delta") \
		or not _report(3):
		return false
	if not await _wait_for(func() -> bool: return _mounted_world(AMBER, "amber") and _baselines == 3, "restored Amber baseline"):
		return false
	if not _expect(_game.get_world_epoch() > cobalt_epoch and _visible_score() == 42 and _dispatch_players.is_empty(),
		"Restore installs the previous world and saved projection at a new epoch.") or not _report(4):
		return false
	return await _wait_for(func() -> bool: return _probe.confirmed, "host confirms the final client observation")


# Send the next assertion milestone to the host without influencing the game logic.
func _report(step: int) -> bool:
	return _expect(_probe.rpc_id(1, &"report", step) == OK, "Report verified client milestone %d." % step)


# Record the admitted peer that the host will bind to the fixture player.
func _on_peer_joined(peer_id: int) -> void:
	_guest_peer = peer_id


# Count client readiness baselines separately from running deltas.
func _on_projection_updated(_revision: int, baseline: bool) -> void:
	if baseline:
		_baselines += 1
	else:
		_deltas += 1


# Read the score from the public client view, which is empty outside a ready world.
func _visible_score() -> int:
	var projected: Dictionary = _game.get_projection().get(STORE, {})
	return int(projected.get("score", -1))
