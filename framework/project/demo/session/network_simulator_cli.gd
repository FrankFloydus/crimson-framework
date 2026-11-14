# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Run reproducible, machine-readable tests against the demo's local ENet guest relay.

const Emulator := preload("res://demo/session/network_emulator.gd")
const SimulatorSession := preload("res://demo/session/network_simulator_session.gd")
const Profiles := preload("res://demo/session/network_simulator_profiles.gd")
const Plan := preload("res://demo/session/network_simulator_plan.gd")
const Metrics := preload("res://demo/session/network_simulator_metrics.gd")

const PREFIX: String = "CRIMSON_SIMULATOR "
const CONNECT_BUDGET_MS: int = 12000
const MOVE_BINDINGS: Dictionary[String, StringName] = {
	"forward": &"move/key_up", "backward": &"move/key_down",
	"left": &"move/key_left", "right": &"move/key_right",
}
const DIRECTIONS: Dictionary[String, int] = {
	"both": Emulator.Direction.BOTH, "uplink": Emulator.Direction.UPLINK,
	"downlink": Emulator.Direction.DOWNLINK, "none": -1,
}

var _output: FileAccess
var _session := SimulatorSession.new()
var _owner: Node
var _controls: Controls
var _address: String = "127.0.0.1"
var _started_ms: int = 0
var _deadline_ms: int = 0
var _step: int = -1
var _operation: GameResult
var _game_error_code: String = ""
var _finishing: bool = false
var _write_failed: bool = false


func _initialize() -> void:
	_run.call_deferred()


## Validate the plan before joining the authority or changing profiles.
func _run() -> void:
	_started_ms = Time.get_ticks_msec()
	var args: Dictionary = _parse_arguments(OS.get_cmdline_user_args())
	if args.has("error"):
		await _finish(2, args.error)
		return
	if args.has("output"):
		if not String(args.output).is_absolute_path() or FileAccess.file_exists(args.output):
			await _finish(2, "Choose a new absolute path for the JSONL output.")
			return
		_output = FileAccess.open(args.output, FileAccess.WRITE)
		if _output == null:
			await _finish(2, "Could not open the JSONL output: %s" % error_string(FileAccess.get_open_error()))
			return
	if args.has("help"):
		print("Run with Godot --headless --script res://demo/session/network_simulator_cli.gd -- --plan=<file> [--port=24830] [--address=127.0.0.1] [--output=<jsonl>]. Use --list to see profiles, templates and actions.")
		await _finish(0)
		return
	if args.has("list"):
		_emit("catalog", _catalog())
		await _finish(0)
		return
	var plan: Plan = Plan.load_file(args.plan)
	if not plan.error.is_empty():
		await _finish(2, plan.error)
		return
	if DisplayServer.get_name() != "headless":
		await _finish(2, "Start Godot with --headless.")
		return
	Engine.max_fps = 120
	_deadline_ms = Time.get_ticks_msec() + plan.timeout_ms
	_address = args.get("address", _address)
	var port: int = int(args.get("port", SimulatorSession.DEFAULT_PORT))
	_owner = Node.new()
	_owner.name = SimulatorSession.ROOT_NAME
	root.add_child(_owner)
	_session.boot_lan(_owner, port, _game_configuration())
	var boot: GameResult = _session.game.boot_result()
	if not boot.ok:
		await _finish(1, boot.message)
		return
	_bind_events()
	_emit("run_started", {"address": _address, "port": port, "steps": plan.steps.size(), "timeout_ms": plan.timeout_ms})
	var error: String = await _connect()
	if not error.is_empty():
		await _finish(1, error)
		return
	for index: int in plan.steps.size():
		_step = index
		if _expired() or _write_failed:
			await _finish(1, "The run deadline expired or the JSONL output could not be written.")
			return
		var step: Dictionary = plan.steps[index]
		_emit("step_started", {"action": step.action})
		error = await _execute(step)
		if not error.is_empty():
			await _finish(1, error)
			return
	await _finish(0)


## Allow headless fixtures to compose the same simulator against their own world and input.
func _game_configuration() -> GameConfig:
	return null


## Accept only explicit local runner options. Ports are shared with the host process.
func _parse_arguments(arguments: PackedStringArray) -> Dictionary:
	var args: Dictionary = {}
	for argument: String in arguments:
		if argument in ["--help", "--list"]:
			args[argument.substr(2)] = true
			continue
		var pair: PackedStringArray = argument.split("=", true, 1)
		if pair.size() != 2 or pair[0] not in ["--plan", "--port", "--address", "--output"] or pair[1].is_empty():
			return {"error": "Unknown or incomplete argument: %s" % argument}
		var key: String = pair[0].substr(2)
		if args.has(key):
			return {"error": "Duplicate argument: %s" % pair[0]}
		args[key] = pair[1]
	if args.has("help") or args.has("list"):
		return args
	if not args.has("plan"):
		return {"error": "Supply --plan=<file>. Use --help for usage."}
	if not String(args.plan).is_absolute_path():
		return {"error": "Choose an absolute or res:// plan path."}
	if args.has("port"):
		var port: String = args.port
		if not port.is_valid_int() or port.to_int() < 1 or port.to_int() > 65535:
			return {"error": "Choose a UDP port from 1 to 65535."}
	if args.has("address") and String(args.address).to_lower() not in ["127.0.0.1", "localhost", "::1", "0:0:0:0:0:0:0:1"]:
		return {"error": "Choose a loopback ENet address for the demo simulator."}
	return args


func _catalog() -> Dictionary:
	var built_in: Array[Dictionary] = []
	var saved: Array[Dictionary] = []
	var templates: Array[Dictionary] = []
	var profiles: Array[Profiles.Profile] = Profiles.presets()
	for index: int in profiles.size():
		var entry: Dictionary = _profile_data(profiles[index])
		entry.id = String(Profiles.preset_id(index))
		built_in.append(entry)
	for profile: Profiles.Profile in Profiles.saved_profiles():
		saved.append(_profile_data(profile))
	var index: int = 0
	while Profiles.template_id(index) != &"":
		templates.append({"id": String(Profiles.template_id(index)), "name": Profiles.template_label(index)})
		index += 1
	return {"presets": built_in, "saved": saved, "templates": templates, "actions": Plan.ACTIONS}


func _profile_data(profile: Profiles.Profile) -> Dictionary:
	return {"name": profile.label, "delay_ms": profile.delay_ms, "jitter_ms": profile.jitter_ms,
		"loss_percent": profile.loss_percent, "uplink_kbps": profile.uplink_kbps, "downlink_kbps": profile.downlink_kbps}


func _bind_events() -> void:
	_session.game.phase_changed.connect(func(phase: int) -> void:
		_emit("event", {"event": "phase_changed", "phase": Metrics.phase_name(phase)}))
	_session.game.world_ready.connect(func(world: StringName, epoch: int) -> void:
		_emit("event", {"event": "world_ready", "world": String(world), "epoch": epoch}))
	_session.game.session_ended.connect(func(reason: StringName) -> void:
		_emit("event", {"event": "session_ended", "reason": String(reason)}))
	_session.relay.scenario_changed.connect(func(index: int, label: String, state: StringName) -> void:
		_emit("event", {"event": "scenario", "index": index, "label": label, "state": String(state)}))
	_session.relay.spike_changed.connect(func(state: StringName, direction: int, extra_ms: int) -> void:
		_emit("event", {"event": "spike", "state": String(state), "direction": Metrics.direction_name(direction),
			"extra_delay_ms": extra_ms}))
	_session.relay.outage_changed.connect(func(state: StringName, direction: int) -> void:
		_emit("event", {"event": "outage", "state": String(state), "direction": Metrics.direction_name(direction)}))


func _connect() -> String:
	if _session.game.get_phase() != Game.IDLE:
		return "Reconnect requires an idle game. Disconnect or wait for the connection to end."
	_operation = null
	_join()
	var finished: bool = await _await_operation(CONNECT_BUDGET_MS)
	if not finished:
		return "Timed out joining the loopback host."
	if not _operation.ok:
		_game_error_code = String(_operation.code)
		return "Could not join: %s" % _operation.message
	if not _session.relay.get_stats().active:
		return "The local ENet relay is inactive."
	_close_controls()
	_controls = SimulatorSession.boot_controls(_session.game, _owner)
	_emit("event", {"event": "connected", "peer_id": _session.game.network_diagnostics().peer_id})
	return ""


func _join() -> void:
	_operation = await _session.game.join(_address)


func _leave() -> void:
	_operation = await SimulatorSession.leave(_session.game, _session.relay)


func _await_operation(timeout_ms: int) -> bool:
	var deadline: int = mini(_deadline_ms, Time.get_ticks_msec() + timeout_ms)
	while _operation == null and Time.get_ticks_msec() < deadline:
		await process_frame
	return _operation != null


func _execute(step: Dictionary) -> String:
	var relay: Emulator = _session.relay
	var stats: Emulator.Stats = relay.get_stats()
	match step.action:
		"status": _snapshot(step.label)
		"list_profiles": _emit("catalog", _catalog())
		"save_profile":
			var error: Error = Profiles.save_profile(_custom_profile(step.name, step.settings))
			if error != OK:
				return "Could not save profile %s: %s" % [step.name, error_string(error)]
			_emit("event", {"event": "profile_saved", "name": step.name})
		"apply_profile":
			if not stats.active or stats.scenario_step_count > 0:
				return "Applying a profile requires a connected relay outside a scenario."
			var profile: Profiles.Profile = _select_profile(step)
			if profile == null:
				return "Saved profile not found: %s" % step.get("saved", "")
			relay.set_conditions(profile.delay_ms, profile.jitter_ms, profile.loss_percent, profile.uplink_kbps, profile.downlink_kbps)
			_emit("event", {"event": "profile_applied", "profile": _profile_data(profile)})
		"spike":
			if not relay.spike_latency(step.extra_delay_ms, step.duration_ms, DIRECTIONS[step.direction] as Emulator.Direction, step.start_delay_ms):
				return "Could not start a spike. Check that the relay is active and no fault or scenario is running."
		"cancel_spike": relay.cancel_spike()
		"outage":
			if stats.link_dropped or stats.outage_starts_in_ms > 0 \
					or not relay.pause_link(step.duration_ms, DIRECTIONS[step.direction] as Emulator.Direction, step.start_delay_ms):
				return "Could not start an outage. Check that the relay is active and no spike or scenario is running."
			_emit("event", {"event": "outage_scheduled", "direction": step.direction, "duration_ms": step.duration_ms,
				"start_delay_ms": step.start_delay_ms})
		"drop_link":
			if not stats.active or stats.link_dropped or stats.outage_starts_in_ms > 0 \
					or stats.scenario_step_count > 0 or stats.spike_delay_ms > 0:
				return "Dropping the link requires a connected relay without an active effect."
			relay.drop_link()
			_emit("event", {"event": "link_dropped"})
		"resume_link":
			if not stats.active or stats.scenario_step_count > 0:
				return "Resuming the link requires a connected relay outside a scenario."
			relay.resume_link()
			_emit("event", {"event": "link_resumed"})
		"disconnect":
			if _session.game.get_phase() != Game.RUNNING:
				return "Disconnect requires a running game."
			_operation = null
			_leave()
			var finished: bool = await _await_operation(CONNECT_BUDGET_MS)
			if not finished:
				return "Timed out leaving the host."
			if not _operation.ok:
				_game_error_code = String(_operation.code)
				return _operation.message
			_close_controls()
		"reconnect": return await _connect()
		"start_scenario": return _start_scenario(step)
		"stop_scenario": relay.stop_scenario()
		"reset_counters":
			if not stats.active:
				return "Resetting counters requires a connected relay."
			relay.reset_counters()
		"wait": return await _wait(step.duration_ms)
		"assert", "wait_until": return await _assert_step(step)
		"sample": return await _sample(step.duration_ms, step.interval_ms)
		"move": return await _move(step.direction, step.duration_ms)
		_: return "Unknown action: %s" % step.action
	return ""


func _select_profile(options: Dictionary) -> Profiles.Profile:
	if options.has("preset"):
		return Profiles.presets()[Profiles.preset_index(StringName(options.preset))]
	if options.has("saved"):
		for profile: Profiles.Profile in Profiles.saved_profiles():
			if profile.label.nocasecmp_to(options.saved) == 0:
				return profile
		return null
	return _custom_profile("Custom", options.settings)


func _custom_profile(label: String, settings: Dictionary) -> Profiles.Profile:
	return Profiles.Profile.new(label, settings.delay_ms, settings.jitter_ms, settings.loss_percent,
		settings.uplink_kbps, settings.downlink_kbps)


func _start_scenario(options: Dictionary) -> String:
	var stages: Array[Emulator.ScenarioStep] = []
	if options.has("template"):
		stages = Profiles.scenario_template(Profiles.template_index(StringName(options.template)) as Profiles.Template)
	else:
		for values: Dictionary in options.stages:
			var profile: Profiles.Profile = _select_profile(values)
			if profile == null:
				return "Scenario profile not found: %s" % values.get("saved", "")
			var stage: Emulator.ScenarioStep = profile.stage(values.duration_ms, DIRECTIONS[values.outage])
			stage.label = values.get("label", stage.label)
			stages.append(stage)
	if not _session.relay.start_scenario(stages, options.loop, options.start_delay_ms):
		return "Could not start the scenario. Check that the relay is active and no fault or scenario is running."
	return ""


func _assert_step(step: Dictionary) -> String:
	var deadline: int = mini(_deadline_ms, Time.get_ticks_msec() + step.get("timeout_ms", 0))
	var result: Dictionary = Metrics.matches(Metrics.snapshot(_session.game, _session.relay), step.field, step.op, step.value)
	while step.action == "wait_until" and not result.ok and not _write_failed and Time.get_ticks_msec() < deadline:
		await process_frame
		result = Metrics.matches(Metrics.snapshot(_session.game, _session.relay), step.field, step.op, step.value)
	_emit("assertion", {"field": step.field, "op": step.op, "expected": step.value,
		"actual": result.actual, "found": result.found, "ok": result.ok})
	if not result.ok:
		return "Assertion failed: %s %s %s" % [step.field, step.op, JSON.stringify(step.value)]
	return ""


func _sample(duration_ms: int, interval_ms: int) -> String:
	var ends: int = Time.get_ticks_msec() + duration_ms
	_snapshot("")
	while Time.get_ticks_msec() < ends:
		var delay: int = mini(interval_ms, ends - Time.get_ticks_msec())
		var error: String = await _wait(delay)
		if not error.is_empty():
			return error
		_snapshot("")
	return ""


func _wait(duration_ms: int) -> String:
	var ends: int = Time.get_ticks_msec() + duration_ms
	while Time.get_ticks_msec() < ends and not _expired() and not _write_failed:
		await process_frame
	return "The run deadline expired or the JSONL output could not be written." if _expired() or _write_failed else ""


func _move(direction: String, duration_ms: int) -> String:
	if _controls == null or _session.game.get_phase() != Game.RUNNING:
		return "Move requires a running game and authored local controls."
	for action: InputAction in _controls.get_layout().resolved_actions():
		for binding: InputBinding in action.bindings:
			if binding.id == MOVE_BINDINGS[direction] and binding.event is InputEventKey:
				var input := binding.event.duplicate() as InputEventKey
				input.pressed = true
				root.push_input(input, true)
				var error: String = await _wait(duration_ms)
				input.pressed = false
				root.push_input(input, true)
				return error
	return "The authored layout has no keyboard binding for %s." % direction


func _close_controls() -> void:
	if _controls != null:
		_controls.close()
		_controls = null


func _expired() -> bool:
	return Time.get_ticks_msec() >= _deadline_ms


func _snapshot(label: String) -> void:
	var data: Dictionary = Metrics.snapshot(_session.game, _session.relay)
	data.label = label
	_emit("snapshot", data)


func _emit(kind: String, data: Dictionary = {}) -> void:
	data.type = kind
	data.elapsed_ms = Time.get_ticks_msec() - _started_ms
	data.step = _step
	var line: String = JSON.stringify(data)
	print(PREFIX + line)
	if _output != null and not _write_failed:
		_write_failed = not _output.store_line(line)
		_output.flush()


func _finish(code: int, message: String = "") -> void:
	if _finishing:
		return
	_finishing = true
	_close_controls()
	if _session.relay != null:
		_session.relay.stop_scenario()
		_session.relay.cancel_spike()
	if _session.game != null and _session.game.get_phase() != Game.IDLE:
		_operation = null
		_deadline_ms = Time.get_ticks_msec() + 2000
		_leave()
		var cleaned: bool = await _await_operation(2000)
		if code == 0 and (not cleaned or not _operation.ok):
			code = 1
			message = "The guest could not leave cleanly."
	if _write_failed and code == 0:
		code = 1
		message = "Could not write the JSONL output."
	_emit("run_finished", {"ok": code == 0, "exit_code": code, "message": message, "game_code": _game_error_code})
	quit(1 if _write_failed and code == 0 else code)
