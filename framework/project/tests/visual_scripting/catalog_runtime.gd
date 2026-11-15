# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

## Compile every saved built-in and utility signature with the stock engine.
## Pass the pinned Godot 4.7 extension_api JSON path after --.


## Wait until the fixture can attach a runtime node and receive its Ready event.
func _initialize() -> void:
	_verify.call_deferred()


## Convert the catalog's type name into the same descriptor used by graph pins.
func _pin(pin_name: StringName, type_name: String) -> Dictionary:
	var tag: int = TYPE_NIL
	if type_name != "Variant":
		for type_index: int in range(TYPE_MAX):
			if type_string(type_index) == type_name:
				tag = type_index
				break
		if tag == TYPE_NIL:
			return {}
	var descriptor := {"id": pin_name, "label": String(pin_name), "type": tag}
	if tag == TYPE_OBJECT:
		descriptor["class"] = &"Object"
	return descriptor


## Build a method descriptor from its authoritative metadata without calling the method.
func _method_node(method_id: int, owner_name: String, signature: Dictionary) -> Dictionary:
	var pins: Array[Dictionary] = []
	for parameter: Dictionary in signature.get("arguments", []):
		var input_pin := _pin(StringName("arg_%d" % pins.size()), parameter.type)
		if input_pin.is_empty():
			return {}
		if parameter.has("default_value"):
			input_pin["default"] = str_to_var(parameter.default_value)
		pins.append(input_pin)
	var is_utility := owner_name == "@GlobalScope"
	var member_data := {"class": StringName(owner_name), "name": StringName(signature.name), "inputs": pins}
	if is_utility:
		member_data["utility"] = true
		member_data["static"] = true
	else:
		var receiver := _pin(&"target", owner_name)
		if receiver.is_empty():
			return {}
		member_data["variant_type"] = receiver.type
	for flag_name: String in ["const", "static", "vararg"]:
		if signature.get("is_" + flag_name, false):
			member_data[flag_name] = true
	if signature.has("return_type"):
		var output_pin := _pin(&"result", signature.return_type)
		if output_pin.is_empty():
			return {}
		member_data["output"] = output_pin
	return {"id": method_id, "kind": &"call_method", "member": member_data}


## Attach all signatures together so a rejected node prevents the Ready sentinel.
func _verify() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or not FileAccess.file_exists(arguments[0]):
		_fail("Pass the path of the pinned Godot 4.7 extension API JSON after --.")
		return
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(arguments[0]))
	if not data is Dictionary or not data.has("builtin_classes") or not data.has("utility_functions"):
		_fail("Load the Godot extension API's built-in and utility signatures.")
		return
	var mark_ready := {"id": 2, "kind": &"call_method", "member": {"class": &"Object", "name": &"set_meta", "inputs": [
		_pin(&"arg_0", "StringName"), _pin(&"arg_1", "Variant"),
	]}, "values": {&"arg_0": &"verified_catalog", &"arg_1": true}}
	var nodes: Array[Dictionary] = [{"id": 1, "kind": &"ready"}, mark_ready]
	var families: Array = data.builtin_classes.duplicate()
	families.append({"name": "@GlobalScope", "methods": data.utility_functions})
	var builtin_total := 0
	var utility_total := 0
	for family: Dictionary in families:
		for method: Dictionary in family.get("methods", []):
			var node_record := _method_node(nodes.size() + 1, family.name, method)
			if node_record.is_empty():
				_fail("Convert Godot metadata for %s.%s." % [family.name, method.name])
				return
			nodes.append(node_record)
			if family.name == "@GlobalScope":
				utility_total += 1
			else:
				builtin_total += 1
	if builtin_total == 0 or utility_total == 0:
		_fail("The API catalog must contain both built-in and utility signatures.")
		return
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		_fail("Instantiate the Visual Script class in the copied addon.")
		return
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", {"version": 1, "nodes": nodes, "connections": [
		{"from": 1, "from_pin": &"then", "to": 2, "to_pin": &"in"},
	]})
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	var compiled: bool = graph_owner.get_meta(&"verified_catalog", false)
	graph_owner.free()
	if not compiled:
		_fail("Compile all descriptors before executing the Ready sentinel.")
		return
	print("CRIMSON_GODOT_SIGNATURE_CATALOG_PASSED %d built-in, %d utility" % [builtin_total, utility_total])
	quit()


## Report an actionable failure to the calling test process.
func _fail(requirement: String) -> void:
	push_error("[Crimson] Godot signature catalog fixture: " + requirement)
	quit(1)
