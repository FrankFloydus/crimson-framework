# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies detached and invalid proof inputs without a signed Steam session.
extends SceneTree

## A synthetic public-format account identifier used by detached checks.
const ACCOUNT_ID := "76561198000000002"

## Number of failed assertions in the process.
var _failures := 0


## Defer native fixture calls until initialization finishes.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the typed unavailable and preflight paths of the verifier.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or args[0] not in ["missing", "sdk-off"]:
		push_error("Choose the Inventory verifier fixture mode missing or sdk-off.")
		quit(2)
		return
	var verifier := SteamInventoryVerifier.new()
	var plausible_bytes := PackedByteArray([1, 2, 3])
	var unavailable: SteamInventorySnapshotResult = await verifier.verify(plausible_bytes, ACCOUNT_ID)
	_check(unavailable.error == ERR_UNAVAILABLE and unavailable.value == null,
		"Detached proof verification returns a typed unavailable result.")
	var empty: SteamInventorySnapshotResult = await verifier.verify(PackedByteArray(), ACCOUNT_ID)
	var invalid_account: SteamInventorySnapshotResult = await verifier.verify(plausible_bytes, "0" + ACCOUNT_ID)
	var oversized_bytes := PackedByteArray()
	oversized_bytes.resize(1024 * 1024 + 1)
	var oversized: SteamInventorySnapshotResult = await verifier.verify(oversized_bytes, ACCOUNT_ID)
	_check(empty.error == ERR_INVALID_PARAMETER and invalid_account.error == ERR_INVALID_PARAMETER and
		oversized.error == ERR_INVALID_PARAMETER, "Proof preflight rejects empty, oversized, and invalid identity input.")
	_check(SteamDedicatedServer.new().inventory_verifier() == null,
		"An unattached dedicated server cannot lend an inventory verifier.")
	if _failures == 0:
		print("CRIMSON_STEAM_INVENTORY_VERIFIER_PASSED %s" % args[0])
	quit(0 if _failures == 0 else 1)


## Report a failed verification assertion without echoing a proof or account identifier.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
