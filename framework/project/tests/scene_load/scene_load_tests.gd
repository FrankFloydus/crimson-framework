# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

# ID of the first valid scene fixture.
const AMBER: StringName = &"amber"
# ID of the second valid scene fixture.
const COBALT: StringName = &"cobalt"
# ID registered with a missing scene path for offline tests.
const MISSING: StringName = &"missing"
# ID registered only on the host to test a client registry mismatch.
const HOST_ONLY: StringName = &"host-only"
# ID known to both peers with a missing client resource.
const BROKEN_ASSET: StringName = &"broken-asset"
# Resource path containing the scene fixtures.
const FIXTURES := "res://tests/scene_load/fixtures/"
# Resource path of the first valid world.
const AMBER_SCENE := FIXTURES + "amber.tscn"
# Resource path of the second valid world.
const COBALT_SCENE := FIXTURES + "cobalt.tscn"
# Intentionally missing resource used to trigger preparation failures.
const MISSING_SCENE := FIXTURES + "missing.tscn"
# Default deadline for asynchronous assertions.
const WAIT_MSEC := 5000

# Keeps one peer's subtree and observed scene transitions together.
class TestContext:
	# Root node of the peer's isolated multiplayer scope.
	var branch: Node
	# Persistent parent for the manager's currently published scene.
	var world_container: Node
	# Native session owned by this context.
	var session: CrimsonGameSession
	# Native scene loader scoped to this context.
	var manager: CrimsonSceneLoadManager
	# Scene IDs that began loading on this peer.
	var started: Array[StringName] = []
	# Scene IDs successfully published on this peer.
	var completed: Array[StringName] = []
	# Scene load errors reported before the commit decision.
	var failed: Array[Dictionary] = []
	# Synchronization failures reported after the commit decision.
	var sync_failed: Array[Dictionary] = []


# Number of assertions evaluated by this process.
var _checks := 0
# Number of assertions that reported a failure.
var _failures := 0


# Defer fixture setup until the scene tree is ready.
func _initialize() -> void:
	_run.call_deferred()


# Run the offline, ENet, and recovery cases and report the aggregate result.
func _run() -> void:
	await _test_offline()
	await _test_network()
	await _test_post_commit_failure()
	print("Crimson Scene Load Manager: %d checks, %d failures." % [_checks, _failures])
	if _failures == 0:
		print("CRIMSON_SCENE_LOAD_TESTS_PASSED")
	quit(0 if _failures == 0 else 1)


# Exercise registration, cancellation, failure, and replacement with local authority.
func _test_offline() -> void:
	var offline := _create_context("OfflineContext")
	_expect(offline.session.start_offline() == OK, "Start an offline session.")
	_expect(offline.manager.get_current_scene_id() == &"" and offline.manager.get_current_scene() == null,
		"The manager starts without a current world.")
	_expect(offline.manager.request_load(&"unregistered") != OK,
		"Reject an unregistered scene ID.")
	_expect(offline.manager.register_scene(&"external", "user://unexpected.tscn") != OK,
		"Reject a path outside the project's scene registry.")
	_expect(offline.manager.request_load(AMBER) == OK, "Request an offline scene.")
	if not await _wait_for(func(): return offline.completed.has(AMBER), "The first offline scene completes."):
		await _dispose_contexts([offline])
		return
	_assert_live_world(offline, AMBER, "amber", "OnlyInAmber")
	var first := offline.manager.get_current_scene()
	var first_instance_id := first.get_instance_id() if first != null else 0
	_expect(offline.started == [AMBER] and offline.completed == [AMBER] and offline.failed.is_empty(),
		"A successful offline load emits one start and completion.")

	_expect(offline.manager.request_load(COBALT) == OK, "Start a second offline load.")
	_expect(offline.manager.request_load(AMBER) == ERR_BUSY, "Reject overlapping load requests.")
	_expect(offline.manager.cancel_load() == OK, "Allow offline cancellation before publication.")
	_expect(offline.failed.size() == 1 and _last_failure_is(offline, COBALT, ERR_SKIP),
		"Cancellation reports the canceled scene and ERR_SKIP.")
	_expect(offline.manager.get_current_scene() == first and not offline.manager.is_loading(),
		"Canceling a load preserves the previous world.")

	_expect(offline.manager.register_scene(MISSING, MISSING_SCENE) == OK,
		"Register a missing scene path for a load-time failure.")
	_expect(offline.manager.request_load(MISSING) == OK, "Admit a request for the registered scene ID.")
	await _wait_for(func(): return _last_failure_is(offline, MISSING, ERR_DOES_NOT_EXIST),
		"A missing resource reports an offline load failure.")
	_expect(offline.manager.get_current_scene() == first and offline.world_container.get_child_count() == 1,
		"A missing resource leaves the exact previous world in the container.")

	_expect(offline.manager.request_load(COBALT) == OK, "Allow a new load after a failure.")
	await _wait_for(func(): return offline.completed.has(COBALT), "The second offline scene completes.")
	_assert_live_world(offline, COBALT, "cobalt", "OnlyInCobalt")
	var replacement := offline.manager.get_current_scene()
	_expect(replacement != null and replacement.get_instance_id() != first_instance_id
		and not offline.manager.is_loading(),
		"The new world replaces the old one and clears the transition.")
	await _dispose_contexts([offline])


# Exercise coordinated readiness, failure, disconnect, and admission with ENet peers.
func _test_network() -> void:
	var host := _create_context("HostContext")
	var client := _create_context("ClientContext")
	if not await _connect_contexts(host, client):
		await _dispose_contexts([client, host])
		return

	_expect(client.manager.request_load(AMBER) == ERR_UNAUTHORIZED,
		"Only the server can initiate a network scene transition.")
	_expect(host.session.is_accepting_connections(), "The server initially accepts lobby connections.")
	_expect(host.manager.register_scene(HOST_ONLY, AMBER_SCENE) == OK,
		"Set up a registry mismatch on the client.")
	_expect(host.manager.request_load(HOST_ONLY) == OK and not host.session.is_accepting_connections(),
		"The first transition closes lobby admission while it is pending.")
	await _wait_for(func(): return _last_failure_is(host, HOST_ONLY, ERR_DOES_NOT_EXIST),
		"A failed first transition reports the client's rejected preparation.")
	_expect(_last_failure_is(client, HOST_ONLY, ERR_DOES_NOT_EXIST), "The client reports its missing scene ID.")
	_expect(host.session.is_accepting_connections() and host.manager.get_current_scene() == null,
		"A failed first transition reopens the lobby without publishing a world.")
	var confirmed_on_completion: Array[bool] = []
	# Each completion observer should already see the matching world on both peers.
	var verify_remote_activation := func(id: StringName):
		confirmed_on_completion.append(host.manager.get_current_scene_id() == id and
			client.manager.get_current_scene_id() == id)
	host.manager.load_completed.connect(verify_remote_activation)
	client.manager.load_completed.connect(verify_remote_activation)
	_expect(host.manager.request_load(AMBER) == OK, "The server initiates a synchronized load.")
	if not await _wait_for(func(): return host.completed.has(AMBER) and client.completed.has(AMBER),
			"Both ENet peers publish the first scene."):
		await _dispose_contexts([client, host])
		return
	_assert_live_world(host, AMBER, "amber", "OnlyInAmber")
	_assert_live_world(client, AMBER, "amber", "OnlyInAmber")
	_expect(confirmed_on_completion == [true, true], "Both completion signals follow group activation.")
	_expect(not host.session.is_accepting_connections(), "Successful publication keeps lobby admission closed.")
	var host_amber := host.manager.get_current_scene()
	var client_amber := client.manager.get_current_scene()

	# Disable local processing after connecting. Godot still polls the subtree's
	# MultiplayerAPI, so this peer receives prepare but cannot report ready.
	client.manager.process_mode = Node.PROCESS_MODE_DISABLED
	_expect(host.manager.request_load(COBALT) == OK, "Start a second synchronized load.")
	await _wait_for(func(): return client.manager.is_loading(), "The client receives prepare over ENet.")
	await create_timer(0.15).timeout
	_expect(host.manager.is_loading() and client.manager.is_loading(), "The barrier waits for the stalled client.")
	_expect(host.manager.get_current_scene() == host_amber and client.manager.get_current_scene() == client_amber,
		"Neither peer publishes while readiness is pending.")
	_expect(host.world_container.get_child_count() == 1 and client.world_container.get_child_count() == 1,
		"Pending loads do not add staged nodes to either live container.")
	_expect(client.manager.cancel_load() == ERR_UNAUTHORIZED, "The client cannot cancel the server's load.")
	client.manager.process_mode = Node.PROCESS_MODE_ALWAYS
	await _wait_for(func(): return host.completed.has(COBALT) and client.completed.has(COBALT),
		"Both peers publish after the stalled client resumes.")
	_assert_live_world(host, COBALT, "cobalt", "OnlyInCobalt")
	_assert_live_world(client, COBALT, "cobalt", "OnlyInCobalt")
	var host_cobalt := host.manager.get_current_scene()
	var client_cobalt := client.manager.get_current_scene()

	var failed_before_mismatch := host.failed.size()
	_expect(host.manager.request_load(HOST_ONLY) == OK, "The server requests a scene missing on the client.")
	await _wait_for(func(): return (host.failed.size() > failed_before_mismatch
		and _last_failure_is(host, HOST_ONLY, ERR_DOES_NOT_EXIST)),
		"The server reports the client's rejected preparation.")
	await _wait_for(func(): return not host.manager.is_loading() and not client.manager.is_loading(),
		"The failed transition clears on both peers.")
	_expect(host.manager.get_current_scene() == host_cobalt and client.manager.get_current_scene() == client_cobalt,
		"A failed client prepare preserves both active worlds.")
	_expect(not host.session.is_accepting_connections(), "A failed later transition keeps admission closed.")

	_expect(host.manager.register_scene(BROKEN_ASSET, AMBER_SCENE) == OK and
		client.manager.register_scene(BROKEN_ASSET, MISSING_SCENE) == OK,
		"Both peers know the same scene ID when one local resource is missing.")
	_expect(host.manager.request_load(BROKEN_ASSET) == OK, "Request the shared scene ID.")
	await _wait_for(func(): return _last_failure_is(host, BROKEN_ASSET, ERR_DOES_NOT_EXIST),
		"A missing client resource aborts preparation on the server.")
	_expect(_last_failure_is(client, BROKEN_ASSET, ERR_DOES_NOT_EXIST) and
		host.manager.get_current_scene() == host_cobalt and client.manager.get_current_scene() == client_cobalt,
		"Asset failure leaves both published worlds in place.")

	_expect(host.manager.set_load_timeout_msec(600) == OK, "Configure a bounded timeout.")
	client.manager.process_mode = Node.PROCESS_MODE_DISABLED
	_expect(host.manager.request_load(AMBER) == OK, "Start a transition with a non-responsive client.")
	await _wait_for(func(): return _last_failure_is(host, AMBER, ERR_TIMEOUT),
		"The server aborts when a client fails to become ready in time.")
	client.manager.process_mode = Node.PROCESS_MODE_ALWAYS
	await _wait_for(func(): return not client.manager.is_loading(),
		"The client exits the aborted transition.")
	_expect(host.manager.get_current_scene() == host_cobalt and client.manager.get_current_scene() == client_cobalt,
		"Timeout leaves the current scenes in place.")

	_expect(host.manager.set_load_timeout_msec(15000) == OK, "Restore the normal timeout.")
	client.manager.process_mode = Node.PROCESS_MODE_DISABLED
	_expect(host.manager.request_load(AMBER) == OK, "Start a transition before a client disconnects.")
	await _wait_for(func(): return client.manager.is_loading(), "The client receives prepare before leaving.")
	client.session.leave()
	client.manager.process_mode = Node.PROCESS_MODE_ALWAYS
	await _wait_for(func(): return _last_failure_is(host, AMBER, ERR_CONNECTION_ERROR),
		"A disconnect invalidates the pending quorum.")
	_expect(host.manager.get_current_scene() == host_cobalt and host.completed == [AMBER, COBALT],
		"The server keeps its previous world after the membership change.")

	_expect(host.manager.request_load(AMBER) == OK, "Start a fresh transition with the remaining peers.")
	await _wait_for(func(): return host.completed.count(AMBER) == 2,
		"The server can publish again after an aborted transition.")
	_assert_live_world(host, AMBER, "amber", "OnlyInAmber")
	await _dispose_contexts([client, host])


# Stall a client during activation and verify failure reporting and recovery.
func _test_post_commit_failure() -> void:
	var host := _create_context("CommitHost")
	var client := _create_context("CommitClient")
	if not await _connect_contexts(host, client):
		await _dispose_contexts([client, host])
		return
	_expect(host.manager.request_load(AMBER) == OK, "Load the baseline world before testing commit failure.")
	if not await _wait_for(func(): return host.completed.has(AMBER) and client.completed.has(AMBER),
			"Both peers activate the baseline world."):
		await _dispose_contexts([client, host])
		return
	var old_client_world := client.manager.get_current_scene()
	_expect(host.manager.set_load_timeout_msec(750) == OK, "Use a bounded commit acknowledgment deadline.")
	# The client's prepare vote has arrived by the time the host enters the new
	# world. Stall only activation so this takes the post-commit failure path.
	var stall_client := func(_world: Node):
		client.manager.process_mode = Node.PROCESS_MODE_DISABLED
	host.world_container.child_entered_tree.connect(stall_client, CONNECT_ONE_SHOT)
	_expect(host.manager.request_load(COBALT) == OK, "Start a transition that will lose the client's activation acknowledgment.")
	await _wait_for(func(): return _last_report_is(host.sync_failed, COBALT, ERR_TIMEOUT),
		"The server reports a synchronization failure after committing.")
	await _wait_for(func(): return _last_report_is(client.sync_failed, COBALT, ERR_TIMEOUT),
		"The client receives the server's post-commit failure over ENet.")
	client.manager.process_mode = Node.PROCESS_MODE_ALWAYS
	_expect(host.manager.get_current_scene_id() == COBALT and client.manager.get_current_scene() == old_client_world,
		"An issued commit can leave the activated host ahead of the stalled client.")
	_expect(host.completed == [AMBER] and client.completed == [AMBER] and host.failed.is_empty() and client.failed.is_empty(),
		"An incomplete group commit produces neither a success nor a precommit failure.")
	_expect(not host.manager.is_loading() and not client.manager.is_loading(), "Both peers release the failed transition.")
	_expect(not host.session.is_accepting_connections(), "Admission stays closed while the group needs recovery.")
	_expect(host.manager.request_load(COBALT) == OK, "Allow the host to recover by issuing a fresh transition.")
	await _wait_for(func(): return host.completed.has(COBALT) and client.completed.has(COBALT),
		"A fresh confirmed commit returns both peers to the same scene.")
	_assert_live_world(host, COBALT, "cobalt", "OnlyInCobalt")
	_assert_live_world(client, COBALT, "cobalt", "OnlyInCobalt")
	await _dispose_contexts([client, host])


# Build an isolated session subtree and capture its scene transition signals.
func _create_context(branch_name: String) -> TestContext:
	var context := TestContext.new()
	context.branch = Node.new()
	context.branch.name = branch_name
	root.add_child(context.branch)
	set_multiplayer(MultiplayerAPI.create_default_interface(), context.branch.get_path())

	context.world_container = Node.new()
	context.world_container.name = "WorldContainer"
	context.branch.add_child(context.world_container)
	context.session = CrimsonGameSession.new()
	context.session.name = "GameSession"
	context.branch.add_child(context.session)
	context.manager = CrimsonSceneLoadManager.new()
	context.manager.name = "SceneLoadManager"
	context.branch.add_child(context.manager)
	# Record the start and publication order for this peer.
	context.manager.load_started.connect(func(id: StringName): context.started.append(id))
	context.manager.load_completed.connect(func(id: StringName): context.completed.append(id))
	# Keep preparation and post-commit failures in separate histories.
	context.manager.load_failed.connect(func(id: StringName, error: int, message: String):
		context.failed.append({"id": id, "error": error, "message": message})
	)
	context.manager.load_sync_failed.connect(func(id: StringName, error: int, message: String):
		context.sync_failed.append({"id": id, "error": error, "message": message})
	)
	_expect(context.manager.set_world_container(context.world_container) == OK,
		"Set a persistent world container for %s." % branch_name)
	_expect(context.manager.register_scene(AMBER, AMBER_SCENE) == OK and
		context.manager.register_scene(COBALT, COBALT_SCENE) == OK,
		"Register the matching scene IDs in %s." % branch_name)
	return context


# Leave each session and free its isolated multiplayer subtree.
func _dispose_contexts(contexts: Array[TestContext]) -> void:
	for context in contexts:
		context.session.leave()
		set_multiplayer(null, context.branch.get_path())
		context.branch.queue_free()
	await process_frame


# Start a loopback session and wait until both peers observe the connection.
func _connect_contexts(host: TestContext, client: TestContext) -> bool:
	var port := _host_on_loopback(host.session)
	if not _expect(port > 0, "Host a real ENet session on loopback."):
		return false
	if not _expect(client.session.join("127.0.0.1", port) == OK, "Connect a real ENet client."):
		return false
	# Consider the fixture connected only after the host and client agree on the peer ID.
	var connected := func():
		return (client.session.get_state() == CrimsonGameSession.CLIENT
			and host.session.get_connected_peer_ids().has(client.session.get_local_peer_id()))
	return await _wait_for(connected, "The server and client discover each other.")


# Find a usable loopback UDP port and host the ENet session.
func _host_on_loopback(session: CrimsonGameSession) -> int:
	for attempt in range(5):
		var socket := PacketPeerUDP.new()
		if socket.bind(0, "127.0.0.1") != OK:
			continue
		var port := socket.get_local_port()
		socket.close()
		if port > 0 and session.host(port, 4, "127.0.0.1") == OK:
			return port
	return 0


# Verify the active scene ID, parent, and unique fixture contents.
func _assert_live_world(context: TestContext, id: StringName, marker: String, child: String) -> void:
	var scene := context.manager.get_current_scene()
	_expect(context.manager.get_current_scene_id() == id and scene != null,
		"%s publishes scene ID %s." % [context.branch.name, id])
	_expect(scene != null and scene.get_parent() == context.world_container and scene.name == &"World" and
		scene.get_meta("test_marker", "") == marker and scene.has_node(child) and context.world_container.get_child_count() == 1,
		"%s owns one published world with the expected scene contents." % context.branch.name)


# Check the latest preparation failure for a scene and error.
func _last_failure_is(context: TestContext, scene_id: StringName, error: int) -> bool:
	return _last_report_is(context.failed, scene_id, error)


# Match the latest failure report by scene and error and require a message.
func _last_report_is(reports: Array[Dictionary], scene_id: StringName, error: int) -> bool:
	if reports.is_empty():
		return false
	var failure: Dictionary = reports.back()
	return failure["id"] == scene_id and failure["error"] == error and not str(failure["message"]).is_empty()


# Wait up to the configured deadline and record the resulting assertion.
func _wait_for(predicate: Callable, description: String, timeout_msec: int = WAIT_MSEC) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_msec
	while not predicate.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return _expect(bool(predicate.call()), description)


# Count a check and report its failure.
func _expect(condition: bool, description: String) -> bool:
	_checks += 1
	if not condition:
		_failures += 1
		push_error(description)
	return condition
