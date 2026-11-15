# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const GDSCRIPT_PICKUP: Script = preload("res://tests/visual_scripting/gameplay_pickup.gd")
const CRIMSON_PICKUP: String = "res://tests/visual_scripting/gameplay_pickup.crimsonscript"
const ORCHESTRATOR_PICKUP: String = "res://tests/visual_scripting/gameplay_pickup.torch"
const HEALTH: StringName = &"health"
const PICKUP_HANDLER: StringName = &"_on_body_entered"
const EVENTS_PER_TRIAL: int = 8_192
const WARMUP_EVENTS: int = 256
const TRIALS: int = 5
const DRIVER_ONLY: int = 2
const EXPECTED_CHECKSUM: int = 673_792
const START_HEALTH: Array[int] = [25, 88, 55, 74]
const UPDATED_HEALTH: Array[int] = [50, 100, 80, 99]


## Start after the tree can own pickups and test bodies.
func _initialize() -> void:
	_run.call_deferred()


## Attach a pickup script; connect the scene's signal when the script requires that setup.
func _make_pickup(pickup_script: Script, connect_handler: bool) -> Area3D:
	var pickup := Area3D.new()
	pickup.monitoring = false
	pickup.monitorable = false
	if pickup_script != null:
		pickup.set_script(pickup_script)
	root.add_child(pickup)
	if connect_handler and pickup.body_entered.connect(Callable(pickup, PICKUP_HANDLER)) != OK:
		pickup.free()
		return null
	return pickup


## Reset the players, time only synchronous signal delivery and its handler, then check every result.
func _measure(entry_signal: Signal, bodies: Array[CharacterBody3D], count: int, driver_only: bool) -> int:
	for body_index: int in range(count):
		bodies[body_index].set_meta(HEALTH, START_HEALTH[body_index % START_HEALTH.size()])
	var started_usec: int = Time.get_ticks_usec()
	for body_index: int in range(count):
		entry_signal.emit(bodies[body_index])
	var elapsed_usec: int = Time.get_ticks_usec() - started_usec
	var expected: Array[int] = START_HEALTH if driver_only else UPDATED_HEALTH
	var checksum: int = 0
	for body_index: int in range(count):
		var health_value: Variant = bodies[body_index].get_meta(HEALTH, -1)
		if typeof(health_value) != TYPE_INT or health_value != expected[body_index % expected.size()]:
			push_error("Pickup state mismatch for body %d: expected %s, got %s." %
					[body_index, expected[body_index % expected.size()], health_value])
			return -1
		checksum += int(health_value)
	if count == EVENTS_PER_TRIAL and not driver_only and checksum != EXPECTED_CHECKSUM:
		push_error("Unexpected pickup checksum: %d." % checksum)
		return -1
	return elapsed_usec


## Benchmark the same area signal in one headless process with setup outside the timer.
func _run() -> void:
	var flags: PackedStringArray = OS.get_cmdline_user_args()
	var use_orchestrator: bool = flags.has("--orchestrator")
	var graph_path: String = ORCHESTRATOR_PICKUP if use_orchestrator else CRIMSON_PICKUP
	var graph_script := ResourceLoader.load(graph_path, "Script") as Script
	if graph_script == null:
		push_error("Could not load pickup graph: %s." % graph_path)
		quit(1)
		return
	var pickups: Array[Area3D] = [_make_pickup(GDSCRIPT_PICKUP, true), _make_pickup(graph_script, use_orchestrator),
			_make_pickup(null, false)]
	var entries: Array[Signal] = []
	for pickup_index: int in range(pickups.size()):
		var pickup: Area3D = pickups[pickup_index]
		var expected_connections: int = 0 if pickup_index == DRIVER_ONLY else 1
		if pickup == null or pickup.body_entered.get_connections().size() != expected_connections:
			push_error("Unexpected number of body_entered subscribers for pickup %d." % pickup_index)
			quit(1)
			return
		entries.append(pickup.body_entered)
	var bodies: Array[CharacterBody3D] = []
	var players := Node3D.new()
	root.add_child(players)
	for body_index: int in range(EVENTS_PER_TRIAL):
		var body := CharacterBody3D.new()
		players.add_child(body)
		bodies.append(body)

	for runner_index: int in range(entries.size()):
		if _measure(entries[runner_index], bodies, WARMUP_EVENTS, runner_index == DRIVER_ONLY) < 0:
			quit(1)
			return
	var samples: Array = [[], [], []]
	for trial_index: int in range(TRIALS):
		var timings: Array[int] = [0, 0, 0]
		# Alternate which graph runs first. The signal-only controller is a separate observation.
		for runner_offset: int in range(entries.size()):
			var runner_index: int = (trial_index + runner_offset) % 2 if runner_offset < DRIVER_ONLY else DRIVER_ONLY
			var elapsed_usec: int = _measure(entries[runner_index], bodies, EVENTS_PER_TRIAL, runner_index == DRIVER_ONLY)
			if elapsed_usec < 0:
				quit(1)
				return
			timings[runner_index] = elapsed_usec
			samples[runner_index].append(elapsed_usec)
		print("Trial %d: GDScript %.3f ms; graph %.3f ms; signal only %.3f ms" %
				[trial_index + 1, timings[0] / 1000.0, timings[1] / 1000.0, timings[DRIVER_ONLY] / 1000.0])

	for pickup: Area3D in pickups:
		pickup.free()
	players.free()
	entries.clear()
	graph_script = null
	await process_frame
	print("GAMEPLAY_GRAPH_BENCHMARK_PASSED ", JSON.stringify({
		"backend": "orchestrator" if use_orchestrator else "crimson",
		"events_per_trial": EVENTS_PER_TRIAL,
		"warmup_events": WARMUP_EVENTS,
		"checksum": EXPECTED_CHECKSUM,
		"gdscript_usec": samples[0],
		"graph_usec": samples[1],
		"signal_only_usec": samples[DRIVER_ONLY],
	}))
	# The Orchestrator launcher collects the results before its DLL's reproduced unload fault.
	if not use_orchestrator or flags.has("--natural-exit"):
		quit()
