# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

## Authored scene that attaches a Visual Script through the ordinary script property.
const READY_SCENE_PATH := "res://tests/visual_scripting/ready_print.tscn"
## Authored scene that assigns a stream and plays it using native graph members.
const AUDIO_SCENE_PATH := "res://tests/visual_scripting/audio_members.tscn"
## Original chime used by the typed resource input.
const CHIME_PATH := "res://tests/visual_scripting/ready_chime.wav"
## Isolated output path used to exercise Godot's registered script saver and loader.
const SAVED_SCRIPT_PATH := "user://mesh_ready.crimsonscript"


## Run after the root window has entered the scene tree.
func _initialize() -> void:
	_verify.call_deferred()


## Check serialized attachment, native audio playback, and graph save and reload.
func _verify() -> void:
	var ready_scene := ResourceLoader.load(READY_SCENE_PATH) as PackedScene
	if ready_scene == null:
		_fail("Load the scene containing an attached Visual Script.")
		return
	var ready_owner := ready_scene.instantiate()
	var attached := ready_owner.get_script() as Script
	if attached == null or attached.get_instance_base_type() != &"Node":
		ready_owner.free()
		_fail("The scene loads its Visual Script as a Node script.")
		return
	root.add_child(ready_owner)

	if not await _verify_audio() or not await _verify_audio_2d() or not _verify_mesh() or not _verify_resource_defaults():
		return
	print("CRIMSON_VISUAL_SCRIPT_TESTS_PASSED")
	quit()


## Check inherited properties, a typed resource, and a native method with defaults.
func _verify_audio() -> bool:
	var audio_scene := ResourceLoader.load(AUDIO_SCENE_PATH) as PackedScene
	if audio_scene == null:
		return _fail("Load the authored audio graph scene.")
	var audio_owner := audio_scene.instantiate() as Node3D
	if audio_owner == null:
		return _fail("The audio graph scene has a Node3D root.")
	var audio_graph := audio_owner.get_script() as Script
	if audio_graph == null or audio_graph.get_instance_base_type() != &"Node3D" or audio_owner.get_child_count() != 1:
		audio_owner.free()
		return _fail("The audio graph is attached and the scene has only the listener camera before Ready.")
	root.add_child(audio_owner)
	if audio_owner.get_child_count() != 2:
		return _fail("Ready adds exactly one audio player under the scene root.")
	var audio_player := audio_owner.get_child(1) as AudioStreamPlayer3D
	if audio_player == null:
		return _fail("Create AudioStreamPlayer3D returns and parents a native audio player.")
	if not audio_player.stream is AudioStreamWAV or audio_player.stream.resource_path != CHIME_PATH:
		return _fail("Set Stream loads the expected WAV asset from its typed resource pin.")
	if not is_equal_approx(audio_player.volume_db, -6.0) or not is_equal_approx(audio_player.max_db, -6.0):
		return _fail("A float property setter and getter transfer the configured volume to Max dB.")
	if not audio_player.position.is_equal_approx(Vector3(0, 0, -2)):
		return _fail("The graph sets the inherited Node3D Position on its wired target.")
	await physics_frame
	await physics_frame
	if not audio_player.playing or not audio_player.has_stream_playback():
		return _fail("Native Play uses its default argument and starts the assigned stream.")
	audio_player.stop()
	audio_owner.queue_free()
	# Give Godot's audio mixer time to release the playback resource before exit.
	await create_timer(0.1).timeout
	return true


## Verify a saved WAV pin also loads and starts playback on a native 2D player.
func _verify_audio_2d() -> bool:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		return _fail("Create a Visual Script for the native 2D audio player.")
	graph_script.call(&"set_graph_base", &"AudioStreamPlayer2D")
	graph_script.call(&"set_graph", {
		"version": 1,
		"nodes": [
			{"id": 1, "kind": "ready", "position": Vector2.ZERO},
			{
				"id": 2, "kind": "set_property", "position": Vector2(248, 0),
				"member": {"class": &"AudioStreamPlayer2D", "name": &"stream", "inputs": [
					{"id": &"value", "label": "Stream", "type": TYPE_OBJECT, "class": &"AudioStream"},
				]},
				"values": {&"value": {"path": CHIME_PATH}},
			},
			{
				"id": 3, "kind": "call_method", "position": Vector2(544, 0),
				"member": {"class": &"AudioStreamPlayer2D", "name": &"play", "inputs": [
					{"id": &"arg_0", "label": "From Position", "type": TYPE_FLOAT, "default": 0.0},
				]},
			},
		],
		"connections": [
			{"from": 1, "from_pin": "then", "to": 2, "to_pin": "in"},
			{"from": 2, "from_pin": "then", "to": 3, "to_pin": "in"},
		],
	})
	var audio_player := AudioStreamPlayer2D.new()
	audio_player.set_script(graph_script)
	root.add_child(audio_player)
	if not audio_player.stream is AudioStreamWAV or audio_player.stream.resource_path != CHIME_PATH:
		return _fail("AudioStreamPlayer2D gets a concrete WAV from its typed Stream pin.")
	await physics_frame
	await physics_frame
	if not audio_player.playing or not audio_player.has_stream_playback():
		return _fail("AudioStreamPlayer2D starts playback using the assigned WAV.")
	audio_player.stop()
	audio_player.queue_free()
	# Give Godot's audio mixer time to release the playback resource before exit.
	await create_timer(0.1).timeout
	print("CRIMSON_VISUAL_SCRIPT_AUDIO_2D_READY")
	return true


## Check a native method result, a typed receiver, and a numeric enum value.
func _verify_mesh() -> bool:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		return _fail("Create a native Visual Script resource.")
	graph_script.call(&"set_graph_base", &"MeshInstance3D")
	var print_node: Dictionary = {"id": 7, "kind": "print", "message": "First save", "position": Vector2(1440, 0)}
	var mesh_graph: Dictionary = {
		"version": 1,
		"nodes": [
			{"id": 1, "kind": "ready", "position": Vector2.ZERO},
			{"id": 2, "kind": "create_node", "class": &"MeshInstance3D", "position": Vector2(240, 0)},
			{"id": 3, "kind": "add_child", "position": Vector2(480, 0)},
			{
				"id": 4, "kind": "call_method", "position": Vector2(720, 0),
				"member": {"class": &"Node", "name": &"get_child_count", "inputs": [
					{"id": &"arg_0", "label": "Include Internal", "type": TYPE_BOOL, "default": false},
				], "output": {"id": &"result", "label": "Child Count", "type": TYPE_INT}},
			},
			{
				"id": 5, "kind": "set_property", "position": Vector2(960, 0),
				"member": {"class": &"GeometryInstance3D", "name": &"cast_shadow", "inputs": [
					{"id": &"value", "label": "Cast Shadow", "type": TYPE_INT},
				]},
			},
			{
				"id": 6, "kind": "set_property", "position": Vector2(1200, 0),
				"member": {"class": &"GeometryInstance3D", "name": &"cast_shadow", "inputs": [
					{"id": &"value", "label": "Cast Shadow", "type": TYPE_INT},
				]},
				"values": {&"value": GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY},
			},
			print_node,
		],
		"connections": [
			{"from": 1, "from_pin": "then", "to": 2, "to_pin": "in"},
			{"from": 2, "from_pin": "then", "to": 3, "to_pin": "in"},
			{"from": 2, "from_pin": "node", "to": 3, "to_pin": "node"},
			{"from": 3, "from_pin": "then", "to": 4, "to_pin": "in"},
			{"from": 4, "from_pin": "then", "to": 5, "to_pin": "in"},
			{"from": 4, "from_pin": "result", "to": 5, "to_pin": "value"},
			{"from": 5, "from_pin": "then", "to": 6, "to_pin": "in"},
			{"from": 2, "from_pin": "node", "to": 6, "to_pin": "target"},
			{"from": 6, "from_pin": "then", "to": 7, "to_pin": "in"},
		],
	}
	graph_script.call(&"set_graph", mesh_graph)
	if ResourceSaver.save(graph_script, SAVED_SCRIPT_PATH) != OK:
		return _fail("Save an authored Visual Script through ResourceSaver.")
	print_node["message"] = "CRIMSON_VISUAL_SCRIPT_MESH_READY"
	graph_script.call(&"set_graph", mesh_graph)
	if ResourceSaver.save(graph_script, SAVED_SCRIPT_PATH) != OK:
		return _fail("Save edits to the existing Visual Script through ResourceSaver.")
	var saved_script := ResourceLoader.load(SAVED_SCRIPT_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if saved_script == null or saved_script.get_instance_base_type() != &"MeshInstance3D":
		return _fail("Reopen the saved graph with its MeshInstance3D base intact.")
	var mesh_owner := MeshInstance3D.new()
	mesh_owner.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mesh_owner.set_script(saved_script)
	root.add_child(mesh_owner)
	if mesh_owner.get_child_count() != 1 or not mesh_owner.get_child(0) is MeshInstance3D:
		return _fail("Ready creates one native MeshInstance3D and parents it to the script owner.")
	if mesh_owner.cast_shadow != GeometryInstance3D.SHADOW_CASTING_SETTING_ON:
		return _fail("The integer result of a Node method feeds an inherited enum setter.")
	var mesh_child := mesh_owner.get_child(0) as MeshInstance3D
	if mesh_child.cast_shadow != GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY:
		return _fail("The enum setter changes the created mesh through a typed target wire.")
	return true


## Save scene defaults, reload their dependency records, and materialize typed values inside the running graph.
func _verify_resource_defaults() -> bool:
	# The selective export includes only collection_export.crimsonscript; its Array must pull in this scene.
	const SCENE_PATH := "res://tests/visual_scripting/collection_asset_only.tscn"
	const GRAPH_PATH := "user://packed_scene_defaults.crimsonscript"
	var script_type := ClassDB.instantiate(&"VisualScript") as Script
	if script_type == null:
		return _fail("Create a disposable graph to test saved scene defaults.")
	var scene_type := {"type": TYPE_OBJECT, "class": &"PackedScene"}
	var scene_ref := {"path": SCENE_PATH, "uid": ""}
	var default_pins: Array[Dictionary] = [
		scene_type,
		{"type": TYPE_ARRAY, "element": scene_type},
		{"type": TYPE_DICTIONARY, "key": {"type": TYPE_STRING_NAME}, "value": scene_type},
	]
	var defaults := [scene_ref, [scene_ref, null, scene_ref], {&"spawn": scene_ref, &"optional": null}]
	var variables: Array[Dictionary] = []
	var nodes: Array[Dictionary] = [{"id": 1, "kind": &"ready"}]
	var wires: Array[Dictionary] = []
	for index: int in range(defaults.size()):
		var variable_id := index + 1
		var getter_id := index * 2 + 2
		var meta_id := getter_id + 1
		var descriptor := default_pins[index].merged({"id": &"value", "label": "Value"})
		variables.append({"id": variable_id, "name": "scene_default_%d" % index, "pin": descriptor, "value": defaults[index]})
		nodes.append({"id": getter_id, "kind": &"get_variable", "variable": variable_id})
		nodes.append({"id": meta_id, "kind": &"call_method", "member": {
			"class": &"Object", "name": &"set_meta", "inputs": [
				{"id": &"arg_0", "label": "Name", "type": TYPE_STRING_NAME},
				{"id": &"arg_1", "label": "Value", "type": TYPE_NIL}],
		}, "values": {&"arg_0": StringName("scene_default_%d" % index)}})
		wires.append({"from": 1 if index == 0 else meta_id - 2, "from_pin": &"then", "to": meta_id, "to_pin": &"in"})
		wires.append({"from": getter_id, "from_pin": &"value", "to": meta_id, "to_pin": &"arg_1"})
	script_type.call(&"set_graph", {"version": 1, "variables": variables, "nodes": nodes, "connections": wires})
	if ResourceSaver.save(script_type, GRAPH_PATH) != OK:
		return _fail("Save the graph with PackedScene defaults.")
	var has_scene_dependency := false
	for dependency: String in ResourceLoader.get_dependencies(GRAPH_PATH):
		if dependency.contains(SCENE_PATH) or dependency.begins_with("uid://"):
			has_scene_dependency = true
			break
	if not has_scene_dependency:
		return _fail("Saved graph includes its project scene dependencies.")
	var reopened := ResourceLoader.load(GRAPH_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if reopened == null:
		return _fail("Reload graph with scalar and collection asset tokens.")
	var owner_node := Node.new()
	owner_node.set_script(reopened)
	root.add_child(owner_node)
	var loaded_scene: Variant = owner_node.get_meta(&"scene_default_0", false)
	var loaded_array: Variant = owner_node.get_meta(&"scene_default_1", [])
	var loaded_map: Variant = owner_node.get_meta(&"scene_default_2", {})
	var result: bool = loaded_scene is PackedScene and loaded_scene.resource_path == SCENE_PATH \
		and loaded_array is Array and loaded_array.size() == 3 and loaded_array.get_typed_class_name() == &"PackedScene" \
		and loaded_array[0] is PackedScene and loaded_array[1] == null and loaded_array[2] is PackedScene \
		and loaded_map is Dictionary and loaded_map.get_typed_key_builtin() == TYPE_STRING_NAME \
		and loaded_map.get_typed_value_class_name() == &"PackedScene" and loaded_map.get(&"spawn") is PackedScene \
		and loaded_map.has(&"optional") and loaded_map[&"optional"] == null
	owner_node.free()
	return result or _fail("Reloaded graph yields a PackedScene, Array[PackedScene], and Dictionary[StringName, PackedScene], preserving nulls.")


## Report the failed observable behavior and exit the headless process.
func _fail(requirement: String) -> bool:
	push_error("[Crimson] Visual Script fixture: " + requirement)
	quit(1)
	return false
