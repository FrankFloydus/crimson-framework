# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const ADDRESS := "127.0.0.1"
const WAIT_MS := 15000

# A game-supplied adapter that records the native Session's factory and rejection calls.
class LoopbackTransport extends CrimsonSessionTransport:
	var enet := CrimsonENetSessionTransport.new()
	var session: WeakRef
	var server_ports: Array[int] = []
	var client_ports: Array[int] = []
	var rejected_peers: Array[int] = []
	var during_open: Array[int] = []

	func _open_server(port: int, remote_limit: int, bind_address: String) -> CrimsonSessionTransportResult:
		server_ports.append(port)
		during_open.append(session.get_ref().start_offline())
		return enet.open_server(port, remote_limit, bind_address)

	func _open_client(endpoint: CrimsonSessionEndpoint) -> CrimsonSessionTransportResult:
		client_ports.append(endpoint.get_port())
		if endpoint.get_address() != ADDRESS:
			return CrimsonSessionTransportResult.failed(ERR_INVALID_PARAMETER)
		return enet.open_client(endpoint)

	func _disconnect_after_flush(peer: MultiplayerPeer, peer_id: int) -> void:
		rejected_peers.append(peer_id)
		enet.disconnect_after_flush(peer, peer_id)


class LoopbackProvider extends CrimsonServiceProvider:
	var adapter := LoopbackTransport.new()

	func _create_session() -> CrimsonGameSession:
		var session := CrimsonGameSession.new()
		adapter.session = weakref(session)
		if session.set_transport_adapter(adapter) != OK:
			session.free()
			return null
		return session


var _demo: Node
var _game: CrimsonGame
var _definition: CrimsonGameDefinition
var _markers: String
var _ended_reason := &""
var _loopback: LoopbackProvider


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or args[0] not in ["solo", "host", "first", "second", "overflow", "dedicated", "dedicated-client"]:
		push_error("Usage: session_smoke.gd -- solo|host|first|second|overflow|dedicated|dedicated-client port marker-directory")
		quit(1)
		return
	Engine.max_fps = 120
	var port := args[1].to_int()
	_markers = args[2]
	var main := load(String(ProjectSettings.get_setting("application/run/main_scene"))) as PackedScene
	_demo = main.instantiate()
	_game = _demo.get_node(^"Game") as CrimsonGame
	var authored := _game.definition
	var authored_rules := authored.multiplayer_rules
	_definition = authored.with_multiplayer(authored_rules.on_port(port).with_max_players(3))
	if args[0] in ["host", "first"]:
		_loopback = LoopbackProvider.new()
		_definition = _definition.with_provider(_loopback)
	_game.definition = _definition
	root.add_child(_demo)
	_game.session_ended.connect(func(reason: StringName) -> void: _ended_reason = reason)
	if not _check(authored_rules.port == 24830 and authored_rules.max_players == 4
			and _game.definition.multiplayer_rules.port == port and _game.definition.multiplayer_rules.max_players == 3,
			"Fixture overrides port and capacity independently of the authored world and rules."):
		quit(1)
		return
	var passed := false
	match args[0]:
		"solo": passed = await _run_solo()
		"host": passed = await _run_host()
		"first": passed = await _run_first()
		"second": passed = await _run_second()
		"overflow": passed = await _run_overflow()
		"dedicated": passed = await _run_dedicated()
		"dedicated-client": passed = await _run_dedicated_client()
	_game.leave()
	if passed:
		print("CRIMSON_DEMO_SESSION_PASSED %s" % args[0])
	quit(0 if passed else 1)


func _run_solo() -> bool:
	if not _check(_is_idle(), "Boot without a session."):
		return false
	_game.definition = _definition.with_multiplayer(_definition.multiplayer_rules.allow_join_in_progress(false))
	if not _check(_game.host_game().result.code == "LobbyRequired"
			and _game.join_game(ADDRESS).result.code == "LobbyRequired" and _is_idle(),
			"Lobby-only rules reject immediate networking without claiming transport."):
		return false
	_game.definition = _definition
	if not _check(_game.join_game("").result.code == "InvalidAddress" and _is_idle() and _ended_reason.is_empty(),
			"Reject a blank address without starting a session."):
		return false
	var pending := _game.join_game(ADDRESS)
	if not _check(not pending.is_finished() and _game.get_phase() == CrimsonGame.TRANSITIONING
			and _game.get_world() == null, "Keep a pending join separate from world readiness."):
		return false
	var cancelled := _game.leave()
	if not _check(cancelled == OK and pending.result.code == "Cancelled"
			and _ended_reason == &"left" and _is_idle(), "Cancel the pending operation and release transport."):
		return false
	var pending_load := _game.play_solo()
	if not _check(not pending_load.is_finished() and _game.get_phase() == CrimsonGame.TRANSITIONING
			and _game.leave() == OK and pending_load.result.code == "Cancelled" and _is_idle(),
			"Cancel during Scene Load and release the mount and transport."):
		return false
	var solo: Dictionary = await _game.play_solo().wait()
	if not _check(solo.ok and _players_match([0]) and _uses_scene_loader(_game)
			and _game.get_player(1) == _game.get_local_player(),
			"Spawn one solo actor from the actual Inspector configuration."):
		return false
	var actor := _game.get_local_player()
	var pose := actor.global_transform
	await create_timer(0.1).timeout
	if not _check(actor.global_transform.is_equal_approx(pose), "Keep the actor stationary."):
		return false
	_game.leave()
	return _check(_is_idle(), "Leaving solo removes the world and actor.") \
		and await _exercise_builder() and _exercise_legacy_world_session() \
		and await _exercise_shared_services() and await _exercise_reattach()


func _exercise_builder() -> bool:
	var authored := _game.definition
	var base := CrimsonGame.configure()
	var ready := (
		base.with_starting_world(authored.starting_world, authored.starting_world_scene)
		.with_player(authored.player_scene)
		.with_multiplayer(authored.multiplayer_rules)
	)
	if not _check(base.starting_world_scene == null and base.player_scene == null and base.multiplayer_rules == null,
			"Fluent branches preserve the base game definition."):
		return false
	var built := ready.with_starting_world(&"missing_spawns", _make_world(0)).create()
	var detached := built.get_parent() == null
	built.name = &"FluentGame"
	_demo.add_child(built)
	var invalid_result: Dictionary = await built.host_game(ADDRESS).wait()
	var invalid: bool = (
		invalid_result.code == "InvalidSpawnSetup" and built.get_phase() == CrimsonGame.IDLE
		and built.get_world() == null and built.get_local_player() == null
	)
	built.definition = ready.with_multiplayer(null)
	var solo_only: bool = built.host_game().result.code == "MultiplayerDisabled"
	var start_result: Dictionary = await built.play_solo().wait()
	var started: bool = start_result.ok and built.get_local_player() != null and _uses_scene_loader(built)
	var during_teardown: Array[String] = []
	var restarted: Array[CrimsonGameOperation] = []
	var on_phase := func(phase: int) -> void:
		if phase == CrimsonGame.IDLE:
			during_teardown.append(built.play_solo().result.code)
	built.phase_changed.connect(on_phase)
	built.session_ended.connect(func(_reason: StringName) -> void:
		restarted.append(built.play_solo())
	, CONNECT_ONE_SHOT)
	var reusable := built.leave() == OK and during_teardown == ["GameBusy"] and restarted.size() == 1
	built.phase_changed.disconnect(on_phase)
	var reused := false
	if reusable:
		var restarted_result: Dictionary = await restarted[0].wait()
		reused = restarted_result.ok and _uses_scene_loader(built)
	var ended := built.leave() == OK and built.get_world() == null and built.get_local_player() == null
	_demo.remove_child(built)
	built.queue_free()
	return _check(detached and invalid and solo_only and started and reused and ended,
			"Fluent create validates spawn setup, supports solo-only, and can restart at session_ended.")


func _exercise_legacy_world_session() -> bool:
	var legacy := (
		CrimsonWorldSession.configure()
		.with_world(_definition.starting_world_scene)
		.with_players(_definition.player_scene)
		.with_local_view(^"Camera3D", ^"Visuals")
		.create()
	)
	_demo.add_child(legacy)
	var started := legacy.start_offline() == OK and legacy.get_state() == CrimsonWorldSession.RUNNING \
		and legacy.get_local_player() != null
	var ended := legacy.leave() == OK and legacy.get_world() == null
	_demo.remove_child(legacy)
	legacy.queue_free()
	return _check(started and ended, "Preserve the standalone World Session entry point and marker convention.")


func _exercise_shared_services() -> bool:
	var world_id := String(_definition.starting_world)
	_game.definition = _definition.with_worlds({
		world_id: {"scene_id": "amber_map", "path": "res://tests/scene_load/fixtures/amber.tscn"},
	})
	var coordinated: Dictionary = await _game.start_offline().wait()
	if not _check(coordinated.ok and _uses_scene_loader(_game)
			and _game.get_node(^"SceneLoader").get_current_scene_id() == &"amber_map",
			"Coordinated startup retakes the shared Scene Loader after immediate play."):
		return false
	_game.leave()
	if not await _wait_until(_is_idle, "coordinated teardown releases the shared services"):
		return false
	var immediate: Dictionary = await _game.play_solo().wait()
	var resumed: bool = immediate.ok and immediate.world_epoch > coordinated.world_epoch \
		and _uses_scene_loader(_game) and _players_match([0])
	_game.leave()
	return _check(resumed and _is_idle(), "Immediate play can reuse the same Game after a coordinated session.")


func _exercise_reattach() -> bool:
	var retained := _definition.create()
	retained.name = &"RetainedGame"
	_demo.add_child(retained)
	var started: Dictionary = await retained.play_solo().wait()
	if not _check(started.ok and Crimson.for_node(retained) == retained, "Publish the initialized Game scope."):
		return false
	var loader := retained.get_node(^"SceneLoader") as CrimsonSceneLoadManager
	var transport := retained.get_node(^"Session") as CrimsonGameSession
	var old_world_id := retained.get_world().get_instance_id()
	_demo.remove_child(retained)
	if not _check(Crimson.for_node(retained) == null and transport.get_state() == CrimsonGameSession.IDLE,
			"Removing the retained Game closes its scope and transport."):
		retained.free()
		return false
	_demo.add_child(retained)
	if not await _wait_until(func() -> bool: return Crimson.for_node(retained) == retained, "retained Game republishes after cleanup"):
		return false
	if not _check(retained.get_node(^"SceneLoader") == loader and retained.get_node(^"Session") == transport
			and loader.get_current_scene() == null and retained.get_phase() == CrimsonGame.IDLE,
			"Reattach keeps the injected services and retires the previous world before publishing."):
		return false
	var restarted: Dictionary = await retained.play_solo().wait()
	var replaced: bool = restarted.ok and restarted.world_epoch > started.world_epoch and _uses_scene_loader(retained) \
		and retained.get_world().get_instance_id() != old_world_id
	retained.leave()
	_demo.remove_child(retained)
	retained.queue_free()
	return _check(replaced, "A retained Game starts a new world and epoch after reattachment.")


func _run_host() -> bool:
	_game.definition = _definition.with_starting_world(&"single_anchor", _make_world(1))
	if not await _start_host("Capacity three can host with one exact spawn.") \
			or not _check(_loopback.adapter.session.get_ref().set_transport_adapter(null) == ERR_BUSY,
				"An active session retains its selected transport.") \
			or not _mark("one-point-host") \
			or not await _wait_until(_marked.bind("one-point-rejected"), "spawn-unavailable guest result") \
			or not _check(_loopback.adapter.rejected_peers.size() == 1 and _loopback.adapter.rejected_peers[0] > 1,
				"The custom host adapter flushes one reliable denial to the admitted guest.") \
			or not _check(_players_match([0]), "An exhausted placement leaves the host and existing actor running."):
		return false
	_game.leave()
	_game.definition = _definition.with_starting_world(&"anchor_with_offsets", _make_world(2))
	if not await _start_host("A single anchor can author additional placement poses.") \
			or not _mark("offset-host") \
			or not await _wait_until(_players_match.bind([0, 1]), "host sees the guest at the authored offset") \
			or not _mark("offset-host-sees-guest") \
			or not await _wait_until(_marked.bind("offset-guest-left"), "offset guest leaves") \
			or not await _wait_until(_players_match.bind([0]), "offset placement becomes available again"):
		return false
	_game.leave()
	_game.definition = _definition
	if not await _start_host("Host opens the actual scene before any guest."):
		return false
	var world := _game.get_world()
	if not _mark("host-playing") \
			or not await _wait_until(_marked.bind("mismatch-left"), "guest with wrong world rejects the baseline") \
			or not await _wait_until(_players_match.bind([0]), "host frees the incompatible guest's seat") \
			or not _mark("host-cleared-mismatch") \
			or not await _wait_until(_marked.bind("rule-mismatch-left"), "guest with wrong cap rejects the baseline") \
			or not await _wait_until(_players_match.bind([0]), "host frees the incompatible rules guest's seat") \
			or not _mark("host-cleared-rule-mismatch") \
			or not await _wait_until(_players_match.bind([0, 1, 2]), "host sees two admitted guests") \
			or not _check(_game.get_world() == world, "Late joins preserve the live authority world.") \
			or not _mark("host-at-capacity") \
			or not await _wait_until(_marked.bind("overflow-rejected"), "full authority rejects a third guest") \
			or not _check(_players_match([0, 1, 2]), "A spare fourth placement does not increase the session capacity.") \
			or not _mark("host-sees-both") \
			or not await _wait_until(_players_match.bind([0, 2]), "host removes the departed guest") \
			or not _mark("host-sees-removal") \
			or not await _wait_until(_players_match.bind([0, 1, 2]), "host sees the rejoined guest") \
			or not await _wait_until(_marked.bind("first-sees-rejoin"), "first guest confirms its restored spawn") \
			or not await _wait_until(_marked.bind("second-sees-rejoin"), "second guest confirms rejoin"):
		return false
	_game.leave()
	var port := _definition.multiplayer_rules.port
	if not _check(_is_idle() and _loopback.adapter.server_ports == [port, port, port]
			and _loopback.adapter.during_open == [ERR_BUSY, ERR_BUSY, ERR_BUSY],
			"The custom host factory opens each real server and blocks reentrant session startup."):
		return false
	return await _wait_until(_marked.bind("first-sees-shutdown"), "first guest observes host shutdown") \
		and await _wait_until(_marked.bind("second-sees-shutdown"), "second guest observes host shutdown")


func _run_first() -> bool:
	if not await _wait_until(_marked.bind("one-point-host"), "host starts its single exact pose"):
		return false
	_game.definition = _definition.with_starting_world(&"single_anchor", _make_world(1))
	if not await _expect_join_denial("SpawnUnavailable", &"spawn_unavailable") \
			or not _mark("one-point-rejected") \
			or not await _wait_until(_marked.bind("offset-host"), "host opens the authored offset world"):
		return false
	_game.definition = _definition.with_starting_world(&"anchor_with_offsets", _make_world(2))
	var offset_start: Dictionary = await _game.join_game(ADDRESS).wait()
	if not _check(offset_start.ok and _players_match([0, 1], 1), "The guest spawns at the anchor's authored offset.") \
			or not await _wait_until(_marked.bind("offset-host-sees-guest"), "host confirms the offset pose"):
		return false
	_game.leave()
	if not _mark("offset-guest-left") or not await _wait_until(_marked.bind("host-playing"), "host enters the real world"):
		return false
	_game.definition = _definition.with_starting_world(&"wrong_world", _definition.starting_world_scene)
	if not await _expect_join_denial("IncompatibleWorld", &"incompatible_world") \
			or not _mark("mismatch-left") \
			or not await _wait_until(_marked.bind("host-cleared-mismatch"), "host frees the world mismatch seat"):
		return false
	_game.definition = _definition.with_multiplayer(_definition.multiplayer_rules.with_max_players(2))
	if not await _expect_join_denial("IncompatibleRules", &"incompatible_rules") \
			or not _mark("rule-mismatch-left") \
			or not await _wait_until(_marked.bind("host-cleared-rule-mismatch"), "host frees the rule mismatch seat"):
		return false
	_game.definition = _definition
	if not await _join([0, 1], 1) \
			or not _mark("first-playing") \
			or not await _wait_until(_players_match.bind([0, 1, 2], 1), "first guest sees the second late join") \
			or not await _wait_until(_marked.bind("host-sees-both"), "host confirms capacity and sees all players") \
			or not await _wait_until(_marked.bind("second-playing"), "second guest sees all players"):
		return false
	_game.leave()
	if not _check(_is_idle(), "Leaving as a guest clears its world.") \
			or not await _wait_until(_marked.bind("host-sees-removal"), "host observes despawn") \
			or not await _wait_until(_marked.bind("second-sees-removal"), "second guest observes despawn") \
			or not await _join([0, 1, 2], 1) \
			or not _mark("first-sees-rejoin"):
		return false
	var port := _definition.multiplayer_rules.port
	return _check(_loopback.adapter.client_ports == [port, port, port, port, port, port],
			"The custom guest factory opens each real direct endpoint.") \
		and await _wait_until(_host_departed, "host shutdown returns the first guest to idle") \
		and _mark("first-sees-shutdown")


func _run_second() -> bool:
	if not await _wait_until(_marked.bind("first-playing"), "first guest enters the world") \
			or not await _join([0, 1, 2], 2) \
			or not _mark("second-playing"):
		return false
	var original_camera := root.get_camera_3d()
	if not await _wait_until(_players_match.bind([0, 2], 2), "second guest sees the first guest despawn") \
			or not _mark("second-sees-removal") \
			or not await _wait_until(_players_match.bind([0, 1, 2], 2), "second guest sees the first guest rejoin") \
			or not _check(root.get_camera_3d() == original_camera, "Other roster changes preserve the local actor and camera.") \
			or not _mark("second-sees-rejoin"):
		return false
	return await _wait_until(_host_departed, "host shutdown returns the second guest to idle") \
		and _mark("second-sees-shutdown")


func _run_overflow() -> bool:
	if not await _wait_until(_marked.bind("host-at-capacity"), "host accepts its two configured guest seats"):
		return false
	var result: Dictionary = await _game.join_game(ADDRESS).wait()
	return _check(not result.ok and result.reason in ["connection_failed", "connection_timeout"] and _is_idle(),
			"ENet rejects a third guest at capacity despite an unoccupied fourth marker.") and _mark("overflow-rejected")


func _run_dedicated() -> bool:
	_game.definition = _definition.with_multiplayer(_definition.multiplayer_rules.with_max_players(1))
	var started: Dictionary = await _game.start_dedicated(ADDRESS).wait()
	if not _check(started.ok and _uses_scene_loader(_game) and _game.get_local_player() == null
			and _game.get_player(1) == null and root.get_camera_3d() == null and _dedicated_actor_count() == 0,
			"Dedicated Scene Load is ready with no local actor, camera, or reserved host slot."):
		return false
	var world := _game.get_world()
	var epoch_file := FileAccess.open(_markers.path_join("dedicated-epoch"), FileAccess.WRITE)
	if not _check(epoch_file != null, "Open the dedicated epoch fixture marker."):
		return false
	epoch_file.store_line(str(started.world_epoch))
	epoch_file.close()
	if not _mark("dedicated-ready") \
			or not await _wait_until(_marked.bind("dedicated-guest-joined"), "first guest confirms dedicated baseline") \
			or not await _wait_until(func() -> bool: return _dedicated_actor_count() == 1, "sole dedicated seat has a remote actor") \
			or not _check(root.get_camera_3d() == null, "A remote actor cannot activate a dedicated viewport camera.") \
			or not _mark("dedicated-sees-guest") \
			or not await _wait_until(func() -> bool: return _dedicated_actor_count() == 0, "dedicated departure frees its only seat") \
			or not _mark("dedicated-seat-cleared") \
			or not await _wait_until(_marked.bind("dedicated-guest-rejoined"), "dedicated guest reuses the released seat") \
			or not _check(_game.get_world() == world and _game.get_local_player() == null
				and _game.get_player(1) == null and root.get_camera_3d() == null
				and _dedicated_actor_count() == 1, "Dedicated late join preserves the authority world."):
		return false
	_game.leave()
	return _check(_is_idle(), "Dedicated shutdown retires its world.") \
		and await _wait_until(_marked.bind("dedicated-guest-ended"), "client observes dedicated shutdown")


func _run_dedicated_client() -> bool:
	_game.definition = _definition.with_multiplayer(_definition.multiplayer_rules.with_max_players(1))
	if not await _wait_until(_marked.bind("dedicated-ready"), "dedicated server completes Scene Load"):
		return false
	var epoch := FileAccess.get_file_as_string(_markers.path_join("dedicated-epoch")).strip_edges().to_int()
	var joined: Dictionary = await _game.join_game(ADDRESS).wait()
	if not _check(joined.ok and joined.world_epoch == epoch and _uses_scene_loader(_game)
			and _game.get_player(1) == null and _players_match([0]), "Dedicated baseline uses the authority epoch and only a guest actor.") \
			or not _mark("dedicated-guest-joined") \
			or not await _wait_until(_marked.bind("dedicated-sees-guest"), "dedicated authority observes the first join"):
		return false
	_game.leave()
	if not await _wait_until(_marked.bind("dedicated-seat-cleared"), "dedicated authority releases the only seat"):
		return false
	var rejoined: Dictionary = await _game.join_game(ADDRESS).wait()
	return _check(rejoined.ok and rejoined.world_epoch == epoch and _players_match([0]), "Reconnect to the same dedicated world.") \
		and _mark("dedicated-guest-rejoined") and await _wait_until(_host_departed, "dedicated shutdown retires the guest scene") \
		and _mark("dedicated-guest-ended")


func _dedicated_actor_count() -> int:
	var world := _game.get_world()
	return world.get_node(^"Players").get_child_count() if world != null else -1


func _start_host(description: String) -> bool:
	var result: Dictionary = await _game.host_game(ADDRESS).wait()
	return _check(result.ok and _players_match([0]) and _uses_scene_loader(_game), description)


func _uses_scene_loader(game: CrimsonGame) -> bool:
	var loader := game.get_node(^"SceneLoader") as CrimsonSceneLoadManager
	return loader.get_current_scene() != null and loader.get_current_scene() == game.get_world()


func _expect_join_denial(code: String, reason: StringName) -> bool:
	var observed: Array[int] = []
	var collect := func(phase: int) -> void: observed.append(phase)
	_game.phase_changed.connect(collect)
	var result: Dictionary = await _game.join_game(ADDRESS).wait()
	_game.phase_changed.disconnect(collect)
	return _check(
		not result.ok and result.code == code and result.get("reason", "") == String(reason)
		and _ended_reason == reason and _is_idle() and observed == [CrimsonGame.TRANSITIONING, CrimsonGame.IDLE],
		"A denied guest never publishes a world or running phase and receives %s." % reason
	)


func _join(expected_slots: Array, local_slot: int) -> bool:
	var result: Dictionary = await _game.join_game(ADDRESS).wait()
	return _check(result.ok and result.world_id == String(_definition.starting_world),
			"Join a running game and settle only after its local actor is ready.") \
		and await _wait_until(_players_match.bind(expected_slots, local_slot), "verify the host's spawn assignments")


func _make_world(num_poses: int) -> PackedScene:
	var world := Node3D.new()
	var points := Node3D.new()
	points.name = &"SpawnPoints"
	world.add_child(points)
	points.owner = world
	if num_poses > 0:
		var anchor := CrimsonSpawnPoint.new()
		anchor.name = &"Anchor"
		points.add_child(anchor)
		anchor.owner = world
		for index in range(1, num_poses):
			var offset := Marker3D.new()
			offset.position.x = float(index) * 2.0
			anchor.add_child(offset, true)
			offset.owner = world
	var scene := PackedScene.new()
	var result := scene.pack(world)
	world.free()
	return scene if result == OK else null


func _players_match(expected_slots: Array, local_slot: int = 0) -> bool:
	var hub := _game.get_world()
	var camera := root.get_camera_3d()
	if _game.get_phase() != CrimsonGame.RUNNING or hub == null or camera == null:
		return false
	var actors := hub.get_node(^"Players")
	var spawns := hub.get_node(^"SpawnPoints")
	var placements: Array[Marker3D] = []
	for point: Marker3D in spawns.get_children():
		placements.append(point)
		for child in point.get_children():
			if child is Marker3D:
				placements.append(child)
	if actors.get_child_count() != expected_slots.size():
		return false
	var observed: Array[int] = []
	for child in actors.get_children():
		var actor := child as Node3D
		var at_slot := -1
		for slot in placements.size():
			if actor.global_transform.is_equal_approx(placements[slot].global_transform):
				at_slot = slot
				break
		var local := at_slot == local_slot
		var visuals := actor.get_node(^"Visuals") as Node3D
		if at_slot == -1 or (camera.get_parent() == actor) != local \
				or (_game.get_local_player() == actor) != local or visuals.visible == local:
			return false
		observed.append(at_slot)
	observed.sort()
	return observed == expected_slots


func _is_idle() -> bool:
	return _game.get_phase() == CrimsonGame.IDLE and _game.get_world() == null and _game.get_local_player() == null


func _host_departed() -> bool:
	return _is_idle() and _ended_reason == &"server_disconnected"


func _mark(event: String) -> bool:
	var file := FileAccess.open(_markers.path_join(event), FileAccess.WRITE)
	return _check(file != null, "Publish test marker %s." % event)


func _marked(event: String) -> bool:
	return FileAccess.file_exists(_markers.path_join(event))


func _wait_until(predicate: Callable, description: String) -> bool:
	var deadline := Time.get_ticks_msec() + WAIT_MS
	while not predicate.call():
		if Time.get_ticks_msec() >= deadline:
			return _check(false, "Timed out waiting for %s." % description)
		await process_frame
	return true


func _check(condition: bool, description: String) -> bool:
	if not condition:
		push_error(description)
	return condition
