# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Validates and normalizes a headless network simulator plan before it runs.

const Emulator := preload("res://demo/session/network_emulator.gd")
const Profiles := preload("res://demo/session/network_simulator_profiles.gd")

const DEFAULT_TIMEOUT_MS: int = 120000
const MAX_TIMEOUT_MS: int = 600000
const MAX_PLAN_BYTES: int = 2 * 1024 * 1024
const MAX_STEPS: int = 1000
const DEFAULT_SAMPLE_INTERVAL_MS: int = 250
const MIN_SAMPLE_INTERVAL_MS: int = 20
const MAX_STAGE_LABEL_LENGTH: int = 80
const MAX_STATUS_LABEL_LENGTH: int = 120
const MAX_FIELD_LENGTH: int = 128

## Supported actions shown by the CLI command catalog.
const ACTIONS: Array[String] = [
	"status", "list_profiles", "apply_profile", "save_profile", "spike", "cancel_spike",
	"outage", "drop_link", "resume_link", "disconnect", "reconnect", "start_scenario", "stop_scenario",
	"reset_counters", "move", "wait", "assert", "wait_until", "sample",
]
const DIRECTIONS: Array[String] = ["both", "uplink", "downlink"]
const MOVE_DIRECTIONS: Array[String] = ["forward", "backward", "left", "right"]
const COMPARISONS: Array[String] = ["eq", "ne", "gt", "gte", "lt", "lte"]
const PROFILE_KEYS: Array[String] = ["preset", "saved", "settings"]
const SETTINGS_KEYS: Array[String] = ["delay_ms", "jitter_ms", "loss_percent", "uplink_kbps", "downlink_kbps"]

## A normalized copy of the supplied steps. Empty if validation fails.
var steps: Array[Dictionary] = []
## The maximum wall time allowed for the whole run.
var timeout_ms: int = DEFAULT_TIMEOUT_MS
## The first parse, I/O, or validation error, including its location in the plan.
var error: String = ""


## Read a bounded plan file without running any of its actions.
static func load_file(path: String):
	var plan = new()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		plan.error = "Could not open the plan file: %s." % error_string(FileAccess.get_open_error())
		return plan
	if file.get_length() > MAX_PLAN_BYTES:
		plan.error = "The plan file exceeds %d bytes." % MAX_PLAN_BYTES
		return plan
	return from_json(file.get_as_text())


## Parse a JSON document and return a plan with either normalized steps or an error.
static func from_json(source: String):
	var plan = new()
	if source.to_utf8_buffer().size() > MAX_PLAN_BYTES:
		plan.error = "The plan exceeds %d bytes." % MAX_PLAN_BYTES
		return plan
	var json := JSON.new()
	if json.parse(source) != OK:
		plan.error = "Invalid JSON on line %d: %s" % [json.get_error_line() + 1, json.get_error_message()]
	else:
		plan._read(json.data)
	return plan


## Reject unsupported root fields and commit the normalized steps only after every check passes.
func _read(value: Variant) -> void:
	if not value is Dictionary:
		_reject("plan", "expected an object")
		return
	if not _known_keys(value, ["steps", "timeout_ms", "$comment"], "plan"):
		return
	if value.has("$comment") and not value["$comment"] is String:
		_reject("plan.$comment", "expected a string")
		return
	timeout_ms = _integer(value, "timeout_ms", 1000, MAX_TIMEOUT_MS, "plan", DEFAULT_TIMEOUT_MS)
	var source: Variant = value.get("steps")
	if not source is Array or source.is_empty() or source.size() > MAX_STEPS:
		_reject("plan.steps", "expected an array of 1 to %d steps" % MAX_STEPS)
		return
	var checked: Array[Dictionary] = []
	for index: int in range(source.size()):
		if not error.is_empty():
			return
		checked.append(_step(source[index], "steps[%d]" % index))
	if error.is_empty():
		steps = checked


## Validate the fields allowed by an action and normalize numbers and omitted options.
func _step(value: Variant, context: String) -> Dictionary:
	if not value is Dictionary:
		_reject(context, "expected an object")
		return {}
	var action: String = _string(value, "action", 32, context)
	var result: Dictionary = {"action": action}
	var fields: Array = []
	match action:
		"status":
			fields = ["label"]
			result.label = _string(value, "label", MAX_STATUS_LABEL_LENGTH, context, true)
		"list_profiles", "cancel_spike", "drop_link", "resume_link", "disconnect", "reconnect", \
				"stop_scenario", "reset_counters":
			pass
		"apply_profile":
			fields = PROFILE_KEYS
			result.merge(_selector(value, context))
		"save_profile":
			fields = ["name", "settings"]
			result.name = _profile_name(value, "name", context)
			result.settings = _settings(value.get("settings"), context + ".settings")
		"spike", "outage":
			fields = ["duration_ms", "direction", "start_delay_ms"]
			result.merge(_fault(value, context))
			if action == "spike":
				fields.append("extra_delay_ms")
				result.extra_delay_ms = _integer(value, "extra_delay_ms", 1, Emulator.MAX_SPIKE_DELAY_MS, context)
		"start_scenario":
			fields = ["template", "stages", "loop", "start_delay_ms"]
			result.merge(_scenario(value, context))
		"move":
			fields = ["direction", "duration_ms"]
			result.direction = _choice(value, "direction", MOVE_DIRECTIONS, context, "")
			result.duration_ms = _integer(value, "duration_ms", 20, 10000, context)
		"wait", "sample":
			fields = ["duration_ms"]
			result.duration_ms = _integer(value, "duration_ms", 1, MAX_TIMEOUT_MS, context)
			if action == "sample":
				fields.append("interval_ms")
				result.interval_ms = _integer(value, "interval_ms", MIN_SAMPLE_INTERVAL_MS, MAX_TIMEOUT_MS,
					context, DEFAULT_SAMPLE_INTERVAL_MS)
		"assert", "wait_until":
			fields = ["field", "op", "value"]
			result.merge(_comparison(value, context))
			if action == "wait_until":
				fields.append("timeout_ms")
				result.timeout_ms = _integer(value, "timeout_ms", 1, MAX_TIMEOUT_MS, context)
		_:
			_reject(context + ".action", "unknown action '%s'" % action)
	_known_keys(value, ["action"] + fields, context)
	return result


## Normalize the three ways to select a profile without loading saved profiles during parsing.
func _selector(value: Dictionary, context: String) -> Dictionary:
	var selected: Array[String] = []
	for key: String in PROFILE_KEYS:
		if value.has(key):
			selected.append(key)
	if selected.size() != 1:
		_reject(context, "choose exactly one of preset, saved, or settings")
		return {}
	match selected[0]:
		"preset":
			var preset: String = _string(value, "preset", 48, context)
			if Profiles.preset_index(StringName(preset)) < 0:
				_reject(context + ".preset", "unknown preset '%s'" % preset)
			return {"preset": preset}
		"saved":
			return {"saved": _profile_name(value, "saved", context)}
	return {"settings": _settings(value.get("settings"), context + ".settings")}


## An inline profile always has every setting, with clear-link defaults for omitted fields.
func _settings(value: Variant, context: String) -> Dictionary:
	if not value is Dictionary:
		_reject(context, "expected an object")
		return {}
	_known_keys(value, SETTINGS_KEYS, context)
	var loss: Variant = value.get("loss_percent", 0.0)
	if not _is_number(loss) or loss < 0.0 or loss > 100.0:
		_reject(context + ".loss_percent", "expected a number from 0 to 100")
		return {}
	return {
		"delay_ms": _integer(value, "delay_ms", 0, Emulator.MAX_DELAY_MS, context, 0),
		"jitter_ms": _integer(value, "jitter_ms", 0, Emulator.MAX_DELAY_MS, context, 0),
		"loss_percent": float(loss),
		"uplink_kbps": _integer(value, "uplink_kbps", 0, Emulator.MAX_BANDWIDTH_KBPS, context, 0),
		"downlink_kbps": _integer(value, "downlink_kbps", 0, Emulator.MAX_BANDWIDTH_KBPS, context, 0),
	}


## Apply the common timing and direction bounds for a transient interruption.
func _fault(value: Dictionary, context: String) -> Dictionary:
	return {
		"duration_ms": _integer(value, "duration_ms", Emulator.MIN_OUTAGE_MS, Emulator.MAX_OUTAGE_MS, context),
		"direction": _choice(value, "direction", DIRECTIONS, context, "both"),
		"start_delay_ms": _integer(value, "start_delay_ms", 0, Emulator.MAX_OUTAGE_START_DELAY_MS, context, 0),
	}


## Validate a built-in scenario or one sequence of independently bounded authored stages.
func _scenario(value: Dictionary, context: String) -> Dictionary:
	var loop: Variant = value.get("loop", false)
	if not loop is bool:
		_reject(context + ".loop", "expected true or false")
		return {}
	var result: Dictionary = {
		"loop": loop,
		"start_delay_ms": _integer(value, "start_delay_ms", 0, Emulator.MAX_OUTAGE_START_DELAY_MS, context, 0),
	}
	if value.has("template") == value.has("stages"):
		_reject(context, "choose exactly one of template or stages")
		return result
	if value.has("template"):
		var template: String = _string(value, "template", 48, context)
		if Profiles.template_index(StringName(template)) < 0:
			_reject(context + ".template", "unknown template '%s'" % template)
		result.template = template
		return result
	var source: Variant = value.get("stages")
	if not source is Array or source.is_empty() or source.size() > Emulator.MAX_SCENARIO_STEPS:
		_reject(context + ".stages", "expected an array of 1 to %d stages" % Emulator.MAX_SCENARIO_STEPS)
		return result
	var stages: Array[Dictionary] = []
	var duration_ms: int = 0
	for index: int in range(source.size()):
		var stage_context: String = "%s.stages[%d]" % [context, index]
		var entry: Variant = source[index]
		if not entry is Dictionary:
			_reject(stage_context, "expected an object")
			break
		if not _known_keys(entry, PROFILE_KEYS + ["label", "duration_ms", "outage"], stage_context):
			break
		var stage: Dictionary = _selector(entry, stage_context)
		stage.duration_ms = _integer(entry, "duration_ms", Emulator.MIN_SCENARIO_STEP_MS,
			Emulator.MAX_SCENARIO_STEP_MS, stage_context)
		if entry.has("label"):
			stage.label = _string(entry, "label", MAX_STAGE_LABEL_LENGTH, stage_context)
		stage.outage = _choice(entry, "outage", DIRECTIONS + ["none"], stage_context, "none")
		if not error.is_empty():
			break
		duration_ms += stage.duration_ms
		stages.append(stage)
	if duration_ms > Emulator.MAX_SCENARIO_TOTAL_MS:
		_reject(context + ".stages", "combined duration exceeds %d ms" % Emulator.MAX_SCENARIO_TOTAL_MS)
	result.stages = stages
	return result


## Comparisons use a dotted snapshot path and one scalar value.
func _comparison(value: Dictionary, context: String) -> Dictionary:
	var field: String = _string(value, "field", MAX_FIELD_LENGTH, context)
	var op: String = _choice(value, "op", COMPARISONS, context, "")
	var operand: Variant = value.get("value")
	var path := RegEx.create_from_string("^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)*$")
	if path.search(field) == null:
		_reject(context + ".field", "expected a dotted field path such as relay.uplink.dropped")
	if not value.has("value") or not (operand == null or operand is bool or operand is String or _is_number(operand)):
		_reject(context + ".value", "expected a scalar value")
	if op not in ["eq", "ne"] and not _is_number(operand):
		_reject(context + ".value", "ordered comparisons require a number")
	return {"field": field, "op": op, "value": operand}


## A reusable name must match the storage contract used by the graphical debugger.
func _profile_name(value: Dictionary, key: String, context: String) -> String:
	var name: String = _string(value, key, Profiles.MAX_PROFILE_NAME, context)
	if name.contains("\n") or name.contains("\r") or name != name.strip_edges():
		_reject(context + "." + key, "use one line without leading or trailing whitespace")
	return name


## Reject misspelled or action-incompatible fields.
func _known_keys(value: Dictionary, allowed: Array, context: String) -> bool:
	for key: String in value:
		if key not in allowed:
			_reject(context + "." + key, "unknown field")
			return false
	return true


## Read a bounded integer, accepting the integral floats returned by Godot's JSON parser.
func _integer(value: Dictionary, key: String, minimum: int, maximum: int, context: String, fallback: int = -1) -> int:
	var number: Variant = value.get(key, fallback)
	if not _is_number(number) or number < minimum or number > maximum or number != floorf(number):
		_reject(context + "." + key, "expected an integer from %d to %d" % [minimum, maximum])
		return minimum
	return int(number)


## Read a nonblank bounded string, or allow an empty optional label.
func _string(value: Dictionary, key: String, maximum: int, context: String, optional: bool = false) -> String:
	var raw: Variant = value.get(key, "")
	if not raw is String or raw.length() > maximum or (raw.strip_edges().is_empty() and not (optional and raw.is_empty())):
		_reject(context + "." + key, "expected a string of %s to %d characters" % ["0" if optional else "1", maximum])
		return ""
	return raw


## Keep string values canonical and reject unknown directions or comparison operators.
func _choice(value: Dictionary, key: String, choices: Array, context: String, fallback: String) -> String:
	var raw: Variant = value.get(key, fallback)
	if not raw is String or raw not in choices:
		_reject(context + "." + key, "expected one of %s" % ", ".join(choices))
		return fallback
	return raw


## Reject infinities without coercing booleans or strings into numbers.
func _is_number(value: Variant) -> bool:
	return value is int or (value is float and is_finite(value))


## Keep the first error so an invalid action reports the earliest applicable location.
func _reject(context: String, reason: String) -> void:
	if error.is_empty():
		error = "%s: %s." % [context, reason]
