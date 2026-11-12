# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies standalone Steam socket boundaries and optional two-account P2P traffic.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480
## A canonical individual fixture account used only for offline endpoint validation.
const SAMPLE_ACCOUNT := "76561197960265729"
## Fixed non-account payloads for the signed transport exchange.
const HOST_DATA := "crimson-sockets-host"
const GUEST_DATA := "crimson-sockets-guest"
const SAMPLE_DATA := "crimson-sockets-sample"


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	var mode := arguments[0] if not arguments.is_empty() else ""
	var passed := false
	if arguments.size() == 1 and mode in ["missing", "sdk-off", "spacewar"]:
		passed = await _verify_scope(mode)
	elif arguments.size() == 3 and mode in ["p2p-host", "p2p-guest"]:
		if OS.get_environment("CRIMSON_STEAM_SOCKETS_ALLOW_LIVE") != "1":
			push_error("Set CRIMSON_STEAM_SOCKETS_ALLOW_LIVE=1 to opt into two-account Steam traffic.")
			quit(2)
			return
		if arguments[1].is_valid_int():
			passed = await _verify_pair(mode == "p2p-host", arguments[1].to_int(), arguments[2])
	if passed:
		print("CRIMSON_STEAM_SOCKETS_PASSED %s" % mode)
	else:
		push_error("Steam Sockets fixture did not verify the selected behavior.")
	quit(0 if passed else 1)


## Validate immutable route construction and failure outcomes on both SDK builds.
func _verify_scope(mode: String) -> bool:
	var endpoint := SteamSocketEndpoint.direct("127.0.0.1", 42000)
	var pinned := endpoint.expect_peer(SAMPLE_ACCOUNT)
	var p2p := SteamSocketEndpoint.p2p(SAMPLE_ACCOUNT, 28)
	var passed := endpoint.kind == SteamSocketEndpoint.DIRECT
	passed = passed and endpoint.address == "127.0.0.1" and endpoint.port == 42000
	passed = passed and endpoint.steam_id.is_empty() and pinned.steam_id == SAMPLE_ACCOUNT
	passed = passed and p2p.kind == SteamSocketEndpoint.P2P and p2p.port == 28
	passed = passed and p2p.address.is_empty() and p2p.steam_id == SAMPLE_ACCOUNT
	passed = passed and p2p.expect_peer(SAMPLE_ACCOUNT) == null
	passed = passed and SteamSocketEndpoint.direct("localhost", 42000) == null
	passed = passed and SteamSocketEndpoint.direct("::1", 0) == null
	passed = passed and SteamSocketEndpoint.direct("::1", 42000) != null
	passed = passed and SteamSocketEndpoint.p2p("0" + SAMPLE_ACCOUNT) == null
	passed = passed and SteamSocketEndpoint.p2p(SAMPLE_ACCOUNT, 65536) == null
	passed = passed and SteamSocketEndpoint.p2p("18446744073709551616") == null
	var detached := SteamSockets.new()
	passed = passed and detached.listen(endpoint).error == ERR_UNAVAILABLE
	passed = passed and detached.listen(pinned).error == ERR_INVALID_PARAMETER
	passed = passed and detached.listen(endpoint, 65).error == ERR_INVALID_PARAMETER
	passed = passed and detached.connect_to(endpoint, 999).result().error == ERR_INVALID_PARAMETER
	var unavailable := detached.connect_to(endpoint)
	passed = passed and unavailable.is_finished() and unavailable.result().error == ERR_UNAVAILABLE
	var networking := SteamNetworking.new()
	passed = passed and networking.relay_status().availability == SteamRelayStatus.UNKNOWN
	passed = passed and networking.warm_relay() == ERR_UNAVAILABLE
	passed = passed and networking.peer_path(1).error == ERR_UNAVAILABLE
	passed = passed and networking.peer_path(0).error == ERR_INVALID_PARAMETER
	var steam := SteamClient.boot(root, SPACEWAR)
	if mode == "spacewar":
		passed = passed and steam.is_ready() and _verify_restart(steam)
		if passed and steam.start(SPACEWAR) == OK:
			passed = await _verify_loopback(steam)
		else:
			passed = false
	else:
		passed = passed and not steam.is_ready() and steam.sockets() == null and steam.networking() == null
	steam.free()
	return passed


## Open local-only handles, then check cancellation, retirement and generation isolation.
func _verify_restart(steam: SteamClient) -> bool:
	var reservation := PacketPeerUDP.new()
	if reservation.bind(0, "127.0.0.1") != OK:
		return false
	var endpoint := SteamSocketEndpoint.direct("127.0.0.1", reservation.get_local_port())
	var sockets := steam.sockets()
	var networking := steam.networking()
	var passed := networking != null and networking.relay_status() != null
	passed = passed and networking.peer_path(1).error == ERR_UNAVAILABLE
	passed = passed and networking.warm_relay() == OK
	passed = passed and sockets.connect_to(SteamSocketEndpoint.direct("0.0.0.0", endpoint.port)).result().error == ERR_INVALID_PARAMETER
	var pending := sockets.connect_to(endpoint, 3000)
	passed = passed and not pending.is_finished() and pending.cancel() == OK
	passed = passed and pending.result().error == ERR_SKIP and pending.cancel() == ERR_DOES_NOT_EXIST
	reservation.close()
	var opened := sockets.listen(endpoint, 1)
	if not opened.ok:
		return false
	var old_listener := opened.value
	passed = passed and old_listener.accept(SteamSocketOffer.new()).error == ERR_DOES_NOT_EXIST
	passed = passed and old_listener.reject(SteamSocketOffer.new()) == ERR_DOES_NOT_EXIST
	passed = passed and old_listener.close() == OK and old_listener.close() == ERR_DOES_NOT_EXIST
	var current := sockets.listen(endpoint, 1)
	if not current.ok:
		return false
	var pending_at_stop := sockets.connect_to(endpoint, 3000)
	passed = passed and not pending_at_stop.is_finished()
	steam.stop()
	passed = passed and pending_at_stop.is_finished() and pending_at_stop.result().error == ERR_SKIP
	passed = passed and current.value.close() == ERR_UNAVAILABLE and sockets.listen(endpoint).error == ERR_UNAVAILABLE
	passed = passed and networking.relay_status().availability == SteamRelayStatus.UNKNOWN
	if steam.start(SPACEWAR) != OK or not steam.is_ready():
		return false
	var restarted := steam.sockets().listen(endpoint, 1)
	passed = passed and steam.sockets() != sockets and restarted.ok
	passed = passed and current.value.close() == ERR_UNAVAILABLE
	if restarted.ok:
		passed = restarted.value.close() == OK and passed
	steam.stop()
	return passed


## Exercise a certificate-pinned loopback connection and the actual offer deadline.
func _verify_loopback(steam: SteamClient) -> bool:
	var reservation := PacketPeerUDP.new()
	if reservation.bind(0, "127.0.0.1") != OK:
		return false
	var endpoint := SteamSocketEndpoint.direct("127.0.0.1", reservation.get_local_port())
	reservation.close()
	var sockets := steam.sockets()
	var opened := sockets.listen(endpoint, 1)
	if not opened.ok:
		return false
	var listener := opened.value
	var offers: Array[SteamSocketOffer] = []
	listener.connection_requested.connect(func(offer: SteamSocketOffer) -> void: offers.append(offer))
	var account := steam.get_local_steam_id()
	var target := endpoint.expect_peer(account)
	var ignored := sockets.connect_to(target, 5000)
	var limit := Time.get_ticks_msec() + 5000
	while offers.is_empty() and not ignored.is_finished() and Time.get_ticks_msec() < limit:
		await process_frame
	if offers.is_empty():
		listener.close()
		return false
	var stale := offers[0]
	while Time.get_ticks_msec() <= stale.expires_at_msec and Time.get_ticks_msec() < limit:
		await process_frame
	var expired := listener.accept(stale).error in [ERR_TIMEOUT, ERR_DOES_NOT_EXIST]
	if not expired or listener.reject(stale) != ERR_DOES_NOT_EXIST:
		listener.close()
		return false
	var accepted: Array[SteamSocketConnection] = []
	var received_by_host: Array[String] = []
	var listener_ref: WeakRef = weakref(listener)
	listener.connection_requested.connect(
		func(offer: SteamSocketOffer) -> void:
			var source := listener_ref.get_ref() as SteamSocketListener
			if source == null:
				return
			var result := source.accept(offer)
			if result.ok and source.accept(offer).error == ERR_DOES_NOT_EXIST:
				accepted.append(result.value)
				_record(result.value, received_by_host)
	)
	var wrong_account := SAMPLE_ACCOUNT if account != SAMPLE_ACCOUNT else "76561197960265730"
	var mismatched: SteamSocketConnectionResult = await sockets.connect_to(endpoint.expect_peer(wrong_account), 5000).wait()
	if mismatched.error != ERR_UNAUTHORIZED or mismatched.value != null or accepted.size() != 1:
		listener.close()
		return false
	accepted[0].close()
	accepted.clear()
	received_by_host.clear()
	var outgoing: SteamSocketConnectionResult = await sockets.connect_to(target, 5000).wait()
	if not outgoing.ok or accepted.size() != 1:
		listener.close()
		return false
	var host := accepted[0]
	var guest := outgoing.value
	var received_by_guest: Array[String] = []
	_record(guest, received_by_guest)
	limit = Time.get_ticks_msec() + 3000
	while not host.is_connected() and Time.get_ticks_msec() < limit:
		await process_frame
	var host_path := host.path()
	var guest_path := guest.path()
	if host.steam_id != account or guest.steam_id != account or host_path == null or guest_path == null:
		listener.close()
		return false
	if host_path.kind != SteamConnectionPath.DIRECT or guest_path.kind != SteamConnectionPath.DIRECT:
		listener.close()
		return false
	var delivered := guest.send(GUEST_DATA.to_utf8_buffer()) == OK
	var guest_message := "%d:%s" % [SteamDelivery.RELIABLE, GUEST_DATA]
	var sample_message := "%d:%s" % [SteamDelivery.UNRELIABLE, SAMPLE_DATA]
	while Time.get_ticks_msec() < limit and not (guest_message in received_by_host and sample_message in received_by_guest):
		host.send(SAMPLE_DATA.to_utf8_buffer(), SteamDelivery.UNRELIABLE)
		await create_timer(0.05).timeout
	delivered = delivered and guest_message in received_by_host and sample_message in received_by_guest
	var closes: Array[int] = []
	guest.closed.connect(func(reason: int) -> void: closes.append(reason))
	var closed := guest.close() == OK and closes == [OK]
	var retired := guest.path() == null and guest.steam_id.is_empty() and not guest.is_connected()
	var listener_closed := listener.close() == OK
	return delivered and closed and retired and listener_closed


## Use only the explicitly named test account. The host must start first.
func _verify_pair(host: bool, port: int, remote_account: String) -> bool:
	var remote := SteamSocketEndpoint.p2p(remote_account, port)
	if remote == null:
		return false
	var steam := SteamClient.boot(root, SPACEWAR)
	if not steam.is_ready() or steam.get_local_steam_id() == remote_account:
		steam.free()
		return false
	var sockets := steam.sockets()
	var connection: SteamSocketConnection
	var listener: SteamSocketListener
	var messages: Array[String] = []
	var limit := Time.get_ticks_msec() + 20000
	if host:
		var local := SteamSocketEndpoint.p2p(steam.get_local_steam_id(), port)
		var opened := sockets.listen(local, 2)
		if not opened.ok:
			steam.free()
			return false
		listener = opened.value
		var accepted: Array[SteamSocketConnection] = []
		var listener_ref: WeakRef = weakref(listener)
		listener.connection_requested.connect(
			func(offer: SteamSocketOffer) -> void:
				var source := listener_ref.get_ref() as SteamSocketListener
				if source == null:
					return
				if offer.steam_id != remote_account or not accepted.is_empty():
					source.reject(offer)
					return
				var result := source.accept(offer)
				if result.ok and source.accept(offer).error == ERR_DOES_NOT_EXIST:
					accepted.append(result.value)
					_record(result.value, messages)
		)
		print("CRIMSON_STEAM_SOCKETS_LISTENING")
		while accepted.is_empty() and Time.get_ticks_msec() < limit:
			await process_frame
		if not accepted.is_empty():
			connection = accepted[0]
	else:
		var operation := sockets.connect_to(remote, 15000)
		var result: SteamSocketConnectionResult = await operation.wait()
		if result.ok:
			connection = result.value
			_record(connection, messages)
	var passed := false
	if connection != null:
		passed = await _exchange(connection, remote_account, host, messages, limit)
	if listener != null:
		listener.close()
	elif connection != null:
		connection.close()
	steam.free()
	return passed


## Capture copied messages and their declared delivery modes.
func _record(connection: SteamSocketConnection, messages: Array[String]) -> void:
	connection.received.connect(
		func(bytes: PackedByteArray, delivery: SteamDelivery.Mode) -> void:
			if messages.size() < 256:
				messages.append("%d:%s" % [delivery, bytes.get_string_from_utf8()])
	)


## Exchange reliable markers and repeated best-effort samples on one secure connection.
func _exchange(connection: SteamSocketConnection, remote_account: String, host: bool,
		messages: Array[String], limit: int) -> bool:
	while not connection.is_connected() and Time.get_ticks_msec() < limit:
		await process_frame
	if connection.steam_id != remote_account or connection.path() == null:
		return false
	var data := HOST_DATA if host else GUEST_DATA
	var expected := "%d:%s" % [SteamDelivery.RELIABLE, GUEST_DATA if host else HOST_DATA]
	var sample := "%d:%s" % [SteamDelivery.UNRELIABLE, SAMPLE_DATA]
	var ack := "%d:%s-ack" % [SteamDelivery.RELIABLE, GUEST_DATA if host else HOST_DATA]
	if connection.send(PackedByteArray()) != ERR_INVALID_PARAMETER or connection.send(data.to_utf8_buffer()) != OK:
		return false
	var acknowledged := false
	while Time.get_ticks_msec() < limit and connection.is_connected():
		connection.send(SAMPLE_DATA.to_utf8_buffer(), SteamDelivery.UNRELIABLE)
		if not acknowledged and expected in messages and sample in messages:
			if connection.send((data + "-ack").to_utf8_buffer()) != OK:
				return false
			acknowledged = true
		if acknowledged and ack in messages:
			await create_timer(0.5).timeout
			return true
		await create_timer(0.05).timeout
	return false
