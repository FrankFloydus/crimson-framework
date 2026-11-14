# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree
## Exercises code-first pickup through a real coordinated host and guest.

const ADDRESS := "127.0.0.1"
const DONE := "inventory.fixture.done"
const TIMEOUT := 10000

## Provides one authoritative pickup store and verifies the resolved player.
class PickupSource extends InventoryPickupSource:
	var player_ids: Array[String] = []

	## Register the fresh authority source records.
	func _register_stores(stores: TransactionCoordinator) -> Error:
		return stores.register_store("network_pickups", {"race": true, "medkit": true, "potion": true}, _consume)

	## Expose the contested item to both players and the other items to the guest.
	func _resolve_pickup(player_id: String, pickup_id: StringName, stores: TransactionCoordinator) -> InventoryPickup:
		player_ids.append(player_id)
		var id := String(pickup_id)
		if (player_id != "guest" and id != "race") or stores.read_store("network_pickups").get(id, false) != true:
			return null
		return InventoryPickup.take(pickup_id, Vector2i.ONE, "network_pickups", {"id": id})

	## Stage removal only if the requested source still exists.
	func _consume(state: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
		for edit: Dictionary in operations:
			var id: String = edit.id
			if not state.get(id, false):
				return {"ok": false, "message": "The pickup was already consumed."}
			state[id] = false
		return {"ok": true, "state": state}

## Code-first game owned by the same deterministic scene path in both processes.
var _game: Game
## Source instance observed by the authority.
var _source := PickupSource.new()
## Admitted guest's trusted peer ID, assigned by the authority.
var _guest_peer := 0
## True after the guest received all command results.
var _guest_finished := false


## Defer the fixture until the tree is ready for a persistent owner.
func _initialize() -> void:
	_run.call_deferred()


## Start the selected fixture role and report a process result.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2 or arguments[0] not in ["host", "client"] or not arguments[1].is_valid_int():
		push_error("Usage: inventory_network.gd -- host|client port")
		quit(1)
		return
	create_timer(16).timeout.connect(_timed_out)
	var owner := Node.new()
	owner.name = &"InventoryNetworkHarness"
	root.add_child(owner)
	var world := WorldDefinition.at(&"pickup", "res://tests/match_runtime/inventory_world.tscn")
	var settings := InventoryConfig.backpack(Vector2i.ONE).pickups(_source)
	_game = Game.campaign(world).lan(2).on_port(arguments[1].to_int()).inventory(settings).boot(owner)
	if not _check(_game.boot_result().ok and _game.register_command(DONE, _mark_done) == OK,
		"Boot a coordinated inventory game and register the fixture completion command."):
		quit(1)
		return
	var passed: bool
	if arguments[0] == "host":
		passed = await _host()
	else:
		passed = await _join()
	await _game.leave()
	if passed:
		print("CRIMSON_INVENTORY_NETWORK_PASSED")
	quit(0 if passed else 1)


## Open the lobby, bind both actors, then verify the guest's committed pickup.
func _host() -> bool:
	_game.peer_joined.connect(_bind_guest)
	var opened := _game.open_lobby(ADDRESS)
	if not _check(opened.ok and _game.bind_player(1, "host") == OK, "Open the lobby and bind the host."):
		return false
	print("CRIMSON_INVENTORY_HOST_READY")
	if not await _wait_for(func() -> bool: return _guest_peer > 1):
		return false
	var started: GameResult = await _game.start_match()
	if not _check(started.ok, "Start the coordinated pickup world."):
		return false
	var host_bag := _game.inventory().backpack()
	if not _check(host_bag != null, "Resolve the host backpack for the contested pickup."):
		return false
	var raced: InventoryResult = await host_bag.pick_up(&"race")
	if not await _wait_for(func() -> bool: return _guest_finished):
		return false
	var stores := _game.transactions()
	var bags := stores.read_store("crimson.inventory")
	var sources := stores.read_store("network_pickups")
	var passed := _check(sources == {"race": false, "medkit": not raced.ok, "potion": true}
		and stores.get_revision() == (2 if raced.ok else 1)
		and bags.has("guest") and bags.has("host") == raced.ok
		and _source.player_ids.count("host") == 1 and _source.player_ids.count("guest") == (4 if raced.ok else 3),
		"Commit the contested pickup once, preserve the full-bag source, and use the trusted identity.")
	await create_timer(0.25).timeout
	return passed


## Join, resolve a guest backpack, and await typed results from the actual authority.
func _join() -> bool:
	var joined := _game.join_lobby(ADDRESS)
	if not _check(joined.ok, "Request a coordinated lobby join."):
		return false
	if not await _wait_for(func() -> bool: return _game.get_phase() == Game.RUNNING):
		return false
	var bag := _game.inventory().backpack()
	if not _check(bag != null and _game.transactions() == null, "Resolve the guest backpack without an authority coordinator."):
		return false
	var raced: InventoryResult = await bag.pick_up(&"race")
	var taken := raced
	if not raced.ok:
		taken = await bag.pick_up(&"medkit")
	var expected := &"race" if raced.ok else &"medkit"
	var duplicate: InventoryResult = await bag.pick_up(expected)
	var full: InventoryResult = await bag.pick_up(&"potion")
	var forged: GameResult = await _game.perform("crimson.inventory.pickup", {"pickup_id": "potion", "player_id": "host"})
	var passed := _check(taken.ok and taken.item_id == expected and taken.origin == Vector2i.ZERO
		and taken.revision == (1 if raced.ok else 2) and duplicate.code == "PickupDenied" and full.code == "InventoryFull"
		and forged.code == "InvalidPickup",
		"Receive typed wire results and reject a payload that tries to redirect the placement.")
	var done: GameResult = await _game.perform(DONE)
	return passed and _check(done.ok, "Acknowledge that the guest observed its command receipts.")


## Assign the one allowed admitted peer its durable game identity.
func _bind_guest(peer_id: int) -> void:
	if _game.bind_player(peer_id, "guest") == OK:
		_guest_peer = peer_id


## Record fixture completion only from the trusted guest.
func _mark_done(player_id: String, _intent: Dictionary) -> Dictionary:
	if player_id != "guest":
		return {"ok": false, "code": "WrongPlayer"}
	_guest_finished = true
	return {"ok": true}


## Wait for network state with a bounded deadline.
func _wait_for(condition: Callable) -> bool:
	var until := Time.get_ticks_msec() + TIMEOUT
	while not condition.call() and Time.get_ticks_msec() < until:
		await process_frame
	return _check(condition.call(), "Observe the expected network state before the deadline.")


## Print the failing expectation and return it to the fixture.
func _check(passed: bool, message: String) -> bool:
	if not passed:
		push_error(message)
	return passed


## Stop if an awaited native operation cannot settle.
func _timed_out() -> void:
	push_error("Coordinated network pickup did not complete.")
	quit(1)
