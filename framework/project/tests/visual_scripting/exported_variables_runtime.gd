# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const SCRIPT_PATH := "user://exported_variables_runtime.crimsonscript"
const SCENE_PATH := "user://exported_variables_runtime.tscn"
const DEFAULT_HEAL := 25
const OVERRIDE_HEAL := 50
const NEW_DEFAULT_HEAL := 30
const HEAL_VARIABLE := 1
const PLAYER_VARIABLE := 2
const AUDIO_VARIABLE := 3
const AUDIO_MIX_RATE := 22050
const HEAL_RANGE := {"min": 10, "max": 40, "step": 5}
const FINE_STEP := 1e-16


## Run once the headless scene tree can attach and execute graph scripts.
func _initialize() -> void:
	_verify.call_deferred()


## Report exactly which part of the scene property round trip failed.
func _need(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("Exported Visual Script variables: " + requirement)
		quit(1)
	return condition


## Describe a scalar pin or a native object reference, including its declared class.
func _pin(pin_id: StringName, value_type: int, native_class: StringName = &"") -> Dictionary:
	return {"id": pin_id, "label": String(pin_id), "type": value_type, "class": native_class}


## Create a typed graph getter using the same Get Variable operation authors wire in the editor.
func _getter(graph_variable: Dictionary) -> Dictionary:
	var result_pin: Dictionary = graph_variable.pin.duplicate()
	result_pin["id"] = &"result"
	return {
		"id": int(graph_variable.id) + 10,
		"name": &"read_" + String(graph_variable.name),
		"inputs": [],
		"outputs": [result_pin],
		"graph": {
			"nodes": [
				{"id": 1, "kind": &"function_entry"},
				{"id": 2, "kind": &"get_variable", "variable": graph_variable.id},
				{"id": 3, "kind": &"return"},
			],
			"connections": [
				{"from": 1, "from_pin": &"then", "to": 3, "to_pin": &"in"},
				{"from": 2, "from_pin": &"value", "to": 3, "to_pin": &"result"},
			],
		},
	}


## Build a graph setter to prove graph writes also update the Godot property.
func _setter() -> Dictionary:
	return {
		"id": 50,
		"name": &"write_heal_amount",
		"inputs": [_pin(&"new_value", TYPE_INT)],
		"outputs": [],
		"graph": {
			"nodes": [
				{"id": 1, "kind": &"function_entry"},
				{"id": 2, "kind": &"set_variable", "variable": HEAL_VARIABLE},
			],
			"connections": [
				{"from": 1, "from_pin": &"then", "to": 2, "to_pin": &"in"},
				{"from": 1, "from_pin": &"new_value", "to": 2, "to_pin": &"value"},
			],
			"locals": [
				{"id": 1, "name": &"local_only", "pin": _pin(&"value", TYPE_INT), "value": 4, "exported": true},
			],
		},
	}


## Supply standalone typed data, leaving scene references unset until the scene assigns them.
func _graph(default_heal: int) -> Dictionary:
	var weapon_pin := _pin(&"value", TYPE_ARRAY)
	weapon_pin["element"] = {"type": TYPE_STRING}
	var ammo_pin := _pin(&"value", TYPE_DICTIONARY)
	ammo_pin["key"] = {"type": TYPE_STRING}
	ammo_pin["value"] = {"type": TYPE_INT}
	var mode_pin := _pin(&"value", TYPE_INT)
	mode_pin["enum_type"] = &"Node.ProcessMode"
	var declarations := [
		{"id": HEAL_VARIABLE, "name": &"heal_amount", "pin": _pin(&"value", TYPE_INT), "value": default_heal,
			"exported": true, "inspector": {"group": "Pickup Tuning", "range": HEAL_RANGE, "suffix": "HP"}},
		{"id": PLAYER_VARIABLE, "name": &"animation_player", "pin": _pin(&"value", TYPE_OBJECT, &"AnimationPlayer"), "value": null, "exported": true},
		{"id": AUDIO_VARIABLE, "name": &"pickup_audio", "pin": _pin(&"value", TYPE_OBJECT, &"AudioStream"), "value": null, "exported": true},
		{"id": 4, "name": &"private_counter", "pin": _pin(&"value", TYPE_INT), "value": 1},
		{"id": 5, "name": &"weapon_names", "pin": weapon_pin, "value": ["Sidearm"], "exported": true},
		{"id": 6, "name": &"ammo_by_weapon", "pin": ammo_pin, "value": {"Sidearm": 10}, "exported": true},
		{"id": 7, "name": &"pickup_process_mode", "pin": mode_pin, "value": Node.PROCESS_MODE_INHERIT,
			"exported": true, "inspector": {"group": "Behavior", "range": HEAL_RANGE, "suffix": "invalid"}},
		{"id": 10, "name": &"respawn_delay", "pin": _pin(&"value", TYPE_FLOAT), "value": 1.25,
			"exported": true, "inspector": {"group": "  Pickup Tuning  ", "suffix": "s",
				"range": {"min": 0.25, "max": 5.5, "step": 0.25, "slider": false}}},
		{"id": 11, "name": &"pickup_capacity", "pin": _pin(&"value", TYPE_INT), "value": 100,
			"exported": true, "inspector": {"suffix": "items"}},
		{"id": 12, "name": &"fine_tuning", "pin": _pin(&"value", TYPE_FLOAT), "value": FINE_STEP,
			"exported": true, "inspector": {"range": {"min": 0.0, "max": 1.0, "step": FINE_STEP}}},
	]
	var readers: Array[Dictionary] = []
	for declaration: Dictionary in declarations:
		readers.append(_getter(declaration))
	declarations.append({"id": 8, "name": &"callback", "pin": _pin(&"value", TYPE_CALLABLE), "value": Callable(), "exported": true})
	declarations.append({"id": 9, "name": &"name", "pin": _pin(&"value", TYPE_INT), "value": 10, "exported": true})
	readers.append(_setter())
	return {"version": 1, "nodes": [], "connections": [], "variables": declarations, "functions": readers}


## Return the native metadata for a particular property on the scripted object.
func _property_info(property_list: Array[Dictionary], key: StringName) -> Dictionary:
	for info: Dictionary in property_list:
		if info.name == key:
			return info
	return {}


## Check native range controls by their behavior flags without constraining hint token ordering.
func _verify_range_hint(info: Dictionary, limits: Vector3, show_slider: bool, suffix: String) -> bool:
	if info.get("hint") != PROPERTY_HINT_RANGE:
		return false
	var tokens := String(info.get("hint_string", "")).split(",")
	if tokens.size() < 3 or not tokens[0].is_valid_float() or not tokens[1].is_valid_float() or not tokens[2].is_valid_float():
		return false
	var stored_limits := Vector3(tokens[0].to_float(), tokens[1].to_float(), tokens[2].to_float())
	var hides_slider := tokens.has("hide_control") or tokens.has("hide_slider")
	return (stored_limits.is_equal_approx(limits) and tokens.has("or_less") and tokens.has("or_greater") and
		hides_slider != show_slider and (not show_slider or info.get("type") != TYPE_INT or tokens.has("prefer_slider")) and
		tokens.has("suffix:" + suffix))


## Resolve each variable's group using the same native group boundaries as the Inspector.
func _verify_groups(properties: Array[Dictionary]) -> bool:
	var tracked := {&"heal_amount": "Pickup Tuning", &"respawn_delay": "Pickup Tuning",
		&"pickup_process_mode": "Behavior", &"pickup_capacity": "", &"animation_player": ""}
	var members := {}
	var group_counts := {"Pickup Tuning": 0, "Behavior": 0}
	var current_group := ""
	var group_prefix := ""
	for info: Dictionary in properties:
		var property_name := String(info.name)
		if (info.usage & PROPERTY_USAGE_CATEGORY) != 0:
			current_group = ""
			group_prefix = ""
		elif (info.usage & PROPERTY_USAGE_GROUP) != 0:
			current_group = property_name
			group_prefix = String(info.hint_string).get_slice(",", 0)
			if group_counts.has(current_group):
				group_counts[current_group] += 1
				if (info.usage & PROPERTY_USAGE_STORAGE) != 0:
					return false
		elif (info.usage & PROPERTY_USAGE_EDITOR) != 0:
			if not group_prefix.is_empty() and not property_name.begins_with(group_prefix):
				current_group = ""
				group_prefix = ""
			if tracked.has(info.name):
				members[info.name] = current_group
	return members == tracked and group_counts == {"Pickup Tuning": 1, "Behavior": 1}


## Validate the type hints Godot uses to select native Inspector editors and to pack node references.
func _verify_metadata(pickup: Node, graph_script: Script) -> bool:
	var properties := pickup.get_property_list()
	var health := _property_info(properties, &"heal_amount")
	var player := _property_info(properties, &"animation_player")
	var audio := _property_info(properties, &"pickup_audio")
	var weapon := _property_info(properties, &"weapon_names")
	var ammo := _property_info(properties, &"ammo_by_weapon")
	var mode := _property_info(properties, &"pickup_process_mode")
	var delay_info := _property_info(properties, &"respawn_delay")
	var capacity := _property_info(properties, &"pickup_capacity")
	var fine_tuning := _property_info(properties, &"fine_tuning")
	var fine_step_hint := String(fine_tuning.get("hint_string", "")).get_slice(",", 2).to_float()
	var script_properties := graph_script.get_script_property_list()
	var usage_mask := PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR
	return (
		_need(not health.is_empty() and health.type == TYPE_INT and (health.usage & usage_mask) == usage_mask,
			"Exported heal_amount is a stored, editable int.") and
		_need(not player.is_empty() and player.type == TYPE_OBJECT and player.hint == PROPERTY_HINT_NODE_TYPE and
			player.hint_string == "AnimationPlayer", "AnimationPlayer has Godot's typed node picker hint.") and
		_need(not audio.is_empty() and audio.type == TYPE_OBJECT and audio.hint == PROPERTY_HINT_RESOURCE_TYPE and
			audio.hint_string == "AudioStream", "AudioStream has Godot's typed resource picker hint.") and
		_need(weapon.get("hint") == PROPERTY_HINT_TYPE_STRING and ammo.get("hint") == PROPERTY_HINT_TYPE_STRING and
			mode.get("hint") == PROPERTY_HINT_ENUM and not String(mode.get("hint_string", "")).contains("suffix:"),
			"Collection and enum hints stay native even if numeric constraints were supplied to an enum.") and
		_need(_verify_range_hint(health, Vector3(10, 40, 5), true, "HP") and
			_verify_range_hint(delay_info, Vector3(0.25, 5.5, 0.25), false, "s"),
			"Integer sliders and float fields publish their intervals, steps and suffixes on attached instances.") and
		_need(capacity.get("hint") == PROPERTY_HINT_NONE and String(capacity.get("hint_string", "")).contains("suffix:items"),
			"A suffix alone keeps the native numeric editor and its bounds.") and
		_need(fine_tuning.get("hint") == PROPERTY_HINT_RANGE and is_equal_approx(fine_step_hint / FINE_STEP, 1.0),
			"The native editor receives a small positive float step without decimal truncation.") and
		_need(_verify_groups(script_properties) and _verify_groups(properties),
			"Nonadjacent declarations share one native group while ungrouped properties remain outside it.") and
		_need(_property_info(properties, &"private_counter").is_empty() and _property_info(properties, &"local_only").is_empty(),
			"Private variables and function locals stay out of the Inspector.") and
		_need(_property_info(script_properties, &"callback").is_empty() and _property_info(script_properties, &"name").is_empty(),
			"Unsupported Callable exports and variables colliding with native properties stay out of the script property list.") and
		_need(_property_info(script_properties, &"heal_amount").get("type") == TYPE_INT and
			graph_script.get_property_default_value(&"heal_amount") == DEFAULT_HEAL,
			"The script publishes the same typed declaration and original default.")
	)


## Feed broken or legacy presentation records through the normal graph loader and preserve their properties.
func _verify_legacy_and_invalid_metadata() -> bool:
	var candidates: Array[Variant] = [
		null, "not a dictionary", {"range": "not a range"}, {"group": 42},
		{"group": "Wrong/Group", "suffix": "s,or_greater"},
		{"group": "Wrong\nGroup", "suffix": "s\nnewline"},
		{"range": {"min": 4, "max": 2, "step": 1}},
		{"range": {"min": 0, "max": 4, "step": 0}},
		{"range": {"min": 0, "max": INF, "step": 1}},
		{"range": {"min": 0, "max": 4, "step": NAN}},
		{"range": {"min": 0, "max": 4, "step": -1}},
		{"range": {"min": 0, "max": 4, "step": 0.5}},
		{"range": {"min": 0, "max": 4.5, "step": 1}},
		{"range": {"min": "0", "max": 4, "step": 1}},
		{"range": {"min": 0, "max": 4}},
		{"range": {"min": 0, "max": 4, "step": 1, "slider": 42}},
	]
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	graph_script.call(&"set_graph_base", &"Node")
	var declarations: Array[Dictionary] = []
	for case_index: int in range(candidates.size()):
		var declaration := {"id": case_index + 1, "name": StringName("case_%d" % case_index),
			"pin": _pin(&"value", TYPE_INT), "value": 25, "exported": true}
		if candidates[case_index] != null:
			declaration["inspector"] = candidates[case_index]
		declarations.append(declaration)
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": [], "variables": declarations})
	var pickup := Node.new()
	pickup.set_script(graph_script)
	var properties := pickup.get_property_list()
	var invalid_cases: Array[StringName] = []
	for declaration: Dictionary in declarations:
		var property_key: StringName = declaration.name
		var info := _property_info(properties, property_key)
		if (info.get("type") != TYPE_INT or info.get("hint") != PROPERTY_HINT_NONE or
			String(info.get("hint_string", "")) != "" or pickup.get(property_key) != 25):
			invalid_cases.append(property_key)
	var invalid_groups := false
	for info: Dictionary in graph_script.get_script_property_list():
		if (info.usage & PROPERTY_USAGE_GROUP) != 0:
			invalid_groups = true
	pickup.free()
	return _need(invalid_cases.is_empty() and not invalid_groups,
		"Invalid or absent metadata keeps native ints ungrouped and without numeric hints. Cases: %s" % [invalid_cases])


## Put a native node under the scene owner so Godot serializes it.
func _add_owned(parent_node: Node, child_node: Node, scene_owner: Node, child_name: StringName) -> void:
	child_node.name = child_name
	parent_node.add_child(child_node)
	child_node.owner = scene_owner


## Save a real scene file and load a fresh instance through Godot's resource system.
func _load_pickups() -> Node:
	var packed := ResourceLoader.load(SCENE_PATH, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE_DEEP) as PackedScene
	if not _need(packed != null, "Load the saved fixture scene."):
		return null
	var scene := packed.instantiate()
	root.add_child(scene)
	return scene


## Save scene overrides and defaults with the stock PackedScene serializer.
func _save_pickups(scene: Node) -> bool:
	var packed := PackedScene.new()
	return _need(packed.pack(scene) == OK and ResourceSaver.save(packed, SCENE_PATH) == OK,
		"Pack and save two pickup instances.")


## Check that packed scene overrides retain their Godot container types and enum values.
func _verify_collection_values(small_pickup: Node, large_pickup: Node) -> bool:
	var loaded_weapons: Array = large_pickup.call(&"read_weapon_names")
	var loaded_ammo: Dictionary = large_pickup.call(&"read_ammo_by_weapon")
	return _need(loaded_weapons.get_typed_builtin() == TYPE_STRING and loaded_weapons == ["Rifle", "Launcher"] and
		loaded_ammo.get_typed_key_builtin() == TYPE_STRING and loaded_ammo.get_typed_value_builtin() == TYPE_INT and
		loaded_ammo == {"Rifle": 30} and large_pickup.call(&"read_pickup_process_mode") == Node.PROCESS_MODE_ALWAYS and
		small_pickup.call(&"read_weapon_names") == ["Sidearm"] and small_pickup.call(&"read_ammo_by_weapon") == {"Sidearm": 10} and
		small_pickup.call(&"read_pickup_process_mode") == Node.PROCESS_MODE_INHERIT,
		"Scene persistence preserves typed arrays, dictionaries, and enum values per pickup.")


## Check that each owner keeps its own native and scripted properties when a derived class introduces a name collision.
func _verify_derived_collision() -> bool:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(graph_script != null, "Create a graph that can attach to both Node and Node3D."):
		return false
	var position_record := {
		"id": 1, "name": &"position", "pin": _pin(&"value", TYPE_INT), "value": 7, "exported": true,
	}
	var ammo_record := {
		"id": 2, "name": &"ammo", "pin": _pin(&"value", TYPE_INT), "value": 20, "exported": true,
	}
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", {
		"version": 1, "nodes": [], "connections": [],
		"variables": [position_record, ammo_record], "functions": [_getter(position_record), _getter(ammo_record)],
	})
	var spatial_owner := Node3D.new()
	spatial_owner.position = Vector3(1, 2, 3)
	spatial_owner.set_script(graph_script)
	var plain_owner := Node.new()
	plain_owner.set_script(graph_script)
	spatial_owner.set(&"position", Vector3(4, 5, 6))
	spatial_owner.set(&"ammo", 30)
	plain_owner.set(&"position", 9)
	var correct_fields: bool = (
		spatial_owner.get(&"position") == Vector3(4, 5, 6) and
		spatial_owner.call(&"read_position") == 7 and
		_property_info(spatial_owner.get_property_list(), &"position").get("type") == TYPE_VECTOR3 and
		spatial_owner.call(&"read_ammo") == 30 and
		plain_owner.get(&"position") == 9 and plain_owner.call(&"read_position") == 9
	)
	spatial_owner.free()
	plain_owner.free()
	return _need(correct_fields,
		"The Node3D transform and graph getter stay independent. Other exports and plain Node owners keep working.")


## Verify that exported values and graph reads survive a full stock Godot round trip.
func _verify() -> void:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(graph_script != null, "Create the Visual Script resource."):
		return
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", _graph(DEFAULT_HEAL))
	if not _need(ResourceSaver.save(graph_script, SCRIPT_PATH) == OK,
		"Save the graph as the shared script dependency."):
		return
	graph_script = ResourceLoader.load(SCRIPT_PATH, "Script") as Script
	if not _need(graph_script != null, "Load the graph from its external resource path."):
		return
	var pickups := Node.new()
	root.add_child(pickups)
	var small_pickup := Node.new()
	var large_pickup := Node.new()
	_add_owned(pickups, small_pickup, pickups, &"SmallPickup")
	_add_owned(pickups, large_pickup, pickups, &"LargePickup")
	var animation := AnimationPlayer.new()
	_add_owned(large_pickup, animation, pickups, &"Animation")
	small_pickup.set_script(graph_script)
	large_pickup.set_script(graph_script)
	if not _verify_metadata(small_pickup, graph_script):
		return
	if not _need(small_pickup.get(&"heal_amount") == DEFAULT_HEAL and
		small_pickup.call(&"read_heal_amount") == DEFAULT_HEAL and large_pickup.call(&"read_private_counter") == 1,
		"Native reads and graph getters start with the declaration defaults."):
		return
	var audio := AudioStreamWAV.new()
	audio.mix_rate = AUDIO_MIX_RATE
	large_pickup.set(&"animation_player", animation)
	large_pickup.set(&"pickup_audio", audio)
	large_pickup.set(&"heal_amount", OVERRIDE_HEAL)
	var weapon_override: Array[String] = ["Rifle", "Launcher"]
	var ammo_override: Dictionary[String, int] = {"Rifle": 30}
	large_pickup.set(&"weapon_names", weapon_override)
	large_pickup.set(&"ammo_by_weapon", ammo_override)
	large_pickup.set(&"pickup_process_mode", Node.PROCESS_MODE_ALWAYS)
	if not _need(large_pickup.call(&"read_heal_amount") == OVERRIDE_HEAL and
		small_pickup.call(&"read_heal_amount") == DEFAULT_HEAL and large_pickup.call(&"read_animation_player") == animation and
		large_pickup.call(&"read_pickup_audio") == audio, "Each Godot property assignment updates its own graph slot."):
		return
	large_pickup.call(&"write_heal_amount", OVERRIDE_HEAL + 1)
	if not _need(large_pickup.get(&"heal_amount") == OVERRIDE_HEAL + 1,
		"A graph setter updates the same value Godot reads, including outside the Inspector slider interval."):
		return
	large_pickup.set(&"heal_amount", OVERRIDE_HEAL)
	if not _save_pickups(pickups):
		return
	pickups.free()
	var restored_scene := _load_pickups()
	if restored_scene == null:
		return
	var restored_small := restored_scene.get_node("SmallPickup")
	var restored_large := restored_scene.get_node("LargePickup")
	if not _verify_metadata(restored_small, restored_small.get_script()):
		return
	if not _need(restored_small.call(&"read_heal_amount") == DEFAULT_HEAL and
		restored_large.call(&"read_heal_amount") == OVERRIDE_HEAL,
		"Both pickups keep their values after reload, including a scene override above the Inspector slider interval."):
		return
	var restored_audio := restored_large.call(&"read_pickup_audio") as AudioStreamWAV
	if not _need(restored_large.call(&"read_animation_player") == restored_large.get_node("Animation") and
		restored_audio != null and restored_audio.mix_rate == AUDIO_MIX_RATE and
		restored_small.call(&"read_animation_player") == null and restored_small.call(&"read_pickup_audio") == null,
		"Scene reload resolves the remapped node and built-in resource, leaving the other pickup unset."):
		return
	if not _verify_collection_values(restored_small, restored_large):
		return
	restored_scene.free()
	graph_script.call(&"set_graph", _graph(NEW_DEFAULT_HEAL))
	if not _need(ResourceSaver.save(graph_script, SCRIPT_PATH) == OK, "Edit the saved graph default."):
		return
	var updated_scene := _load_pickups()
	if updated_scene == null:
		return
	var updated_small := updated_scene.get_node("SmallPickup")
	var updated_large := updated_scene.get_node("LargePickup")
	if not _need(updated_small.call(&"read_heal_amount") == NEW_DEFAULT_HEAL and
		updated_large.call(&"read_heal_amount") == OVERRIDE_HEAL,
		"The unchanged instance follows the new default while the scene override stays fixed."):
		return
	if not _need(updated_large.property_can_revert(&"heal_amount") and
		updated_large.property_get_revert(&"heal_amount") == NEW_DEFAULT_HEAL,
		"The runtime instance publishes the new native revert value."):
		return
	updated_large.set(&"heal_amount", updated_large.property_get_revert(&"heal_amount"))
	if not _need(updated_large.call(&"read_heal_amount") == NEW_DEFAULT_HEAL, "The graph reads the reverted default."):
		return
	if not _save_pickups(updated_scene):
		return
	updated_scene.free()
	if not _need(not FileAccess.get_file_as_string(SCENE_PATH).contains("heal_amount ="),
		"Saving a reverted value removes the scene override."):
		return
	if not _verify_derived_collision() or not _verify_legacy_and_invalid_metadata():
		return
	print("CRIMSON_VISUAL_SCRIPT_EXPORTED_VARIABLES_RUNTIME_PASSED")
	quit()
