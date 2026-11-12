# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

# Starting world and checkpoint destination.
const AMBER := &"campaign.amber"
# Travel destination used before restoring Amber.
const COBALT := &"campaign.cobalt"
# World whose catalog entry points to an intentionally absent asset.
const MISSING_SCENE := &"campaign.missing_scene"
# Stable authority store ID and client projection record key.
const STORE := "match_state"
# Named gameplay command declared on every fixture peer.
const COMMAND := "world.set_score"
# Profile namespace inside the disposable checkpoint root.
const PROFILE := "fixture"
# Slot holding the valid cross-world checkpoint.
const SLOT := "cross_world"
# Maximum wait for a multiplayer fixture milestone.
const TIMEOUT_MS := 10000

# Public game facade under a persistent path shared by ENet processes.
var _game: GameRuntime
# Persistent parent for the game and the network fixture's control node.
var _shell: Node
# Per-process checkpoint location, separate from other suites and user saves.
var _save_root := ""
# Ordered authority handler calls used to verify trusted player identity.
var _dispatch_players: Array[String] = []


# Set up a disposable fixture and register the same gameplay contract on each peer.
func _configure_game(authority: bool = true, provider: GameServiceProvider = null) -> bool:
	Engine.max_fps = 120
	_save_root = "user://crimson-game-flow-%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]
	var definition := (
		GameDefinition.configure()
		.with_worlds({
			AMBER: {"scene_id": &"amber_map", "path": "res://tests/game_flow/fixtures/amber.tscn"},
			COBALT: {"scene_id": &"cobalt_map", "path": "res://tests/game_flow/fixtures/cobalt.tscn"},
			MISSING_SCENE: {"scene_id": &"missing_map", "path": "res://tests/game_flow/fixtures/unavailable.tscn"},
		})
		.starting_in(AMBER)
		.with_saves("crimson.game.flow.fixture", _save_root)
		.with_provider(provider)
	)
	_shell = Node.new()
	_shell.name = &"CrimsonFlowHarness"
	root.add_child(_shell)
	_game = definition.boot(_shell)
	if not _expect(_game != null and _game.get_parent() == _shell and _game.name == &"Game",
			"The configured game attaches to its owner on the shared Game node path."):
		return false
	var registered := _game.register_command(COMMAND, _set_score) if authority else _game.declare_command(COMMAND)
	return _expect(registered == OK, "Register the command on authority and declare it on clients.") \
		and _expect(_game.set_shared_stores(PackedStringArray([STORE])) == OK, "Share the selected store with admitted clients.")


# Register the game's authoritative store before opening a match.
func _register_authority_store() -> bool:
	var coordinator := _game.get_transactions()
	return _expect(coordinator.register_store(STORE, {"score": 0, "player": ""}, _prepare_score) == OK,
		"Register the authoritative score store.")


# Validate detached store operations and produce the next candidate state.
func _prepare_score(state: Dictionary, changes: Array, _context: Dictionary) -> Dictionary:
	for change: Dictionary in changes:
		if not change.get("score") is int or not change.get("player") is String:
			return {"ok": false, "message": "Expected an integer score and a player ID."}
		state["score"] = change["score"]
		state["player"] = change["player"]
	if not state.get("score") is int or not state.get("player") is String:
		return {"ok": false, "message": "The stored score is invalid."}
	return {"ok": true, "state": state}


# Commit one game-owned command using the identity resolved by the native router.
func _set_score(player_id: String, intent: Dictionary) -> Dictionary:
	_dispatch_players.append(player_id)
	var payload: Dictionary = intent["payload"]
	var coordinator := _game.get_transactions()
	var transaction := coordinator.begin()
	var error := transaction.add_change(STORE, {"score": payload.get("score"), "player": player_id},
		coordinator.get_store_revision(STORE))
	if error != OK:
		return {"ok": false, "code": "TestChangeRejected"}
	var result := coordinator.commit(transaction)
	result["player"] = player_id
	return result


# Read the committed authority value at the current revision.
func _authority_score() -> int:
	return int(_game.get_transactions().read_store(STORE).get("score", -1))


# Verify that the ready phase exposes the expected logical world and its mounted fixture.
func _mounted_world(world_id: StringName, layout: String) -> bool:
	var world := _game.get_world()
	return _game.get_phase() == GameRuntime.RUNNING and _game.get_world_id() == world_id \
		and is_instance_valid(world) and world.get_meta("fixture_layout", "") == layout


# Stop stalled operation awaits and provide a diagnostic before the process runner times out.
func _start_watchdog() -> void:
	create_timer(30.0).timeout.connect(func() -> void:
		push_error("Game Flow fixture exceeded its 30-second operation budget.")
		quit(1)
	)


# Poll a fixture condition across frames within a bounded deadline.
func _wait_for(condition: Callable, description: String) -> bool:
	var deadline := Time.get_ticks_msec() + TIMEOUT_MS
	while not condition.call():
		if Time.get_ticks_msec() >= deadline:
			push_error("Timed out waiting for %s." % description)
			return false
		await process_frame
	return true


# Report a failed assertion and let its caller stop the scenario immediately.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		push_error(description)
	return condition
