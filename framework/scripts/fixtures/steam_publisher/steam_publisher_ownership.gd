# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises trusted encrypted ownership locally and through an explicitly enabled Steam app.
extends SceneTree

## Synthetic AppID and account used by the deterministic ownership fixture.
const APP_ID := 123456
const USER_ID := "76561197972495329"
const OTHER_USER_ID := "76561197972495330"
const OWNED_DLC := 987651
const OTHER_DLC := 987652

## Counts failed assertions without printing proof or secret data.
var _failures := 0


## Resolves a disposable synthetic byte key and only the compile-time fixture's test metadata.
class OwnershipHost extends SteamPublisherHost:
	var key_calls := 0
	var decoder_calls := 0
	var key_size := 32
	var next_proof: Dictionary = {}

	## Supply synthetic bytes only for this fixture's explicitly configured reference.
	func _resolve_encrypted_ticket_key(reference: StringName) -> PackedByteArray:
		key_calls += 1
		var secret := PackedByteArray()
		if reference == &"ownership_fixture":
			secret.resize(key_size)
			secret.fill(17)
		return secret

	## Return the canned proof only for the exact fixture ticket bytes.
	func _test_decrypted_ownership_ticket(ticket: PackedByteArray) -> Dictionary:
		decoder_calls += 1
		return next_proof if ticket == PackedByteArray([11, 19, 53]) else {}


## Resolves the authorized app's key from the file provisioned outside the copied test project.
class LiveOwnershipHost extends SteamPublisherHost:
	var key_file := ""

	## Read the raw key only when the trusted publisher requests its exact reference.
	func _resolve_encrypted_ticket_key(reference: StringName) -> PackedByteArray:
		if reference != &"ownership_live" or key_file.is_empty():
			return PackedByteArray()
		var key := FileAccess.open(key_file, FileAccess.READ)
		if key == null or key.get_length() != 32:
			return PackedByteArray()
		var bytes := key.get_buffer(32)
		key.close()
		return bytes


## Models atomic matching and retirement of one backend-issued challenge across scopes.
class ChallengeStore extends SteamOwnershipChallengeStore:
	var expected_app := 0
	var expected_owner := ""
	var expected_challenge := PackedByteArray()
	var expires := 0
	var attempts := 0
	var spent := false
	var after_consume: Callable

	## Match the exact app, account, challenge and expiry before marking it spent.
	func _consume(app_id: int, challenge: PackedByteArray, steam_id: String, issued_at_unix: int, now_unix: int) -> bool:
		attempts += 1
		if spent or app_id != expected_app or steam_id != expected_owner or challenge != expected_challenge \
				or now_unix >= expires or issued_at_unix <= 0:
			return false
		spent = true
		if after_consume.is_valid():
			after_consume.call()
		return true


## Run after the root can own the trusted publisher host.
func _initialize() -> void:
	_run.call_deferred()


## Verify the selected local mode and emit one explicit result marker.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["mock", "sdk-off", "missing", "invalid", "app"]:
		push_error("Choose the ownership fixture mode mock, sdk-off, missing, invalid, or app.")
		quit(2)
		return
	var mode := arguments[0]
	if mode == "app":
		await _run_live()
		return
	var host := OwnershipHost.new()
	var test_transport := host.has_method(&"_fixture_transport_enabled")
	if (mode == "mock" and not test_transport) or (mode in ["missing", "invalid"] and test_transport):
		push_error("Use the matching trusted publisher build for the selected ownership fixture mode.")
		host.free()
		quit(2)
		return
	root.add_child(host)
	var store := ChallengeStore.new()
	var publisher := _boot(host, store)
	if not _check(publisher != null, "Boot a trusted publisher with only its encrypted ticket credential reference."):
		host.free()
		quit(1)
		return
	var challenge := _new_challenge()
	_seed(store, challenge)
	if mode == "mock":
		await _check_mock(host, store, publisher, challenge)
	else:
		var invalid_ticket := PackedByteArray()
		invalid_ticket.resize(64)
		invalid_ticket.fill(0)
		var rejected := publisher.auth().verify_ownership(invalid_ticket, challenge)
		var expected_error := ERR_UNAUTHORIZED if mode == "invalid" else ERR_UNAVAILABLE
		_check(rejected.error == expected_error and rejected.value == null and store.attempts == 0,
			"No invalid or unavailable native ticket can reach the durable store.")
		_check(host.decoder_calls == 0 and host.key_calls == (1 if mode == "invalid" else 0),
			"Only the available production decryptor resolves the synthetic key.")
	publisher.close()
	host.queue_free()
	if _failures == 0:
		print("CRIMSON_STEAM_PUBLISHER_OWNERSHIP_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Verify one live encrypted ticket against the exact authorized app and the signed-in account.
func _run_live() -> void:
	if OS.get_environment("CRIMSON_STEAM_OWNERSHIP_ALLOW_LIVE") != "1":
		push_error("App mode requires CRIMSON_STEAM_OWNERSHIP_ALLOW_LIVE=1.")
		quit(2)
		return
	var app_id := _parse_app_id(OS.get_environment("CRIMSON_STEAM_OWNERSHIP_APP_ID"))
	if app_id == 0 or app_id == 480:
		push_error("App mode requires an authorized non-480 CRIMSON_STEAM_OWNERSHIP_APP_ID.")
		quit(2)
		return
	var key_file := _configured_key_file()
	if key_file.is_empty():
		push_error("App mode requires CRIMSON_STEAM_OWNERSHIP_KEY_FILE to name an absolute, readable 32-byte raw key file outside the project.")
		quit(2)
		return
	var expected_dlc: Dictionary[int, bool] = {}
	if not _read_dlc_expectations(app_id, expected_dlc):
		push_error("Use distinct DLC AppIDs followed by :owned or :unowned in CRIMSON_STEAM_OWNERSHIP_DLC_EXPECTATIONS.")
		quit(2)
		return

	var host := LiveOwnershipHost.new()
	if host.has_method(&"_fixture_transport_enabled"):
		push_error("App mode requires the production trusted publisher build with the Steam SDK enabled.")
		host.free()
		quit(2)
		return
	host.key_file = key_file
	root.add_child(host)
	var store := ChallengeStore.new()
	var settings := SteamPublisherSettings.new()
	settings.app_id = app_id
	settings.host = host
	settings.encrypted_ticket_key_ref = &"ownership_live"
	settings.ownership_challenge_store = store
	var boot := SteamPublisher.boot(settings)
	if not _check(boot.ok and boot.value != null, "Boot the trusted ownership verifier. Error %d." % boot.error):
		host.queue_free()
		quit(1)
		return

	var client := SteamClient.new()
	root.add_child(client)
	var started := client.start(app_id)
	if _check(started == OK and client.get_app_id() == app_id and not client.get_local_steam_id().is_empty(),
			"Start the exact authorized Steam app with its signed-in account. Error %d." % started):
		await _check_live_round_trip(client, boot.value, store, expected_dlc)
	client.stop()
	client.queue_free()
	boot.value.close()
	host.key_file = ""
	host.queue_free()
	if _failures == 0:
		print("CRIMSON_STEAM_PUBLISHER_OWNERSHIP_DLC_CASES %d" % expected_dlc.size())
		print("CRIMSON_STEAM_PUBLISHER_OWNERSHIP_PASSED app")
	quit(0 if _failures == 0 else 1)


## Request one Steam ticket and validate its exact challenge, entitlements, and replay behavior locally.
func _check_live_round_trip(client: SteamClient, publisher: SteamPublisher, store: ChallengeStore,
		expected_dlc: Dictionary[int, bool]) -> void:
	var challenge := _new_challenge()
	if not _check(challenge.size() == 32, "Generate a fresh backend challenge."):
		return
	var account := client.get_local_steam_id()
	var app_id := client.get_app_id()
	store.expected_app = app_id
	store.expected_owner = account
	store.expected_challenge = challenge.duplicate()
	store.expires = _now() + 120
	var auth := client.auth()
	if not _check(auth != null, "Resolve the Steam client ticket issuer."):
		return
	var issued: SteamOwnershipTicketResult = await auth.ownership_ticket(challenge)
	if not _check(issued.ok and issued.value != null, "Request one encrypted ticket from Steam. Error %d." % issued.error):
		return
	var encrypted := issued.value.bytes()
	if not _check(not encrypted.is_empty(), "Steam returned encrypted ticket bytes."):
		return
	var verifier := publisher.auth()
	if not _check(verifier != null, "Resolve the trusted ticket verifier."):
		return
	var requested := PackedInt64Array(expected_dlc.keys())
	var wrong_challenge := challenge.duplicate()
	wrong_challenge[0] = wrong_challenge[0] ^ 1
	var mismatch := verifier.verify_ownership(encrypted, wrong_challenge, requested)
	_check(mismatch.error == ERR_UNAUTHORIZED and mismatch.value == null and store.attempts == 0,
		"Reject a mismatched embedded challenge before consuming the outstanding record.")
	var accepted := verifier.verify_ownership(encrypted, challenge, requested)
	if not _check(accepted.ok and accepted.value != null and accepted.value.identity != null
			and accepted.value.identity.steam_id == account and accepted.value.identity.app_id == app_id
			and accepted.value.app_owned and store.spent and store.attempts == 1,
			"Verify the exact signed-in account, base entitlement and one-time challenge. Error %d." % accepted.error):
		return
	_check(accepted.value.checked_dlc_ids == requested, "The receipt lists the requested DLC checks.")
	for dlc_id: int in expected_dlc:
		_check(accepted.value.owns_dlc(dlc_id) == expected_dlc[dlc_id],
			"A requested DLC entitlement matches the configured expectation.")
	var replay := verifier.verify_ownership(encrypted, challenge, requested)
	_check(replay.error == ERR_UNAUTHORIZED and replay.value == null and store.attempts == 2,
		"Reject replay of the exact ticket and consumed challenge.")


## Resolve and preflight the private key path without reading the key or printing the path.
func _configured_key_file() -> String:
	var path := OS.get_environment("CRIMSON_STEAM_OWNERSHIP_KEY_FILE").replace("\\", "/").simplify_path()
	if not path.is_absolute_path() or path.contains("://"):
		return ""
	var project := ProjectSettings.globalize_path("res://").replace("\\", "/").simplify_path().trim_suffix("/")
	var normalized := path.to_lower()
	var project_prefix := project.to_lower()
	if normalized == project_prefix or normalized.begins_with(project_prefix + "/"):
		return ""
	var key := FileAccess.open(path, FileAccess.READ)
	if key == null:
		return ""
	var valid := key.get_length() == 32
	key.close()
	return path if valid else ""


## Parse a canonical positive Steam AppID without accepting signed or padded strings.
func _parse_app_id(value: String) -> int:
	if value.length() > 10 or not value.is_valid_int():
		return 0
	var id := value.to_int()
	return id if id > 0 and id <= 4294967295 and str(id) == value else 0


## Parse optional owned and unowned DLC expectations before requesting a live ticket.
func _read_dlc_expectations(app_id: int, expected: Dictionary[int, bool]) -> bool:
	var configured := OS.get_environment("CRIMSON_STEAM_OWNERSHIP_DLC_EXPECTATIONS")
	if configured.is_empty():
		return true
	var entries := configured.split(",", true, 32)
	if entries.size() > 32:
		return false
	for entry in entries:
		var fields := entry.split(":", true, 2)
		if fields.size() != 2:
			return false
		var id := _parse_app_id(fields[0])
		if id == 0 or id == app_id or expected.has(id) or fields[1] not in ["owned", "unowned"]:
			return false
		expected[id] = fields[1] == "owned"
	return true


## Boot the trusted scope with the encrypted-only credential reference.
func _boot(host: OwnershipHost, store: ChallengeStore) -> SteamPublisher:
	var settings := SteamPublisherSettings.new()
	settings.app_id = APP_ID
	settings.host = host
	settings.encrypted_ticket_key_ref = &"ownership_fixture"
	settings.ownership_challenge_store = store
	return SteamPublisher.boot(settings).value


## Reject invalid proof before consume, then verify one-time success and cross-scope replay.
func _check_mock(host: OwnershipHost, store: ChallengeStore, publisher: SteamPublisher,
		challenge: PackedByteArray) -> void:
	var auth := publisher.auth()
	var ticket := PackedByteArray([11, 19, 53])
	var requested := PackedInt64Array([OWNED_DLC, OTHER_DLC])
	var good := _proof(challenge)
	var oversized := PackedByteArray()
	oversized.resize(1025)
	_check(auth.verify_ownership(ticket, oversized).error == ERR_INVALID_PARAMETER
		and auth.verify_ownership(ticket, challenge, PackedInt64Array([OWNED_DLC, OWNED_DLC])).error == ERR_INVALID_PARAMETER
		and auth.verify_ownership(ticket, challenge, PackedInt64Array([APP_ID])).error == ERR_INVALID_PARAMETER
		and host.key_calls == 0 and host.decoder_calls == 0, "Bounded requests are validated before resolving secrets.")
	host.key_size = 31
	var short_key := auth.verify_ownership(ticket, challenge)
	host.key_size = 32
	_check(short_key.error == ERR_UNCONFIGURED and short_key.value == null and host.decoder_calls == 0,
		"A key with the wrong byte length cannot reach decryption.")
	host.next_proof = good
	_check(auth.verify_ownership(PackedByteArray([1, 2]), challenge).error == ERR_UNAUTHORIZED,
		"Invalid ciphertext exposes no ownership proof.")

	var invalid_cases := [
		{"app_id": APP_ID + 1},
		{"issued_at_unix": _now() - 600},
		{"issued_at_unix": _now() + 120},
		{"steam_id": "0"},
		{"challenge": _new_challenge()},
		{"owned_app_ids": PackedInt64Array([OWNED_DLC])},
	]
	for changes in invalid_cases:
		host.next_proof = good.duplicate()
		host.next_proof.merge(changes, true)
		var invalid := auth.verify_ownership(ticket, challenge, requested)
		_check(invalid.error == ERR_UNAUTHORIZED and invalid.value == null and store.attempts == 0,
			"Wrong-app, stale, future, invalid account, challenge mismatch, and base entitlement failures reach no store hook.")

	host.next_proof = good
	store.expires = _now() - 1
	var expired := auth.verify_ownership(ticket, challenge, requested)
	store.expires = _now() + 60
	store.expected_owner = OTHER_USER_ID
	var wrong_owner := auth.verify_ownership(ticket, challenge, requested)
	store.expected_owner = USER_ID
	_check(expired.error == ERR_UNAUTHORIZED and wrong_owner.error == ERR_UNAUTHORIZED and not store.spent,
		"Expired and differently bound backend challenges yield no identity or consumption.")
	var accepted: SteamOwnershipResult = await auth.verify_ownership(ticket, challenge, requested)
	if not _check(accepted.ok and accepted.value != null and accepted.value.identity.steam_id == USER_ID
			and accepted.value.identity.app_id == APP_ID and accepted.value.app_owned and store.spent,
			"Valid app ownership and atomic consume mint a typed publisher identity."):
		return
	_check(accepted.value.issued_at_unix == good.issued_at_unix and accepted.value.checked_dlc_ids == requested
		and accepted.value.owned_dlc_ids == PackedInt64Array([OWNED_DLC]) and accepted.value.owns_dlc(OWNED_DLC)
		and not accepted.value.owns_dlc(OTHER_DLC), "The receipt reports exactly the requested DLC ownership snapshot.")
	var replay := auth.verify_ownership(ticket, challenge, requested)
	var replacement := _boot(host, store)
	var across_scope := replacement.auth().verify_ownership(ticket, challenge, requested)
	_check(replay.error == ERR_UNAUTHORIZED and across_scope.error == ERR_UNAUTHORIZED,
		"The durable store rejects repeated challenges across publisher scopes.")

	var closing_challenge := _new_challenge()
	_seed(store, closing_challenge)
	host.next_proof = _proof(closing_challenge)
	store.after_consume = replacement.close
	var retired := replacement.auth().verify_ownership(ticket, closing_challenge)
	store.after_consume = Callable()
	_check(retired.error == ERR_SKIP and retired.value == null and store.spent,
		"A scope retired inside the consume hook emits no identity.")
	publisher.close()
	_check(auth.verify_ownership(ticket, challenge).error == ERR_UNAVAILABLE,
		"A retained authentication facade cannot use a stopped publisher.")


## Set one outstanding challenge bound to the exact mock account and app.
func _seed(store: ChallengeStore, challenge: PackedByteArray) -> void:
	store.expected_app = APP_ID
	store.expected_owner = USER_ID
	store.expected_challenge = challenge.duplicate()
	store.expires = _now() + 60
	store.spent = false


## Create a disposable fixture challenge without logging its bytes.
func _new_challenge() -> PackedByteArray:
	return Crypto.new().generate_random_bytes(32)


## Make a typed synthetic decoder reply for a fresh challenge.
func _proof(challenge: PackedByteArray) -> Dictionary:
	return {"app_id": APP_ID, "steam_id": USER_ID, "issued_at_unix": _now(), "challenge": challenge,
		"owned_app_ids": PackedInt64Array([APP_ID, OWNED_DLC])}


## Match the verifier's Unix timestamp unit.
func _now() -> int:
	return int(Time.get_unix_time_from_system())


## Record a failing assertion without exposing account, challenge, ticket, or key material.
func _check(condition: bool, message: String) -> bool:
	if not condition:
		_failures += 1
		push_error(message)
	return condition
