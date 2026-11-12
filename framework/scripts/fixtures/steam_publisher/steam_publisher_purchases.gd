# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Runs the trusted purchase state machine with synthetic IDs and a socket-free transport.
extends SceneTree

const APP_ID := 123456
const PLAYER := "76561197972495329"
const OTHER_PLAYER := "76561197972495330"
const FIRST_ORDER := "938473"
const SECOND_ORDER := "938474"
const THIRD_ORDER := "938475"
const FIRST_CHECKOUT := "000000000000000000000000000000a1"
const SECOND_CHECKOUT := "000000000000000000000000000000a2"
const THIRD_CHECKOUT := "000000000000000000000000000000a3"
const FIRST_TXN := "374839"
const SECOND_TXN := "374840"
const THIRD_TXN := "374841"
const START := "2026-01-01T00:00:00Z"
const PURCHASED := "2026-01-01T00:02:00Z"
const REVERSED_AT := "2026-01-01T00:03:00Z"

var _failures := 0


## Uses a queue so multi-stage actions can receive a different reply for each fixed route.
class MockHost extends SteamPublisherHost:
	var responses: Array[Dictionary] = []
	var requests: Array[Dictionary] = []

	## Resolve a fixture-only key reference. The test transport never opens a socket.
	func _resolve_secret(reference: StringName) -> String:
		return "publisher-test-key" if reference == &"purchase_fixture" else ""

	## Capture synthetic form inputs and deliver the next canned Steam reply.
	func _test_response(method: String, host: String, target: String, body: String, authenticated: bool) -> Dictionary:
		requests.append({"method": method, "host": host, "target": target, "body": body, "authenticated": authenticated})
		if responses.is_empty():
			return {"status": 500, "body": PackedByteArray()}
		return responses.pop_front()


## Models compare-and-commit for the fixture only. Production supplies a durable game store.
class MockStore extends SteamPurchaseStore:
	var orders: Dictionary[String, SteamPublisherPurchaseOrder] = {}
	var checkouts: Dictionary[String, String] = {}
	var entitlements: Dictionary[String, PackedInt64Array] = {}
	var grant_count := 0
	var reversed_item_count := 0
	var refund_allowed := false
	var commit_blocked := false
	var lookup_error := OK

	## Reserve configured store orders using server prices.
	func _reserve_checkout(app_id: int, environment: SteamPublisherSettings.Environment, steam_id: String,
			cart: SteamPurchaseCart, checkout_id: String) -> SteamPublisherPurchaseOrder:
		if app_id != APP_ID or environment != SteamPublisherSettings.SANDBOX or steam_id != PLAYER:
			return null
		if checkouts.has(checkout_id):
			return orders[checkouts[checkout_id]]
		var order_id: String = {
			FIRST_CHECKOUT: FIRST_ORDER, SECOND_CHECKOUT: SECOND_ORDER, THIRD_CHECKOUT: THIRD_ORDER
		}.get(checkout_id, "")
		if order_id.is_empty():
			return null
		var quoted: Array[SteamPublisherPurchaseLine] = []
		for row in cart.lines():
			match row.sku:
				&"pump_finish":
					quoted.append(SteamPublisherPurchaseLine.create(row.sku, 101, row.quantity, 109 * row.quantity, "Pump & finish"))
				&"rifle_finish":
					quoted.append(SteamPublisherPurchaseLine.create(row.sku, 102, row.quantity, 220 * row.quantity, "Rifle finish"))
				_:
					return null
		var order := SteamPublisherPurchaseOrder.create(app_id, environment, order_id, steam_id, checkout_id,
			"USD", "en", quoted, SteamPublisherPurchaseOrder.GROSS)
		if order:
			orders[order_id] = order
			checkouts[checkout_id] = order_id
		return order

	## Distinguish missing orders from a persistence failure during settlement.
	func _load_order(app_id: int, environment: SteamPublisherSettings.Environment,
			order_id: String) -> SteamPublisherPurchaseOrderResult:
		if lookup_error != OK:
			return SteamPublisherPurchaseOrderResult.create(lookup_error)
		if app_id != APP_ID or environment != SteamPublisherSettings.SANDBOX or not orders.has(order_id):
			return SteamPublisherPurchaseOrderResult.create(ERR_DOES_NOT_EXIST)
		return SteamPublisherPurchaseOrderResult.create(OK, orders[order_id])

	## Commit an exact revision and model the entitlement difference once.
	func _compare_and_commit(expected_revision: int, next: SteamPublisherPurchaseOrder) -> bool:
		if commit_blocked or not orders.has(next.order_id):
			return false
		var before := orders[next.order_id]
		if before.revision != expected_revision or next.revision != expected_revision + 1:
			return false
		var entitled := PackedInt64Array()
		if next.phase in [SteamPublisherPurchaseOrder.FULFILLED, SteamPublisherPurchaseOrder.REFUNDING,
				SteamPublisherPurchaseOrder.PARTIALLY_REVERSED]:
			for line in next.lines:
				if not next.reversed_item_ids.has(line.item_id):
					entitled.append(line.item_id)
		var existing: PackedInt64Array = entitlements.get(next.order_id, PackedInt64Array())
		if not entitled.is_empty() and existing.is_empty():
			grant_count += 1
		entitlements[next.order_id] = entitled
		reversed_item_count += next.reversed_item_ids.size() - before.reversed_item_ids.size()
		orders[next.order_id] = next
		return true

	## Deny operator refunds by default.
	func _authorize_refund(order: SteamPublisherPurchaseOrder) -> bool:
		return refund_allowed and order.order_id == SECOND_ORDER


## Wait for the scene root before attaching the synthetic publisher host.
func _initialize() -> void:
	_run.call_deferred()


## Run trusted checkout, replay, ambiguous native replies, settlement and refund.
func _run() -> void:
	if OS.get_cmdline_user_args() != PackedStringArray(["mock"]):
		push_error("Use mock with a trusted test-transport build.")
		quit(2)
		return
	var host := MockHost.new()
	if not host.has_method(&"_fixture_transport_enabled"):
		push_error("Trusted purchase verification requires the compile-time test transport.")
		host.free()
		quit(2)
		return
	root.add_child(host)
	var store := MockStore.new()
	var settings := SteamPublisherSettings.new()
	settings.host = host
	settings.app_id = APP_ID
	settings.environment = SteamPublisherSettings.SANDBOX
	settings.publisher_key_ref = &"purchase_fixture"
	settings.purchase_store = store
	var boot := SteamPublisher.boot(settings)
	if not boot.ok:
		_check(false, "Trusted purchase settings boot an attached publisher.")
		host.free()
		quit(1)
		return
	var publisher := boot.value
	host.responses.append(_ok({"result": "OK", "steamid": PLAYER}))
	var verified: SteamIdentityResult = await publisher.auth().verify(PackedByteArray([1, 2]), "test-wallet")
	if not verified.ok:
		_check(false, "The fixture ticket produces an exact trusted player.")
		host.free()
		quit(1)
		return
	var purchases := publisher.purchases()
	var cart := SteamPurchaseCart.new().with_item(&"pump_finish", 1).with_item(&"rifle_finish", 1)
	var unauthorized: SteamPurchaseOutcomeResult = await purchases.begin(SteamIdentity.new(), cart, FIRST_CHECKOUT)
	_check(unauthorized.error == ERR_UNAUTHORIZED and host.requests.size() == 1,
		"A forged player cannot price, reserve or submit a checkout.")
	var malformed: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart, "000000000000000000000000000000A1")
	_check(malformed.error == ERR_INVALID_PARAMETER and store.orders.is_empty() and host.requests.size() == 1,
		"An invalid retry token cannot reach the store or Steam.")
	var absent: SteamPurchaseOutcomeResult = await purchases.query("818181")
	_check(absent.error == ERR_DOES_NOT_EXIST and host.requests.size() == 1, "Unknown order IDs stay local.")
	await _check_buyer_info(host, purchases, verified.value)

	host.responses.append(_ok({"orderid": FIRST_ORDER, "transid": FIRST_TXN}))
	var begun: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart, FIRST_CHECKOUT)
	_check(begun.ok and begun.value.state == SteamPurchaseOutcome.AWAITING_APPROVAL and begun.value.total == 329
		and begun.value.order_id == FIRST_ORDER and begun.checkout_id == FIRST_CHECKOUT,
		"Steam acknowledgment stores the exact order before reporting authorization pending.")
	var init: Dictionary = host.requests.back()
	_check(init.host == "partner.steam-api.com" and init.method == "POST" and init.authenticated
		and init.target == "/ISteamMicroTxnSandbox/InitTxn/v3/" and init.body.contains("steamid=" + PLAYER)
		and init.body.contains("itemcount=2") and init.body.contains("usersession=client")
		and init.body.contains("amount%5B0%5D=109") and init.body.contains("description%5B0%5D=Pump%20%26%20finish")
		and not init.body.contains("key="), "InitTxn uses the sandbox route and the exact server-priced form.")
	var before := host.requests.size()
	var replay: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart, FIRST_CHECKOUT)
	var edited: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart.with_item(&"pump_finish", 2), FIRST_CHECKOUT)
	_check(replay.ok and replay.value.order_id == FIRST_ORDER and edited.error == ERR_INVALID_DATA
		and host.requests.size() == before, "Checkout replay reuses the exact quote and rejects changed quantities.")

	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Approved", START, OTHER_PLAYER)))
	var mismatched: SteamPurchaseOutcomeResult = await purchases.finalize(FIRST_ORDER)
	_check(mismatched.error == ERR_INVALID_DATA and _calls(host, "FinalizeTxn") == 0 and store.grant_count == 0,
		"A mismatched Steam payer cannot authorize a charge or entitlement.")
	var wrong_amount := _row(FIRST_ORDER, FIRST_TXN, "Approved", START)
	wrong_amount.items[0].amount = 109
	host.responses.append(_ok(wrong_amount))
	var mismatched_price: SteamPurchaseOutcomeResult = await purchases.finalize(FIRST_ORDER)
	_check(mismatched_price.error == ERR_INVALID_DATA and _calls(host, "FinalizeTxn") == 0,
		"The selected gross tax basis must match exactly even when the alternate basis would match.")
	host.responses.append(_reply(200, {"response": {"result": "OK",
		"params": _row(FIRST_ORDER, FIRST_TXN, "Approved", START), "error": {"errorcode": 10}}}))
	var contradictory: SteamPurchaseOutcomeResult = await purchases.finalize(FIRST_ORDER)
	_check(contradictory.error == ERR_INVALID_DATA and _calls(host, "FinalizeTxn") == 0,
		"An OK envelope with an explicit error cannot authorize a charge.")

	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Approved", START)))
	host.responses.append(_reply(504, {}))
	var uncertain: SteamPurchaseOutcomeResult = await purchases.finalize(FIRST_ORDER)
	_check(not uncertain.ok and uncertain.value != null and uncertain.value.state == SteamPurchaseOutcome.RECONCILING
		and store.orders[FIRST_ORDER].phase == SteamPublisherPurchaseOrder.FINALIZING
		and _calls(host, "FinalizeTxn") == 1 and store.grant_count == 0,
		"An ambiguous charge leaves a durable claim and a recoverable order without granting.")
	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Approved", START)))
	var retry: SteamPurchaseOutcomeResult = await purchases.finalize(FIRST_ORDER)
	_check(retry.ok and retry.value.state == SteamPurchaseOutcome.RECONCILING and _calls(host, "FinalizeTxn") == 1,
		"A previously claimed charge queries Steam and does not repeat the charge while approval remains unresolved.")

	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Succeeded", PURCHASED)))
	store.commit_blocked = true
	var blocked: SteamPurchaseOutcomeResult = await purchases.query(FIRST_ORDER)
	_check(blocked.error == ERR_BUSY and store.grant_count == 0, "A failed durable commit cannot report fulfillment.")
	store.commit_blocked = false
	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Succeeded", PURCHASED)))
	var fulfilled: SteamPurchaseOutcomeResult = await purchases.finalize(FIRST_ORDER)
	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Succeeded", PURCHASED)))
	var observed: SteamPurchaseOutcomeResult = await purchases.query(FIRST_ORDER)
	_check(fulfilled.ok and observed.ok and observed.value.state == SteamPurchaseOutcome.FULFILLED
		and store.grant_count == 1 and _calls(host, "FinalizeTxn") == 1,
		"A verified paid order grants once even when queried again.")

	host.responses.append(_ok(_row(FIRST_ORDER, FIRST_TXN, "Succeeded", PURCHASED)))
	var denied_refund: SteamPurchaseOutcomeResult = await purchases.refund(FIRST_ORDER)
	_check(denied_refund.error == ERR_UNAUTHORIZED and _calls(host, "RefundTxn") == 0,
		"Operator refunds require explicit policy approval for the exact order.")

	var cursor := Time.get_unix_time_from_datetime_string(PURCHASED)
	var next_cursor := Time.get_unix_time_from_datetime_string(REVERSED_AT)
	var partial := _row(FIRST_ORDER, FIRST_TXN, "PartialRefund", REVERSED_AT)
	partial.items[0].itemstatus = "Refunded"
	var page := {"count": 2, "orders": [{"orderid": "0", "time": PURCHASED}, partial]}
	host.responses.append(_ok(page))
	store.lookup_error = ERR_CANT_OPEN
	var stalled: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor)
	_check(stalled.error == ERR_CANT_OPEN and stalled.value.next_since_unix == cursor and store.reversed_item_count == 0,
		"Storage errors leave the report cursor on the known order for replay.")
	store.lookup_error = OK
	host.responses.append(_ok(page))
	var settled: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor)
	host.responses.append(_ok(page))
	var settlement_replay: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor)
	host.responses.append(_ok(partial))
	var partial_order: SteamPurchaseOutcomeResult = await purchases.query(FIRST_ORDER)
	_check(settled.ok and settled.value.processed == 1 and settled.value.next_since_unix == next_cursor
		and settlement_replay.ok and partial_order.ok and partial_order.value.state == SteamPurchaseOutcome.PARTIALLY_REVERSED
		and store.orders[FIRST_ORDER].reversed_item_ids == PackedInt64Array([101])
		and store.reversed_item_count == 1,
		"Settlement ignores out-of-scope orders and reverses exactly the known refunded item idempotently.")
	var report_request: Dictionary = host.requests[host.requests.size() - 2]
	_check(report_request.target.begins_with("/ISteamMicroTxnSandbox/GetReport/v5/?")
		and report_request.target.contains("type=SETTLEMENT") and report_request.target.contains("time=2026-01-01T00%3A02%3A00Z"),
		"Settlement uses the fixed sandbox reporting route and UTC cursor.")

	host.responses.append(_reply(200, {"response": {"result": "Failure", "params": {"orderid": FIRST_ORDER},
		"error": {"errorcode": 10}}}))
	var ambiguous_init: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart, SECOND_CHECKOUT)
	_check(ambiguous_init.error == ERR_INVALID_DATA
		and store.orders[SECOND_ORDER].phase == SteamPublisherPurchaseOrder.INITIALIZING,
		"A rejection echo for a different order leaves the exact initialization uncertain.")
	host.responses.append(_ok(_row(SECOND_ORDER, SECOND_TXN, "Approved", START)))
	var init_replay: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart, SECOND_CHECKOUT)
	_check(init_replay.ok and _calls(host, "InitTxn") == 2, "Checkout replay resolves an uncertain InitTxn by querying only.")
	host.responses.append(_ok(_row(SECOND_ORDER, SECOND_TXN, "Approved", START)))
	host.responses.append(_ok({"orderid": SECOND_ORDER, "transid": SECOND_TXN}))
	var direct: SteamPurchaseOutcomeResult = await purchases.finalize(SECOND_ORDER)
	_check(direct.ok and direct.value.state == SteamPurchaseOutcome.FULFILLED and store.grant_count == 2,
		"An exact successful finalization acknowledgment durably grants the second order.")
	store.refund_allowed = true
	host.responses.append(_ok(_row(SECOND_ORDER, SECOND_TXN, "Succeeded", PURCHASED)))
	host.responses.append(_ok({"orderid": SECOND_ORDER, "transid": SECOND_TXN}))
	host.responses.append(_ok(_row(SECOND_ORDER, SECOND_TXN, "Refunded", REVERSED_AT)))
	var refunded: SteamPurchaseOutcomeResult = await purchases.refund(SECOND_ORDER)
	_check(refunded.ok and refunded.value.state == SteamPurchaseOutcome.REVERSED and store.reversed_item_count == 3
		and _calls(host, "RefundTxn") == 1 and host.responses.is_empty(),
		"An approved full refund confirms Steam state before reversing all remaining order entitlements.")

	host.responses.append(_ok({"orderid": THIRD_ORDER, "transid": THIRD_TXN}))
	var third: SteamPurchaseOutcomeResult = await purchases.begin(verified.value, cart, THIRD_CHECKOUT)
	host.responses.append(_ok(_row(THIRD_ORDER, THIRD_TXN, "Approved", START)))
	host.responses.append(_reply(504, {}))
	var lost: SteamPurchaseOutcomeResult = await purchases.finalize(THIRD_ORDER)
	var first_observed := _row(THIRD_ORDER, THIRD_TXN, "PartialRefund", REVERSED_AT)
	first_observed.items[0].itemstatus = "Refunded"
	host.responses.append(_ok({"count": 1, "orders": [first_observed]}))
	var recovered_partial: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor)
	_check(third.ok and not lost.ok and recovered_partial.ok and store.grant_count == 3
		and store.entitlements[THIRD_ORDER] == PackedInt64Array([102]),
		"A partial refund first seen after a lost finalization grants only the surviving paid line.")
	await _check_report_limits(host, purchases, cursor, next_cursor)

	publisher.close()
	var retired: SteamPurchaseOutcomeResult = await purchases.query(FIRST_ORDER)
	_check(retired.error == ERR_UNAVAILABLE, "A retained service cannot reopen a closed publisher.")
	host.free()
	if _failures == 0:
		print("CRIMSON_STEAM_PUBLISHER_PURCHASES_PASSED mock")
	quit(0 if _failures == 0 else 1)


## Read typed Wallet metadata through the same exact issuer and sandbox transport as checkout.
func _check_buyer_info(host: MockHost, purchases: SteamPublisherPurchases, player: SteamIdentity) -> void:
	var before := host.requests.size()
	var unauthorized: SteamPurchaseBuyerInfoResult = await purchases.buyer_info(SteamIdentity.new())
	_check(unauthorized.error == ERR_UNAUTHORIZED and host.requests.size() == before,
		"An unverified identity cannot query buyer metadata.")
	host.responses.append(_ok({"state": "WA", "country": "US", "currency": "USD", "status": "Trusted"}))
	var trusted: SteamPurchaseBuyerInfoResult = await purchases.buyer_info(player)
	var request: Dictionary = host.requests.back()
	_check(trusted.ok and trusted.value.country == "US" and trusted.value.currency == "USD" and
		trusted.value.region == "WA" and trusted.value.account_status == SteamPurchaseBuyerInfo.TRUSTED and
		request.method == "GET" and request.authenticated and request.host == "partner.steam-api.com" and
		request.target.begins_with("/ISteamMicroTxnSandbox/GetUserInfo/v2/?") and
		request.target.contains("appid=" + str(APP_ID)) and request.target.contains("steamid=" + PLAYER),
		"The sandbox route requests the exact authenticated buyer and returns typed regional quote metadata.")
	host.responses.append(_ok({"country": "JP", "currency": "JPY"}))
	var omitted: SteamPurchaseBuyerInfoResult = await purchases.buyer_info(player)
	_check(omitted.ok and omitted.value.region.is_empty() and omitted.value.account_status == SteamPurchaseBuyerInfo.UNKNOWN,
		"Wallet responses without region or status preserve an empty region and unknown purchasing state.")
	host.responses.append(_ok({"country": "IT", "currency": "EUR", "status": "Locked"}))
	var locked: SteamPurchaseBuyerInfoResult = await purchases.buyer_info(player)
	_check(locked.ok and locked.value.account_status == SteamPurchaseBuyerInfo.LOCKED,
		"Steam's locked status is exposed to the game's eligibility policy.")
	host.responses.append(_ok({"country": "US", "currency": "USD", "steamid": OTHER_PLAYER}))
	var mismatched: SteamPurchaseBuyerInfoResult = await purchases.buyer_info(player)
	host.responses.append(_ok({"country": "US", "currency": "INVALID"}))
	var malformed: SteamPurchaseBuyerInfoResult = await purchases.buyer_info(player)
	_check(mismatched.error == ERR_INVALID_DATA and mismatched.value == null and
		malformed.error == ERR_INVALID_DATA and malformed.value == null,
		"Wallet metadata rejects mismatched accounts and malformed currency without exposing a snapshot.")


## Check the requested settlement size, the larger report-only response limit, and backpressure.
func _check_report_limits(host: MockHost, purchases: SteamPublisherPurchases, cursor: int, next_cursor: int) -> void:
	var before := host.requests.size()
	var invalid: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor, 1001)
	_check(invalid.error == ERR_INVALID_PARAMETER and host.requests.size() == before,
		"Page sizes above the supported bound cannot reach Steam.")
	host.responses.append(_ok({"count": 2, "orders": [
		{"orderid": "0", "time": PURCHASED}, {"orderid": "0", "time": PURCHASED}]}))
	var blocked: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor, 2)
	_check(blocked.error == ERR_BUSY and blocked.value.next_since_unix == cursor and
		host.requests.back().target.contains("maxresults=2"),
		"A full page at the inclusive cursor applies backpressure without skipping entries.")
	var rows: Array[Dictionary] = []
	var row := {"orderid": "0", "time": REVERSED_AT, "fixture_padding": "x".repeat(300)}
	for _index in range(1000):
		rows.append(row)
	var response := _ok({"count": 1000, "orders": rows})
	_check(response.body.size() > 256 * 1024, "The default page fixture exceeds the ordinary transport body limit.")
	host.responses.append(response)
	var accepted: SteamPurchaseReconciliationResult = await purchases.reconcile(cursor)
	_check(accepted.ok and accepted.value.processed == 0 and accepted.value.next_since_unix == next_cursor and
		host.requests.back().target.contains("maxresults=1000"),
		"The report endpoint accepts a full default page beyond the ordinary response bound.")


## Count exact API operations independently of transport status.
func _calls(host: MockHost, operation: String) -> int:
	var count := 0
	for request in host.requests:
		if request.target.contains("/" + operation + "/"):
			count += 1
	return count


## Build the documented complete Steam transaction row from synthetic receipt data.
func _row(order_id: String, transaction: String, status: String, time: String, payer := PLAYER) -> Dictionary:
	return {"orderid": order_id, "transid": transaction, "steamid": payer, "status": status,
		"appid": APP_ID, "currency": "USD", "time": time, "items": [
			{"itemid": 101, "qty": 1, "amount": 100, "vat": 9, "itemstatus": status},
			{"itemid": 102, "qty": 1, "amount": 200, "vat": 20, "itemstatus": "Succeeded"},
		]}


## Wrap a documented successful MicroTxn reply.
func _ok(params: Dictionary) -> Dictionary:
	return _reply(200, {"response": {"result": "OK", "params": params}})


## Prepare a native test transport response.
func _reply(status: int, body: Dictionary) -> Dictionary:
	return {"status": status, "body": JSON.stringify(body).to_utf8_buffer()}


## Report a failed invariant without printing any request data.
func _check(condition: bool, detail: String) -> void:
	if not condition:
		_failures += 1
		push_error(detail)
