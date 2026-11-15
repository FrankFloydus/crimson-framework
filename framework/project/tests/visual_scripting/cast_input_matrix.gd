# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const CAST_EVENT := &"run_cast_probe"
const POLL_EVENT := &"run_input_probe"
const BASE_SCRIPT := "res://cast_matrix_base.gd"
const CHILD_SCRIPT := "res://cast_matrix_child.gd"
const SIBLING_SCRIPT := "res://cast_matrix_sibling.gd"
const PRIMARY_ACTION := &"cast_matrix_primary"
const NEGATIVE_X := &"cast_matrix_left"
const POSITIVE_X := &"cast_matrix_right"
const NEGATIVE_Y := &"cast_matrix_up"
const POSITIVE_Y := &"cast_matrix_down"
const UNBOUND_ACTION := &"cast_matrix_unbound"
const MISSING_ACTION := &"cast_matrix_missing"
const TEST_KEY := KEY_F8

var checked_cases: int = 0
var failed_cases: int = 0


## Run once Godot has initialized the Input singleton and scene tree.
func _initialize() -> void:
	_verify.call_deferred()


## Keep counting independent cases and identify the exact failing scenario.
func _check(condition: bool, case_description: String) -> void:
	checked_cases += 1
	if not condition:
		failed_cases += 1
		push_error("[Crimson] Cast/Input interaction matrix: " + case_description)


## Describe a graph pin with a distinct native object identity when required.
func _pin(pin_name: StringName, variant_type: int, native_class: StringName = &"") -> Dictionary:
	var descriptor := {"id": pin_name, "label": String(pin_name), "type": variant_type}
	if not native_class.is_empty():
		descriptor["class"] = native_class
	return descriptor


## Make a stable connection without referring to positions or editor presentation.
func _wire(source_id: int, source_port: StringName, target_id: int, target_port: StringName) -> Dictionary:
	return {"from": source_id, "from_pin": source_port, "to": target_id, "to_pin": target_port}


## Use the scene owner as an observable receiver for each graph result.
func _store(node_id: int, meta_key: StringName, fallback: Variant = null) -> Dictionary:
	return {"id": node_id, "kind": &"call_method", "member": {"class": &"Object", "name": &"set_meta",
		"inputs": [_pin(&"arg_0", TYPE_STRING_NAME), _pin(&"arg_1", TYPE_NIL)]},
		"values": {&"arg_0": meta_key, &"arg_1": fallback}}


## Attach this fixture's generated graph and wait for no editor-owned state.
func _owner(node_records: Array, connections: Array, event_definitions: Array) -> Node:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", {"version": 1, "nodes": node_records, "connections": connections,
		"events": event_definitions})
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	return graph_owner


## Publish the cast branch and result, optionally deleting the candidate before casting.
func _cast_owner(target_pin: Dictionary, queue_before_cast: bool = false) -> Node:
	var records := [{"id": 1, "kind": &"custom_event", "definition": 1},
		{"id": 2, "kind": &"cast", "pin": target_pin},
		_store(3, &"branch", &"success"), _store(4, &"branch", &"failure"),
		_store(5, &"result"), _store(6, &"result"),
		{"id": 8, "kind": &"call_method", "member": {
			"class": &"Object", "name": &"notify_property_list_changed", "inputs": []}}]
	var connections := [_wire(1, &"candidate", 2, &"object"), _wire(2, &"success", 3, &"in"),
		_wire(2, &"failure", 4, &"in"), _wire(3, &"then", 8, &"in"), _wire(8, &"then", 5, &"in"), _wire(4, &"then", 6, &"in"),
		_wire(2, &"result", 5, &"arg_1"), _wire(2, &"result", 6, &"arg_1")]
	if queue_before_cast:
		records.append({"id": 7, "kind": &"call_method", "member": {
			"class": &"Node", "name": &"queue_free", "inputs": []}})
		connections.append_array([_wire(1, &"then", 7, &"in"), _wire(1, &"candidate", 7, &"target"),
			_wire(7, &"then", 2, &"in")])
	else:
		connections.append(_wire(1, &"then", 2, &"in"))
	return _owner(records, connections, [{"id": 1, "name": CAST_EVENT, "inputs": [
		_pin(&"candidate", TYPE_OBJECT if queue_before_cast else TYPE_NIL, &"Node" if queue_before_cast else &"")]}])


## Poison the prior output before each invocation so Failure must really write null.
func _expect_cast(graph_owner: Node, supplied: Variant, expect_success: bool, case_description: String) -> void:
	graph_owner.set_meta(&"branch", &"unreached")
	graph_owner.set_meta(&"result", &"previous call")
	graph_owner.call(CAST_EVENT, supplied)
	var visited: bool = graph_owner.get_meta(&"branch") == (&"success" if expect_success else &"failure")
	var correct_result: bool = graph_owner.get_meta(&"result", &"missing") == supplied if expect_success else \
		not graph_owner.has_meta(&"result")
	_check(visited and correct_result, case_description)


## Write a GDScript class into the isolated project's res:// for exact nominal casts.
func _script_fixture(script_path: String, declaration: String) -> Script:
	var file := FileAccess.open(script_path, FileAccess.WRITE)
	if file == null:
		return null
	file.store_string(declaration + "\n")
	file.close()
	return ResourceLoader.load(script_path, "GDScript", ResourceLoader.CACHE_MODE_IGNORE) as Script


## Exercise repeated, native, nominal, null, wrong-type, and pending deletion dispatch.
func _run_cast_cases() -> void:
	var native_owner := _cast_owner(_pin(&"target", TYPE_OBJECT, &"Node3D"))
	var exact_node := Node3D.new()
	var derived_node := Marker3D.new()
	var wrong_node := Node2D.new()
	var resource := Resource.new()
	_expect_cast(native_owner, exact_node, true, "exact native class")
	_expect_cast(native_owner, null, false, "null after success clears the result")
	_expect_cast(native_owner, derived_node, true, "native subclass after a failed call")
	_expect_cast(native_owner, wrong_node, false, "sibling native class")
	_expect_cast(native_owner, resource, false, "Resource into Node target")
	_expect_cast(native_owner, 37, false, "integer from a dynamic Variant producer")
	_expect_cast(native_owner, [&"value"], false, "Array from a dynamic Variant producer")
	_expect_cast(native_owner, exact_node, true, "same valid object retried after invalid candidates")
	native_owner.property_list_changed.connect(func() -> void: native_owner.call(CAST_EVENT, null), CONNECT_ONE_SHOT)
	native_owner.call(CAST_EVENT, exact_node)
	_check(native_owner.get_meta(&"branch") == &"failure" and native_owner.get_meta(&"result") == exact_node,
		"a native signal reenters the same cast with null while the outer frame retains its successful result")
	var delete_owner := _cast_owner(_pin(&"target", TYPE_OBJECT, &"Node3D"), true)
	var deleting := Node3D.new()
	root.add_child(deleting)
	_expect_cast(delete_owner, deleting, false, "node queued for deletion during this dispatch")
	delete_owner.free()
	var base_type := _script_fixture(BASE_SCRIPT, "extends Node3D")
	var child_type := _script_fixture(CHILD_SCRIPT, "extends \"" + BASE_SCRIPT + "\"")
	var sibling_type := _script_fixture(SIBLING_SCRIPT, "extends Node3D")
	_check(base_type != null and child_type != null and sibling_type != null, "generate three isolated nominal types")
	if base_type != null and child_type != null and sibling_type != null:
		var nominal_pin := _pin(&"target", TYPE_OBJECT, &"Node3D")
		nominal_pin["script_path"] = BASE_SCRIPT
		nominal_pin["script_name"] = "CastMatrixBase"
		var nominal_owner := _cast_owner(nominal_pin)
		exact_node.set_script(base_type)
		derived_node.set_script(child_type)
		_expect_cast(nominal_owner, exact_node, true, "exact script type")
		_expect_cast(nominal_owner, derived_node, true, "script inheritance accepts a derived owner")
		exact_node.set_script(sibling_type)
		_expect_cast(nominal_owner, exact_node, false, "same native object changed to unrelated script")
		exact_node.set_script(null)
		_expect_cast(nominal_owner, exact_node, false, "same native object had its script detached")
		exact_node.set_script(base_type)
		_expect_cast(nominal_owner, exact_node, true, "same owner accepts the required script again")
		nominal_owner.free()
	native_owner.free()
	derived_node.free()
	exact_node.free()
	wrong_node.free()


## Poll every operation against the same event argument and configured directional actions.
func _poll_owner() -> Node:
	var records := [{"id": 1, "kind": &"custom_event", "definition": 1}]
	var connections: Array[Dictionary] = []
	var outgoing_id := 1
	var outgoing_pin := &"then"
	var operations: Array[StringName] = [&"pressed", &"just_pressed", &"just_released", &"strength", &"axis", &"vector"]
	for operation_index in operations.size():
		var operation := operations[operation_index]
		var query_id := 2 + operation_index * 2
		var store_id := query_id + 1
		var defaults: Dictionary = {}
		if operation_index < 4:
			connections.append(_wire(1, &"action", query_id, &"action"))
			connections.append(_wire(1, &"exact", query_id, &"exact_match"))
		elif operation == &"axis":
			defaults = {&"negative": NEGATIVE_X, &"positive": POSITIVE_X}
		else:
			defaults = {&"negative_x": NEGATIVE_X, &"positive_x": POSITIVE_X,
				&"negative_y": NEGATIVE_Y, &"positive_y": POSITIVE_Y}
			connections.append(_wire(1, &"deadzone", query_id, &"deadzone"))
		records.append({"id": query_id, "kind": &"input_poll", "operation": operation, "values": defaults})
		records.append(_store(store_id, operation))
		connections.append(_wire(outgoing_id, outgoing_pin, store_id, &"in"))
		connections.append(_wire(query_id, &"result", store_id, &"arg_1"))
		outgoing_id = store_id
		outgoing_pin = &"then"
	return _owner(records, connections, [{"id": 1, "name": POLL_EVENT, "inputs": [
		_pin(&"action", TYPE_STRING_NAME), _pin(&"exact", TYPE_BOOL), _pin(&"deadzone", TYPE_FLOAT)]}])


## Feed a real engine input event into the fixture's only keyboard binding.
func _set_key(pressed: bool, shifted: bool = false) -> void:
	var event := InputEventKey.new()
	event.keycode = TEST_KEY
	event.pressed = pressed
	event.shift_pressed = shifted
	Input.parse_input_event(event)
	Input.flush_buffered_events()


## Change configured bindings and input state while reusing the same compiled graph.
func _run_input_cases() -> void:
	var mapped_actions: Array[StringName] = [PRIMARY_ACTION, NEGATIVE_X, POSITIVE_X, NEGATIVE_Y, POSITIVE_Y, UNBOUND_ACTION]
	for mapped_action in mapped_actions:
		InputMap.add_action(mapped_action, 0.2)
	var key_binding := InputEventKey.new()
	key_binding.keycode = TEST_KEY
	InputMap.action_add_event(PRIMARY_ACTION, key_binding)
	var graph_owner := _poll_owner()
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"pressed") == false and graph_owner.get_meta(&"strength") == 0.0,
		"configured key idle")
	_check(graph_owner.get_meta(&"axis") == 0.0 and graph_owner.get_meta(&"vector") == Vector2.ZERO,
		"configured directions idle")
	_set_key(true)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"pressed") == true and graph_owner.get_meta(&"strength") == 1.0,
		"bound key press reports held and full strength")
	var saw_press_edge := false
	for sample_index in 3:
		graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
		var engine_edge := Input.is_action_just_pressed(PRIMARY_ACTION)
		_check(graph_owner.get_meta(&"just_pressed") == engine_edge, "bound press edge matches Godot on sample %d" % sample_index)
		saw_press_edge = saw_press_edge or engine_edge
		await process_frame
	_check(saw_press_edge, "observe one press edge in the window around input and process dispatch")
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"pressed") == true and graph_owner.get_meta(&"just_pressed") == false,
		"held key keeps pressed while its edge expires")
	_set_key(false)
	var saw_release_edge := false
	for sample_index in 3:
		graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
		var engine_edge := Input.is_action_just_released(PRIMARY_ACTION)
		_check(graph_owner.get_meta(&"pressed") == false and graph_owner.get_meta(&"just_released") == engine_edge,
			"bound release edge matches Godot on sample %d" % sample_index)
		saw_release_edge = saw_release_edge or engine_edge
		await process_frame
	_check(saw_release_edge, "observe one release edge in the window around input and process dispatch")
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"just_released") == false, "release edge expires next frame")
	_set_key(true, true)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"pressed") == Input.is_action_pressed(PRIMARY_ACTION, false),
		"modified key with permissive matching uses Godot rules")
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, true, -1.0)
	_check(graph_owner.get_meta(&"pressed") == Input.is_action_pressed(PRIMARY_ACTION, true) and
		graph_owner.get_meta(&"strength") == Input.get_action_strength(PRIMARY_ACTION, true), "exact modifier matching")
	_set_key(false, true)
	InputMap.action_erase_events(PRIMARY_ACTION)
	_set_key(true)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"pressed") == false, "removing the binding disables subsequent key presses")
	_set_key(false)
	graph_owner.call(POLL_EVENT, UNBOUND_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"strength") == 0.0, "configured action without bindings remains neutral")
	graph_owner.call(POLL_EVENT, MISSING_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"pressed") == false and graph_owner.get_meta(&"just_released") == false and
		graph_owner.get_meta(&"strength") == 0.0, "missing action remains neutral")
	graph_owner.call(POLL_EVENT, &"", false, -1.0)
	_check(graph_owner.get_meta(&"just_pressed") == false and graph_owner.get_meta(&"strength") == 0.0,
		"empty action remains neutral")
	Input.action_press(POSITIVE_X, 0.6)
	Input.action_press(NEGATIVE_X, 0.2)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(is_equal_approx(graph_owner.get_meta(&"axis"), 0.4), "positive axis subtracts opposing strength")
	_check(graph_owner.get_meta(&"vector").is_equal_approx(Input.get_vector(NEGATIVE_X, POSITIVE_X, NEGATIVE_Y, POSITIVE_Y)),
		"automatic vector deadzone follows bound action deadzones")
	Input.action_release(NEGATIVE_X)
	Input.action_press(POSITIVE_X)
	Input.action_press(NEGATIVE_Y)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, 0.0)
	_check(graph_owner.get_meta(&"vector").is_equal_approx(Vector2(1.0, -1.0).normalized()), "diagonal direction length is limited")
	Input.action_release(POSITIVE_X)
	Input.action_release(NEGATIVE_Y)
	Input.action_press(POSITIVE_Y, 0.4)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, 0.5)
	_check(graph_owner.get_meta(&"vector") == Vector2.ZERO, "explicit deadzone zeros small direction")
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, 0.0)
	_check(graph_owner.get_meta(&"vector").is_equal_approx(Vector2(0.0, 0.4)), "explicit zero deadzone returns raw magnitude")
	Input.action_release(POSITIVE_Y)
	Input.action_press(NEGATIVE_X)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"axis") == -1.0, "negative axis follows remaining action")
	Input.action_release(NEGATIVE_X)
	InputMap.erase_action(NEGATIVE_X)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"axis") == 0.0 and graph_owner.get_meta(&"vector") == Vector2.ZERO,
		"removed direction makes existing compiled queries neutral")
	InputMap.add_action(NEGATIVE_X, 0.2)
	Input.action_press(NEGATIVE_X)
	graph_owner.call(POLL_EVENT, PRIMARY_ACTION, false, -1.0)
	_check(graph_owner.get_meta(&"axis") == -1.0, "restoring the action recovers the existing graph")
	graph_owner.free()
	for mapped_action in mapped_actions:
		Input.action_release(mapped_action)
		InputMap.erase_action(mapped_action)


## Run the same independent interaction matrix for each DLL configuration.
func _verify() -> void:
	_run_cast_cases()
	await _run_input_cases()
	if failed_cases != 0:
		quit(1)
		return
	print("CRIMSON_VISUAL_SCRIPT_CAST_INPUT_MATRIX_PASSED ", checked_cases)
	quit()
