# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Saturates both directions of the actual demo UDP relay with owned loopback endpoints.

const Emulator := preload("res://demo/session/network_emulator.gd")
const LOOPBACK: String = "127.0.0.1"
const UPLINK_KBPS: int = 96
const DOWNLINK_KBPS: int = 192
const PAYLOAD_SIZE: int = 1200
const PACKETS_PER_FRAME: int = 4
const WINDOW_US: int = 1_600_000


func _initialize() -> void:
	_run.call_deferred()


## Verify directional payload rates, congestion drops, and non-flushing profile changes.
func _run() -> void:
	Engine.max_fps = 240
	var relay := Emulator.new()
	root.add_child(relay)
	var client := PacketPeerUDP.new()
	var server := PacketPeerUDP.new()
	var client_side := PacketPeerUDP.new()
	var server_side := PacketPeerUDP.new()
	var sockets: Array[PacketPeerUDP] = [client, server, client_side, server_side]
	for socket: PacketPeerUDP in sockets:
		if socket.bind(0, LOOPBACK) != OK:
			_fail("Bind an isolated loopback socket for each relay endpoint.")
			return
	if client.connect_to_host(LOOPBACK, client_side.get_local_port()) != OK \
			or server.connect_to_host(LOOPBACK, server_side.get_local_port()) != OK \
			or client_side.set_dest_address(LOOPBACK, client.get_local_port()) != OK \
			or server_side.connect_to_host(LOOPBACK, server.get_local_port()) != OK:
		_fail("Connect each endpoint to its corresponding loopback relay socket.")
		return
	# Exercise the production packet pump and owned client tuple without coupling the
	# load generator to ENet's own adaptive send rate. The ENet fixture covers admission.
	relay._client_socket = client_side
	relay._server_socket = server_side
	relay._client_port = client.get_local_port()
	relay.set_conditions(0, 0, 0.0, UPLINK_KBPS, DOWNLINK_KBPS)
	var payload := PackedByteArray()
	payload.resize(PAYLOAD_SIZE)
	var started_us: int = Time.get_ticks_usec()
	var now_us: int = started_us
	var uplink_bytes: int = 0
	var downlink_bytes: int = 0
	while now_us - started_us < WINDOW_US:
		for index: int in PACKETS_PER_FRAME:
			if client.put_packet(payload) != OK or server.put_packet(payload) != OK:
				_fail("Send load only between owned loopback sockets.")
				return
		relay._pump(now_us)
		uplink_bytes += _receive_bytes(server)
		downlink_bytes += _receive_bytes(client)
		await process_frame
		now_us = Time.get_ticks_usec()
	var elapsed_us: int = now_us - started_us
	var up_kbps: float = uplink_bytes * 8000.0 / elapsed_us
	var down_kbps: float = downlink_bytes * 8000.0 / elapsed_us
	if up_kbps < UPLINK_KBPS * 0.78 or up_kbps > UPLINK_KBPS * 1.05 \
			or down_kbps < DOWNLINK_KBPS * 0.85 or down_kbps > DOWNLINK_KBPS * 1.05:
		_fail("Expected asymmetric UDP shaping, measured %.1f up / %.1f down kbit/s." % [up_kbps, down_kbps])
		return
	var loaded: Emulator.Stats = relay.get_stats()
	if loaded.uplink.overflowed <= 0 or loaded.downlink.overflowed <= 0 \
			or loaded.uplink.queued <= 0 or loaded.downlink.queued <= 0 \
			or loaded.uplink.queued > Emulator.MAX_QUEUE_PER_DIRECTION or loaded.downlink.queued > Emulator.MAX_QUEUE_PER_DIRECTION \
			or loaded.uplink.queued_bytes != loaded.uplink.queued * PAYLOAD_SIZE \
			or loaded.downlink.queued_bytes != loaded.downlink.queued * PAYLOAD_SIZE:
		_fail("Each direction must bound its queue and report bytes and congestion drops.")
		return
	relay.set_conditions(0, 0, 0.0)
	var cleared: Emulator.Stats = relay.get_stats()
	if cleared.queued != loaded.queued or cleared.uplink_kbps != 0 or cleared.downlink_kbps != 0:
		_fail("Clearing limits must keep already assigned delivery times.")
		return
	relay.reset_counters()
	var reset: Emulator.Stats = relay.get_stats()
	if reset.queued != loaded.queued or reset.dropped != 0 or reset.uplink.overflowed != 0:
		_fail("Counter reset must preserve pending datagrams.")
		return
	print("CRIMSON_NETWORK_PACING_RATE %.1f UPLINK %.1f DOWNLINK KBPS; queued=%d overflow=%d" %
		[up_kbps, down_kbps, loaded.queued, loaded.uplink.overflowed + loaded.downlink.overflowed])
	print("CRIMSON_NETWORK_PACING_PASS")
	quit()


## Count actual bytes delivered to an endpoint during this measurement interval.
func _receive_bytes(socket: PacketPeerUDP) -> int:
	var size: int = 0
	for index: int in socket.get_available_packet_count():
		size += socket.get_packet().size()
	return size


## Mark the fixture failed without editing the game's transport or settings.
func _fail(message: String) -> void:
	push_error(message)
	quit(1)
