# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const STUN: StringName = &"state.control.stun"
const CONTROL: StringName = &"state.control"
const SLOW: StringName = &"state.control.slow"
const DASH: StringName = &"ability.dash"
const DEFINITIONS: StringName = &"crimson/gameplay_tags/definitions"
const REDIRECTS: StringName = &"crimson/gameplay_tags/redirects"
const SAVE_TAG: String = "user://gameplay_tag_runtime.tres"
const SAVE_CONTAINER: String = "user://gameplay_tag_container_runtime.tres"
const SAVE_SCENE: String = "user://gameplay_tag_fields_runtime.tscn"
const SAVE_GRAPH: String = "user://gameplay_tag_runtime.crimsonscript"
const INLINE_GRAPH: String = "user://gameplay_tag_inline_runtime.crimsonscript"
const TAG_ASSET: String = "res://gameplay_tag_external_fixture.tres"
const PREVIEW: PackedScene = preload("res://tests/gameplay_tags/inspector_preview.tscn")


## Run tag and graph checks after the detached root is ready.
func _initialize() -> void:
	_verify.call_deferred()


## Reports one failed requirement to the fixture runner.
func _expect(result: bool, requirement: String) -> bool:
	if not result:
		push_error("[Crimson] Gameplay Tags fixture: " + requirement)
	return result


## Verifies gameplay queries, catalog scope, and canonical serialized references.
func _verify_tags() -> bool:
	ProjectSettings.set_setting(DEFINITIONS, PackedStringArray([STUN, SLOW, DASH]))
	var catalog: GameplayTagCatalog = GameplayTagCatalog.project()
	var stun_tag: GameplayTag = GameplayTag.find(STUN)
	var control_tag: GameplayTag = catalog.find(CONTROL)
	if not _expect(stun_tag != null and control_tag != null, "project catalog resolves declared tags and implied parents"):
		return false
	var dash_tag: GameplayTag = catalog.find(DASH)
	var standalone_catalog: GameplayTagCatalog = GameplayTagCatalog.new()
	if not _expect(standalone_catalog.find(STUN) == null and GameplayTag.from_name(&"State..Stun") == null,
			"instance catalogs are independent and malformed names fail"):
		return false
	if not _expect(stun_tag.matches(control_tag) and not stun_tag.matches_exact(control_tag)
			and stun_tag.parent().get_tag_name() == CONTROL and stun_tag.match_depth(control_tag) == 2,
			"tag methods expose exact and hierarchical behavior"):
		return false
	var values: Array[GameplayTag] = [stun_tag, dash_tag, stun_tag]
	var tag_set: GameplayTagContainer = GameplayTagContainer.from_tags(values)
	var required: GameplayTagContainer = control_tag.as_container()
	var has_control: GameplayTagQuery = GameplayTagQuery.all_tags(required)
	var lacks_explicit_control: GameplayTagQuery = GameplayTagQuery.no_tags(required, true)
	var predicates: Array[GameplayTagQuery] = [has_control, lacks_explicit_control]
	var condition: GameplayTagQuery = GameplayTagQuery.all_queries(predicates)
	required.clear()
	if not _expect(tag_set.size() == 2 and condition.matches(tag_set) and tag_set.matches_query(condition)
			and not tag_set.has_exact(control_tag), "nested queries capture an independent snapshot"):
		return false
	var empty_group: Array[GameplayTagQuery] = []
	if not _expect(not GameplayTagQuery.new().matches(tag_set) and not GameplayTagQuery.any_queries(empty_group).matches(tag_set)
			and GameplayTagQuery.all_queries(empty_group).matches(tag_set) and GameplayTagQuery.no_queries(empty_group).matches(tag_set),
			"unset queries and authored empty groups have distinct outcomes"):
		return false
	var detached: GameplayTagContainer = tag_set.copy()
	if not _expect(detached.add(control_tag) and detached.has_all_exact(control_tag.as_container())
			and detached.remove(stun_tag) and tag_set.has_exact(stun_tag),
			"container copies own their explicit tags"):
		return false
	var preview: Node = PREVIEW.instantiate()
	var single_export: bool = false
	var container_export: bool = false
	for info: Dictionary in preview.get_property_list():
		single_export = single_export or info.name == &"selected_tag" and info.hint_string == "GameplayTag"
		container_export = container_export or info.name == &"owned_tags" and info.hint_string == "GameplayTagContainer"
	preview.set(&"selected_tag", stun_tag)
	preview.set(&"owned_tags", tag_set)
	var saved_scene: PackedScene = PackedScene.new()
	var pack_status: Error = saved_scene.pack(preview)
	preview.free()
	if not _expect(single_export and container_export, "test scene exports two typed blank properties"):
		return false
	if not _expect(ResourceSaver.save(stun_tag, SAVE_TAG) == OK and ResourceSaver.save(tag_set, SAVE_CONTAINER) == OK
			and pack_status == OK and ResourceSaver.save(saved_scene, SAVE_SCENE) == OK,
			"ResourceSaver serializes both tag types independently and in scene fields"):
		return false
	ProjectSettings.set_setting(DEFINITIONS, PackedStringArray([SLOW, DASH]))
	ProjectSettings.set_setting(REDIRECTS, {STUN: SLOW})
	var loaded_tag: GameplayTag = ResourceLoader.load(SAVE_TAG, "GameplayTag", ResourceLoader.CACHE_MODE_IGNORE) as GameplayTag
	var loaded_container: GameplayTagContainer = ResourceLoader.load(SAVE_CONTAINER, "GameplayTagContainer",
			ResourceLoader.CACHE_MODE_IGNORE) as GameplayTagContainer
	var slow_tag: GameplayTag = GameplayTag.find(STUN)
	if not _expect(loaded_tag != null and loaded_container != null and slow_tag != null
			and loaded_tag.matches_exact(slow_tag) and loaded_container.has_exact(slow_tag)
			and loaded_container.get_tags()[0].get_tag_name() == SLOW and not loaded_container.has_exact(stun_tag),
			"tag and container round trips apply rename redirects and retain insertion order"):
		return false
	var reopened_scene: PackedScene = ResourceLoader.load(SAVE_SCENE, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	if not _expect(reopened_scene != null, "the preview scene with assigned fields reloads"):
		return false
	var reopened_node: Node = reopened_scene.instantiate()
	var reopened_tag: GameplayTag = reopened_node.get(&"selected_tag") as GameplayTag
	var reopened_container: GameplayTagContainer = reopened_node.get(&"owned_tags") as GameplayTagContainer
	reopened_node.free()
	if not _expect(reopened_tag != null and reopened_container != null
			and reopened_tag.matches_exact(slow_tag) and reopened_container.equals(loaded_container),
			"saved Inspector fields reload as their typed resources and resolve the rename"):
		return false
	var redirected_catalog: GameplayTagCatalog = GameplayTagCatalog.project()
	if not _expect(redirected_catalog.register_tag(&"state.control.stun.deep") == null
			and redirected_catalog.find(STUN).matches_exact(slow_tag),
			"registration cannot recreate a retired path as an implied ancestor"):
		return false
	ProjectSettings.set_setting(REDIRECTS, {STUN: &"state.missing"})
	return _expect(GameplayTag.find(STUN) == null and GameplayTag.from_name(STUN).matches_exact(stun_tag),
			"a broken redirect preserves serialized names and fails catalog lookup")


## Creates a typed graph port using native binding metadata.
func _pin(pin_id: StringName, value_type: int, native_type: StringName = &"") -> Dictionary:
	var info: Dictionary = {"id": pin_id, "type": value_type, "label": String(pin_id)}
	if not native_type.is_empty():
		info["class"] = native_type
	return info


## Creates the graph representation of a native static or instance call.
func _call(node_id: int, native_type: StringName, method_name: StringName, args: Array,
		output: Dictionary, values: Dictionary = {}, is_static: bool = false) -> Dictionary:
	var member_info: Dictionary = {"class": native_type, "name": method_name, "inputs": args}
	if not output.is_empty():
		member_info["output"] = output
	if is_static:
		member_info["static"] = true
	return {"id": node_id, "kind": &"call_method", "member": member_info, "values": values}


## Records one stable execution or data edge.
func _wire(start_id: int, output_pin: StringName, end_id: int, input_pin: StringName) -> Dictionary:
	return {"from": start_id, "from_pin": output_pin, "to": end_id, "to_pin": input_pin}


## Records a call that leaves an observable result on the attached node.
func _write_meta(node_id: int, marker: StringName) -> Dictionary:
	return _call(node_id, &"Object", &"set_meta",
		[_pin(&"arg_0", TYPE_STRING_NAME), _pin(&"arg_1", TYPE_NIL)], {}, {&"arg_0": marker})


## Runs real Visual Script nodes with the same static calls and typed ports as the picker.
func _verify_graph() -> bool:
	ProjectSettings.set_setting(REDIRECTS, {})
	var tag_type: StringName = &"GameplayTag"
	var container_type: StringName = &"GameplayTagContainer"
	var query_type: StringName = &"GameplayTagQuery"
	var tag_output: Dictionary = _pin(&"result", TYPE_OBJECT, tag_type)
	var container_output: Dictionary = _pin(&"result", TYPE_OBJECT, container_type)
	var array_output: Dictionary = _pin(&"result", TYPE_ARRAY)
	array_output["element"] = {"type": TYPE_OBJECT, "class": tag_type}
	var tag_array_input: Dictionary = array_output.duplicate(true)
	tag_array_input["id"] = &"arg_0"
	var exact_default: Dictionary = _pin(&"arg_1", TYPE_BOOL)
	exact_default["default"] = false
	var nodes: Array[Dictionary] = [
		{"id": 1, "kind": &"ready"},
		_call(2, tag_type, &"from_name", [_pin(&"arg_0", TYPE_STRING_NAME)], tag_output, {&"arg_0": STUN}, true),
		_call(3, tag_type, &"from_name", [_pin(&"arg_0", TYPE_STRING_NAME)], tag_output, {&"arg_0": CONTROL}, true),
		_call(4, tag_type, &"as_container", [], container_output),
		_call(5, tag_type, &"as_container", [], container_output),
		_call(6, container_type, &"get_tags", [], array_output),
		_call(7, container_type, &"from_tags", [tag_array_input], container_output, {&"arg_0": PackedStringArray([DASH])}, true),
		_call(8, query_type, &"all_tags", [_pin(&"arg_0", TYPE_OBJECT, container_type), exact_default],
			_pin(&"result", TYPE_OBJECT, query_type), {}, true),
		_call(9, container_type, &"matches_query", [_pin(&"arg_0", TYPE_OBJECT, query_type)], _pin(&"result", TYPE_BOOL)),
		_write_meta(10, &"query_passed"),
		{"id": 11, "kind": &"foreach"},
		_call(12, tag_type, &"get_tag_name", [], _pin(&"result", TYPE_STRING_NAME)),
		_write_meta(13, &"visited_tag"),
	]
	var wires: Array[Dictionary] = [
		_wire(2, &"result", 4, &"target"), _wire(3, &"result", 5, &"target"),
		_wire(4, &"result", 6, &"target"), _wire(6, &"result", 7, &"arg_0"),
		_wire(5, &"result", 8, &"arg_0"), _wire(7, &"result", 9, &"target"),
		_wire(8, &"result", 9, &"arg_0"), _wire(9, &"result", 10, &"arg_1"),
		_wire(6, &"result", 11, &"array"), _wire(11, &"body", 12, &"in"),
		_wire(11, &"item", 12, &"target"), _wire(12, &"then", 13, &"in"), _wire(12, &"result", 13, &"arg_1"),
	]
	for node_id: int in range(1, 11):
		wires.append(_wire(node_id, &"then", node_id + 1, &"in"))
	return _run_graph(nodes, wires, SAVE_GRAPH, {&"query_passed": true, &"visited_tag": STUN})


## Covers inline tag, container and array defaults, redirects, and a legacy external asset.
func _verify_inline_defaults() -> bool:
	ProjectSettings.set_setting(REDIRECTS, {STUN: SLOW})
	if not _expect(ResourceSaver.save(GameplayTag.from_name(SLOW), TAG_ASSET) == OK, "create an external tag fixture"):
		return false
	var tag_type: StringName = &"GameplayTag"
	var container_type: StringName = &"GameplayTagContainer"
	var tag_input: Dictionary = _pin(&"arg_0", TYPE_OBJECT, tag_type)
	var array_input: Dictionary = _pin(&"arg_0", TYPE_ARRAY)
	array_input["element"] = {"type": TYPE_OBJECT, "class": tag_type}
	var result_pin: Dictionary = _pin(&"result", TYPE_BOOL)
	var nodes: Array[Dictionary] = [
		{"id": 1, "kind": &"ready"},
		_call(2, container_type, &"from_tags", [array_input], _pin(&"result", TYPE_OBJECT, container_type),
			{&"arg_0": PackedStringArray([STUN])}, true),
		_call(3, container_type, &"has_exact", [tag_input], result_pin, {&"arg_0": SLOW}),
		_write_meta(4, &"array_default"),
		_call(5, container_type, &"remove", [tag_input], result_pin,
			{&"target": PackedStringArray([STUN]), &"arg_0": SLOW}),
		_write_meta(6, &"removed_from_literal"),
		_call(7, container_type, &"has_exact", [tag_input], result_pin,
			{&"target": PackedStringArray([STUN]), &"arg_0": {"path": TAG_ASSET, "uid": ""}}),
		_write_meta(8, &"external_default"),
		_call(9, tag_type, &"matches", [tag_input], result_pin, {&"target": STUN, &"arg_0": CONTROL}),
		_write_meta(10, &"single_default"),
	]
	var wires: Array[Dictionary] = [
		_wire(2, &"result", 3, &"target"),
		_wire(3, &"result", 4, &"arg_1"), _wire(5, &"result", 6, &"arg_1"),
		_wire(7, &"result", 8, &"arg_1"), _wire(9, &"result", 10, &"arg_1"),
	]
	for node_id: int in range(1, 10):
		wires.append(_wire(node_id, &"then", node_id + 1, &"in"))
	return _run_graph(nodes, wires, INLINE_GRAPH, {
		&"array_default": true, &"removed_from_literal": true, &"external_default": true, &"single_default": true,
	})


## Saves and reloads a graph, then checks the same literal data in independent instances.
func _run_graph(nodes: Array[Dictionary], wires: Array[Dictionary], storage_path: String, markers: Dictionary) -> bool:
	var flow_script: Script = ClassDB.instantiate(&"VisualScript") as Script
	flow_script.call(&"set_graph_base", &"Node")
	flow_script.call(&"set_graph", {"version": 1, "nodes": nodes, "connections": wires})
	if not _expect(flow_script.can_instantiate(), "typed native Gameplay Tag graph compiles"):
		return false
	if not _expect(ResourceSaver.save(flow_script, storage_path) == OK, "typed tag graph saves independently"):
		return false
	var saved_script: Script = ResourceLoader.load(storage_path, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _expect(saved_script != null and saved_script.can_instantiate(), "typed tag graph reloads"):
		return false
	for instance_number: int in range(2):
		var graph_node: Node = Node.new()
		graph_node.set_script(saved_script)
		root.add_child(graph_node)
		for marker: StringName in markers:
			var actual: Variant = graph_node.get_meta(marker, false)
			if not _expect(actual == markers[marker], String(marker) + " result on graph instance " + str(instance_number)):
				graph_node.free()
				return false
		graph_node.free()
	return true


## Exits with the same result in debug and release.
func _verify() -> void:
	if not _verify_tags() or not _verify_graph() or not _verify_inline_defaults():
		quit(1)
		return
	print("CRIMSON_GAMEPLAY_TAGS_RUNTIME_PASSED")
	quit()
