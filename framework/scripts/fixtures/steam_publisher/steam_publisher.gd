# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises trusted publisher routing and results with a transport that never connects to Steam.
extends SceneTree

## Synthetic AppID scoped to the mock transport.
const APP_ID := 123456
## Synthetic account IDs chosen above the exact range of a double.
const PLAYER_ID := "76561197972495329"
const OWNER_ID := "76561197972495330"
## Stable test-only audience with reserved form characters.
const AUDIENCE := "transfer hall/profile&test"

## Counts failed assertions without printing request details.
var _failures := 0


## Supplies a fake credential and records only requests made by the test transport.
class MockHost extends SteamPublisherHost:
	## The next canned response. A reply is delivered by the native host on a later frame.
	var next_reply: Dictionary = {}
	## Number of native calls into the mock transport.
	var request_count := 0
	## Number of references the host attempted to resolve.
	var secret_reads := 0
	## Method passed by the fixed endpoint table.
	var last_method := ""
	## Network origin passed by the fixed endpoint table.
	var last_host := ""
	## Encoded target, containing only synthetic fixture inputs.
	var last_target := ""
	## Encoded body, containing only synthetic fixture inputs.
	var last_body := ""
	## Whether native transport resolved a valid key for the authentication header.
	var last_authenticated := false

	## Resolve only the synthetic fixture reference.
	func _resolve_secret(reference: StringName) -> String:
		secret_reads += 1
		return "mock-publisher-placeholder" if reference == &"publisher_fixture" else ""

	## Record the exact wire shape without receiving the credential or opening a socket.
	func _test_response(method: String, host: String, target: String, body: String, authenticated: bool) -> Dictionary:
		request_count += 1
		last_method = method
		last_host = host
		last_target = target
		last_body = body
		last_authenticated = authenticated
		return next_reply


## Start only after the root can attach the mock host.
func _initialize() -> void:
	_run.call_deferred()


## Run the mock suite. A production binary has no test override to service these requests.
func _run() -> void:
	if OS.get_cmdline_user_args() != PackedStringArray(["mock"]):
		push_error("Use the mock lane with a publisher test-transport build.")
		quit(2)
		return
	var host := MockHost.new()
	if not host.has_method(&"_fixture_transport_enabled"):
		push_error("Publisher mock verification requires the compile-time test transport.")
		host.free()
		quit(2)
		return
	var settings := SteamPublisherSettings.new()
	settings.app_id = APP_ID
	settings.environment = SteamPublisherSettings.SANDBOX
	settings.publisher_key_ref = &"publisher_fixture"
	settings.host = host
	_check(SteamPublisher.boot(settings).error == ERR_UNCONFIGURED, "A detached host cannot boot a publisher.")
	root.add_child(host)
	var boot := SteamPublisher.boot(settings)
	if not boot.ok or boot.value == null:
		_check(false, "Valid settings and an attached host boot a publisher.")
		host.free()
		quit(1)
		return
	var publisher := boot.value
	var auth := publisher.auth()
	_check(publisher.is_ready() and host.secret_reads == 0, "Boot copies references and resolves no secret until needed.")
	settings.app_id = APP_ID + 5
	var invalid: SteamIdentityResult = await auth.verify(PackedByteArray(), AUDIENCE)
	_check(invalid.error == ERR_INVALID_PARAMETER and host.request_count == 0, "Empty tickets fail before transport.")

	var ticket := PackedByteArray([1, 0, 255])
	host.next_reply = _reply(200, '{"response":{"error":{"errorcode":101,"errordesc":"private vendor response"}}}')
	var denied: SteamIdentityResult = await auth.verify(ticket, "wrong-audience")
	_check(denied.error == ERR_UNAUTHORIZED and denied.value == null and not denied.message.contains("private vendor"),
		"Rejected tickets never expose an identity or vendor error text.")
	_check(host.last_target.contains("identity=wrong-audience"), "The expected audience is sent to Valve for verification.")

	host.next_reply = _reply(200, '{"response":{"params":{"result":"OK","steamid":%s,"ownersteamid":"%s"}}}' % [PLAYER_ID, OWNER_ID])
	var verified: SteamIdentityResult = await auth.verify(ticket, AUDIENCE)
	if not verified.ok or verified.value == null:
		_check(false, "The valid mock Valve reply issues a typed identity.")
		host.free()
		quit(1)
		return
	var player := verified.value
	_check(player.steam_id == PLAYER_ID and player.license_owner_steam_id == OWNER_ID and player.app_id == APP_ID,
		"Verified identity preserves exact numeric Steam IDs and the boot-time AppID.")
	_check(host.last_host == "partner.steam-api.com" and host.last_method == "GET" and host.last_authenticated
		and host.last_target.begins_with("/ISteamUserAuth/AuthenticateUserTicket/v1/?")
		and host.last_target.contains("appid=%d" % APP_ID) and host.last_target.contains("ticket=0100ff")
		and host.last_target.contains("identity=transfer%20hall%2Fprofile%26test")
		and not host.last_target.contains("key=") and host.last_body.is_empty(),
		"Ticket verification uses the fixed host, exact encoded parameters and header-only key.")

	host.next_reply = _reply(200, '{"response":{"params":{"result":"OK","steamid":"0001"}}}')
	var malformed: SteamIdentityResult = await auth.verify(ticket, AUDIENCE)
	_check(malformed.error == ERR_INVALID_DATA and malformed.value == null, "A noncanonical ID cannot mint an identity.")

	host.next_reply = _reply(200, '{"appownership":{"ownsapp":true,"permanent":false,"timestamp":"2025-01-01T00:00:00Z","timeexpires":"never","ownersteamid":"%s","sitelicense":false,"usercanceled":false}}' % OWNER_ID)
	var owned: SteamAppOwnershipResult = await auth.owns_app(player, APP_ID + 1)
	_check(owned.ok and owned.value.owns_app and not owned.value.permanent
		and owned.value.app_id == APP_ID + 1 and owned.value.license_owner_steam_id == OWNER_ID,
		"Ownership preserves Valve's active and permanent-license distinction for the requested app.")
	_check(host.last_target.begins_with("/ISteamUser/CheckAppOwnership/v4/?")
		and host.last_target.contains("steamid=" + PLAYER_ID), "Ownership uses the exact verified player ID.")

	host.next_reply = _reply(200, '{"appownership":{"ownsapp":false,"ownersteamid":"0"}}')
	var unowned: SteamAppOwnershipResult = await auth.owns_app(player, APP_ID + 1)
	_check(unowned.ok and not unowned.value.owns_app and unowned.value.license_owner_steam_id.is_empty(),
		"A valid negative license lookup remains distinct from a transport or authentication error.")

	settings.app_id = APP_ID
	var other := SteamPublisher.boot(settings).value
	var before := host.request_count
	var forged: SteamAppOwnershipResult = await auth.owns_app(SteamIdentity.new(), APP_ID)
	var foreign: SteamLeaderboardSubmissionResult = await other.leaderboards().submit_score(player, 7, 19)
	_check(forged.error == ERR_UNAUTHORIZED and foreign.error == ERR_UNAUTHORIZED and host.request_count == before,
		"Forged identities and identities issued by another publisher cannot reach Steam.")

	host.next_reply = _reply(200, '{"response":{}}')
	var score: SteamLeaderboardSubmissionResult = await publisher.leaderboards().submit_score(player, 7, -19, PackedByteArray([0, 255, 38]))
	_check(score.ok and score.value.submitted_score == -19 and score.value.upload == null
		and score.value.replay_state == SteamLeaderboardSubmission.NONE,
		"Publisher score acknowledgment keeps the requested score without inventing rank or change.")
	_check(host.last_method == "POST" and host.last_target == "/ISteamLeaderboards/SetLeaderboardScore/v1/"
		and host.last_body.contains("scoremethod=KeepBest") and host.last_body.contains("details=%00%FF%26")
		and host.last_body.contains("steamid=" + PLAYER_ID) and not host.last_body.contains("key="),
		"Trusted score submission form preserves binary details and omits credentials.")
	host.next_reply = _reply(403, "private vendor response")
	var forbidden: SteamLeaderboardSubmissionResult = await publisher.leaderboards().submit_score(player, 7, -19)
	_check(forbidden.error == ERR_UNAUTHORIZED and forbidden.value == null
		and not forbidden.message.contains("private vendor"), "HTTP refusal is redacted and returns no score receipt.")

	host.next_reply = _reply(200, '{"response":{}}', 2)
	var pending: Variant = publisher.leaderboards().submit_score(player, 7, -19)
	publisher.close()
	var closed: SteamLeaderboardSubmissionResult = await pending
	_check(closed.error == ERR_SKIP and not publisher.is_ready() and publisher.auth() == null,
		"Closing the exact publisher ends pending observations and retires its services.")

	settings.publisher_key_ref = &"missing_fixture_key"
	var unconfigured := SteamPublisher.boot(settings).value
	before = host.request_count
	var no_secret: SteamIdentityResult = await unconfigured.auth().verify(ticket, AUDIENCE)
	_check(no_secret.error == ERR_UNCONFIGURED and host.request_count == before,
		"An unresolved credential fails before sending an HTTP request.")

	settings.publisher_key_ref = &""
	_check(SteamPublisher.boot(settings).error == ERR_INVALID_PARAMETER,
		"Trusted boot requires at least one configured credential reference.")
	settings.encrypted_ticket_key_ref = &"encrypted_ticket_fixture"
	var offline_boot := SteamPublisher.boot(settings)
	if offline_boot.ok:
		var secret_reads := host.secret_reads
		var no_web_key: SteamIdentityResult = await offline_boot.value.auth().verify(ticket, AUDIENCE)
		_check(no_web_key.error == ERR_UNCONFIGURED and host.secret_reads == secret_reads and host.request_count == before,
			"Encrypted-only boot does not resolve or send a missing partner Web API credential.")
	else:
		_check(false, "An encrypted-only trusted publisher boots without a Web API credential.")

	root.remove_child(host)
	root.add_child(host)
	_check(not other.is_ready(), "Reattaching the same host cannot revive a previous issuer generation.")
	host.free()
	if _failures == 0:
		print("CRIMSON_STEAM_PUBLISHER_PASSED mock")
	quit(0 if _failures == 0 else 1)


## Encode a synthetic response without passing any actual Steam credential.
func _reply(status: int, body: String, ticks: int = 1) -> Dictionary:
	return {"status": status, "body": body.to_utf8_buffer(), "ticks": ticks}


## Report assertion names without dumping secrets, tickets, vendor responses, or account IDs.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
