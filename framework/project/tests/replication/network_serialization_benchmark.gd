# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree
## Measures actual Godot RPC framing and fanout against in-process packet peers.

const PACKETS_PER_SAMPLE: int = 16
const SAMPLES: int = 21
const INPUT_ROWS: int = 24
const POSE_ROWS: int = 24
const COMMAND_BYTES: int = 22
const POSE_BYTES: int = 28
const STATE_RECORDS: int = 128
const STATE_COMMITS_PER_SAMPLE: int = 4


## Queues one encoded SceneMultiplayer packet and its delivery metadata.
class Packet extends RefCounted:
	## Source peer as seen by the receiver.
	var sender: int
	## Exact bytes delivered by the Godot RPC layer.
	var bytes: PackedByteArray
	## Selected transport channel.
	var channel: int
	## Selected reliability mode.
	var mode: MultiplayerPeer.TransferMode

	## Keep the packet alive until its receiver polls.
	func _init(source: int, data: PackedByteArray, wire_channel: int, wire_mode: MultiplayerPeer.TransferMode) -> void:
		sender = source
		bytes = data
		channel = wire_channel
		mode = wire_mode


## Delivers encoded packets in memory so socket and scheduler variance do not dominate.
class LoopPeer extends MultiplayerPeerExtension:
	## Local peer identifier.
	var id: int
	## Total packets captured since the last reset.
	var packets: int = 0
	## Total exact SceneMultiplayer bytes captured since the last reset.
	var bytes: int = 0
	## Connected peers share weak references to avoid cyclic lifetime.
	var links: Dictionary[int, WeakRef] = {}
	## Incoming frames await the receiver's explicit poll.
	var _incoming: Array[Packet] = []
	## Cursor avoids shifting the queue for each packet.
	var _cursor: int = 0
	## Current outgoing target.
	var _target: int = 0
	## Current outgoing channel.
	var _channel: int = 0
	## Current outgoing reliability mode.
	var _mode: MultiplayerPeer.TransferMode = MultiplayerPeer.TRANSFER_MODE_RELIABLE

	## Set the identifier exposed to SceneMultiplayer.
	func _init(peer_id: int) -> void:
		id = peer_id

	## Connect two endpoints after their SceneMultiplayer instances are attached.
	func link(other: LoopPeer) -> void:
		links[other.id] = weakref(other)
		peer_connected.emit(other.id)

	## Release consumed packet storage outside measured poll intervals.
	func clear_consumed() -> void:
		if _cursor == _incoming.size():
			_incoming.clear()
			_cursor = 0

	## Queue the exact encoded packet in its destination endpoint.
	func _put_packet_script(packet: PackedByteArray) -> Error:
		if not links.has(_target):
			return ERR_INVALID_PARAMETER
		var destination: LoopPeer = links[_target].get_ref()
		if destination == null:
			return ERR_CONNECTION_ERROR
		packets += 1
		bytes += packet.size()
		destination._incoming.append(Packet.new(id, packet, _channel, _mode))
		return OK

	## Return the pending packet and advance the receive cursor.
	func _get_packet_script() -> PackedByteArray:
		var packet: PackedByteArray = _incoming[_cursor].bytes
		_cursor += 1
		return packet

	## Report packets ready for the next SceneMultiplayer poll.
	func _get_available_packet_count() -> int:
		return _incoming.size() - _cursor

	## The benchmark packets fit within a normal multiplayer payload budget.
	func _get_max_packet_size() -> int:
		return 1200

	## Report the next packet's sender before consumption.
	func _get_packet_peer() -> int:
		return _incoming[_cursor].sender

	## Report the next packet's channel before consumption.
	func _get_packet_channel() -> int:
		return _incoming[_cursor].channel

	## Report the next packet's reliability before consumption.
	func _get_packet_mode() -> MultiplayerPeer.TransferMode:
		return _incoming[_cursor].mode

	## Select the destination for the next packet.
	func _set_target_peer(peer: int) -> void:
		_target = peer

	## Select the channel for the next packet.
	func _set_transfer_channel(channel: int) -> void:
		_channel = channel

	## Report the current outgoing channel.
	func _get_transfer_channel() -> int:
		return _channel

	## Select the reliability mode for the next packet.
	func _set_transfer_mode(mode: MultiplayerPeer.TransferMode) -> void:
		_mode = mode

	## Report the current outgoing reliability mode.
	func _get_transfer_mode() -> MultiplayerPeer.TransferMode:
		return _mode

	## SceneMultiplayer treats peer one as the authority.
	func _is_server() -> bool:
		return id == 1

	## The fixture only needs directly connected links.
	func _is_server_relay_supported() -> bool:
		return false

	## Return the local peer identifier.
	func _get_unique_id() -> int:
		return id

	## The fixture creates links explicitly before measuring.
	func _get_connection_status() -> MultiplayerPeer.ConnectionStatus:
		return MultiplayerPeer.CONNECTION_CONNECTED

	## SceneMultiplayer drives receive work by calling the packet methods directly.
	func _poll() -> void:
		pass


## Uses the actual legacy and new RPC argument shapes under a shared Godot node path.
class Motion extends Node:
	## Number of motion packets decoded into RPC arguments.
	var received: int = 0
	## Last seen tick or command sequence.
	var last_sequence: int = 0
	## Last decoded argument contents for the fixture's equivalence checks.
	var last_payload: Array = []

	## Deliver legacy movement inputs as a typed numeric array.
	@rpc("any_peer", "call_remote", "unreliable_ordered")
	func legacy_input(epoch: int, ticket: int, sequence: int, commands: PackedFloat32Array) -> void:
		received += 1
		last_sequence = sequence
		last_payload = [epoch, ticket, commands]

	## Deliver new movement inputs as a packed byte argument.
	@rpc("any_peer", "call_remote", "unreliable_ordered")
	func codec_input(epoch: int, ticket: int, sequence: int, commands: PackedByteArray) -> void:
		received += 1
		last_sequence = sequence
		last_payload = [epoch, ticket, commands]

	## Deliver legacy server poses as separate ID and float arrays.
	@rpc("authority", "call_remote", "unreliable")
	func legacy_pose(epoch: int, revision: int, tick: int, peers: PackedInt32Array, poses: PackedFloat32Array) -> void:
		received += 1
		last_sequence = tick
		last_payload = [epoch, revision, peers, poses]

	## Deliver new server poses as one byte array.
	@rpc("authority", "call_remote", "unreliable")
	func codec_pose(epoch: int, revision: int, tick: int, poses: PackedByteArray) -> void:
		received += 1
		last_sequence = tick
		last_payload = [epoch, revision, poses]


## Keep the related scene and transport handles together.
class Endpoint extends RefCounted:
	## Root of this endpoint's dedicated SceneMultiplayer scope.
	var scope: Node
	## Real Godot RPC encoder and decoder for this endpoint.
	var api: SceneMultiplayer
	## In-process packet capture endpoint.
	var peer: LoopPeer
	## Matching motion RPC node.
	var motion: Motion
	## Production endpoint used by the committed-state baseline.
	var replication: StateReplication


## Number of fixture assertions that failed.
var _failures: int = 0
## Legacy input rows as Godot packed floats.
var _commands: PackedFloat32Array
## Legacy pose recipients as Godot packed integers.
var _ids: PackedInt32Array
## Legacy pose data as Godot packed floats.
var _poses: PackedFloat32Array
## New input rows, serialized once outside RPC timing.
var _command_bytes: PackedByteArray
## New pose rows, serialized once outside RPC timing.
var _pose_bytes: PackedByteArray
## One committed view shared by all recipients for the scale comparison.
var _state: Dictionary = {}


## Defer setup until the SceneTree is ready for nodes.
func _initialize() -> void:
	_run.call_deferred()


## Measure both wire representations at each recipient count.
func _run() -> void:
	multiplayer_poll = false
	var scales: PackedInt32Array = PackedInt32Array([1, 8, 32, 128, 256])
	var arguments: PackedStringArray = OS.get_cmdline_user_args()
	if not arguments.is_empty():
		scales.clear()
		for argument: String in arguments:
			if not argument.is_valid_int() or argument.to_int() < 1 or argument.to_int() > 512:
				push_error("Recipient counts must be integers from 1 to 512.")
				quit(1)
				return
			scales.append(argument.to_int())
	_build_payloads()
	for count: int in scales:
		_benchmark(count)
	if _failures == 0:
		print("CRIMSON_NETWORK_RPC_BENCHMARK_PASS")
	quit(0 if _failures == 0 else 1)


## Construct both pose payloads and the old and current input payloads outside RPC timing.
func _build_payloads() -> void:
	_commands.resize(INPUT_ROWS * 4)
	_command_bytes.resize(INPUT_ROWS * COMMAND_BYTES)
	for row: int in INPUT_ROWS:
		for field: int in 4:
			var index: int = row * 4 + field
			_commands[index] = float(index % 17 - 8) / 16.0
			_command_bytes.encode_float(row * COMMAND_BYTES + field * 4, _commands[index])
		_command_bytes.encode_u8(row * COMMAND_BYTES + 16, 1 if row == 0 else 0)
		_command_bytes.encode_u8(row * COMMAND_BYTES + 17, 1)
		_command_bytes.encode_float(row * COMMAND_BYTES + 18, 0.5)
	_ids.resize(POSE_ROWS)
	_poses.resize(POSE_ROWS * 6)
	_pose_bytes.resize(POSE_ROWS * POSE_BYTES)
	for row: int in POSE_ROWS:
		var offset: int = row * POSE_BYTES
		_ids[row] = row + 2
		_pose_bytes.encode_s32(offset, _ids[row])
		for column: int in 6:
			var value: int = row * 6 + column
			_poses[value] = float(value - 60) / 32.0
			_pose_bytes.encode_float(offset + 4 + column * 4, _poses[value])


## Mount an isolated endpoint with the same RPC path beneath its scope.
func _endpoint(id: int) -> Endpoint:
	var endpoint := Endpoint.new()
	endpoint.scope = Node.new()
	endpoint.scope.name = "Peer%d" % id
	root.add_child(endpoint.scope)
	endpoint.api = SceneMultiplayer.new()
	endpoint.api.server_relay = false
	set_multiplayer(endpoint.api, endpoint.scope.get_path())
	endpoint.peer = LoopPeer.new(id)
	endpoint.api.multiplayer_peer = endpoint.peer
	endpoint.motion = Motion.new()
	endpoint.motion.name = "Motion"
	endpoint.scope.add_child(endpoint.motion)
	return endpoint


## Complete path-cache handshakes and measure warmed per-recipient sends and polls.
func _benchmark(count: int) -> void:
	var previous_failures: int = _failures
	var host: Endpoint = _endpoint(1)
	var guests: Array[Endpoint] = []
	for index: int in count:
		var guest: Endpoint = _endpoint(index + 2)
		host.peer.link(guest.peer)
		guest.peer.link(host.peer)
		guests.append(guest)
		host.motion.legacy_pose.rpc_id(guest.peer.id, 3, 5, 9, _ids, _poses)
		guest.api.poll()
		host.api.poll()
		guest.peer.clear_consumed()
		host.peer.clear_consumed()
	_check(guests[0].motion.received == 1 and guests[-1].motion.received == 1, "Pose warmup must reach both ends of the cohort.")
	_check_input_frames(host, guests[0])
	var legacy_send: Array[float] = []
	var legacy_receive: Array[float] = []
	var codec_send: Array[float] = []
	var codec_receive: Array[float] = []
	var legacy_bytes: int = 0
	var codec_bytes: int = 0
	for sample: int in SAMPLES + 2:
		for pass_index: int in 2:
			var codec: bool = (sample + pass_index) % 2 == 1
			var measurement: Dictionary = _measure_fanout(host, guests, codec)
			if sample < 2:
				continue
			if codec:
				codec_send.append(measurement.send)
				codec_receive.append(measurement.receive)
				codec_bytes = measurement.bytes
			else:
				legacy_send.append(measurement.send)
				legacy_receive.append(measurement.receive)
				legacy_bytes = measurement.bytes
	if previous_failures == _failures:
		_emit_pose("packed_arrays", count, legacy_bytes, legacy_send, legacy_receive)
		_emit_pose("binary_codec", count, codec_bytes, codec_send, codec_receive)
		_benchmark_state(host, guests)
	for guest: Endpoint in guests:
		set_multiplayer(null, guest.scope.get_path())
		guest.scope.free()
	set_multiplayer(null, host.scope.get_path())
	host.scope.free()


## Install actual StateReplication endpoints and measure whole-view work for one changed record.
func _benchmark_state(host: Endpoint, guests: Array[Endpoint]) -> void:
	var previous_failures: int = _failures
	_state.clear()
	for record: int in STATE_RECORDS:
		_state["record_%03d" % record] = {"count": record, "cell": [record % 8, record / 8], "active": true}
	host.replication = _replication(host.scope)
	_check(host.replication.set_projector(_project_state) == OK, "Install the benchmark projector.")
	_check(host.replication.advance_world(1, 1) == OK, "Set the state replication epoch.")
	for guest: Endpoint in guests:
		guest.replication = _replication(guest.scope)
		_check(host.replication.register_peer(guest.peer.id, "guest_%d" % guest.peer.id, 1) == OK,
			"Register the committed-state recipient.")
		_check(host.replication.send_baseline(guest.peer.id) == OK, "Send the benchmark baseline.")
		guest.api.poll()
		host.api.poll()
		guest.peer.clear_consumed()
		host.peer.clear_consumed()
		_check(host.replication.is_peer_ready(guest.peer.id, 1) and guest.replication.is_client_ready(),
			"Every recipient must acknowledge its actual baseline.")
	if previous_failures != _failures:
		return
	var published: Array[float] = []
	var delivered: Array[float] = []
	var measured_bytes: int = 0
	var measured_packets: int = 0
	var revision: int = 1
	for sample: int in SAMPLES + 2:
		host.peer.packets = 0
		host.peer.bytes = 0
		var started: int = Time.get_ticks_usec()
		for commit: int in STATE_COMMITS_PER_SAMPLE:
			revision += 1
			_state["record_000"]["count"] = revision
			_check(host.replication.publish_changes(revision) == OK, "Publish one changed record.")
		var enqueued: int = Time.get_ticks_usec()
		for guest: Endpoint in guests:
			guest.api.poll()
		var finished: int = Time.get_ticks_usec()
		for guest: Endpoint in guests:
			_check(guest.replication.get_revision() == revision and guest.replication.is_client_ready(),
				"Each recipient must apply the same committed revision.")
			guest.peer.clear_consumed()
		var expected_count: int = guests.size() * STATE_COMMITS_PER_SAMPLE
		_check(host.peer.packets == expected_count,
			"One upsert must travel as one RPC frame per recipient and commit.")
		if sample >= 2:
			published.append(float(enqueued - started) / STATE_COMMITS_PER_SAMPLE)
			delivered.append(float(finished - enqueued) / STATE_COMMITS_PER_SAMPLE)
			measured_bytes += host.peer.bytes
			measured_packets += host.peer.packets
	var actual: Dictionary = guests[-1].replication.get_projection()
	_check(actual.size() == STATE_RECORDS and actual["record_000"]["count"] == revision,
		"The received view must contain every record and the last upsert.")
	if previous_failures == _failures:
		published.sort()
		delivered.sort()
		print("CRIMSON_NETWORK_RPC_BENCHMARK " + JSON.stringify({
			"workload": "committed_state", "format": "godot_variant", "visible_records": STATE_RECORDS,
			"changed_records": 1, "recipients": guests.size(),
			"mean_rpc_bytes_per_packet": float(measured_bytes) / measured_packets,
			"send_median_us": published[SAMPLES >> 1], "send_p95_us": published[ceili(SAMPLES * 0.95) - 1],
			"receive_median_us": delivered[SAMPLES >> 1], "receive_p95_us": delivered[ceili(SAMPLES * 0.95) - 1],
			"sample_count": SAMPLES, "commits_per_sample": STATE_COMMITS_PER_SAMPLE,
		}))


## Mount the native replication service at a consistent RPC path.
func _replication(scope: Node) -> StateReplication:
	var replication := StateReplication.new()
	replication.name = "Replication"
	scope.add_child(replication)
	return replication


## Let the native replication service validate and detach the same view for each recipient.
func _project_state(_peer: int, _player: String, _epoch: int, _revision: int) -> Dictionary:
	return {"ok": true, "view": _state}


## Verify exact engine-frame sizes for both input shapes after caching the sender path.
func _check_input_frames(host: Endpoint, guest: Endpoint) -> void:
	guest.motion.legacy_input.rpc_id(1, 3, 5, 9, _commands)
	host.api.poll()
	guest.api.poll()
	host.peer.clear_consumed()
	guest.peer.clear_consumed()
	guest.peer.packets = 0
	guest.peer.bytes = 0
	guest.motion.legacy_input.rpc_id(1, 3, 5, 10000, _commands)
	host.api.poll()
	_check(guest.peer.packets == 1 and host.motion.last_sequence == 10000
		and host.motion.last_payload == [3, 5, _commands], "Legacy input must round-trip as one packet.")
	print("CRIMSON_NETWORK_RPC_BENCHMARK " + JSON.stringify({
		"workload": "input", "format": "packed_arrays", "rows": INPUT_ROWS,
		"rpc_bytes_per_packet": guest.peer.bytes,
	}))
	guest.peer.packets = 0
	guest.peer.bytes = 0
	guest.motion.codec_input.rpc_id(1, 3, 5, 10000, _command_bytes)
	host.api.poll()
	_check(guest.peer.packets == 1 and host.motion.last_sequence == 10000
		and host.motion.last_payload == [3, 5, _command_bytes], "Codec input must round-trip as one packet.")
	print("CRIMSON_NETWORK_RPC_BENCHMARK " + JSON.stringify({
		"workload": "input", "format": "binary_codec", "rows": INPUT_ROWS,
		"rpc_bytes_per_packet": guest.peer.bytes,
	}))
	host.peer.clear_consumed()


## Separate engine encode plus enqueue from engine decode plus callback dispatch.
func _measure_fanout(host: Endpoint, guests: Array[Endpoint], codec: bool) -> Dictionary:
	host.peer.packets = 0
	host.peer.bytes = 0
	for guest: Endpoint in guests:
		guest.motion.received = 0
	var started: int = Time.get_ticks_usec()
	if codec:
		for frame: int in PACKETS_PER_SAMPLE:
			for guest: Endpoint in guests:
				host.motion.codec_pose.rpc_id(guest.peer.id, 3, 5, 10000 + frame, _pose_bytes)
	else:
		for frame: int in PACKETS_PER_SAMPLE:
			for guest: Endpoint in guests:
				host.motion.legacy_pose.rpc_id(guest.peer.id, 3, 5, 10000 + frame, _ids, _poses)
	var enqueued: int = Time.get_ticks_usec()
	for guest: Endpoint in guests:
		guest.api.poll()
	var received: int = Time.get_ticks_usec()
	var expected: Array = [3, 5, _pose_bytes] if codec else [3, 5, _ids, _poses]
	for guest: Endpoint in guests:
		_check(guest.motion.received == PACKETS_PER_SAMPLE and guest.motion.last_sequence == 10000 + PACKETS_PER_SAMPLE - 1
			and guest.motion.last_payload == expected, "Each recipient must receive the exact benchmark pose.")
		guest.peer.clear_consumed()
	var expected_count: int = PACKETS_PER_SAMPLE * guests.size()
	_check(host.peer.packets == expected_count and host.peer.bytes % expected_count == 0,
		"Every measured pose must use one cached-path RPC packet of equal size.")
	@warning_ignore("integer_division")
	return {
		"send": float(enqueued - started) / PACKETS_PER_SAMPLE,
		"receive": float(received - enqueued) / PACKETS_PER_SAMPLE,
		"bytes": host.peer.bytes / expected_count,
	}


## Publish one JSON line per scale with times per 24-actor broadcast.
func _emit_pose(format: String, count: int, bytes: int, send: Array[float], receive: Array[float]) -> void:
	send.sort()
	receive.sort()
	print("CRIMSON_NETWORK_RPC_BENCHMARK " + JSON.stringify({
		"workload": "pose_fanout", "format": format, "rows": POSE_ROWS, "recipients": count,
		"rpc_bytes_per_packet": bytes, "rpc_bytes_per_fanout": bytes * count,
		"send_median_us": send[SAMPLES >> 1], "send_p95_us": send[ceili(SAMPLES * 0.95) - 1],
		"receive_median_us": receive[SAMPLES >> 1], "receive_p95_us": receive[ceili(SAMPLES * 0.95) - 1],
		"sample_count": SAMPLES, "broadcasts_per_sample": PACKETS_PER_SAMPLE,
	}))


## Make a corrupted measurement fail the benchmark process.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		if _failures <= 8:
			push_error(message)
