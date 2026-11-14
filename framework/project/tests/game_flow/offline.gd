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
# Inject a game-owned validation failure without changing the persisted checkpoint.
var _reject_restore := false


# Defer setup until the scene tree accepts persistent fixture nodes.
func _initialize() -> void:
	_run.call_deferred()


# Run the offline scenario, release the fixture, and emit the process result.
func _run() -> void:
	_start_watchdog()
	var passed := await _run_offline()
	if is_instance_valid(_game):
		if _game.is_inside_tree():
			_game.leave()
			await process_frame
			_shell.remove_child(_game)
		await process_frame
		_game.free()
	if passed:
		print("CRIMSON_GAME_FLOW_OFFLINE_PASSED")
	quit(0 if passed else 1)


# Return a recognizable cause for the fixture's intentionally rejected restore.
func _validate_fixture_restore(_candidate: Dictionary, _context: Dictionary) -> Dictionary:
	if _reject_restore:
		return {"ok": false, "message": "Fixture score is blocked for restore."}
	return {"ok": true}


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
	if not _configure_game(true, provider) or not _register_authority_store(_validate_fixture_restore):
		return false
	if not _expect(GameServices.game() == _game and GameServices.for_node(provider.loader) == _game
			and _game.get_node("SceneLoader") == provider.loader,
			"The global facade resolves the game and uses the injected loader."):
		return false
	var unavailable_save: Dictionary = await _game.save(PROFILE, SLOT).wait()
	if not _expect(unavailable_save.get("code") == "SaveNotReady" and "authority" in unavailable_save.message,
			"A save before entering the world identifies its readiness requirement: %s" % unavailable_save):
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
	var unknown_world: Dictionary = await _game.travel_to(&"campaign.unregistered").wait()
	if not _expect(unknown_world.get("code") == "UnknownWorld" and "campaign.unregistered" in unknown_world.message,
			"An unknown world result identifies the requested world: %s" % unknown_world):
		return false
	var invalid_metadata: Dictionary = await _game.save(PROFILE, SLOT, {"invalid": self}).wait()
	if not _expect(invalid_metadata.get("code") == "InvalidData" and "JSON-compatible" in invalid_metadata.message,
			"The save result retains the coordinator's metadata failure: %s" % invalid_metadata):
		return false
	var invalid_profile: Dictionary = await _game.restore("Bad Profile", SLOT, "1", "invalid").wait()
	var invalid_generation: Dictionary = await _game.restore(PROFILE, SLOT, "1", "").wait()
	if not _expect(invalid_profile.get("code") == "InvalidArgument" and "Profile and slot" in invalid_profile.message
			and invalid_generation.get("code") == "InvalidArgument" and "sequence and ID" in invalid_generation.message,
			"Restore rejects invalid routing and generation selection with distinct messages: %s, %s" % [invalid_profile, invalid_generation]):
		return false
	var saved: Dictionary = await request.submit().wait()
	if not _expect(saved.get("ok", false) and not String(saved.get("message", "")).is_empty(),
			"Commit the Amber checkpoint with a success message: %s" % saved):
		return false
	var superseded := _game.save(PROFILE, "autosave", {}, true)
	var replacement := _game.save(PROFILE, "autosave", {}, true)
	var replaced: Dictionary = await superseded.wait()
	var autosaved: Dictionary = await replacement.wait()
	if not _expect(replaced.get("code") == "Superseded" and "replaced" in replaced.message and autosaved.get("ok", false),
			"A newer autosave supersedes the pending intent with an explicit result: %s, %s" % [replaced, autosaved]):
		return false
	var list := slot.history()
	var listed: Dictionary = await list.wait()
	var settled_list: Dictionary = await list.wait()
	var generations: Array = listed.get("value", [])
	if not _expect(listed.get("ok", false) and generations.size() == 1 and settled_list == listed,
		"Read the same generation listing from the pending operation and its retained result: %s" % listed):
		return false
	var newer: Dictionary = await request.submit().wait()
	if not _expect(newer.get("ok", false), "Create a newer generation so the original can be restored as a backup."):
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
	if not _expect(missing.get("code") == "NotFound" and "no committed checkpoint" in missing.message
			and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
		"A missing slot preserves Cobalt, state, epoch, and revision: %s" % missing):
		return false
	var malformed: Dictionary = await _game.restore(PROFILE, CORRUPT_SLOT).wait()
	if not _expect(malformed.get("code") == "Corrupt" and not String(malformed.get("message", "")).is_empty()
			and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
		"A malformed generation preserves Cobalt, state, epoch, and revision: %s" % malformed):
		return false
	var missing_scene: Dictionary = await _game.travel_to(MISSING_SCENE).wait()
	if not _expect(not missing_scene.get("ok", true) and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
		"A missing destination scene fails before changing the live mount: %s" % missing_scene):
		return false

	var generation: Dictionary = generations[0]
	_reject_restore = true
	var rejected_restore: Dictionary = await slot.restore(generation.sequence, generation.id).wait()
	_reject_restore = false
	if not _expect(rejected_restore.get("code") == "RestoreFailed"
			and "Fixture score is blocked for restore." in String(rejected_restore.get("message", ""))
			and _cobalt_unchanged(cobalt_world, cobalt_epoch, cobalt_revision),
			"A rejected restore keeps the validator's reason and leaves the mounted world unchanged: %s" % rejected_restore):
		return false
	coordinator.committed.connect(_record_restore_commit)
	var restored: Dictionary = await slot.restore(generation.sequence, generation.id).wait()
	if not _expect(restored.get("ok", false) and _mounted_world(AMBER, "amber") and _authority_score() == 17
			and _game.get_world_epoch() > cobalt_epoch and coordinator.get_revision() > cobalt_revision
			and "different slot" in String(restored.get("warning", "")),
		"Restore Amber from Cobalt at a new epoch and revision: %s" % restored):
		return false
	var recovery_save: Dictionary = await request.submit().wait()
	if not _expect(recovery_save.get("code") == "RecoveryRequiresNewSlot" and "different slot" in recovery_save.message,
			"A backup restore directs a subsequent save into a different slot: %s" % recovery_save):
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
	if not _expect(continued.get("ok", false) and _mounted_world(AMBER, "amber") and _authority_score() == 17,
			"Continue the saved game in a fresh authority scope: %s" % continued):
		return false

	# Detach during admission, before the asynchronous scene loader starts.
	_game.phase_changed.connect(func(value: int) -> void:
		if value == GameRuntime.TRANSITIONING:
			_shell.remove_child(_game),
		CONNECT_ONE_SHOT)
	var canceled: Dictionary = await _game.travel_to(COBALT).wait()
	var expired_scene: Dictionary = await scenes.load(AMBER).wait()
	var expired_save: Dictionary = await request.submit().wait()
	var expired_restore: Dictionary = await slot.restore_latest().wait()
	if not _expect(canceled.get("code") == "Cancelled" and "scene tree" in canceled.message,
			"Travel settles when a phase observer detaches the game: %s" % canceled):
		return false
	return _expect(GameServices.scenes() == null and scenes.get_world() == null
			and expired_scene.get("code") == "Unavailable" and expired_save.get("code") == "Unavailable"
			and expired_restore.get("code") == "Unavailable",
			"Retained scene and checkpoint handles expire with their owning game.")


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
