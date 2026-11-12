# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies Steam HTTP through the public client facade against the harness loopback server.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480

## Assertion count reported by the fixture process.
var _failures := 0
## Whether a stopping scope's cancellation observer actually executed.
var _saw_stop := false


## Start after the tree root is ready to own the client scope.
func _initialize() -> void:
	_run.call_deferred()


## Select the test lane and keep the fixture bounded if a callback never arrives.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.is_empty() or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose Steam HTTP fixture mode spacewar, missing, or sdk-off.")
		quit(2)
		return
	var mode := arguments[0]
	if mode == "spacewar" and (arguments.size() < 3 or arguments.size() > 4
			or not arguments[1].begins_with("http://127.0.0.1:")
			or not arguments[2].begins_with("https://127.0.0.1:")
			or (arguments.size() == 4 and not arguments[3].begins_with("https://"))):
		push_error("Steam HTTP Spacewar tests require both harness loopback endpoints.")
		quit(2)
		return
	if mode != "spacewar" and arguments.size() != 1:
		push_error("The unavailable HTTP test accepts only its mode.")
		quit(2)
		return
	create_timer(30.0).timeout.connect(func() -> void:
		push_error("Steam HTTP fixture timed out.")
		quit(1))

	var client := SteamClient.new()
	root.add_child(client)
	_check(client.http() == null, "An unstarted client has no HTTP service.")
	var started := client.start(SPACEWAR)
	if mode == "spacewar":
		if started == OK and client.http() != null and client.http().is_available():
			var trusted := arguments[3] if arguments.size() == 4 else ""
			await _check_requests(client, arguments[1], arguments[2], trusted)
		else:
			_check(false, "The Spacewar client failed to expose Steam HTTP.")
	else:
		var expected := ERR_CANT_CONNECT if mode == "missing" else ERR_UNAVAILABLE
		_check(started == expected and client.http() == null,
			"A client without usable Steam cannot expose HTTP.")
		var detached := SteamHttp.new()
		var result: SteamHttpResponseResult = await detached.fetch("http://127.0.0.1:1")
		_check(not detached.is_available() and result.error == ERR_UNAVAILABLE,
			"An unattached standalone facade fails without opening a request.")
	client.stop()
	client.queue_free()
	await process_frame
	if _failures == 0:
		print("CRIMSON_STEAM_HTTP_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Verify complete bytes, status, option branching, bounds, and explicit cancellation.
func _check_requests(client: SteamClient, base: String, untrusted_https: String, trusted_https: String) -> void:
	var http := client.http()
	var request := http.request(base + "/ok")
	var rejected: SteamHttpResponseResult = await request.with_timeout(0).send().wait()
	_check(rejected.error == ERR_INVALID_PARAMETER and rejected.value == null,
		"An invalid branch settles before opening the request.")
	var forbidden: SteamHttpResponseResult = await request.with_header("Content-Length", "42").send().wait()
	_check(forbidden.error == ERR_INVALID_PARAMETER, "Steam HTTP owns request framing.")
	var control: SteamHttpResponseResult = await request.with_header("X-Crimson-Test", "line\nline").send().wait()
	_check(control.error == ERR_INVALID_PARAMETER, "Header values cannot contain control characters.")
	var bad_scheme: SteamHttpResponseResult = await http.fetch("file:///test")
	_check(bad_scheme.error == ERR_INVALID_PARAMETER, "Requests require an absolute HTTP(S) URL.")

	var fetched: SteamHttpResponseResult = await http.fetch(base + "/ok")
	_check_response(_matches(fetched, 200, "crimson-http-ok") and fetched.value.is_successful(),
		"Awaiting fetch directly returns the typed response.", fetched)

	var certificate_error: SteamHttpResponseResult = await http.with_timeout(4000).fetch(untrusted_https + "/ok")
	_check(certificate_error.error == ERR_CANT_CONNECT and certificate_error.value == null,
		"Steam rejects a self-signed loopback certificate.")
	if not trusted_https.is_empty():
		var secure: SteamHttpResponseResult = await http.with_timeout(6000).fetch(trusted_https)
		_check(secure.ok and secure.value != null and secure.value.is_successful(),
			"Steam accepts the configured endpoint's trusted certificate and returns a 2xx response.")

	var opened := request.send()
	_check(not opened.is_finished() and opened.result() == null, "A submitted GET starts pending.")
	var result: SteamHttpResponseResult = await opened.wait()
	_check_response(_matches(result, 200, "crimson-http-ok") and result.value.is_successful(),
		"GET returns the expected 2xx status and complete bytes.", result)
	var settled_again: SteamHttpResponseResult = await opened.wait()
	_check(settled_again == result and opened.result() == result and opened.is_finished(),
		"Waiting after completion returns the same immutable response.")
	var copied := result.value.get_body() if result.value != null else PackedByteArray()
	if not copied.is_empty():
		copied[0] = 0
		_check(_matches(result, 200, "crimson-http-ok"), "Response bytes are detached from caller edits.")

	var status: SteamHttpResponseResult = await http.request(base + "/status").send().wait()
	_check_response(_matches(status, 418, "crimson-http-status") and not status.value.is_successful(),
		"An HTTP error response retains its status and body without becoming a transport error.", status)
	var dropped: SteamHttpResponseResult = await http.request(base + "/abort").with_timeout(5000).send().wait()
	_check(dropped.error == ERR_CANT_CONNECT and dropped.value == null,
		"A dropped connection without a response reports a transport failure.")
	var bytes := "crimson-http-echo".to_utf8_buffer()
	var echo: SteamHttpResponseResult = await http.request(base + "/echo", SteamHttp.METHOD_POST) \
		.with_header("X-Crimson-Test", "scoped").with_body(bytes, "application/octet-stream").send().wait()
	_check_response(echo.error == OK and echo.value.get_status() == 200 and echo.value.get_body() == bytes,
		"POST sends the chosen header and raw body to the loopback endpoint.", echo)

	var large := http.request(base + "/large")
	var bounded: SteamHttpResponseResult = await large.with_max_response_bytes(4096).send().wait()
	_check_response(bounded.error == ERR_PARAMETER_RANGE_ERROR and bounded.value == null,
		"An oversized response fails without publishing partial bytes.", bounded)
	var unmodified: SteamHttpResponseResult = await large.send().wait()
	_check_response(unmodified.ok and unmodified.value.is_successful() and unmodified.value.get_body().size() == 16384,
		"The base request retains its original byte limit.", unmodified)
	await _check_options_and_json(http, base)

	var delayed: SteamHttpResponseResult = await http.request(base + "/slow").with_timeout(300).send().wait()
	_check(delayed.error == ERR_TIMEOUT and delayed.value == null,
		"A stalled response expires with a transport timeout and no HTTP status.")
	var cancelled := http.request(base + "/slow").send()
	_check(cancelled.cancel() == OK and cancelled.cancel() == ERR_DOES_NOT_EXIST,
		"Cancellation settles the native request exactly once.")
	var cancellation: SteamHttpResponseResult = await cancelled.wait()
	_check(cancellation.error == ERR_SKIP and cancellation.value == null,
		"An explicitly cancelled request has no HTTP response.")

	var active: Array[SteamHttpResponseOperation] = []
	for _index in range(8):
		active.append(http.request(base + "/slow").send())
	var saturated: SteamHttpResponseResult = await http.request(base + "/slow").send().wait()
	_check(saturated.error == ERR_BUSY, "One facade cannot exceed its eight-request budget.")
	for operation in active:
		_check(operation.cancel() == OK, "Cancellation returns each occupied native request slot.")

	await _check_scope_stop(client, base, request)


## Exercise identical direct terminals, reusable options, captured metadata, and typed JSON.
func _check_options_and_json(http: SteamHttp, base: String) -> void:
	var json_url := base + "/json"
	var selected := PackedStringArray(["X-Fixture-Accept", "x-fixture-accept", "X-Fixture-Missing"])
	var configured := http.accept_json().with_timeout(3000).with_response_headers(selected)
	selected[0] = "x-altered"
	var rotation: SteamHttpResponseResult = await configured.fetch(json_url)
	_check_response(_has_status(rotation, 200) and rotation.value.header("X-FIXTURE-ACCEPT") == "json"
		and rotation.value.header("Content-Type") == "application/json"
		and rotation.value.header("x-fixture-missing").is_empty(),
		"The configured terminal requests JSON and captures selected headers once.", rotation)
	if rotation.ok:
		var parsed := rotation.value.json()
		_check(parsed.ok and parsed.value is Dictionary and parsed.value.get("mode") == "rotation",
			"A JSON object produces a typed successful parse result.")

	var overwritten: SteamHttpResponseResult = await configured.with_header("ACCEPT", "text/plain").fetch(json_url)
	_check_response(_has_status(overwritten, 200) and overwritten.value.header("x-fixture-accept") == "other",
		"A branch may replace Accept using a differently cased header name.", overwritten)
	var draft_result: SteamHttpResponseResult = await configured.request(json_url).send().wait()
	_check_response(_has_status(draft_result, 200) and draft_result.value.header("x-fixture-accept") == "json",
		"The original options keep their Accept value and configure an explicit request.", draft_result)
	var default_result: SteamHttpResponseResult = await http.fetch(json_url)
	_check_response(_has_status(default_result, 200) and default_result.value.header("x-fixture-accept").is_empty()
		and default_result.value.header("content-type") == "application/json",
		"Default fetch captures Content-Type and skips unselected fields.", default_result)

	var head_result: SteamHttpResponseResult = await configured.head(json_url)
	_check_response(_has_status(head_result, 200) and head_result.value.get_body().is_empty()
		and head_result.value.header("x-fixture-accept") == "json",
		"HEAD captures metadata while returning an empty body.", head_result)
	var failed_status: SteamHttpResponseResult = await http.fetch(base + "/unavailable")
	_check_response(_matches(failed_status, 503, "crimson-http-unavailable") and not failed_status.value.is_successful(),
		"A 503 response retains the complete HTTP exchange.", failed_status)
	var null_result: SteamHttpResponseResult = await http.fetch(base + "/json-null")
	if null_result.ok:
		var decoded_null := null_result.value.json()
		_check(decoded_null.ok and decoded_null.value == null, "A valid JSON null is distinct from parse failure.")
	else:
		_check_response(false, "The JSON null route must complete.", null_result)
	var invalid_json: SteamHttpResponseResult = await http.fetch(base + "/ok")
	_check_response(_has_status(invalid_json, 200) and invalid_json.value.json().error == ERR_PARSE_ERROR,
		"A JSON parse failure leaves the transport result successful.", invalid_json)
	var nul_json: SteamHttpResponseResult = await http.fetch(base + "/json-nul")
	_check_response(_has_status(nul_json, 200) and nul_json.value.json().error == ERR_PARSE_ERROR,
		"A raw NUL cannot hide bytes after a valid JSON prefix.", nul_json)

	var oversized_header: SteamHttpResponseResult = await http.with_response_headers(
		PackedStringArray(["X-Fixture-Large"])).fetch(base + "/large-header")
	_check(oversized_header.error == ERR_PARAMETER_RANGE_ERROR and oversized_header.value == null,
		"An explicitly captured oversized header settles as a limit error.")
	var unselected_header: SteamHttpResponseResult = await http.fetch(base + "/large-header")
	_check_response(_has_status(unselected_header, 200) and unselected_header.value.header("x-fixture-large").is_empty(),
		"Unselected header values are skipped.", unselected_header)
	var invalid_selection: SteamHttpResponseResult = await http.with_response_headers(
		PackedStringArray(["invalid\nname"])).fetch(json_url)
	_check(invalid_selection.error == ERR_INVALID_PARAMETER and invalid_selection.value == null,
		"Response selectors reject invalid field names before submission.")

	var echo := configured.with_response_headers(PackedStringArray(["X-Fixture-Method", "Allow"]))
	var payload := {"mode": "scoped"}
	var posted: SteamHttpResponseResult = await echo.post(json_url, payload)
	_check_json_write(posted, "POST")
	var updated: SteamHttpResponseResult = await echo.put(json_url, payload)
	_check_json_write(updated, "PUT")
	var patched: SteamHttpResponseResult = await echo.patch(json_url, payload)
	_check_json_write(patched, "PATCH")
	var deleted: SteamHttpResponseResult = await echo.delete(json_url)
	_check_response(_has_status(deleted, 200) and deleted.value.header("x-fixture-method") == "DELETE",
		"The DELETE terminal submits the selected method.", deleted)
	var methods: SteamHttpResponseResult = await echo.options(json_url)
	_check_response(_has_status(methods, 204) and methods.value.header("allow").contains("PATCH"),
		"The OPTIONS terminal returns selected response metadata.", methods)


## Check method, content type, and object bytes echoed by the loopback server.
func _check_json_write(result: SteamHttpResponseResult, expected_method: String) -> void:
	if not _has_status(result, 200):
		_check_response(false, "A JSON write must complete.", result)
		return
	var echoed := result.value.json()
	_check(result.value.header("x-fixture-method") == expected_method and echoed.ok
		and echoed.value is Dictionary and echoed.value.get("mode") == "scoped",
		"The configured JSON terminal sends the correct method and object.")


## Stop one client with requests in flight, retain old handles, and restart inside its callback.
func _check_scope_stop(client: SteamClient, base: String, old_request: SteamHttpRequest) -> void:
	var second := SteamClient.new()
	root.add_child(second)
	if second.start(SPACEWAR) != OK or second.http() == null or not second.http().is_available():
		_check(false, "A second standalone client could not attach to the shared runtime.")
		second.queue_free()
		return

	var old_http := client.http()
	var old_options := old_http.accept_json().with_timeout(3000)
	var old_cloud := client.cloud()
	_check(old_cloud != null and old_cloud.is_available(), "The original scope has active Cloud services.")
	var pending := old_http.request(base + "/slow").send()
	var other := old_http.request(base + "/slow").send()
	pending.completed.connect(func(result: SteamHttpResponseResult) -> void:
		_saw_stop = true
		_check(result.error == ERR_SKIP and not old_http.is_available() and client.http() == null,
			"Client stop invalidates the HTTP service before notifying.")
		_check(old_cloud != null and not old_cloud.is_available(),
			"HTTP cancellation listeners cannot use Cloud from the retiring client scope.")
		var stale := old_request.send()
		_check(stale.is_finished() and stale.result().error == ERR_UNAVAILABLE,
			"A retained request cannot submit through a retiring client.")
		var stale_branch := old_options.request(base + "/ok").send()
		_check(stale_branch.is_finished() and stale_branch.result().error == ERR_UNAVAILABLE,
			"A retained options branch cannot submit through a retiring client.")
		_check(client.start(SPACEWAR) == OK and client.http() != null and client.http() != old_http,
			"A cancellation listener may restart its client with a fresh service scope."),
		CONNECT_ONE_SHOT)
	client.stop()
	_check(_saw_stop and other.is_finished() and other.result().error == ERR_SKIP,
		"Stopping settles every request owned by the old HTTP scope.")
	_check(not old_http.is_available() and second.is_ready() and second.http().is_available(),
		"Stopping one client preserves the other runtime lease.")
	if client.http() != null:
		var restarted: SteamHttpResponseResult = await client.http().request(base + "/ok").send().wait()
		_check(_matches(restarted, 200, "crimson-http-ok"), "The restarted client can issue a fresh request.")
	var surviving: SteamHttpResponseResult = await second.http().request(base + "/ok").send().wait()
	_check(_matches(surviving, 200, "crimson-http-ok"), "The second client's request completes after teardown.")
	second.stop()
	second.queue_free()


## Match a complete loopback response without logging its data.
func _matches(response: SteamHttpResponseResult, status: int, body: String) -> bool:
	return _has_status(response, status) and response.value.get_body() == body.to_utf8_buffer()


## Check the typed outcome and HTTP code without dereferencing a failed result.
func _has_status(response: SteamHttpResponseResult, status: int) -> bool:
	return response != null and response.ok and response.value != null and response.value.get_status() == status


## Include only numeric response metadata on a request assertion failure.
func _check_response(condition: bool, message: String, response: SteamHttpResponseResult) -> void:
	if not condition:
		var value := response.value if response != null else null
		_check(false, "%s Error %d, HTTP %d, %d bytes." %
			[message, response.error if response != null else ERR_UNCONFIGURED,
				value.get_status() if value != null else 0, value.get_body().size() if value != null else 0])


## Report failures without printing request URLs, response data, or account identifiers.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
