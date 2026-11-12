# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies the client commerce boundary without calling Steam's payment service.
extends SceneTree

## Valve's shared sample AppID used only for the client lifecycle.
const SPACEWAR := 480


## A deterministic game adapter that never makes a network request.
class Backend extends SteamCommerceBackend:
	## Checkout IDs observed on the mock backend.
	var attempts: PackedStringArray = []
	## Retained request for the game-detach cancellation path.
	var held: SteamCommerceRequest
	## Whether begin should leave its local observer pending.
	var hold := false
	## Whether an order refresh should return an unrelated order ID.
	var wrong_receipt := false

	## Return one typed server-priced offer.
	func _catalog(request: SteamCommerceRequest) -> void:
		var entries: Array[SteamPurchaseItem] = [SteamPurchaseItem.create(&"finish", "Portal Finish", 199, "USD")]
		request.complete_catalog(SteamPurchaseCatalogResult.create(OK, SteamPurchaseCatalog.create(entries)))

	## Simulate a lost first reply and recover the same order from a repeated checkout ID.
	func _begin(_cart: SteamPurchaseCart, checkout_id: String, request: SteamCommerceRequest) -> void:
		attempts.append(checkout_id)
		if hold:
			held = request
		elif attempts.size() == 1:
			request.complete_order(SteamPurchaseOutcomeResult.create(ERR_CANT_CONNECT))
		else:
			var receipt := SteamPurchaseOutcome.create("987", "USD", 199, SteamPurchaseOutcome.AWAITING_APPROVAL)
			request.complete_order(SteamPurchaseOutcomeResult.create(OK, receipt, checkout_id))

	## Return the mock backend's state for an exact order.
	func _order(order_id: String, request: SteamCommerceRequest) -> void:
		var receipt_id := "123" if wrong_receipt else order_id
		var receipt := SteamPurchaseOutcome.create(receipt_id, "USD", 199, SteamPurchaseOutcome.FULFILLED)
		request.complete_order(SteamPurchaseOutcomeResult.create(OK, receipt))


## Start after the SceneTree root can attach the fixture Game.
func _initialize() -> void:
	_run.call_deferred()


## Check cart branching, typed snapshots, and exact game adapter teardown.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose a valid Steam Purchases fixture mode.")
		quit(2)
		return

	var mode := arguments[0]
	var blank := SteamPurchaseCart.new()
	var selected := blank.with_item(&"finish", 1)
	var changed := selected.with_item(&"finish", 2)
	var passed := blank.lines().is_empty() and selected.lines()[0].quantity == 1
	passed = passed and changed.lines()[0].quantity == 2 and selected.lines().is_read_only()
	passed = passed and blank.with_item(&"", 1) == null and SteamPurchaseCatalog.new().entries.is_read_only()
	var unavailable: SteamPurchaseCatalogResult = await SteamPurchases.new().catalog()
	passed = passed and unavailable.error == ERR_UNAVAILABLE
	if mode == "spacewar":
		passed = (await _check_bound_game(selected)) and passed
	if passed:
		print("CRIMSON_STEAM_PURCHASES_PASSED %s" % mode)
	else:
		push_error("Steam Purchases catalog, order correlation, retry, or scope verification failed.")
	quit(0 if passed else 1)


## Check a mock authenticated backend on a real Spacewar client attachment.
func _check_bound_game(cart: SteamPurchaseCart) -> bool:
	var backend := Backend.new()
	var provider := SteamProvider.configure().for_app(SPACEWAR).with_commerce_backend(backend)
	var game := GameDefinition.configure().with_provider(provider).boot(root)
	var scope := SteamServices.for_game(game)
	if scope == null or not scope.is_ready() or scope.purchases() == null:
		if game != null:
			game.free()
		return false

	var purchases := scope.purchases()
	var catalog: SteamPurchaseCatalogResult = await purchases.catalog()
	var passed := catalog.ok and catalog.value.entries[0].amount == 199
	var uncertain: SteamPurchaseOutcomeResult = await purchases.checkout(cart)
	passed = passed and uncertain.error == ERR_CANT_CONNECT and not uncertain.checkout_id.is_empty()
	passed = passed and uncertain.checkout_id == cart.checkout_id

	var restored := cart.with_checkout_id(uncertain.checkout_id)
	if restored == null:
		game.free()
		return false
	var receipt: SteamPurchaseOutcomeResult = await purchases.checkout(restored)
	passed = passed and receipt.ok and receipt.value.state == SteamPurchaseOutcome.AWAITING_APPROVAL
	passed = passed and backend.attempts.size() == 2 and backend.attempts[0] == backend.attempts[1]

	backend.wrong_receipt = true
	var wrong: SteamPurchaseOutcomeResult = await purchases.order("987")
	passed = passed and wrong.error == ERR_INVALID_DATA
	backend.wrong_receipt = false
	var observed: SteamPurchaseOutcomeResult = await purchases.order("987")
	passed = passed and observed.ok and observed.value.state == SteamPurchaseOutcome.FULFILLED

	backend.hold = true
	var pending: Variant = purchases.checkout(cart.with_item(&"finish", 2))
	var retired: Array[SteamPurchaseOutcomeResult] = []
	if pending is Signal:
		var completion: Signal = pending
		completion.connect(func(result: SteamPurchaseOutcomeResult) -> void: retired.append(result), CONNECT_ONE_SHOT)
	root.remove_child(game)
	passed = passed and retired.size() == 1 and retired[0].error == ERR_SKIP
	passed = passed and not retired[0].checkout_id.is_empty() and scope.purchases() == null
	if backend.held != null:
		passed = passed and backend.held.complete_order(SteamPurchaseOutcomeResult.create(FAILED)) == ERR_UNAVAILABLE
	else:
		passed = false
	game.free()
	return passed
