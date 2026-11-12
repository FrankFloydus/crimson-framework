# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises trusted moderation through the publisher build's local no-socket fixture.
extends SceneTree

## Synthetic app and public-format identities used only inside the fixture.
const APP_ID := 12345
const REPORTER_ID := "76561198000000001"
const PLAYER_ID := "76561198000000002"
const REPORT_ID := "18446744073709551600"
const START_UNIX := 1700000000

## Number of failed assertions in the current process.
var _failures := 0


## Records policy decisions before local transport dispatch and observations after settlement.
class ReviewPolicy extends SteamModerationPolicy:
	## Controls authorization of all operations for denial tests.
	var allow := true
	## Returns an approval tied to an older immutable request when requested.
	var reuse_approval := false
	## Deliberately substitutes a different player in removal authorization.
	var wrong_removal := false
	## Simulates a failed terminal audit write while keeping the observed outcome available.
	var reject_audit_write := false
	## Closes this scope from inside authorization to test generation revalidation.
	var retire_on_review: SteamPublisher
	## Exact requests considered by the policy.
	var considered: Array[SteamModerationRequest] = []
	## Audit references reserved before returning sanction authorization.
	var reserved: PackedStringArray = []
	## Detached audited receipts observed on settlement.
	var receipts: Array[SteamModerationReceipt] = []
	## Terminal error associated with each observed receipt.
	var outcomes: Array[int] = []

	## Simulate trusted match eligibility, review lookup, and a durable audit reservation.
	func _authorize(request: SteamModerationRequest) -> SteamModerationAuthorization:
		considered.append(request)
		if not allow or request.app_id != APP_ID:
			return null
		if reuse_approval and considered.size() > 1:
			return SteamModerationAuthorization.approve(considered[-2])
		match request.action:
			SteamModerationRequest.REPORT:
				if request.evidence.match_id != "fixture-match" or request.player_id != PLAYER_ID:
					return null
				return SteamModerationAuthorization.approve(request)
			SteamModerationRequest.READ_REPORTS:
				return SteamModerationAuthorization.approve(request)
			SteamModerationRequest.BAN, SteamModerationRequest.SUSPEND:
				if request.report_id != REPORT_ID:
					return null
		var audit := "fixture-audit-%d" % (reserved.size() + 1)
		reserved.append(audit)
		if retire_on_review != null:
			var publisher := retire_on_review
			retire_on_review = null
			publisher.close()
		var player := REPORTER_ID if wrong_removal else PLAYER_ID
		return SteamModerationAuthorization.approve(request, player, audit)

	## Record the typed result of an already reserved moderation attempt.
	func _record_outcome(receipt: SteamModerationReceipt, error: Error) -> bool:
		receipts.append(receipt)
		outcomes.append(error)
		return not reject_audit_write


## Supplies one synthetic reply and records only fixture-local request details.
class FixtureHost extends SteamPublisherHost:
	## Total calls that reached the local fake transport.
	var request_count := 0
	## Most recent method and path accepted by the fixed route table.
	var last_method := ""
	var last_target := ""
	## Most recent POST form, or empty for a GET.
	var last_form := ""
	## Whether a trusted secret reference was resolved without exposing its value.
	var authenticated := false
	## Most recent audit reservation count at the time transport accepted a request.
	var audits_at_dispatch := 0
	## Trusted fake review policy observed by the transport fixture.
	var policy: ReviewPolicy
	## Next reply status, bytes, and local ticks.
	var next_reply: Dictionary

	## Resolve a dummy fixture value. This build never opens a network socket.
	func _resolve_secret(reference: StringName) -> String:
		return "fixture-token" if reference == &"fixture_only" else ""

	## Record the fixed partner route and return the configured local reply.
	func _test_response(method: String, _host: String, target: String, form: String, authorized: bool) -> Dictionary:
		request_count += 1
		last_method = method
		last_target = target
		last_form = form
		authenticated = authorized
		audits_at_dispatch = policy.reserved.size() if policy != null else 0
		return next_reply


## Defer initialization until the scene root is ready.
func _initialize() -> void:
	_run.call_deferred()


## Run report, read, review, and cancellation checks without a Steam runtime or real credential.
func _run() -> void:
	var host := FixtureHost.new()
	if not host.has_method("_fixture_transport_enabled") or not host.call("_fixture_transport_enabled"):
		push_error("Moderation fixtures require the publisher test transport build.")
		host.free()
		quit(2)
		return
	root.add_child(host)
	var policy := ReviewPolicy.new()
	host.policy = policy
	var settings := SteamPublisherSettings.new()
	settings.app_id = APP_ID
	settings.host = host
	settings.publisher_key_ref = &"fixture_only"
	settings.moderation_policy = policy
	var opened := SteamPublisher.boot(settings)
	if not opened.ok:
		_check(false, "The test publisher opens on its attached fixture host.")
		host.free()
		quit(1)
		return

	var publisher := opened.value
	await _check_report(host, publisher, settings)
	await _check_pages(host, publisher.moderation(), policy)
	await _check_review(host, publisher, policy)
	await _check_retirement(host, settings, policy)
	publisher.close()
	host.free()
	if _failures == 0:
		print("CRIMSON_STEAM_MODERATION_PASSED mock")
	quit(0 if _failures == 0 else 1)


## Confirm exact reporter issuance, policy authorization, and lossless Valve report IDs.
func _check_report(host: FixtureHost, publisher: SteamPublisher, settings: SteamPublisherSettings) -> void:
	var moderation := publisher.moderation()
	var detached: SteamCheatReportResult = await SteamPublisherModeration.new().report(PLAYER_ID,
		SteamCheatEvidence.server("fixture-match", 9))
	_check(detached.error == ERR_UNAVAILABLE, "Detached moderation returns a typed unavailable result.")
	var count := host.request_count
	var forged: SteamCheatReportResult = await moderation.report(PLAYER_ID,
		SteamCheatEvidence.player("fixture-match", 9, SteamIdentity.new()))
	_check(forged.error == ERR_UNAUTHORIZED and host.request_count == count,
		"A forged player identity cannot reach the transport.")
	host.next_reply = _reply(200, '{"response":{"params":{"result":"OK","steamid":%s,"appid":%d}}}' %
		[REPORTER_ID, APP_ID])
	var identity: SteamIdentityResult = await publisher.auth().verify(PackedByteArray([1, 2, 3]), "fixture-audience")
	if not identity.ok:
		_check(false, "The synthetic verified reporter identity is available.")
		return
	var evidence := SteamCheatEvidence.player("fixture-match", 9, identity.value)
	var other := SteamPublisher.boot(settings).value
	count = host.request_count
	var foreign: SteamCheatReportResult = await other.moderation().report(PLAYER_ID, evidence)
	_check(foreign.error == ERR_UNAUTHORIZED and host.request_count == count,
		"A report cannot use an identity minted by a sibling publisher.")
	other.close()

	host.policy.allow = false
	var denied: SteamCheatReportResult = await moderation.report(PLAYER_ID, evidence)
	_check(denied.error == ERR_UNAUTHORIZED and host.request_count == count,
		"The trusted policy can deny an otherwise authenticated report before dispatch.")
	host.policy.allow = true
	host.next_reply = _reply(200, '{"response":{"reportid":%s,"steamid":%s,"appid":%d}}' %
		[REPORT_ID, PLAYER_ID, APP_ID])
	var accepted: SteamCheatReportResult = await moderation.report(PLAYER_ID, evidence)
	_check(accepted.ok and accepted.value.report_id == REPORT_ID and accepted.value.player_id == PLAYER_ID and
		accepted.value.match_id == evidence.match_id and accepted.value.reason_code == 9,
		"The typed report preserves its exact unsigned Valve ID and trusted local match context.")
	var payload := _payload(host)
	_check(host.last_method == "POST" and host.last_target == "/ICheatReportingService/ReportPlayerCheating/v1/" and
		host.authenticated and payload.contains('"steamidreporter":' + REPORTER_ID) and
		payload.contains('"appdata":9') and not payload.contains("fixture-match") and not host.last_form.contains("key="),
		"Report dispatch uses the fixed route, exact numeric reporter, and app reason code.")
	_check(host.policy.receipts.is_empty(), "Reporting alone creates no sanction or audit receipt.")


## Verify exact cursor arithmetic, read-only pagination, and fail-closed list parsing.
func _check_pages(host: FixtureHost, moderation: SteamPublisherModeration, policy: ReviewPolicy) -> void:
	var count := host.request_count
	policy.reuse_approval = true
	var stale: SteamCheatReportPageResult = await moderation.reports(START_UNIX, START_UNIX + 60)
	_check(stale.error == ERR_UNAUTHORIZED and host.request_count == count,
		"An approval cannot be reused for a different immutable request.")
	policy.reuse_approval = false
	var invalid: SteamCheatReportPageResult = await moderation.reports(START_UNIX, START_UNIX + 604801)
	_check(invalid.error == ERR_INVALID_PARAMETER and host.request_count == count,
		"A report window beyond seven days fails before dispatch.")
	var exhausted: SteamCheatReportPageResult = await moderation.reports(START_UNIX, START_UNIX + 60,
		"18446744073709551615")
	_check(exhausted.ok and exhausted.value.reports.is_empty() and host.request_count == count,
		"The largest unsigned cursor yields an empty authorized result without overflow.")
	host.next_reply = _reply(200, '{"response":{"reports":[]}}')
	var empty: SteamCheatReportPageResult = await moderation.reports(START_UNIX, START_UNIX + 60, REPORT_ID)
	_check(empty.ok and empty.value.reports.is_empty() and empty.value.next_report_id.is_empty(),
		"An empty provider page supplies no invented continuation.")
	_check(host.last_method == "GET" and host.last_target.begins_with("/ICheatReportingService/GetCheatingReports/v1/?") and
		_payload(host).contains('"reportidmin":18446744073709551601'),
		"The exclusive public cursor maps to the exact inclusive Steam minimum.")
	var rows := PackedStringArray()
	for index in range(64, -1, -1):
		rows.append('{"reportid":%d,"steamid":%s,"timecreated":%d}' %
			[9007199254740993 + index, PLAYER_ID, START_UNIX + 1])
	host.next_reply = _reply(200, '{"response":{"reports":[%s]}}' % ",".join(rows))
	var page: SteamCheatReportPageResult = await moderation.reports(START_UNIX, START_UNIX + 60)
	_check(page.ok and page.value.reports.size() == 64 and page.value.reports.is_read_only() and
		page.value.reports[0].report_id == "9007199254740993" and page.value.next_report_id == "9007199254741056",
		"A locally truncated page preserves sorted unsigned IDs and a real continuation.")
	host.next_reply = _reply(200, '{"response":{"reports":[%s,%s]}}' % [rows[0], rows[0]])
	var malformed: SteamCheatReportPageResult = await moderation.reports(START_UNIX, START_UNIX + 60)
	_check(malformed.error == ERR_INVALID_DATA and malformed.value == null,
		"A malformed page with duplicate report IDs fails closed.")


## Verify reviewed player binding, exact sanction requests, and outcome auditing.
func _check_review(host: FixtureHost, publisher: SteamPublisher, policy: ReviewPolicy) -> void:
	var moderation := publisher.moderation()
	var count := host.request_count
	var absent: SteamModerationReceiptResult = await moderation.ban("73", "Reviewed fixture evidence.")
	var reviews := policy.considered.size()
	var zero: SteamModerationReceiptResult = await moderation.suspend(REPORT_ID, 0, "Fixture duration.")
	var year: SteamModerationReceiptResult = await moderation.suspend(REPORT_ID, 31536000, "Fixture duration.")
	_check(absent.error == ERR_UNAUTHORIZED and zero.error == ERR_INVALID_PARAMETER and year.error == ERR_INVALID_PARAMETER
		and host.request_count == count and policy.considered.size() == reviews,
		"An unreviewed report and invalid duration never dispatch a sanction. Zero and one year stay outside suspension policy.")
	host.next_reply = _reply(200, '{"response":{"success":true,"steamid":%s,"reportid":%s}}' % [PLAYER_ID, REPORT_ID])
	var ban_result: SteamModerationReceiptResult = await moderation.ban(REPORT_ID, "Reviewed fixture evidence.")
	var payload := _payload(host)
	_check(ban_result.ok and ban_result.value.acknowledged and ban_result.value.player_id == PLAYER_ID and
		ban_result.value.audit_id == policy.reserved[-1] and policy.receipts[-1] == ban_result.value and
		host.audits_at_dispatch == policy.reserved.size(),
		"Ban uses the reviewed player and records the outcome of the prior audit reservation.")
	_check(host.last_method == "POST" and host.last_target == "/ICheatReportingService/RequestPlayerGameBan/v1/" and
		payload.contains('"steamid":' + PLAYER_ID) and payload.contains('"reportid":' + REPORT_ID) and
		payload.contains('"duration":0'), "Permanent ban uses the fixed route and Valve's zero duration.")

	host.next_reply = _reply(200, '{"response":{"success":false}}')
	var refused: SteamModerationReceiptResult = await moderation.suspend(REPORT_ID, 60, "Fixture suspension.")
	_check(refused.error == ERR_QUERY_FAILED and refused.value != null and not refused.value.acknowledged and
		refused.value.duration_seconds == 60 and policy.outcomes[-1] == ERR_QUERY_FAILED and
		_payload(host).contains('"duration":60'), "A rejected suspension keeps its typed audited attempt.")
	host.next_reply = _reply(200, '{"response":{"success":true}}')
	var maximum: SteamModerationReceiptResult = await moderation.suspend(REPORT_ID, 31535999, "Fixture suspension.")
	_check(maximum.ok and maximum.value.duration_seconds == 31535999 and
		policy.considered[-1].action == SteamModerationRequest.SUSPEND and _payload(host).contains('"duration":31535999'),
		"The last second below one year reaches Steam under the suspension policy.")

	policy.wrong_removal = true
	count = host.request_count
	var wrong: SteamModerationReceiptResult = await moderation.remove_ban(PLAYER_ID, "Fixture correction.")
	_check(wrong.error == ERR_UNAUTHORIZED and host.request_count == count,
		"Removal authorization cannot substitute another player.")
	policy.wrong_removal = false
	host.next_reply = _reply(200, '{"response":{}}')
	var unrecognized: SteamModerationReceiptResult = await moderation.remove_ban(PLAYER_ID, "Fixture correction.")
	_check(unrecognized.error == ERR_INVALID_DATA and unrecognized.value != null and not unrecognized.value.acknowledged,
		"An unknown response cannot become a successful sanction receipt.")
	host.next_reply = _reply(200, '{"response":{"success":true}}')
	var removed: SteamModerationReceiptResult = await moderation.remove_ban(PLAYER_ID, "Fixture correction.")
	_check(removed.ok and removed.value.acknowledged and removed.value.action == SteamModerationRequest.REMOVE_BAN and
		host.last_target == "/ICheatReportingService/RemovePlayerGameBan/v1/" and
		not _payload(host).contains("Fixture correction"), "Removal submits the authorized AppID and player only.")
	policy.reject_audit_write = true
	var unrecorded: SteamModerationReceiptResult = await moderation.remove_ban(PLAYER_ID, "Fixture audit failure.")
	_check(unrecorded.error == ERR_DATABASE_CANT_WRITE and unrecorded.value != null and
		unrecorded.value.acknowledged and policy.outcomes[-1] == OK,
		"A terminal audit failure reports a store error while retaining the Steam acknowledgment.")
	policy.reject_audit_write = false


## Verify scope retirement during authorization and during a pending observed request.
func _check_retirement(host: FixtureHost, settings: SteamPublisherSettings, policy: ReviewPolicy) -> void:
	var publisher := SteamPublisher.boot(settings).value
	policy.retire_on_review = publisher
	var count := host.request_count
	var skipped: SteamModerationReceiptResult = await publisher.moderation().ban(REPORT_ID, "Fixture stale scope.")
	_check(skipped.error == ERR_SKIP and skipped.value != null and host.request_count == count and
		policy.outcomes[-1] == ERR_SKIP, "Reentrant scope retirement prevents dispatch and records the attempt.")

	publisher = SteamPublisher.boot(settings).value
	host.next_reply = _reply(200, '{"response":{"success":true}}', 3)
	var pending: Variant = publisher.moderation().ban(REPORT_ID, "Fixture closed scope.")
	publisher.close()
	var closed: SteamModerationReceiptResult = await pending
	_check(closed.error == ERR_SKIP and closed.value != null and not closed.value.acknowledged and
		policy.outcomes[-1] == ERR_SKIP, "A closed publisher settles its pending audited observation once.")


## Decode only the fixture's form payload, retaining exact decimal JSON integer tokens.
func _payload(host: FixtureHost) -> String:
	var form := host.last_form if host.last_method == "POST" else host.last_target.get_slice("?", 1)
	for field in form.split("&"):
		if field.begins_with("input_json="):
			return field.trim_prefix("input_json=").uri_decode()
	return ""


## Construct a synthetic reply for the no-socket transport.
func _reply(status: int, body: String, ticks: int = 1) -> Dictionary:
	return {"status": status, "body": body.to_utf8_buffer(), "ticks": ticks}


## Log fixture assertion failures without echoing personal or credential values.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
