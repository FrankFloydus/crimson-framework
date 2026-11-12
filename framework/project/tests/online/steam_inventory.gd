# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises typed Inventory reads and explicitly selected actions on a configured test app.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480
## Exact write actions accepted by the authorized test-app lane.
const WRITE_ACTIONS := ["claim_promotions", "claim_promotion", "claim_playtime_drop",
	"craft", "reject_craft", "consume", "split", "merge"]
## Environment variable prefix for one explicitly configured test action.
const TEST_SETTING_PREFIX := "CRIMSON_STEAM_INVENTORY_TEST_"

## Number of failed assertions in this run.
var _failures := 0
## Outcome captured while a sibling client scope is stopping.
var _retired_result: SteamInventorySnapshotResult


## Defer until the root can host Steam client nodes.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the fixture's detached, signed Spacewar, or unavailable runtime path.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or args[0] not in ["spacewar", "app", "missing", "sdk-off"]:
		push_error("Choose the Steam inventory fixture mode spacewar, app, missing, or sdk-off.")
		quit(2)
		return

	var mode := args[0]
	var expected_app_id := SPACEWAR
	var action := ""
	if mode == "app":
		expected_app_id = _read_positive(OS.get_environment("CRIMSON_STEAM_INVENTORY_APP_ID"), 4294967295)
		action = OS.get_environment(TEST_SETTING_PREFIX + "ACTION")
		if expected_app_id == 0 or expected_app_id == SPACEWAR:
			push_error("Inventory app mode requires a valid non-Spacewar CRIMSON_STEAM_INVENTORY_APP_ID.")
			quit(2)
			return
		if not action.is_empty() and (action not in WRITE_ACTIONS or
				OS.get_environment("CRIMSON_STEAM_INVENTORY_ALLOW_WRITES") != "1"):
			push_error("Inventory mutation requires one supported test action and explicit write opt-in.")
			quit(2)
			return
	var detached := SteamInventory.new()
	var detached_items: SteamInventorySnapshotResult = await detached.items()
	_check(detached_items.error == ERR_UNAVAILABLE and detached_items.value == null,
		"Detached inventory reads return a typed unavailable result.")
	var detached_catalog: SteamItemDefinitionsResult = await detached.definitions()
	_check(detached_catalog.error == ERR_UNAVAILABLE and detached_catalog.value.is_empty(),
		"Detached catalog reads return a typed empty error payload.")
	var detached_promo: SteamInventoryChangeResult = await detached.claim_promotions()
	_check(detached_promo.error == ERR_UNAVAILABLE and detached_promo.value == null,
		"Detached mutations return unavailable before any Steam action.")
	await _check_validation(detached)

	var client := SteamClient.new()
	root.add_child(client)
	var started := client.start(expected_app_id)
	match mode:
		"spacewar":
			if started == OK:
				await _check_live(client)
			else:
				_check(false, "Spacewar initialization failed. %s" % client.get_initialization_error())
		"app":
			if started == OK and client.get_app_id() == expected_app_id:
				var snapshot := await _check_live(client, true)
				if _failures == 0 and snapshot != null and not action.is_empty():
					await _check_authorized_action(client.inventory(), snapshot, action)
			else:
				_check(false, "The authorized Inventory AppID did not start.")
		"missing":
			_check(started == ERR_CANT_CONNECT and client.inventory() == null,
				"A missing Steam runtime leaves inventory detached.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and client.inventory() == null,
				"An SDK-free build exposes the typed API and has no active inventory service.")

	client.stop()
	client.free()
	if _failures == 0:
		print("CRIMSON_STEAM_INVENTORY_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Verify invalid arguments without dispatching any mutation, even on a signed client.
func _check_validation(inventory: SteamInventory) -> void:
	var invalid_definition: SteamItemDefinitionResult = await inventory.definition(0)
	_check(invalid_definition.error == ERR_INVALID_PARAMETER, "A definition ID must be positive.")
	var invalid_selection: SteamInventorySnapshotResult = await inventory.items_by_id(PackedStringArray(["-1"]))
	_check(invalid_selection.error == ERR_INVALID_PARAMETER, "An instance ID uses unsigned decimal.")
	var invalid_proof: SteamBytesResult = await inventory.proof(PackedStringArray(["42", "42"]))
	_check(invalid_proof.error == ERR_INVALID_PARAMETER and invalid_proof.value.is_empty(),
		"Proof selection rejects duplicates.")
	var invalid_promotion: SteamInventoryChangeResult = await inventory.claim_promotion(1000000000)
	var invalid_drop: SteamInventoryChangeResult = await inventory.claim_playtime_drop(-1)
	var invalid_consume: SteamInventoryChangeResult = await inventory.consume("42", 0)
	var invalid_split: SteamInventoryChangeResult = await inventory.split("18446744073709551615", 1)
	var invalid_merge: SteamInventoryChangeResult = await inventory.merge("42", "42", 1)
	var input := SteamItemQuantity.make("42", 1)
	_check(input.instance_id == "42" and input.quantity == 1, "Craft inputs preserve the typed instance and quantity.")
	var ingredients: Array[SteamItemQuantity] = [input, input]
	var invalid_craft: SteamInventoryChangeResult = await inventory.craft(1, ingredients)
	_check(invalid_promotion.error == ERR_INVALID_PARAMETER and invalid_drop.error == ERR_INVALID_PARAMETER and
		invalid_consume.error == ERR_INVALID_PARAMETER and invalid_split.error == ERR_INVALID_PARAMETER and
		invalid_merge.error == ERR_INVALID_PARAMETER and invalid_craft.error == ERR_INVALID_PARAMETER,
		"Invalid mutation inputs settle locally before Steam dispatch.")


## Read a catalog, inventory snapshot, and a proof for one existing item when available.
func _check_live(client: SteamClient, require_configured_inventory: bool = false) -> SteamInventorySnapshot:
	var inventory := client.inventory()
	if inventory == null:
		_check(false, "The signed client exposes inventory.")
		return null
	await _check_validation(inventory)

	var catalog: SteamItemDefinitionsResult = await inventory.definitions()
	if catalog.ok:
		_check(catalog.value.is_read_only(), "Catalog snapshots are read-only.")
		for item in catalog.value:
			_check(item.definition_id > 0 and item.properties.is_read_only(),
				"Definitions expose positive IDs and read-only properties.")
		if not catalog.value.is_empty():
			var item := catalog.value[0]
			var matching: SteamItemDefinitionResult = await inventory.definition(item.definition_id)
			_check(matching.ok and matching.value.definition_id == item.definition_id,
				"Definition lookup resolves the selected published ID.")
		print("CRIMSON_STEAM_INVENTORY_DEFINITIONS count=%d" % catalog.value.size())
	else:
		_check(not require_configured_inventory and catalog.error in [ERR_UNAVAILABLE, ERR_TIMEOUT],
			"The catalog returns a supported availability error.")
		print("CRIMSON_STEAM_INVENTORY_DEFINITIONS_UNAVAILABLE error=%d" % catalog.error)

	var items: SteamInventorySnapshotResult = await inventory.items()
	if items.ok:
		var snapshot := items.value
		_check(snapshot.items.is_read_only() and snapshot.observed_at_unix >= 0,
			"Inventory snapshots contain read-only rows and a valid timestamp.")
		for item in snapshot.items:
			_check(item.quantity > 0 and item.definition_id > 0 and not item.instance_id.is_empty(),
				"An inventory row describes a present stack and opaque instance ID.")
		print("CRIMSON_STEAM_INVENTORY_ITEMS count=%d freshness=%d" % [snapshot.items.size(), snapshot.freshness])
		if not snapshot.items.is_empty():
			await _check_selection(inventory, snapshot.items[0])
	else:
		_check(not require_configured_inventory and items.error in [ERR_UNAVAILABLE, ERR_UNAUTHORIZED],
			"Inventory reads return a supported availability error.")
		print("CRIMSON_STEAM_INVENTORY_ITEMS_UNAVAILABLE error=%d" % items.error)

	await _check_retirement(client)
	return items.value


## Dispatch only the selected test-app action and verify the response without logging item identifiers.
func _check_authorized_action(inventory: SteamInventory, snapshot: SteamInventorySnapshot, action: String) -> void:
	var definition_id := _read_positive(OS.get_environment(TEST_SETTING_PREFIX + "DEFINITION_ID"), 999999999)
	if action in ["claim_promotion", "claim_playtime_drop", "craft", "reject_craft"] and definition_id == 0:
		_check(false, "The selected test action requires a valid published test definition ID.")
		return
	var result: SteamInventoryChangeResult
	match action:
		"claim_promotions":
			result = await inventory.claim_promotions()
		"claim_promotion":
			result = await inventory.claim_promotion(definition_id)
		"claim_playtime_drop":
			result = await inventory.claim_playtime_drop(definition_id)
		"craft", "reject_craft":
			await _check_authorized_craft(inventory, snapshot, definition_id, action == "reject_craft")
			return
		"consume", "split", "merge":
			await _check_authorized_stack_action(inventory, snapshot, action)
			return
	_log_change(result, action)


## Verify that a configured recipe changes the exact owned ingredients, or that a known-invalid one changes none.
func _check_authorized_craft(inventory: SteamInventory, snapshot: SteamInventorySnapshot,
		definition_id: int, expect_rejection: bool) -> void:
	var configured := OS.get_environment(TEST_SETTING_PREFIX + "INGREDIENTS")
	var ingredients: Array[SteamItemQuantity] = []
	var before: Dictionary[String, int] = {}
	var selected := PackedStringArray()
	for entry in configured.split(",", false):
		var fields := entry.split(":")
		if fields.size() != 2:
			_check(false, "Use comma-separated instance:quantity test ingredients.")
			return
		var id := fields[0]
		var quantity := _read_positive(fields[1], 4294967295)
		var item := _find_stack(snapshot, id)
		if before.has(id) or quantity == 0 or item == null or item.quantity < quantity:
			_check(false, "Each test ingredient must name a distinct owned stack with sufficient quantity.")
			return
		before[id] = item.quantity
		selected.append(id)
		ingredients.append(SteamItemQuantity.make(id, quantity))
	if ingredients.is_empty() or ingredients.size() > 256:
		_check(false, "The recipe test requires one to 256 ingredients.")
		return
	var result: SteamInventoryChangeResult = await inventory.craft(definition_id, ingredients)
	if expect_rejection:
		_check(not result.ok and result.error in [FAILED, ERR_INVALID_PARAMETER, ERR_DOES_NOT_EXIST],
			"Steam rejected the known-invalid test recipe.")
		var observed: SteamInventorySnapshotResult = await inventory.items_by_id(selected)
		_check(observed.ok and observed.value.items.size() == selected.size(),
			"The rejected exchange left all selected stacks present.")
		if observed.ok:
			for item in observed.value.items:
				_check(before.has(item.instance_id) and before[item.instance_id] == item.quantity,
					"The rejected exchange left the selected stack quantity unchanged.")
		print("CRIMSON_STEAM_INVENTORY_ACTION reject_craft")
		return
	if not _log_change(result, "craft"):
		return
	var output_count := 0
	for item in result.value.granted:
		if item.definition_id == definition_id:
			output_count += item.quantity
	_check(output_count == 1, "The test exchange granted exactly one unit of its configured output.")
	for ingredient in ingredients:
		_check(_confirmed_quantity(result.value, ingredient.instance_id) ==
			before[ingredient.instance_id] - ingredient.quantity, "The test exchange consumed the exact input quantity.")


## Verify consume, split, or merge against the fixture's explicitly selected owned stacks.
func _check_authorized_stack_action(inventory: SteamInventory, snapshot: SteamInventorySnapshot, action: String) -> void:
	var source := _find_stack(snapshot, OS.get_environment(TEST_SETTING_PREFIX + "FROM_INSTANCE_ID"))
	var quantity := _read_positive(OS.get_environment(TEST_SETTING_PREFIX + "QUANTITY"), 4294967295)
	if source == null or quantity == 0 or source.quantity < quantity:
		_check(false, "The test action requires an owned source stack with the requested quantity.")
		return
	var destination: SteamItemInstance
	if action == "merge":
		destination = _find_stack(snapshot, OS.get_environment(TEST_SETTING_PREFIX + "INTO_INSTANCE_ID"))
		if (destination == null or source.instance_id == destination.instance_id or
				source.definition_id != destination.definition_id):
			_check(false, "The test merge requires distinct owned stacks with matching definitions.")
			return
	var result: SteamInventoryChangeResult
	match action:
		"consume":
			result = await inventory.consume(source.instance_id, quantity)
		"split":
			result = await inventory.split(source.instance_id, quantity)
		"merge":
			result = await inventory.merge(source.instance_id, destination.instance_id, quantity)
	if not _log_change(result, action):
		return
	_check(_confirmed_quantity(result.value, source.instance_id) == source.quantity - quantity,
		"Steam reported the exact requested source quantity decrease.")
	if action == "split":
		_check(result.value.granted.size() == 1 and result.value.granted[0].definition_id == source.definition_id and
			result.value.granted[0].quantity == quantity, "Steam granted one matching split stack of the requested size.")
	if action == "merge":
		_check(_confirmed_quantity(result.value, destination.instance_id) == destination.quantity + quantity,
			"Steam reported the exact requested destination quantity increase.")


## Verify typed change arrays and report only the action and aggregate counts.
func _log_change(result: SteamInventoryChangeResult, action: String) -> bool:
	_check(result != null and result.ok and result.value != null,
		"Steam accepted the explicitly selected test inventory action.")
	if result == null or not result.ok or result.value == null:
		return false
	_check(result.value.granted.is_read_only() and result.value.changed.is_read_only(),
		"Steam inventory changes are read-only.")
	print("CRIMSON_STEAM_INVENTORY_ACTION %s granted=%d changed=%d removed=%d" %
		[action, result.value.granted.size(), result.value.changed.size(), result.value.removed_instance_ids.size()])
	return true


## Locate a stack in the read-only baseline by its exact observed decimal ID.
func _find_stack(snapshot: SteamInventorySnapshot, id: String) -> SteamItemInstance:
	for item in snapshot.items:
		if item.instance_id == id:
			return item
	return null


## Return a confirmed quantity from mutation rows, or -1 if Steam did not report it.
func _confirmed_quantity(change: SteamInventoryChange, id: String) -> int:
	if change.removed_instance_ids.has(id):
		return 0
	for item in change.changed:
		if item.instance_id == id:
			return item.quantity
	return -1


## Parse a bounded canonical positive decimal setting.
func _read_positive(text: String, maximum: int) -> int:
	if not text.is_valid_int():
		return 0
	var value := text.to_int()
	return value if value > 0 and value <= maximum and str(value) == text else 0


## Verify selected item reads and signed proof generation without logging the selected ID or proof.
func _check_selection(inventory: SteamInventory, item: SteamItemInstance) -> void:
	var selection := PackedStringArray([item.instance_id])
	var selected: SteamInventorySnapshotResult = await inventory.items_by_id(selection)
	_check(selected.ok, "Steam can read the selected observed instance.")
	if not selected.ok:
		return
	for row in selected.value.items:
		_check(row.instance_id == item.instance_id, "Selected reads expose only the requested instance.")
	var signed_result: SteamBytesResult = await inventory.proof(selection)
	if selected.value.items.is_empty():
		_check(signed_result.error == ERR_DOES_NOT_EXIST, "Missing instances cannot produce a proof.")
	else:
		_check(signed_result.ok and not signed_result.value.is_empty(),
			"The observed owned selection produces nonempty signed Steam proof bytes.")
		if signed_result.ok:
			print("CRIMSON_STEAM_INVENTORY_PROOF bytes=%d" % signed_result.value.size())


## Verify that stopping a sibling scope settles its in-flight read and retires only its service.
func _check_retirement(client: SteamClient) -> void:
	var sibling := SteamClient.new()
	root.add_child(sibling)
	if sibling.start() != OK:
		_check(false, "A sibling Steam client scope can share the signed runtime.")
		sibling.free()
		return
	var retired := sibling.inventory()
	_observe_retirement(retired)
	sibling.stop()
	await process_frame
	_check(_retired_result != null and _retired_result.error == ERR_UNAVAILABLE,
		"Scope stop settles and retires an owned pending inventory request.")
	var read_after_stop: SteamInventorySnapshotResult = await retired.items()
	_check(read_after_stop.error == ERR_UNAVAILABLE and client.is_ready(),
		"The retained service remains detached while the original client remains active.")
	sibling.free()


## Begin observing the read before retirement synchronously settles its signal.
func _observe_retirement(inventory: SteamInventory) -> void:
	_retired_result = await inventory.items()


## Report assertion failures without logging private SDK payloads.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
