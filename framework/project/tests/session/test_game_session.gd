# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Runs offline and loopback tests for the native game session.
extends SceneTree


## Groups a scene branch, its multiplayer API, and the session events it receives.
class Endpoint:
	## Scene tree branch that contains the session.
	var branch: Node
	## Multiplayer API associated with the branch.
	var api: SceneMultiplayer
	## Native session under test.
	var session: GameSession
	## Session state transitions in signal order.
	var states: Array[int] = []
	## Session states reported when a session becomes active.
	var started_states: Array[int] = []
	## Local peer IDs reported by the corresponding start events.
	var started_ids: Array[int] = []
	## Session termination reasons in signal order.
	var ended: Array[StringName] = []
	## Peer IDs reported as admitted to the session.
	var joined: Array[int] = []
	## Peer IDs reported as disconnected from the session.
	var left: Array[int] = []


## Returns a precise factory failure or replaces the API slot while opening a real server.
class FactoryProbe extends SessionTransport:
	var api: SceneMultiplayer
	var replacement: MultiplayerPeer
	var candidate: MultiplayerPeer

	func _open_server(port: int, remote_limit: int, bind_address: String) -> SessionTransportResult:
		var result := ENetSessionTransport.new().open_server(port, remote_limit, bind_address)
		candidate = result.get_peer()
		api.multiplayer_peer = replacement
		return result

	func _open_client(_endpoint: SessionEndpoint) -> SessionTransportResult:
		return SessionTransportResult.failed(ERR_CANT_CONNECT)


## Verifies a fixture-only challenge response and can hold a valid request for async completion.
class AuthProbe extends SessionAuthenticator:
	var local_key: String
	var expected_key: String
	var expected_subject: String
	var hold := false
	var before_proof: Callable
	var requests: Array[SessionAuthRequest] = []
	var denials := 0

	func _make_proof(_peer_id: int, authority: bool, challenge: PackedByteArray) -> PackedByteArray:
		if before_proof.is_valid():
			before_proof.call()
		return _proof(local_key, authority, challenge)

	func _verify(request: SessionAuthRequest) -> void:
		var expected := _proof(expected_key, not request.is_authority(), request.get_challenge())
		if not Crypto.new().constant_time_compare(expected, request.get_proof()):
			denials += 1
			request.reject()
		elif hold:
			requests.append(request)
		else:
			request.accept(SessionIdentity.verified(&"test", expected_subject))

	func _proof(key: String, authority: bool, challenge: PackedByteArray) -> PackedByteArray:
		var message := ("crimson-test-authority" if authority else "crimson-test-guest").to_utf8_buffer()
		message.append_array(challenge)
		return Crypto.new().hmac_digest(HashingContext.HASH_SHA256, key.to_utf8_buffer(), message)


## Total assertions evaluated across the suite.
var _checks := 0
## Assertions that reported errors and will cause a nonzero exit.
var _failures := 0


## Defers test execution until scene tree initialization has finished.
func _initialize() -> void:
	_run.call_deferred()


## Runs the session scenarios and reports the result to the headless runner.
func _run() -> void:
	_test_offline()
	_test_transport_factory()
	await _test_external_transport()
	await _test_loopback()
	await _test_verified_admission()
	await _test_timeout_and_cancel()
	print("GameSession: %d checks, %d failures." % [_checks, _failures])
	if _failures == 0:
		print("CRIMSON_GAME_SESSION_TESTS_PASSED")
	quit(0 if _failures == 0 else 1)


## Creates an endpoint with its own API by default and records its session events.
func _make_endpoint(branch_name: String, isolated: bool = true) -> Endpoint:
	var endpoint := Endpoint.new()
	endpoint.branch = Node.new()
	endpoint.branch.name = branch_name
	root.add_child(endpoint.branch)
	if isolated:
		endpoint.api = SceneMultiplayer.new()
		set_multiplayer(endpoint.api, endpoint.branch.get_path())
	else:
		endpoint.api = endpoint.branch.get_multiplayer() as SceneMultiplayer
	endpoint.session = GameSession.new()
	endpoint.session.name = "GameSession"
	# Record state transitions for later order and cardinality checks.
	endpoint.session.state_changed.connect(func(state: int) -> void: endpoint.states.append(state))
	# Record the state and peer ID together for each start event.
	endpoint.session.session_started.connect(func(state: int, id: int) -> void:
		endpoint.started_states.append(state)
		endpoint.started_ids.append(id)
	)
	# Preserve termination reasons so voluntary leaves and failures can be distinguished.
	endpoint.session.session_ended.connect(func(reason: StringName) -> void: endpoint.ended.append(reason))
	# Track roster additions and removals as separate events.
	endpoint.session.peer_joined.connect(func(id: int) -> void: endpoint.joined.append(id))
	endpoint.session.peer_left.connect(func(id: int) -> void: endpoint.left.append(id))
	endpoint.branch.add_child(endpoint.session)
	return endpoint


## Checks the offline lifecycle, API ownership, and transport restoration.
func _test_offline() -> void:
	var endpoint := _make_endpoint("Offline", false)
	var session := endpoint.session
	var previous := endpoint.api.multiplayer_peer
	_expect(session.get_state() == GameSession.State.IDLE, "New session starts idle.")
	_expect(session.get_local_peer_id() == 0 and not session.is_authority(), "Idle has no local authority.")
	_expect(session.get_connected_peer_ids().is_empty(), "Idle has no connected peers.")
	_expect(session.host(-1) != OK, "Reject an invalid host port.")
	_expect(session.join("", 45000) != OK, "Reject an empty join address.")
	_expect(session.join("127.0.0.1", 45000, 0.0) != OK, "Reject a zero join timeout.")
	_expect(endpoint.states.is_empty(), "Invalid arguments leave session state untouched.")
	_expect(session.start_offline() == OK, "Start offline on the default multiplayer API.")
	_expect(session.get_state() == GameSession.State.OFFLINE, "Offline mode has its own state.")
	_expect(session.is_authority() and session.get_local_peer_id() == 1, "Offline uses local authority peer 1.")
	_expect(session.get_connected_peer_ids().is_empty(), "Offline has no remote peers.")
	_expect(endpoint.started_states == [GameSession.State.OFFLINE] and endpoint.started_ids == [1],
		"Offline emits one session-start event with the authority ID.")
	_expect(session.start_offline() != OK, "A second start cannot replace an active offline session.")
	var offline_peer := endpoint.api.multiplayer_peer
	var contender := GameSession.new()
	endpoint.branch.add_child(contender)
	_expect(contender.start_offline() != OK, "Another manager cannot take over the active offline API.")
	contender.free()
	_expect(endpoint.api.multiplayer_peer == offline_peer and session.get_state() == GameSession.State.OFFLINE,
		"Freeing the contender keeps the original offline session active.")
	session.leave()
	session.leave()
	_expect(session.get_state() == GameSession.State.IDLE and session.get_local_peer_id() == 0,
		"Leaving offline returns to idle.")
	_expect(endpoint.ended == [&"left"], "Repeated leave emits one termination event.")
	_expect(endpoint.states == [GameSession.State.OFFLINE, GameSession.State.IDLE],
		"Offline lifecycle publishes only its two state changes.")
	_expect(endpoint.api.multiplayer_peer == previous, "Offline leave restores the original peer.")


## Checks typed open errors, ownership conflicts during the factory, and retry with the default.
func _test_transport_factory() -> void:
	var endpoint := _make_endpoint("Factory")
	var session := endpoint.session
	var port := _get_unused_loopback_port()
	if port == 0:
		return
	var probe := FactoryProbe.new()
	probe.api = endpoint.api
	probe.replacement = OfflineMultiplayerPeer.new()
	_expect(session.set_transport_adapter(probe) == OK, "Set a custom adapter while idle.")
	var target := SessionEndpoint.direct(" 127.0.0.1 ", port)
	_expect(target != null and target.get_address() == "127.0.0.1" and target.get_port() == port,
		"Construct a normalized direct endpoint.")
	_expect(SessionEndpoint.direct(" ", port) == null and SessionEndpoint.direct("127.0.0.1", 0) == null,
		"Reject invalid direct endpoints.")
	_expect(session.join_endpoint(target) == ERR_CANT_CONNECT, "Keep the adapter's exact client-open error.")
	_expect(session.host(port, 1, "127.0.0.1") == ERR_BUSY,
		"Abort installation if the factory changes the MultiplayerAPI peer.")
	_expect(endpoint.api.multiplayer_peer == probe.replacement and endpoint.states.is_empty(),
		"Keep the replacement installed and publish no lifecycle signals on failure.")
	_expect(probe.candidate != null and probe.candidate.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED,
		"Close the unused candidate and release its port.")
	_expect(session.set_transport_adapter(null) == OK and session.host(port, 1, "127.0.0.1") == OK,
		"Restore default ENet and reuse the same endpoint after failure.")
	session.leave()
	_expect(endpoint.api.multiplayer_peer == probe.replacement, "Normal teardown restores the replacement offline peer.")


## Checks how the session handles an external transport and unsafe API configuration.
func _test_external_transport() -> void:
	var endpoint := _make_endpoint("External")
	var port := _get_unused_loopback_port()
	if port == 0:
		return
	var external := ENetMultiplayerPeer.new()
	external.set_bind_ip("127.0.0.1")
	if not _expect(external.create_server(port, 2) == OK, "Create a transport owned by the game."):
		return
	endpoint.api.multiplayer_peer = external
	var session := endpoint.session
	_expect(session.start_offline() != OK, "An idle session cannot replace an external transport.")
	_expect(session.join("127.0.0.1", port) != OK, "Joining also preserves the external transport.")
	session.leave()
	_expect(endpoint.api.multiplayer_peer == external, "Leaving an idle session preserves the external transport.")
	_expect(external.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED,
		"The external transport remains connected.")
	endpoint.api.multiplayer_peer = null
	if _expect(session.start_offline() == OK, "The API is available once the game removes its transport."):
		endpoint.api.multiplayer_peer = external
		# Wait for the session to acknowledge the replacement before inspecting cleanup.
		await _wait_until(func() -> bool: return endpoint.ended.has(&"transport_replaced"),
			"The session notices when the game replaces its transport.")
		_expect(endpoint.api.multiplayer_peer == external
			and external.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED,
			"Cleanup of a replaced transport preserves the game's new transport.")
	endpoint.api.multiplayer_peer = null
	external.close()
	endpoint.api.allow_object_decoding = true
	_expect(session.host(port, 2, "127.0.0.1") == ERR_UNAUTHORIZED,
		"Networking refuses an API that allows object decoding.")
	_expect(endpoint.api.allow_object_decoding and session.get_state() == GameSession.State.IDLE,
		"Rejection leaves the caller's API settings and session state intact.")
	endpoint.api.allow_object_decoding = false


## Exercises a loopback host with two clients through admission, rejoin, and shutdown.
func _test_loopback() -> void:
	var host := _make_endpoint("Host")
	var first := _make_endpoint("FirstClient")
	var second := _make_endpoint("SecondClient")
	var port := _get_unused_loopback_port()
	if port == 0:
		return
	var host_session := host.session
	var original_host_peer := host.api.multiplayer_peer
	if not _expect(host_session.host(port, 4, "127.0.0.1") == OK, "Start a real localhost ENet server."):
		return
	_expect(host_session.get_state() == GameSession.State.HOSTING and host_session.is_authority(),
		"Host is the session authority.")
	_expect(host_session.get_local_peer_id() == 1 and host.started_ids == [1], "Host uses ENet server ID 1.")
	_expect(host_session.is_accepting_connections(), "A newly started host accepts joins.")

	var owned_peer := host.api.multiplayer_peer
	var contender := GameSession.new()
	host.branch.add_child(contender)
	_expect(contender.start_offline() != OK, "A second session cannot take over the host's multiplayer API.")
	contender.leave()
	contender.queue_free()
	await process_frame
	_expect(host.api.multiplayer_peer == owned_peer, "Destroying the contender preserves the active host.")

	if not await _join_client(first, port):
		return
	if not await _join_client(second, port):
		return
	var first_id: int = first.session.get_local_peer_id()
	var second_id: int = second.session.get_local_peer_id()
	# Wait until the host has observed both joins before checking roster order.
	if not await _wait_until(func() -> bool: return host.session.get_connected_peer_ids().size() == 2,
		"Host tracks both connected clients."):
		return
	var expected := PackedInt32Array([first_id, second_id])
	expected.sort()
	_expect(PackedInt32Array(host_session.get_connected_peer_ids()) == expected,
		"Host roster contains only remote peers in deterministic order.")
	_expect(host.joined.has(first_id) and host.joined.has(second_id), "Host emits an event for each join.")
	_expect(first.session.get_connected_peer_ids().has(1) and second.session.get_connected_peer_ids().has(1),
		"Each client sees the authority in its remote roster.")
	_expect(not first.session.get_connected_peer_ids().has(first_id), "A client roster excludes itself.")
	_expect(first.session.set_accepting_connections(false) != OK, "Only the host can change join admission.")
	_expect(host_session.set_accepting_connections(false) == OK and not host_session.is_accepting_connections(),
		"The host can pause new joins.")
	_expect(first.session.get_state() == GameSession.State.CLIENT
		and second.session.get_state() == GameSession.State.CLIENT,
		"Pausing joins keeps existing clients connected.")

	first.session.leave()
	_expect(first.session.get_state() == GameSession.State.IDLE and first.ended == [&"left"],
		"A voluntary client leave returns to idle and reports its reason.")
	# Require the removal event and the updated roster before attempting a rejoin.
	var client_removed := func() -> bool:
		return host.left.has(first_id) and not host.session.get_connected_peer_ids().has(first_id)
	if not await _wait_until(client_removed, "Host removes the departing client."):
		return
	var started_count := first.started_ids.size()
	var join_count := host.joined.size()
	if not _expect(first.session.join("127.0.0.1", port, 0.25) == OK,
		"The client can attempt a new connection to the closed host."):
		return
	# Wait for the refused join to terminate before inspecting its reason.
	if not await _wait_until(func() -> bool: return first.ended.size() == 2,
		"The closed host does not accept a new join."):
		return
	_expect(first.ended.size() == 2 and first.ended.back() in [&"connection_failed", &"connection_timeout"],
		"The refused connection reports a terminal failure.")
	_expect(first.started_ids.size() == started_count and host.joined.size() == join_count,
		"The refused connection emits no successful join events.")
	_expect(host_session.set_accepting_connections(true) == OK, "The host can reopen admission.")
	if not await _join_client(first, port):
		return
	_expect(first.started_ids.size() == started_count + 1, "A previously refused client can rejoin.")

	host_session.queue_free()
	await process_frame
	_expect(host.api.multiplayer_peer == original_host_peer, "Destroying the host restores its prior multiplayer peer.")
	# Both clients must observe shutdown before their termination reasons are checked.
	var clients_disconnected := func() -> bool:
		return first.ended.size() == 3 and second.ended.size() == 1
	if await _wait_until(clients_disconnected, "Both clients detect that the host left."):
		_expect(first.ended.back() == &"server_disconnected" and second.ended == [&"server_disconnected"],
			"Clients distinguish server departure from a voluntary leave.")
		_expect(first.session.get_local_peer_id() == 0 and first.session.get_connected_peer_ids().is_empty(),
			"Disconnection clears local and remote peer IDs.")


## Verifies that unanswered joins time out and pending joins can be canceled.
func _test_timeout_and_cancel() -> void:
	var endpoint := _make_endpoint("Timeout")
	var port := _get_unused_loopback_port()
	if port == 0:
		return
	var session := endpoint.session
	if not _expect(session.join("127.0.0.1", port, 0.15) == OK, "Start a join with no listening server."):
		return
	_expect(session.get_state() == GameSession.State.CONNECTING and session.get_local_peer_id() == 0,
		"Connecting exposes no assigned peer ID.")
	# Wait for the asynchronous timeout signal from the session.
	if not await _wait_until(func() -> bool: return not endpoint.ended.is_empty(),
		"An unanswered join stops waiting."):
		return
	_expect(endpoint.ended == [&"connection_timeout"], "An unanswered join emits a timeout reason.")
	_expect(endpoint.started_ids.is_empty(), "An unanswered join never starts a session.")
	if not _expect(session.join("127.0.0.1", port) == OK, "The session can retry after timing out."):
		return
	session.leave()
	_expect(endpoint.ended == [&"connection_timeout", &"left"], "A connecting client can cancel its attempt.")
	_expect(session.start_offline() == OK and session.is_authority(), "A canceled client can start offline.")
	session.leave()


## Authenticates over real ENet and checks that proof settlement and identity follow connection lifetime.
func _test_verified_admission() -> void:
	var host := _make_endpoint("VerifiedHost")
	var guest := _make_endpoint("VerifiedGuest")
	var port := _get_unused_loopback_port()
	if port == 0:
		return
	var verifier := AuthProbe.new()
	verifier.local_key = "fixture-authority-key"
	verifier.expected_key = "fixture-guest-key"
	verifier.expected_subject = "guest-42"
	verifier.hold = true
	var prover := AuthProbe.new()
	prover.local_key = "fixture-invalid-key"
	prover.expected_key = verifier.local_key
	prover.expected_subject = "authority-17"
	_expect(host.session.set_authenticator(verifier) == OK and host.session.get_authenticator() == verifier
		and guest.session.set_authenticator(prover) == OK, "Select typed authenticators before the network starts.")
	var foreign := func(_peer: int, _data: PackedByteArray) -> void: pass
	host.api.auth_callback = foreign
	_expect(host.session.host(port, 2, "127.0.0.1") == ERR_ALREADY_IN_USE and host.api.auth_callback == foreign,
		"A configured authenticator preserves a foreign callback and refuses to start networking.")
	_expect(host.session.start_offline() == OK and host.api.auth_callback == foreign,
		"Offline mode does not install the configured network authenticator.")
	host.session.leave()
	host.api.auth_callback = Callable()
	host.api.auth_timeout = 0.0
	_expect(host.session.host(port, 2, "127.0.0.1") == ERR_INVALID_PARAMETER,
		"Verified startup requires a bounded authentication timeout.")
	host.api.auth_timeout = 2.0
	if not _expect(host.session.host(port, 2, "127.0.0.1") == OK and host.api.auth_callback.is_valid(),
		"Start the verified authority using its existing SceneMultiplayer timeout."):
		return
	if not _expect(guest.session.join("127.0.0.1", port) == OK, "Start an unauthorized connection.") \
			or not await _wait_until(func() -> bool: return not guest.ended.is_empty(), "The unauthorized connection ends."):
		return
	_expect(verifier.denials == 1 and host.joined.is_empty() and guest.started_ids.is_empty()
		and host.session.get_connected_peer_ids().is_empty(), "An invalid challenge response cannot publish admission.")

	var joined_subjects: Array[String] = []
	host.session.peer_joined.connect(func(peer_id: int) -> void:
		var identity := host.session.get_peer_identity(peer_id)
		if identity != null and identity.get_provider() == &"test":
			joined_subjects.append(identity.get_subject())
	)
	prover.local_key = verifier.expected_key
	if not _expect(guest.session.join("127.0.0.1", port) == OK, "Retry with a valid proof.") \
			or not await _wait_until(func() -> bool: return verifier.requests.size() == 1, "The verifier receives an async request."):
		return
	var accepted := verifier.requests[0]
	var guest_id := accepted.get_peer_id()
	_expect(accepted.get_challenge().size() == 32 and accepted.get_proof().size() == 32
		and accepted.is_authority() and guest_id > 1 and host.api.get_authenticating_peers().has(guest_id),
		"The async request is bound to the engine's pending connection and bounded challenge.")
	_expect(host.session.get_peer_identity(guest_id) == null and host.joined.is_empty()
		and guest.session.get_state() == GameSession.CONNECTING,
		"An unresolved verifier holds both roster admission and identity publication.")
	var guest_identity := SessionIdentity.verified(&"test", verifier.expected_subject)
	_expect(accepted.accept(guest_identity) == OK and accepted.reject() == ERR_DOES_NOT_EXIST,
		"The verification request settles exactly once.")
	if not await _wait_until(func() -> bool: return guest.session.get_state() == GameSession.CLIENT,
			"The verified guest receives admission."):
		return
	var authority_identity := guest.session.get_peer_identity(1)
	_expect(joined_subjects == [verifier.expected_subject] and host.session.get_peer_identity(guest_id) == guest_identity
		and authority_identity != null and authority_identity.get_subject() == prover.expected_subject,
		"Each direct peer sees its verified remote identity at admission.")
	guest.session.leave()
	if not await _wait_until(func() -> bool: return host.left.has(guest_id), "Verified departure reaches the authority."):
		return
	_expect(host.session.get_peer_identity(guest_id) == null and guest.session.get_peer_identity(1) == null,
		"Departure retires both connection identities.")
	if not _expect(guest.session.join("127.0.0.1", port) == OK, "Reconnect before testing a retired request.") \
			or not await _wait_until(func() -> bool: return verifier.requests.size() == 2, "The new connection has a fresh attempt."):
		return
	var stale := verifier.requests[1]
	_expect(stale.get_challenge() != accepted.get_challenge(), "A reconnect gets a different cryptographic challenge.")
	host.session.leave()
	guest.session.leave()
	_expect(stale.accept(guest_identity) == ERR_DOES_NOT_EXIST and not host.api.auth_callback.is_valid()
		and not guest.api.auth_callback.is_valid() and is_equal_approx(host.api.auth_timeout, 2.0),
		"Teardown invalidates a pending request and restores only the owned callback.")
	if not _expect(host.session.host(port, 2, "127.0.0.1") == OK, "Restart the verified host after teardown."):
		return
	prover.before_proof = guest.session.leave
	if not _expect(guest.session.join("127.0.0.1", port) == OK, "Start a connection that will cancel in its provider hook.") \
			or not await _wait_until(func() -> bool: return guest.session.get_state() == GameSession.IDLE,
				"The provider callback can cancel safely outside the engine packet poll."):
		return
	_expect(not guest.api.auth_callback.is_valid() and guest.ended.back() == &"left",
		"Provider cancellation leaves no installed callback or successful admission.")
	host.api.auth_callback = foreign
	if await _wait_until(func() -> bool: return host.ended.has(&"authentication_replaced"),
			"Replacing the active callback closes the verified session."):
		_expect(host.api.auth_callback == foreign, "Teardown preserves the replacement callback.")


## Starts a loopback client and checks the start event against its active peer ID.
func _join_client(endpoint: Endpoint, port: int) -> bool:
	var session := endpoint.session
	if not _expect(session.join("127.0.0.1", port) == OK, "%s starts a localhost connection." % endpoint.branch.name):
		return false
	_expect(session.get_state() == GameSession.State.CONNECTING, "Join begins in the connecting state.")
	# Wait for the client to reach its active state before checking its identity.
	if not await _wait_until(func() -> bool: return session.get_state() == GameSession.State.CLIENT,
		"%s connects through ENet." % endpoint.branch.name):
		return false
	return _expect(not session.is_authority() and session.get_local_peer_id() > 1
		and endpoint.started_states.back() == GameSession.State.CLIENT
		and endpoint.started_ids.back() == session.get_local_peer_id(),
		"The client gets a remote peer ID and publishes it in its start event.")


## Finds an ephemeral UDP port by binding and closing a loopback socket.
func _get_unused_loopback_port() -> int:
	var socket := PacketPeerUDP.new()
	if not _expect(socket.bind(0, "127.0.0.1") == OK, "Reserve an ephemeral loopback port for this test."):
		return 0
	var port := socket.get_local_port()
	socket.close()
	_expect(port > 0, "Godot assigned a usable UDP port.")
	return port


## Polls a predicate across frames and reports a failed assertion at the deadline.
func _wait_until(predicate: Callable, message: String) -> bool:
	var deadline := Time.get_ticks_msec() + 5000
	while not predicate.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return _expect(predicate.call(), message)


## Counts an assertion, reports its failure, and returns the condition for callers.
func _expect(condition: bool, message: String) -> bool:
	_checks += 1
	if not condition:
		_failures += 1
		push_error(message)
	return condition
