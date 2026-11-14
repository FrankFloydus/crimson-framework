# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Checks the demo's relay against a separate, unmodified ENet host.

const Fixture := preload("res://tests/fixtures/network_fixture.gd")
const Profile := preload("res://demo/tests/player_profile_fixture.gd")
const Emulator := preload("res://demo/session/network_emulator.gd")
const ADDRESS: String = "127.0.0.1"
const ONE_WAY_MS: int = 75
const JITTER_MS: int = 8
const MINIMUM_RTT_MS: int = 120
const SIMULATED_LOSS_PERCENT: float = 25.0
const TIMEOUT_MS: int = 12000
const FIXTURE_ENET_TIMEOUT_MS: int = 2500
const FIXTURE_PING_INTERVAL_MS: int = 150
const OUTAGE_START_DELAY_MS: int = 250
const OUTAGE_DURATION_MS: int = 650
const CANCEL_DURATION_MS: int = 1500


## Produce repeatable traffic in each direction even when the players are idle.
class Probe extends Node:
	const INTERVAL_MS: int = 30

	var received: int = 0
	var _remote_id: int = 0
	var _next_send_ms: int = 0

	func start(remote_id: int) -> void:
		_remote_id = remote_id
		set_process(true)

	func stop() -> void:
		_remote_id = 0
		set_process(false)

	func _process(_delta: float) -> void:
		var now: int = Time.get_ticks_msec()
		if now < _next_send_ms or not multiplayer.get_peers().has(_remote_id):
			return
		_next_send_ms = now + INTERVAL_MS
		rpc_id(_remote_id, &"receive")

	# ENet can suppress every unreliable packet after a sufficiently large RTT change.
	@rpc("any_peer", "call_remote", "reliable", 0)
	func receive() -> void:
		if _remote_id > 0 and multiplayer.get_remote_sender_id() == _remote_id:
			received += 1


var _owner: Node
var _emulator: Emulator
var _probe: Probe
var _game: Game
var _reason: StringName


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if not _check(args.size() == 2 and args[0] in ["host", "client"], "Pass host|client and the test port."):
		quit(1)
		return
	Engine.max_fps = 120
	_owner = Node.new()
	_owner.name = &"DebuggerFixture"
	root.add_child(_owner)
	_emulator = Emulator.new()
	_owner.add_child(_emulator)
	_probe = Probe.new()
	_probe.name = &"DebuggerProbe"
	_owner.add_child(_probe)
	_probe.stop()
	var profile: PlayerControlProfile = Profile.build()
	var config: GameConfig = Game.configure(Fixture.world(), Fixture.player()).lan(4).on_port(args[1].to_int()).player_controls(profile)
	if args[0] == "client":
		config = config.provider(_emulator.provider())
	_game = config.boot(_owner)
	_game.session_ended.connect(func(reason: StringName) -> void: _reason = reason)
	var passed: bool = _check(_game.boot_result().ok, "Boot the network debugger fixture.")
	if passed:
		passed = await _host() if args[0] == "host" else await _client(profile)
	var result: GameResult = await _game.leave()
	if passed and result.ok:
		print("CRIMSON_NETWORK_DEBUGGER_PASS %s" % args[0])
	quit(0 if passed and result.ok else 1)


func _host() -> bool:
	var result: GameResult = await _game.host(ADDRESS)
	if not _check(result.ok, "Start the standard ENet authority."):
		return false
	var initial: GameNetworkDiagnostics = _game.network_diagnostics()
	if not _check(initial.role == GameNetworkDiagnostics.HOST and initial.transport == &"enet"
			and initial.round_trip_ms < 0.0 and initial.state_age_ms == -1 and initial.player_number == 1,
			"Host starts in slot one with no RTT or remote state age."):
		return false
	print("CRIMSON_NETWORK_DEBUGGER_READY")
	var first_id: int = await _next_guest(0)
	if not _check(first_id > 1, "Admit the relay guest."):
		return false
	_probe.start(first_id)
	_shorten_test_timeout(first_id, FIXTURE_ENET_TIMEOUT_MS)
	if not _check(await _wait(func() -> bool: return _game.get_player(first_id) == null, TIMEOUT_MS * 2),
			"Blackhole retires the first guest."):
		return false
	_probe.stop()
	var second_id: int = await _next_guest(first_id)
	if not _check(second_id > 1, "A fresh route rejoins through normal admission."):
		return false
	return _check(await _wait(func() -> bool: return _game.get_player(second_id) == null), "Normal leave retires the second guest.")


func _client(profile: PlayerControlProfile) -> bool:
	var result: GameResult = await _game.join(ADDRESS)
	if not _check(result.ok and _emulator.get_stats().active, "Join uses the installed loopback route."):
		return false
	var first: GameNetworkDiagnostics = _game.network_diagnostics()
	if not _check(first.role == GameNetworkDiagnostics.GUEST and first.transport == &"enet" and first.peer_id > 1
			and first.player_number == 2 and first.movement_mode == GameNetworkDiagnostics.GUEST_PREDICTED,
			"Guest diagnostics reflect the admitted ENet owner in slot two."):
		return false
	var actor: CharacterBody3D = _game.get_local_player() as CharacterBody3D
	var view: Camera3D = root.get_camera_3d()
	if not _check(actor != null and view != null and profile.boot(actor, view).is_ready(), "Attach the guest input stream."):
		return false
	_probe.start(1)
	if not _check(await _wait(func() -> bool: return _probe.received > 0), "Receive host probes through the relay."):
		return false
	_emulator.set_conditions(ONE_WAY_MS, JITTER_MS, 0.0)
	if not _check(await _wait(func() -> bool: return _game.network_diagnostics().round_trip_ms >= MINIMUM_RTT_MS),
			"Measured ENet RTT rises with bidirectional delay."):
		return false
	var delayed: GameNetworkDiagnostics = _game.network_diagnostics()
	if not _check(delayed.simulation_tick > first.simulation_tick and delayed.state_age_ms >= 0,
			"A fresh snapshot reports a later simulation tick and received state age."):
		return false
	_emulator.set_conditions(ONE_WAY_MS, JITTER_MS, SIMULATED_LOSS_PERCENT)
	if not _check(await _wait(func() -> bool: return _emulator.get_stats().dropped > 0), "Packet loss drops live datagrams."):
		return false
	var lossy: Emulator.Stats = _emulator.get_stats()
	if not _check(lossy.forwarded > 0 and lossy.loss_percent == SIMULATED_LOSS_PERCENT, "The relay forwards and drops independently."):
		return false
	if not await _timed_outage(first.peer_id):
		return false
	if not await _latency_spike(first.peer_id) or not await _run_scenario(first.peer_id):
		return false
	_probe.stop()
	_emulator.set_conditions(0, 0, 0.0)
	if not _check(await _wait(func() -> bool: return _emulator.get_stats().queued == 0),
			"Previously delayed packets finish before the final blackhole test."):
		return false
	_shorten_test_timeout(1, FIXTURE_ENET_TIMEOUT_MS)
	_emulator.drop_link()
	if not _check(_emulator.get_stats().active and _emulator.get_stats().link_dropped and _emulator.get_stats().queued == 0
			and _emulator.get_stats().outage_remaining_ms == -1,
			"Drop link immediately discards pending datagrams and starts a blackhole."):
		return false
	if not _check(await _wait(func() -> bool: return _reason != &""), "ENet detects the dead link."):
		return false
	var drop_reason: StringName = _reason
	if not _check(drop_reason == &"server_disconnected" and not _emulator.get_stats().active
			and _game.network_diagnostics().role == GameNetworkDiagnostics.IDLE,
			"The session retires after ENet times out."):
		return false
	# ENet checks each peer on its own reliable-send schedule. Allow the authority's
	# outstanding ping to time out before checking that rejoin reuses the old seat.
	await create_timer(FIXTURE_ENET_TIMEOUT_MS * 2.0 / 1000.0).timeout
	result = await _game.join(ADDRESS)
	var second: GameNetworkDiagnostics = _game.network_diagnostics()
	var fresh: Emulator.Stats = _emulator.get_stats()
	if not _check(result.ok and fresh.active and not fresh.link_dropped and fresh.dropped == 0
			and second.peer_id != first.peer_id and second.player_number == first.player_number
			and first.role == GameNetworkDiagnostics.GUEST,
			"Reconnect gets a clean route. ok=%s, active=%s, dropped=%d, peer=%d, slot=%d." %
			[result.ok, fresh.active, fresh.dropped, second.peer_id, second.player_number]):
		return false
	_emulator.set_conditions(ONE_WAY_MS, JITTER_MS, 100.0)
	_emulator.drop_link()
	_emulator.restore_link()
	_emulator.cancel_restore_link()
	if not _check(_emulator.get_stats().link_dropped, "A declined leave can restore the existing outage."):
		return false
	_emulator.restore_link()
	result = await _game.leave()
	if not _check(result.ok and not _emulator.get_stats().active, "Clean disconnect retires the route despite configured full loss."):
		return false
	print("CRIMSON_NETWORK_DEBUGGER_RTT_MS %d forwarded=%d dropped=%d reason=%s" %
		[roundi(delayed.round_trip_ms), lossy.forwarded, lossy.dropped, drop_reason])
	return true


## Queue real uplink packets during a transient delay, then retain their deadlines past expiry.
func _latency_spike(peer_id: int) -> bool:
	_emulator.set_conditions(0, 0, 0.0)
	if not _check(await _wait(func() -> bool: return _emulator.get_stats().queued == 0), "Start with an empty spike queue."):
		return false
	_emulator.reset_counters()
	var events: Array[StringName] = []
	var observe := func(state: StringName, _direction: Emulator.Direction, _delay_ms: int) -> void: events.append(state)
	_emulator.spike_changed.connect(observe)
	var scheduled: bool = _emulator.spike_latency(650, 300, Emulator.Direction.UPLINK, 100)
	var armed: Emulator.Stats = _emulator.get_stats()
	if not _check(scheduled and armed.spike_starts_in_ms > 0 and armed.spike_remaining_ms == 300
			and armed.spike_delay_ms == 650 and events == [&"armed"], "The lag spike can be armed with a countdown."):
		return false
	if not _check(await _wait(func() -> bool: return _emulator.get_stats().uplink.queued > 0),
			"The spike delays actual ENet uplink datagrams."):
		return false
	var downlink_received: int = _probe.received
	var downlink_forwarded: int = _emulator.get_stats().downlink.forwarded_bytes
	if not _check(await _wait(func() -> bool: return events.back() == &"completed"), "The relay processes the spike expiry."):
		return false
	var expired: Emulator.Stats = _emulator.get_stats()
	if not _check(expired.uplink.queued > 0 and expired.uplink.dropped == 0 and expired.downlink.queued == 0
			and expired.downlink.dropped == 0 and expired.downlink.forwarded_bytes > downlink_forwarded
			and events == [&"armed", &"active", &"completed"],
			"Expiry preserves scheduled uplink packets while the relay forwards downlink traffic. " +
			"Observed queued up=%d down=%d, dropped=%d, down bytes=%d/%d, events=%s." %
			[expired.uplink.queued, expired.downlink.queued, expired.uplink.dropped,
				expired.downlink.forwarded_bytes, downlink_forwarded, events]):
		return false
	# A downlink RPC can wait for its ACK on the delayed uplink. Check receipt separately.
	var drained := func() -> bool:
		return _probe.received > downlink_received and _emulator.get_stats().queued == 0
	if not _check(await _wait(drained) and _game.network_diagnostics().peer_id == peer_id,
			"Delayed traffic drains, application probes arrive, and the same peer remains admitted."):
		return false
	_emulator.spike_changed.disconnect(observe)
	print("CRIMSON_NETWORK_DEBUGGER_SPIKE_RETAINED %d" % expired.uplink.queued)
	return true


## Exercise detached authored phases, timed completion, looping, stop, and manual profile restoration.
func _run_scenario(peer_id: int) -> bool:
	_emulator.set_conditions(12, 0, 0.0, 4096, 8192)
	var outage := Emulator.ScenarioStep.new()
	outage.label = "Uplink interruption"
	outage.duration_ms = 200
	outage.outage_direction = Emulator.Direction.UPLINK
	var degraded := Emulator.ScenarioStep.new()
	degraded.label = "Recovery"
	degraded.duration_ms = 400
	degraded.delay_ms = 25
	degraded.uplink_kbps = 2048
	degraded.downlink_kbps = 4096
	var steps: Array[Emulator.ScenarioStep] = [outage, degraded]
	var events: Array[StringName] = []
	var record := func(_index: int, _label: String, reason: StringName) -> void: events.append(reason)
	_emulator.scenario_changed.connect(record)
	_emulator.reset_counters()
	if not _check(_emulator.start_scenario(steps, false, 100), "Arm a valid finite scenario."):
		return false
	degraded.uplink_kbps = 12
	var armed: Emulator.Stats = _emulator.get_stats()
	if not _check(armed.scenario_step == -1 and armed.scenario_step_count == 2 and armed.scenario_starts_in_ms > 0
			and armed.uplink_kbps == 4096, "A scenario countdown leaves the manual profile active."):
		return false
	var second_phase := func() -> bool: return _emulator.get_stats().scenario_step == 1
	if not _check(await _wait(second_phase), "The scenario advances to the authored recovery phase."):
		return false
	var recovery: Emulator.Stats = _emulator.get_stats()
	if not _check(recovery.scenario_label == "Recovery" and recovery.uplink_kbps == 2048
			and recovery.downlink_kbps == 4096 and recovery.uplink.dropped > 0 and recovery.downlink.dropped == 0,
			"The runner uses a detached phase snapshot and drops only the chosen direction."):
		return false
	var finished := func() -> bool: return _emulator.get_stats().scenario_step_count == 0
	if not _check(await _wait(finished), "A finite scenario completes automatically."):
		return false
	var baseline: Emulator.Stats = _emulator.get_stats()
	if not _check(baseline.one_way_delay_ms == 12 and baseline.uplink_kbps == 4096 and baseline.downlink_kbps == 8192
			and events == [&"armed", &"phase", &"phase", &"completed"], "Completion restores the previous manual profile."):
		return false
	degraded.uplink_kbps = 2048
	if not _check(_emulator.start_scenario(steps, true), "Start the same phases as a loop."):
		return false
	var next_cycle := func() -> bool: return events.size() >= 7
	if not _check(await _wait(next_cycle), "A looping scenario restarts after its last phase."):
		return false
	_emulator.stop_scenario()
	var stopped: Emulator.Stats = _emulator.get_stats()
	if not _check(stopped.scenario_step_count == 0 and stopped.uplink_kbps == 4096 and events.back() == &"stopped"
			and _game.network_diagnostics().peer_id == peer_id, "Stopping restores the baseline on the same ENet session."):
		return false
	_emulator.scenario_changed.disconnect(record)
	print("CRIMSON_NETWORK_DEBUGGER_SCENARIO_RESTORED")
	return true


## Keep the same ENet peer through a scheduled one-direction outage and its automatic recovery.
func _timed_outage(peer_id: int) -> bool:
	_emulator.set_conditions(ONE_WAY_MS, JITTER_MS, 0.0)
	_emulator.reset_counters()
	var empty: Emulator.Stats = _emulator.get_stats()
	if not _check(empty.forwarded == 0 and empty.dropped == 0 and empty.uplink.forwarded_bytes == 0,
			"Reset clears relay datagrams and bytes."):
		return false
	if not _check(not _emulator.pause_link(0) and not _emulator.pause_link(OUTAGE_DURATION_MS, Emulator.Direction.BOTH, -1),
			"An invalid outage duration or start delay leaves the link unchanged."):
		return false
	if not _check(_emulator.pause_link(OUTAGE_DURATION_MS, Emulator.Direction.UPLINK, OUTAGE_START_DELAY_MS),
			"Schedule a delayed client-to-server outage."):
		return false
	var armed: Emulator.Stats = _emulator.get_stats()
	if not _check(armed.outage_starts_in_ms > 0 and armed.outage_remaining_ms == OUTAGE_DURATION_MS and not armed.link_dropped,
			"A scheduled outage exposes its start delay and does not yet drop the link."):
		return false
	if not _check(await _wait(func() -> bool: return _emulator.get_stats().link_dropped), "The scheduled outage starts."):
		return false
	# Clearing delay during the outage must not accidentally flush the blocked uplink.
	_emulator.set_conditions(0, 0, 0.0)
	var downlink_received: int = _probe.received
	var traffic_observed := func() -> bool:
		return _emulator.get_stats().uplink.dropped > 0 and _probe.received > downlink_received
	if not _check(await _wait(traffic_observed, OUTAGE_DURATION_MS), "The uplink drops datagrams and downlink still delivers probes."):
		return false
	var during: Emulator.Stats = _emulator.get_stats()
	if not _check(during.link_dropped and during.outage_direction == Emulator.Direction.UPLINK and during.downlink.dropped == 0
			and during.downlink.forwarded_bytes > 0 and during.uplink.dropped_bytes > 0 and during.uplink.queued == 0,
			"The uplink alone drops packets and its detached traffic accounts for the payload."):
		return false
	var uplink_resumed := func() -> bool:
		var sample: Emulator.Stats = _emulator.get_stats()
		return not sample.link_dropped and sample.uplink.forwarded > during.uplink.forwarded
	if not _check(await _wait(uplink_resumed), "Automatic recovery resumes uplink forwarding."):
		return false
	var recovered: Emulator.Stats = _emulator.get_stats()
	if not _check(recovered.outage_remaining_ms == 0 and recovered.active and _reason == &""
			and _game.network_diagnostics().peer_id == peer_id and _game.get_phase() == Game.RUNNING,
			"A short outage recovers in place without retiring or rejoining the game."):
		return false
	if not _check(_emulator.pause_link(CANCEL_DURATION_MS, Emulator.Direction.DOWNLINK), "Start a downlink outage."):
		return false
	if not _check(await _wait(func() -> bool: return _emulator.get_stats().downlink.dropped > 0), "The downlink drops real datagrams."):
		return false
	_emulator.resume_link()
	var resumed: Emulator.Stats = _emulator.get_stats()
	if not _check(not resumed.link_dropped and resumed.outage_remaining_ms == 0 and resumed.downlink.dropped_bytes > 0
			and resumed.uplink.dropped == recovered.uplink.dropped and empty.forwarded == 0,
			"Manual resume clears the deadline and the detached reset snapshot remains unchanged."):
		return false
	print("CRIMSON_NETWORK_DEBUGGER_OUTAGE_UPLINK %d DOWNLINK %d SAME_PEER" % [resumed.uplink.dropped, resumed.downlink.dropped])
	return true


## Use shorter ENet liveness deadlines only in this fixture.
func _shorten_test_timeout(peer_id: int, timeout_ms: int) -> void:
	var enet := _owner.get_multiplayer().multiplayer_peer as ENetMultiplayerPeer
	var peer: ENetPacketPeer = enet.get_peer(peer_id)
	peer.ping_interval(FIXTURE_PING_INTERVAL_MS)
	peer.set_timeout(8, timeout_ms, timeout_ms)


func _next_guest(previous: int) -> int:
	var deadline: int = Time.get_ticks_msec() + TIMEOUT_MS
	while Time.get_ticks_msec() < deadline:
		for peer_id: int in _owner.get_multiplayer().get_peers():
			if peer_id != previous and _game.get_player(peer_id) != null:
				return peer_id
		await process_frame
	return 0


func _wait(condition: Callable, timeout_ms: int = TIMEOUT_MS) -> bool:
	var deadline: int = Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if condition.call():
			return true
		await process_frame
	return false


func _check(condition: bool, message: String) -> bool:
	if not condition:
		push_error(message)
	return condition
