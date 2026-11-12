# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends "res://tests/game_flow/game_flow_harness.gd"


class FixtureProvider extends GameServiceProvider:
	# Loader returned by the factory and later used for the real transition.
	var loader: SceneLoadManager

	# Create one distinct loader before the game is published by the container.
	func _create_scene_loader() -> SceneLoadManager:
		loader = SceneLoadManager.new()
		loader.set_load_timeout_msec(7000)
		return loader


# Separate slot whose one fixture generation is deliberately damaged.
const CORRUPT_SLOT := "corrupt_fixture"
# Restore commit observations recorded only while the selected generation is activating.
var _restore_observations: Array[Dictionary] = []


# Defer setup until the scene tree accepts persistent fixture nodes.
func _initialize() -> void:
	_run.call_deferred()


# Run the offline scenario, release the fixture, and emit the process result.
func _run() -> void:
	_start_watchdog()
	var passed := await _run_offline()
	if is_instance_valid(_game):
		var scenes := GameServices.scenes()
		_game.leave()
		await process_frame
		_shell.remove_child(_game)
		if passed:
			var expired: Dictionary = await scenes.load(AMBER).wait()
			passed = _expect(GameServices.scenes() == null and scenes.get_world() == null
					and expired.get("code") == "Unavailable",
					"A retained scene facade expires when the game leaves its scope.")
		_game.free()
	if passed:
		print("CRIMSON_GAME_FLOW_OFFLINE_PASSED")
	quit(0 if passed else 1)


# Save a world, change the active mount and state, and verify atomic restore and failure paths.
func _run_offline() -> bool:
	var detached := Node.new()
	var definition := GameDefinition.configure()
	var rejected_owner := definition.boot(detached)
	detached.free()
	if not _expect(rejected_owner == null, "Game boot rejects an owner outside the tree."):
		return false
	var retiring := Node.new()
	root.add_child(retiring)
	retiring.child_entered_tree.connect(func(_node: Node) -> void: retiring.queue_free(), CONNECT_ONE_SHOT)
	if not _expect(definition.boot(retiring) == null, "Game boot returns null when its owner is retired during attachment."):
		return false
	await process_frame
	var provider := FixtureProvider.new()
	if not _configure_game(true, provider) or not _register_authority_store():
		return false
	if not _expect(GameServices.game() == _game and GameServices.for_node(provider.loader) == _game
			and _game.get_node("SceneLoader") == provider.loader,
			"The global facade resolves the game and uses the injected loader."):
		return false
	var start := _game.start_offline()
	if not _expect(not start.is_finished(), "Start returns a pending operation while the scene prepares."):
		return false
	var started: Dictionary = await start.wait()
	if not _expect(started.get("ok", false) and _mounted_world(AMBER, "amber"), "Await a pending start operation into Amber: %s" % started):
		return false
	if not _expect(provider.loader.get_current_scene() == _game.get_world(),
			"The injected loader owns the world mounted through the public game entry point."):
		return false
	var rejected := _game.start_match()
	var rejected_result: Dictionary = await rejected.wait()
	if not _expect(rejected.is_finished() and rejected_result.get("code") == "NotInAuthorityLobby",
		"Await a start rejected synchronously while already running: %s" % rejected_result):
		return false
	var amber_epoch := _game.get_world_epoch()
	var changed: Dictionary = await _game.submit(COMMAND, {"score": 17}).wait()
	if not _expect(changed.get("ok", false) and changed.get("player") == "local" and _authority_score() == 17,
		"Dispatch through the offline authority's stable local player: %s" % changed):
		return false
	var slot := _game.save_slot(PROFILE, SLOT)
	var request := slot.save().named("Amber checkpoint").with_context({"region": "Amber"})
	var saved: Dictionary = await request.submit().wait()
	if not _expect(saved.get("ok", false), "Commit the Amber checkpoint: %s" % saved):
		return false
	var list := slot.history()
	var listed: Dictionary = await list.wait()
	var settled_list: Dictionary = await list.wait()
	var generations: Array = listed.get("value", [])
	if not _expect(listed.get("ok", false) and generations.size() == 1 and settled_list == listed,
		"Read the same generation listing from the pending operation and its retained result: %s" % listed):
		return false
	var corrupt: Dictionary = await _game.save(PROFILE, CORRUPT_SLOT).wait()
	if not _expect(corrupt.get("ok", false), "Commit an independent generation for the corruption case: %s" % corrupt) \
		or not _corrupt_fixture_generation():
		return false

	var scenes := GameServices.scenes()
	var traveled: Dictionary = await scenes.load(COBALT).wait()
	if not _expect(traveled.get("ok", false) and _mounted_world(COBALT, "cobalt") and _game.get_world_epoch() > amber_epoch,
		"Travel to Cobalt and advance the world epoch: %s" % traveled):
		return false
	if not _expect(scenes.get_world() == _game.get_world(),
			"The global scene service returns the world mounted by the injected loader."):
		return false
	var mutated: Dictionary = await _game.submit(COMMAND, {"score": 92}).wait()
	if not _expect(mutated.get("ok", false) and _authority_score() == 92, "Change authority state after travel: %s" % mutated):
		return false
	var cobalt_world := _game.get_world()
	var cobalt_epoch := _game.get_world_epoch()
	var coordinator := _game.get_transactions()
	var cobalt_revision := coordinator.get_revision()

	var missing: Dictionary = await _game.restore(PROFILE, "empty_fixture").wait()
	if not _expect(not missing.get("ok", true) and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
		"A missing slot preserves Cobalt, state, epoch, and revision: %s" % missing):
		return false
	var malformed: Dictionary = await _game.restore(PROFILE, CORRUPT_SLOT).wait()
	if not _expect(not malformed.get("ok", true) and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
		"A malformed generation preserves Cobalt, state, epoch, and revision: %s" % malformed):
		return false
	var missing_scene: Dictionary = await _game.travel_to(MISSING_SCENE).wait()
	if not _expect(not missing_scene.get("ok", true) and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
		"A missing destination scene fails before changing the live mount: %s" % missing_scene):
		return false

	coordinator.committed.connect(_record_restore_commit)
	var restored: Dictionary = await slot.restore_latest().wait()
	if not _expect(restored.get("ok", false) and _mounted_world(AMBER, "amber") and _authority_score() == 17
			and _game.get_world_epoch() > cobalt_epoch and coordinator.get_revision() > cobalt_revision,
		"Restore Amber from Cobalt at a new epoch and revision: %s" % restored):
		return false
	var incoming := _game.get_world()
	if not _expect(incoming.get_meta("world_at_mount", &"") == AMBER and incoming.get_meta("score_at_mount", -1) == 17,
		"The incoming scene sees restored world identity and score in _ready."):
		return false
	if not _expect(_restore_observations == [{"world_id": AMBER, "layout": "amber", "score": 17}],
			"The single deferred committed notification observes the new mount: %s" % [_restore_observations]):
		return false

	if not _expect(_game.leave() == OK, "End the offline session.") \
			or not await _wait_for(func() -> bool: return _game.get_phase() == GameRuntime.IDLE, "offline teardown"):
		return false
	if not _expect(_game.get_world() == null and _game.get_transactions() != coordinator,
			"Teardown removes the scene and replaces the authority scope.") \
			or not _register_authority_store() \
			or not _expect(_game.open_offline() == OK, "Reopen an empty offline lobby."):
		return false
	var continued: Dictionary = await slot.restore_latest().wait()
	return _expect(continued.get("ok", false) and _mounted_world(AMBER, "amber") and _authority_score() == 17,
		"Continue the saved game in a fresh authority scope: %s" % continued)


# Confirm that a rejected transition leaves the live scene and committed state intact.
func _cobalt_unchanged(world: Node, epoch: int, revision: int) -> bool:
	return _mounted_world(COBALT, "cobalt") and _game.get_world() == world and _authority_score() == 92 \
		and _game.get_world_epoch() == epoch and _game.get_transactions().get_revision() == revision


# Capture the authority scene visible to a normal store observer at publication time.
func _record_restore_commit(_revision: int, _stores: PackedStringArray) -> void:
	var world := _game.get_world()
	_restore_observations.append({
		"world_id": _game.get_world_id(),
		"layout": world.get_meta("fixture_layout", "") if is_instance_valid(world) else "",
		"score": _authority_score(),
	})


# Truncate the only generation of the isolated corruption fixture and leave the good slot alone.
func _corrupt_fixture_generation() -> bool:
	var folder := _save_root.path_join("p-%s/s-%s" % [PROFILE, CORRUPT_SLOT])
	var generations := PackedStringArray()
	for filename in DirAccess.get_files_at(folder):
		if filename.begins_with("g-") and filename.ends_with(".save"):
			generations.append(filename)
	if not _expect(generations.size() == 1, "Find exactly one isolated generation to damage."):
		return false
	var file := FileAccess.open(folder.path_join(generations[0]), FileAccess.WRITE)
	if not _expect(file != null, "Open the isolated fixture generation for corruption."):
		return false
	file.store_string("invalid checkpoint fixture")
	file.close()
	return true
