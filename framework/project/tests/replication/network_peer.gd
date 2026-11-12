# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree
## Runs the native replication node across separate ENet host and client processes.

# Loopback address shared by both test processes.
const ADDRESS := "127.0.0.1"
# Stable player ID bound by the host after client admission.
const GUEST := "guest"
# Generation of the fixture's one admitted binding.
const BINDING := 3
# Maximum time to wait for a state or transport event.
const TIMEOUT_MS := 10000

# Owns the fixture's ENet session.
var _session: CrimsonGameSession
# Native replication endpoint with the same path on both peers.
var _replication: CrimsonReplication
# Test-only confirmation endpoint.
var _control: TestControl
# Projection history recorded by the receiving process.
var _updates: Array = []
# Number of times an installed client view was cleared.
var _clears := 0
# Fixture's host-side world records.
var _visible := {"item": {"position": [1, 2]}, "leaving": true}
# Authority-only fixture records, including one outside the guest's view.
var _private := {"guest": "guest-inventory", "host": "host-inventory"}
# Host peer ID recorded during admission.
var _guest_peer := 0
# Number of matching baseline acknowledgments received by the host.
var _acknowledged := 0
# Failed assertions including checks run in signal callbacks.
var _failures := 0


# Confirms that the host received the second baseline acknowledgment.
class TestControl extends Node:
	# True after the connected server reports completion.
	var finished := false

	# Receive the authority's confirmation before disconnecting the client.
	@rpc("authority", "call_remote", "reliable")
	func confirm() -> void:
		if multiplayer.get_remote_sender_id() == 1:
			finished = true


# Defer fixture setup until the scene tree can accept test nodes.
func _initialize() -> void:
	_run.call_deferred()


# Start the selected role and report a clean result to the harness.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2 or arguments[0] not in ["host", "client"] or not arguments[1].is_valid_int():
		push_error("Usage: network_peer.gd -- host|client port")
		quit(1)
		return

	Engine.max_fps = 120
	var shell := Node.new()
	shell.name = "ReplicationHarness"
	root.add_child(shell)
	_session = CrimsonGameSession.new()
	_session.name = "Session"
	shell.add_child(_session)
	_replication = CrimsonReplication.new()
	_replication.name = "Replication"
	shell.add_child(_replication)
	_control = TestControl.new()
	_control.name = "Control"
	shell.add_child(_control)

	_replication.projection_updated.connect(_on_projection_updated)
	_replication.projection_cleared.connect(_on_projection_cleared)
	_replication.peer_ready.connect(_on_peer_ready)
	_session.peer_joined.connect(_on_peer_joined)

	var port := arguments[1].to_int()
	var passed: bool
	if arguments[0] == "host":
		passed = await _run_host(port)
	else:
		passed = await _run_client(port)
	_session.leave()
	passed = passed and _failures == 0
	if passed:
		print("CRIMSON_REPLICATION_NETWORK_PASSED")
	quit(0 if passed else 1)


# Listen and wait for the admitted peer to synchronize and disconnect.
func _run_host(port: int) -> bool:
	if not _expect(_session.host(port, 1, ADDRESS) == OK, "Start the ENet server."):
		return false
	if not _expect(_replication.set_projector(_project_view) == OK, "Set the recipient projector."):
		return false
	if not _expect(_replication.advance_world(1, 1) == OK, "Initialize the first replication epoch."):
		return false
	print("CRIMSON_REPLICATION_HOST_READY")
	if not await _wait_for(func() -> bool: return _acknowledged == 2, "both baseline acknowledgments"):
		return false
	if not await _wait_for(func() -> bool: return _session.get_connected_peer_ids().is_empty(), "client shutdown"):
		return false
	return _expect(not _replication.is_peer_ready(_guest_peer, BINDING), "Disconnected peers lose readiness.")


# Join and verify the exact filtered history, then verify cleanup after disconnect.
func _run_client(port: int) -> bool:
	if not _expect(_session.join(ADDRESS, port, 6.0) == OK, "Join the ENet server."):
		return false
	if not await _wait_for(func() -> bool: return _control.finished, "the second baseline confirmation"):
		return false
	var expected := [
		[1, 1, true, {"item": {"position": [1, 2]}, "leaving": true, "owner": "guest-inventory"}],
		[1, 2, false, {"item": {"position": [1, 2]}, "leaving": true, "owner": "guest-inventory", "pending": true}],
		[1, 4, false, {"item": {"position": [2, 3]}, "owner": "guest-inventory", "pending": true}],
		[2, 0, true, {"new-world": true, "owner": "guest-inventory"}],
	]
	_expect(_updates == expected, "Only authorized changes and removals reached the client. %s" % str(_updates))
	_expect(_replication.is_client_ready(), "The new baseline makes the client ready.")
	var detached := _replication.get_projection()
	detached["new-world"] = false
	_expect(_replication.get_projection().get("new-world") == true, "Client readers receive detached copies.")
	_session.leave()
	if not await _wait_for(func() -> bool: return _clears == 1, "replication cleanup after disconnect"):
		return false
	return _expect(not _replication.is_client_ready() and _replication.get_projection().is_empty(),
		"Disconnect clears the client view.")


# Bind the admitted connection and send its first baseline.
func _on_peer_joined(peer_id: int) -> void:
	if not _session.is_authority():
		return
	_guest_peer = peer_id
	if not _expect(_replication.register_peer(peer_id, GUEST, BINDING) == OK, "Register the admitted binding."):
		return
	_expect(_replication.send_baseline(peer_id) == OK, "Send the initial baseline.")
	_expect(not _replication.is_peer_ready(peer_id, BINDING), "Sending alone does not open readiness.")
	_visible["pending"] = true
	_expect(_replication.publish_changes(2) == OK, "Record a visible commit while the baseline awaits acknowledgment.")


# Project only records visible to the admitted player.
func _project_view(peer_id: int, player_id: String, _epoch: int, _revision: int) -> Dictionary:
	if not _expect(peer_id == _guest_peer and player_id == GUEST, "Project using the trusted peer and player IDs."):
		return {"ok": false}
	var view := _visible.duplicate()
	view["owner"] = _private[player_id]
	return {"ok": true, "view": view}


# Publish hidden and visible commits, then move the peer to another world epoch.
func _on_peer_ready(peer_id: int, epoch: int, generation: int) -> void:
	_expect(peer_id == _guest_peer and generation == BINDING, "The host receives a binding-scoped acknowledgment.")
	_expect(_replication.is_peer_ready(peer_id, generation), "The acknowledged peer is now ready.")
	_expect(not _replication.is_peer_ready(peer_id, generation - 1), "A prior binding cannot inherit readiness.")
	_acknowledged += 1
	if epoch == 1:
		_private["host"] = "new-host-inventory"
		_expect(_replication.publish_changes(3) == OK, "Accept a hidden-only commit.")
		_visible["item"]["position"] = [2, 3]
		_visible.erase("leaving")
		_expect(_replication.publish_changes(4) == OK, "Publish a visible update and removal.")
		_visible = {"new-world": true}
		_expect(_replication.advance_world(2, 0) == OK, "Advance to a new epoch.")
		_expect(not _replication.is_peer_ready(peer_id, generation), "The prior baseline loses readiness.")
		_expect(_replication.send_baseline(peer_id) == OK, "Send a baseline for the new epoch.")
	elif _expect(epoch == 2, "The second acknowledgment belongs to the new world."):
		_expect(_control.rpc_id(peer_id, &"confirm") == OK, "Confirm the host's assertions.")


# Record the complete visible result for each client delivery.
func _on_projection_updated(epoch: int, revision: int, baseline: bool) -> void:
	_updates.append([epoch, revision, baseline, _replication.get_projection()])


# Record client view invalidation for the disconnect check.
func _on_projection_cleared() -> void:
	_clears += 1


# Wait across scene frames with a bounded deadline.
func _wait_for(condition: Callable, description: String) -> bool:
	var deadline := Time.get_ticks_msec() + TIMEOUT_MS
	while not condition.call():
		if Time.get_ticks_msec() >= deadline:
			push_error("Timed out waiting for %s." % description)
			return false
		await process_frame
	return true


# Count and report a failed fixture assertion.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		_failures += 1
		push_error(description)
	return condition
