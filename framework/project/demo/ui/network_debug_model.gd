# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Reads this demo process and issues its local network test actions.

const NetworkEmulator := preload("res://demo/session/network_emulator.gd")
const SimulatorSession := preload("res://demo/session/network_simulator_session.gd")

## The latest feedback from a requested disconnect.
signal disconnect_finished(ok: bool, message: String)
## Records every scenario phase even when the tab or game window is unfocused.
signal scenario_changed(step_index: int, label: String, reason: StringName)
## Records a one-shot spike without relying on the chart sampling interval.
signal spike_changed(state: StringName, direction: NetworkEmulator.Direction, extra_delay_ms: int)
## Records manual outage transitions independently of scenario stages and chart samples.
signal outage_changed(state: StringName, direction: NetworkEmulator.Direction)

## True while the session is settling a requested leave.
var disconnecting: bool = false
## Scoped game providing current connection and movement data.
var _game: Game
## Demo transport supplying loopback-only packet conditioning.
var _emulator: NetworkEmulator


## Bind this view model to the booted demo and its optional local relay.
func bind_game(game: Game, emulator: NetworkEmulator) -> void:
	_game = game
	_emulator = emulator
	if _emulator != null:
		_emulator.scenario_changed.connect(scenario_changed.emit)
		_emulator.spike_changed.connect(spike_changed.emit)
		_emulator.outage_changed.connect(outage_changed.emit)


## Read one typed snapshot for this process, including movement when available.
func diagnostics() -> GameNetworkDiagnostics:
	return _game.network_diagnostics()


## Return the relay counters and active preset for the local guest.
func conditions() -> NetworkEmulator.Stats:
	return _emulator.get_stats() if _emulator != null else null


## Apply the edited conditions to this demo's local loopback route.
func apply_profile(profile: NetworkEmulator.Profile) -> void:
	if _emulator == null:
		return
	_emulator.set_conditions(profile.delay_ms, profile.jitter_ms, profile.loss_percent,
		profile.uplink_kbps, profile.downlink_kbps)


## Add one-way latency to a chosen direction for a bounded time.
func start_spike(extra_ms: int, duration_ms: int, direction: NetworkEmulator.Direction, start_delay_ms: int) -> bool:
	return _emulator != null and _emulator.spike_latency(extra_ms, duration_ms, direction, start_delay_ms)


## Stop adding latency to newly received datagrams.
func cancel_spike() -> void:
	if _emulator != null:
		_emulator.cancel_spike()


## Start a copied sequence on the relay.
func start_scenario(steps: Array[NetworkEmulator.ScenarioStep], loop: bool, start_delay_ms: int) -> bool:
	return _emulator != null and _emulator.start_scenario(steps, loop, start_delay_ms)


## End the scenario and restore the previously applied profile.
func stop_scenario() -> void:
	if _emulator != null:
		_emulator.stop_scenario()


## Start a timed outage on the active relay after the requested countdown.
func start_outage(duration_ms: int, direction: NetworkEmulator.Direction, start_delay_ms: int) -> bool:
	return _emulator != null and _emulator.pause_link(duration_ms, direction, start_delay_ms)


## Stop forwarding packets so the real connection timeout can run.
func drop_link() -> void:
	if _emulator != null:
		_emulator.drop_link()


## Cancel an armed or active outage while keeping the configured profile.
func resume_link() -> void:
	if _emulator != null:
		_emulator.resume_link()


## Clear the relay's traffic totals while leaving queued datagrams intact.
func reset_counters() -> void:
	if _emulator != null:
		_emulator.reset_counters()


## Leave through the normal session lifecycle and settle when the world retires.
func disconnect_game() -> void:
	if disconnecting:
		return
	disconnecting = true
	var result: GameResult = await SimulatorSession.leave(_game, _emulator)
	disconnecting = false
	disconnect_finished.emit(result.ok, result.message)
