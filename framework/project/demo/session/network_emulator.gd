# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Node
## Routes the demo's loopback ENet guest through a two-ended UDP relay for local testing.

const IPV4_LOOPBACK: String = "127.0.0.1"
const IPV6_LOOPBACK: String = "::1"
const IPV6_EXPANDED_LOOPBACK: String = "0:0:0:0:0:0:0:1"
const MAX_DELAY_MS: int = 1000
const MAX_DATAGRAM_BYTES: int = 4096
const MAX_QUEUE_PER_DIRECTION: int = 256
const MAX_QUEUE_TIME_US: int = 5_000_000
const MAX_READS_PER_FRAME: int = 128
const DRAIN_MS: int = MAX_DELAY_MS * 2
const MIN_OUTAGE_MS: int = 100
const MAX_OUTAGE_MS: int = 30000
const MAX_OUTAGE_START_DELAY_MS: int = 10000
const MAX_BANDWIDTH_KBPS: int = 1000000
const MAX_SPIKE_DELAY_MS: int = 3000
const MAX_SCENARIO_STEPS: int = 16
const MIN_SCENARIO_STEP_MS: int = 100
const MAX_SCENARIO_STEP_MS: int = 120000
const MAX_SCENARIO_TOTAL_MS: int = 600000

## Identifies the datagram path affected by a link outage.
enum Direction {
	## Affects both traffic directions.
	BOTH,
	## Affects traffic from the client to the server.
	UPLINK,
	## Affects traffic from the server to the client.
	DOWNLINK,
}

## Emitted when a scenario is armed, changes phase, completes, or stops.
signal scenario_changed(step_index: int, label: String, reason: StringName)
## Emitted when a lag spike is armed, starts, expires, or is cancelled.
signal spike_changed(state: StringName, direction: Direction, extra_delay_ms: int)
## Emitted as the manual outage is armed, starts, expires, or is cancelled.
signal outage_changed(state: StringName, direction: Direction)


## One editable profile. Bandwidth limits use decimal kilobits per second, or zero for unlimited.
class Profile extends RefCounted:
	## Baseline delay per direction in milliseconds.
	var delay_ms: int
	## Maximum signed deviation from delay in milliseconds.
	var jitter_ms: int
	## Independent chance of dropping each packet, from zero to 100.
	var loss_percent: float
	## Client-to-server UDP payload bandwidth.
	var uplink_kbps: int
	## Server-to-client UDP payload bandwidth.
	var downlink_kbps: int


## One scenario phase. Use -1 for a connected link or a Direction for a complete outage.
class ScenarioStep extends Profile:
	## Short label displayed in the tool while the phase runs.
	var label: String
	## Time this phase owns the relay, in milliseconds.
	var duration_ms: int
	## Optional complete outage that applies throughout the phase.
	var outage_direction: int = -1


## Local relay traffic measured per direction. Byte counters exclude UDP/IP headers.
class Traffic extends RefCounted:
	## Datagram writes accepted by the outgoing socket.
	var forwarded: int
	## Datagrams rejected by the relay after identifying the route.
	var dropped: int
	## Datagram payload bytes accepted by the outgoing socket.
	var forwarded_bytes: int
	## Datagram payload bytes discarded by the relay.
	var dropped_bytes: int
	## Datagrams waiting in the delay queue when the snapshot was taken.
	var queued: int
	## Payload bytes waiting in the delay queue when the snapshot was taken.
	var queued_bytes: int
	## Datagrams discarded when the queue count or scheduling horizon was exceeded.
	var overflowed: int

## Detached counts and settings for the current or most recent local route.
class Stats extends RefCounted:
	## Whether this relay currently owns a live ENet guest.
	var active: bool
	## Whether packet delivery is currently blocked on at least one leg.
	var link_dropped: bool
	## Delay added in each direction, before jitter, in milliseconds.
	var one_way_delay_ms: int
	## Maximum signed deviation from the base delay, in milliseconds.
	var jitter_ms: int
	## Probability of random loss in each direction, from zero to 100.
	var loss_percent: float
	## Effective client-to-server payload bandwidth in kbit/s, zero for unlimited.
	var uplink_kbps: int
	## Effective server-to-client payload bandwidth in kbit/s, zero for unlimited.
	var downlink_kbps: int
	## Successful writes on both legs.
	var forwarded: int
	## Discards on both legs.
	var dropped: int
	## Current queued datagrams on both legs.
	var queued: int
	## Affected leg for a scheduled or active outage.
	var outage_direction: Direction = Direction.BOTH
	## Time until a scheduled outage starts, or zero once it starts or clears.
	var outage_starts_in_ms: int
	## Active time left, full duration if armed, -1 if indefinite, or zero when clear.
	var outage_remaining_ms: int
	## Direction affected by a scheduled or active lag spike.
	var spike_direction: Direction = Direction.BOTH
	## Extra delay applied to packets entering the relay during the spike, in milliseconds.
	var spike_delay_ms: int
	## Time before the lag spike starts, or zero when active or clear.
	var spike_starts_in_ms: int
	## Active time left, full duration when armed, or zero when clear.
	var spike_remaining_ms: int
	## Zero-based active scenario phase, or -1 while armed or idle.
	var scenario_step: int = -1
	## Number of phases when a scenario is active or armed, or zero when idle.
	var scenario_step_count: int
	## Label of the phase currently active.
	var scenario_label: String
	## Time before the current scenario phase ends, or zero while armed or idle.
	var scenario_remaining_ms: int
	## Time before the scenario's first phase starts, or zero when running or idle.
	var scenario_starts_in_ms: int
	## Detached client-to-server traffic counters.
	var uplink: Traffic
	## Detached server-to-client traffic counters.
	var downlink: Traffic


## Installs the emulator at the existing game transport factory boundary.
class Provider extends GameProvider:
	var owner: WeakRef

	func _register(bindings: GameBindings) -> void:
		bindings.session_transport(_make_transport)

	func _make_transport() -> SessionTransport:
		var transport := Transport.new()
		transport.owner = owner
		return transport


## Composes the built-in ENet factories and routes only supported local guest endpoints.
class Transport extends SessionTransport:
	var owner: WeakRef
	var enet := ENetSessionTransport.new()

	func _open_server(port: int, remote_limit: int, bind_address: String) -> SessionTransportResult:
		return enet.open_server(port, remote_limit, bind_address)

	func _open_client(endpoint: SessionEndpoint) -> SessionTransportResult:
		var emulator: Node = owner.get_ref()
		if emulator == null or not emulator.supports(endpoint.get_address()):
			return enet.open_client(endpoint)
		return emulator._open_route(enet, endpoint)

	func _disconnect_after_flush(peer: MultiplayerPeer, peer_id: int) -> void:
		enet.disconnect_after_flush(peer, peer_id)


## One intact UDP datagram scheduled against monotonic time.
class Datagram extends RefCounted:
	## Socket used to forward this packet.
	var destination: PacketPeerUDP
	## Intact UDP payload.
	var payload: PackedByteArray
	## Monotonic forwarding deadline in microseconds.
	var due_us: int
	## Exact direction of this packet on the owned relay.
	var direction: Direction


var _client_socket: PacketPeerUDP
var _server_socket: PacketPeerUDP
var _guest: WeakRef
var _client_port: int
var _retire_after_ms: int
var _queue: Array[Datagram] = []
var _random := RandomNumberGenerator.new()
var _outage_direction: Direction = Direction.BOTH
var _outage_starts_ms: int
var _outage_ends_ms: int
var _outage_started: bool
var _graceful_leave: bool
var _manual_profile: Profile = Profile.new()
var _uplink_due_us: int
var _downlink_due_us: int
var _spike_direction: Direction = Direction.BOTH
var _spike_delay_ms: int
var _spike_starts_ms: int
var _spike_ends_ms: int
var _spike_started: bool
var _scenario: Array[ScenarioStep] = []
var _scenario_begins_ms: int
var _scenario_duration_ms: int
var _scenario_loop: bool
var _scenario_index: int = -1
var _scenario_phase_end_ms: int
var _uplink: Traffic = Traffic.new()
var _downlink: Traffic = Traffic.new()


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_process(false)


## Return the demo's optional transport provider for use before Game boot.
func provider() -> GameProvider:
	var result := Provider.new()
	result.owner = weakref(self)
	return result


## Check whether a direct address names this PC's local test interface.
func supports(address: String) -> bool:
	return address.to_lower() in ["localhost", IPV4_LOOPBACK, IPV6_LOOPBACK, IPV6_EXPANDED_LOOPBACK]


## Apply a manual profile. New packets use it; queued packets retain their delivery times.
func set_conditions(one_way_delay_ms: int, jitter_ms: int, loss_percent: float,
		uplink_kbps: int = 0, downlink_kbps: int = 0) -> void:
	stop_scenario()
	_manual_profile.delay_ms = clampi(one_way_delay_ms, 0, MAX_DELAY_MS)
	_manual_profile.jitter_ms = clampi(jitter_ms, 0, MAX_DELAY_MS)
	_manual_profile.loss_percent = clampf(loss_percent, 0.0, 100.0) if is_finite(loss_percent) else 0.0
	_manual_profile.uplink_kbps = clampi(uplink_kbps, 0, MAX_BANDWIDTH_KBPS)
	_manual_profile.downlink_kbps = clampi(downlink_kbps, 0, MAX_BANDWIDTH_KBPS)
	_graceful_leave = false


## Drop both directions until manually resumed or this session ends.
func drop_link() -> void:
	if not _is_guest_live():
		return
	stop_scenario()
	cancel_spike()
	_outage_direction = Direction.BOTH
	_outage_starts_ms = Time.get_ticks_msec()
	_outage_ends_ms = -1
	_outage_started = false
	_graceful_leave = false
	_advance_outage(_outage_starts_ms)


## Schedule complete loss on a chosen leg. Returns false for invalid bounds or an inactive route.
func pause_link(duration_ms: int, direction: Direction = Direction.BOTH, start_delay_ms: int = 0) -> bool:
	if not _is_guest_live() or _graceful_leave or not _scenario.is_empty() or _spike_ends_ms != 0 \
			or not _valid_fault_window(duration_ms, direction, start_delay_ms):
		return false
	var now_ms: int = Time.get_ticks_msec()
	_outage_direction = direction
	_outage_starts_ms = now_ms + start_delay_ms
	_outage_ends_ms = _outage_starts_ms + duration_ms
	_outage_started = false
	_graceful_leave = false
	if start_delay_ms > 0:
		outage_changed.emit(&"armed", direction)
	_advance_outage(now_ms)
	return true


## Cancel a pending or active outage and return to the current base profile.
func resume_link() -> void:
	if not _scenario.is_empty():
		stop_scenario()
	_clear_outage(&"cancelled")
	_graceful_leave = false


## Add latency to arrivals on one or both legs for a bounded time, without dropping them deliberately.
func spike_latency(extra_delay_ms: int, duration_ms: int, direction: Direction = Direction.BOTH,
		start_delay_ms: int = 0) -> bool:
	if not _is_guest_live() or _graceful_leave or not _scenario.is_empty() or _outage_ends_ms != 0 \
			or _spike_ends_ms != 0 or extra_delay_ms < 1 or extra_delay_ms > MAX_SPIKE_DELAY_MS \
			or not _valid_fault_window(duration_ms, direction, start_delay_ms):
		return false
	var now_ms: int = Time.get_ticks_msec()
	_spike_direction = direction
	_spike_delay_ms = extra_delay_ms
	_spike_starts_ms = now_ms + start_delay_ms
	_spike_ends_ms = _spike_starts_ms + duration_ms
	if start_delay_ms > 0:
		spike_changed.emit(&"armed", direction, extra_delay_ms)
	else:
		_advance_spike(now_ms)
	return true


## Cancel an armed or active spike. Packets already delayed keep their scheduled times.
func cancel_spike() -> void:
	_clear_spike(&"cancelled")


## Snapshot validated phases, then apply them to this route until stopped or completed.
func start_scenario(steps: Array[ScenarioStep], loop: bool = false, start_delay_ms: int = 0) -> bool:
	if not _is_guest_live() or _graceful_leave or not _scenario.is_empty() or _outage_ends_ms != 0 \
			or _spike_ends_ms != 0 or steps.is_empty() or steps.size() > MAX_SCENARIO_STEPS \
			or start_delay_ms < 0 or start_delay_ms > MAX_OUTAGE_START_DELAY_MS:
		return false
	var checked: Array[ScenarioStep] = []
	var duration_ms: int = 0
	for step: ScenarioStep in steps:
		if not _valid_scenario_step(step):
			return false
		duration_ms += step.duration_ms
		if duration_ms > MAX_SCENARIO_TOTAL_MS:
			return false
		var copy := ScenarioStep.new()
		copy.label = step.label
		copy.duration_ms = step.duration_ms
		copy.delay_ms = step.delay_ms
		copy.jitter_ms = step.jitter_ms
		copy.loss_percent = step.loss_percent
		copy.uplink_kbps = step.uplink_kbps
		copy.downlink_kbps = step.downlink_kbps
		copy.outage_direction = step.outage_direction
		checked.append(copy)
	var now_ms: int = Time.get_ticks_msec()
	_scenario = checked
	_scenario_begins_ms = now_ms + start_delay_ms
	_scenario_duration_ms = duration_ms
	_scenario_loop = loop
	if start_delay_ms > 0:
		scenario_changed.emit(-1, "", &"armed")
	else:
		_advance_scenario(now_ms)
	return true


## Stop the current scenario and return to the manual connection profile.
func stop_scenario() -> void:
	_clear_scenario(&"stopped")


## Resume immediate, lossless forwarding for the game's normal disconnect action.
func restore_link() -> void:
	_graceful_leave = true
	_flush(0, true)
	_reset_pacing()


## Restore the previous impairment if the game declined to leave.
func cancel_restore_link() -> void:
	_graceful_leave = false
	_advance_conditions(Time.get_ticks_msec())


## Clear accumulated datagrams and bytes without discarding pending datagrams.
func reset_counters() -> void:
	for traffic: Traffic in [_uplink, _downlink]:
		traffic.forwarded = 0
		traffic.dropped = 0
		traffic.forwarded_bytes = 0
		traffic.dropped_bytes = 0
		traffic.overflowed = 0


## Sample relay counters without consuming them or changing the connection.
func get_stats() -> Stats:
	var stats := Stats.new()
	stats.active = _is_guest_live()
	var now_ms: int = Time.get_ticks_msec()
	var profile: Profile = _effective_profile()
	stats.one_way_delay_ms = profile.delay_ms
	stats.jitter_ms = profile.jitter_ms
	stats.loss_percent = profile.loss_percent
	stats.uplink_kbps = profile.uplink_kbps
	stats.downlink_kbps = profile.downlink_kbps
	if stats.active and not _graceful_leave and _outage_ends_ms != 0:
		stats.outage_direction = _outage_direction
		stats.outage_starts_in_ms = maxi(0, _outage_starts_ms - now_ms)
		stats.outage_remaining_ms = -1 if _outage_ends_ms < 0 else maxi(0, _outage_ends_ms - maxi(now_ms, _outage_starts_ms))
		stats.link_dropped = stats.outage_starts_in_ms == 0 and stats.outage_remaining_ms != 0
	if stats.active and not _graceful_leave and _spike_ends_ms != 0:
		stats.spike_direction = _spike_direction
		stats.spike_delay_ms = _spike_delay_ms
		stats.spike_starts_in_ms = maxi(0, _spike_starts_ms - now_ms)
		stats.spike_remaining_ms = maxi(0, _spike_ends_ms - maxi(now_ms, _spike_starts_ms))
	if stats.active and not _graceful_leave and not _scenario.is_empty():
		stats.scenario_step_count = _scenario.size()
		stats.scenario_starts_in_ms = maxi(0, _scenario_begins_ms - now_ms)
		stats.scenario_step = _scenario_index
		if _scenario_index >= 0:
			var step: ScenarioStep = _scenario[_scenario_index]
			stats.scenario_label = step.label
			stats.scenario_remaining_ms = maxi(0, _scenario_phase_end_ms - now_ms)
			if step.outage_direction != -1:
				stats.link_dropped = true
				stats.outage_direction = step.outage_direction as Direction
				stats.outage_remaining_ms = stats.scenario_remaining_ms
	stats.uplink = _sample_traffic(_uplink)
	stats.downlink = _sample_traffic(_downlink)
	stats.forwarded = _uplink.forwarded + _downlink.forwarded
	stats.dropped = _uplink.dropped + _downlink.dropped
	stats.queued = _queue.size()
	return stats


## Create separate local sockets for the guest-facing and server-facing ends of this route.
func _open_route(enet: ENetSessionTransport, endpoint: SessionEndpoint) -> SessionTransportResult:
	if _is_guest_live():
		return SessionTransportResult.failed(ERR_ALREADY_IN_USE)
	if _client_socket != null:
		# The old guest may have just written its ENet disconnect datagram on leave.
		_pump(Time.get_ticks_usec())
	_close_route()
	reset_counters()
	var address: String = endpoint.get_address().to_lower()
	if address == "localhost":
		address = IPV4_LOOPBACK
	var server_port: int = endpoint.get_port()
	_client_socket = PacketPeerUDP.new()
	_server_socket = PacketPeerUDP.new()
	var error: Error = _client_socket.bind(0, address)
	if error == OK:
		error = _server_socket.bind(0, address)
	if error == OK:
		if server_port in [_client_socket.get_local_port(), _server_socket.get_local_port()]:
			error = ERR_ALREADY_IN_USE
		else:
			error = _server_socket.connect_to_host(address, server_port)
	if error != OK:
		_close_route()
		return SessionTransportResult.failed(error)
	var local := SessionEndpoint.direct(address, _client_socket.get_local_port())
	var result := enet.open_client(local)
	if result.get_error() == OK:
		_guest = weakref(result.get_peer())
		set_process(true)
	else:
		_close_route()
	return result


## Report the ENet connection's actual lifecycle rather than inferring it from UDP traffic.
func _is_guest_live() -> bool:
	var guest: MultiplayerPeer = _guest.get_ref() if _guest != null else null
	return guest != null and guest.get_connection_status() != MultiplayerPeer.CONNECTION_DISCONNECTED


## Close the exact route and discard pending packets before the next generation starts.
func _close_route() -> void:
	set_process(false)
	resume_link()
	cancel_spike()
	_queue.clear()
	_uplink.queued = 0
	_uplink.queued_bytes = 0
	_downlink.queued = 0
	_downlink.queued_bytes = 0
	_reset_pacing()
	_guest = null
	_client_port = 0
	_retire_after_ms = 0
	if _client_socket != null:
		_client_socket.close()
		_client_socket = null
	if _server_socket != null:
		_server_socket.close()
		_server_socket = null


func _exit_tree() -> void:
	_close_route()


func _process(_delta: float) -> void:
	var now_us: int = Time.get_ticks_usec()
	@warning_ignore("integer_division")
	var now_ms: int = now_us / 1000
	if not _is_guest_live():
		if _retire_after_ms == 0:
			_retire_after_ms = now_ms + DRAIN_MS
			stop_scenario()
			cancel_spike()
		elif now_ms >= _retire_after_ms:
			_close_route()
			return
	_pump(now_us)


## Process a bounded amount of UDP work, including frames where the game is paused.
func _pump(now_us: int) -> void:
	@warning_ignore("integer_division")
	var now_ms: int = now_us / 1000
	_advance_conditions(now_ms)
	_flush(now_us)
	var profile: Profile = _effective_profile()
	_read(_client_socket, _server_socket, now_us, Direction.UPLINK, profile)
	_read(_server_socket, _client_socket, now_us, Direction.DOWNLINK, profile)


## Accept the exact guest socket and trust only replies from the connected server endpoint.
func _read(source: PacketPeerUDP, destination: PacketPeerUDP, now_us: int, direction: Direction, profile: Profile) -> void:
	var traffic: Traffic = _traffic_for(direction)
	var kbps: int = profile.uplink_kbps if direction == Direction.UPLINK else profile.downlink_kbps
	var spike_ms: int = _spike_delay_ms if _spike_started and _matches_direction(_spike_direction, direction) else 0
	for _index: int in mini(source.get_available_packet_count(), MAX_READS_PER_FRAME):
		var payload: PackedByteArray = source.get_packet()
		if source.get_packet_error() != OK:
			continue
		if (direction == Direction.UPLINK and not _accept_guest(source)) or _client_port == 0:
			continue
		if payload.is_empty() or payload.size() > MAX_DATAGRAM_BYTES:
			_record_drop(traffic, payload.size())
			continue
		if _graceful_leave:
			_send(destination, payload, traffic)
			continue
		if _is_blocked(direction) or (profile.loss_percent > 0.0 and _random.randf() * 100.0 < profile.loss_percent):
			_record_drop(traffic, payload.size())
			continue
		var delay_ms: int = clampi(profile.delay_ms + _random.randi_range(-profile.jitter_ms, profile.jitter_ms), 0, MAX_DELAY_MS) + spike_ms
		_schedule(destination, payload, now_us, delay_ms, kbps, direction, traffic)


## Schedule datagrams at independent directional payload rates without accumulating idle credit.
func _schedule(destination: PacketPeerUDP, payload: PackedByteArray, now_us: int, delay_ms: int,
		kbps: int, direction: Direction, traffic: Traffic) -> void:
	var due_us: int = now_us + delay_ms * 1000
	var previous_due_us: int = _uplink_due_us if direction == Direction.UPLINK else _downlink_due_us
	if kbps > 0:
		# kbit/s is bits per millisecond. Use microseconds so sub-ms packets do not all round to 1 ms.
		var serialize_us: int = ceili(float(payload.size() * 8000) / kbps)
		due_us = maxi(due_us, previous_due_us) + serialize_us
	else:
		# Preserve already reserved bandwidth deadlines through profile transitions.
		due_us = maxi(due_us, previous_due_us)
	if due_us <= now_us:
		_send(destination, payload, traffic)
		return
	if traffic.queued >= MAX_QUEUE_PER_DIRECTION or due_us - now_us > MAX_QUEUE_TIME_US:
		traffic.overflowed += 1
		_record_drop(traffic, payload.size())
		return
	if kbps > 0:
		if direction == Direction.UPLINK:
			_uplink_due_us = due_us
		else:
			_downlink_due_us = due_us
	var packet := Datagram.new()
	packet.destination = destination
	packet.payload = payload
	packet.due_us = due_us
	packet.direction = direction
	_queue.append(packet)
	traffic.queued += 1
	traffic.queued_bytes += payload.size()


## Lock the client-facing socket to the source tuple used by the owned ENet guest.
func _accept_guest(source: PacketPeerUDP) -> bool:
	var ip: String = source.get_packet_ip()
	var port: int = source.get_packet_port()
	if not supports(ip):
		return false
	if _client_port == 0:
		var guest: ENetMultiplayerPeer = _guest.get_ref() if _guest != null else null
		if guest == null or guest.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
			return false
		if guest.get_host().get_local_port() != port or source.set_dest_address(ip, port) != OK:
			return false
		_client_port = port
	return port == _client_port


## Forward due packets in receive order when they share a deadline.
func _flush(now_us: int, all: bool = false) -> void:
	if _queue.is_empty():
		return
	var next: Array[Datagram] = []
	for packet: Datagram in _queue:
		var traffic: Traffic = _traffic_for(packet.direction)
		if _is_blocked(packet.direction) and not _graceful_leave:
			_record_drop(traffic, packet.payload.size())
		elif all or packet.due_us <= now_us:
			_send(packet.destination, packet.payload, traffic)
		else:
			next.append(packet)
			continue
		traffic.queued -= 1
		traffic.queued_bytes -= packet.payload.size()
	_queue = next


## Count only successful socket writes as forwarded packets.
func _send(destination: PacketPeerUDP, payload: PackedByteArray, traffic: Traffic) -> void:
	if destination.put_packet(payload) == OK:
		traffic.forwarded += 1
		traffic.forwarded_bytes += payload.size()
	else:
		_record_drop(traffic, payload.size())


## Start or finish the outage once using the relay's monotonic clock.
func _advance_outage(now_ms: int) -> void:
	if _outage_ends_ms == 0:
		return
	if _outage_ends_ms > 0 and now_ms >= _outage_ends_ms:
		_clear_outage(&"completed")
	elif not _outage_started and now_ms >= _outage_starts_ms:
		_outage_started = true
		# Drop any queued traffic on the failed leg at the start of the outage.
		_reset_pacing(_outage_direction)
		_flush(now_ms * 1000)
		outage_changed.emit(&"active", _outage_direction)


## Reset a manual outage before announcing that forwarding has resumed.
func _clear_outage(state: StringName) -> void:
	if _outage_ends_ms == 0:
		return
	var direction: Direction = _outage_direction
	_outage_direction = Direction.BOTH
	_outage_ends_ms = 0
	_outage_starts_ms = 0
	_outage_started = false
	outage_changed.emit(state, direction)


## Advance temporary conditions against the same monotonic timeline before processing packets.
func _advance_conditions(now_ms: int) -> void:
	if _graceful_leave:
		return
	_advance_outage(now_ms)
	_advance_spike(now_ms)
	_advance_scenario(now_ms)


## Reject out-of-range timing and directions before changing a transient fault.
func _valid_fault_window(duration_ms: int, direction: Direction, start_delay_ms: int) -> bool:
	return duration_ms >= MIN_OUTAGE_MS and duration_ms <= MAX_OUTAGE_MS \
		and start_delay_ms >= 0 and start_delay_ms <= MAX_OUTAGE_START_DELAY_MS \
		and direction in [Direction.BOTH, Direction.UPLINK, Direction.DOWNLINK]


## Advance a scheduled spike without modifying existing packet deadlines.
func _advance_spike(now_ms: int) -> void:
	if _spike_ends_ms == 0:
		return
	if now_ms >= _spike_ends_ms:
		_clear_spike(&"completed")
	elif not _spike_started and now_ms >= _spike_starts_ms:
		_spike_started = true
		spike_changed.emit(&"active", _spike_direction, _spike_delay_ms)


## Clear the spike before notifying an observer so callbacks see the restored link state.
func _clear_spike(state: StringName) -> void:
	if _spike_ends_ms == 0:
		return
	var direction: Direction = _spike_direction
	var extra_delay_ms: int = _spike_delay_ms
	_spike_starts_ms = 0
	_spike_ends_ms = 0
	_spike_started = false
	_spike_delay_ms = 0
	spike_changed.emit(state, direction, extra_delay_ms)


## Reject malformed phases rather than partially applying a scenario or clipping an authored step.
func _valid_scenario_step(step: ScenarioStep) -> bool:
	return step != null and step.label.length() <= 80 \
		and step.duration_ms >= MIN_SCENARIO_STEP_MS and step.duration_ms <= MAX_SCENARIO_STEP_MS \
		and step.delay_ms >= 0 and step.delay_ms <= MAX_DELAY_MS \
		and step.jitter_ms >= 0 and step.jitter_ms <= MAX_DELAY_MS \
		and is_finite(step.loss_percent) and step.loss_percent >= 0.0 and step.loss_percent <= 100.0 \
		and step.uplink_kbps >= 0 and step.uplink_kbps <= MAX_BANDWIDTH_KBPS \
		and step.downlink_kbps >= 0 and step.downlink_kbps <= MAX_BANDWIDTH_KBPS \
		and step.outage_direction in [-1, Direction.BOTH, Direction.UPLINK, Direction.DOWNLINK]


## Locate the current phase in bounded work even when a paused frame skipped multiple cycles.
func _advance_scenario(now_ms: int) -> void:
	if _scenario.is_empty() or now_ms < _scenario_begins_ms or now_ms < _scenario_phase_end_ms:
		return
	var elapsed_ms: int = now_ms - _scenario_begins_ms
	if not _scenario_loop and elapsed_ms >= _scenario_duration_ms:
		_clear_scenario(&"completed")
		return
	var cycle_offset_ms: int = elapsed_ms % _scenario_duration_ms
	var phase_end_ms: int = 0
	for index: int in _scenario.size():
		var step: ScenarioStep = _scenario[index]
		phase_end_ms += step.duration_ms
		if cycle_offset_ms >= phase_end_ms:
			continue
		_scenario_phase_end_ms = now_ms - cycle_offset_ms + phase_end_ms
		_scenario_index = index
		if step.outage_direction != -1:
			_reset_pacing(step.outage_direction as Direction)
			_flush(now_ms * 1000)
		scenario_changed.emit(index, step.label, &"phase")
		return


## Restore the manual profile without releasing packets that were already scheduled by a phase.
func _clear_scenario(reason: StringName) -> void:
	if _scenario.is_empty():
		return
	var phase: int = _scenario_index
	var label: String = _scenario[phase].label if phase >= 0 else ""
	_scenario.clear()
	_scenario_index = -1
	_scenario_begins_ms = 0
	_scenario_phase_end_ms = 0
	_scenario_duration_ms = 0
	_scenario_loop = false
	scenario_changed.emit(phase, label, reason)


## Return the active scenario phase, or the manual profile while armed or idle.
func _effective_profile() -> Profile:
	return _scenario[_scenario_index] if _scenario_index >= 0 else _manual_profile


## Release pacing reservations for datagrams discarded together during a link outage or teardown.
func _reset_pacing(direction: Direction = Direction.BOTH) -> void:
	if _matches_direction(direction, Direction.UPLINK):
		_uplink_due_us = 0
	if _matches_direction(direction, Direction.DOWNLINK):
		_downlink_due_us = 0


## Check whether a fault targets this direction or both relay legs.
func _matches_direction(affected: Direction, direction: Direction) -> bool:
	return affected == Direction.BOTH or affected == direction


## Check the effective outage independently of configured delay and random loss.
func _is_blocked(direction: Direction) -> bool:
	if _scenario_index >= 0:
		var affected: int = _scenario[_scenario_index].outage_direction
		return affected != -1 and _matches_direction(affected as Direction, direction)
	return _outage_started and _matches_direction(_outage_direction, direction)


## Resolve a packet direction to its live counter set.
func _traffic_for(direction: Direction) -> Traffic:
	return _uplink if direction == Direction.UPLINK else _downlink


## Account a discard after the datagram has been identified as part of the owned route.
func _record_drop(traffic: Traffic, payload_bytes: int) -> void:
	traffic.dropped += 1
	traffic.dropped_bytes += payload_bytes


## Copy accumulators so later packets or resets cannot mutate an earlier observation.
func _sample_traffic(source: Traffic) -> Traffic:
	var snapshot := Traffic.new()
	snapshot.forwarded = source.forwarded
	snapshot.dropped = source.dropped
	snapshot.forwarded_bytes = source.forwarded_bytes
	snapshot.dropped_bytes = source.dropped_bytes
	snapshot.queued = source.queued
	snapshot.queued_bytes = source.queued_bytes
	snapshot.overflowed = source.overflowed
	return snapshot
