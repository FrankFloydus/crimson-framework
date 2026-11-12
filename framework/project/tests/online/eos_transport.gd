# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree

const TEST_USER := "0123456789abcdef0123456789abcdef"
const TEST_SOCKET := "Crimson-test"


## Start after Godot has initialized its multiplayer scope.
func _initialize() -> void:
	_run.call_deferred()


## Verify typed routes, immutable payloads, and transport selection without signing in.
func _run() -> void:
	var endpoint := CrimsonEOSSessionTransport.route(TEST_USER, TEST_SOCKET)
	if not _check(
		endpoint != null and endpoint.get_transport() == &"eos"
			and endpoint.get_address().is_empty() and endpoint.get_port() == 0,
		"EOS publishes an opaque endpoint without direct IP fields."
	):
		quit(1)
		return

	var bytes := endpoint.get_route()
	bytes[0] = 2
	var malformed := CrimsonSessionEndpoint.opaque(&"eos", bytes)
	bytes[0] = 3
	var passed := true
	passed = _check(endpoint.get_route()[0] == 1, "Editing the returned route cannot mutate the endpoint.") and passed
	passed = _check(malformed.get_route()[0] == 2, "The opaque constructor copies its input.") and passed
	passed = _check(
		CrimsonEOSSessionTransport.route(TEST_USER, "invalid/socket") == null,
		"Reject a socket name outside the EOS character set."
	) and passed
	passed = _check(
		CrimsonSessionEndpoint.opaque(&"enet", bytes) == null and CrimsonSessionEndpoint.opaque(&"Invalid", bytes) == null,
		"Reserve the ENet tag and validate opaque adapter names."
	) and passed
	passed = _check(
		CrimsonENetSessionTransport.new().open_client(endpoint).get_error() == ERR_INVALID_PARAMETER,
		"Reject an EOS endpoint in the default ENet adapter."
	) and passed

	var session := CrimsonGameSession.new()
	root.add_child(session)
	var expected := ERR_UNAVAILABLE if "disabled" in OS.get_cmdline_user_args() else ERR_UNCONFIGURED
	passed = _check(session.set_transport_adapter(CrimsonEOSSessionTransport.new()) == OK,
		"Select the built-in EOS transport through the normal Session API.") and passed
	passed = _check(session.join_endpoint(malformed) == ERR_INVALID_PARAMETER,
		"Reject a route from an incompatible EOS protocol version.") and passed
	passed = _check(session.join_endpoint(endpoint) == expected and session.get_state() == CrimsonGameSession.IDLE,
		"An unconfigured EOS peer leaves the Session idle.") and passed
	session.free()
	if passed:
		print("CRIMSON_EOS_TRANSPORT_PASSED")
	quit(0 if passed else 1)


## Report a failing assertion to the headless harness.
func _check(condition: bool, message: String) -> bool:
	if not condition:
		push_error(message)
	return condition
