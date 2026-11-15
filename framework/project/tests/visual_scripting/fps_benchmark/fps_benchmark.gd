# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const SUITE: String = "res://tests/visual_scripting/fps_benchmark/"
const HANDLER: StringName = &"_on_body_entered"
const DRIVER_ONLY: int = 2
const DEFAULT_EVENTS: int = 2048
const DEFAULT_TRIALS: int = 5
const MAX_WARMUP: int = 128


## Defer benchmark setup until the headless scene tree can receive its fixture nodes.
func _initialize() -> void:
	_run.call_deferred()


## Read an optional integer argument sent by the benchmark launcher.
func _int_arg(flags: PackedStringArray, prefix: String, fallback: int) -> int:
	for flag: String in flags:
		if flag.begins_with(prefix):
			var raw_value: String = flag.trim_prefix(prefix)
			return int(raw_value) if raw_value.is_valid_int() else 0
	return fallback


## Parse the independently specified state vectors shared by all three runtimes.
func _manifest() -> Dictionary:
	var manifest_path: String = SUITE + "scenarios.json"
	var decoded: Variant = JSON.parse_string(FileAccess.get_file_as_string(manifest_path))
	if decoded is Dictionary and decoded.get("version") == 1 and decoded.get("scenarios") is Array:
		return decoded
	push_error("Could not load version 1 of the FPS benchmark manifest at %s." % manifest_path)
	return {}


## Use a real Area3D signal for every backend; physics and scene setup happen outside timing.
func _make_area(owner_group: Node, receiver: Script, wire_handler: bool) -> Area3D:
	var area := Area3D.new()
	area.monitoring = false
	area.monitorable = false
	if receiver:
		area.set_script(receiver)
	owner_group.add_child(area)
	if wire_handler and area.body_entered.connect(Callable(area, HANDLER)) != OK:
		area.free()
		return null
	var expected_connections: int = int(receiver != null)
	if area.body_entered.get_connections().size() != expected_connections:
		area.free()
		return null
	return area


## Preserve integer metadata after JSON parses all numbers as floats.
func _meta_value(value: Variant) -> Variant:
	if value is float and value == floorf(value):
		return int(value)
	return value


## Cache metadata and group keys before timing, including keys introduced by a graph.
func _managed_keys(cases: Array, field: String) -> Array[StringName]:
	var keys: Dictionary = {}
	for test_case: Dictionary in cases:
		for state: Dictionary in [test_case["start"], test_case["expect"]]:
			for key: String in state.get(field, [] if field == "groups" else {}):
				keys[StringName(key)] = true
	var key_names: Array[StringName] = []
	key_names.assign(keys.keys())
	key_names.sort()
	return key_names


## Resolve exact initial or expected body state, carrying forward unspecified start fields.
func _state(initial: Dictionary, updates: Dictionary) -> Dictionary:
	var resolved_meta: Dictionary = initial.get("meta", {}).duplicate()
	resolved_meta.merge(updates.get("meta", {}), true)
	for key: String in resolved_meta:
		resolved_meta[key] = _meta_value(resolved_meta[key])
	var position_vector: Array = updates.get("position", initial.get("position", [0.0, 0.0, 0.0]))
	var groups: Array = updates.get("groups", initial.get("groups", []))
	return {
		"meta": resolved_meta,
		"position": Vector3(position_vector[0], position_vector[1], position_vector[2]),
		"visible": bool(updates.get("visible", initial.get("visible", true))),
		"groups": groups,
	}


## Restore the same body state before every timed pass.
func _reset(body: CharacterBody3D, state: Dictionary, meta_keys: Array[StringName], group_keys: Array[StringName]) -> void:
	var metadata: Dictionary = state["meta"]
	for key: StringName in meta_keys:
		var wanted: Variant = metadata.get(String(key))
		if wanted != null:
			body.set_meta(key, wanted)
		elif body.has_meta(key):
			body.remove_meta(key)
	var groups: Array = state["groups"]
	for group: StringName in group_keys:
		var contains_group: bool = body.is_in_group(group)
		if groups.has(String(group)) and not contains_group:
			body.add_to_group(group)
		elif contains_group and not groups.has(String(group)):
			body.remove_from_group(group)
	body.position = state["position"]
	body.visible = state["visible"]


## Check reflected gameplay state and exact metadata types against the manifest.
func _mismatch(body: CharacterBody3D, state: Dictionary, meta_keys: Array[StringName], group_keys: Array[StringName]) -> String:
	var metadata: Dictionary = state["meta"]
	for key: StringName in meta_keys:
		var wanted: Variant = metadata.get(String(key))
		var actual: Variant = body.get_meta(key) if body.has_meta(key) else null
		if typeof(wanted) != typeof(actual) or wanted != actual:
			return "metadata %s expected %s (%s), got %s (%s)" % [
				key, wanted, type_string(typeof(wanted)), actual, type_string(typeof(actual))]
	var groups: Array = state["groups"]
	for group: StringName in group_keys:
		if body.is_in_group(group) != groups.has(String(group)):
			return "group membership for %s" % group
	if body.position != state["position"]:
		return "position expected %s, got %s" % [state["position"], body.position]
	if body.visible != state["visible"]:
		return "visibility expected %s, got %s" % [state["visible"], body.visible]
	for unexpected_key: StringName in body.get_meta_list():
		if not meta_keys.has(unexpected_key):
			return "unexpected metadata %s" % unexpected_key
	for unexpected_group: StringName in body.get_groups():
		if not group_keys.has(unexpected_group):
			return "unexpected group %s" % unexpected_group
	return ""


## Time only signal delivery and the triggered script; verify every body after the clock stops.
func _measure(entry: Signal, bodies: Array[CharacterBody3D], count: int, starts: Array[Dictionary],
		outcomes: Array[Dictionary], meta_keys: Array[StringName], group_keys: Array[StringName]) -> Array:
	var case_count: int = starts.size()
	for body_index: int in range(count):
		_reset(bodies[body_index], starts[body_index % case_count], meta_keys, group_keys)
	var started_usec: int = Time.get_ticks_usec()
	for body_index: int in range(count):
		entry.emit(bodies[body_index])
	var elapsed_usec: int = Time.get_ticks_usec() - started_usec
	for body_index: int in range(count):
		var mismatch: String = _mismatch(bodies[body_index], outcomes[body_index % case_count], meta_keys, group_keys)
		if not mismatch.is_empty():
			return [-1, "body %d, case %d: %s" % [body_index, body_index % case_count, mismatch]]
	return [elapsed_usec, ""]


## Run alternating GDScript and graph samples with the unsubscribed signal measured separately.
func _sample_scenario(scenario: Dictionary, use_orchestrator: bool, bodies: Array[CharacterBody3D],
		events: int, warmup: int, trials: int) -> Dictionary:
	var scenario_id: String = scenario["id"]
	var script_base: String = SUITE + "scenarios/" + scenario_id
	var reference: Script = ResourceLoader.load(script_base + ".gd", "Script") as Script
	var graph_suffix: String = ".torch" if use_orchestrator else ".crimsonscript"
	var graph: Script = ResourceLoader.load(script_base + graph_suffix, "Script") as Script
	if reference == null or graph == null:
		push_error("Could not load the scripts for %s." % scenario_id)
		return {}

	var owner_group := Node.new()
	root.add_child(owner_group)
	var areas: Array[Area3D] = [_make_area(owner_group, reference, true),
		_make_area(owner_group, graph, use_orchestrator), _make_area(owner_group, null, false)]
	for lane: int in range(areas.size()):
		if areas[lane] == null:
			push_error("%s lane %d has the wrong signal connection count." % [scenario_id, lane])
			owner_group.free()
			return {}
	var cases: Array = scenario["cases"]
	if cases.is_empty() or cases.size() > warmup:
		push_error("%s needs at least one manifest case and warmup must include every case." % scenario_id)
		owner_group.free()
		return {}
	var starts: Array[Dictionary] = []
	var outcomes: Array[Dictionary] = []
	for test_case: Dictionary in cases:
		starts.append(_state(test_case["start"], {}))
		outcomes.append(_state(test_case["start"], test_case["expect"]))
	var meta_keys: Array[StringName] = _managed_keys(cases, "meta")
	var group_keys: Array[StringName] = _managed_keys(cases, "groups")
	var lanes: Array[String] = ["GDScript", "Orchestrator" if use_orchestrator else "Crimson", "signal only"]
	var samples: Array = [[], [], []]
	for trial: int in range(-1, trials):
		var is_warmup: bool = trial == -1
		for lane_offset: int in range(areas.size()):
			var lane: int = (maxi(trial, 0) + lane_offset) % 2 if lane_offset < DRIVER_ONLY else DRIVER_ONLY
			var expected_states: Array[Dictionary] = starts if lane == DRIVER_ONLY else outcomes
			var sample: Array = _measure(areas[lane].body_entered, bodies, warmup if is_warmup else events,
				starts, expected_states, meta_keys, group_keys)
			if sample[0] < 0:
				push_error("%s, %s, trial %d: %s." % [scenario_id, lanes[lane], trial + 1, sample[1]])
				owner_group.free()
				return {}
			if not is_warmup:
				samples[lane].append(sample[0])
	owner_group.free()
	return {
		"id": scenario_id,
		"family": scenario["family"],
		"label": scenario["label"],
		"cases": cases.size(),
		"verified_states": (events * trials + warmup) * areas.size(),
		"gdscript_usec": samples[0],
		"graph_usec": samples[1],
		"signal_only_usec": samples[DRIVER_ONLY],
	}


## Run selected fixtures in manifest order and report verified raw samples to the launcher.
func _run() -> void:
	var flags: PackedStringArray = OS.get_cmdline_user_args()
	var use_orchestrator: bool = flags.has("--orchestrator")
	var events: int = _int_arg(flags, "--events=", DEFAULT_EVENTS)
	var trials: int = _int_arg(flags, "--trials=", DEFAULT_TRIALS)
	if events < 16 or trials < 1:
		push_error("The FPS benchmark needs at least 16 events and one trial.")
		quit(1)
		return
	var warmup: int = mini(events, MAX_WARMUP)
	var manifest: Dictionary = _manifest()
	if manifest.is_empty():
		quit(1)
		return
	var selected: PackedStringArray = []
	var scenarios: Array[Dictionary] = []
	for flag: String in flags:
		if flag.begins_with("--scenario="):
			selected.append(flag.trim_prefix("--scenario="))
	for scenario: Dictionary in manifest["scenarios"]:
		if selected.is_empty() or selected.has(scenario["id"]):
			scenarios.append(scenario)
	if scenarios.is_empty() or (not selected.is_empty() and scenarios.size() != selected.size()):
		push_error("The FPS benchmark selection has unknown or duplicate scenario IDs.")
		quit(1)
		return

	var results: Array[Dictionary] = []
	for scenario: Dictionary in scenarios:
		# A scenario starts with fresh bodies, so metadata and groups from prior cases cannot affect it.
		var body_container := Node3D.new()
		root.add_child(body_container)
		var bodies: Array[CharacterBody3D] = []
		for body_index: int in range(events):
			var body := CharacterBody3D.new()
			body_container.add_child(body)
			bodies.append(body)
		var result: Dictionary = _sample_scenario(scenario, use_orchestrator, bodies, events, warmup, trials)
		body_container.free()
		if result.is_empty():
			quit(1)
			return
		results.append(result)
		print("FPS_BENCHMARK_SCENARIO_PASSED ", result["id"])

	await process_frame
	print("FPS_GRAPH_BENCHMARK_PASSED ", JSON.stringify({
		"backend": "orchestrator" if use_orchestrator else "crimson",
		"events_per_trial": events,
		"warmup_events": warmup,
		"trials": trials,
		"scenarios": results,
	}))
	# Orchestrator remains running until the launcher collects the result and its diagnostic output.
	if not use_orchestrator or flags.has("--natural-exit"):
		quit()
