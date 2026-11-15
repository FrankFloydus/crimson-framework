# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const FIXTURE_SCRIPT: Script = preload("res://tests/visual_scripting/custom_script.crimsonscript")
const AUTHORED_SCRIPT_PATH := "res://authored_custom_script.crimsonscript"


func _initialize() -> void:
	_verify.call_deferred()


func _verify() -> void:
	var arguments := OS.get_cmdline_user_args()
	if "--custom-failure" in arguments:
		await _verify_failed_snippet("var missing: Variant = owner.get_meta(&\"missing\", false)\nmissing.nonexistent_call()")
		return
	if "--custom-suspension" in arguments:
		await _verify_failed_snippet("await owner.get_tree().process_frame\nowner.set_meta(&\"resumed\", true)")
		return
	if "--custom-early-return" in arguments:
		await _verify_failed_snippet("if true:\n    return [true]")
		return
	if "--custom-static-init" in arguments:
		await _verify_failed_snippet("static func _static_init() -> void: print(\"CRIMSON_UNEXPECTED_STATIC_INIT\")\npass")
		return
	if "--custom-source-initializer" in arguments:
		await _verify_failed_snippet("#@input\nvar rounds: int = \"bad\"\npass", true)
		return
	var test_script := FIXTURE_SCRIPT
	if "--editor-authored" in arguments:
		test_script = ResourceLoader.load(AUTHORED_SCRIPT_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
		if not _need(test_script != null, "Reload the graph saved by the real CodeEdit dialog"):
			return
	var lead_player := _spawn(test_script)
	var other_player := _spawn(test_script)
	var expected_volume := -18.0 if "--editor-authored" in arguments else -12.0
	var checks_directives := "--editor-authored" in arguments
	if not _need(lead_player.has_method(&"consume_ammo"), "Loaded graph exposes its typed entry event"):
		return
	_consume_ammo(lead_player, 2, checks_directives)
	if not _need(lead_player.volume_db == expected_volume and lead_player.stream is AudioStreamWAV
			and lead_player.stream.resource_path.ends_with("/custom_script_alarm.tres") and _rounds_match(lead_player, 4),
			"Typed owner, input, outputs, function, variable and event all execute"):
		return
	if checks_directives and not _need(lead_player.get_meta(&"calculated", -1) == 105
			and lead_player.get_meta(&"explicit_null", false) == true
			and lead_player.get_meta(&"example", "") == "\n#@input\nvar inside_string: int\n",
			"Source pins use declared and typed defaults, honor wired null, and initialize the output"):
		return
	if not _need(not other_player.has_meta(&"remaining") and not other_player.has_meta(&"event_payload"),
			"Changing one script instance does not alter another owner"):
		return
	_consume_ammo(lead_player, 9, checks_directives)
	_consume_ammo(other_player, 1, checks_directives)
	if not _need(_rounds_match(lead_player, 0) and _rounds_match(other_player, 5),
			"Both typed outputs reflect the graph function and each owner's state"):
		return
	if checks_directives and not _need(lead_player.get_meta(&"calculated", -1) == 112 and other_player.get_meta(&"calculated", -1) == 104,
			"Directive initializers run for each call with its live input"):
		return
	lead_player.free()
	other_player.free()
	print("CRIMSON_VISUAL_SCRIPT_CUSTOM_RUNTIME_PASSED")
	quit()


func _verify_failed_snippet(source: String, has_source_input: bool = false) -> void:
	var failure_script := ClassDB.instantiate(&"VisualScript") as Script
	failure_script.call(&"set_graph_base", &"AudioStreamPlayer3D")
	var state: Dictionary = FIXTURE_SCRIPT.call(&"get_graph")
	var code_node: Dictionary = state.nodes[1]
	code_node["code"] = source
	code_node["outputs"] = []
	var input_id := &"input_1"
	if has_source_input:
		input_id = &"source_rounds"
		code_node["inputs"] = [{
			"id": input_id, "label": "rounds", "type": TYPE_INT,
			"source_owned": true, "source_default": true,
		}]
	# No typed output is wired here, so a prematurely successful completion would still fire Then.
	state["connections"] = [
		{"from": 1, "from_pin": &"then", "to": 2, "to_pin": &"in"},
		{"from": 1, "from_pin": &"rounds", "to": 2, "to_pin": input_id},
		{"from": 2, "from_pin": &"then", "to": 3, "to_pin": &"in"},
	]
	state.nodes[2]["values"] = {&"arg_0": &"continued", &"arg_1": true}
	failure_script.call(&"set_graph", state)
	var failure_owner := _spawn(failure_script)
	if not _need(failure_owner.has_method(&"consume_ammo"), "Graph with failing snippet exposes its entry"):
		return
	failure_owner.call(&"consume_ammo", 1)
	await process_frame
	await process_frame
	if not _need(not failure_owner.has_meta(&"continued") and not failure_owner.has_meta(&"resumed"),
			"Failed Custom Script neither executes Then nor resumes after its graph frame"):
		return
	failure_owner.free()
	print("CRIMSON_VISUAL_SCRIPT_CUSTOM_FAILURE_PASSED")
	quit()


func _spawn(test_script: Script) -> AudioStreamPlayer3D:
	var graph_owner := AudioStreamPlayer3D.new()
	graph_owner.set_script(test_script)
	root.add_child(graph_owner)
	return graph_owner


func _consume_ammo(player: AudioStreamPlayer3D, rounds: int, has_source_pins: bool) -> void:
	if has_source_pins:
		player.call(&"consume_ammo", rounds, null)
	else:
		player.call(&"consume_ammo", rounds)


func _rounds_match(graph_owner: AudioStreamPlayer3D, expected: int) -> bool:
	var count_value: Variant = graph_owner.get_meta(&"remaining", "")
	var depleted_value: Variant = graph_owner.get_meta(&"depleted", "")
	return count_value is int and count_value == expected and depleted_value is bool \
		and depleted_value == (expected == 0) \
		and graph_owner.get_meta(&"event_payload", "") == expected


func _need(condition: bool, description: String) -> bool:
	if not condition:
		push_error("Custom Script runtime fixture: " + description)
		quit(1)
	return condition
