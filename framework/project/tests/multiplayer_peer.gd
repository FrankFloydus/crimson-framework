# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree
## Run one Godot process per role. The PowerShell runner starts the client
## after the host prints CRIMSON_MULTIPLAYER_HOST_LISTENING.

# Loopback address for the separate host and client processes.
const ADDRESS := "127.0.0.1"
# Maximum time to wait for a peer, scene, or confirmation.
const TIMEOUT_MS := 8000
# Fixture scene IDs in transition order.
const SCENES: Array[StringName] = [&"amber", &"cobalt"]

# Native transport and admission lifecycle for this process.
var _session: CrimsonGameSession
# Native manager for synchronized scene activation.
var _loader: CrimsonSceneLoadManager
# Persistent parent of the scene manager's currently mounted world.
var _world_container: Node
# RPC endpoint that compares publication across the two processes.
var _probe: CommitProbe
# Scene IDs reported complete by the local manager.
var _completed: Array[StringName] = []
# First observable load failure for the current run.
var _load_error := ""


# Exchanges the observed scene ID after each synchronized commit.
class CommitProbe extends Node:
	# Admitted client allowed to report its observed scene.
	var expected_peer := 0
	# Most recent scene observed by the client and reported to the host.
	var reported_scene := &""
	# Most recent scene confirmed by the host on the client.
	var confirmed_scene := &""

	# Record a client's published scene only for the expected sender.
	@rpc("any_peer", "call_remote", "reliable")
	func report_commit(scene_id: StringName) -> void:
		if multiplayer.is_server() and multiplayer.get_remote_sender_id() == expected_peer:
			reported_scene = scene_id

	# Record the server's acknowledgment of a client's published scene.
	@rpc("authority", "call_remote", "reliable")
	func confirm_commit(scene_id: StringName) -> void:
		if multiplayer.get_remote_sender_id() == 1:
			confirmed_scene = scene_id


# Defer fixture setup until the scene tree is ready.
func _initialize() -> void:
	_run.call_deferred()


# Parse the role and port, execute the scenario, and quit with its result.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2 or arguments[0] not in ["host", "client"] or not arguments[1].is_valid_int():
		push_error("Usage: multiplayer_peer.gd -- host|client port")
		quit(1)
		return
	var port := arguments[1].to_int()
	if port < 1 or port > 65535:
		push_error("The ENet test port must be between 1 and 65535.")
		quit(1)
		return

	Engine.max_fps = 120
	if not _configure_world():
		quit(1)
		return
	var passed: bool
	if arguments[0] == "host":
		passed = await _run_host(port)
	else:
		passed = await _run_client(port)
	_session.leave()
	quit(0 if passed else 1)


# Mount matching node paths and register the same scenes in both processes.
func _configure_world() -> bool:
	var shell := Node.new()
	shell.name = &"MultiplayerHarness"
	root.add_child(shell)

	_session = CrimsonGameSession.new()
	_session.name = &"Session"
	shell.add_child(_session)
	_world_container = Node.new()
	_world_container.name = &"WorldContainer"
	shell.add_child(_world_container)
	_loader = CrimsonSceneLoadManager.new()
	_loader.name = &"SceneLoader"
	shell.add_child(_loader)
	_probe = CommitProbe.new()
	_probe.name = &"CommitProbe"
	shell.add_child(_probe)

	# Record successful publications locally before reporting them over ENet.
	_loader.load_completed.connect(func(scene_id: StringName) -> void: _completed.append(scene_id))
	_loader.load_failed.connect(_on_load_failed)
	_loader.load_sync_failed.connect(_on_load_failed)
	if not _expect(_loader.set_world_container(_world_container) == OK, "Configure the world container"):
		return false
	for scene_id in SCENES:
		var path := "res://tests/scene_load/fixtures/%s.tscn" % scene_id
		if not _expect(_loader.register_scene(scene_id, path) == OK, "Register scene %s" % scene_id):
			return false
	return true


# Load each scene and verify that the connected client publishes it as well.
func _run_host(port: int) -> bool:
	if not _expect(_session.host(port, 4, ADDRESS) == OK, "Listen on UDP port %d" % port):
		return false
	if not _expect(_session.is_authority() and _session.get_local_peer_id() == 1
			and _session.is_accepting_connections(), "Start as an accepting server with peer ID 1"):
		return false
	print("CRIMSON_MULTIPLAYER_HOST_LISTENING")
	if not await _wait_for(func() -> bool: return _session.get_connected_peer_ids().size() == 1, "client joins"):
		return false
	_probe.expected_peer = _session.get_connected_peer_ids()[0]

	for scene_id in SCENES:
		var previous := _loader.get_current_scene()
		if not _expect(_loader.request_load(scene_id) == OK, "Host requests %s" % scene_id):
			return false
		if not _expect(_loader.get_current_scene() == previous, "Keep the live world while preparing %s" % scene_id):
			return false
		if not await _wait_for_scene(scene_id):
			return false
		if not _expect(not _session.is_accepting_connections(), "Close admissions after committing %s" % scene_id):
			return false
		if not await _wait_for(func() -> bool: return _probe.reported_scene == scene_id, "client reports %s" % scene_id):
			return false
		if not _expect(_probe.rpc_id(_probe.expected_peer, &"confirm_commit", scene_id) == OK, "Confirm client %s" % scene_id):
			return false

	# Keep the server alive while ENet delivers the last confirmation.
	await create_timer(0.5).timeout
	print("CRIMSON_MULTIPLAYER_HOST_PASSED")
	return true


# Validate each local world, report its scene ID, and wait for host confirmation.
func _run_client(port: int) -> bool:
	if not _expect(_session.join(ADDRESS, port, 6.0) == OK, "Begin joining UDP port %d" % port):
		return false
	if not await _wait_for(func() -> bool: return _session.get_state() == CrimsonGameSession.State.CLIENT, "connected client"):
		return false
	if not _expect(not _session.is_authority() and _session.get_local_peer_id() > 1, "Join as a non-authoritative peer"):
		return false

	for scene_id in SCENES:
		if not await _wait_for_scene(scene_id):
			return false
		if not _expect(_probe.rpc_id(1, &"report_commit", scene_id) == OK, "Report client %s" % scene_id):
			return false
		if not await _wait_for(func() -> bool: return _probe.confirmed_scene == scene_id, "host confirms %s" % scene_id):
			return false

	print("CRIMSON_MULTIPLAYER_CLIENT_PASSED")
	return true


# Wait for a scene completion and validate the mounted fixture.
func _wait_for_scene(scene_id: StringName) -> bool:
	# Stop waiting on either completion or a terminal load error.
	var finished := func() -> bool: return _completed.has(scene_id) or not _load_error.is_empty()
	if not await _wait_for(finished, "commit %s" % scene_id):
		return false
	if not _expect(_load_error.is_empty(), _load_error):
		return false
	var world := _loader.get_current_scene()
	return _expect(
		_loader.get_current_scene_id() == scene_id and is_instance_valid(world)
		and world.get_parent() == _world_container and _world_container.get_child_count() == 1
		and world.get_meta("test_marker", "") == String(scene_id),
		"Publish the %s fixture in the world container" % scene_id
	)


# Capture a loader failure so the scene wait can report it.
func _on_load_failed(scene_id: StringName, error: int, message: String) -> void:
	_load_error = "Scene %s failed (%d): %s" % [scene_id, error, message]


# Poll a condition across frames with a bounded timeout.
func _wait_for(condition: Callable, description: String) -> bool:
	var deadline := Time.get_ticks_msec() + TIMEOUT_MS
	while not condition.call():
		if Time.get_ticks_msec() >= deadline:
			push_error("Timed out waiting for %s." % description)
			return false
		await process_frame
	return true


# Report an assertion failure and return the condition.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		push_error(description)
	return condition
