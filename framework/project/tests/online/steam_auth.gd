# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies ticket issuance and teardown without printing or transmitting a ticket.
extends SceneTree

## Valve's shared Steamworks sample AppID.
const SPACEWAR := 480


## Start after the root can attach the client scope.
func _initialize() -> void:
	_run.call_deferred()


## Check typed client results and scope retirement.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose a valid Steam Auth fixture mode.")
		quit(2)
		return

	var mode := arguments[0]
	var steam := SteamClient.boot(root, SPACEWAR)
	var detached := SteamAuth.new()
	var invalid: SteamWebTicketResult = await detached.web_ticket("")
	var unavailable: SteamWebTicketResult = await detached.web_ticket("crimson-fixture")
	var no_challenge: SteamOwnershipTicketResult = await detached.ownership_ticket(PackedByteArray())
	var passed := invalid.error == ERR_INVALID_PARAMETER and unavailable.error == ERR_UNAVAILABLE
	passed = passed and no_challenge.error == ERR_INVALID_PARAMETER
	if mode == "spacewar":
		passed = passed and steam.is_ready() and steam.auth() != null
		if steam.auth() != null:
			passed = (await _check_spacewar(steam)) and passed
	else:
		passed = passed and not steam.is_ready() and steam.auth() == null
	steam.free()
	if passed:
		print("CRIMSON_STEAM_AUTH_PASSED %s" % mode)
	else:
		push_error("Steam ticket issuance or scope verification failed.")
	quit(0 if passed else 1)


## Obtain two tickets and verify that only their exact scope can retire each one.
func _check_spacewar(steam: SteamClient) -> bool:
	var auth := steam.auth()
	var sibling := SteamClient.boot(root, SPACEWAR)
	if not sibling.is_ready():
		sibling.free()
		return false

	var primary: SteamWebTicketResult = await auth.web_ticket("crimson-fixture-primary")
	var secondary: SteamWebTicketResult = await sibling.auth().web_ticket("crimson-fixture-sibling")
	var passed := primary.ok and secondary.ok and primary.value != null and secondary.value != null
	if passed:
		passed = not primary.value.bytes().is_empty() and not secondary.value.bytes().is_empty()
		primary.value.close()
		primary.value.close()
		passed = passed and primary.value.bytes().is_empty() and not secondary.value.bytes().is_empty()

	var encrypted: SteamOwnershipTicketResult = await auth.ownership_ticket(
		"crimson-fixture-ownership".to_utf8_buffer())
	if encrypted.ok:
		passed = passed and encrypted.value != null and not encrypted.value.bytes().is_empty()
	print("CRIMSON_STEAM_AUTH_OWNERSHIP status=%d" % encrypted.error)

	var pending: Variant = auth.web_ticket("crimson-fixture-cancel")
	var retired: Array[SteamWebTicketResult] = []
	if pending is Signal:
		var completion: Signal = pending
		completion.connect(func(result: SteamWebTicketResult) -> void: retired.append(result), CONNECT_ONE_SHOT)
	steam.stop()
	var old_scope: SteamWebTicketResult = await auth.web_ticket("crimson-fixture-retired")
	passed = passed and old_scope.error == ERR_UNAVAILABLE and steam.auth() == null
	passed = passed and retired.size() == 1 and retired[0].error == ERR_SKIP
	if secondary.ok:
		passed = passed and not secondary.value.bytes().is_empty()
	sibling.stop()
	if secondary.ok:
		passed = passed and secondary.value.bytes().is_empty()
	sibling.free()
	return passed
