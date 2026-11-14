# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Node
## Owns a separate developer window and a scoped snapshot of this game process.

const MainMenu := preload("res://demo/ui/main_menu.gd")
const NetworkDebugModel := preload("res://demo/ui/network_debug_model.gd")
const NetworkEmulator := preload("res://demo/session/network_emulator.gd")
const Profiles := preload("res://demo/session/network_simulator_profiles.gd")
const MetricGraph := preload("res://demo/ui/network_latency_graph.gd")
const NetworkScenarioPanel := preload("res://demo/ui/network_scenario_panel.gd")
const REFRESH_SECONDS: float = 0.2
const EVENT_LIMIT: int = 96

@onready var _window: Window = %Tool
@onready var _gate: Control = %InputGate
@onready var _focus_game_button: Button = %FocusGame
@onready var _scenarios: NetworkScenarioPanel = %Scenarios
@onready var _profile: OptionButton = %Presets
@onready var _delay: SpinBox = %Delay
@onready var _jitter: SpinBox = %Jitter
@onready var _loss: SpinBox = %Loss
@onready var _uplink: SpinBox = %Uplink
@onready var _downlink: SpinBox = %Downlink
@onready var _profile_name: LineEdit = %ProfileName
@onready var _save_profile_button: Button = %SaveProfile
@onready var _apply: Button = %Apply
@onready var _spike_direction: OptionButton = %SpikeDirection
@onready var _spike_delay: SpinBox = %SpikeExtra
@onready var _spike_duration: SpinBox = %SpikeDuration
@onready var _spike_countdown: SpinBox = %SpikeCountdown
@onready var _start_spike: Button = %StartSpike
@onready var _cancel_spike: Button = %CancelSpike
@onready var _direction: OptionButton = %Direction
@onready var _duration: SpinBox = %Duration
@onready var _countdown: SpinBox = %Countdown
@onready var _start_outage: Button = %StartOutage
@onready var _drop: Button = %DropLink
@onready var _resume: Button = %ResumeLink
@onready var _disconnect: Button = %Disconnect
@onready var _reconnect: Button = %Reconnect
@onready var _reset_counters: Button = %ResetCounters
@onready var _pause: Button = %Pause
@onready var _feedback: Label = %Feedback
@onready var _event_log: TextEdit = %EventLog
@onready var _graphs: Array[MetricGraph] = [%RTTGraph, %AgeGraph, %PendingGraph, %CorrectionGraph]
@onready var _graph_labels: Array[Label] = [%RTTValue, %AgeValue, %PendingValue, %CorrectionValue]
@onready var _up_cells: Array[Label] = [%UpSent, %UpDrop, %UpQueue, %UpBytes, %UpLimit]
@onready var _down_cells: Array[Label] = [%DownSent, %DownDrop, %DownQueue, %DownBytes, %DownLimit]

## Live diagnostics and commands for the current demo process.
var _model := NetworkDebugModel.new()
## The main menu owns the direct endpoint and ordinary reconnect workflow.
var _menu: MainMenu
## The game input scope, independent of the tool window's viewport.
var _controls: Controls
## The game overlay must finish rebinding before the tool can take focus.
var _settings: Control
## The process's game window, used for focus and conditional pointer restoration.
var _game_window: Window
## Whether to recover the pointer capture that the tool displaced.
var _restore_pointer: bool = false
## Seconds remaining until the next live sample.
var _refresh_after: float = 0.0
## Whether the charts are frozen. Status and event collection continue.
var _charts_paused: bool = false
## Whether spin box changes are populating a preset.
var _filling_profile: bool = false
## Entry IDs are rebuilt on open, independently of unsaved field values.
var _profiles: Array[Profiles.Profile] = []
## Count separates built-in names from persisted custom names.
var _builtin_count: int = 0
## The role and peer that own the current charts.
var _last_role: int = -1
var _last_peer: int = -1
## Counter used to report new corrections in the event log.
var _last_corrections: int = 0
## Baseline for estimating forwarded UDP payload rate between monitor samples.
var _last_traffic_sample_ms: int = 0
var _last_uplink_bytes: int = 0
var _last_downlink_bytes: int = 0
var _uplink_rate: float = 0.0
var _downlink_rate: float = 0.0
## Monotonic start time and bounded event history for clipboard export.
var _opened_at_ms: int = 0
var _events := PackedStringArray()


func _ready() -> void:
	set_process(false)
	# Keep dropdown popups attached to their tool viewport.
	_window.gui_embed_subwindows = true
	_window.close_requested.connect(_close)
	_window.focus_entered.connect(_on_tool_focus)
	_window.focus_exited.connect(_on_tool_focus_exited)
	%Content.dismissed.connect(_close)
	_focus_game_button.pressed.connect(_focus_game)
	_reload_profiles()
	_profile.select(0)
	_profile.item_selected.connect(_fill_profile)
	for field: SpinBox in [_delay, _jitter, _loss, _uplink, _downlink]:
		field.value_changed.connect(_set_custom)
	for picker: OptionButton in [_direction, _spike_direction]:
		picker.add_item(tr("Both directions"), NetworkEmulator.Direction.BOTH)
		picker.add_item(tr("Uplink"), NetworkEmulator.Direction.UPLINK)
		picker.add_item(tr("Downlink"), NetworkEmulator.Direction.DOWNLINK)
		picker.select(0)
	_profile_name.text_changed.connect(_update_save_profile)
	_save_profile_button.pressed.connect(_save_profile)
	_apply.pressed.connect(_apply_profile)
	_start_spike.pressed.connect(_arm_spike)
	_cancel_spike.pressed.connect(_end_spike)
	_start_outage.pressed.connect(_arm_outage)
	_drop.pressed.connect(_drop_link)
	_resume.pressed.connect(_resume_link)
	_disconnect.pressed.connect(_disconnect_game)
	_reconnect.pressed.connect(_reconnect_game)
	_reset_counters.pressed.connect(_reset_traffic)
	_pause.pressed.connect(_toggle_charts)
	%ClearCharts.pressed.connect(_clear_charts)
	%CopyEvents.pressed.connect(_copy_events)
	%ClearEvents.pressed.connect(_clear_events)
	_model.disconnect_finished.connect(_on_disconnected)
	_model.spike_changed.connect(_on_spike_changed)
	_model.scenario_changed.connect(_on_scenario_changed)
	_model.outage_changed.connect(_on_outage_changed)
	_scenarios.setup(_model)
	_scenarios.reported.connect(_report)
	_update_save_profile()


## Attach the tool to the persistent game and its existing menu.
func setup(game: Game, emulator: NetworkEmulator, menu: MainMenu, settings: Control) -> void:
	_model.bind_game(game, emulator)
	_menu = menu
	_settings = settings
	_game_window = get_window()
	_game_window.focus_entered.connect(_on_game_focus)
	_settings.visibility_changed.connect(_on_settings_changed)
	game.phase_changed.connect(_on_phase_changed)
	game.session_ended.connect(_on_session_ended)
	_menu.launch_finished.connect(_on_launch_finished)


## Connect or retire local gameplay input when the session replaces its actor.
func set_controls(controls: Controls) -> void:
	if _controls == controls:
		return
	if _controls != null:
		_controls.stop_suspending_while_visible(_gate)
	_controls = controls
	_restore_pointer = false
	if _controls != null:
		_restore_pointer = _gate.visible and _controls.pointer_captured()
		var error: Error = _controls.suspend_while_visible(_gate)
		if error != OK:
			push_error("[Crimson] Could not suspend gameplay input for Network Debugger: %s" % error_string(error))


func _shortcut_input(event: InputEvent) -> void:
	if _menu == null or _settings.visible or not event.is_pressed() or event.is_echo():
		return
	var key: InputEventKey = event as InputEventKey
	var button: InputEventJoypadButton = event as InputEventJoypadButton
	if (key != null and (key.keycode == KEY_F8 or key.physical_keycode == KEY_F8)) \
			or (button != null and button.button_index == JOY_BUTTON_BACK):
		get_viewport().set_input_as_handled()
		if _window.visible and _gate.visible:
			_close()
		else:
			_open()


## Start sampling, keep the tool scoped to this game window, and move input into it.
func _open() -> void:
	if _opened_at_ms == 0:
		_opened_at_ms = Time.get_ticks_msec()
		_record(tr("Monitor opened"))
	_reload_profiles()
	_window.unfocusable = false
	_set_interacting(true)
	_window.show()
	_window.grab_focus()
	_focus_game_button.grab_focus()
	_refresh(false)
	_refresh_after = REFRESH_SECONDS
	set_process(true)


## Hide this tool and let the game regain its normal input focus.
func _close() -> void:
	_window.hide()
	set_process(false)
	_set_interacting(false)
	_restore_gameplay_pointer.call_deferred()


## Keep the monitor live while returning input to the game.
func _focus_game() -> void:
	_set_interacting(false)
	if _window.is_embedded():
		# Releasing an embedded child's focus requires removing it from the viewport first.
		_window.hide()
		_window.unfocusable = true
		_window.show()
	else:
		_game_window.grab_focus()
	_restore_gameplay_pointer.call_deferred()


## Suspend gameplay only while the tool owns interaction.
func _set_interacting(active: bool) -> void:
	if _gate.visible == active:
		return
	if active and _controls != null:
		_restore_pointer = _restore_pointer or _controls.pointer_captured()
	_gate.visible = active
	_menu.set_debugger_modal(active)


## When the tool gains focus again, release any gameplay input and pointer lease.
func _on_tool_focus() -> void:
	if _window.visible and not _settings.visible:
		_set_interacting(true)


## Popups may gain focus inside the tool. Only the game can resume interaction.
func _on_tool_focus_exited() -> void:
	if _game_window.has_focus():
		_on_game_focus()


## Clicking the game window is equivalent to the Focus game button.
func _on_game_focus() -> void:
	_set_interacting(false)
	_restore_gameplay_pointer.call_deferred()


## Restore a prior capture only if the correct game window actually owns focus.
func _restore_gameplay_pointer() -> void:
	if not _restore_pointer or _gate.visible or _settings.visible or not _game_window.has_focus():
		return
	if _controls != null and _controls.capture_pointer() == OK:
		_restore_pointer = false


## Settings owns its own modal interaction and can interrupt an open tool.
func _on_settings_changed() -> void:
	if _settings.visible and _window.visible:
		_close()


## Keep session transitions in the event history even while a later tab is visible.
func _on_phase_changed(phase: int) -> void:
	if _opened_at_ms != 0:
		_record(tr("Phase: %s") % _phase_name(phase))
	_refresh_if_visible()


## Preserve the exact lifecycle reason for a developer to copy after reconnect.
func _on_session_ended(reason: StringName) -> void:
	if _opened_at_ms != 0:
		_record(tr("Session ended: %s") % reason)
		if reason == &"server_disconnected":
			_feedback.text = tr("The server connection timed out or closed.")
	_refresh_if_visible()


## Report the same result that the ordinary direct join presents.
func _on_launch_finished(ok: bool, message: String) -> void:
	if _opened_at_ms != 0:
		_report(tr("Session ready.") if ok else message)
	_refresh_if_visible()


## Rebuild the dropdown without replacing a draft or adopting another window's saved edits.
func _reload_profiles(saved_name: String = "") -> void:
	var selected_id: int = _profile.get_selected_id()
	var previous: Profiles.Profile = _profiles[selected_id] if selected_id >= 0 and selected_id < _profiles.size() else null
	_profiles = Profiles.presets()
	_builtin_count = _profiles.size()
	_profiles.append_array(Profiles.saved_profiles())
	_profile.clear()
	for index: int in range(_profiles.size()):
		var profile: Profiles.Profile = _profiles[index]
		var label: String = tr("Saved · %s") % profile.label if index >= _builtin_count else profile.label
		_profile.add_item(label, index)
	_profile.add_item(tr("Custom"), _profiles.size())
	var selected: int = _profiles.size()
	for index: int in range(_profiles.size()):
		var profile: Profiles.Profile = _profiles[index]
		if (not saved_name.is_empty() and index >= _builtin_count and profile.label == saved_name) \
				or (saved_name.is_empty() and previous != null and profile.label == previous.label \
					and (selected_id >= _builtin_count) == (index >= _builtin_count) and profile.matches(previous)):
			selected = index
			break
	_profile.select(selected)
	_scenarios.update_profiles(_profiles, _builtin_count)


## Fill a draft from a named profile. The relay only changes when Apply is pressed.
func _fill_profile(index: int) -> void:
	var id: int = _profile.get_item_id(index)
	if id < 0 or id >= _profiles.size():
		return
	var profile: Profiles.Profile = _profiles[id]
	_filling_profile = true
	_delay.value = profile.delay_ms
	_jitter.value = profile.jitter_ms
	_loss.value = profile.loss_percent
	_uplink.value = profile.uplink_kbps
	_downlink.value = profile.downlink_kbps
	_filling_profile = false
	_profile_name.text = profile.label if id >= _builtin_count else ""
	_update_save_profile()


## Edits to the draft switch the preset selector to Custom.
func _set_custom(_value: float) -> void:
	if not _filling_profile:
		_profile.select(_profile.get_item_index(_profiles.size()))


## Save requires a name. Draft editing is available before joining a game.
func _update_save_profile(_text: String = "") -> void:
	_save_profile_button.disabled = _profile_name.text.strip_edges().is_empty()


## Commit typed SpinBox text before applying or saving a profile.
func _read_profile(label: String) -> Profiles.Profile:
	for field: SpinBox in [_delay, _jitter, _loss, _uplink, _downlink]:
		field.apply()
	return Profiles.Profile.new(label, int(_delay.value), int(_jitter.value), _loss.value,
		int(_uplink.value), int(_downlink.value))


## Store a named custom profile without changing the active relay.
func _save_profile() -> void:
	var profile_label: String = _profile_name.text.strip_edges()
	var error: Error = Profiles.save_profile(_read_profile(profile_label))
	if error == OK:
		_profile_name.text = profile_label
		_reload_profiles(profile_label)
		_report(tr("Saved profile: %s") % profile_label)
	else:
		_report(tr("Could not save the profile: %s") % error_string(error))


## Commit the pending profile to the active loopback guest.
func _apply_profile() -> void:
	var draft: Profiles.Profile = _read_profile("")
	draft.label = _profile.get_item_text(_profile.selected)
	_model.apply_profile(draft)
	_report(tr("Applied %s: +%d ms RTT, ±%d ms each way, %.1f%% loss, up %s, down %s.") % [
		draft.label, draft.delay_ms * 2, draft.jitter_ms, draft.loss_percent,
		_rate(draft.uplink_kbps), _rate(draft.downlink_kbps)])
	_refresh(false)


## Add temporary delay to the selected route and leave time to refocus gameplay.
func _arm_spike() -> void:
	for field: SpinBox in [_spike_delay, _spike_duration, _spike_countdown]:
		field.apply()
	var direction: NetworkEmulator.Direction = _spike_direction.get_selected_id() as NetworkEmulator.Direction
	if not _model.start_spike(int(_spike_delay.value), int(_spike_duration.value), direction, int(_spike_countdown.value)):
		_report(tr("No active relay could start the lag spike."))
	_refresh(false)


## End the spike for newly arriving packets. Existing queue deadlines are retained.
func _end_spike() -> void:
	_model.cancel_spike()
	_refresh(false)


## Keep short spike transitions visible in Events even between chart samples.
func _on_spike_changed(state: StringName, direction: NetworkEmulator.Direction, extra_delay_ms: int) -> void:
	match state:
		&"armed": _report(tr("Lag spike armed: %s, +%d ms one-way.") % [_direction_name(direction), extra_delay_ms])
		&"active": _report(tr("Lag spike started: %s, +%d ms one-way.") % [_direction_name(direction), extra_delay_ms])
		&"completed": _report(tr("Lag spike ended. Queued datagrams keep their delivery times."))
		&"cancelled": _report(tr("Lag spike canceled."))


## Keep scenario phase changes visible even when a short phase finishes between samples.
func _on_scenario_changed(step_index: int, label: String, reason: StringName) -> void:
	match reason:
		&"armed": _report(tr("Scenario armed. Focus the game during the countdown."))
		&"phase": _report(tr("Scenario stage %d: %s") % [step_index + 1, label])
		&"completed": _report(tr("Scenario completed. The applied profile was restored."))
		&"stopped": _report(tr("Scenario stopped. The applied profile was restored."))


## Record manual outages even when they start or end between monitor samples.
func _on_outage_changed(state: StringName, direction: NetworkEmulator.Direction) -> void:
	var direction_label: String = _direction_name(direction)
	match state:
		&"armed": _record(tr("Outage armed: %s") % direction_label)
		&"active": _record(tr("Outage started: %s") % direction_label)
		&"completed": _record(tr("Outage ended: %s") % direction_label)
		&"cancelled": _record(tr("Outage canceled: %s") % direction_label)


## Arm a repeatable outage and leave enough time to return to the game.
func _arm_outage() -> void:
	_duration.apply()
	_countdown.apply()
	var direction: NetworkEmulator.Direction = _direction.get_selected_id() as NetworkEmulator.Direction
	if _model.start_outage(int(_duration.value), direction, int(_countdown.value)):
		_feedback.text = tr("%s outage scheduled for %d ms with a %d ms start delay.") % [
			_direction_name(direction), int(_duration.value), int(_countdown.value)]
	else:
		_report(tr("No active relay could start the outage."))
	_refresh(false)


## Force a real timeout by dropping both directions until resumed.
func _drop_link() -> void:
	_model.drop_link()
	_feedback.text = tr("Both directions blocked until resumed or disconnected.")
	_refresh(false)


## Restore the applied profile without recreating the connection.
func _resume_link() -> void:
	_model.resume_link()
	_feedback.text = tr("Link resumed with the applied profile.")
	_refresh(false)


## Retire the active connection through its ordinary game action.
func _disconnect_game() -> void:
	_report(tr("Disconnecting…"))
	_model.disconnect_game()
	_refresh(false)


## Keep the tool open when the game returns to its join menu.
func _on_disconnected(ok: bool, message: String) -> void:
	_report(tr("Disconnected.") if ok else message)
	_refresh_if_visible()


## Repeat the same last successful direct join.
func _reconnect_game() -> void:
	_report(tr("Reconnecting to %s…") % _menu.last_join_address())
	_menu.reconnect()
	_refresh(false)


## Clear counts without disturbing the link's queued traffic.
func _reset_traffic() -> void:
	_model.reset_counters()
	_report(tr("Relay counters reset."))
	_refresh(false)


## Freeze chart samples only. Connection and event updates remain live.
func _toggle_charts() -> void:
	_charts_paused = not _charts_paused
	_pause.text = tr("Resume charts") if _charts_paused else tr("Pause charts")
	%History.text = tr("History paused") if _charts_paused else tr("History · last 12 s")


## Reset each trace on request or after a session role or peer change.
func _clear_charts() -> void:
	for graph: MetricGraph in _graphs:
		graph.clear()


## Store a time-stamped line and retain only the latest bounded history.
func _record(message: String) -> void:
	var elapsed: float = (Time.get_ticks_msec() - _opened_at_ms) * 0.001
	_events.append("%.3f  %s" % [elapsed, message])
	if _events.size() > EVENT_LIMIT:
		_events.remove_at(0)
	_render_events()


## Keep the end of the live event log visible.
func _render_events() -> void:
	_event_log.text = "\n".join(_events)
	_event_log.scroll_vertical = _event_log.get_line_count()


## Show action feedback across tabs and store it in the event history.
func _report(message: String) -> void:
	_feedback.text = message
	_record(message)


## Copy only the local process identity and the visible event history.
func _copy_events() -> void:
	DisplayServer.clipboard_set(_window.title + "\n" + "\n".join(_events))
	_feedback.text = tr("Copied %d events.") % _events.size()


## Clear only this tool's event history.
func _clear_events() -> void:
	_events.clear()
	_render_events()
	_feedback.text = tr("Events cleared.")


func _process(delta: float) -> void:
	_refresh_after -= delta
	if _refresh_after <= 0.0:
		_refresh_after = REFRESH_SECONDS
		_refresh(true)


## Update the visible controls immediately after a lifecycle transition.
func _refresh_if_visible() -> void:
	if _window.visible:
		_refresh(false)


## Read one detached snapshot and one relay snapshot for this display pass.
func _refresh(sample_graphs: bool) -> void:
	var stats: GameNetworkDiagnostics = _model.diagnostics()
	var relay: NetworkEmulator.Stats = _model.conditions()
	var predicted: bool = stats.movement_mode == GameNetworkDiagnostics.GUEST_PREDICTED
	var has_motion: bool = stats.movement_mode != GameNetworkDiagnostics.MOVEMENT_DISABLED
	var guest: bool = stats.role == GameNetworkDiagnostics.GUEST
	if _last_role != stats.role or _last_peer != stats.peer_id:
		_last_role = stats.role
		_last_peer = stats.peer_id
		_last_corrections = 0
		_last_traffic_sample_ms = 0
		_clear_charts()
		var identity: String = _role_name(stats.role)
		if stats.player_number > 0:
			identity = tr("Player %d (%s)") % [stats.player_number, identity]
		_window.title = tr("Network Debugger — %s") % identity
		_record(tr("Process role: %s") % identity)
	%Session.text = tr("%s  /  %s") % [_role_name(stats.role), _phase_name(stats.phase)]
	_refresh_monitor(stats, sample_graphs, has_motion, guest, predicted)
	_refresh_simulator(stats, relay, sample_graphs)
	_scenarios.refresh(relay)
	if predicted and stats.corrections > _last_corrections:
		_record(tr("Tick %d / ACK %d: %d new corrections. Sampled error %.3f m, replayed %d.") % [
			stats.simulation_tick, stats.acknowledged_input, stats.corrections - _last_corrections,
			stats.correction_distance, stats.replayed_inputs])
	_last_corrections = stats.corrections


## Render metrics with their own units and availability, never inferring a missing measurement.
func _refresh_monitor(stats: GameNetworkDiagnostics, sample: bool, has_motion: bool, guest: bool, predicted: bool) -> void:
	var rtt_name: String = tr("RTT")
	if stats.role in [GameNetworkDiagnostics.HOST, GameNetworkDiagnostics.DEDICATED]:
		rtt_name = tr("Worst guest RTT")
	var values: Array[float] = [stats.round_trip_ms, float(stats.state_age_ms),
		float(stats.pending_inputs) if guest and has_motion else -1.0,
		stats.correction_distance if predicted and stats.corrections > 0 else -1.0]
	var names: Array[String] = [rtt_name, tr("Snapshot age"), tr("Pending inputs"), tr("Reconcile error")]
	var units: Array[String] = [tr("%d ms"), tr("%d ms"), "%d", tr("%.3f m")]
	var scales: Array[String] = [tr("%d ms"), tr("%d ms"), "%d", tr("%.2f m")]
	for index: int in range(values.size()):
		var value: float = values[index]
		if sample and not _charts_paused:
			_graphs[index].add_sample(value)
		_graph_labels[index].text = "%s  %s  (≤%s)" % [
			names[index], units[index] % value if value >= 0.0 else "—",
			scales[index] % _graphs[index].ceiling]
	%Connection.text = tr("Transport  %s\nPeer ID     %s\nRemote peers  %d") % [
		String(stats.transport).to_upper() if not stats.transport.is_empty() else "—",
		str(stats.peer_id) if stats.peer_id > 0 else "—", stats.connected_peers]
	%Movement.text = _movement_name(stats.movement_mode)
	if has_motion:
		%Movement.text += tr("\nServer tick  %d") % stats.simulation_tick
	if guest and has_motion:
		%Movement.text += tr("\nInput ACK  %d") % stats.acknowledged_input
	if predicted:
		%Movement.text += tr("   /   Replayed  %d\nCorrections  %d") % [stats.replayed_inputs, stats.corrections]
	%Positions.visible = guest and has_motion
	if %Positions.visible:
		%Positions.text = tr("Server snapshot  %s") % _position(stats.authority_position)
		if predicted:
			%Positions.text += tr("\nLocal prediction  %s") % _position(stats.predicted_position)


## Render the actual applied link separately from the editable profile draft.
func _refresh_simulator(stats: GameNetworkDiagnostics, relay: NetworkEmulator.Stats, sample_traffic: bool) -> void:
	var active: bool = relay != null and relay.active
	var scenario_running: bool = active and relay.scenario_step_count > 0
	var spike: bool = active and relay.spike_remaining_ms > 0
	var outage: int = 0
	if active:
		if relay.outage_starts_in_ms > 0:
			outage = 1
			%Relay.text = tr("%s outage starts in %.1f s. Focus the game to continue moving.") % [
				_direction_name(relay.outage_direction), relay.outage_starts_in_ms * 0.001]
		elif relay.link_dropped:
			outage = 2
			%Relay.text = tr("%s blocked · %s") % [_direction_name(relay.outage_direction),
				tr("%.1f s remaining") % (relay.outage_remaining_ms * 0.001) if relay.outage_remaining_ms >= 0 else tr("until resumed")]
		elif spike and relay.spike_starts_in_ms > 0:
			%Relay.text = tr("Lag spike: %s +%d ms one-way in %.1f s.") % [
				_direction_name(relay.spike_direction), relay.spike_delay_ms, relay.spike_starts_in_ms * 0.001]
		elif spike:
			%Relay.text = tr("Lag spike: %s +%d ms one-way · %.1f s remaining") % [
				_direction_name(relay.spike_direction), relay.spike_delay_ms, relay.spike_remaining_ms * 0.001]
		else:
			%Relay.text = tr("Applied: +%d ms RTT  /  ±%d ms each way  /  %.1f%% loss\nUpload %s  /  Download %s") % [
				relay.one_way_delay_ms * 2, relay.jitter_ms, relay.loss_percent, _rate(relay.uplink_kbps), _rate(relay.downlink_kbps)]
		if scenario_running:
			%Relay.text = tr("Scenario running. See Scenarios to track or stop it.")
		if sample_traffic:
			_sample_traffic(relay)
		_render_traffic(_up_cells, relay.uplink, relay.uplink_kbps, _uplink_rate)
		_render_traffic(_down_cells, relay.downlink, relay.downlink_kbps, _downlink_rate)
	else:
		%Relay.text = tr("Join a loopback ENet server on this machine to simulate link conditions.")
		_render_traffic(_up_cells, null, 0, 0.0)
		_render_traffic(_down_cells, null, 0, 0.0)
	var can_start_effect: bool = active and not scenario_running and outage == 0 and not spike
	_apply.disabled = not active or scenario_running
	_direction.disabled = not can_start_effect
	_spike_direction.disabled = not can_start_effect
	for field: SpinBox in [_duration, _countdown]:
		field.editable = can_start_effect
	for field: SpinBox in [_spike_delay, _spike_duration, _spike_countdown]:
		field.editable = can_start_effect
	_start_spike.disabled = not can_start_effect
	_cancel_spike.disabled = not spike
	_start_outage.disabled = not can_start_effect
	_drop.disabled = not can_start_effect
	_resume.disabled = not active or scenario_running or outage == 0
	_reset_counters.disabled = not active
	_disconnect.disabled = stats.role != GameNetworkDiagnostics.GUEST or _model.disconnecting
	_reconnect.disabled = stats.phase != Game.IDLE or _menu.last_join_address().is_empty() or _model.disconnecting
	var focus: BaseButton = _window.gui_get_focus_owner() as BaseButton
	if _gate.visible and focus != null and focus.disabled:
		_focus_game_button.grab_focus()


## Show counters as datagrams and UDP payload units rather than guessed message sizes.
func _render_traffic(cells: Array[Label], traffic: NetworkEmulator.Traffic, bandwidth_kbps: int, rate: float) -> void:
	if traffic == null:
		for cell: Label in cells:
			cell.text = "—"
			cell.tooltip_text = ""
		return
	cells[0].text = str(traffic.forwarded)
	cells[1].text = str(traffic.dropped) if traffic.overflowed == 0 else "%d (%d)" % [traffic.dropped, traffic.overflowed]
	cells[1].tooltip_text = tr("%d drops exceeded the queue limit or scheduling horizon.") % traffic.overflowed
	cells[2].text = str(traffic.queued)
	cells[2].tooltip_text = tr("%.1f KiB of payload waiting for delivery.") % (traffic.queued_bytes / 1024.0)
	cells[3].text = tr("%.1f kbps") % rate
	cells[3].tooltip_text = tr("%.1f KiB total UDP payload forwarded.") % (traffic.forwarded_bytes / 1024.0)
	cells[4].text = _rate(bandwidth_kbps)


## Estimate the observed payload rate from the difference between successive relay counters.
func _sample_traffic(relay: NetworkEmulator.Stats) -> void:
	var now_ms: int = Time.get_ticks_msec()
	var elapsed_ms: int = now_ms - _last_traffic_sample_ms
	if _last_traffic_sample_ms != 0 and elapsed_ms > 0:
		_uplink_rate = maxi(0, relay.uplink.forwarded_bytes - _last_uplink_bytes) * 8.0 / elapsed_ms
		_downlink_rate = maxi(0, relay.downlink.forwarded_bytes - _last_downlink_bytes) * 8.0 / elapsed_ms
	else:
		_uplink_rate = 0.0
		_downlink_rate = 0.0
	_last_traffic_sample_ms = now_ms
	_last_uplink_bytes = relay.uplink.forwarded_bytes
	_last_downlink_bytes = relay.downlink.forwarded_bytes


## Display a cap in the same decimal units as the simulator fields.
func _rate(rate_kbps: int) -> String:
	return tr("Unlimited") if rate_kbps == 0 else tr("%d kbps") % rate_kbps


## Report both positions as independent samples in world metres.
func _position(value: Vector3) -> String:
	return "(%.2f, %.2f, %.2f) m" % [value.x, value.y, value.z]


## Name the currently selected packet direction.
func _direction_name(direction: NetworkEmulator.Direction) -> String:
	match direction:
		NetworkEmulator.Direction.UPLINK: return tr("Uplink")
		NetworkEmulator.Direction.DOWNLINK: return tr("Downlink")
	return tr("Both directions")


## Describe the local process's role using the same terms as the player HUD.
func _role_name(role: int) -> String:
	match role:
		GameNetworkDiagnostics.OFFLINE: return tr("Solo")
		GameNetworkDiagnostics.HOST: return tr("Server")
		GameNetworkDiagnostics.DEDICATED: return tr("Dedicated server")
		GameNetworkDiagnostics.CONNECTING: return tr("Connecting")
		GameNetworkDiagnostics.GUEST: return tr("Client")
	return tr("No session")


## Distinguish the game phase from the local transport role.
func _phase_name(phase: int) -> String:
	match phase:
		Game.LOBBY: return tr("Lobby")
		Game.TRANSITIONING: return tr("Loading")
		Game.RUNNING: return tr("Playing")
		Game.RECOVERING: return tr("Recovering")
	return tr("Idle")


## Describe the movement path that produced the current diagnostics.
func _movement_name(mode: int) -> String:
	match mode:
		GameNetworkDiagnostics.AUTHORITY_LIVE: return tr("Live server physics")
		GameNetworkDiagnostics.AUTHORITY_PREDICTIVE: return tr("Server simulation")
		GameNetworkDiagnostics.GUEST_INTERPOLATED: return tr("Server snapshots")
		GameNetworkDiagnostics.GUEST_PREDICTED: return tr("Local prediction")
	return tr("Inactive")
