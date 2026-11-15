# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

## Save the fixture once and reload it before attaching it to native nodes.
const SAVE_PATH := "user://advanced_graph.crimsonscript"
## Isolate the saved disabled-action fixture from other graph round trips.
const DISABLED_SAVE_PATH := "user://disabled_graph.crimsonscript"
## Isolate the collection schema fixture from the other resource round trips.
const COLLECTION_SAVE_PATH := "user://typed_collection_graph.crimsonscript"


## Run after the root window enters the scene tree.
func _initialize() -> void:
	_verify.call_deferred()


## Write an actionable assertion failure and terminate the isolated runtime.
func _fail(requirement: String) -> bool:
	push_error("[Crimson] Advanced Visual Script fixture: " + requirement)
	quit(1)
	return false


## Create a native graph resource without a static dependency on addon registration.
func _script_for(base_class: StringName, graph_data: Dictionary) -> Script:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script != null:
		graph_script.call(&"set_graph_base", base_class)
		graph_script.call(&"set_graph", graph_data)
	return graph_script


## Make one graph node record with any operation-specific fields.
func _op(node_id: int, kind: StringName, fields: Dictionary = {}) -> Dictionary:
	var node_record := {"id": node_id, "kind": kind}
	node_record.merge(fields)
	return node_record


## Reference one execution or data connection using stable pin IDs.
func _wire(source: int, output_pin: StringName, destination: int, input_pin: StringName) -> Dictionary:
	return {"from": source, "from_pin": output_pin, "to": destination, "to_pin": input_pin}


## Wire an execution output to a graph node's In pin.
func _flow(source: int, destination: int, output_pin: StringName = &"then") -> Dictionary:
	return _wire(source, output_pin, destination, &"in")


## Describe a typed Variant pin and optionally its native object class.
func _pin(pin_id: StringName, pin_type: int, native_type: StringName = &"") -> Dictionary:
	var descriptor := {"id": pin_id, "label": String(pin_id), "type": pin_type}
	if native_type != &"":
		descriptor["class"] = native_type
	return descriptor


## Describe an execution tunnel when a macro has multiple entry or exit paths.
func _exec_pin(pin_id: StringName) -> Dictionary:
	return {"id": pin_id, "label": String(pin_id), "type": TYPE_NIL, "exec": true}


## Prepare an observable native Object.set_meta call in a fixture graph.
func _write_meta(node_id: int, meta_key: StringName, literal: Variant = null) -> Dictionary:
	return _op(node_id, &"call_method", {
		"member": {"class": &"Object", "name": &"set_meta", "inputs": [
			_pin(&"arg_0", TYPE_STRING_NAME), _pin(&"arg_1", TYPE_NIL),
		]},
		"values": {&"arg_0": meta_key, &"arg_1": literal},
	})


## Set up one root graph in the serialized format accepted by ResourceSaver.
func _graph(node_records: Array, wires: Array, declarations: Dictionary = {}) -> Dictionary:
	var graph_data := {"version": 1, "nodes": node_records, "connections": wires}
	graph_data.merge(declarations)
	return graph_data


## Define one typed graph variable and its default value.
func _variable(variable_id: int, variable_name: StringName, variable_type: int, initial_value: Variant) -> Dictionary:
	return {"id": variable_id, "name": variable_name, "pin": _pin(&"value", variable_type), "value": initial_value}


## Make a declaration for a function, macro, or event with stable signature IDs.
func _definition(definition_id: int, definition_name: StringName, inputs: Array, outputs: Array, body: Dictionary) -> Dictionary:
	return {"id": definition_id, "name": definition_name, "inputs": inputs, "outputs": outputs, "graph": body}


## Verify an isolated Ready graph and release its owner immediately after the check.
func _verify_ready(graph_data: Dictionary, expected_meta: Dictionary, label: String) -> bool:
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create graph for " + label + ".")
	return _verify_ready_script(graph_script, expected_meta, label)


## Verify a prepared or reloaded graph resource against its script owner's metadata.
func _verify_ready_script(graph_script: Script, expected_meta: Dictionary, label: String) -> bool:
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	for meta_key: StringName in expected_meta:
		var expected: Variant = expected_meta[meta_key]
		if (expected == null and graph_owner.has_meta(meta_key)) or (expected != null and
				(not graph_owner.has_meta(meta_key) or graph_owner.get_meta(meta_key) != expected)):
			graph_owner.free()
			return _fail("%s must produce %s = %s." % [label, meta_key, expected])
	graph_owner.free()
	return true


## Run all focused graph behavior checks in one independent engine process.
func _verify() -> void:
	if not _verify_control_flow() or not _verify_enum_switch() or not _verify_data_nodes() or not _verify_reroute_fanout():
		return
	if not _verify_collection_defaults():
		return
	if not _verify_variant_operators() or not _verify_integer_operator_boundaries():
		return
	if not _verify_godot_member_dispatch() or not _verify_global_constants():
		return
	if not _verify_singleton_sources():
		return
	if not _verify_packed_receiver_isolation():
		return
	if not _verify_foreign_script_dispatch():
		return
	if not _verify_disabled_actions():
		return
	if not _verify_definitions() or not _verify_recursive_function() or not _verify_macro_routes():
		return
	if not _verify_loops() or not _verify_loop_output_scope() or not _verify_nested_loop_values():
		return
	if not _verify_collection_loops():
		return
	if not _verify_stateful_flow() or not await _verify_events():
		return
	if not _verify_script_detaches_during_native_call():
		return
	if not _verify_native_method_reentry() or not _verify_native_argument_spill():
		return
	if not _verify_wired_signals() or not _verify_signal_replacement() or not _verify_self_signal():
		return
	if not _verify_detach_during_signal_delivery():
		return
	if not _verify_disabled_signal_receiver():
		return
	print("CRIMSON_VISUAL_SCRIPT_ADVANCED_PASSED")
	quit()


## Resolve saved global symbols after reloading and pass the key through a native method.
func _verify_global_constants() -> bool:
	const PATH := "user://visual_script_global_constants.crimsonscript"
	const EVENT_ID := 1
	const EVENT_NAME := &"apply_global_constants"
	var graph_script := _script_for(&"Node", _graph([
		_op(1, &"custom_event", {"definition": EVENT_ID}),
		_op(2, &"call_method", {"member": {"class": &"InputEventKey", "name": &"set_keycode",
			"inputs": [_pin(&"arg_0", TYPE_INT)]}}),
		_op(3, &"literal", {"global_constant": &"KEY_ESCAPE"}),
		_write_meta(4, &"minimum"),
		_op(5, &"literal", {"global_constant": &"INT64_MIN"}),
		_write_meta(6, &"mouse_button"),
		_op(7, &"literal", {"global_constant": &"MOUSE_BUTTON_LEFT"}),
	], [
		_flow(1, 2), _wire(3, &"result", 2, &"arg_0"), _wire(1, &"key_event", 2, &"target"),
		_flow(2, 4), _wire(5, &"result", 4, &"arg_1"),
		_flow(4, 6), _wire(7, &"result", 6, &"arg_1"),
	], {"events": [{"id": EVENT_ID, "name": EVENT_NAME, "inputs": [_pin(&"key_event", TYPE_OBJECT, &"InputEventKey")]}]}))
	if graph_script == null or ResourceSaver.save(graph_script, PATH) != OK:
		return _fail("Save source with named global constant nodes.")
	var reloaded := ResourceLoader.load(PATH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if reloaded == null:
		return _fail("Reload global constant nodes by symbol.")
	var key_event := InputEventKey.new()
	var graph_owner := Node.new()
	graph_owner.set_script(reloaded)
	root.add_child(graph_owner)
	graph_owner.call(EVENT_NAME, key_event)
	var resolved: bool = key_event.keycode == KEY_ESCAPE and graph_owner.get_meta(&"minimum", 0) == INT64_MIN and \
		graph_owner.get_meta(&"mouse_button", 0) == MOUSE_BUTTON_LEFT
	graph_owner.free()
	return resolved or _fail("Resolve keycodes, other global enums, and signed 64-bit constants after save and reload.")


## Ensure references read existing native and project singletons without recreating their state.
func _verify_singleton_sources() -> bool:
	const REGISTERED_NAME := &"CrimsonFixtureRegisteredService"
	const AUTOLOAD_NAME := &"CrimsonFixtureAutoload"
	const SCENE_AUTOLOAD_NAME := &"CrimsonFixtureSceneAutoload"
	var registered: Node3D = Node3D.new()
	Engine.register_singleton(REGISTERED_NAME, registered)
	var graph_data := _graph([
		_op(1, &"ready"), _write_meta(2, &"engine_input"), _write_meta(3, &"registered"),
		_write_meta(4, &"script_autoload"), _write_meta(5, &"scene_autoload"),
		_op(6, &"singleton", {"singleton": &"Input", "source": &"engine", "pin": {"class": &"Input"}}),
		_op(7, &"singleton", {"singleton": REGISTERED_NAME, "source": &"engine", "pin": {"class": &"Node3D"}}),
		_op(8, &"singleton", {"singleton": AUTOLOAD_NAME, "source": &"autoload", "pin": {"class": &"Node3D"}}),
		_op(9, &"singleton", {"singleton": SCENE_AUTOLOAD_NAME, "source": &"autoload", "pin": {"class": &"Node3D"}}),
	], [
		_flow(1, 2), _flow(2, 3), _flow(3, 4), _flow(4, 5),
		_wire(6, &"value", 2, &"arg_1"), _wire(7, &"value", 3, &"arg_1"),
		_wire(8, &"value", 4, &"arg_1"), _wire(9, &"value", 5, &"arg_1"),
	])
	var script_autoload := root.get_node_or_null(NodePath(AUTOLOAD_NAME))
	var scene_autoload := root.get_node_or_null(NodePath(SCENE_AUTOLOAD_NAME))
	var correct: bool = script_autoload is Node3D and scene_autoload is Node3D
	if correct:
		correct = _verify_ready(graph_data, {
			&"engine_input": Input, &"registered": registered,
			&"script_autoload": script_autoload, &"scene_autoload": scene_autoload,
		}, "compiled singleton sources")
	Engine.unregister_singleton(REGISTERED_NAME)
	registered.free()
	return correct or _fail("Resolve registered engine objects and both script and scene Autoloads by exact identity.")


## Record one native method descriptor with the same stable pins used by the picker.
func _engine_method(method_id: int, owner: StringName, method: StringName, arguments: Array,
		output_type: int, flags: Dictionary = {}, values: Dictionary = {}) -> Dictionary:
	var member_data := {"class": owner, "name": method, "inputs": arguments}
	if output_type >= TYPE_NIL:
		member_data["output"] = _pin(&"result", output_type)
	member_data.merge(flags)
	return _op(method_id, &"call_method", {"member": member_data, "values": values})


## Check graph calls to built-in values, engine globals, and native Object methods in stock Godot.
func _verify_godot_member_dispatch() -> bool:
	var prefix_method := _engine_method(2, &"String", &"begins_with", [_pin(&"arg_0", TYPE_STRING)],
		TYPE_BOOL, {"variant_type": TYPE_STRING, "const": true}, {&"target": "north_exit", &"arg_0": "north"})
	var suffix_method := _engine_method(3, &"StringName", &"ends_with", [_pin(&"arg_0", TYPE_STRING)],
		TYPE_BOOL, {"variant_type": TYPE_STRING_NAME, "const": true}, {&"target": &"north_exit", &"arg_0": "entry"})
	var replace_method := _engine_method(4, &"String", &"replace", [_pin(&"arg_0", TYPE_STRING), _pin(&"arg_1", TYPE_STRING)],
		TYPE_STRING, {"variant_type": TYPE_STRING, "const": true}, {&"target": "north north", &"arg_0": "north", &"arg_1": "east"})
	var substring_method := _engine_method(5, &"String", &"substr", [_pin(&"arg_0", TYPE_INT),
		_pin(&"arg_1", TYPE_INT).merged({"default": -1})], TYPE_STRING,
		{"variant_type": TYPE_STRING, "const": true}, {&"arg_0": 5})
	var length_method := _engine_method(6, &"Vector3", &"length", [], TYPE_FLOAT,
		{"variant_type": TYPE_VECTOR3, "const": true}, {&"target": Vector3(0, 3, 4)})
	var static_value := _engine_method(7, &"String", &"num_int64", [_pin(&"arg_0", TYPE_INT),
		_pin(&"arg_1", TYPE_INT).merged({"default": 10}), _pin(&"arg_2", TYPE_BOOL).merged({"default": false})],
		TYPE_STRING, {"variant_type": TYPE_STRING, "static": true}, {&"arg_0": 255, &"arg_1": 16})
	var clamp_utility := _engine_method(8, &"@GlobalScope", &"clampf", [_pin(&"arg_0", TYPE_FLOAT), _pin(&"arg_1", TYPE_FLOAT),
		_pin(&"arg_2", TYPE_FLOAT)], TYPE_FLOAT, {"utility": true, "static": true}, {&"arg_0": -10.0, &"arg_1": -3.0, &"arg_2": 5.0})
	var variadic_utility := _engine_method(9, &"@GlobalScope", &"str", [_pin(&"arg_0", TYPE_NIL)], TYPE_STRING,
		{"utility": true, "static": true, "vararg": true}, {&"arg_0": "stage ", &"arguments": [7, true]})
	var static_class_method := _engine_method(10, &"JSON", &"stringify", [_pin(&"arg_0", TYPE_NIL),
		_pin(&"arg_1", TYPE_STRING).merged({"default": ""}), _pin(&"arg_2", TYPE_BOOL).merged({"default": true}),
		_pin(&"arg_3", TYPE_BOOL).merged({"default": false})], TYPE_STRING, {"static": true}, {&"arg_0": {"door": 3}})
	var variadic_class_method := _engine_method(11, &"Object", &"call", [_pin(&"arg_0", TYPE_STRING_NAME)], TYPE_NIL,
		{"vararg": true}, {&"arg_0": &"set_meta", &"arguments": [&"vararg_called", 52]})
	var bind_method := _engine_method(12, &"Callable", &"bind", [], TYPE_CALLABLE,
		{"variant_type": TYPE_CALLABLE, "const": true, "vararg": true}, {&"arguments": [&"mixed", 9]})
	var obtain_callable := _op(20, &"custom_script", {"code": "callback = owner.set_meta", "outputs": [_pin(&"callback", TYPE_CALLABLE)]})
	var call_bound := _engine_method(13, &"Callable", &"call", [], TYPE_NIL,
		{"variant_type": TYPE_CALLABLE, "const": true, "vararg": true})
	var cases := {2: [&"starts", true], 3: [&"ends", false], 4: [&"replaced", "east east"], 5: [&"sliced", "east"],
		6: [&"length", 5.0], 7: [&"hex_number", "ff"], 8: [&"limited", -3.0], 9: [&"combined", "stage 7true"],
		10: [&"encoded", '{"door":3}']}
	var nodes: Array = [_op(1, &"ready"), prefix_method, suffix_method, replace_method, substring_method, length_method,
		static_value, clamp_utility, variadic_utility, static_class_method, variadic_class_method, obtain_callable, bind_method, call_bound]
	var wires: Array = [_wire(4, &"result", 5, &"target"), _wire(20, &"callback", 12, &"target"), _wire(12, &"result", 13, &"target"),
		_flow(11, 20), _flow(20, 12), _flow(12, 13)]
	var expected := {&"vararg_called": 52, &"mixed": 9}
	var previous := 1
	for case_id: int in cases:
		var meta_id := case_id + 30
		nodes.append(_write_meta(meta_id, cases[case_id][0]))
		wires.append_array([_flow(previous, case_id), _flow(case_id, meta_id), _wire(case_id, &"result", meta_id, &"arg_1")])
		expected[cases[case_id][0]] = cases[case_id][1]
		previous = meta_id
	wires.append(_flow(previous, 11))
	return _verify_ready(_graph(nodes, wires), expected, "Godot value, utility and native static and variadic method dispatch")


## Packed mutations affect the connected value but never the defaults of another graph instance.
func _verify_packed_receiver_isolation() -> bool:
	var append_method := _engine_method(3, &"PackedInt32Array", &"append", [_pin(&"arg_0", TYPE_INT)],
		TYPE_BOOL, {"variant_type": TYPE_PACKED_INT32_ARRAY}, {&"arg_0": 7})
	var packed_variable := _variable(1, &"checkpoints", TYPE_PACKED_INT32_ARRAY, PackedInt32Array([1, 2]))
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"get_variable", {"variable": 1}), append_method,
		_write_meta(4, &"updated_target"), _write_meta(5, &"variable_value"),
	], [_flow(1, 3), _flow(3, 4), _flow(4, 5), _wire(2, &"value", 3, &"target"),
		_wire(3, &"updated_target", 4, &"arg_1"), _wire(2, &"value", 5, &"arg_1")], {"variables": [packed_variable]})
	var reusable_graph := _script_for(&"Node", graph_data)
	var expected := PackedInt32Array([1, 2, 7])
	for instance_index: int in range(2):
		if not _verify_ready_script(reusable_graph, {&"updated_target": expected, &"variable_value": expected},
				"Packed array owner %d" % instance_index):
			return false
	return true


## Read, write, and call a different script and subscribe to its signal through a typed scene reference.
func _verify_foreign_script_dispatch() -> bool:
	const SCRIPT_PATH := "res://tests/visual_scripting/foreign_controller.gd"
	var script_type := load(SCRIPT_PATH) as Script
	if script_type == null:
		return _fail("Load the tested controller script.")
	var script_flags := {"script_path": SCRIPT_PATH}
	var optional_pin := _pin(&"arg_1", TYPE_ARRAY).merged({"element": {"type": TYPE_INT}, "script_optional": true})
	var call_refill := _engine_method(3, &"Node", &"recharge", [_pin(&"arg_0", TYPE_INT), optional_pin],
		TYPE_INT, script_flags, {&"arg_0": 2})
	var document := _graph([
		_op(1, &"ready"), _op(2, &"scene_node", {"source": ^"Controller", "class": &"Node", "script_path": SCRIPT_PATH}),
		call_refill,
		_op(4, &"set_property", {"member": {"class": &"Node", "name": &"charge", "script_path": SCRIPT_PATH,
			"inputs": [_pin(&"value", TYPE_INT)]}, "values": {&"value": 21}}),
		_op(5, &"get_property", {"member": {"class": &"Node", "name": &"charge", "script_path": SCRIPT_PATH,
			"inputs": [], "output": _pin(&"value", TYPE_INT)}}),
		_op(6, &"native_signal", {"class": &"Node", "target_class": &"Node", "script_path": SCRIPT_PATH,
			"signal": &"recharged", "inputs": [_pin(&"arg_0", TYPE_INT)], "wired_target": true}),
		_write_meta(7, &"called"), _write_meta(8, &"property"), _write_meta(9, &"signaled"),
	], [
		_flow(1, 4), _flow(4, 3), _flow(3, 7), _flow(7, 8), _flow(6, 9),
		_wire(2, &"node", 3, &"target"), _wire(2, &"node", 4, &"target"), _wire(2, &"node", 5, &"target"),
		_wire(2, &"node", 6, &"target"), _wire(3, &"result", 7, &"arg_1"),
		_wire(5, &"value", 8, &"arg_1"), _wire(6, &"arg_0", 9, &"arg_1"),
	])
	var graph_script := _script_for(&"Node", document)
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	var controller := Node.new()
	controller.name = &"Controller"
	controller.set_script(script_type)
	graph_owner.add_child(controller)
	root.add_child(graph_owner)
	var success: bool = graph_owner.get_meta(&"called", -1) == 30 and graph_owner.get_meta(&"property", -1) == 30 and \
		graph_owner.get_meta(&"signaled", -1) == 30
	graph_owner.free()
	return success or _fail("Typed Script dispatch must apply the setter, dynamic default, method result, getter, and signal payload.")


## Iterate native typed dictionaries and connected typed arrays without changing legacy Array behavior.
func _verify_collection_loops() -> bool:
	var counts_pin := _pin(&"value", TYPE_DICTIONARY)
	counts_pin["key"] = {"type": TYPE_STRING_NAME}
	counts_pin["value"] = {"type": TYPE_INT}
	var labels_pin := _pin(&"value", TYPE_ARRAY)
	labels_pin["element"] = {"type": TYPE_STRING}
	var nested_pin := _pin(&"value", TYPE_ARRAY)
	nested_pin["element"] = {"type": TYPE_DICTIONARY}
	var graph_data := _graph([
		_op(1, &"ready"),
		_op(2, &"get_variable", {"variable": 1}),
		_op(3, &"reroute", {"pin": _pin(&"value", TYPE_DICTIONARY)}),
		_op(4, &"foreach"),
		_write_meta(5, &"dictionary_end", true),
		_write_meta(6, &"dictionary_value"),
		_op(7, &"get_variable", {"variable": 2}),
		_op(8, &"foreach"),
		_write_meta(9, &"label"),
		_op(10, &"continue"),
		_op(11, &"foreach", {"values": {&"array": {}}}),
		_write_meta(12, &"empty_visited", true),
		_op(13, &"foreach", {"values": {&"array": {&"first": 10, &"second": false}}}),
		_write_meta(14, &"break_key"),
		_op(15, &"break"),
		_write_meta(16, &"done", true),
		_write_meta(17, &"dictionary_key"),
		_op(18, &"get_variable", {"variable": 3}),
		_op(19, &"foreach"),
		_op(20, &"foreach"),
		_write_meta(21, &"nested_key"),
		_write_meta(22, &"nested_value"),
		_write_meta(23, &"nested_done", true),
	], [
		_flow(1, 4), _flow(4, 17, &"body"), _flow(17, 6), _flow(4, 5, &"completed"), _flow(5, 8),
		_flow(8, 9, &"body"), _flow(9, 10), _flow(8, 11, &"completed"), _flow(11, 12, &"body"),
		_flow(11, 13, &"completed"), _flow(13, 14, &"body"), _flow(14, 15), _flow(13, 16, &"completed"),
		_flow(16, 19), _flow(19, 20, &"body"), _flow(20, 21, &"body"), _flow(21, 22), _flow(19, 23, &"completed"),
		_wire(2, &"value", 3, &"value"), _wire(3, &"value", 4, &"array"),
		_wire(4, &"key", 17, &"arg_1"), _wire(4, &"item", 6, &"arg_1"),
		_wire(7, &"value", 8, &"array"), _wire(8, &"item", 9, &"arg_1"), _wire(13, &"key", 14, &"arg_1"),
		_wire(18, &"value", 19, &"array"), _wire(19, &"item", 20, &"array"),
		_wire(20, &"key", 21, &"arg_1"), _wire(20, &"item", 22, &"arg_1"),
	], {"variables": [
		{"id": 1, "name": &"counts", "pin": counts_pin, "value": {&"shells": 18, &"rockets": 4}},
		{"id": 2, "name": &"labels", "pin": labels_pin, "value": ["alpha", "omega"]},
		{"id": 3, "name": &"nested", "pin": nested_pin, "value": [{&"west": 8}, {&"east": 9}]},
	]})
	return _verify_ready(graph_data, {
		&"dictionary_end": true, &"dictionary_key": &"rockets", &"dictionary_value": 4, &"label": "omega",
		&"empty_visited": null, &"break_key": &"first", &"done": true,
		&"nested_key": &"east", &"nested_value": 9, &"nested_done": true,
	}, "For Each inference, Dictionary keys, typed Arrays, mixed values, empty pairs, Continue, and Break")


## Check typed and legacy collection defaults after saving a resource and executing its graph.
func _verify_collection_defaults() -> bool:
	var counts_pin := _pin(&"value", TYPE_DICTIONARY)
	counts_pin["key"] = {"type": TYPE_STRING_NAME}
	counts_pin["value"] = {"type": TYPE_INT}
	var status_pin := _pin(&"value", TYPE_ARRAY)
	status_pin["element"] = {"type": TYPE_INT, "enum_type": &"Node.ProcessMode"}
	var graph_data := _graph([
		_op(1, &"ready"),
		_op(2, &"get_variable", {"variable": 1}),
		_op(3, &"get_variable", {"variable": 2}),
		_op(4, &"get_variable", {"variable": 3}),
		_write_meta(5, &"counts"), _write_meta(6, &"modes"), _write_meta(7, &"legacy"),
		_op(8, &"set_variable", {"variable": 4}),
		_op(9, &"get_variable", {"variable": 4}),
		_write_meta(10, &"copied_counts"),
	], [
		_flow(1, 5), _flow(5, 6), _flow(6, 7), _flow(7, 8), _flow(8, 10),
		_wire(2, &"value", 5, &"arg_1"), _wire(3, &"value", 6, &"arg_1"), _wire(4, &"value", 7, &"arg_1"),
		_wire(2, &"value", 8, &"value"), _wire(9, &"value", 10, &"arg_1"),
	], {"variables": [
		{"id": 1, "name": &"counts", "pin": counts_pin, "value": {&"shells": 18}},
		{"id": 2, "name": &"modes", "pin": status_pin, "value": [Node.PROCESS_MODE_ALWAYS]},
		_variable(3, &"legacy", TYPE_ARRAY, ["mixed contents", 5]),
		{"id": 4, "name": &"copied_counts", "pin": counts_pin, "value": {}},
	]})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null or ResourceSaver.save(graph_script, COLLECTION_SAVE_PATH) != OK:
		return _fail("Save a graph containing collection type metadata and plain serialized defaults.")
	var reloaded := ResourceLoader.load(COLLECTION_SAVE_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if reloaded == null:
		return _fail("Reload a graph with typed collection descriptors.")
	var graph_owner := Node.new()
	graph_owner.set_script(reloaded)
	root.add_child(graph_owner)
	var counts: Dictionary = graph_owner.get_meta(&"counts", {})
	var modes: Array = graph_owner.get_meta(&"modes", [])
	var legacy: Array = graph_owner.get_meta(&"legacy", [])
	var copied_counts: Dictionary = graph_owner.get_meta(&"copied_counts", {})
	var success := (
		counts.get_typed_key_builtin() == TYPE_STRING_NAME and counts.get_typed_value_builtin() == TYPE_INT
		and counts == {&"shells": 18} and modes.get_typed_builtin() == TYPE_INT and modes == [Node.PROCESS_MODE_ALWAYS]
		and not legacy.is_typed() and legacy == ["mixed contents", 5] and counts.is_same_typed(copied_counts)
		and copied_counts == counts
	)
	graph_owner.free()
	return success or _fail("Restored variables must preserve shapes, native enum values, compatible wires, and untyped legacy values.")


## Round trip disabled actions and verify execution bypass, data defaults, and restored wires.
func _verify_disabled_actions() -> bool:
	var skipped_call := _write_meta(2, &"skipped", true)
	skipped_call["disabled"] = true
	var disabled_source := _op(3, &"call_function", {"definition": 1, "disabled": true})
	var graph_data := _graph([
		_op(1, &"ready"), skipped_call, disabled_source,
		_write_meta(4, &"result", "fallback"),
	], [
		_flow(1, 2), _flow(2, 3), _flow(3, 4),
		_wire(3, &"result", 4, &"arg_1"),
	], {"functions": [_definition(1, &"supply_value", [], [_pin(&"result", TYPE_STRING)], _graph([
		_op(1, &"function_entry"), _op(2, &"return", {"values": {&"result": "supplied"}}),
	], [_flow(1, 2)]))]})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null or ResourceSaver.save(graph_script, DISABLED_SAVE_PATH) != OK:
		return _fail("Save disabled flags alongside the original wires.")
	var saved_script := ResourceLoader.load(DISABLED_SAVE_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if saved_script == null:
		return _fail("Reload a graph with disabled actions.")
	if not _verify_ready_script(saved_script, {&"skipped": null, &"result": "fallback"}, "Saved disabled chain"):
		return false
	var reenabled_graph: Dictionary = saved_script.call(&"get_graph")
	var reenabled_nodes: Array = reenabled_graph["nodes"]
	reenabled_nodes[1]["disabled"] = false
	reenabled_nodes[2]["disabled"] = false
	if not _verify_ready(reenabled_graph, {&"skipped": true, &"result": "supplied"}, "Re-enabled chain"):
		return false
	var unavailable_member := _op(2, &"call_method", {
		"disabled": true, "member": {"class": &"Object", "name": &"unavailable_method", "inputs": []},
	})
	return _verify_ready(_graph([_op(1, &"ready"), unavailable_member, _write_meta(3, &"continued", true)],
		[_flow(1, 2), _flow(2, 3)]), {&"continued": true}, "Disabled unresolvable member")


## Run both conditional arms, route branches in order, and dispatch Switch default.
func _verify_control_flow() -> bool:
	var graph_data := _graph([
		_op(1, &"ready"),
		_op(2, &"sequence", {"count": 5}),
		_op(3, &"branch", {"values": {&"condition": true}}),
		_op(4, &"branch", {"values": {&"condition": false}}),
		_write_meta(5, &"branch_true", "true path"),
		_write_meta(6, &"branch_false", "false path"),
		_op(7, &"switch", {"cases": [10, 20], "values": {&"value": 20}}),
		_write_meta(8, &"matched_case", 20),
		_op(9, &"switch", {"cases": [10, 20], "values": {&"value": 99}}),
		_write_meta(10, &"default_case", 99),
		_write_meta(11, &"order", "last"),
		_write_meta(12, &"order", "first"),
		_write_meta(13, &"wrong_branch", "unexpected"),
	], [
		_flow(1, 2),
		_flow(2, 3, &"then_0"), _flow(3, 5, &"true"), _flow(5, 12),
		_flow(2, 4, &"then_1"), _flow(4, 6, &"false"), _flow(4, 13, &"true"),
		_flow(2, 7, &"then_2"), _flow(7, 8, &"case_1"),
		_flow(2, 9, &"then_3"), _flow(9, 10, &"default"),
		_flow(2, 11, &"then_4"),
	])
	return _verify_ready(graph_data, {
		&"branch_true": "true path", &"branch_false": "false path", &"matched_case": 20,
		&"default_case": 99, &"order": "last", &"wrong_branch": null,
	}, "Branch, Sequence, and Switch")


## Route one valid native enum value and one unknown value through distinct cases.
func _verify_enum_switch() -> bool:
	const ENUM_TYPE := "Node.ProcessMode"
	var enum_pin := _pin(&"value", TYPE_INT)
	enum_pin["enum_type"] = ENUM_TYPE
	var cases := [
		{"id": &"case_PROCESS_MODE_INHERIT", "label": "Inherit", "value": Node.PROCESS_MODE_INHERIT},
		{"id": &"case_PROCESS_MODE_PAUSABLE", "label": "Pausable", "value": Node.PROCESS_MODE_PAUSABLE},
		{"id": &"case_PROCESS_MODE_WHEN_PAUSED", "label": "When Paused", "value": Node.PROCESS_MODE_WHEN_PAUSED},
		{"id": &"case_PROCESS_MODE_ALWAYS", "label": "Always", "value": Node.PROCESS_MODE_ALWAYS},
		{"id": &"case_PROCESS_MODE_DISABLED", "label": "Disabled", "value": Node.PROCESS_MODE_DISABLED},
	]
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"sequence", {"count": 2}),
		_op(3, &"switch_enum", {"enum_type": ENUM_TYPE, "pin": enum_pin, "cases": cases,
			"values": {&"value": Node.PROCESS_MODE_ALWAYS}}),
		_op(4, &"switch_enum", {"enum_type": ENUM_TYPE, "pin": enum_pin, "cases": cases, "values": {&"value": 99}}),
		_write_meta(5, &"named_case", "Always"), _write_meta(6, &"unknown_case", "Default"),
		_write_meta(7, &"wrong_case", "unexpected"),
	], [_flow(1, 2), _flow(2, 3, &"then_0"), _flow(2, 4, &"then_1"),
		_flow(3, 5, &"case_PROCESS_MODE_ALWAYS"), _flow(3, 7, &"default"), _flow(4, 6, &"default")])
	return _verify_ready(graph_data, {&"named_case": "Always", &"unknown_case": "Default", &"wrong_case": null},
		"Native enum switch")


## Compose typed constants, arithmetic, comparison, and a data-only selector.
func _verify_data_nodes() -> bool:
	var integer_pin := _pin(&"value", TYPE_INT)
	var graph_data := _graph([
		_op(1, &"ready"),
		_op(2, &"literal", {"pin": integer_pin, "values": {&"value": 12}}),
		_op(3, &"operator", {"operator": "add", "pin": integer_pin, "values": {&"b": 4}}),
		_op(4, &"operator", {"operator": "greater", "pin": integer_pin, "values": {&"b": 15}}),
		_op(5, &"select", {"pin": integer_pin, "values": {&"false": -1}}),
		_write_meta(6, &"selected"),
	], [_flow(1, 6), _wire(2, &"result", 3, &"a"), _wire(3, &"result", 4, &"a"),
		_wire(3, &"result", 5, &"true"), _wire(4, &"result", 5, &"condition"), _wire(5, &"result", 6, &"arg_1")])
	return _verify_ready(graph_data, {&"selected": 16}, "Literal, operators, and Select")


## Keep each numeric result's actual type when one authored operator receives different value types.
func _verify_variant_operators() -> bool:
	var sum_value := _definition(301, &"sum_value", [_pin(&"left", TYPE_NIL), _pin(&"right", TYPE_FLOAT)],
		[_pin(&"sum", TYPE_NIL)], _graph([
			_op(1, &"function_entry"),
			_op(2, &"operator", {"operator": "add", "pin": _pin(&"value", TYPE_NIL)}),
			_op(3, &"return"),
		], [_flow(1, 3), _wire(1, &"left", 2, &"a"), _wire(1, &"right", 2, &"b"), _wire(2, &"result", 3, &"sum")]))
	var graph_script := _script_for(&"Node", _graph([], [], {"functions": [sum_value]}))
	if graph_script == null:
		return _fail("Create a mixed numeric operator graph.")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var integer_sum: Variant = graph_owner.call(&"sum_value", 6, 11)
	var fraction_sum: Variant = graph_owner.call(&"sum_value", 6, 0.5)
	var later_integer_sum: Variant = graph_owner.call(&"sum_value", -9, 4)
	graph_owner.free()
	if typeof(integer_sum) != TYPE_INT or integer_sum != 17 or typeof(fraction_sum) != TYPE_FLOAT or fraction_sum != 6.5:
		return _fail("The same operator must accept Int on a Float pin and preserve the result type for mixed numbers.")
	if typeof(later_integer_sum) != TYPE_INT or later_integer_sum != -5:
		return _fail("A mixed arithmetic call must leave later integer operations independent.")
	return true


## Preserve signed remainder, truncating division, and exact 64-bit integer behavior.
func _verify_integer_operator_boundaries() -> bool:
	const MIN_INT := -9223372036854775807 - 1
	const MAX_INT := 9223372036854775807
	const DOUBLE_EXACT_LIMIT := 9007199254740992
	const FIRST_OPERATOR_ID := 3
	var output_operations: Dictionary[StringName, String] = {
		&"sum": "add", &"difference": "subtract", &"product": "multiply", &"quotient": "divide",
		&"remainder": "modulo", &"greater": "greater", &"equal": "equal",
	}
	var nodes: Array = [_op(1, &"function_entry"), _op(2, &"return")]
	var wires: Array = []
	var outputs: Array = []
	var locals: Array = []
	var operator_id := FIRST_OPERATOR_ID
	var previous_action_id := 1
	for output_name: StringName in output_operations:
		var result_type := TYPE_BOOL if output_name == &"greater" or output_name == &"equal" else TYPE_INT
		var default_value: Variant = false if result_type == TYPE_BOOL else 0
		var setter_id := operator_id + 1
		var getter_id := operator_id + 2
		var local_reference := {"scope": "local", "variable": operator_id}
		outputs.append(_pin(output_name, result_type))
		locals.append(_variable(operator_id, output_name, result_type, default_value))
		nodes.append(_op(operator_id, &"operator", {"operator": output_operations[output_name], "pin": _pin(&"value", TYPE_INT)}))
		nodes.append(_op(setter_id, &"set_variable", local_reference))
		nodes.append(_op(getter_id, &"get_variable", local_reference))
		wires.append_array([
			_flow(previous_action_id, setter_id), _wire(1, &"left", operator_id, &"a"),
			_wire(1, &"right", operator_id, &"b"), _wire(operator_id, &"result", setter_id, &"value"),
			_wire(getter_id, &"value", 2, output_name),
		])
		previous_action_id = setter_id
		operator_id = getter_id + 1
	wires.append(_flow(previous_action_id, 2))
	var arithmetic := _definition(302, &"integer_ops", [_pin(&"left", TYPE_INT), _pin(&"right", TYPE_INT)],
		outputs, _graph(nodes, wires, {"locals": locals}))
	var graph_script := _script_for(&"Node", _graph([], [], {"functions": [arithmetic]}))
	if graph_script == null:
		return _fail("Create a typed integer operator graph.")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var arithmetic_cases: Array[Dictionary] = [
		{"operands": [-7, 3], "expected": {&"quotient": -2, &"remainder": -1}},
		{"operands": [7, -3], "expected": {&"quotient": -2, &"remainder": 1}},
		{"operands": [-7, -3], "expected": {&"quotient": 2, &"remainder": -1}},
		{"operands": [MIN_INT, 2], "expected": {&"quotient": -4611686018427387904, &"remainder": 0}},
		{"operands": [MIN_INT, -1], "expected": {&"quotient": MIN_INT, &"remainder": 0}},
		{"operands": [MAX_INT, 1], "expected": {&"sum": MIN_INT}},
		{"operands": [MIN_INT, 1], "expected": {&"difference": MAX_INT}},
		{"operands": [MAX_INT, 2], "expected": {&"product": -2}},
		{"operands": [DOUBLE_EXACT_LIMIT + 1, DOUBLE_EXACT_LIMIT], "expected": {&"greater": true, &"equal": false}},
	]
	for arithmetic_case: Dictionary in arithmetic_cases:
		var operands: Array = arithmetic_case["operands"]
		var expected_outputs: Dictionary = arithmetic_case["expected"]
		var actual_outputs: Variant = graph_owner.call(&"integer_ops", operands[0], operands[1])
		for output_name: StringName in expected_outputs:
			var expected_output: Variant = expected_outputs[output_name]
			var actual_output: Variant = actual_outputs.get(output_name) if actual_outputs is Dictionary else null
			if typeof(actual_output) != typeof(expected_output) or actual_output != expected_output:
				graph_owner.free()
				return _fail("Int %s with %s must produce %s." % [output_name, operands, expected_output])
	graph_owner.free()
	return true


## Read the same value through two data wires from one reroute.
func _verify_reroute_fanout() -> bool:
	var integer_pin := _pin(&"value", TYPE_INT)
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"literal", {"pin": integer_pin, "values": {&"value": 42}}),
		_op(3, &"reroute", {"mode": "data", "pin": integer_pin}),
		_write_meta(4, &"first_consumer"), _write_meta(5, &"second_consumer"),
	], [_flow(1, 4), _flow(4, 5), _wire(2, &"result", 3, &"value"),
		_wire(3, &"value", 4, &"arg_1"), _wire(3, &"value", 5, &"arg_1")])
	return _verify_ready(graph_data, {&"first_consumer": 42, &"second_consumer": 42}, "Data reroute fan-out")


## Verify property defaults, callable return values, macros, and fresh call frames.
func _verify_definitions() -> bool:
	var output_pin := _pin(&"result", TYPE_INT)
	var input_pin := _pin(&"new_value", TYPE_INT)
	var read_counter := _definition(101, &"read_counter", [], [output_pin], _graph([
		_op(1, &"function_entry"), _op(2, &"get_variable", {"scope": "instance", "variable": 11}), _op(3, &"return"),
	], [_flow(1, 3), _wire(2, &"value", 3, &"result")]))
	var write_counter := _definition(102, &"write_counter", [input_pin], [output_pin], _graph([
		_op(1, &"function_entry"), _op(2, &"set_variable", {"scope": "instance", "variable": 11}), _op(3, &"return"),
		_op(4, &"get_variable", {"scope": "instance", "variable": 11}),
	], [_flow(1, 2), _flow(2, 3), _wire(1, &"new_value", 2, &"value"), _wire(4, &"value", 3, &"result")]))
	var before_pin := _pin(&"before", TYPE_INT)
	var after_pin := _pin(&"after", TYPE_INT)
	var local_counter := _definition(103, &"local_counter", [input_pin], [before_pin, after_pin], _graph([
		_op(1, &"function_entry"),
		_op(2, &"get_variable", {"scope": "local", "variable": 12}),
		_write_meta(3, &"local_before"),
		_op(4, &"set_variable", {"scope": "local", "variable": 12}),
		_op(5, &"return", {"values": {&"before": 0}}),
	], [_flow(1, 3), _wire(2, &"value", 3, &"arg_1"), _flow(3, 4), _wire(1, &"new_value", 4, &"value"),
		_flow(4, 5), _wire(2, &"value", 5, &"after")], {"locals": [_variable(12, &"scratch", TYPE_INT, 7)]}))
	var route_value := _definition(104, &"route_value", [input_pin], [output_pin], _graph([
		_op(1, &"macro_entry"), _op(2, &"return"),
		_op(3, &"reroute", {"mode": "data", "pin": _pin(&"value", TYPE_INT)}),
	], [_flow(1, 2), _wire(1, &"new_value", 3, &"value"), _wire(3, &"value", 2, &"result")]))
	var graph_data := _graph([
		_op(1, &"ready"),
		_op(2, &"call_function", {"definition": 101}),
		_op(3, &"call_macro", {"definition": 104}),
		_write_meta(4, &"ready_counter"),
		_op(5, &"self"),
		_op(6, &"reroute", {"mode": "exec"}),
	], [
		_flow(1, 2), _flow(2, 3), _wire(2, &"result", 3, &"new_value"), _flow(3, 6), _flow(6, 4),
		_wire(3, &"result", 4, &"arg_1"), _wire(5, &"node", 4, &"target"),
	], {
		"variables": [_variable(11, &"counter", TYPE_INT, 5)],
		"functions": [read_counter, write_counter, local_counter], "macros": [route_value],
	})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null or ResourceSaver.save(graph_script, SAVE_PATH) != OK:
		return _fail("Save declarations, variables, functions, and macros in one graph.")
	var saved_script := ResourceLoader.load(SAVE_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if saved_script == null:
		return _fail("Reload advanced declarations through the native graph loader.")
	var first_owner := Node.new()
	var second_owner := Node.new()
	first_owner.set_script(saved_script)
	second_owner.set_script(saved_script)
	root.add_child(first_owner)
	root.add_child(second_owner)
	if first_owner.get_meta(&"ready_counter", -1) != 5 or second_owner.get_meta(&"ready_counter", -1) != 5:
		return _fail("Ready gets the default through a function, macro, reroutes, and Self.")
	var callable_function: Dictionary = {}
	for method: Dictionary in first_owner.get_method_list():
		if method.name == &"write_counter":
			callable_function = method
			break
	if callable_function.is_empty() or callable_function.args.size() != 1 or callable_function.args[0].type != TYPE_INT or callable_function["return"].type != TYPE_INT:
		return _fail("Godot lists the graph function with its typed argument and return value.")
	if not first_owner.has_method(&"write_counter") or first_owner.call(&"write_counter", 41) != 41:
		return _fail("A native node can invoke a typed graph function and receive a single result.")
	if first_owner.call(&"read_counter") != 41 or second_owner.call(&"read_counter") != 5:
		return _fail("Two owners of the same graph resource keep independent instance state.")
	var first_result: Variant = first_owner.call(&"local_counter", 21)
	if first_result != {&"before": 0, &"after": 21} or first_owner.get_meta(&"local_before", -1) != 7:
		return _fail("Function locals have defaults and a typed two-output return.")
	var second_result: Variant = first_owner.call(&"local_counter", 32)
	if second_result != {&"before": 0, &"after": 32} or first_owner.get_meta(&"local_before", -1) != 7:
		return _fail("Each function invocation gets fresh local variables.")
	first_owner.free()
	second_owner.free()
	return true


## A recursive invocation of the same compiled function must preserve the suspended parent's slots.
func _verify_recursive_function() -> bool:
	var function_id := 304
	var remaining_id := 51
	var int_pin := _pin(&"value", TYPE_INT)
	var sum_remaining := _definition(function_id, &"sum_remaining", [_pin(&"count", TYPE_INT)], [_pin(&"sum", TYPE_INT)],
		_graph([
			_op(1, &"function_entry"), _op(2, &"set_variable", {"scope": "local", "variable": remaining_id}),
			_op(3, &"get_variable", {"scope": "local", "variable": remaining_id}),
			_op(4, &"operator", {"operator": "greater", "pin": int_pin, "values": {&"b": 0}}),
			_op(5, &"branch"), _op(6, &"operator", {"operator": "subtract", "pin": int_pin, "values": {&"b": 1}}),
			_op(7, &"call_function", {"definition": function_id}), _op(8, &"operator", {"operator": "add", "pin": int_pin}),
			_op(9, &"return", {"values": {&"sum": 0}}), _op(10, &"return"),
		], [
			_flow(1, 2), _wire(1, &"count", 2, &"value"), _flow(2, 5),
			_wire(3, &"value", 4, &"a"), _wire(4, &"result", 5, &"condition"),
			_flow(5, 7, &"true"), _wire(3, &"value", 6, &"a"), _wire(6, &"result", 7, &"count"),
			_flow(7, 10), _wire(7, &"sum", 8, &"a"), _wire(3, &"value", 8, &"b"),
			_wire(8, &"result", 10, &"sum"), _flow(5, 9, &"false"),
		], {"locals": [_variable(remaining_id, &"remaining", TYPE_INT, 0)]}))
	var graph_script := _script_for(&"Node", _graph([], [], {"functions": [sum_remaining]}))
	if graph_script == null:
		return _fail("Create a recursive function graph.")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var five_sum: Variant = graph_owner.call(&"sum_remaining", 5)
	var three_sum: Variant = graph_owner.call(&"sum_remaining", 3)
	graph_owner.free()
	if five_sum != 15 or three_sum != 6:
		return _fail("Every recursive call needs its own argument, local, and return value slots.")
	return true


## Dispatch distinct macro execution entries and return to their matching continuations.
func _verify_macro_routes() -> bool:
	var routes := _definition(201, &"route_paths", [_exec_pin(&"start_a"), _exec_pin(&"start_b")],
		[_exec_pin(&"done_a"), _exec_pin(&"done_b")], _graph([
			_op(1, &"macro_entry"), _op(2, &"return"),
		], [_wire(1, &"start_a", 2, &"done_a"), _wire(1, &"start_b", 2, &"done_b")]))
	var broadcast := _definition(202, &"broadcast_paths", [], [_exec_pin(&"done_a"), _exec_pin(&"done_b")], _graph([
		_op(1, &"macro_entry"), _op(2, &"sequence", {"count": 2}), _op(3, &"return"),
	], [_flow(1, 2), _wire(2, &"then_0", 3, &"done_a"), _wire(2, &"then_1", 3, &"done_b")]))
	var inactive := _definition(203, &"no_continuation", [], [], _graph([
		_op(1, &"macro_entry"), _op(2, &"branch"), _op(3, &"return"),
	], [_flow(1, 2), _flow(2, 3, &"true")]))
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"sequence", {"count": 4}),
		_op(3, &"call_macro", {"definition": 201}),
		_write_meta(4, &"macro_a", "A"), _write_meta(5, &"macro_b", "B"),
		_op(6, &"call_macro", {"definition": 202}),
		_write_meta(7, &"broadcast_a", "A"), _write_meta(8, &"broadcast_b", "B"),
		_op(9, &"call_macro", {"definition": 203}), _write_meta(10, &"inactive", "unexpected"),
	], [
		_flow(1, 2), _wire(2, &"then_0", 3, &"start_a"), _wire(2, &"then_1", 3, &"start_b"),
		_flow(3, 4, &"done_a"), _flow(3, 5, &"done_b"),
		_flow(2, 6, &"then_2"), _flow(6, 7, &"done_a"), _flow(6, 8, &"done_b"),
		_flow(2, 9, &"then_3"), _flow(9, 10),
	], {"macros": [routes, broadcast, inactive]})
	return _verify_ready(graph_data, {&"macro_a": "A", &"macro_b": "B", &"broadcast_a": "A", &"broadcast_b": "B", &"inactive": null},
		"Macro execution tunnels and conditional continuations")


## Check inclusive loops, empty collection behavior, and the nearest loop controls.
func _verify_loops() -> bool:
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"sequence", {"count": 6}),
		_op(3, &"for", {"values": {&"first": 1, &"last": 5, &"step": 2}}),
		_write_meta(4, &"inclusive_index"),
		_op(5, &"foreach", {"values": {&"array": ["alpha", "omega"]}}),
		_write_meta(6, &"last_item"), _write_meta(7, &"last_item_index"),
		_op(8, &"while"), _op(9, &"get_variable", {"scope": "instance", "variable": 21}),
		_write_meta(10, &"while_body", true),
		_op(11, &"set_variable", {"scope": "instance", "variable": 21, "values": {&"value": false}}),
		_write_meta(12, &"while_completed", true),
		_op(13, &"for", {"values": {&"first": 2, &"last": 8, &"step": 2}}),
		_op(14, &"switch", {"cases": [2, 6]}), _op(15, &"continue"), _op(16, &"break"),
		_write_meta(17, &"non_skipped_index"), _write_meta(18, &"broken_loop_completed", true),
		_op(19, &"foreach", {"values": {&"array": []}}),
		_write_meta(20, &"empty_body", true), _write_meta(21, &"empty_completed", true),
		_op(22, &"for", {"values": {&"first": 5, &"last": 1, &"step": -2}}),
		_write_meta(23, &"descending_index"),
	], [
		_flow(1, 2), _flow(2, 3, &"then_0"), _flow(3, 4, &"body"), _wire(3, &"index", 4, &"arg_1"),
		_flow(2, 5, &"then_1"), _flow(5, 6, &"body"), _wire(5, &"item", 6, &"arg_1"),
		_flow(6, 7), _wire(5, &"index", 7, &"arg_1"),
		_flow(2, 8, &"then_2"), _wire(9, &"value", 8, &"condition"), _flow(8, 10, &"body"), _flow(10, 11),
		_flow(8, 12, &"completed"),
		_flow(2, 13, &"then_3"), _flow(13, 14, &"body"), _wire(13, &"index", 14, &"value"),
		_flow(14, 15, &"case_0"), _flow(14, 16, &"case_1"), _flow(14, 17, &"default"),
		_wire(13, &"index", 17, &"arg_1"), _flow(13, 18, &"completed"),
		_flow(2, 19, &"then_4"), _flow(19, 20, &"body"), _flow(19, 21, &"completed"),
		_flow(2, 22, &"then_5"), _flow(22, 23, &"body"), _wire(22, &"index", 23, &"arg_1"),
	], {"variables": [_variable(21, &"keep_looping", TYPE_BOOL, true)]})
	return _verify_ready(graph_data, {
		&"inclusive_index": 5, &"descending_index": 1, &"last_item": "omega", &"last_item_index": 1,
		&"while_body": true, &"while_completed": true, &"non_skipped_index": 4,
		&"broken_loop_completed": true, &"empty_body": null, &"empty_completed": true,
	}, "For, For Each, While, Continue, and Break")


## A loop restores its prior execution outputs while stateful nodes retain their advanced state.
func _verify_loop_output_scope() -> bool:
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"sequence", {"count": 2}), _op(3, &"flip_flop"),
		_op(4, &"for", {"values": {&"first": 1, &"last": 1, &"step": 1}}),
		_write_meta(5, &"initial_output"), _write_meta(6, &"inside_loop"), _write_meta(7, &"after_loop"),
		_op(8, &"custom_event", {"definition": 401}),
	], [
		_flow(1, 2), _flow(2, 3, &"then_0"), _flow(2, 4, &"then_1"), _flow(4, 3, &"body"),
		_flow(3, 5, &"a"), _wire(3, &"is_a", 5, &"arg_1"), _flow(3, 6, &"b"), _wire(3, &"is_a", 6, &"arg_1"),
		_flow(4, 7, &"completed"), _wire(3, &"is_a", 7, &"arg_1"), _flow(8, 3),
	], {"events": [{"id": 401, "name": &"pulse_again", "inputs": []}]})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create a graph that executes the same Flip-Flop inside and outside a loop.")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var restored: bool = (
		graph_owner.get_meta(&"initial_output", false) == true
		and graph_owner.get_meta(&"inside_loop", true) == false
		and graph_owner.get_meta(&"after_loop", false) == true
	)
	graph_owner.remove_meta(&"initial_output")
	graph_owner.remove_meta(&"inside_loop")
	graph_owner.call(&"pulse_again")
	var state_advanced: bool = graph_owner.get_meta(&"initial_output", false) == true and not graph_owner.has_meta(&"inside_loop")
	graph_owner.free()
	if not restored or not state_advanced:
		return _fail("Loop completion must restore the earlier output while the Flip-Flop continues to its next state.")
	return true


## Accumulate nested loop pairs and keep caller and callee locals with the same ID independent.
func _verify_nested_loop_values() -> bool:
	# Each call starts its scratch value at 7 and returns it plus the supplied pair.
	var scratch_id := 41
	var int_pin := _pin(&"value", TYPE_INT)
	var add_scratch := _definition(302, &"add_scratch", [_pin(&"pair", TYPE_INT)], [_pin(&"delta", TYPE_INT)],
		_graph([
			_op(1, &"function_entry"), _op(2, &"get_variable", {"scope": "local", "variable": scratch_id}),
			_op(3, &"operator", {"operator": "add", "pin": int_pin}),
			_op(4, &"set_variable", {"scope": "local", "variable": scratch_id}), _op(5, &"return"),
		], [
			_flow(1, 4), _flow(4, 5), _wire(2, &"value", 3, &"a"), _wire(1, &"pair", 3, &"b"),
			_wire(3, &"result", 4, &"value"), _wire(2, &"value", 5, &"delta"),
		], {"locals": [_variable(scratch_id, &"callee_total", TYPE_INT, 7)]}))
	# Add 7 + outer index * 10 + inner index for each pair, then add the enclosing index once.
	var sample_pairs := _definition(303, &"sample_pairs", [_pin(&"seed", TYPE_INT)],
		[_pin(&"total", TYPE_INT), _pin(&"instance_value", TYPE_INT)], _graph([
			_op(1, &"function_entry"), _op(2, &"set_variable", {"scope": "local", "variable": scratch_id}),
			_op(3, &"for", {"values": {&"first": 1, &"last": 2, &"step": 1}}),
			_op(4, &"for", {"values": {&"first": 1, &"last": 2, &"step": 1}}),
			_op(5, &"operator", {"operator": "multiply", "pin": int_pin, "values": {&"b": 10}}),
			_op(6, &"operator", {"operator": "add", "pin": int_pin}),
			_op(7, &"call_function", {"definition": 302}),
			_op(8, &"get_variable", {"scope": "local", "variable": scratch_id}),
			_op(9, &"operator", {"operator": "add", "pin": int_pin}),
			_op(10, &"set_variable", {"scope": "local", "variable": scratch_id}),
			_op(11, &"operator", {"operator": "add", "pin": int_pin}),
			_op(12, &"set_variable", {"scope": "local", "variable": scratch_id}),
			_op(13, &"return"), _op(14, &"get_variable", {"scope": "instance", "variable": scratch_id}),
		], [
			_flow(1, 2), _wire(1, &"seed", 2, &"value"), _flow(2, 3), _flow(3, 4, &"body"),
			_flow(4, 7, &"body"), _wire(3, &"index", 5, &"a"), _wire(5, &"result", 6, &"a"),
			_wire(4, &"index", 6, &"b"), _wire(6, &"result", 7, &"pair"), _flow(7, 10),
			_wire(8, &"value", 9, &"a"), _wire(7, &"delta", 9, &"b"), _wire(9, &"result", 10, &"value"),
			_flow(4, 12, &"completed"), _wire(8, &"value", 11, &"a"), _wire(3, &"index", 11, &"b"),
			_wire(11, &"result", 12, &"value"), _flow(3, 13, &"completed"), _wire(8, &"value", 13, &"total"),
			_wire(14, &"value", 13, &"instance_value"),
		], {"locals": [_variable(scratch_id, &"caller_total", TYPE_INT, 0)]}))
	var graph_script := _script_for(&"Node", _graph([], [], {
		"functions": [add_scratch, sample_pairs], "variables": [_variable(scratch_id, &"fixed_score", TYPE_INT, 40)],
	}))
	if graph_script == null:
		return _fail("Create a nested loop and function graph.")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var first_result: Variant = graph_owner.call(&"sample_pairs", 3)
	var second_result: Variant = graph_owner.call(&"sample_pairs", 20)
	graph_owner.free()
	if first_result != {&"total": 100, &"instance_value": 40} or second_result != {&"total": 117, &"instance_value": 40}:
		return _fail("Nested loops keep the enclosing index and re-read writes, with separate local and instance values.")
	return true


## Check gates and counters across separate calls and across two scene instances.
func _verify_stateful_flow() -> bool:
	var controls: Array = [
		{"id": 1, "name": &"activate_nodes", "inputs": [_pin(&"round", TYPE_INT)]},
		{"id": 2, "name": &"reset_nodes", "inputs": []},
		{"id": 3, "name": &"open_gate", "inputs": []},
		{"id": 4, "name": &"toggle_gate", "inputs": []},
	]
	var graph_data := _graph([
		_op(1, &"custom_event", {"definition": 1}), _op(2, &"sequence", {"count": 4}),
		_op(3, &"do_once"), _op(4, &"do_n", {"values": {&"count": 2}}),
		_op(5, &"flip_flop"), _op(6, &"gate"),
		_write_meta(7, &"once_round"), _write_meta(8, &"count_round"),
		_write_meta(9, &"side_a"), _write_meta(10, &"side_b"), _write_meta(11, &"gate_round"),
		_op(12, &"custom_event", {"definition": 2}), _op(13, &"sequence", {"count": 4}),
		_op(14, &"custom_event", {"definition": 3}), _op(15, &"custom_event", {"definition": 4}),
	], [
		_flow(1, 2), _flow(2, 3, &"then_0"), _flow(2, 4, &"then_1"), _flow(2, 5, &"then_2"), _flow(2, 6, &"then_3"),
		_flow(3, 7), _wire(1, &"round", 7, &"arg_1"), _flow(4, 8), _wire(1, &"round", 8, &"arg_1"),
		_flow(5, 9, &"a"), _flow(5, 10, &"b"), _wire(5, &"is_a", 9, &"arg_1"), _wire(5, &"is_a", 10, &"arg_1"),
		_flow(6, 11), _wire(1, &"round", 11, &"arg_1"),
		_flow(12, 13), _wire(13, &"then_0", 3, &"reset"), _wire(13, &"then_1", 4, &"reset"),
		_wire(13, &"then_2", 5, &"reset"), _wire(13, &"then_3", 6, &"close"),
		_wire(14, &"then", 6, &"open"), _wire(15, &"then", 6, &"toggle"),
	], {"events": controls})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create graph with stateful flow nodes.")
	var first_owner := Node.new()
	var second_owner := Node.new()
	first_owner.set_script(graph_script)
	second_owner.set_script(graph_script)
	root.add_child(first_owner)
	root.add_child(second_owner)
	first_owner.call(&"activate_nodes", 1)
	if first_owner.get_meta(&"once_round", -1) != 1 or first_owner.get_meta(&"count_round", -1) != 1 or not first_owner.get_meta(&"side_a", false) or first_owner.has_meta(&"gate_round"):
		return _fail("Do Once and Do N fire initially, Flip Flop starts at A, and Gate starts closed.")
	first_owner.call(&"open_gate")
	first_owner.call(&"activate_nodes", 2)
	first_owner.call(&"activate_nodes", 3)
	if first_owner.get_meta(&"once_round", -1) != 1 or first_owner.get_meta(&"count_round", -1) != 2 or first_owner.get_meta(&"side_b", true) or first_owner.get_meta(&"gate_round", -1) != 3:
		return _fail("Do Once blocks repeats, Do N stops after two, Flip Flop reaches B, and open Gate continues.")
	first_owner.call(&"reset_nodes")
	first_owner.remove_meta(&"side_a")
	first_owner.call(&"activate_nodes", 4)
	if first_owner.get_meta(&"once_round", -1) != 4 or first_owner.get_meta(&"count_round", -1) != 4 or not first_owner.get_meta(&"side_a", false) or first_owner.get_meta(&"gate_round", -1) != 3:
		return _fail("Reset controls reopen Do Once and Do N, restore Flip Flop A, and close Gate without forwarding.")
	first_owner.call(&"toggle_gate")
	first_owner.call(&"activate_nodes", 5)
	second_owner.call(&"activate_nodes", 11)
	if first_owner.get_meta(&"gate_round", -1) != 5 or second_owner.get_meta(&"once_round", -1) != 11 or second_owner.has_meta(&"gate_round"):
		return _fail("Gate toggle works and stateful nodes do not share state between graph owners.")
	first_owner.free()
	second_owner.free()
	return true


## Exercise native scene references and signal arguments alongside custom and lifecycle events.
func _verify_events() -> bool:
	var graph_data := _graph([
		_op(1, &"lifecycle", {"callback": &"_enter_tree"}), _write_meta(2, &"entered", true),
		_op(3, &"ready"),
		_op(4, &"scene_node", {"source": NodePath("Volume"), "class": &"HSlider"}),
		_op(5, &"set_property", {
			"member": {"class": &"Range", "name": &"value", "inputs": [_pin(&"value", TYPE_FLOAT)]},
			"values": {&"value": 24.0},
		}),
		_op(6, &"native_signal", {"source": NodePath("Volume"), "class": &"Range", "signal": &"value_changed",
			"inputs": [_pin(&"arg_0", TYPE_FLOAT)]}),
		_write_meta(7, &"signal_value"),
		_op(8, &"emit_event", {"definition": 41, "values": {&"payload": "graph emitted"}}),
		_op(9, &"custom_event", {"definition": 41}), _write_meta(10, &"custom_payload"),
		_op(11, &"lifecycle", {"callback": &"_exit_tree"}), _write_meta(12, &"exited", true),
		_op(13, &"lifecycle", {"callback": &"_process"}), _write_meta(14, &"frame_delta"),
	], [
		_flow(1, 2), _flow(3, 5), _wire(4, &"node", 5, &"target"), _flow(5, 8),
		_flow(6, 7), _wire(6, &"arg_0", 7, &"arg_1"),
		_flow(9, 10), _wire(9, &"payload", 10, &"arg_1"), _flow(11, 12),
		_flow(13, 14), _wire(13, &"delta", 14, &"arg_1"),
	], {"events": [{"id": 41, "name": &"on_graph_message", "inputs": [_pin(&"payload", TYPE_STRING)]}]})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create graph with scene node references and three kinds of events.")
	var graph_owner := Node.new()
	var slider := HSlider.new()
	slider.name = &"Volume"
	graph_owner.add_child(slider)
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	if not graph_owner.get_meta(&"entered", false) or graph_owner.get_meta(&"signal_value", -1.0) != 24.0:
		return _fail("Enter Tree runs and the native signal receives a typed payload from Ready.")
	if graph_owner.get_meta(&"custom_payload", "") != "graph emitted":
		return _fail("Emit Event dispatches the graph's own typed event.")
	slider.value = 31.0
	if graph_owner.get_meta(&"signal_value", -1.0) != 31.0:
		return _fail("Native signal remains connected after Ready returns.")
	if not graph_owner.has_method(&"on_graph_message"):
		return _fail("A custom event is callable on the native node that owns the graph.")
	graph_owner.call(&"on_graph_message", "Godot called")
	if graph_owner.get_meta(&"custom_payload", "") != "Godot called":
		return _fail("A Godot call passes the typed custom event payload.")
	await process_frame
	await process_frame
	if not graph_owner.get_meta(&"frame_delta", null) is float:
		return _fail("Godot dispatches Process and passes its float Delta pin.")
	root.remove_child(graph_owner)
	if not graph_owner.get_meta(&"exited", false):
		graph_owner.free()
		return _fail("Exit Tree runs on a graph owner leaving the scene.")
	slider.value = 45.0
	if graph_owner.get_meta(&"signal_value", -1.0) != 31.0:
		graph_owner.free()
		return _fail("Native signal subscriptions stop when the graph owner leaves the scene.")
	root.add_child(graph_owner)
	slider.value = 54.0
	if graph_owner.get_meta(&"signal_value", -1.0) != 54.0:
		return _fail("Native signals reconnect when an existing graph owner re-enters the tree without another Ready.")
	graph_owner.free()
	return true


## Stop an in-flight graph when a synchronous native callback removes its script.
func _verify_script_detaches_during_native_call() -> bool:
	const EVENT_ID := 51
	const EVENT_NAME := &"change_slider"
	const SLIDER_VALUE := 25.0
	var graph_data := _graph([
		_op(1, &"custom_event", {"definition": EVENT_ID}),
		_op(2, &"set_property", {
			"member": {"class": &"Range", "name": &"value", "inputs": [_pin(&"value", TYPE_FLOAT)]},
			"values": {&"value": SLIDER_VALUE},
		}),
		_write_meta(3, &"ran_after_detach", true),
	], [_flow(1, 2), _wire(1, &"slider", 2, &"target"), _flow(2, 3)], {
		"events": [{"id": EVENT_ID, "name": EVENT_NAME, "inputs": [_pin(&"slider", TYPE_OBJECT, &"Range")]}],
	})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create a graph with a synchronous native callback.")
	var graph_owner := Node.new()
	var emitter := HSlider.new()
	emitter.value_changed.connect(func(_new_value: float) -> void:
		graph_owner.set_script(null)
	)
	root.add_child(emitter)
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	graph_owner.call(EVENT_NAME, emitter)
	var detached := graph_owner.get_script() == null
	var detached_graph_continued := graph_owner.has_meta(&"ran_after_detach")
	var native_action_finished := is_equal_approx(emitter.value, SLIDER_VALUE)
	graph_owner.free()
	emitter.free()
	return (detached and native_action_finished and not detached_graph_continued) or _fail(
		"A native callback that removes the running script must stop its remaining actions.")


## Keep a native method's arguments available when its signal re-enters the same graph.
func _verify_native_method_reentry() -> bool:
	const EVENT_ID := 52
	const EVENT_NAME := &"attach_internal_child"
	var graph_data := _graph([
		_op(1, &"custom_event", {"definition": EVENT_ID}),
		_op(2, &"call_method", {
			"member": {"class": &"Node", "name": &"add_child", "inputs": [
				_pin(&"arg_0", TYPE_OBJECT, &"Node"),
				{"id": &"arg_1", "label": "Force Readable Name", "type": TYPE_BOOL, "default": false},
				_pin(&"arg_2", TYPE_INT),
			]},
		}),
		_op(3, &"native_signal", {"class": &"Node", "signal": &"child_entered_tree", "source": NodePath("."),
			"inputs": [_pin(&"arg_0", TYPE_OBJECT, &"Node")]}),
		_write_meta(4, &"entered_child"),
		_op(5, &"operator", {"operator": "equal", "pin": _pin(&"value", TYPE_INT),
			"values": {&"b": Node.INTERNAL_MODE_FRONT}}),
		_op(6, &"branch"),
		_write_meta(7, &"outer_child"),
	], [
		_flow(1, 2), _wire(1, &"child_node", 2, &"arg_0"), _wire(1, &"internal_mode", 2, &"arg_2"),
		_flow(3, 4), _wire(3, &"arg_0", 4, &"arg_1"),
		_flow(2, 6), _wire(1, &"internal_mode", 5, &"a"), _wire(5, &"result", 6, &"condition"),
		_flow(6, 7, &"true"), _wire(1, &"child_node", 7, &"arg_1"),
	], {"events": [{"id": EVENT_ID, "name": EVENT_NAME,
		"inputs": [_pin(&"child_node", TYPE_OBJECT, &"Node"), _pin(&"internal_mode", TYPE_INT)]}]})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create a graph whose native method synchronously re-enters it.")
	var graph_owner := Node.new()
	var added_child := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	graph_owner.call(EVENT_NAME, added_child, Node.INTERNAL_MODE_FRONT)
	var correct_arguments := (added_child.get_parent() == graph_owner and graph_owner.get_child_count() == 0
		and graph_owner.get_child_count(true) == 1)
	var correct_signal: bool = graph_owner.get_meta(&"entered_child", false) == added_child
	var correct_resume: bool = graph_owner.get_meta(&"outer_child", false) == added_child
	if added_child.get_parent() != graph_owner:
		added_child.free()
	graph_owner.free()
	return (correct_arguments and correct_signal and correct_resume) or _fail(
		"Native method re-entry must preserve the child argument and internal mode in the outer event.")


## Check a five-argument reflected call, its synchronous signal, and the shorter call that follows it.
func _verify_native_argument_spill() -> bool:
	const EVENT_ID := 53
	const EVENT_NAME := &"write_annotation"
	const START_LINE := 1
	const END_LINE := 2
	const INSERT_COLUMN := 2
	const ANNOTATION := " restored\nmarker"
	var graph_data := _graph([
		_op(1, &"custom_event", {"definition": EVENT_ID}),
		_op(2, &"call_method", {
			"member": {"class": &"TextEdit", "name": &"insert_text", "inputs": [
				_pin(&"arg_0", TYPE_STRING), _pin(&"arg_1", TYPE_INT), _pin(&"arg_2", TYPE_INT),
				{"id": &"arg_3", "label": "Before Selection Begin", "type": TYPE_BOOL, "default": true},
				{"id": &"arg_4", "label": "Before Selection End", "type": TYPE_BOOL, "default": false},
			]},
			"values": {&"arg_1": START_LINE, &"arg_2": INSERT_COLUMN, &"arg_4": true},
		}),
		_op(3, &"native_signal", {"class": &"TextEdit", "signal": &"lines_edited_from", "source": NodePath("."),
			"inputs": [_pin(&"arg_0", TYPE_INT), _pin(&"arg_1", TYPE_INT)]}),
		_write_meta(4, &"from_line"), _write_meta(5, &"to_line"),
		_op(6, &"call_method", {"member": {"class": &"TextEdit", "name": &"get_line",
			"inputs": [_pin(&"arg_0", TYPE_INT)], "output": _pin(&"result", TYPE_STRING)},
			"values": {&"arg_0": END_LINE}}),
		_write_meta(7, &"inserted_line"),
	], [
		_flow(1, 2), _wire(1, &"annotation", 2, &"arg_0"),
		_flow(3, 4), _wire(3, &"arg_0", 4, &"arg_1"), _flow(4, 5), _wire(3, &"arg_1", 5, &"arg_1"),
		_flow(2, 6), _flow(6, 7), _wire(6, &"result", 7, &"arg_1"),
	], {"events": [{"id": EVENT_ID, "name": EVENT_NAME, "inputs": [_pin(&"annotation", TYPE_STRING)]}]})
	var graph_script := _script_for(&"TextEdit", graph_data)
	if graph_script == null:
		return _fail("Create a graph with a reflected method that takes five arguments.")
	var graph_owner := TextEdit.new()
	graph_owner.text = "Notes\nOK."
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	graph_owner.call(EVENT_NAME, ANNOTATION)
	var correct_text := graph_owner.text == "Notes\nOK restored\nmarker."
	var correct_signal: bool = (graph_owner.get_meta(&"from_line", -1) == START_LINE
		and graph_owner.get_meta(&"to_line", -1) == END_LINE)
	var correct_resume: bool = graph_owner.get_meta(&"inserted_line", "") == "marker."
	graph_owner.free()
	return (correct_text and correct_signal and correct_resume) or _fail(
		"A five-argument call must preserve its insertion and signal values, then resume with correct shorter arguments.")


## Subscribe through a constructor pin before the new node enters the scene tree.
func _verify_wired_signals() -> bool:
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"create_node", {"class": &"HSlider"}), _op(3, &"add_child"),
		_op(4, &"native_signal", {"class": &"Node", "signal": &"ready", "inputs": [], "wired_target": true}),
		_write_meta(5, &"created_ready", true),
		_op(6, &"native_signal", {"class": &"Range", "signal": &"value_changed", "wired_target": true,
			"inputs": [_pin(&"arg_0", TYPE_FLOAT)]}),
		_write_meta(7, &"created_value"),
		_op(8, &"set_property", {"member": {"class": &"Range", "name": &"value", "inputs": [_pin(&"value", TYPE_FLOAT)]},
			"values": {&"value": 24.0}}),
		_op(9, &"set_property", {"member": {"class": &"Range", "name": &"step", "inputs": [_pin(&"value", TYPE_FLOAT)]},
			"values": {&"value": 2.0}}),
	], [
		_flow(1, 2), _flow(2, 3), _wire(2, &"node", 3, &"node"),
		_wire(2, &"node", 4, &"target"), _flow(4, 5),
		_wire(2, &"node", 6, &"target"), _flow(6, 7), _wire(6, &"arg_0", 7, &"arg_1"),
		_flow(3, 8), _wire(2, &"node", 8, &"target"),
		_flow(7, 9), _wire(6, &"sender", 9, &"target"),
	])
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create a graph with wired native signal sources.")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	if graph_owner.get_child_count() != 1 or not graph_owner.get_meta(&"created_ready", false):
		return _fail("The inherited Ready signal is already connected when Add Child enters the created node.")
	var slider := graph_owner.get_child(0) as HSlider
	if slider == null or not is_equal_approx(slider.step, 2.0) or graph_owner.get_meta(&"created_value", -1.0) != 24.0:
		return _fail("The signal supplies the typed float and its Sender can target the created object.")
	slider.value = 42.0
	if graph_owner.get_meta(&"created_value", -1.0) != 42.0:
		return _fail("The wired native signal remains connected after the constructor's event ends.")
	graph_owner.free()
	return true


## Stop both the active graph path and any other handlers captured for the same emission.
func _verify_detach_during_signal_delivery() -> bool:
	const TRIGGER_NAME := &"Trigger"
	const DETACH_NAME := &"Detach"
	const START_VALUE := 10.0
	const DETACH_VALUE := 20.0
	var signal_node := {"class": &"Range", "signal": &"value_changed", "source": NodePath("Trigger"),
		"inputs": [_pin(&"arg_0", TYPE_FLOAT)]}
	var graph_data := _graph([
		_op(1, &"native_signal", signal_node), _write_meta(2, &"started_first_handler", true),
		_op(3, &"native_signal", signal_node), _write_meta(4, &"started_second_handler", true),
		_op(5, &"scene_node", {"source": NodePath("Detach"), "class": &"HSlider"}),
		_op(6, &"set_property", {"member": {"class": &"Range", "name": &"value", "inputs": [_pin(&"value", TYPE_FLOAT)]},
			"values": {&"value": DETACH_VALUE}}),
		_write_meta(7, &"continued_after_detach", true),
	], [_flow(1, 2), _flow(3, 4), _flow(2, 6), _flow(4, 6), _wire(5, &"node", 6, &"target"), _flow(6, 7)])
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create two native graph handlers for the same emitter.")
	var graph_owner := Node.new()
	var trigger := HSlider.new()
	trigger.name = TRIGGER_NAME
	var detach_slider := HSlider.new()
	detach_slider.name = DETACH_NAME
	detach_slider.value_changed.connect(func(_new_value: float) -> void:
		graph_owner.set_script(null)
	)
	graph_owner.add_child(trigger)
	graph_owner.add_child(detach_slider)
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var had_two_handlers := trigger.value_changed.get_connections().size() == 2
	trigger.value = START_VALUE
	var started_once := graph_owner.has_meta(&"started_first_handler") != graph_owner.has_meta(&"started_second_handler")
	var detached := graph_owner.get_script() == null and is_equal_approx(detach_slider.value, DETACH_VALUE)
	var stopped := not graph_owner.has_meta(&"continued_after_detach") and trigger.value_changed.get_connections().is_empty()
	graph_owner.free()
	return (had_two_handlers and started_once and detached and stopped) or _fail(
		"Detaching inside a signal handler must stop its current path and skip the other pending handler.")


## Switching the emitter on a typed event pin replaces the existing subscription.
func _verify_signal_replacement() -> bool:
	var graph_data := _graph([
		_op(1, &"custom_event", {"definition": 1}),
		_op(2, &"native_signal", {"class": &"Range", "signal": &"value_changed", "wired_target": true,
			"inputs": [_pin(&"arg_0", TYPE_FLOAT)]}),
		_write_meta(3, &"current_value"),
	], [_wire(1, &"emitter", 2, &"target"), _flow(2, 3), _wire(2, &"arg_0", 3, &"arg_1")], {
		"events": [{"id": 1, "name": &"set_signal_source", "inputs": [_pin(&"emitter", TYPE_OBJECT, &"Range")]}],
	})
	var graph_script := _script_for(&"Node", graph_data)
	if graph_script == null:
		return _fail("Create a graph with a signal target supplied by a typed event argument.")
	var graph_owner := Node.new()
	var first_emitter := HSlider.new()
	var second_emitter := HSlider.new()
	graph_owner.add_child(first_emitter)
	graph_owner.add_child(second_emitter)
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	graph_owner.call(&"set_signal_source", first_emitter)
	first_emitter.value = 12.0
	if graph_owner.get_meta(&"current_value", -1.0) != 12.0:
		return _fail("An event argument can supply a wired native signal target.")
	graph_owner.call(&"set_signal_source", second_emitter)
	first_emitter.value = 24.0
	if graph_owner.get_meta(&"current_value", -1.0) != 12.0:
		return _fail("Changing a wired target removes the subscription from the original emitter.")
	second_emitter.value = 36.0
	if graph_owner.get_meta(&"current_value", -1.0) != 36.0:
		return _fail("Only the replacement emitter drives the native signal event.")
	graph_owner.free()
	return true


## A compatible native owner supplies Self when a signal's Target has no wire.
func _verify_self_signal() -> bool:
	var graph_data := _graph([
		_op(1, &"native_signal", {"class": &"Range", "signal": &"value_changed", "wired_target": true,
			"inputs": [_pin(&"arg_0", TYPE_FLOAT)]}),
		_write_meta(2, &"self_value"),
	], [_flow(1, 2), _wire(1, &"arg_0", 2, &"arg_1")])
	var graph_script := _script_for(&"HSlider", graph_data)
	if graph_script == null:
		return _fail("Create a graph with a native signal and an implicit Self target.")
	var slider := HSlider.new()
	slider.set_script(graph_script)
	root.add_child(slider)
	slider.value = 18.0
	if slider.get_meta(&"self_value", -1.0) != 18.0:
		return _fail("A compatible graph owner supplies the default native signal target.")
	slider.free()
	return true


## A receiver whose chosen emitter was disabled must not listen to the graph owner instead.
func _verify_disabled_signal_receiver() -> bool:
	var graph_data := _graph([
		_op(1, &"ready"), _op(2, &"create_node", {"class": &"HSlider", "disabled": true}),
		_op(3, &"native_signal", {"class": &"Range", "signal": &"value_changed", "wired_target": true,
			"inputs": [_pin(&"arg_0", TYPE_FLOAT)]}),
		_write_meta(4, &"emitted", true), _write_meta(5, &"continued", true),
	], [_flow(1, 2), _flow(2, 5), _wire(2, &"node", 3, &"target"), _flow(3, 4)])
	var graph_script := _script_for(&"HSlider", graph_data)
	if graph_script == null:
		return _fail("Create a graph with a disabled native signal emitter.")
	var slider := HSlider.new()
	slider.set_script(graph_script)
	root.add_child(slider)
	slider.value = 18.0
	var continued := slider.has_meta(&"continued")
	var emitted := slider.has_meta(&"emitted")
	slider.free()
	return (continued and not emitted) or _fail("A disabled emitter allows execution to continue without subscribing to Self.")
