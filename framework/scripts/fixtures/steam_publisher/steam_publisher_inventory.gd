# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies publisher Inventory routes and typed outcomes with the no-socket fixture transport.
extends SceneTree

## Synthetic app, verified account, item, and idempotency values.
const APP_ID := 12345
const PLAYER_ID := "76561198000000002"
const PARTNER_ID := "76561198000000003"
const ITEM_ID := "18446744073709551500"
const REQUEST_ID := "18446744073709551601"
const CONSUME_REQUEST_ID := "18446744073709551602"

## Number of failed assertions in the process.
var _failures := 0


## Captures fixed requests and returns local fixture data without network access.
class FixtureHost extends SteamPublisherHost:
	## Number of requests the fake transport accepted.
	var sent := 0
	## HTTP method passed to the fixed route.
	var method := ""
	## Fixed route and encoded query string.
	var target := ""
	## Encoded POST form supplied to the transport.
	var form := ""
	## Local synthetic transport response.
	var next_reply: Dictionary

	## Resolve only the synthetic fixture secret reference.
	func _resolve_secret(reference: StringName) -> String:
		return "fixture-token" if reference == &"fixture_only" else ""

	## Capture the request after trusted validation and return a local reply.
	func _test_response(verb: String, _host: String, path: String, body: String, _authorized: bool) -> Dictionary:
		sent += 1
		method = verb
		target = path
		form = body
		return next_reply


## Start after the SceneTree root can own the fixture host.
func _initialize() -> void:
	_run.call_deferred()


## Run exact-issuer, nested response, idempotency, and trade tests.
func _run() -> void:
	var host := FixtureHost.new()
	if not host.has_method("_fixture_transport_enabled") or not host.call("_fixture_transport_enabled"):
		push_error("Publisher Inventory fixtures require the no-socket publisher test build.")
		host.free()
		quit(2)
		return
	root.add_child(host)
	var settings := SteamPublisherSettings.new()
	settings.app_id = APP_ID
	settings.host = host
	settings.publisher_key_ref = &"fixture_only"
	var opened := SteamPublisher.boot(settings)
	if not opened.ok:
		_check(false, "The synthetic trusted publisher starts.")
		host.free()
		quit(1)
		return
	var publisher := opened.value
	var player := await _identity(host, publisher, PLAYER_ID)
	var partner := await _identity(host, publisher, PARTNER_ID)
	if player == null or partner == null:
		_check(false, "Both synthetic identities verify.")
	else:
		await _check_items(host, publisher, settings, player)
		await _check_writes(host, publisher.inventory(), player)
		await _check_trade(host, publisher.inventory(), player, partner)
		await _check_retirement(host, publisher, player)
	publisher.close()
	host.free()
	if _failures == 0:
		print("CRIMSON_STEAM_PUBLISHER_INVENTORY_PASSED mock")
	quit(0 if _failures == 0 else 1)


## Mint an identity from an exact numeric Valve authentication fixture.
func _identity(host: FixtureHost, publisher: SteamPublisher, player_id: String) -> SteamIdentity:
	host.next_reply = _reply('{"response":{"params":{"result":"OK","steamid":%s,"appid":%d}}}' %
		[player_id, APP_ID])
	var verified: SteamIdentityResult = await publisher.auth().verify(PackedByteArray([4, 5, 6]), "fixture-audience")
	return verified.value if verified.ok else null


## Check nested item_json parsing, exact account and app validation, and read-only typed rows.
func _check_items(host: FixtureHost, publisher: SteamPublisher, settings: SteamPublisherSettings,
		player: SteamIdentity) -> void:
	var inventory := publisher.inventory()
	var detached: SteamInventorySnapshotResult = await SteamPublisherInventory.new().items(player)
	_check(detached.error == ERR_UNAVAILABLE, "A detached publisher service cannot query Inventory.")
	var sibling := SteamPublisher.boot(settings).value
	var before := host.sent
	var foreign: SteamInventorySnapshotResult = await sibling.inventory().items(player)
	_check(foreign.error == ERR_UNAUTHORIZED and host.sent == before,
		"An identity minted by another publisher cannot query Inventory.")
	sibling.close()
	var present := _item_row(ITEM_ID, PLAYER_ID, 4)
	var removed := '{"itemid":"41","state":"removed","accountid":"%s","appid":%d}' % [PLAYER_ID, APP_ID]
	host.next_reply = _items_reply("[%s,%s]" % [present, removed])
	var observed: SteamInventorySnapshotResult = await inventory.items(player)
	_check(observed.ok and observed.value.items.is_read_only() and observed.value.items.size() == 1 and
		observed.value.items[0].instance_id == ITEM_ID and observed.value.items[0].quantity == 4 and
		observed.value.freshness == SteamInventorySnapshot.FRESHNESS_UNCONFIRMED,
		"The publisher snapshot preserves unsigned item identity and omits removed stacks.")
	_check(host.method == "GET" and host.target.begins_with("/IInventoryService/GetInventory/v1/?") and
		_payload(host).contains('"steamid":' + PLAYER_ID), "Inventory reads use the fixed route and exact account.")
	host.next_reply = _items_reply("[%s]" % _item_row(ITEM_ID, PARTNER_ID, 1))
	var wrong_owner: SteamInventorySnapshotResult = await inventory.items(player)
	_check(wrong_owner.error == ERR_INVALID_DATA and wrong_owner.value == null,
		"A mismatched item owner fails the response before publication.")


## Check idempotent grant arguments, current-state replay rows, and exact consumption scope.
func _check_writes(host: FixtureHost, inventory: SteamPublisherInventory, player: SteamIdentity) -> void:
	var definitions := PackedInt32Array([42, 43])
	var before := host.sent
	var invalid: SteamInventoryChangeResult = await inventory.grant(player, definitions, "0")
	var invalid_consume: SteamInventoryChangeResult = await inventory.consume(player, ITEM_ID, 0, CONSUME_REQUEST_ID)
	_check(invalid.error == ERR_INVALID_PARAMETER and invalid_consume.error == ERR_INVALID_PARAMETER and
		host.sent == before, "Invalid request IDs and amounts fail before dispatch.")

	host.next_reply = _items_reply("[%s]" % _item_row(ITEM_ID, PLAYER_ID, 4))
	var granted: SteamInventoryChangeResult = await inventory.grant(player, definitions, REQUEST_ID)
	var original_form := host.form
	_check(granted.ok and granted.value.granted.is_read_only() and granted.value.granted.size() == 1 and
		granted.value.granted[0].instance_id == ITEM_ID and host.sent == before + 1,
		"A confirmed grant exposes the provider's actual typed item rows.")
	_check(host.method == "POST" and host.target == "/IInventoryService/AddItem/v1/" and
		_payload(host).contains('"requestid":' + REQUEST_ID) and _payload(host).contains('"itemdefid":[42,43]') and
		not host.form.contains("key="), "Grant forwards the exact unsigned request ID and definition array.")

	host.next_reply = _items_reply("[%s]" % _item_row(ITEM_ID, PLAYER_ID, 2), true)
	var replayed: SteamInventoryChangeResult = await inventory.grant(player, definitions, REQUEST_ID)
	_check(replayed.ok and replayed.value.granted[0].instance_id == ITEM_ID and
		replayed.value.granted[0].quantity == 2 and original_form == host.form and host.sent == before + 2,
		"A same-intent retry submits the same request once and observes the provider's current replay rows.")

	host.next_reply = _items_reply('[{"itemid":"%s","state":"removed","accountid":"%s","appid":%d}]' %
		[ITEM_ID, PLAYER_ID, APP_ID])
	var consumed: SteamInventoryChangeResult = await inventory.consume(player, ITEM_ID, 2, CONSUME_REQUEST_ID)
	_check(consumed.ok and consumed.value.changed.is_empty() and
		consumed.value.removed_instance_ids == PackedStringArray([ITEM_ID]) and
		host.target == "/IInventoryService/ConsumeItem/v1/" and _payload(host).contains('"quantity":"2"') and
		_payload(host).contains('"requestid":' + CONSUME_REQUEST_ID),
		"Consumption sends Valve's string quantity and exact request ID and reports the removed stack.")
	host.next_reply = _items_reply("[%s]" % _item_row("75", PLAYER_ID, 1))
	var unrelated: SteamInventoryChangeResult = await inventory.consume(player, ITEM_ID, 2, CONSUME_REQUEST_ID)
	_check(unrelated.error == ERR_INVALID_DATA and unrelated.value == null and unrelated.message.contains("Reconcile"),
		"An unrelated response row cannot confirm a consumption outcome.")
	host.next_reply = _reply('{"response":{"success":true}}')
	var missing_rows: SteamInventoryChangeResult = await inventory.consume(player, ITEM_ID, 2, CONSUME_REQUEST_ID)
	_check(missing_rows.error == ERR_INVALID_DATA and missing_rows.value == null,
		"A reply without recognized item rows leaves the consumption outcome unresolved.")
	host.next_reply = _items_reply("[]")
	var empty_rows: SteamInventoryChangeResult = await inventory.consume(player, ITEM_ID, 2, CONSUME_REQUEST_ID)
	_check(empty_rows.error == ERR_INVALID_DATA and empty_rows.value == null,
		"An empty item array cannot confirm the requested consumption.")
	host.next_reply = _reply('{"response":{"success":true,"errorcode":17,"item_json":"[]"}}')
	var contradictory: SteamInventoryChangeResult = await inventory.grant(player, definitions, REQUEST_ID)
	_check(contradictory.error == ERR_QUERY_FAILED and contradictory.value == null,
		"An explicit provider error code prevents a confirmed grant.")


## Verify distinct exact-issuer trade parties and a recognized Steam acknowledgment.
func _check_trade(host: FixtureHost, inventory: SteamPublisherInventory, player: SteamIdentity,
		partner: SteamIdentity) -> void:
	var before := host.sent
	var same: SteamTradeSessionResult = await inventory.start_trade(player, player)
	_check(same.error == ERR_INVALID_PARAMETER and host.sent == before, "A trade requires distinct verified users.")
	host.next_reply = _reply('{"response":{}}')
	var unknown: SteamTradeSessionResult = await inventory.start_trade(player, partner)
	_check(unknown.error == ERR_INVALID_DATA and unknown.value == null,
		"An unrecognized response does not become a trade acknowledgment.")
	host.next_reply = _reply('{"response":{"success":true,"partya":%s,"partyb":%s,"appid":%d}}' %
		[PLAYER_ID, PARTNER_ID, APP_ID])
	var trade: SteamTradeSessionResult = await inventory.start_trade(player, partner)
	_check(trade.ok and trade.value.acknowledged and trade.value.player_id == PLAYER_ID and
		trade.value.partner_id == PARTNER_ID and host.method == "GET" and
		host.target.begins_with("/ISteamEconomy/StartTrade/v1/?") and host.target.contains("partya=" + PLAYER_ID) and
		host.target.contains("partyb=" + PARTNER_ID), "The fixed trade route confirms the exact request participants.")


## Check that closing the issuer settles a pending read and leaves the old service detached.
func _check_retirement(host: FixtureHost, publisher: SteamPublisher, player: SteamIdentity) -> void:
	var inventory := publisher.inventory()
	host.next_reply = _items_reply("[]", false, 3)
	var pending: Variant = inventory.items(player)
	publisher.close()
	var closed: SteamInventorySnapshotResult = await pending
	var retired: SteamInventorySnapshotResult = await inventory.items(player)
	_check(closed.error == ERR_SKIP and retired.error == ERR_UNAVAILABLE,
		"Retirement settles the pending observer and invalidates the retained service.")


## Return the exact decoded JSON form field without parsing unsigned tokens as floats.
func _payload(host: FixtureHost) -> String:
	var encoded := host.form if host.method == "POST" else host.target.get_slice("?", 1)
	for field in encoded.split("&"):
		if field.begins_with("input_json="):
			return field.trim_prefix("input_json=").uri_decode()
	return ""


## Construct an exact nested inventory row for the synthetic app.
func _item_row(item_id: String, account_id: String, quantity: int) -> String:
	return ('{"itemid":%s,"itemdefid":"42","quantity":%d,"accountid":%s,"appid":%d,"state":""}' %
		[item_id, quantity, account_id, APP_ID])


## Wrap Valve's encoded item_json field in a bounded synthetic reply.
func _items_reply(rows: String, replayed: bool = false, ticks: int = 1) -> Dictionary:
	return _reply(JSON.stringify({"response": {"item_json": rows, "replayed": replayed}}), ticks)


## Return a synthetic successful HTTP response with the specified body.
func _reply(body: String, ticks: int = 1) -> Dictionary:
	return {"status": 200, "body": body.to_utf8_buffer(), "ticks": ticks}


## Report an assertion without printing account or item identifiers.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
