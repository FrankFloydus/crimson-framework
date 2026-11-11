# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Runs native match runtime assertions in a headless scene tree.
extends SceneTree


## Total assertions evaluated across the suite.
var _checks := 0
## Assertions that reported errors and will cause a nonzero exit.
var _failures := 0
## Coordinator shared by the transaction test and its callback fixtures.
var _coordinator: CrimsonTransactionCoordinator
## Separate transaction used to probe writes during preparation and notification.
var _nested: CrimsonTransaction
## Snapshots captured by the synchronous commit observer.
var _observed: Array[Dictionary] = []
## Result of attempting a nested commit from a store preparer.
var _prepare_result: Dictionary
## Retained candidate used to verify that publication copies prepared data.
var _prepared_a: Dictionary
## Candidate and live values recorded by the rejecting aggregate validator.
var _validated_pair: Dictionary
## Router used by the command test and its reentrant handler.
var _router: CrimsonCommandRouter
## Commands that reached the gameplay handler, including policy denials.
var _commands: Array[Dictionary] = []
## Result of attempting recursive command dispatch from the handler.
var _nested_command: Dictionary


## Defers test execution until scene tree initialization has finished.
func _initialize() -> void:
	_run.call_deferred()


## Runs the suite and reports its result to the headless runner.
func _run() -> void:
	_test_transactions()
	_test_checkpoint_restore()
	_test_inventory_pickup()
	_test_possession_and_commands()
	_test_player_binding_generations()
	print("Crimson Match Runtime: %d checks, %d failures." % [_checks, _failures])
	if _failures == 0:
		print("CRIMSON_MATCH_RUNTIME_TESTS_PASSED")
	quit(0 if _failures == 0 else 1)


## Checks detachment, revisions, reentrant calls, and all-or-nothing publication.
func _test_transactions() -> void:
	_coordinator = CrimsonTransactionCoordinator.new()
	var initial := {"nested": {"actor": "nobody"}}
	_expect(_coordinator.register_store("a", initial, _prepare_a) == OK, "Register the first store.")
	_expect(_coordinator.register_store("b", {"count": 0}, _prepare_b) == OK, "Register the second store.")
	_expect(_coordinator.register_validator("pair", PackedStringArray(["a", "b"]), _validate_pair) == OK,
		"Register a validator for changes spanning both stores.")
	initial["nested"]["actor"] = "mutated"
	var view := _coordinator.read_store("a")
	_expect(view["nested"]["actor"] == "nobody", "Registration detaches nested input state.")
	view["nested"]["actor"] = "leaked"
	_expect(_coordinator.read_store("a")["nested"]["actor"] == "nobody", "Reads cannot mutate live state.")

	var foreign := CrimsonTransactionCoordinator.new()
	var context := {"actor": {"id": "alice"}}
	var transaction := _coordinator.begin(context)
	var operation := {"value": {"id": "first"}}
	_expect(transaction.add_change("a", operation, 0) == OK, "Stage the first store at its current revision.")
	_expect(transaction.add_change("b", {"add": 1}, 0) == OK, "Stage a second store in the same transaction.")
	_expect(transaction.add_change("b", {"add": 2}, 0) == OK, "Batch more than one operation in a store.")
	operation["value"]["id"] = "mutated"
	context["actor"]["id"] = "mutated"
	_expect(not _ok(foreign.commit(transaction)), "Reject a transaction from another coordinator.")
	_nested = _coordinator.begin()
	_expect(_nested.add_change("b", {"add": 1}, 0) == OK, "Prepare a distinct handle for reentrancy checks.")
	_coordinator.committed.connect(_observe_commit)
	var result := _coordinator.commit(transaction)
	_expect(_ok(result) and result.get("revision") == 1, "Publish both stores at a single committed revision.")
	_expect(_coordinator.get_store_revision("a") == 1 and _coordinator.get_store_revision("b") == 1,
		"Advance both affected store revisions once.")
	_expect(_prepare_result.get("code") == "busy", "A participant cannot commit reentrantly during preparation.")
	_expect(_observed.size() == 1, "Send one notification for the transaction.")
	if not _observed.is_empty():
		var event := _observed[0]
		_expect(event["revision"] == 1 and event["stores"].size() == 2
			and "a" in event["stores"] and "b" in event["stores"], "The event identifies both stores and the shared revision.")
		_expect(event["a"] == {"actor": "alice", "value": {"id": "first"}} and event["b"] == {"count": 3},
			"Observers read the whole committed state with detached operations and context.")
		_expect(event["nested"].get("code") == "busy", "Reject a nested commit during publication notification.")
	_prepared_a["nested"]["value"]["id"] = "mutated after publication"
	_expect(_coordinator.read_store("a")["nested"]["value"]["id"] == "first",
		"A preparer cannot change the live store through a retained candidate reference.")
	_expect(not _ok(_coordinator.commit(transaction)), "A committed handle cannot publish twice.")
	_expect(_coordinator.commit(_nested).get("code") == "stale_revision", "A busy attempt does not consume its handle, but later detects the stale revision.")

	var rejected := _coordinator.begin({"actor": {"id": "bob"}})
	_expect(rejected.add_change("a", {"value": {"id": "aborted"}}, 1) == OK
		and rejected.add_change("b", {"add": -1}, 1) == OK, "Stage a two-store transaction whose second operation will fail.")
	_expect(not _ok(_coordinator.commit(rejected)), "A participant can reject the entire transaction.")
	_expect(_coordinator.get_revision() == 1 and _coordinator.get_store_revision("a") == 1
		and _coordinator.get_store_revision("b") == 1 and _observed.size() == 1,
		"Rejection advances no revision and sends no publication notification.")
	_expect(_coordinator.read_store("a")["nested"]["value"]["id"] == "first" and _coordinator.read_store("b")["count"] == 3,
		"Rejection leaves both stores at their previous values.")
	_expect(not _ok(_coordinator.commit(rejected)), "A rejected handle cannot be retried implicitly.")
	var incompatible := _coordinator.begin({"actor": {"id": "carol"}, "deny_pair": true})
	_expect(incompatible.add_change("a", {"value": {"id": "candidate"}}, 1) == OK
		and incompatible.add_change("b", {"add": 4}, 1) == OK, "Stage valid store operations for aggregate validation.")
	_expect(_coordinator.commit(incompatible).get("code") == "validation_failed",
		"Reject a pair of individually valid operations that fails aggregate validation.")
	_expect(_validated_pair.get("prepared_a") == "candidate" and _validated_pair.get("prepared_b") == 7
		and _validated_pair.get("live_a") == "first" and _validated_pair.get("live_b") == 3,
		"The validator sees both prepared stores while reads still expose the previous commit.")
	_expect(_coordinator.get_revision() == 1 and _coordinator.get_store_revision("a") == 1
		and _coordinator.get_store_revision("b") == 1 and _observed.size() == 1
		and _coordinator.read_store("a")["nested"]["value"]["id"] == "first" and _coordinator.read_store("b")["count"] == 3,
		"Aggregate rejection leaves live data and revisions untouched and sends no notification.")
	_expect(_coordinator.begin({"object": RefCounted.new()}) == null, "Reject live objects in transaction context.")
	var invalid := _coordinator.begin()
	_expect(invalid.add_change("a", {"nested": [{"object": RefCounted.new()}]}, 1) != OK,
		"Reject live objects nested in an operation.")
	_coordinator.committed.disconnect(_observe_commit)
	_nested = null
	_coordinator = null


## Builds the first candidate and probes reentrancy during preparation.
func _prepare_a(current: Dictionary, operations: Array, context: Dictionary) -> Dictionary:
	_prepare_result = _coordinator.commit(_nested)
	current["nested"] = {"actor": context["actor"]["id"], "value": operations[0]["value"]}
	_prepared_a = current
	return {"ok": true, "state": current}


## Applies a batch of counter increments and rejects negative additions.
func _prepare_b(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
	for operation: Dictionary in operations:
		if operation["add"] < 0:
			return {"ok": false, "message": "Only additions are allowed."}
		current["count"] += operation["add"]
	return {"ok": true, "state": current}


## Compares staged and committed stores when the fixture requests a rejection.
func _validate_pair(prepared: Dictionary, context: Dictionary) -> Dictionary:
	if not context.get("deny_pair", false):
		return {"ok": true}
	_validated_pair = {
		"prepared_a": prepared["a"]["nested"]["value"]["id"],
		"prepared_b": prepared["b"]["count"],
		"live_a": _coordinator.read_store("a")["nested"]["value"]["id"],
		"live_b": _coordinator.read_store("b")["count"],
	}
	return {"ok": false, "message": "The prepared pair violates a shared rule."}


## Captures all published stores and probes reentrancy during notification.
func _observe_commit(revision: int, stores: PackedStringArray) -> void:
	_observed.append({
		"revision": revision,
		"stores": stores,
		"a": _coordinator.read_store("a")["nested"],
		"b": _coordinator.read_store("b"),
		"nested": _coordinator.commit(_nested),
	})


## Checks detached capture, full validation, stale candidates, and deferred publication.
func _test_checkpoint_restore() -> void:
	var coordinator := CrimsonTransactionCoordinator.new()
	_expect(coordinator.register_store("a", {"count": 2}, _prepare_b, _check_checkpoint_counter) == OK
		and coordinator.register_store("b", {"count": 3}, _prepare_checkpoint_counter) == OK, "Register checkpoint stores with explicit and implicit checks.")
	_expect(coordinator.register_validator("order", PackedStringArray(["a", "b"]), _check_checkpoint_order) == OK,
		"Register an invariant spanning all checkpoint stores.")
	var captured := coordinator.capture_snapshot()
	var snapshot: Dictionary = captured.get("stores", {})
	_expect(_ok(captured) and captured.get("revision") == 0 and snapshot == {"a": {"count": 2}, "b": {"count": 3}},
		"Capture each committed store at the same revision.")
	_expect(not _ok(coordinator.prepare_restore({"a": {"count": 2}}))
		and not _ok(coordinator.prepare_restore({"a": {"count": 2}, "unknown": {"count": 3}}))
		and not _ok(coordinator.prepare_restore({"a": {"count": 2}, "b": 3})), "Reject missing, unknown, and malformed stores.")
	_expect(coordinator.prepare_restore({"a": {"count": 1.0}, "b": {"count": 3}}).get("code") == "restore_validation_failed",
		"Reject a store value that violates its explicit schema.")
	_expect(coordinator.prepare_restore({"a": {"count": 2}, "b": {"count": -1}}).get("code") == "restore_validation_failed",
		"Run the existing store preparer as a restore check when no separate check is registered.")
	_expect(coordinator.prepare_restore({"a": {"count": 4}, "b": {"count": 3}}).get("code") == "validation_failed",
		"Check invariants across the complete candidate set.")

	var stale: CrimsonTransaction = coordinator.prepare_restore(snapshot).get("candidate")
	_expect(stale != null, "Prepare a checkpoint without modifying live stores.")
	var mutation := coordinator.begin()
	_expect(mutation.add_change("b", {"add": 2}, 0) == OK and _ok(coordinator.commit(mutation)), "Advance one store after preparation.")
	_expect(coordinator.publish_restore(stale).get("code") == "stale_revision" and coordinator.read_store("b")["count"] == 5,
		"A stale checkpoint candidate cannot overwrite an intervening commit.")
	var registry_stale: CrimsonTransaction = coordinator.prepare_restore(snapshot).get("candidate")
	_expect(coordinator.register_validator("new_rule", PackedStringArray(["b"]), _check_checkpoint_order) == OK
		and coordinator.publish_restore(registry_stale).get("code") == "stale_revision",
		"A newly registered validator invalidates a previously checked candidate.")
	var candidate: CrimsonTransaction = coordinator.prepare_restore(snapshot).get("candidate")
	_expect(candidate != null and candidate.add_change("a", {"add": 99}, 0) != OK
		and coordinator.commit(candidate).get("code") == "invalid_transaction", "A restore candidate cannot be edited or used as a transaction.")
	var foreign := CrimsonTransactionCoordinator.new()
	_expect(foreign.publish_restore(candidate).get("code") == "invalid_restore", "Only the preparing coordinator can publish the candidate.")
	snapshot["a"]["count"] = 900
	_expect(coordinator.read_store("a")["count"] == 2, "Captured snapshots cannot mutate live stores.")

	var events: Array = []
	# Observe which revision and stores become visible at the transition boundary.
	coordinator.committed.connect(func(revision: int, stores: PackedStringArray) -> void:
		events.append([revision, stores]))
	var result := coordinator.publish_restore(candidate, true)
	_expect(_ok(result) and result.get("revision") == 2 and coordinator.read_store("a")["count"] == 2
		and coordinator.read_store("b")["count"] == 3, "Publish the detached candidate at one new revision for scene mounting.")
	_expect(events.is_empty() and coordinator.get_store_revision("a") == 1 and coordinator.get_store_revision("b") == 2
		and coordinator.begin() == null and coordinator.capture_snapshot().get("code") == "busy",
		"Defer observers and keep checkpoint capture and mutations gated through activation.")
	_expect(coordinator.notify_restore_published(1) != OK and events.is_empty(), "Reject a release for a different revision.")
	_expect(coordinator.notify_restore_published(2) == OK and events == [[2, PackedStringArray(["a", "b"])]]
		and coordinator.begin() != null and _ok(coordinator.capture_snapshot()), "Release the gate and announce all restored stores once.")
	_expect(coordinator.notify_restore_published(2) != OK and coordinator.publish_restore(candidate).get("code") == "consumed_transaction",
		"A restore and its notification can each complete only once.")


## Rejects malformed saved counters while leaving the candidate owned by the coordinator.
func _check_checkpoint_counter(candidate: Dictionary, _context: Dictionary) -> Dictionary:
	return {"ok": candidate.size() == 1 and candidate.get("count") is int and candidate["count"] >= 0}


## Checks the saved counter schema before applying optional counter updates.
func _prepare_checkpoint_counter(current: Dictionary, operations: Array, context: Dictionary) -> Dictionary:
	return _prepare_b(current, operations, context) if _ok(_check_checkpoint_counter(current, context)) else {"ok": false}


## Requires the first counter to stay below the second whenever both are prepared.
func _check_checkpoint_order(prepared: Dictionary, _context: Dictionary) -> Dictionary:
	return {"ok": not prepared.has("a") or not prepared.has("b") or prepared["a"]["count"] < prepared["b"]["count"]}


## Checks atomic pickups using the native grid and a world pickup store.
func _test_inventory_pickup() -> void:
	var coordinator := CrimsonTransactionCoordinator.new()
	var inventory := SpatialInventory.new()
	var adapter := CrimsonInventoryTransactionAdapter.new()
	_expect(inventory.configure(Vector2i(1, 1)) == OK, "Create a one-cell native inventory.")
	_expect(adapter.configure(coordinator, "bag", inventory.export_snapshot()) == OK, "Register the native inventory transaction adapter.")
	_expect(coordinator.register_store("world", {"sword": true, "potion": true}, _prepare_pickup) == OK, "Register the world pickup store.")
	var observed: Array[Dictionary] = []
	# Read the world and inventory from the same commit notification.
	var listener := func(revision: int, _stores: PackedStringArray) -> void:
		observed.append({"revision": revision, "world": coordinator.read_store("world"), "bag": adapter.get_snapshot()})
	coordinator.committed.connect(listener)
	var pick_up := coordinator.begin()
	_expect(pick_up.add_change("world", {"id": "sword"}, 0) == OK
		and adapter.queue_place(pick_up, "sword", Vector2i.ONE, Vector2i.ZERO) == OK, "Stage the pickup and the native grid insertion together.")
	_expect(_ok(coordinator.commit(pick_up)), "Commit a pickup into an empty native inventory.")
	_expect(not coordinator.read_store("world")["sword"] and _inventory_has(adapter.get_snapshot(), "sword"),
		"The item leaves the world and appears in the inventory.")

	var blocked := coordinator.begin()
	_expect(blocked.add_change("world", {"id": "potion"}, 1) == OK
		and adapter.queue_place(blocked, "potion", Vector2i.ONE, Vector2i.ZERO) == OK, "Stage a pickup into an occupied cell.")
	_expect(not _ok(coordinator.commit(blocked)), "Native placement rejection aborts the pickup.")
	_expect(coordinator.read_store("world")["potion"] and _inventory_has(adapter.get_snapshot(), "sword")
		and not _inventory_has(adapter.get_snapshot(), "potion") and coordinator.get_revision() == 1,
		"The failed pickup preserves the world item, existing placement, and revision.")
	_expect(observed.size() == 1, "The pickup emits one event and the rejected attempt emits none.")
	if observed.size() == 1:
		_expect(observed[0]["revision"] == 1 and not observed[0]["world"]["sword"] and _inventory_has(observed[0]["bag"], "sword"),
			"The pickup observer sees both changes at the same revision.")
	coordinator.committed.disconnect(listener)


## Stages removal of each requested pickup and rejects already consumed items.
func _prepare_pickup(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
	for operation: Dictionary in operations:
		var id: String = operation["id"]
		if not current.get(id, false):
			return {"ok": false, "message": "The pickup was already consumed."}
		current[id] = false
	return {"ok": true, "state": current}


## Uses a detached native grid to check whether a snapshot contains an item.
func _inventory_has(snapshot: Dictionary, id: String) -> bool:
	var grid := SpatialInventory.new()
	return grid.import_snapshot(snapshot) == OK and not grid.get_placement(StringName(id)).is_empty()


## Exercises primary-character ownership and routes commands through the authority API.
func _test_possession_and_commands() -> void:
	var coordinator := CrimsonTransactionCoordinator.new()
	_expect(coordinator.register_store("players", {"alice": {"team": "red"}, "bob": {"team": "blue"}}, _keep_state) == OK,
		"Register stable player records.")
	_expect(coordinator.register_store("characters", {"hero": {"health": 30}, "summon": {"health": 10}}, _prepare_characters) == OK,
		"Register independent character records.")
	var possession := CrimsonPossession.new()
	_expect(possession.configure(coordinator, "players", "characters") == OK, "Configure possession over match records.")
	var starting_stores: Dictionary = coordinator.capture_snapshot()["stores"]
	var changes: Array = []
	# Record committed assignments to check both order and cardinality.
	possession.primary_changed.connect(func(player: String, old_character: String, new_character: String, revision: int) -> void:
		changes.append([player, old_character, new_character, revision]))
	_expect(possession.get_primary("alice").is_empty() and possession.get_revision("alice") == 0, "Players start without a primary character.")
	_expect(_ok(possession.try_set_primary("alice", "hero", 0)), "Assign a primary character.")
	_expect(possession.get_primary("alice") == "hero" and possession.get_controller("hero") == "alice",
		"Both directions of the assignment agree.")
	var malformed: Dictionary = coordinator.capture_snapshot()["stores"]
	malformed["possession"]["alice"]["revision"] = 1.0
	_expect(coordinator.prepare_restore(malformed).get("code") == "validation_failed" and possession.get_primary("alice") == "hero",
		"Reject a saved assignment with an invalid revision type without changing possession.")
	malformed["possession"]["alice"]["revision"] = 1
	malformed["characters"].erase("hero")
	_expect(coordinator.prepare_restore(malformed).get("code") == "validation_failed", "Reject a saved assignment whose character is missing.")
	_expect(not _ok(possession.try_set_primary("bob", "hero", 0)) and possession.get_primary("bob").is_empty(),
		"A second player cannot claim an occupied primary character.")
	_expect(not _ok(possession.try_set_primary("alice", "summon", 0)) and possession.get_primary("alice") == "hero",
		"Reject a switch based on the previous possession revision.")

	_router = CrimsonCommandRouter.new()
	_expect(_router.configure("match-1", 4, possession) == OK, "Configure the local authority command router.")
	_expect(_router.register_handler("move", _handle_command, true) == OK
		and _router.register_handler("ready", _handle_command) == OK, "Register primary-targeted and match commands.")
	var move := _intent("move", 1, "hero", possession.get_revision("alice"))
	_expect(_router.submit("alice", move).get("code") == "CommandsClosed", "The match starts with its command gate closed.")
	_expect(_router.set_accepting_commands(true) == OK, "Open the authority command gate.")
	_expect(_router.submit("bob", move).get("code") == "NoPrimary", "A player with no primary cannot issue a character command.")
	_expect(_ok(_router.submit("bob", _intent("ready", 1))), "A player with no primary can issue a match command.")
	_expect(_ok(_router.submit("alice", move)), "Route a local command for the currently possessed character.")
	_expect(_nested_command.get("code") == "Busy", "A command handler cannot dispatch recursively.")
	_expect(_router.submit("alice", move).get("code") == "Replay", "Reject replaying a dispatched sequence.")

	var delayed := _intent("move", 2, "hero", possession.get_revision("alice"))
	_expect(_ok(possession.try_set_primary("alice", "summon", possession.get_revision("alice"))), "Switch the same player to another character.")
	_expect(possession.get_controller("hero").is_empty() and possession.get_controller("summon") == "alice"
		and coordinator.read_store("characters")["hero"]["health"] == 30, "The switch releases the old character and preserves its record.")
	_expect(changes == [["alice", "", "hero", 1], ["alice", "hero", "summon", 2]],
		"Only committed assignment changes emit the possession signal.")
	_expect(_router.submit("alice", delayed).get("code") == "StalePossession", "A delayed command cannot steer the new primary character.")
	var current := _intent("move", 2, "summon", possession.get_revision("alice"))
	_expect(_ok(_router.submit("alice", current)), "A stale pre-dispatch intent leaves its sequence available.")
	var denied := _intent("move", 3, "summon", possession.get_revision("alice"), {"deny": true})
	_expect(not _ok(_router.submit("alice", denied)) and _router.submit("alice", denied).get("code") == "Replay",
		"A dispatched command rejection still consumes the sequence.")
	_expect(_router.advance_world(5) == OK and _router.set_accepting_commands(true) == OK, "Advance to a new world and reopen its command gate.")
	_expect(_router.submit("alice", _intent("move", 4, "summon", possession.get_revision("alice"))).get("code") == "StaleWorld",
		"Commands for the previous world are rejected after travel.")
	_expect(_ok(_router.submit("alice", _intent("move", 4, "summon", possession.get_revision("alice"), {}, 5))),
		"The current world accepts valid commands.")
	_expect(_commands.size() == 5, "Exactly the valid, dispatched commands reached gameplay.")
	_router = null
	_test_possessed_character_despawn(coordinator, possession)
	_expect(_ok(possession.try_set_primary("alice", "hero", possession.get_revision("alice"))), "Assign a character before restoring earlier possession state.")
	var earlier: CrimsonTransaction = coordinator.prepare_restore(starting_stores).get("candidate")
	var change_count := changes.size()
	var restore := coordinator.publish_restore(earlier, true)
	_expect(_ok(restore) and changes.size() == change_count and possession.get_primary("alice").is_empty(),
		"The restored scene sees no assignment before possession observers are notified.")
	_expect(coordinator.notify_restore_published(restore["revision"]) == OK and changes.size() == change_count + 1
		and changes[-1] == ["alice", "hero", "", 0], "A restored snapshot that omits an assignment still notifies its removal.")


## Checks that removing a possessed character requires releasing it in the same commit.
func _test_possessed_character_despawn(coordinator: CrimsonTransactionCoordinator, possession: CrimsonPossession) -> void:
	var revision := coordinator.get_revision()
	var unsafe := coordinator.begin()
	_expect(unsafe.add_change("characters", {"despawn": "summon"}, 0) == OK, "Stage a despawn for an actively possessed character.")
	_expect(coordinator.commit(unsafe).get("code") == "validation_failed", "Reject a despawn that would leave a dangling possession.")
	_expect(coordinator.get_revision() == revision and coordinator.read_store("characters").has("summon")
		and possession.get_primary("alice") == "summon", "The rejected despawn preserves the character and its assignment.")
	var snapshots: Array = []
	# Read the removed character and its former controller at one published revision.
	var listener := func(committed_revision: int, _stores: PackedStringArray) -> void:
		snapshots.append([committed_revision, coordinator.read_store("characters").has("summon"), possession.get_primary("alice")])
	coordinator.committed.connect(listener)
	var released := coordinator.begin()
	_expect(released.add_change("characters", {"despawn": "summon"}, 0) == OK
		and possession.queue_primary_change(released, "alice", "", possession.get_revision("alice")) == OK,
		"Stage release and despawn in one transaction.")
	_expect(_ok(coordinator.commit(released)), "Publish the release and despawn together.")
	_expect(snapshots == [[revision + 1, false, ""]] and possession.get_controller("summon").is_empty(),
		"Observers see the removed character and released assignment together.")
	coordinator.committed.disconnect(listener)


## Keeps the stable player registry unchanged during the fixture's commands.
func _keep_state(current: Dictionary, _operations: Array, _context: Dictionary) -> Dictionary:
	return {"ok": true, "state": current}


## Stages character removals and rejects an unknown character ID.
func _prepare_characters(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
	for operation: Dictionary in operations:
		if not current.erase(operation["despawn"]):
			return {"ok": false, "message": "The character does not exist."}
	return {"ok": true, "state": current}


## Builds a command envelope and includes possession data for a targeted command.
func _intent(type: String, sequence: int, character: String = "", revision: int = 0,
		payload: Dictionary = {}, world: int = 4) -> Dictionary:
	var intent := {"type": type, "sequence": sequence, "match_id": "match-1", "world_epoch": world, "payload": payload}
	if not character.is_empty():
		intent["character_id"] = character
		intent["possession_revision"] = revision
	return intent


## Records dispatch, probes recursive routing, and applies the fixture's denial flag.
func _handle_command(player_id: String, intent: Dictionary) -> Dictionary:
	_commands.append({"player": player_id, "intent": intent})
	_nested_command = _router.submit(player_id, intent)
	return {"ok": not intent["payload"].get("deny", false)}


## Checks unique peer and player bindings and guards replacements from stale disconnects.
func _test_player_binding_generations() -> void:
	var bindings := CrimsonPlayerBindings.new()
	var original := bindings.bind_peer(7, "alice")
	_expect(original > 0 and bindings.get_player(7) == "alice" and bindings.get_binding_generation(7) == original,
		"Bind a peer to its stable player ID and expose the binding generation.")
	_expect(bindings.bind_peer(7, "bob") == 0, "Reject binding a peer that already belongs to a player.")
	_expect(bindings.bind_peer(8, "alice") == 0, "Reject binding a player who already has a peer.")
	_expect(not bindings.unbind_peer(7, original + 1) and bindings.get_player(7) == "alice",
		"An incorrect generation cannot remove the current binding.")
	_expect(bindings.unbind_peer(7, original) and bindings.get_player(7).is_empty() and bindings.get_binding_generation(7) == 0,
		"The current generation can remove a binding.")

	var replacement := bindings.bind_peer(7, "alice")
	_expect(replacement > original and bindings.get_binding_generation(7) == replacement,
		"Reusing a peer ID assigns a newer binding generation.")
	_expect(not bindings.unbind_peer(7, original) and bindings.get_player(7) == "alice"
		and bindings.get_binding_generation(7) == replacement, "A stale disconnect cannot remove the replacement binding.")


## Reads the success flag from a coordinator or router result.
func _ok(result: Dictionary) -> bool:
	return result.get("ok", false) == true


## Counts an assertion and reports its description on failure.
func _expect(condition: bool, description: String) -> void:
	_checks += 1
	if not condition:
		_failures += 1
		push_error(description)
