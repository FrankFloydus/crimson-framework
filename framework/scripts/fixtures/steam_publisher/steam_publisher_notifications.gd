# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises the trusted notification service through a transport that opens no network connections.
extends SceneTree

## Synthetic app and identity fixtures used exclusively by the mock transport.
const APP_ID := 123456
const USER_ID := "76561197972495329"
const SESSION_ID := "9223372036854775810"
const NEXT_SESSION_ID := "9223372036854775811"
const CONTEXT := "18446744073709551614"
const ROUTE := "/IGameNotificationsService/"
const FIRST_LAUNCH := "fixture-launch-1"

## Counts failed assertions without logging fixture payloads.
var _failures := 0


## Receives fixed partner requests using the trusted transport's compile-time test seam.
class MockHost extends SteamPublisherHost:
	var allow_secret := true
	var next_reply: Dictionary = {}
	var requests := 0
	var verb := ""
	var target := ""
	var body := ""
	var authenticated := false

	## Resolve only a synthetic reference for the no-network fixture.
	func _resolve_secret(reference: StringName) -> String:
		return "mock-notification-placeholder" if allow_secret and reference == &"notification_fixture" else ""

	## Retain the sanitized wire request and deliver the next canned reply.
	func _test_response(method: String, host: String, path: String, payload: String, authorized: bool) -> Dictionary:
		requests += 1
		verb = method
		target = path
		body = payload
		authenticated = authorized and host == "partner.steam-api.com"
		return next_reply

	## Decode only the input_json field of this fixture's latest request.
	func input_json() -> Variant:
		var form := body if not body.is_empty() else target.get_slice("?", 1)
		for part in form.split("&", false):
			if part.begins_with("input_json="):
				return JSON.parse_string(part.trim_prefix("input_json=").uri_decode())
		return null


## Models an atomic durable operation ledger and target reservation across publisher boots.
class MockStore extends SteamNotificationStore:
	var ledger := {}
	var owned := {}
	var targets := {}
	var commits := 0
	var fail_commit := false
	## Models a unique durable app, account, and launch key across publisher boots.
	var launch_claims: Dictionary[String, bool] = {}

	## Reserve a launch exactly once, independently of the identity object's lifetime.
	func _claim_launch_opt_in(app_id: int, steam_id: String, launch_id: String) -> Error:
		var identity := "%d/%s/%s" % [app_id, steam_id, launch_id]
		if launch_claims.has(identity):
			return ERR_ALREADY_IN_USE
		launch_claims[identity] = true
		return OK

	## Roll back only the claim for a submission that never reached the test transport.
	func _release_unsent_launch_opt_in(app_id: int, steam_id: String, launch_id: String) -> bool:
		return launch_claims.erase("%d/%s/%s" % [app_id, steam_id, launch_id])

	## Compare immutable keys before checking current ownership or reserving a new target.
	func _claim(app_id: int, key: String, fingerprint: String, action: int, target_id: String) -> SteamNotificationClaim:
		var identity := "%d/%s" % [app_id, key]
		if ledger.has(identity):
			var existing: Dictionary = ledger[identity]
			if existing.fingerprint != fingerprint or existing.action != action or existing.target != target_id:
				return SteamNotificationClaim.create(SteamNotificationClaim.CONFLICT)
			if existing.committed:
				return SteamNotificationClaim.create(SteamNotificationClaim.COMMITTED, existing.provider_id)
			return SteamNotificationClaim.create(SteamNotificationClaim.IN_FLIGHT)
		if action != SteamNotificationStore.CREATE:
			var target := "%d/%s" % [app_id, target_id]
			if not owned.has(target):
				return SteamNotificationClaim.create(SteamNotificationClaim.DENIED)
			if targets.has(target):
				return SteamNotificationClaim.create(SteamNotificationClaim.IN_FLIGHT)
			targets[target] = identity
		ledger[identity] = {"fingerprint": fingerprint, "action": action, "target": target_id,
			"provider_id": "", "committed": false}
		return SteamNotificationClaim.create(SteamNotificationClaim.CLAIMED)

	## Commit only the exact pending request and change ownership in the same transaction.
	func _commit(app_id: int, key: String, fingerprint: String, action: int, provider_id: String) -> bool:
		var identity := "%d/%s" % [app_id, key]
		var entry: Dictionary = ledger.get(identity, {})
		if fail_commit or entry.is_empty() or entry.committed or entry.fingerprint != fingerprint or entry.action != action:
			return false
		var target := "%d/%s" % [app_id, provider_id]
		if action == SteamNotificationStore.CREATE:
			if owned.has(target):
				return false
			owned[target] = true
		elif entry.target != provider_id or targets.get(target, "") != identity:
			return false
		else:
			targets.erase(target)
			if action == SteamNotificationStore.REMOVE:
				owned.erase(target)
		entry.committed = true
		entry.provider_id = provider_id
		commits += 1
		return true

	## Release a claim proven unsent, along with its exact target reservation.
	func _release_unsent(app_id: int, key: String, fingerprint: String, action: int) -> bool:
		var identity := "%d/%s" % [app_id, key]
		var entry: Dictionary = ledger.get(identity, {})
		if entry.is_empty() or entry.committed or entry.fingerprint != fingerprint or entry.action != action:
			return false
		if action != SteamNotificationStore.CREATE:
			targets.erase("%d/%s" % [app_id, entry.target])
		ledger.erase(identity)
		return true

	## Resolve ownership by exact app and provider session ID.
	func _owns_session(app_id: int, session_id: String) -> bool:
		return owned.has("%d/%s" % [app_id, session_id])


## Run once the root can own the trusted host.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the notification facade and its durable host integration.
func _run() -> void:
	if OS.get_cmdline_user_args() != PackedStringArray(["mock"]):
		push_error("Use the notification mock lane.")
		quit(2)
		return
	var host := MockHost.new()
	if not host.has_method(&"_fixture_transport_enabled"):
		push_error("Notification verification requires the compile-time test transport.")
		host.free()
		quit(2)
		return
	root.add_child(host)
	var store := MockStore.new()
	var publisher := _boot(host, store)
	if not _check(publisher != null, "Boot the trusted publisher with the mock store."):
		host.free()
		quit(1)
		return
	var user := await _verify(publisher, host)
	if _check(user != null, "The trusted authentication route issues a verified launch identity."):
		await _exercise(host, store, publisher, user)
	publisher.close()
	host.queue_free()
	if _failures == 0:
		print("CRIMSON_STEAM_PUBLISHER_NOTIFICATIONS_PASSED mock")
	quit(0 if _failures == 0 else 1)


## Boot an exact trusted scope for the fixture app.
func _boot(host: MockHost, store: MockStore) -> SteamPublisher:
	var settings := SteamPublisherSettings.new()
	settings.app_id = APP_ID
	settings.publisher_key_ref = &"notification_fixture"
	settings.host = host
	settings.notification_store = store
	return SteamPublisher.boot(settings).value


## Get an exact publisher identity using the same fake Valve authentication path as production.
func _verify(publisher: SteamPublisher, host: MockHost) -> SteamIdentity:
	host.next_reply = _reply(200, '{"response":{"params":{"result":"OK","steamid":%s}}}' % USER_ID)
	var verified: SteamIdentityResult = await publisher.auth().verify(PackedByteArray([1, 5, 7]), "notification-fixture")
	return verified.value


## Verify exact routes, authorization, cancellation, durable replay, and removal.
func _exercise(host: MockHost, store: MockStore, publisher: SteamPublisher, user: SteamIdentity) -> void:
	var notifications := publisher.game_notifications()
	await _check_launch_opt_in(host, store, publisher, user)

	var waiting := _spec(user, SteamNotificationUserState.WAITING)
	var ready := _spec(user, SteamNotificationUserState.READY)
	host.next_reply = _reply(200, '{"response":{"sessionid":%s}}' % SESSION_ID, 2)
	var create := notifications.create(waiting, "turn-create")
	var request: Dictionary = host.input_json()
	_check(_wire(host, "POST", "CreateSession") and request.context == CONTEXT and request.users.size() == 1
		and request.users[0].steamid == USER_ID and request.users[0].state == "waiting",
		"Create sends exact uint64 strings and complete participant state.")
	var calls := host.requests
	var pending: SteamNotificationSessionResult = await notifications.create(waiting, "turn-create").wait()
	var conflict: SteamNotificationSessionResult = await notifications.create(ready, "turn-create").wait()
	var created: SteamNotificationSessionResult = await create.wait()
	if not _check(created.ok and created.value.session_id == SESSION_ID and created.value.context == CONTEXT,
			"Create commits and preserves exact unsigned session and context IDs."):
		return
	_check(pending.error == ERR_BUSY and conflict.error == ERR_ALREADY_IN_USE and host.requests == calls,
		"Duplicate and conflicting keys reach only the durable store.")

	var replacement := _boot(host, store)
	var next_user := await _verify(replacement, host)
	var next_notifications := replacement.game_notifications()
	calls = host.requests
	var foreign: SteamNotificationSessionsResult = await next_notifications.sessions_for(user).wait()
	var replayed: SteamNotificationSessionResult = await next_notifications.create(
		_spec(next_user, SteamNotificationUserState.WAITING), "turn-create").wait()
	_check(foreign.error == ERR_UNAUTHORIZED and replayed.ok and replayed.value.session_id == SESSION_ID
		and host.requests == calls, "A restarted publisher replays durable data while enforcing its own identity issuer.")
	var claimed_launch: SteamBoolResult = await next_notifications.request_opt_in(next_user, FIRST_LAUNCH).wait()
	_check(claimed_launch.error == ERR_ALREADY_IN_USE and host.requests == calls,
		"A recreated publisher and reverified account cannot prompt again for the same launch.")
	host.next_reply = _reply(200, '{"response":{"result":"Failure"}}')
	var refused: SteamBoolResult = await next_notifications.request_opt_in(next_user, "fixture-launch-refused").wait()
	_check(refused.error == ERR_INVALID_DATA and not refused.value,
		"An HTTP 200 with an explicit Steam refusal does not acknowledge opt-in.")
	replacement.close()

	host.next_reply = _reply(200, '{"response":{}}', 2)
	var update := notifications.update(SESSION_ID, ready, "turn-ready")
	request = host.input_json()
	_check(_wire(host, "POST", "UpdateSession") and request.sessionid == SESSION_ID
		and not request.has("context") and request.users[0].state == "ready", "Update sends complete state to the owned session.")
	calls = host.requests
	var competing: SteamBoolResult = await notifications.remove(SESSION_ID, "competing-remove").wait()
	var updated: SteamNotificationSessionResult = await update.wait()
	_check(competing.error == ERR_BUSY and updated.ok and updated.value.users[0].state == SteamNotificationUserState.READY
		and host.requests == calls, "Different keys cannot mutate the same provider target concurrently.")

	host.next_reply = _reply(200, _session_reply(APP_ID, SESSION_ID))
	var listed: SteamNotificationSessionsResult = await notifications.sessions_for(user).wait()
	_check(_wire(host, "GET", "EnumerateSessionsForApp") and listed.ok and listed.value.is_read_only()
		and listed.value.size() == 1 and listed.value[0].users[0].steam_id == USER_ID,
		"Listing returns bounded, typed same-app session snapshots.")
	var details: SteamNotificationSessionResult = await notifications.details(SESSION_ID).wait()
	_check(_wire(host, "GET", "GetSessionDetailsForApp") and details.ok and details.value.session_id == SESSION_ID,
		"Details return only the exact owned provider session.")
	host.next_reply = _reply(200, _session_reply(APP_ID + 1, SESSION_ID))
	var wrong_app: SteamNotificationSessionResult = await notifications.details(SESSION_ID).wait()
	_check(wrong_app.error == ERR_INVALID_DATA and wrong_app.value == null, "Wrong-app detail replies expose no data.")

	await _exercise_failures(host, store, notifications, waiting)
	host.next_reply = _reply(200, '{"response":{}}')
	var removed: SteamBoolResult = await notifications.remove(SESSION_ID, "turn-remove").wait()
	calls = host.requests
	var repeated_remove: SteamBoolResult = await notifications.remove(SESSION_ID, "turn-remove").wait()
	var denied: SteamNotificationSessionResult = await notifications.details(SESSION_ID).wait()
	_check(_wire(host, "POST", "DeleteSession") and removed.ok and repeated_remove.ok and repeated_remove.value
		and denied.error == ERR_UNAUTHORIZED and host.requests == calls,
		"Deletion retires ownership while its exact key remains durably replayable.")


## Verify durable opt-in claims through identity refresh, fresh launches and failed submissions.
func _check_launch_opt_in(host: MockHost, store: MockStore, publisher: SteamPublisher, user: SteamIdentity) -> void:
	var notifications := publisher.game_notifications()
	var calls := host.requests
	var forged: SteamBoolResult = await notifications.request_opt_in(SteamIdentity.new(), FIRST_LAUNCH).wait()
	var missing_launch: SteamBoolResult = await notifications.request_opt_in(user).wait()
	_check(forged.error == ERR_UNAUTHORIZED and missing_launch.error == ERR_INVALID_PARAMETER and
		host.requests == calls and store.launch_claims.is_empty(), "Opt-in requires both a verified user and trusted launch ID before claiming.")
	host.next_reply = _reply(200, '{"response":{}}', 2)
	var opt_in := notifications.request_opt_in(user, FIRST_LAUNCH)
	var duplicate: SteamBoolResult = await notifications.request_opt_in(user, FIRST_LAUNCH).wait()
	var accepted: SteamBoolResult = await opt_in.wait()
	_check(duplicate.error == ERR_ALREADY_IN_USE and accepted.ok and accepted.value and host.requests == calls + 1,
		"Only one request registers the exact launch.")
	_check(_wire(host, "POST", "RequestNotifications") and host.input_json().steamid == USER_ID and
		not host.input_json().has("launch_id"), "Opt-in uses the verified account and keeps the backend launch ID local.")
	var refreshed := await _verify(publisher, host)
	calls = host.requests
	var same_launch: SteamBoolResult = await notifications.request_opt_in(refreshed, FIRST_LAUNCH).wait()
	_check(refreshed != user and same_launch.error == ERR_ALREADY_IN_USE and host.requests == calls,
		"Re-verifying the account cannot resend opt-in within the same launch.")
	host.allow_secret = false
	var unsent: SteamBoolResult = await notifications.request_opt_in(refreshed, "fixture-next-launch").wait()
	host.allow_secret = true
	_check(unsent.error == ERR_UNCONFIGURED and host.requests == calls and store.launch_claims.size() == 1,
		"A failed pre-dispatch lookup releases only the unsent launch claim.")
	host.next_reply = _reply(200, '{"response":{}}')
	var next_launch: SteamBoolResult = await notifications.request_opt_in(refreshed, "fixture-next-launch").wait()
	_check(next_launch.ok and next_launch.value and host.requests == calls + 1,
		"The same verified account can register on the next game launch.")
	host.next_reply = _reply(504, '{}')
	var uncertain: SteamBoolResult = await notifications.request_opt_in(refreshed, "fixture-uncertain-launch").wait()
	var uncertain_retry: SteamBoolResult = await notifications.request_opt_in(refreshed, "fixture-uncertain-launch").wait()
	_check(uncertain.error == ERR_TIMEOUT and uncertain_retry.error == ERR_ALREADY_IN_USE and host.requests == calls + 2,
		"An ambiguous dispatched request holds its claim and cannot send a duplicate prompt.")


## Verify unsent release, ambiguous failure, and native completion after observation cancellation.
func _exercise_failures(host: MockHost, store: MockStore, notifications: SteamGameNotifications,
		spec: SteamNotificationSessionSpec) -> void:
	var calls := host.requests
	host.allow_secret = false
	var unsent: SteamNotificationSessionResult = await notifications.create(spec, "unsent-create").wait()
	host.allow_secret = true
	_check(unsent.error == ERR_UNCONFIGURED and host.requests == calls
		and not store.ledger.has("%d/unsent-create" % APP_ID), "A proven unsent request releases its durable claim.")

	host.next_reply = _reply(504, '{}')
	var unresolved: SteamNotificationSessionResult = await notifications.create(spec, "ambiguous-create").wait()
	calls = host.requests
	var held: SteamNotificationSessionResult = await notifications.create(spec, "ambiguous-create").wait()
	_check(unresolved.error == ERR_TIMEOUT and held.error == ERR_BUSY and host.requests == calls,
		"An ambiguous sent request retains its durable claim and is never submitted twice.")

	store.fail_commit = true
	host.next_reply = _reply(200, '{"response":{"sessionid":"%s"}}' % NEXT_SESSION_ID)
	var uncommitted: SteamNotificationSessionResult = await notifications.create(spec, "failed-commit").wait()
	store.fail_commit = false
	_check(uncommitted.error == ERR_CANT_ACQUIRE_RESOURCE and uncommitted.value != null
		and uncommitted.value.session_id == NEXT_SESSION_ID, "A failed store commit preserves the known provider receipt.")

	host.next_reply = _reply(200, '{"error":"Refused","response":{"sessionid":"%s"}}' % NEXT_SESSION_ID)
	var rejected: SteamNotificationSessionResult = await notifications.create(spec, "rejected-create").wait()
	_check(rejected.error == ERR_INVALID_DATA and rejected.value == null
		and not store._owns_session(APP_ID, NEXT_SESSION_ID), "An outer Steam error prevents ownership commit after HTTP 200.")

	host.next_reply = _reply(200, '{"response":{}}', 2)
	var commits := store.commits
	var cancelled := notifications.update(SESSION_ID, spec, "cancelled-observer")
	_check(cancelled.cancel() == OK and cancelled.result().error == ERR_SKIP and cancelled.cancel() == ERR_DOES_NOT_EXIST,
		"Local cancellation settles once without undoing an issued request.")
	for frame in 6:
		await process_frame
	var replayed: SteamNotificationSessionResult = await notifications.update(SESSION_ID, spec, "cancelled-observer").wait()
	_check(replayed.ok and store.commits == commits + 1 and cancelled.result().error == ERR_SKIP,
		"The native callback commits a receipt after its original observer cancels.")


## Create a complete detached notification spec for the requested state.
func _spec(user: SteamIdentity, state: SteamNotificationUserState.State) -> SteamNotificationSessionSpec:
	var substitutions: Dictionary[StringName, String] = {&"name": "Fixture & Recipient"}
	var localized := SteamLocalizedText.create(&"#FixtureTitle", substitutions)
	var users: Array[SteamNotificationUserState] = [SteamNotificationUserState.create(user, state, localized)]
	return SteamNotificationSessionSpec.create(CONTEXT, localized, users)


## Return one exact provider session response for readback tests.
func _session_reply(app_id: int, id: String) -> String:
	var localized := {"token": "#FixtureTitle", "variables": [{"key": "name", "value": "Fixture"}]}
	return JSON.stringify({"response": {"sessions": [{"appid": app_id, "sessionid": id,
		"context": CONTEXT, "title": localized, "user_status": [{"steamid": USER_ID, "state": "ready",
			"message": localized}]}]}})


## Return a bounded synthetic response with optional native-style scheduling delay.
func _reply(status: int, body: String, ticks: int = 1) -> Dictionary:
	return {"status": status, "body": body.to_utf8_buffer(), "ticks": ticks}


## Verify the fixed partner wire route and absence of a form credential.
func _wire(host: MockHost, verb: String, endpoint: String) -> bool:
	return host.authenticated and host.verb == verb and host.target.begins_with(ROUTE + endpoint + "/v1/") \
		and not host.target.contains("key=") and not host.body.contains("key=")


## Report a failure without disclosing fixture payloads or account data.
func _check(condition: bool, message: String) -> bool:
	if not condition:
		_failures += 1
		push_error(message)
	return condition
