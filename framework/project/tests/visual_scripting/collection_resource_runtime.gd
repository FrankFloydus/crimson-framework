# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const PRELOAD_GRAPH := "res://tests/visual_scripting/preload_resource_fixture.crimsonscript"
const PRELOAD_ASSET := "res://tests/visual_scripting/preload_resource_asset.tres"
const SAVED_GRAPH := "user://collection_matrix.crimsonscript"
const MISSING_ASSET := "res://tests/visual_scripting/preload_resource_missing.tres"
const EXPECTED_COLLECTION_CASES := 35
const RESOURCE_DIRECTORY := "res://resource_matrix_generated"
const RESOURCE_GRAPH := RESOURCE_DIRECTORY + "/references.crimsonscript"
const GDSCRIPT_REFERENCE := RESOURCE_DIRECTORY + "/references.gd"
const TEXT_SCENE := RESOURCE_DIRECTORY + "/text_scene.tscn"
const BINARY_SCENE := RESOURCE_DIRECTORY + "/binary_scene.scn"
const SCRIPT_ASSET := RESOURCE_DIRECTORY + "/script_asset.gd"
const GRAPH_ASSET := RESOURCE_DIRECTORY + "/script_asset.crimsonscript"
const CONSTRUCTOR_ASSET := RESOURCE_DIRECTORY + "/constructor_asset.gd"
const MATERIAL_ASSET := RESOURCE_DIRECTORY + "/material.tres"
const BARE_AUDIO_ASSET := RESOURCE_DIRECTORY + "/bare_audio.tres"
const USER_RESOURCE := "user://resource_matrix_material.tres"
const TEXT_ASSET := RESOURCE_DIRECTORY + "/notes.txt"
const IMPORTED_AUDIO := "res://tests/visual_scripting/ready_chime.wav"
const SCENE_MARKER := &"resource_matrix_marker"
const SCRIPT_MARKER := 117
const CONSTRUCTOR_DEFAULT := 23
const CONSTRUCTOR_OVERRIDE := 41


## Run after the root Window has joined the scene tree.
func _initialize() -> void:
	_verify.call_deferred()


## Record a type for a function, boundary or reflected pin.
func _pin(pin_id: StringName, value_type: int, native_class: StringName = &"") -> Dictionary:
	var descriptor := {"id": pin_id, "label": String(pin_id), "type": value_type}
	if not native_class.is_empty():
		descriptor["class"] = native_class
	return descriptor


## Describe a connection between stable authored pins.
func _wire(start_id: int, start_pin: StringName, end_id: int, end_pin: StringName) -> Dictionary:
	return {"from": start_id, "from_pin": start_pin, "to": end_id, "to_pin": end_pin}


## Write a case failure to the isolated harness log.
func _fail(requirement: String) -> bool:
	push_error("[Crimson] Collection and resource matrix: " + requirement)
	quit(1)
	return false


## Build one callable collection operation with both outputs exposed by the graph.
func _definition(action: StringName, is_dictionary: bool, action_inputs: Array[Dictionary], graph_outputs: Array[Dictionary],
		body_options: Dictionary = {}) -> Dictionary:
	var node_record := {"id": 2, "kind": action}
	node_record.merge(body_options)
	var is_edit := action in [&"set_array_item", &"append_array_item", &"remove_array_item", &"set_dictionary_value", &"erase_dictionary_key"]
	var wires: Array[Dictionary] = []
	if is_edit:
		wires.append(_wire(1, &"then", 2, &"in"))
		wires.append(_wire(2, &"then", 3, &"in"))
	else:
		wires.append(_wire(1, &"then", 3, &"in"))
	for descriptor: Dictionary in action_inputs:
		wires.append(_wire(1, descriptor.id, 2, descriptor.id))
	for descriptor: Dictionary in graph_outputs:
		wires.append(_wire(2, descriptor.id, 3, descriptor.id))
	if not action.begins_with("make_") and action_inputs[0].id != (&"dictionary" if is_dictionary else &"array"):
		return {}
	return {"name": action, "id": 0, "inputs": action_inputs, "outputs": graph_outputs, "graph": {"nodes": [
		{"id": 1, "kind": &"function_entry"}, node_record, {"id": 3, "kind": &"return"}], "connections": wires}}


## Author a detached graph resource through the same loader used by exported games.
func _new_script(definitions: Array[Dictionary]) -> Script:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		return null
	for entry_number: int in range(definitions.size()):
		definitions[entry_number].id = entry_number + 1
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": [], "functions": definitions})
	return graph_script


## Check literal and wired constructors, reads and value edits against the stock runtime.
func _verify_collections() -> bool:
	var array_pin := _pin(&"array", TYPE_ARRAY)
	var dictionary_pin := _pin(&"dictionary", TYPE_DICTIONARY)
	var index_pin := _pin(&"index", TYPE_INT)
	var key_pin := _pin(&"key", TYPE_NIL)
	var value_pin := _pin(&"value", TYPE_NIL)
	var lookup_pins: Array[Dictionary] = [value_pin, _pin(&"found", TYPE_BOOL)]
	var array_edit_pins: Array[Dictionary] = [_pin(&"result", TYPE_ARRAY), _pin(&"succeeded", TYPE_BOOL)]
	var dictionary_edit_pins: Array[Dictionary] = [_pin(&"result", TYPE_DICTIONARY), _pin(&"succeeded", TYPE_BOOL)]
	var definitions: Array[Dictionary] = [
		_definition(&"make_array", false, [_pin(&"item_1", TYPE_NIL), _pin(&"item_2", TYPE_NIL)], [_pin(&"value", TYPE_ARRAY)], {"collection_size": 3}),
		_definition(&"make_dictionary", true, [_pin(&"key_1", TYPE_NIL), _pin(&"value_1", TYPE_NIL), _pin(&"key_2", TYPE_NIL),
			_pin(&"value_2", TYPE_NIL)], [_pin(&"value", TYPE_DICTIONARY)], {"collection_size": 3}),
		_definition(&"get_array_item", false, [array_pin, index_pin], lookup_pins),
		_definition(&"get_dictionary_value", true, [dictionary_pin, key_pin], lookup_pins),
		_definition(&"set_array_item", false, [array_pin, index_pin, value_pin], array_edit_pins),
		_definition(&"append_array_item", false, [array_pin, value_pin], array_edit_pins),
		_definition(&"remove_array_item", false, [array_pin, index_pin], array_edit_pins),
		_definition(&"set_dictionary_value", true, [dictionary_pin, key_pin, value_pin], dictionary_edit_pins),
		_definition(&"erase_dictionary_key", true, [dictionary_pin, key_pin], dictionary_edit_pins),
	]
	var empty_array := _definition(&"make_array", false, [], [_pin(&"value", TYPE_ARRAY)], {"collection_size": 0})
	empty_array.name = &"make_empty_array"
	definitions.append(empty_array)
	var empty_dictionary := _definition(&"make_dictionary", true, [], [_pin(&"value", TYPE_DICTIONARY)], {"collection_size": 0})
	empty_dictionary.name = &"make_empty_dictionary"
	definitions.append(empty_dictionary)
	var literal_array := _definition(&"make_array", false, [], [_pin(&"value", TYPE_ARRAY)], {"collection_size": 2,
		"values": {&"item_1": 11, &"item_2": "a"}})
	literal_array.name = &"make_literal_array"
	definitions.append(literal_array)
	var typed_constructor := _definition(&"make_dictionary", true, [_pin(&"key_1", TYPE_STRING), _pin(&"value_1", TYPE_INT)],
		[_pin(&"value", TYPE_DICTIONARY)], {"collection_size": 3, "dictionary_types": {
			"key": {"type": TYPE_STRING}, "value": {"type": TYPE_INT},
		}, "values": {&"key_2": "incomplete", &"value_3": 9}})
	typed_constructor.name = &"make_typed_dictionary"
	definitions.append(typed_constructor)
	var partial_constructor := _definition(&"make_dictionary", true, [], [_pin(&"value", TYPE_DICTIONARY)],
		{"collection_size": 3, "dictionary_types": {"key": {"type": TYPE_STRING}, "value": {"type": TYPE_NIL}},
		"values": {&"key_1": "intentionally null", &"key_2": "mixed", &"value_2": true}})
	partial_constructor.name = &"make_partly_typed_dictionary"
	definitions.append(partial_constructor)
	var typed_input_pin := array_pin.duplicate()
	typed_input_pin.element = _pin(&"element", TYPE_INT)
	var typed_array_edit := _definition(&"append_array_item", false, [typed_input_pin, value_pin],
		[_pin(&"result", TYPE_ARRAY).merged({"element": _pin(&"element", TYPE_INT)}), _pin(&"succeeded", TYPE_BOOL)])
	typed_array_edit.name = &"append_twice"
	typed_array_edit.graph.nodes.append({"id": 4, "kind": &"append_array_item"})
	typed_array_edit.graph.connections = [
		_wire(1, &"then", 2, &"in"), _wire(2, &"then", 4, &"in"), _wire(4, &"then", 3, &"in"),
		_wire(1, &"array", 2, &"array"), _wire(1, &"value", 2, &"value"), _wire(2, &"result", 4, &"array"),
		_wire(1, &"value", 4, &"value"), _wire(4, &"result", 3, &"result"), _wire(4, &"succeeded", 3, &"succeeded"),
	]
	definitions.append(typed_array_edit)
	var graph_script := _new_script(definitions)
	if graph_script == null or ResourceSaver.save(graph_script, SAVED_GRAPH) != OK:
		return _fail("Could not save the collection graph")
	var resumed_script := ResourceLoader.load(SAVED_GRAPH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if resumed_script == null:
		return _fail("Could not reload the edited collection graph")
	var graph_owner := Node.new()
	graph_owner.set_script(resumed_script)
	root.add_child(graph_owner)
	var typed_array: Array[int] = [3, 7]
	var typed_dictionary: Dictionary[String, int] = {"a": 4}
	var read_only_array: Array = [9, 10]
	read_only_array.make_read_only()
	var read_only_dictionary: Dictionary = {"c": 3}
	read_only_dictionary.make_read_only()
	var cases: Array[Array] = [
		[&"make_empty_array", [], []],
		[&"make_empty_dictionary", [], {}],
		[&"make_literal_array", [], [11, "a"]],
		[&"make_array", [42, "mixed"], [42, "mixed", null]],
		[&"make_array", [false, [1]], [false, [1], null]],
		[&"make_dictionary", ["same", 1, "same", 2], {"same": 2}],
		[&"make_dictionary", ["name", null, 35, true], {"name": null, 35: true}],
		[&"make_dictionary", [[1, 2], 3, "valid", 4], {"valid": 4}],
		[&"make_typed_dictionary", ["typed", 5], {"typed": 5}],
		[&"make_partly_typed_dictionary", [], {"intentionally null": null, "mixed": true}],
		[&"get_array_item", [["a", "b"], 1], {&"value": "b", &"found": true}],
		[&"get_array_item", [[null], 0], {&"value": null, &"found": true}],
		[&"get_array_item", [[], 0], {&"value": null, &"found": false}],
		[&"get_array_item", [[42], -1], {&"value": null, &"found": false}],
		[&"get_array_item", [[42], 9223372036854775807], {&"value": null, &"found": false}],
		[&"get_dictionary_value", [{"present": null}, "present"], {&"value": null, &"found": true}],
		[&"get_dictionary_value", [{}, "missing"], {&"value": null, &"found": false}],
		[&"get_dictionary_value", [typed_dictionary, 4], {&"value": null, &"found": false}],
		[&"get_dictionary_value", [typed_dictionary, [1]], {&"value": null, &"found": false}],
		[&"set_array_item", [typed_array, 0, 18], {&"result": [18, 7], &"succeeded": true}],
		[&"set_array_item", [typed_array, 0, "wrong"], {&"result": [3, 7], &"succeeded": false}],
		[&"set_array_item", [typed_array, 2, 18], {&"result": [3, 7], &"succeeded": false}],
		[&"set_array_item", [read_only_array, 1, 16], {&"result": [9, 16], &"succeeded": true}],
		[&"append_array_item", [typed_array, 14], {&"result": [3, 7, 14], &"succeeded": true}],
		[&"append_array_item", [typed_array, false], {&"result": [3, 7], &"succeeded": false}],
		[&"append_array_item", [[], null], {&"result": [null], &"succeeded": true}],
		[&"remove_array_item", [typed_array, 0], {&"result": [7], &"succeeded": true}],
		[&"remove_array_item", [typed_array, -1], {&"result": [3, 7], &"succeeded": false}],
		[&"set_dictionary_value", [typed_dictionary, "a", 9], {&"result": {"a": 9}, &"succeeded": true}],
		[&"set_dictionary_value", [typed_dictionary, 4, 9], {&"result": {"a": 4}, &"succeeded": false}],
		[&"set_dictionary_value", [typed_dictionary, "b", "wrong"], {&"result": {"a": 4}, &"succeeded": false}],
		[&"set_dictionary_value", [read_only_dictionary, "d", 4], {&"result": {"c": 3, "d": 4}, &"succeeded": true}],
		[&"erase_dictionary_key", [typed_dictionary, "a"], {&"result": {}, &"succeeded": true}],
		[&"erase_dictionary_key", [typed_dictionary, "missing"], {&"result": {"a": 4}, &"succeeded": false}],
		[&"append_twice", [typed_array, 8], {&"result": [3, 7, 8, 8], &"succeeded": true}],
	]
	if cases.size() != EXPECTED_COLLECTION_CASES:
		graph_owner.free()
		return _fail("The recorded collection matrix count must match the exercised cases")
	for case_id: int in range(cases.size()):
		var case_data: Array = cases[case_id]
		var actual: Variant = graph_owner.callv(case_data[0], case_data[1])
		if actual != case_data[2]:
			graph_owner.free()
			return _fail("Case %d (%s) returned %s, expected %s" % [case_id + 1, case_data[0], actual, case_data[2]])
	var array_copy: Array = graph_owner.call(&"append_array_item", typed_array, 99).result
	var dictionary_copy: Dictionary = graph_owner.call(&"set_dictionary_value", typed_dictionary, "b", 8).result
	var constructed: Dictionary = graph_owner.call(&"make_typed_dictionary", "first", 13)
	var constructed_again: Dictionary = graph_owner.call(&"make_typed_dictionary", "second", 17)
	var partially_constructed: Dictionary = graph_owner.call(&"make_partly_typed_dictionary")
	graph_owner.free()
	if not array_copy.is_typed() or array_copy.get_typed_builtin() != TYPE_INT or not dictionary_copy.is_typed() or \
			dictionary_copy.get_typed_key_builtin() != TYPE_STRING or dictionary_copy.get_typed_value_builtin() != TYPE_INT:
		return _fail("Collection edits must preserve native typed metadata")
	if typed_array != [3, 7] or typed_dictionary != {"a": 4} or read_only_array != [9, 10] or read_only_dictionary != {"c": 3}:
		return _fail("Editing a collection must not modify the original shared container")
	if constructed.get_typed_key_builtin() != TYPE_STRING or constructed.get_typed_value_builtin() != TYPE_INT or \
			partially_constructed.get_typed_key_builtin() != TYPE_STRING or partially_constructed.is_typed_value():
		return _fail("Make Dictionary must publish its independently typed native lanes")
	if constructed != {"first": 13} or constructed_again != {"second": 17}:
		return _fail("Each invocation must start with an empty typed dictionary")
	return true


## Reject a wire that would require converting a typed dictionary entry.
func _verify_constructor_type_failure() -> bool:
	var invalid_constructor := _definition(&"make_dictionary", true, [_pin(&"key_1", TYPE_STRING), _pin(&"value_1", TYPE_INT)],
		[_pin(&"value", TYPE_DICTIONARY)], {"dictionary_types": {"key": {"type": TYPE_STRING}, "value": {"type": TYPE_FLOAT}}})
	var bad_script := _new_script([invalid_constructor])
	if bad_script == null:
		return _fail("Could not create the incompatible dictionary graph")
	var graph_owner := Node.new()
	graph_owner.set_script(bad_script)
	root.add_child(graph_owner)
	var invalid_result: Variant = graph_owner.call(&"make_dictionary", "key", 4)
	graph_owner.free()
	if invalid_result != null:
		return _fail("A mismatched typed Make Dictionary wire must stop compilation")
	return true


## Verify the fixture's resource has no explicit export entry and is retained before Ready runs.
func _verify_preload() -> bool:
	var graph_script := ResourceLoader.load(PRELOAD_GRAPH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if graph_script == null:
		return _fail("Could not load the typed Preload Resource graph")
	var dependencies := ResourceLoader.get_dependencies(PRELOAD_GRAPH)
	var declared := false
	for dependency: String in dependencies:
		declared = declared or dependency.contains(PRELOAD_ASSET)
	if not declared:
		return _fail("Preload Resource must declare its asset dependency")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	if not ResourceLoader.has_cached(PRELOAD_ASSET):
		graph_owner.free()
		return _fail("Preload Resource must retain the resource before executing Ready")
	root.add_child(graph_owner)
	var asset := graph_owner.get_meta(&"preloaded", null) as Gradient
	graph_owner.free()
	if asset == null or asset.get_point_count() != 3 or not is_equal_approx(asset.get_offset(1), 0.25):
		return _fail("The typed Preload Resource did not reach the graph with its expected content")
	return true


## Exercise stale path and replacement-type diagnostics without executing a partly valid graph.
func _verify_preload_failures() -> bool:
	var valid_script := ResourceLoader.load(PRELOAD_GRAPH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if valid_script == null:
		return _fail("Could not load the Preload Resource failure fixture")
	var matrix: Array[Dictionary] = [
		{"class": &"Texture2D", "path": PRELOAD_ASSET, "graph": "user://preload_resource_wrong_type.crimsonscript"},
		{"class": &"Gradient", "path": MISSING_ASSET, "graph": "user://preload_resource_missing.crimsonscript"},
	]
	for fixture_case: Dictionary in matrix:
		var graph_data: Dictionary = valid_script.call(&"get_graph")
		graph_data.nodes[2]["asset_class"] = fixture_case["class"]
		graph_data.nodes[2]["values"][&"asset"]["path"] = fixture_case["path"]
		var graph_script := _new_script([])
		if graph_script == null:
			return _fail("Could not create a failing Preload Resource graph")
		graph_script.call(&"set_graph", graph_data)
		if ResourceSaver.save(graph_script, fixture_case["graph"]) != OK:
			return _fail("Could not save a failing Preload Resource graph")
		var reloaded := ResourceLoader.load(fixture_case["graph"], "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
		if reloaded == null:
			return _fail("Could not re-open a failing Preload Resource graph")
		var graph_owner := Node.new()
		graph_owner.set_script(reloaded)
		root.add_child(graph_owner)
		var did_run := graph_owner.has_meta(&"preloaded")
		graph_owner.free()
		if did_run:
			return _fail("A stale or wrongly typed Preload Resource must stop graph preparation")
	return true


## Generate both scene encodings and resources without using scenes from the demo project.
func _write_resource_fixtures() -> bool:
	if DirAccess.make_dir_recursive_absolute(RESOURCE_DIRECTORY) != OK:
		return _fail("Could not create the resource test directory")
	var scene_root := Node3D.new()
	scene_root.name = &"ResourceMatrixScene"
	scene_root.set_meta(SCENE_MARKER, true)
	var packed_scene := PackedScene.new()
	var packed_result := packed_scene.pack(scene_root)
	scene_root.free()
	if packed_result != OK or ResourceSaver.save(packed_scene, TEXT_SCENE) != OK or ResourceSaver.save(packed_scene, BINARY_SCENE) != OK:
		return _fail("Could not save the two PackedScene encodings")
	var material := StandardMaterial3D.new()
	material.albedo_color = Color.REBECCA_PURPLE
	if ResourceSaver.save(material, MATERIAL_ASSET) != OK or ResourceSaver.save(material, USER_RESOURCE) != OK:
		return _fail("Could not save the material")
	var bare_audio := ClassDB.instantiate(&"AudioStream") as AudioStream
	if bare_audio == null or ResourceSaver.save(bare_audio, BARE_AUDIO_ASSET) != OK:
		return _fail("Could not save the bare AudioStream resource")
	var script_source := FileAccess.open(SCRIPT_ASSET, FileAccess.WRITE)
	if script_source == null:
		return _fail("Could not write the scripted resource")
	script_source.store_string("extends Node\nconst MARKER: int = %d\n" % SCRIPT_MARKER)
	script_source.close()
	var graph_asset := _new_script([])
	if graph_asset == null or ResourceSaver.save(graph_asset, GRAPH_ASSET) != OK:
		return _fail("Could not save the external Visual Script resource")
	var constructor_source := FileAccess.open(CONSTRUCTOR_ASSET, FileAccess.WRITE)
	if constructor_source == null:
		return _fail("Could not write the constructor fixture")
	constructor_source.store_string("extends Node3D\n\n" +
		"static var last_created_id: int\nvar _value: int\n\n" +
		"func _init(initial_value: int, marker_value: int = %d) -> void:\n" % CONSTRUCTOR_DEFAULT +
		"\t_value = initial_value + marker_value\n\tlast_created_id = get_instance_id()\n\n" +
		"func read_value() -> int:\n\treturn _value\n\n" +
		"static func new() -> String:\n\treturn \"shadowed\"\n")
	constructor_source.close()
	var plain_text := FileAccess.open(TEXT_ASSET, FileAccess.WRITE)
	if plain_text == null:
		return _fail("Could not write the non-resource control file")
	plain_text.store_string("Plain text is outside ResourceLoader.")
	plain_text.close()
	return true


## Expose one authored preload through a typed function result.
func _preload_definition(method_name: StringName, asset_path: String, native_class: StringName, asset_uid: String = "") -> Dictionary:
	var preload_record := {"id": 2, "kind": &"preload_resource", "class": &"Resource",
		"asset_class": native_class, "values": {&"asset": {"path": asset_path, "uid": asset_uid}}}
	return {"id": 0, "name": method_name, "outputs": [_pin(&"resource", TYPE_OBJECT, native_class)],
		"graph": {"nodes": [{"id": 1, "kind": &"function_entry"}, preload_record, {"id": 3, "kind": &"return"}],
			"connections": [_wire(1, &"then", 3, &"in"), _wire(2, &"value", 3, &"resource")]}}


## List the saved resources authored by the fixture's typed graph.
func _preload_cases() -> Array[Dictionary]:
	return [
		{"method": &"text_scene", "path": TEXT_SCENE, "class": &"PackedScene"},
		{"method": &"binary_scene", "path": BINARY_SCENE, "class": &"PackedScene"},
		{"method": &"script_asset", "path": SCRIPT_ASSET, "class": &"GDScript"},
		{"method": &"script_graph", "path": GRAPH_ASSET, "class": &"VisualScript"},
		{"method": &"material_asset", "path": MATERIAL_ASSET, "class": &"StandardMaterial3D"},
		{"method": &"bare_audio", "path": BARE_AUDIO_ASSET, "class": &"AudioStream"},
		{"method": &"imported_audio", "path": IMPORTED_AUDIO, "class": &"AudioStreamWAV"},
	]


## Compare the exact resources produced by a saved Crimson graph and GDScript preload.
func _verify_preload_parity() -> bool:
	var asset_cases := _preload_cases()
	var definitions: Array[Dictionary] = []
	var reference_source := "extends Node\n"
	for fixture_case: Dictionary in asset_cases:
		definitions.append(_preload_definition(fixture_case.method, fixture_case.path, fixture_case.class))
		var reference_path: String = fixture_case.path.trim_prefix(RESOURCE_DIRECTORY + "/")
		reference_source += "const %s = preload(\"%s\")\n" % [String(fixture_case.method).to_upper(), reference_path]
	definitions.append(_constructor_definition(&"spawn_script_default", true))
	definitions.append(_constructor_definition(&"spawn_script_override", true, CONSTRUCTOR_OVERRIDE))
	definitions.append(_constructor_definition(&"spawn_script_detached", false))
	var generated_script := _new_script(definitions)
	if generated_script == null or ResourceSaver.save(generated_script, RESOURCE_GRAPH) != OK:
		return _fail("Could not save the typed preloads")
	var saved_script := ResourceLoader.load(RESOURCE_GRAPH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if saved_script == null:
		return _fail("Could not reload the typed preloads")
	var dependencies := ResourceLoader.get_dependencies(RESOURCE_GRAPH)
	var graph_owner := Node.new()
	graph_owner.set_script(saved_script)
	for fixture_case: Dictionary in asset_cases:
		var declared := false
		for dependency: String in dependencies:
			declared = declared or dependency.contains(fixture_case.path)
		if not declared or not ResourceLoader.has_cached(fixture_case.path):
			graph_owner.free()
			return _fail("Preload must declare and hold %s before any function runs" % fixture_case.path)
	root.add_child(graph_owner)
	var reference_file := FileAccess.open(GDSCRIPT_REFERENCE, FileAccess.WRITE)
	if reference_file == null:
		graph_owner.free()
		return _fail("Could not save the GDScript parity probe")
	reference_file.store_string(reference_source)
	reference_file.close()
	var reference_script := ResourceLoader.load(GDSCRIPT_REFERENCE, "GDScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if reference_script == null or not reference_script.can_instantiate():
		graph_owner.free()
		return _fail("GDScript could not prepare the same preload paths")
	var reference_values := reference_script.get_script_constant_map()
	for fixture_case: Dictionary in asset_cases:
		var actual := graph_owner.call(fixture_case.method) as Resource
		var expected := reference_values.get(String(fixture_case.method).to_upper()) as Resource
		if actual == null or actual != expected or not actual.is_class(fixture_case.class):
			graph_owner.free()
			return _fail("Crimson and GDScript did not preload the same typed resource for %s" % fixture_case.path)
	if not _verify_script_construction(graph_owner):
		graph_owner.free()
		return false
	graph_owner.free()
	var packed_uid := ResourceUID.create_id()
	ResourceUID.add_id(packed_uid, TEXT_SCENE)
	var uid_graph := _new_script([_preload_definition(&"scene_from_uid", MISSING_ASSET, &"PackedScene", ResourceUID.id_to_text(packed_uid))])
	var uid_owner := Node.new()
	uid_owner.set_script(uid_graph)
	root.add_child(uid_owner)
	var scene_from_uid := uid_owner.call(&"scene_from_uid") as PackedScene
	uid_owner.free()
	ResourceUID.remove_id(packed_uid)
	if not _verify_packed_scene_content(scene_from_uid):
		return _fail("Preload must resolve the UID when its stored path is stale")
	return true


## Verify scene content with a real node produced by PackedScene.instantiate.
func _verify_packed_scene_content(scene_resource: PackedScene) -> bool:
	if scene_resource == null:
		return false
	var scene_instance := scene_resource.instantiate()
	if scene_instance == null:
		return false
	var scene_content_ok: bool = scene_instance.get_meta(SCENE_MARKER, false) == true
	scene_instance.free()
	return scene_content_ok


## Read the generated graph from a pack selecting only the graph, so its assets must be discovered.
func _verify_selective_preloads() -> bool:
	var generated_script := ResourceLoader.load(RESOURCE_GRAPH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if generated_script == null:
		return _fail("The selective resource export must include the fixture graph")
	var graph_owner := Node.new()
	graph_owner.set_script(generated_script)
	root.add_child(graph_owner)
	var scene_value := graph_owner.call(&"text_scene") as PackedScene
	var binary_scene_value := graph_owner.call(&"binary_scene") as PackedScene
	var script_value := graph_owner.call(&"script_asset") as Script
	var graph_value := graph_owner.call(&"script_graph") as Script
	var constructors_ok := _verify_script_construction(graph_owner)
	graph_owner.free()
	if not constructors_ok:
		return false
	if not _verify_packed_scene_content(scene_value) or not _verify_packed_scene_content(binary_scene_value):
		return _fail("The exported preloads must instantiate both scene encodings")
	if script_value == null or script_value.get_script_constant_map().get(&"MARKER") != SCRIPT_MARKER or graph_value == null:
		return _fail("The exported preloads must retain real GDScript and Visual Script resources")
	return true


## Model a typed native GDScript constructor, member call, and optional handoff to the scene tree.
func _constructor_definition(method_name: StringName, attach: bool, marker_override: Variant = null) -> Dictionary:
	var marker_pin := _pin(&"arg_1", TYPE_INT).merged({"script_optional": true, "default": CONSTRUCTOR_DEFAULT})
	var values := {&"arg_1": marker_override} if marker_override != null else {}
	var create_instance := {"id": 2, "kind": &"create_script_instance", "class": &"Node3D",
		"script_path": CONSTRUCTOR_ASSET, "script_name": &"ConstructorAsset",
		"inputs": [_pin(&"arg_0", TYPE_INT), marker_pin], "values": values}
	var call_reader := {"id": 3, "kind": &"call_method",
		"member": {"class": &"Node3D", "name": &"read_value", "script_path": CONSTRUCTOR_ASSET,
			"script_name": &"ConstructorAsset", "inputs": [], "output": _pin(&"result", TYPE_INT)}}
	var records := [{"id": 1, "kind": &"function_entry"}, create_instance, call_reader, {"id": 4, "kind": &"return"}]
	var wires := [_wire(1, &"then", 2, &"in"), _wire(1, &"initial_value", 2, &"arg_0"),
		_wire(2, &"then", 3, &"in"), _wire(2, &"instance", 3, &"target"),
		_wire(3, &"result", 4, &"result")]
	if attach:
		records.append({"id": 5, "kind": &"add_child"})
		wires.append_array([_wire(3, &"then", 5, &"in"), _wire(2, &"instance", 5, &"node"), _wire(5, &"then", 4, &"in")])
	else:
		wires.append(_wire(3, &"then", 4, &"in"))
	return {"id": 0, "name": method_name, "inputs": [_pin(&"initial_value", TYPE_INT)],
		"outputs": [_pin(&"result", TYPE_INT)], "graph": {"nodes": records, "connections": wires}}


## Check constructed objects have their nominal script, constructor data, and normal graph lifetime.
func _verify_script_construction(graph_owner: Node) -> bool:
	const INITIAL_VALUE := 7
	var fixture_script := ResourceLoader.load(CONSTRUCTOR_ASSET, "GDScript") as GDScript
	if fixture_script == null:
		return _fail("The script constructor must be a declared graph dependency")
	var starting_children := graph_owner.get_child_count()
	var detached_value: Variant = graph_owner.call(&"spawn_script_detached", INITIAL_VALUE)
	var detached_id: int = fixture_script.get(&"last_created_id")
	if (detached_value != INITIAL_VALUE + CONSTRUCTOR_DEFAULT or graph_owner.get_child_count() != starting_children or
			detached_id == 0 or is_instance_id_valid(detached_id)):
		return _fail("An unparented constructed node must be usable during the action and released afterward")
	var default_value: Variant = graph_owner.call(&"spawn_script_default", INITIAL_VALUE)
	var override_value: Variant = graph_owner.call(&"spawn_script_override", INITIAL_VALUE)
	if default_value != INITIAL_VALUE + CONSTRUCTOR_DEFAULT or override_value != INITIAL_VALUE + CONSTRUCTOR_OVERRIDE:
		return _fail("Create from Script did not call the native constructor with defaults and explicit values")
	if graph_owner.get_child_count() != starting_children + 2:
		return _fail("Add Child must keep both constructed nodes alive in the scene")
	var first := graph_owner.get_child(starting_children)
	var second := graph_owner.get_child(starting_children + 1)
	var first_script := first.get_script() as Script
	var second_script := second.get_script() as Script
	if first_script == null or first_script.resource_path != CONSTRUCTOR_ASSET or first_script != second_script or first == second:
		return _fail("Independent constructor calls must produce fresh nodes with the same attached script")
	return true


## Return the loaded resource and the chosen execution route from a typed graph.
func _load_definition(method_name: StringName, native_class: StringName) -> Dictionary:
	return {"id": 0, "name": method_name, "inputs": [_pin(&"path", TYPE_STRING)], "outputs": [
		_pin(&"resource", TYPE_OBJECT, native_class), _pin(&"succeeded", TYPE_BOOL)],
		"graph": {"nodes": [
			{"id": 1, "kind": &"function_entry"},
			{"id": 2, "kind": &"load_resource", "class": native_class},
			{"id": 3, "kind": &"return", "values": {&"succeeded": true}},
			{"id": 4, "kind": &"return", "values": {&"succeeded": false}}],
		"connections": [
			_wire(1, &"then", 2, &"in"), _wire(1, &"path", 2, &"path"),
			_wire(2, &"success", 3, &"in"), _wire(2, &"failure", 4, &"in"),
			_wire(2, &"resource", 3, &"resource"), _wire(2, &"resource", 4, &"resource")]}}


## Call the same runtime loader repeatedly with new paths, missing files and incompatible classes.
func _verify_load(packed: bool) -> bool:
	var graph_script := _new_script([
		_load_definition(&"load_any_asset", &"Resource"),
		_load_definition(&"load_scene_asset", &"PackedScene")])
	if graph_script == null:
		return _fail("Could not create the runtime resource loader graph")
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	if not packed and ResourceLoader.has_cached(TEXT_SCENE):
		graph_owner.free()
		return _fail("A dynamic path must not load its resource before execution")
	var cases: Array[Array] = [
		[&"load_any_asset", PRELOAD_ASSET, &"Gradient"],
		[&"load_any_asset", MISSING_ASSET, &""],
		[&"load_scene_asset", PRELOAD_ASSET, &""],
		[&"load_any_asset", "", &""],
		[&"load_any_asset", ResourceUID.id_to_text(ResourceUID.create_id()), &""],
		[&"load_any_asset", "uid://invalid!", &""],
		[&"load_any_asset", PRELOAD_ASSET, &"Gradient"],
		[&"load_scene_asset", TEXT_SCENE, &"PackedScene"],
		[&"load_scene_asset", SCRIPT_ASSET, &""],
		[&"load_any_asset", SCRIPT_ASSET, &"GDScript"],
	]
	var load_uid := ResourceUID.INVALID_ID
	if not packed:
		load_uid = ResourceUID.create_id()
		ResourceUID.add_id(load_uid, BINARY_SCENE)
		cases.append_array([
			[&"load_scene_asset", ResourceUID.id_to_text(load_uid), &"PackedScene"],
			[&"load_any_asset", GRAPH_ASSET, &"VisualScript"],
			[&"load_any_asset", TEXT_ASSET, &""],
			[&"load_any_asset", MATERIAL_ASSET.trim_prefix("res://"), &"StandardMaterial3D"],
			[&"load_any_asset", USER_RESOURCE, &"StandardMaterial3D"],
			[&"load_any_asset", BARE_AUDIO_ASSET, &"AudioStream"],
			[&"load_any_asset", IMPORTED_AUDIO, &"AudioStreamWAV"],
			[&"load_scene_asset", TEXT_SCENE, &"PackedScene"],
		])
	for fixture_case: Array in cases:
		var loaded_result: Variant = graph_owner.call(fixture_case[0], fixture_case[1])
		var expected_class: StringName = fixture_case[2]
		var branch_ok: bool = loaded_result is Dictionary and loaded_result.get(&"succeeded") == not expected_class.is_empty()
		var resource_ok := false
		if branch_ok and loaded_result.has(&"resource"):
			var asset: Variant = loaded_result[&"resource"]
			resource_ok = asset == null if expected_class.is_empty() else asset is Resource and asset.is_class(expected_class)
		if not resource_ok:
			graph_owner.free()
			if load_uid != ResourceUID.INVALID_ID:
				ResourceUID.remove_id(load_uid)
			return _fail("%s returned %s for %s. Expected %s" % [fixture_case[0], loaded_result, fixture_case[1], expected_class])
	graph_owner.free()
	if load_uid != ResourceUID.INVALID_ID:
		ResourceUID.remove_id(load_uid)
	return true


## Run all collection interactions in source and the resource-only check in selective exports.
func _verify() -> void:
	var options := OS.get_cmdline_user_args()
	if "--constructor-type-error" in options:
		if _verify_constructor_type_failure():
			print("CRIMSON_VISUAL_SCRIPT_CONSTRUCTOR_TYPE_REJECTION_PASSED")
			quit()
		return
	if "--preload-errors" in options:
		if _verify_preload_failures():
			print("CRIMSON_VISUAL_SCRIPT_PRELOAD_FAILURE_MATRIX_PASSED")
			quit()
		return
	var packed := "--packed" in options
	if not packed and (not _verify_collections() or not _write_resource_fixtures()):
		return
	if not _verify_preload() or not _verify_load(packed):
		return
	var valid_preloads := _verify_selective_preloads() if packed else _verify_preload_parity()
	if not valid_preloads:
		return
	print("CRIMSON_VISUAL_SCRIPT_COLLECTION_RESOURCE_MATRIX_PASSED ",
		"packed preload and load" if packed else "%d collection cases and resource loading" % EXPECTED_COLLECTION_CASES)
	quit()
