# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree


## Run the graph and report failures without depending on the project scene.
func _initialize() -> void:
	_verify.call_deferred()


## Connect named data pins or the default execution path.
func _wire(source: int, output_pin: StringName, target: int, input_pin: StringName) -> Dictionary:
	return {"from": source, "from_pin": output_pin, "to": target, "to_pin": input_pin}


## Turn a graph result into a value observed directly from Godot.
func _observe(nodes: Array, wires: Array, source: int, pin: StringName, key: StringName, previous: int) -> int:
	var observer_id := nodes.size() + 1
	nodes.append({"id": observer_id, "kind": &"call_method", "member": {
		"class": &"Object", "name": &"set_meta", "inputs": [
			{"id": &"arg_0", "type": TYPE_STRING_NAME, "label": "Name"},
			{"id": &"arg_1", "type": TYPE_NIL, "label": "Value"},
		]}, "values": {&"arg_0": key}})
	wires.append(_wire(previous, &"then", observer_id, &"in"))
	wires.append(_wire(source, pin, observer_id, &"arg_1"))
	return observer_id


## Build one row that independently exercises Make defaults and Make/Break wiring.
func _exercise(sample: Variant, default_value: Variant, components: Array, derived: Array = []) -> bool:
	var value_kind := typeof(sample)
	var authored_values: Dictionary = {}
	for field: StringName in components:
		authored_values[field] = sample[field]
	var nodes: Array = [
		{"id": 1, "kind": &"ready"},
		{"id": 2, "kind": &"make_value", "value_type": value_kind, "values": authored_values},
		{"id": 3, "kind": &"break_value", "value_type": value_kind},
		{"id": 4, "kind": &"make_value", "value_type": value_kind},
	]
	var wires: Array = [_wire(2, &"value", 3, &"value")]
	var previous := _observe(nodes, wires, 2, &"value", &"custom_value", 1)
	previous = _observe(nodes, wires, 4, &"value", &"default_value", previous)
	for field: StringName in components + derived:
		previous = _observe(nodes, wires, 3, field, field, previous)
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		return false
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", {"version": 1, "nodes": nodes, "connections": wires})
	var owner_node := Node.new()
	owner_node.set_script(graph_script)
	root.add_child(owner_node)
	var result: Variant = owner_node.get_meta(&"custom_value", false)
	var stock_default: Variant = owner_node.get_meta(&"default_value", false)
	var passed: bool = typeof(result) == value_kind and result == sample and \
		typeof(stock_default) == value_kind and stock_default == default_value
	for field: StringName in components + derived:
		var actual_component: Variant = owner_node.get_meta(field, false)
		var expected_component: Variant = sample[field]
		passed = passed and typeof(actual_component) == typeof(expected_component) and actual_component == expected_component
	owner_node.free()
	if not passed:
		push_error("[Crimson] Make/Break fixture: unexpected source, default or components for %s." % type_string(value_kind))
	return passed


## Verify a Float component can receive an integer wire, including on subsequent evaluation.
func _exercise_wired_component() -> bool:
	var nodes: Array = [
		{"id": 1, "kind": &"ready"},
		{"id": 2, "kind": &"literal", "global_constant": &"KEY_ENTER"},
		{"id": 3, "kind": &"make_value", "value_type": TYPE_VECTOR3, "values": {&"y": -7.5}},
	]
	var wires: Array = [_wire(2, &"result", 3, &"x")]
	_observe(nodes, wires, 3, &"value", &"result", 1)
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		return false
	graph_script.call(&"set_graph", {"version": 1, "nodes": nodes, "connections": wires})
	var owner_node := Node.new()
	owner_node.set_script(graph_script)
	root.add_child(owner_node)
	var passed: bool = owner_node.get_meta(&"result", false) == Vector3(KEY_ENTER, -7.5, 0.0)
	owner_node.free()
	if not passed:
		push_error("[Crimson] Make/Break fixture: a compatible wire did not override its component default.")
	return passed


## Exercise malformed saved data in separate invocations so normal test output stays clean.
func _reject_invalid_node(unknown_type: bool) -> void:
	var type_id: Variant = TYPE_STRING if unknown_type else TYPE_VECTOR3
	var nodes: Array = [{"id": 1, "kind": &"ready"},
		{"id": 2, "kind": &"make_value", "value_type": type_id, "values": {&"x": "not a number"}}]
	var wires: Array = []
	_observe(nodes, wires, 2, &"value", &"unexpected", 1)
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		quit(1)
		return
	graph_script.call(&"set_graph", {"version": 1, "nodes": nodes, "connections": wires})
	var owner_node := Node.new()
	owner_node.set_script(graph_script)
	root.add_child(owner_node)
	var passed := not owner_node.has_meta(&"unexpected")
	owner_node.free()
	if passed:
		print("CRIMSON_VISUAL_SCRIPT_VALUE_NODES_REJECTION_PASSED")
	quit(0 if passed else 1)


## Check the native values and component shapes handled by the picker.
func _verify() -> void:
	if "--invalid-type" in OS.get_cmdline_user_args():
		_reject_invalid_node(true)
		return
	if "--wrong-field" in OS.get_cmdline_user_args():
		_reject_invalid_node(false)
		return
	var basis_value := Basis(Vector3(1, 2, 3), Vector3(-4, 5, 6), Vector3(7, 8, 9))
	var fixtures: Array = [
		[Vector2(1.25, -8.5), Vector2(), [&"x", &"y"]],
		[Vector2i(-2147483648, 2147483647), Vector2i(), [&"x", &"y"]],
		[Vector3(-1.5, 2.25, 3.5), Vector3(), [&"x", &"y", &"z"]],
		[Vector3i(123, -456, 789), Vector3i(), [&"x", &"y", &"z"]],
		[Vector4(1.5, 2.25, -3.75, 4.5), Vector4(), [&"x", &"y", &"z", &"w"]],
		[Vector4i(1, -2, 3, -4), Vector4i(), [&"x", &"y", &"z", &"w"]],
		[Rect2(Vector2(4.5, -6.25), Vector2(3.5, 9.0)), Rect2(), [&"position", &"size"], [&"end"]],
		[Rect2i(Vector2i(8, -4), Vector2i(5, 16)), Rect2i(), [&"position", &"size"], [&"end"]],
		[AABB(Vector3(4, -6, 8), Vector3(12, 6, 4)), AABB(), [&"position", &"size"], [&"end"]],
		[Plane(Vector3(3, 2, 1), -7.5), Plane(), [&"normal", &"d"], [&"x", &"y", &"z"]],
		[Quaternion(0.25, -0.5, 0.75, 0.125), Quaternion(), [&"x", &"y", &"z", &"w"]],
		[Color(0.125, 0.375, 0.625, 0.875), Color(), [&"r", &"g", &"b", &"a"]],
		[Transform2D(Vector2(2, 1), Vector2(-2, 3), Vector2(14, -6)), Transform2D(), [&"x", &"y", &"origin"]],
		[basis_value, Basis(), [&"x", &"y", &"z"]],
		[Transform3D(basis_value, Vector3(3, 9, -1)), Transform3D(), [&"basis", &"origin"]],
		[Projection(Vector4(1, 2, 3, 4), Vector4(5, 6, 7, 8), Vector4(9, 10, 11, 12), Vector4(13, 14, 15, 16)),
			Projection(), [&"x", &"y", &"z", &"w"]],
	]
	for fixture: Array in fixtures:
		if not _exercise(fixture[0], fixture[1], fixture[2], fixture[3] if fixture.size() > 3 else []):
			quit(1)
			return
	if not _exercise_wired_component():
		quit(1)
		return
	print("CRIMSON_VISUAL_SCRIPT_VALUE_NODES_PASSED")
	quit()
