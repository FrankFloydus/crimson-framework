# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

## Write a disposable resource to the staging project so the editor opens it normally.
const GRAPH_PATH := "res://editor_smoke.crimsonscript"
const SCENE_PATH := "res://editor_smoke_signals.tscn"
const ALTERNATE_GRAPH_PATH := "res://editor_smoke_other.crimsonscript"
const EXTERNAL_GRAPH_PATH := "res://editor_smoke_external.crimsonscript"
## Saved script to test a typed member lookup on a scene reference.
const OTHER_SCRIPT_PATH := "res://editor_smoke_script_member.gd"
## Allow the editor to debounce disk writes without depending on its exact delay.
const AUTOSAVE_TIMEOUT_SECONDS := 4.0


## Wait for Godot and Crimson to finish attaching their editor plugins.
func _enter_tree() -> void:
	_verify.call_deferred()


## Report a failure and stop the isolated editor process.
func _require(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("[Crimson] Visual Script editor fixture: " + requirement)
		get_tree().quit(1)
	return condition


## Use the native editor opener and exercise the palette's normal selection signals.
func _verify() -> void:
	await get_tree().process_frame
	for _scan_frame: int in range(240):
		if not EditorInterface.get_resource_filesystem().is_scanning():
			break
		await get_tree().process_frame
	await get_tree().process_frame
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _require(graph_script != null and ResourceSaver.save(graph_script, GRAPH_PATH) == OK, "Save an empty Visual Script."):
		return
	var saved_script := ResourceLoader.load(GRAPH_PATH, "Script") as Script
	if not _require(saved_script != null, "Open the saved Visual Script resource."):
		return
	if not _open_signal_source_scene(saved_script):
		return
	await get_tree().process_frame
	await get_tree().process_frame
	EditorInterface.edit_script(saved_script, -1, 0, true)
	await get_tree().process_frame
	var graph_window := get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _require(graph_window != null and graph_window.visible, "Godot opens the Crimson graph editor."):
		return
	var canvas := graph_window.find_child("GraphCanvas", true, false) as GraphEdit
	var search_field := graph_window.find_child("GraphPaletteSearch", true, false) as LineEdit
	var result_tree := graph_window.find_child("GraphPaletteResults", true, false) as Tree
	if not _require(canvas != null and search_field != null and result_tree != null, "The editor exposes its graph canvas and searchable palette."):
		return
	if not await _verify_picker_keyboard(graph_window, canvas, search_field, saved_script):
		return
	saved_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _insert(canvas, search_field, result_tree, "On Ready", Vector2(64, 64)):
		return
	if not await _insert_with_click(canvas, search_field, result_tree, "Print", Vector2(384, 64), saved_script):
		return
	await _settle_canvas(canvas)
	var ready_node := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var print_node := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(ready_node != null and print_node != null, "Palette selection creates both native graph widgets."):
		return
	canvas.connection_request.emit(ready_node.name, 0, print_node.name, 0)
	await get_tree().process_frame
	var graph_data: Dictionary = saved_script.call(&"get_graph")
	if not _require(graph_data.get("connections", []).size() == 1, "Dragging a wire updates the graph resource."):
		return
	# The headless editor has a fixed small viewport. Native tests cover the stock docks;
	# hide them here so the larger multi-card fixture can hit every socket and title.
	EditorInterface.set_distraction_free_mode(true)
	await _settle_canvas(canvas)
	if not await _verify_wire_restoration(graph_window, canvas, saved_script):
		return
	if not await _verify_rewire(graph_window, canvas, saved_script) or not await _verify_extraction(graph_window, canvas, saved_script):
		return
	if not await _verify_context_routing(graph_window, canvas, result_tree, saved_script):
		return
	if not await _verify_delete_key(graph_window, canvas, saved_script):
		return
	if not await _verify_clipboard_commands(graph_window, canvas, saved_script):
		return
	if not await _verify_dependency_selection(graph_window, canvas, saved_script):
		return
	if not await _verify_comment_frame(graph_window, canvas, saved_script):
		return
	if not await _verify_wire_commands(graph_window, canvas, saved_script):
		return
	if not await _verify_navigation(graph_window, canvas, saved_script):
		return
	if not await _verify_pin_commands(graph_window, canvas, saved_script):
		return
	if not await _verify_wire_gestures(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_pointer_interruptions(graph_window, canvas, saved_script):
		return
	if not await _verify_quick_connections(graph_window, canvas, saved_script):
		return
	if not await _verify_wire_exchange(graph_window, canvas, saved_script):
		return
	if not await _verify_shake_bypass(graph_window, canvas, saved_script):
		return
	if not await _verify_auto_insert(graph_window, canvas, saved_script):
		return
	if not await _verify_declarations(graph_window, canvas, saved_script):
		return
	if not await _verify_promotion(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_variable_drop(graph_window, canvas, saved_script):
		return
	if not await _verify_variable_pin_drop(graph_window, canvas, saved_script):
		return
	if not await _verify_enum_variable_drop(graph_window, canvas, saved_script):
		return
	if not await _verify_declaration_reorder(graph_window, canvas, saved_script):
		return
	if not await _verify_pin_promotion(graph_window, canvas, saved_script):
		return
	if not await _verify_macro_signature(graph_window, saved_script):
		return
	if not await _verify_wired_signal(canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_method_help(canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_builtin_method_picker(canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_global_constant_picker(canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_context_sensitive_picker(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_reroute_drag(graph_window, canvas, saved_script):
		return
	if not await _verify_inline_pin_literals(graph_window, canvas, saved_script):
		return
	if not await _verify_reroute_fanout(graph_window, canvas, saved_script):
		return
	if not await _verify_straighten_connections(graph_window, canvas, saved_script):
		return
	if not await _verify_create_custom_event(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_enum_switch(canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_collection_loop_types(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_asset_field(graph_window, canvas, saved_script):
		return
	if not await _verify_canvas_asset_drop(graph_window, canvas, saved_script):
		return
	if not await _verify_node_ports(canvas, saved_script):
		return
	if not await _verify_property_receiver(graph_window, canvas, saved_script):
		return
	if not await _verify_variable_node_details(graph_window, canvas, saved_script):
		return
	if not await _verify_scene_signals(graph_window, canvas, saved_script):
		return
	if not await _verify_script_member_picker(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_autosave(graph_window, canvas, search_field, result_tree, saved_script):
		return
	if not await _verify_file_prompts(graph_window, saved_script):
		return
	print("CRIMSON_VISUAL_SCRIPT_EDITOR_PASSED")
	get_tree().quit()


## Save through ordinary graph edits, including undo and redo, and protect an externally changed file.
func _verify_autosave(workspace: Control, canvas: GraphEdit, picker_search: LineEdit,
		picker_results: Tree, graph_script: Script) -> bool:
	var baseline := {"version": 1, "nodes": [], "connections": []}
	graph_script.call(&"set_graph", baseline)
	await _settle_canvas(canvas)
	await _shortcut(workspace, canvas, KEY_S)
	var initial_graph: Dictionary = graph_script.call(&"get_graph")
	if not await _insert(canvas, picker_search, picker_results, "On Ready", Vector2(48, 56)):
		return false
	var edited_graph: Dictionary = graph_script.call(&"get_graph")
	if not _require(edited_graph != initial_graph and await _wait_for_disk_graph(GRAPH_PATH, edited_graph),
			"Creating a node writes the graph automatically without manually saving."):
		return false
	await _shortcut(workspace, canvas, KEY_Z)
	if not _require(await _wait_for_disk_graph(GRAPH_PATH, initial_graph), "Undo also updates the saved graph automatically."):
		return false
	await _shortcut(workspace, canvas, KEY_Y)
	if not _require(await _wait_for_disk_graph(GRAPH_PATH, edited_graph), "Redo also updates the saved graph automatically."):
		return false
	var external_script := ClassDB.instantiate(&"VisualScript") as Script
	external_script.call(&"set_graph", {"version": 1, "nodes": [{"id": 1, "kind": &"print", "message": "External edit"}], "connections": []})
	if not _require(ResourceSaver.save(external_script, EXTERNAL_GRAPH_PATH) == OK, "Write a separate graph for the external edit."):
		return false
	var external_bytes := FileAccess.get_file_as_bytes(EXTERNAL_GRAPH_PATH)
	var external_file := FileAccess.open(GRAPH_PATH, FileAccess.WRITE)
	if not _require(external_file != null and not external_bytes.is_empty(), "Open the graph path for an external write."):
		return false
	external_file.store_buffer(external_bytes)
	external_file.close()
	var external_graph := _read_disk_graph(GRAPH_PATH)
	if not await _insert(canvas, picker_search, picker_results, "Print", Vector2(272, 56)):
		return false
	var local_graph: Dictionary = graph_script.call(&"get_graph")
	await get_tree().create_timer(1.0).timeout
	if not _require(local_graph != external_graph and graph_script.call(&"get_graph") == local_graph and
			FileAccess.get_file_as_bytes(GRAPH_PATH) == external_bytes,
			"Autosave preserves edits on disk from another process and keeps the local draft available."):
		return false
	if not await _reload_from_disk(workspace):
		return false
	var reload_prompt := workspace.find_child("VisualScriptReload", true, false) as Control
	if not _require(reload_prompt != null and reload_prompt.visible, "Reload asks before discarding the draft after a disk conflict."):
		return false
	var confirm_reload := reload_prompt.find_child("ConfirmDialog", true, false) as Button
	if not _require(confirm_reload != null, "Find the Reload from Disk confirmation."):
		return false
	confirm_reload.pressed.emit()
	await get_tree().process_frame
	if not _require(not reload_prompt.visible and graph_script.call(&"get_graph") == external_graph,
			"Confirm Reload reads the external graph without overwriting it."):
		return false
	if not await _insert(canvas, picker_search, picker_results, "On Ready", Vector2(48, 56)):
		return false
	var changed_after_reload: Dictionary = graph_script.call(&"get_graph")
	return _require(await _wait_for_disk_graph(GRAPH_PATH, changed_after_reload), "Reloading resolves the conflict and allows later edits to autosave.")


## Wait for the saved graph to match an observed editor change.
func _wait_for_disk_graph(path: String, expected: Dictionary) -> bool:
	var deadline := Time.get_ticks_msec() + int(AUTOSAVE_TIMEOUT_SECONDS * 1000.0)
	while Time.get_ticks_msec() < deadline:
		if _read_disk_graph(path) == expected:
			return true
		await get_tree().create_timer(0.025).timeout
	return false


## Read the resource from disk without resolving the live cached script.
func _read_disk_graph(path: String) -> Dictionary:
	var disk_script := ResourceLoader.load(path, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	return disk_script.call(&"get_graph") if disk_script != null else {}


## Invoke Reload from Disk from its Graph Tools command.
func _reload_from_disk(workspace: Control) -> bool:
	var graph_tools := workspace.find_child("GraphTools", true, false) as MenuButton
	if not _require(graph_tools != null, "Find Graph Tools in the toolbar."):
		return false
	graph_tools.show_popup()
	return await _choose_menu_item(graph_tools.get_popup(), "Reload from Disk")


## Check that file prompts share the workbench and perform only the chosen action.
func _verify_file_prompts(graph_window: Control, graph_script: Script) -> bool:
	var initial_graph: Dictionary = graph_script.call(&"get_graph")
	var original_graph := initial_graph.duplicate(true)
	original_graph.nodes.append({"id": 41, "kind": &"print", "message": "Unsaved fixture draft"})
	graph_script.call(&"set_graph", original_graph)
	await get_tree().process_frame
	original_graph = graph_script.call(&"get_graph")
	var reload_prompt := graph_window.find_child("VisualScriptReload", true, false) as Control
	var switch_prompt := graph_window.find_child("VisualScriptSaveBeforeOpen", true, false) as Control
	var alternate_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _require(reload_prompt != null and switch_prompt != null and alternate_script != null,
			"Find shared graph prompts and create a second graph."):
		return false
	alternate_script.call(&"set_graph_base", &"Camera3D")
	if not _require(ResourceSaver.save(alternate_script, ALTERNATE_GRAPH_PATH) == OK, "Save the second graph with a different Godot base class."):
		return false
	alternate_script = ResourceLoader.load(ALTERNATE_GRAPH_PATH, "Script") as Script
	if not _require(alternate_script != null and alternate_script.resource_path == ALTERNATE_GRAPH_PATH,
			"Open the second graph from disk with its saved resource path."):
		return false
	var cancel_reload := reload_prompt.find_child("CancelDialog", true, false) as Button
	var confirm_reload := reload_prompt.find_child("ConfirmDialog", true, false) as Button
	if not _require(cancel_reload != null and confirm_reload != null, "Find the reload confirmation commands."):
		return false
	if not await _reload_from_disk(graph_window):
		return false
	if not _require(reload_prompt.visible and graph_window.get_window().gui_get_focus_owner() == cancel_reload,
			"Dirty Reload opens with Cancel selected. Visible: %s. Focus: %s." %
			[reload_prompt.visible, graph_window.get_window().gui_get_focus_owner()]):
		return false
	cancel_reload.pressed.emit()
	if not _require(not reload_prompt.visible and graph_script.call(&"get_graph") == original_graph,
			"Cancel closes Reload and keeps the draft intact."):
		return false
	if not await _reload_from_disk(graph_window):
		return false
	confirm_reload.pressed.emit()
	await get_tree().process_frame
	var saved_body: Dictionary = graph_script.call(&"get_graph")
	if not _require(not reload_prompt.visible and saved_body != original_graph, "Confirm Reload discards only the unsaved draft."):
		return false
	graph_script.call(&"set_graph", original_graph)
	EditorInterface.edit_script(alternate_script, -1, 0, true)
	await get_tree().process_frame
	var discard_edits := switch_prompt.find_child("AlternateDialog", true, false) as Button
	var save_and_open := switch_prompt.find_child("ConfirmDialog", true, false) as Button
	if not _require(switch_prompt.visible and discard_edits != null and save_and_open != null,
		"Opening another graph offers Cancel, Discard Edits, and Save and Open."):
		return false
	var prompt_panel := switch_prompt.get_node(^"DialogPanel") as Control
	await get_tree().process_frame
	if not _require(prompt_panel.get_rect().end.x <= switch_prompt.size.x and prompt_panel.position.x > 0,
		"The three-action prompt fits when the workbench is resized."):
		return false
	discard_edits.pressed.emit()
	await get_tree().process_frame
	if not _require(not switch_prompt.visible and graph_script.call(&"get_graph") == saved_body,
		"Discard Edits switches resources and restores the prior saved graph."):
		return false
	var event_root := graph_window.find_child("GraphEventRoot", true, false) as Button
	if not _require(event_root != null and event_root.disabled, "The Event Graph breadcrumb identifies the root of the second script."):
		return false
	EditorInterface.edit_script(graph_script, -1, 0, true)
	await get_tree().process_frame
	graph_script.call(&"set_graph", original_graph)
	EditorInterface.edit_script(alternate_script, -1, 0, true)
	await get_tree().process_frame
	if not _require(switch_prompt.visible, "Reopening the second graph asks before saving the new draft."):
		return false
	save_and_open.pressed.emit()
	await get_tree().process_frame
	var saved_copy := ResourceLoader.load(GRAPH_PATH, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	return _require(not switch_prompt.visible and saved_copy != null and saved_copy.call(&"get_graph") == original_graph,
		"Save and Open writes the current graph before opening the next one.")


## Open an authored scene with the exact script instance and a typed signal source.
func _open_signal_source_scene(graph_script: Script) -> bool:
	var scene_root := Node.new()
	scene_root.name = "SignalSource"
	scene_root.set_script(graph_script)
	var slider := HSlider.new()
	slider.name = "Volume"
	scene_root.add_child(slider)
	slider.owner = scene_root
	var controller_script := GDScript.new()
	controller_script.source_code = "extends Node\n\n## Sent when power changes.\nsignal power_changed(charge: int)\n\nvar charge_units: int = 14\n\nfunc refill(amount: int = 4) -> int:\n\tcharge_units += amount\n\treturn charge_units\n"
	if not _require(controller_script.reload() == OK and ResourceSaver.save(controller_script, OTHER_SCRIPT_PATH) == OK,
			"Save a fixture with a typed property, method and signal."):
		scene_root.free()
		return false
	var controller := Node.new()
	controller.name = "ControllerFixture"
	controller.set_script(ResourceLoader.load(OTHER_SCRIPT_PATH, "Script"))
	scene_root.add_child(controller)
	controller.owner = scene_root
	var scene_asset := PackedScene.new()
	var packed := scene_asset.pack(scene_root)
	scene_root.free()
	if not _require(packed == OK and ResourceSaver.save(scene_asset, SCENE_PATH) == OK, "Save a scene with a typed signal source."):
		return false
	EditorInterface.open_scene_from_path(SCENE_PATH)
	return true


## Use a scene reference to add members declared by a separate GDScript without manually specifying its type.
func _verify_script_member_picker(graph_window: Control, canvas: GraphEdit, search_field: LineEdit,
		result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _verify_scene_dock_drop(graph_window, canvas, graph_script):
		return false
	var reference_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var reference_caption := reference_card.find_child("GraphSceneReferenceName", true, false) as Label if reference_card else null
	if not _require(reference_caption != null and reference_caption.text == "ControllerFixture" and
			reference_caption.tooltip_text.contains("Node") and reference_card.tooltip_text.contains("Node"),
			"A scene reference shows its actual scene name and exposes its native class on hover."):
		return false
	for example: Dictionary in [
		{"query": "Refill", "kind": &"call_method", "name": &"refill"},
		{"query": "Get Charge Units", "kind": &"get_property", "name": &"charge_units"},
		{"query": "Set Charge Units", "kind": &"set_property", "name": &"charge_units"},
		{"query": "On Power Changed", "kind": &"native_signal", "name": &"power_changed"},
	]:
		var source := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
		if not _require(source != null and source.get_output_port_count() == 1, "A scripted scene reference has a typed data output."):
			return false
		canvas.connection_to_empty.emit(source.name, 0, Vector2(384, 160))
		if example.name == &"refill":
			var direct := _find_entry(result_tree, "Refill")
			var native_group := _find_entry(result_tree, "Node")
			if not _require(direct != null and direct.get_parent() == result_tree.get_root() and
					native_group != null and native_group.collapsed and native_group.get_child_count() > 0,
					"The script action is directly visible and the native Node category starts collapsed."):
				return false
		if not await _choose_palette_item(search_field, result_tree, example.query):
			return false
		var document: Dictionary = graph_script.call(&"get_graph")
		var placed: Dictionary = document.nodes.back()
		var binding: Dictionary = placed.get("member", placed)
		var member_name: StringName = binding.get("name", binding.get("signal", &""))
		if not _require(placed.kind == example.kind and member_name == example.name and
				binding.script_path == OTHER_SCRIPT_PATH and _wire_present(graph_script, 1, &"node", placed.id, &"target"),
				"The selected method, property or signal knows the script that declared it and the receiver."):
			return false
	return true


## Pass the same active-scene and absolute-path payload authored by the stock Scene dock.
func _verify_scene_dock_drop(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var scene := EditorInterface.get_edited_scene_root()
	var controller := scene.get_node(^"ControllerFixture")
	var target := canvas.get_global_transform_with_canvas() * Vector2(264, 272)
	for entry: Dictionary in [
		{"path": NodePath(str(controller.get_path()) + "/Missing"), "node_count": 0},
		{"path": controller.get_path(), "node_count": 1},
	]:
		canvas.force_drag({"type": "nodes", "nodes": [entry.path], "scene_root": scene}, null)
		var motion := InputEventMouseMotion.new()
		motion.position = target
		motion.global_position = target
		graph_window.get_window().push_input(motion, true)
		await _click_graph(graph_window, target, MOUSE_BUTTON_LEFT)
		var document: Dictionary = graph_script.call(&"get_graph")
		if not _require(document.nodes.size() == entry.node_count,
				"The graph accepts only resolved nodes in the active Scene dock payload."):
			return false
	var document: Dictionary = graph_script.call(&"get_graph")
	var dropped: Dictionary = document.nodes.back()
	return _require(dropped.kind == &"scene_node" and dropped.label == &"ControllerFixture" and
			dropped.source == scene.get_path_to(controller),
			"A Scene dock drag creates a named, typed and relative scene reference.")


## Check the scene disclosure, native layout and focus, and authored signal payload.
func _verify_scene_signals(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var scene := EditorInterface.get_edited_scene_root()
	var volume := scene.get_node_or_null(^"Volume")
	if not _require(volume != null, "Find the Volume slider in the generated scene."):
		return false
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
	await _settle_canvas(canvas)
	var target := canvas.get_global_transform_with_canvas() * (canvas.size * 0.5)
	canvas.force_drag({"type": "nodes", "nodes": [volume.get_path()], "scene_root": scene}, null)
	await _click_graph(graph_window, target, MOUSE_BUTTON_LEFT)
	await _settle_canvas(canvas)
	var volume_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(volume_card != null, "Dropping Volume creates its scene reference."):
		return false
	volume_card.selected = true
	var disclosure := graph_window.find_child("VisualScriptSignalsDisclosure", true, false) as Button
	var signal_rows := graph_window.find_child("VisualScriptSignalRows", true, false) as VBoxContainer
	var add_signal := graph_window.find_child("AddSignal_value_changed", true, false) as Button
	if not _require(disclosure != null and signal_rows != null and add_signal != null and
			not disclosure.button_pressed and not signal_rows.is_visible_in_tree(),
			"The scene's signal actions start under a collapsed disclosure."):
		return false
	disclosure.button_pressed = true
	await get_tree().process_frame
	if not _require(signal_rows.is_visible_in_tree(), "Expanding Signals reveals its actions."):
		return false
	var signal_label := add_signal.get_parent().get_child(0) as Label
	if not _require(signal_label != null and signal_label.text == "On Value Changed" and
			signal_label.horizontal_alignment == HORIZONTAL_ALIGNMENT_LEFT and
			signal_label.position.x < add_signal.position.x and
			is_equal_approx(add_signal.position.x + add_signal.size.x, (add_signal.get_parent() as Control).size.x) and
			add_signal.accessibility_name == "Add On Value Changed",
			"Signals have readable left text and an accessible right-aligned add control."):
		return false
	if not _require(signal_label.tooltip_text.begins_with("On Value Changed") and
			signal_label.tooltip_text.contains("Emitted when value changes.") and
			add_signal.tooltip_text == signal_label.tooltip_text,
			"The signal name and add action expose the same native member help."):
		return false

	disclosure.button_pressed = false
	await get_tree().process_frame
	if not _require(not disclosure.button_pressed and not add_signal.is_visible_in_tree(),
			"The native disclosure toggle folds its signal actions."):
		return false
	volume_card.selected = false
	volume_card.selected = true
	await get_tree().process_frame
	disclosure = graph_window.find_child("VisualScriptSignalsDisclosure", true, false) as Button
	add_signal = graph_window.find_child("AddSignal_value_changed", true, false) as Button
	if not _require(disclosure != null and add_signal != null and not add_signal.is_visible_in_tree(),
			"Changing graph selection preserves the Signals disclosure."):
		return false
	disclosure.grab_focus()
	var confirm := InputEventKey.new()
	confirm.keycode = KEY_SPACE
	confirm.pressed = true
	graph_window.get_window().push_input(confirm)
	confirm = confirm.duplicate() as InputEventKey
	confirm.pressed = false
	graph_window.get_window().push_input(confirm)
	await get_tree().process_frame
	if not _require(disclosure.has_focus() and add_signal.is_visible_in_tree(),
			"Keyboard Confirm opens Signals and leaves focus on the disclosure."):
		return false

	# Focus the row so the Details scroll container brings its action into view.
	add_signal.grab_focus()
	await get_tree().process_frame
	add_signal.pressed.emit()
	await get_tree().process_frame
	var document: Dictionary = graph_script.call(&"get_graph")
	var nodes: Array = document.get("nodes", [])
	if not _require(not nodes.is_empty() and canvas.has_focus(), "Adding a scene signal returns focus to its graph."):
		return false
	var inserted_signal: Dictionary = nodes.back()
	return _require(inserted_signal.get("kind") == &"native_signal" and
			inserted_signal.get("source") == ^"Volume" and inserted_signal.get("signal") == &"value_changed",
			"The plus action inserts the selected scene node's typed signal.")


## Search from the canvas and insert the exact selected palette action.
func _insert(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, query: String, anchor: Vector2) -> bool:
	canvas.popup_request.emit(anchor)
	return await _choose_palette_item(search_field, result_tree, query)


## Verify that a real single left click places a result, while search alone does not.
func _insert_with_click(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, query: String, anchor: Vector2, graph_script: Script) -> bool:
	canvas.popup_request.emit(anchor)
	search_field.text = query
	search_field.text_changed.emit(query)
	await get_tree().process_frame
	var result := result_tree.get_selected()
	if not _require(result != null and result.get_text(0) == query and graph_script.call(&"get_graph").nodes.size() == 1,
		"Search highlights the matching result without placing anything."):
		return false
	result_tree.scroll_to_item(result)
	await get_tree().process_frame
	var popup := result_tree.get_window()
	var local_hit := result_tree.get_item_area_rect(result, 0).get_center()
	result_tree.item_mouse_selected.emit(local_hit, MOUSE_BUTTON_LEFT)
	await get_tree().process_frame
	return _require(not popup.is_visible() and graph_script.call(&"get_graph").nodes.size() == 2,
		"The native Tree left-click signal places exactly one node and closes the picker.")


## Search the currently open free or wire-context palette and accept the result.
func _choose_palette_item(search_field: LineEdit, result_tree: Tree, query: String) -> bool:
	search_field.text = query
	search_field.text_changed.emit(query)
	var selected_item := result_tree.get_selected()
	if not _require(selected_item != null and selected_item.get_text(0) == query, "Palette finds exact action %s." % query):
		return false
	search_field.text_submitted.emit(query)
	await get_tree().process_frame
	return true


## Locate an enabled command by its displayed label so native menu item IDs can change.
func _menu_item_index(menu: PopupMenu, label: String) -> int:
	for item_index in range(menu.item_count):
		if not menu.is_item_disabled(item_index) and menu.get_item_text(item_index) == label:
			return item_index
	return -1


## Activate the same signal Godot emits when an entry in the popup is chosen.
func _choose_menu_item(menu: PopupMenu, label: String) -> bool:
	if not _require(menu != null and menu.visible, "Open the native menu before choosing %s." % label):
		return false
	var item_index := _menu_item_index(menu, label)
	if not _require(item_index >= 0, "The native menu offers %s." % label):
		return false
	menu.id_pressed.emit(menu.get_item_id(item_index))
	if menu.visible:
		menu.hide()
	await get_tree().process_frame
	return true


## Return from a function or macro using the Event Graph breadcrumb.
func _open_event_graph(workspace: Control) -> bool:
	var event_root := workspace.find_child("GraphEventRoot", true, false) as Button
	if not _require(event_root != null and not event_root.disabled, "The Event Graph breadcrumb is actionable from a subgraph."):
		return false
	event_root.pressed.emit()
	await get_tree().process_frame
	return _require(event_root.disabled, "The Event Graph breadcrumb identifies the active root after returning.")


## Open the menu on an actual card instead of routing its command through the picker.
func _choose_node_command(graph_window: Control, canvas: GraphEdit, node_id: int, label: String) -> bool:
	var widget := canvas.get_node_or_null(NodePath("GraphNode_%d" % node_id)) as GraphNode
	if not _require(widget != null, "Find card %d for %s." % [node_id, label]):
		return false
	await _right_click_card(graph_window, canvas, widget)
	return await _choose_menu_item(graph_window.find_child("GraphContextMenu", true, false) as PopupMenu, label)


## A native context click must hit either a titlebar or a compact card body.
func _right_click_card(graph_window: Control, canvas: GraphEdit, widget: GraphNode) -> void:
	var titlebar := widget.get_titlebar_hbox()
	var context_region: Control = titlebar if titlebar.visible else widget
	var point := context_region.get_global_rect().get_center()
	var canvas_rect := canvas.get_global_rect()
	if not canvas_rect.grow(-16).has_point(point):
		canvas.scroll_offset += point - canvas_rect.get_center()
		await get_tree().process_frame
		point = context_region.get_global_rect().get_center()
	await _click_graph(graph_window, point, MOUSE_BUTTON_RIGHT)


## Verify that blank space and node bodies offer distinct menus, including disable and re-enable.
func _verify_context_routing(graph_window: Control, canvas: GraphEdit, result_tree: Tree, graph_script: Script) -> bool:
	var initial := _operation_graph()
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [2, 3]):
		return false
	var context_menu := graph_window.find_child("GraphContextMenu", true, false) as PopupMenu
	var first_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(context_menu != null and first_card != null, "Find the shared context menu and selected card."):
		return false
	await _right_click_card(graph_window, canvas, first_card)
	if not _require(context_menu.visible and not result_tree.is_visible_in_tree() and _selected_ids(canvas) == [2, 3],
		"Right clicking either selected card opens the node menu and preserves the selection."):
		return false
	for label in ["Delete", "Cut", "Copy", "Duplicate", "Collapse to Function", "Collapse to Macro"]:
		if not _require(_menu_item_index(context_menu, label) >= 0, "The node menu offers %s." % label):
			return false
	if not await _choose_menu_item(context_menu, "Duplicate"):
		return false
	var current_graph: Dictionary = graph_script.call(&"get_graph")
	if not _require(current_graph.nodes.size() == 7 and _wire_present(graph_script, 6, &"then", 7, &"in"),
		"The node menu duplicates the full selection and its internal link."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	await _settle_canvas(canvas)

	var blank_point := canvas.get_global_transform_with_canvas() * Vector2(128, 340)
	await _click_graph(graph_window, blank_point, MOUSE_BUTTON_RIGHT)
	if not _require(not context_menu.visible and result_tree.is_visible_in_tree(), "Right clicking blank canvas opens the node creation picker."):
		return false
	var items: Array[TreeItem] = [result_tree.get_root()]
	var offers_custom_event := false
	while not items.is_empty():
		var row: TreeItem = items.pop_back()
		var row_data: Variant = row.get_metadata(0)
		if row_data is Dictionary:
			if not _require(not row_data.has("graph_tool") and not row_data.has("wire_action") and not row_data.has("layout") and
				not row_data.has("collapse") and not row_data.has("navigation"), "The creation picker excludes graph command %s." % row.get_text(0)):
				return false
			offers_custom_event = offers_custom_event or row.get_text(0) == "Add Custom Event"
		items.append_array(row.get_children())
	if not _require(offers_custom_event, "The Event Graph offers Add Custom Event before typing a query."):
		return false
	var picker_window := result_tree.get_window()
	picker_window.hide()

	if not _select(canvas, [3]) or not await _choose_node_command(graph_window, canvas, 2, "Disable (Do Not Compile)"):
		return false
	current_graph = graph_script.call(&"get_graph")
	var disabled_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var disabled_status := disabled_card.find_child("GraphNodeDisabled", true, false) as Control if disabled_card != null else null
	if not _require(_selected_ids(canvas) == [2] and current_graph.nodes[1].get("disabled", false) and
		not current_graph.nodes[2].get("disabled", false) and current_graph.connections == initial.connections and
		disabled_status != null and disabled_status.is_visible_in_tree(),
		"Right clicking an unselected action disables only that action, marks its card, and keeps its authored wires."):
		return false
	if not await _choose_node_command(graph_window, canvas, 2, "Enable Compile"):
		return false
	current_graph = graph_script.call(&"get_graph")
	var enabled_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(not current_graph.nodes[1].get("disabled", false) and current_graph.connections == initial.connections and
		enabled_card != null and enabled_card.find_child("GraphNodeDisabled", true, false) == null,
		"Enable Compile removes the disabled appearance and preserves the original links."):
		return false
	if not await _choose_node_command(graph_window, canvas, 2, "Add Breakpoint"):
		return false
	var settings := EditorInterface.get_editor_settings()
	var registered: Dictionary = settings.get_project_metadata("crimson_visual_scripting", "breakpoints", {})
	if not _require(registered.get(graph_script.resource_path, PackedInt32Array()).has(2),
		"Adding a breakpoint records the event graph node in the editor's breakpoint table."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_F9)
	registered = settings.get_project_metadata("crimson_visual_scripting", "breakpoints", {})
	if not _require(not registered.get(graph_script.resource_path, PackedInt32Array()).has(2), "F9 removes the breakpoint exactly once."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_F9)
	if not await _choose_node_command(graph_window, canvas, 2, "Remove Breakpoint"):
		return false
	var after_breakpoint: Dictionary = graph_script.call(&"get_graph")
	registered = settings.get_project_metadata("crimson_visual_scripting", "breakpoints", {})
	if not _require(after_breakpoint == current_graph and not registered.has(graph_script.resource_path),
		"Breakpoint commands preserve the authored graph and remove their editor registration."):
		return false
	if not await _choose_node_command(graph_window, canvas, 2, "Add Breakpoint") or not await _choose_node_command(graph_window, canvas, 2, "Delete"):
		return false
	registered = settings.get_project_metadata("crimson_visual_scripting", "breakpoints", {})
	if not _require(not registered.has(graph_script.resource_path), "Deleting the node removes its stale breakpoint registration."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(graph_script.call(&"get_graph") == current_graph, "Undo restores the deleted graph node and connections.")


## Replace one occupied typed input, then undo and redo the entire wire change.
func _verify_rewire(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", _rewire_graph())
	await get_tree().process_frame
	var replacement := canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	var output := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(replacement != null and output != null, "Typed Literal and Print render in the workspace."):
		return false
	canvas.connection_request.emit(replacement.name, 0, output.name, 1)
	await get_tree().process_frame
	if not _require(_wire_present(graph_script, 4, &"result", 2, &"message") and not _wire_present(graph_script, 3, &"result", 2, &"message"),
		"Connecting a second data output replaces the wire on the occupied input."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _require(_wire_present(graph_script, 3, &"result", 2, &"message") and not _wire_present(graph_script, 4, &"result", 2, &"message"),
		"Undo restores the original data wire in one step."):
		return false
	await _shortcut(graph_window, canvas, KEY_Y)
	return _require(_wire_present(graph_script, 4, &"result", 2, &"message"), "Redo reconnects the replacement data wire.")


## Delete a connected node through GraphEdit's keyboard input and undo once.
func _verify_delete_key(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _rewire_graph()
	graph_script.call(&"set_graph", initial)
	await get_tree().process_frame
	if not _select(canvas, [2]):
		return false
	canvas.grab_focus()
	var delete_event := InputEventKey.new()
	delete_event.keycode = KEY_DELETE
	delete_event.physical_keycode = KEY_DELETE
	delete_event.pressed = true
	graph_window.get_window().push_input(delete_event)
	await get_tree().process_frame
	var remaining: Dictionary = graph_script.call(&"get_graph")
	if not _require(remaining.nodes.size() == 3 and remaining.connections.is_empty() and canvas.get_node_or_null(^"GraphNode_2") == null,
		"Delete immediately removes the selected Print and both attached wires."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	var restored: Dictionary = graph_script.call(&"get_graph")
	return _require(restored.nodes == initial.nodes and restored.connections == initial.connections,
		"One Undo restores the deleted node and both wires.")


## Exercise the normal clipboard and duplicate shortcuts against connected graph nodes.
func _verify_clipboard_commands(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _operation_graph()
	graph_script.call(&"set_graph", initial)
	await get_tree().process_frame
	if not _select(canvas, [2, 3]):
		return false
	await _push_graph_key(graph_window, canvas, KEY_C)
	await _push_graph_key(graph_window, canvas, KEY_V)
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 7 and graph_data.connections.size() == 5 and
		_wire_present(graph_script, 6, &"then", 7, &"in") and
		graph_data.nodes[6].position - graph_data.nodes[5].position == initial.nodes[2].position - initial.nodes[1].position,
		"Copy and Paste keep internal wires and the relative layout with fresh node IDs."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _select(canvas, [2, 3]):
		return false
	await _push_graph_key(graph_window, canvas, KEY_D)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 7 and graph_data.connections.size() == 5 and _wire_present(graph_script, 6, &"then", 7, &"in"),
		"One Ctrl+D copies the selection and its internal wire exactly once."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data == initial, "One Undo removes the entire duplicate without leaving a second copy."):
		return false
	if not _select(canvas, [2, 3]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_V, false, true)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 7 and graph_data.connections.size() == 6 and
		_wire_present(graph_script, 5, &"result", 6, &"message") and _wire_present(graph_script, 5, &"result", 2, &"message") and
		_wire_present(graph_script, 6, &"then", 7, &"in"), "Duplicate with Inputs keeps the original and copied incoming data wire without adding an execution link."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _select(canvas, [2, 3]):
		return false
	await _push_graph_key(graph_window, canvas, KEY_X)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 3 and graph_data.connections.is_empty(), "Cut removes the two selected nodes and all their links."):
		return false
	await _push_graph_key(graph_window, canvas, KEY_V)
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.nodes.size() == 5 and graph_data.connections.size() == 1 and _wire_present(graph_script, 6, &"then", 7, &"in"),
		"Pasting the cut selection restores its internal execution wire.")


## Expand upstream and downstream selection across execution and data dependencies.
func _verify_dependency_selection(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _operation_graph()
	graph_script.call(&"set_graph", initial)
	await get_tree().process_frame
	if not _select(canvas, [3]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_A, false, true)
	if not _require(_selected_ids(canvas) == [1, 2, 3, 5], "Select Upstream includes transitive execution and data producers."):
		return false
	if not _select(canvas, [2]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_D, false, true)
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	return _require(_selected_ids(canvas) == [2, 3, 4] and graph_data == initial,
		"Select Downstream follows outgoing wires without changing the document.")


## Persist the frame around selected nodes independently of the executable graph.
func _verify_comment_frame(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	const COMMENT_GRAPH := "user://comment_roundtrip.crimsonscript"
	var initial := _operation_graph()
	graph_script.call(&"set_graph", initial)
	await get_tree().process_frame
	if not _select(canvas, [2, 3]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_C)
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var comments: Array = graph_data.get("comments", [])
	if not _require(comments.size() == 1 and comments[0].members == [2, 3] and comments[0].size.x > 0 and comments[0].size.y > 0 and
		graph_data.nodes == initial.nodes and graph_data.connections == initial.connections, "C adds one comment around the selected nodes without changing their graph logic."):
		return false
	var frame := canvas.get_node_or_null(^"GraphComment_1") as GraphFrame
	var rename_button := frame.find_child("RenameGraphComment", true, false) as Button if frame != null else null
	if not _require(frame != null and rename_button != null and canvas.get_attached_nodes_of_frame(frame.name).size() == 2,
		"The comment renders as a native frame and is attached to both selected nodes."):
		return false
	if not await _verify_comment_drag(graph_window, canvas, graph_script, frame):
		return false
	frame = canvas.get_node_or_null(^"GraphComment_1") as GraphFrame
	rename_button = frame.find_child("RenameGraphComment", true, false) as Button
	rename_button.pressed.emit()
	await get_tree().process_frame
	var comment_title := graph_window.find_child("GraphCommentTitle", true, false) as LineEdit
	if not _require(comment_title != null, "The frame exposes its title editor."):
		return false
	comment_title.text = "Training sequence"
	comment_title.text_submitted.emit(comment_title.text)
	await get_tree().process_frame
	graph_data = graph_script.call(&"get_graph")
	comments = graph_data.get("comments", [])
	if not _require(comments.size() == 1 and comments[0].title == "Training sequence", "The renamed frame keeps its new title in the graph resource."):
		return false
	if not _require(ResourceSaver.save(graph_script, COMMENT_GRAPH) == OK, "Save the script with its comment frame."):
		return false
	var reopened := ResourceLoader.load(COMMENT_GRAPH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _require(reopened != null and reopened.call(&"get_graph").get("comments", []) == comments, "Reopening the script preserves the frame, membership and bounds."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.get("comments", []).is_empty() and graph_data.nodes == initial.nodes,
		"Undo removes the comment while keeping the executable nodes.")


## Repeated grouped drags preserve the native frame and its saved member offsets with snapping enabled.
func _verify_comment_drag(graph_window: Control, canvas: GraphEdit, graph_script: Script, frame: GraphFrame) -> bool:
	await _settle_canvas(canvas)
	var original_snap := canvas.snapping_enabled
	canvas.snapping_enabled = true
	var initial_size := frame.size
	var left_member := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var right_member := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var left_offset := left_member.position_offset - frame.position_offset
	var right_offset := right_member.position_offset - frame.position_offset
	for displacement: Vector2 in [Vector2(45, 35), Vector2(-31, 25)]:
		var member_selected := displacement.x < 0.0
		frame.selected = true
		left_member.selected = member_selected
		right_member.selected = false
		var previous_position := frame.position_offset
		var grab_point := frame.get_global_transform_with_canvas() * Vector2(25, 9)
		var safe_area := canvas.get_global_rect().grow(-10)
		if not safe_area.has_point(grab_point) or not safe_area.has_point(grab_point + displacement):
			canvas.scroll_offset += grab_point - safe_area.get_center()
			await get_tree().process_frame
			grab_point = frame.get_global_transform_with_canvas() * Vector2(25, 9)
		await _drag_mouse(graph_window, grab_point, displacement)
		var data: Dictionary = graph_script.call(&"get_graph")
		var saved_frame: Dictionary = data.comments[0]
		if not _require(frame.position_offset.is_equal_approx(previous_position + displacement) and
			frame.size.is_equal_approx(initial_size) and Vector2(saved_frame.size).is_equal_approx(initial_size) and
			(left_member.position_offset - frame.position_offset).is_equal_approx(left_offset) and
			(right_member.position_offset - frame.position_offset).is_equal_approx(right_offset) and canvas.snapping_enabled and
			left_member.selected == member_selected and left_member.draggable,
			"Dragging a comment alone or with a selected member preserves size, offsets, selection and snap preference."):
			canvas.snapping_enabled = original_snap
			return false
	canvas.snapping_enabled = original_snap
	# Undo both drag transactions. Reacquire the native frame as Undo rebuilds the canvas.
	await _shortcut(graph_window, canvas, KEY_Z)
	await _shortcut(graph_window, canvas, KEY_Z)
	return true


## Bypass, reconnect, break links, and dissolve one fan-out reroute.
func _verify_wire_commands(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _operation_graph()
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [2, 3]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_X, false, true, true)
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 5 and graph_data.connections.size() == 3 and
		_wire_present(graph_script, 1, &"then", 4, &"in") and _wire_present(graph_script, 2, &"then", 3, &"in"),
		"Bypass and Keep joins the outer execution path and leaves the selected nodes and their internal wire."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _select(canvas, [2, 3]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_X, false, true)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 3 and graph_data.connections.size() == 1 and _wire_present(graph_script, 1, &"then", 4, &"in"),
		"Delete and Reconnect removes the selection and joins the outer execution path."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not await _choose_node_command(graph_window, canvas, 2, "Break All Node Links"):
		return false
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 5 and graph_data.connections.size() == 1 and _wire_present(graph_script, 3, &"then", 4, &"in"),
		"Breaking a node's links keeps unrelated wires and all nodes."):
		return false
	var string_pin := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	initial.nodes.append({"id": 6, "kind": &"reroute", "mode": &"data", "pin": string_pin, "position": Vector2(88, 144)})
	initial.connections.pop_back()
	initial.connections.append_array([_wire(5, &"result", 6, &"value"), _wire(6, &"value", 2, &"message"), _wire(6, &"value", 3, &"message")])
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var dot_widget := canvas.get_node_or_null(^"GraphNode_6") as GraphNode
	if not _require(dot_widget != null, "Find the reroute dot for its context menu."):
		return false
	await _click_graph(graph_window, dot_widget.get_global_rect().get_center(), MOUSE_BUTTON_RIGHT)
	if not await _choose_menu_item(graph_window.find_child("GraphContextMenu", true, false) as PopupMenu, "Dissolve Reroute"):
		return false
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.nodes.size() == 5 and graph_data.connections.size() == 5 and
		_wire_present(graph_script, 5, &"result", 2, &"message") and _wire_present(graph_script, 5, &"result", 3, &"message"),
		"Dissolve Reroute reconnects its data producer directly to both consumers.")


## Frame and search nodes, use the root breadcrumb, and revisit saved views.
func _verify_navigation(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _operation_graph()
	initial.nodes[3].position = Vector2(920, 320)
	initial["functions"] = [{"id": 1, "name": "signal_probe", "inputs": [], "outputs": [], "graph": {
		"nodes": [{"id": 9, "kind": &"function_entry", "definition": 1, "position": Vector2(480, 320)},
			{"id": 10, "kind": &"return", "definition": 1, "position": Vector2(800, 320)}],
		"connections": [_wire(9, &"then", 10, &"in")],
	}}]
	graph_script.call(&"set_graph", initial)
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	if not _select(canvas, [4]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_F)
	var framed := canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	if not _require(framed != null and framed.get_global_rect().get_center().distance_to(canvas.get_global_rect().get_center()) <= 3.0,
		"Frame Selection centers the selected node in the canvas."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_HOME)
	if not _require(canvas.zoom < 0.8, "Frame All zooms out to fit the distant graph nodes."):
		return false
	canvas.zoom = 0.8
	canvas.scroll_offset = Vector2(240, 96)
	await get_tree().process_frame
	var saved_center := (canvas.scroll_offset + canvas.size * 0.5) / canvas.zoom
	var saved_zoom := canvas.zoom
	await _press_graph_key(graph_window, canvas, KEY_1, true)
	var finder := graph_window.find_child("GraphFindQuery", true, false) as LineEdit
	var matches := graph_window.find_child("GraphFindResults", true, false) as Tree
	if not _require(finder != null and finder.is_editable() and finder.is_visible_in_tree() and matches != null and
			not matches.is_visible_in_tree(), "The toolbar has a directly editable search field."):
		return false
	await _click_graph(graph_window, finder.get_global_rect().get_center(), MOUSE_BUTTON_LEFT)
	if not _require(finder.has_focus() and matches.is_visible_in_tree(), "Clicking the field opens its results and keeps text focus."):
		return false
	var matching_card := canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	var other_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(matching_card != null and other_card != null, "Search has both matching and unrelated cards to filter."):
		return false
	var matching_baseline := matching_card.modulate
	var other_baseline := other_card.modulate
	finder.text = "Print"
	finder.text_changed.emit(finder.text)
	await get_tree().process_frame
	if not _require(matching_card.modulate == matching_baseline and other_card.modulate.a < other_baseline.a,
			"A query leaves its matching cards unchanged and dims unrelated cards."):
		return false
	finder.text = ""
	finder.text_changed.emit(finder.text)
	if not _require(matches.is_visible_in_tree() and finder.has_focus() and other_card.modulate == other_baseline,
			"Clearing the query restores card colors while keeping the field ready to type."):
		return false
	finder.text = "Print"
	finder.text_changed.emit(finder.text)
	var dismiss := InputEventKey.new()
	dismiss.keycode = KEY_ESCAPE
	dismiss.pressed = true
	graph_window.get_window().push_input(dismiss, true)
	await get_tree().process_frame
	if not _require(not matches.is_visible_in_tree() and finder.text.is_empty() and canvas.has_focus() and
			matching_card.modulate == matching_baseline and other_card.modulate == other_baseline,
			"Escape closes Search, restores the complete graph and returns focus."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_F, true)
	if not _require(finder.has_focus() and matches.is_visible_in_tree(), "Ctrl+F focuses the field and opens its results."):
		return false
	finder.text = "signal_probe"
	finder.text_changed.emit(finder.text)
	var result := matches.get_selected()
	if not _require(result != null and result.get_text(1) == "signal_probe", "A filtered result displays its authored graph."):
		return false
	var next_result := InputEventKey.new()
	next_result.keycode = KEY_DOWN
	next_result.pressed = true
	graph_window.get_window().push_input(next_result, true)
	if not _require(matches.get_selected() != result and finder.has_focus() and finder.text == "signal_probe",
			"Down moves through results with the query and caret still in the field."):
		return false
	next_result.keycode = KEY_UP
	graph_window.get_window().push_input(next_result, true)
	finder.text_submitted.emit(finder.text)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _require(canvas.get_node_or_null(^"GraphNode_9") != null and _selected_ids(canvas) == [9], "Opening a result selects the matching node in its function graph."):
		return false
	var event_root := graph_window.find_child("GraphEventRoot", true, false) as Button
	var graph_label := graph_window.find_child("VisualScriptActiveGraph", true, false) as Label
	if not _require(event_root != null and not event_root.disabled and graph_label != null and
			graph_label.is_visible_in_tree() and graph_label.text == "signal_probe",
			"Entering a function adds its breadcrumb and enables returning to the Event Graph."):
		return false
	if not await _open_event_graph(graph_window):
		return false
	if not _require(canvas.get_node_or_null(^"GraphNode_1") != null and not graph_label.is_visible_in_tree(),
			"Clicking the Event Graph breadcrumb returns to the main graph and hides the subgraph caption."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_LEFT, false, true)
	if not _require(canvas.get_node_or_null(^"GraphNode_9") != null, "Back returns to the prior function after following a breadcrumb."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_LEFT, false, true)
	if not _require(canvas.get_node_or_null(^"GraphNode_1") != null, "Back returns to the Event Graph."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_RIGHT, false, true)
	if not _require(canvas.get_node_or_null(^"GraphNode_9") != null, "Forward reopens the function location."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_1, false, false, true)
	var restored_center := (canvas.scroll_offset + canvas.size * 0.5) / canvas.zoom
	if not _require(canvas.get_node_or_null(^"GraphNode_1") != null and is_equal_approx(canvas.zoom, saved_zoom) and
			restored_center.distance_to(saved_center) <= 3.0, "A recalled bookmark restores the saved graph, zoom and view center."):
		return false
	await _settle_canvas(canvas)
	other_card = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	other_baseline = other_card.modulate
	await _click_graph(graph_window, finder.get_global_rect().get_center(), MOUSE_BUTTON_LEFT)
	finder.text = "Print"
	finder.text_changed.emit(finder.text)
	await _click_graph(graph_window, other_card.get_titlebar_hbox().get_global_rect().get_center(), MOUSE_BUTTON_LEFT)
	if not _require(not matches.is_visible_in_tree() and not finder.has_focus() and other_card.modulate == other_baseline and
			_selected_ids(canvas) == [1], "The first click outside the dropdown closes it and selects the clicked graph card."):
		return false
	var outline := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var functions := _find_entry(outline, "Functions")
	var function_row := functions.get_first_child() if functions != null else null
	if not _require(function_row != null, "Find the authored function in the declarations."):
		return false
	function_row.select(0)
	outline.item_activated.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	var entry_card := canvas.get_node_or_null(^"GraphNode_9") as GraphNode
	var return_card := canvas.get_node_or_null(^"GraphNode_10") as GraphNode
	if not _require(entry_card != null and return_card != null, "Opening the function shows both boundary nodes."):
		return false
	var function_bounds := entry_card.get_global_rect().merge(return_card.get_global_rect())
	return _require(canvas.get_global_rect().encloses(function_bounds) and
			canvas.get_global_rect().get_center().distance_to(function_bounds.get_center()) <= 3.0 and
			_selected_ids(canvas).is_empty(), "Opening the function fits and centers its whole graph.")


## Reset an authored literal and break one socket's links via the native pin menu.
func _verify_pin_commands(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _rewire_graph()
	initial.nodes[1]["message"] = "Authored before reset"
	initial.nodes[1].position = Vector2(128, 72)
	initial.connections.pop_back()
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var print_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(print_widget != null, "The Print node exposes its authored data pin."):
		return false
	await _click_graph(graph_window, _socket_position(canvas, print_widget, 1, false), MOUSE_BUTTON_RIGHT)
	var context_menu := graph_window.find_child("GraphContextMenu", true, false) as PopupMenu
	if not await _choose_menu_item(context_menu, "Reset Input to Default"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(not graph_data.nodes[1].has("message") and graph_data.connections.size() == 1, "Reset restores the pin's fallback while keeping the execution wire."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes[1].message == "Authored before reset", "Undo restores the authored pin value."):
		return false
	print_widget = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	await _click_graph(graph_window, _socket_position(canvas, print_widget, 0, false), MOUSE_BUTTON_RIGHT)
	if not _require(context_menu.visible and _menu_item_index(context_menu, "Promote to Variable") < 0,
		"Execution pins keep their link commands and omit typed variable promotion."):
		return false
	if not await _choose_menu_item(context_menu, "Break All Pin Links"):
		return false
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.connections.is_empty() and graph_data.nodes.size() == 4, "Breaking the socket removes only its links.")


## Use viewport gestures to splice a node into a wire, then sweep another wire away.
func _verify_wire_gestures(graph_window: Control, canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	var graph_data := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(300, 64)},
	], "connections": [_wire(1, &"then", 2, &"in")]}
	graph_script.call(&"set_graph", graph_data)
	await _settle_canvas(canvas)
	var ready_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var print_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(ready_widget != null and print_widget != null, "Find both endpoints of the test execution wire."):
		return false
	var midpoint := (_socket_position(canvas, ready_widget, 0, true) + _socket_position(canvas, print_widget, 0, false)) / 2.0
	await _click_graph(graph_window, midpoint, MOUSE_BUTTON_RIGHT)
	if not await _choose_menu_item(graph_window.find_child("GraphContextMenu", true, false) as PopupMenu, "Insert Node on Wire"):
		return false
	if not _require(result_tree.is_visible_in_tree(), "Insert Node on Wire opens the compatible node creation picker."):
		return false
	if not await _choose_palette_item(search_field, result_tree, "Print"):
		return false
	if not _require(_wire_present(graph_script, 1, &"then", 3, &"in") and _wire_present(graph_script, 3, &"then", 2, &"in") and
		not _wire_present(graph_script, 1, &"then", 2, &"in"), "Inserting a compatible node replaces the wire with its two connected sides."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	await _sweep_wire(graph_window, midpoint)
	var cut: Dictionary = graph_script.call(&"get_graph")
	if not _require(cut.nodes.size() == 2 and cut.connections.is_empty(), "An Alt+middle-button sweep removes the crossed wire while preserving both nodes."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(_wire_present(graph_script, 1, &"then", 2, &"in"), "Undo restores the swept wire.")


## Save execution and data wires, invalidate hidden port caches, and reopen the same graph.
func _verify_wire_restoration(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var graph_data := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(48, 56)},
		{"id": 2, "kind": &"print", "position": Vector2(280, 56)},
		{"id": 3, "kind": &"literal", "position": Vector2(48, 132),
			"pin": {"id": &"value", "label": "Value", "type": TYPE_STRING},
			"values": {&"value": "Restored value"}},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(3, &"result", 2, &"message")]}
	graph_script.call(&"set_graph", graph_data)
	await _settle_canvas(canvas)
	var ready_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var print_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(ready_card != null and print_card != null, "Find the editable connected cards."):
		return false
	var visible_wires := canvas.get_connection_list()
	if not _require(visible_wires.size() == 2 and ready_card.get_output_port_count() == 1 and print_card.get_input_port_count() == 2,
			"Execution and data wires use the correct two native input indices. Wires %s, ports %d/%d, modes %d/%d." %
			[visible_wires, ready_card.get_output_port_count(), print_card.get_input_port_count(), graph_window.get_window().mode, get_tree().root.mode]):
		return false
	print_card.selected = true
	await _shortcut(graph_window, canvas, KEY_S)
	if not _require(_read_disk_graph(GRAPH_PATH) == graph_script.call(&"get_graph"),
			"Ctrl+S writes the wired graph before switching editor screens."):
		return false
	EditorInterface.set_main_screen_editor("3D")
	ready_card.queue_sort()
	print_card.queue_sort()
	await get_tree().process_frame
	await get_tree().process_frame
	if not _require(not graph_window.visible and ready_card.get_output_port_count() == 0 and print_card.get_input_port_count() == 0,
			"Hidden layout excludes the native port rows."):
		return false
	if not _require(canvas.get_connection_list().is_empty(), "Hidden graph detaches native wire indices while keeping the saved model."):
		return false
	EditorInterface.edit_script(graph_script, -1, 0, true)
	await get_tree().process_frame
	await get_tree().process_frame
	var restored_wires := canvas.get_connection_list()
	return _require(graph_window.visible and print_card.selected and restored_wires == visible_wires and
		ready_card.get_output_port_count() == 1 and print_card.get_input_port_count() == 2 and
		_wire_present(graph_script, 1, &"then", 2, &"in") and _wire_present(graph_script, 3, &"result", 2, &"message"),
		"Reopening the same saved graph restores execution and data wires and keeps card selection. Visible %s, selected %s, wires %s -> %s, ports %d/%d." %
		[graph_window.visible, print_card.selected, visible_wires, restored_wires, ready_card.get_output_port_count(), print_card.get_input_port_count()])


## Exercise selection and wire focus cancellation through the native viewport input pipeline.
func _verify_pointer_interruptions(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(206, 64)},
		{"id": 3, "kind": &"branch", "position": Vector2(45, 220)},
	], "connections": [_wire(1, &"then", 2, &"in")], "functions": [{
		"id": 1, "name": &"focus_probe", "inputs": [], "outputs": [], "graph": {
			"nodes": [{"id": 1, "kind": &"return", "definition": 1, "position": Vector2(192, 80)}],
			"connections": [],
		},
	}]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var ready_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var print_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var branch_card := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var picker := graph_window.find_child("GraphNodePalette", true, false) as PopupPanel
	if not _require(ready_card != null and print_card != null and branch_card != null and picker != null, "Find the cards and picker for the input interruption tests."):
		return false

	# Modified clicks add or remove explicitly, including when the target has the requested state.
	ready_card.selected = true
	await _modified_pointer_gesture(graph_window, print_card.get_titlebar_hbox().get_global_rect().get_center(), Vector2.ZERO, true)
	if not _require(ready_card.selected and print_card.selected, "Shift-click adds a node and preserves prior selection. Cards %s, %s, canvas %s; selection %s, %s." %
		[ready_card.get_global_rect(), print_card.get_global_rect(), canvas.get_global_rect(), ready_card.selected, print_card.selected]):
		return false
	await _modified_pointer_gesture(graph_window, ready_card.get_titlebar_hbox().get_global_rect().get_center(), Vector2.ZERO, false)
	await _modified_pointer_gesture(graph_window, ready_card.get_titlebar_hbox().get_global_rect().get_center(), Vector2.ZERO, false)
	if not _require(not ready_card.selected and print_card.selected, "Ctrl-click removes a node and cannot toggle an unselected node on."):
		return false
	var marquee_start := branch_card.get_global_rect().position - Vector2(20, 20)
	var marquee_span := Vector2(65, 58)
	await _modified_pointer_gesture(graph_window, marquee_start, marquee_span, true)
	if not _require(branch_card.selected and print_card.selected, "Shift marquee adds its contents to the previous selection."):
		return false
	await _modified_pointer_gesture(graph_window, marquee_start, marquee_span, false, true)
	if not _require(branch_card.selected and print_card.selected, "Focus loss restores the selection from before an unfinished Ctrl marquee."):
		return false
	await _modified_pointer_gesture(graph_window, marquee_start, marquee_span, false)
	if not _require(not branch_card.selected and print_card.selected, "Ctrl marquee removes only its contents."):
		return false

	# Capture state in an Array so the closures share the counters with the assertions.
	var counts := [0, 0]
	var started := func(_from: StringName, _index: int, _output: bool) -> void: counts[0] += 1
	var ended := func() -> void: counts[1] += 1
	canvas.connection_drag_started.connect(started)
	canvas.connection_drag_ended.connect(ended)

	var wire_tip := _socket_position(canvas, ready_card, 0, true)
	var print_socket := _socket_position(canvas, print_card, 0, false)
	var blank := wire_tip + Vector2(18, 92)
	graph_window.get_window().push_input(_mouse_button(wire_tip, true), true)
	_pointer_motion(graph_window, wire_tip, blank)
	await get_tree().process_frame
	if not _require(counts == [1, 0], "Begin an actual native drag from the event output."):
		return false
	_notify_pointer_focus_loss(graph_window, canvas)
	graph_window.get_window().push_input(_mouse_button(blank, false), true)
	await get_tree().process_frame
	if not _require(counts == [1, 1] and not picker.visible and _wire_present(graph_script, 1, &"then", 2, &"in"),
		"Focus loss cancels the wire. A stale release leaves the existing link intact and the picker closed."):
		return false

	graph_window.get_window().push_input(_mouse_button(wire_tip, true), true)
	_pointer_motion(graph_window, wire_tip, blank)
	var escape_key := InputEventKey.new()
	escape_key.keycode = KEY_ESCAPE
	escape_key.pressed = true
	graph_window.get_window().push_input(escape_key, true)
	graph_window.get_window().push_input(_mouse_button(blank, false), true)
	await get_tree().process_frame
	if not _require(counts == [2, 2] and not picker.visible, "Escape cancels an active pointer wire without opening the picker."):
		return false

	graph_window.get_window().push_input(_mouse_button(print_socket, true), true)
	_pointer_motion(graph_window, print_socket, wire_tip)
	await get_tree().process_frame
	if not _require(counts == [3, 2], "A reverse drag starts normally after focus returns."):
		return false
	var outline := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var function_group := _find_entry(outline, "Functions") if outline != null else null
	var function_row := function_group.get_first_child() if function_group != null else null
	if not _require(function_row != null and function_row.get_text(0) == "focus_probe",
		"Find the function whose return node reuses the event graph's ID."):
		return false
	function_row.select(0)
	outline.item_activated.emit()
	graph_window.get_window().push_input(_mouse_button(wire_tip, false), true)
	await _settle_canvas(canvas)
	var return_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(counts == [3, 3] and return_card != null and return_card.get_output_port_count() == 0 and not picker.visible,
		"Opening the function during a wire drag cancels it before an input-only return widget reuses the event node ID."):
		return false
	canvas.connection_drag_started.disconnect(started)
	canvas.connection_drag_ended.disconnect(ended)
	if not await _open_event_graph(graph_window):
		return false
	await _settle_canvas(canvas)
	ready_card = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	print_card = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	branch_card = canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var move_handle := branch_card.get_titlebar_hbox().get_global_rect().get_center()
	var splice_point := (_socket_position(canvas, ready_card, 0, true) + _socket_position(canvas, print_card, 0, false)) * 0.5
	graph_window.get_window().push_input(_mouse_button(move_handle, true), true)
	_pointer_motion(graph_window, move_handle, splice_point)
	await get_tree().process_frame
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0) and
		not canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0),
		"The interrupted insert first shows the splice preview on the native canvas."):
		return false
	_notify_pointer_focus_loss(graph_window, canvas)
	graph_window.get_window().push_input(_mouse_button(splice_point, false), true)
	await get_tree().process_frame
	var completed: Dictionary = graph_script.call(&"get_graph")
	if not _require(completed.connections == initial.connections and completed.nodes[2].position != initial.nodes[2].position and
		canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0),
		"Losing focus during a card drag keeps the moved position without auto-inserting it into a wire. Start %s, end %s, handle %s, target %s, widget %s, canvas %s, wires %s." %
		[initial.nodes[2].position, completed.nodes[2].position, move_handle, splice_point, branch_card.get_global_rect(), canvas.get_global_rect(), completed.connections]):
		return false
	await _drag_mouse(graph_window, branch_card.get_titlebar_hbox().get_global_rect().get_center(), Vector2(0, 83))
	var subsequent: Dictionary = graph_script.call(&"get_graph")
	return _require(subsequent.nodes[2].position != completed.nodes[2].position and subsequent.connections == initial.connections,
		"A fresh card drag still works after an interrupted move and a stale release.")


## Simulate the focus signal routing for an embedded editor viewport.
func _notify_pointer_focus_loss(graph_window: Control, canvas: GraphEdit) -> void:
	graph_window.notification(NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	canvas.notification(NOTIFICATION_WM_WINDOW_FOCUS_OUT)


## Send a pointer movement while the left button is held.
func _pointer_motion(graph_window: Control, origin: Vector2, destination: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = destination
	motion.global_position = destination
	motion.relative = destination - origin
	motion.button_mask = MOUSE_BUTTON_MASK_LEFT
	graph_window.get_window().push_input(motion, true)


## Apply a Shift-add or Ctrl-remove click or rectangle, with optional focus interruption.
func _modified_pointer_gesture(graph_window: Control, origin: Vector2, span: Vector2, add_to: bool, interrupted: bool = false) -> void:
	var press := _mouse_button(origin, true)
	press.shift_pressed = add_to
	press.ctrl_pressed = not add_to
	graph_window.get_window().push_input(press, true)
	if span != Vector2.ZERO:
		_pointer_motion(graph_window, origin, origin + span)
	if interrupted:
		_notify_pointer_focus_loss(graph_window, graph_window.find_child("GraphCanvas", true, false) as GraphEdit)
	graph_window.get_window().push_input(_mouse_button(origin + span, false), true)
	await get_tree().process_frame


## Connect a selected chain with Alt+C and attach two inputs to a remembered data source.
func _verify_quick_connections(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _operation_graph()
	initial["connections"] = []
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [1, 2, 3]):
		return false
	await _press_graph_key(graph_window, canvas, KEY_C, false, true)
	if not _require(_wire_present(graph_script, 1, &"then", 2, &"in") and _wire_present(graph_script, 2, &"then", 3, &"in"),
		"Alt+C connects compatible execution pins of the selected nodes in order."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	initial.nodes[1].position = Vector2(152, 64)
	initial.nodes[2].position = Vector2(152, 224)
	initial.nodes[4].position = Vector2(16, 16)
	initial.nodes.pop_at(3)
	initial.nodes.pop_front()
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var producer := canvas.get_node_or_null(^"GraphNode_5") as GraphNode
	if not _require(producer != null, "Find the literal's data output for multi-connect."):
		return false
	await _click_graph(graph_window, _socket_position(canvas, producer, 0, true), MOUSE_BUTTON_LEFT, true)
	for consumer_id: int in [2, 3]:
		var receiver := canvas.get_node_or_null(NodePath("GraphNode_%d" % consumer_id)) as GraphNode
		if not _require(receiver != null, "Find a compatible input for the remembered source."):
			return false
		await _click_graph(graph_window, _socket_position(canvas, receiver, 1, false), MOUSE_BUTTON_LEFT)
		if not _require(_wire_present(graph_script, 5, &"result", consumer_id, &"message"), "The remembered data output connects to each clicked input."):
			return false
	await _press_graph_key(graph_window, canvas, KEY_ESCAPE)
	if not _require(_wire_present(graph_script, 5, &"result", 2, &"message") and _wire_present(graph_script, 5, &"result", 3, &"message"),
		"Ending multi-connect preserves both outgoing wires."):
		return false
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	producer = canvas.get_node_or_null(^"GraphNode_5") as GraphNode
	var first_receiver := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var second_receiver := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(producer != null and first_receiver != null and second_receiver != null, "Find the three native batch-connection sockets."):
		return false
	var start := _socket_position(canvas, producer, 0, true)
	var first_target := _socket_position(canvas, first_receiver, 1, false)
	var second_target := _socket_position(canvas, second_receiver, 1, false)
	var press := _mouse_button(start, true)
	press.shift_pressed = true
	graph_window.get_window().push_input(press, true)
	for point in [first_target, second_target]:
		var motion := InputEventMouseMotion.new()
		motion.position = point
		motion.global_position = point
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT
		motion.shift_pressed = true
		graph_window.get_window().push_input(motion, true)
	var release := _mouse_button(second_target, false)
	release.shift_pressed = true
	graph_window.get_window().push_input(release, true)
	await get_tree().process_frame
	if not _require(_wire_present(graph_script, 5, &"result", 2, &"message") and _wire_present(graph_script, 5, &"result", 3, &"message"),
		"A single Shift-drag connects both compatible sockets it crosses."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.connections.is_empty(), "One Undo removes the two wires made by the batch gesture."):
		return false
	await _press_graph_key(graph_window, canvas, KEY_ESCAPE)
	producer = canvas.get_node_or_null(^"GraphNode_5") as GraphNode
	await _click_graph(graph_window, _socket_position(canvas, producer, 0, true), MOUSE_BUTTON_LEFT, true)
	if not _select(canvas, [5]):
		return false
	canvas.grab_focus()
	var delete_event := InputEventKey.new()
	delete_event.keycode = KEY_DELETE
	delete_event.physical_keycode = KEY_DELETE
	delete_event.pressed = true
	graph_window.get_window().push_input(delete_event)
	await get_tree().process_frame
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.nodes.size() == 2 and graph_data.connections.is_empty(), "Deleting the remembered producer leaves no stale wire.")


## Click two different wires, exchange their destinations, and undo the swap.
func _verify_wire_exchange(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var string_pin := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"literal", "pin": string_pin, "position": Vector2(12, 24), "values": {&"value": "First"}},
		{"id": 2, "kind": &"literal", "pin": string_pin, "position": Vector2(12, 196), "values": {&"value": "Second"}},
		{"id": 3, "kind": &"print", "position": Vector2(276, 24)},
		{"id": 4, "kind": &"print", "position": Vector2(276, 196)},
	], "connections": [_wire(1, &"result", 3, &"message"), _wire(2, &"result", 4, &"message")]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var wire_centers: Array[Vector2] = []
	for source_id: int in [1, 2]:
		var producer := canvas.get_node_or_null(NodePath("GraphNode_%d" % source_id)) as GraphNode
		var receiver := canvas.get_node_or_null(NodePath("GraphNode_%d" % (source_id + 2))) as GraphNode
		if not _require(producer != null and receiver != null, "Find the native endpoints of each wire to exchange."):
			return false
		var midpoint := (_socket_position(canvas, producer, 0, true) + _socket_position(canvas, receiver, 1, false)) / 2.0
		wire_centers.append(midpoint)
		await _click_graph(graph_window, midpoint, MOUSE_BUTTON_LEFT, source_id == 2)
	await _modified_pointer_gesture(graph_window, wire_centers[1], Vector2.ZERO, false)
	await _press_graph_key(graph_window, canvas, KEY_T, false, true)
	if not _require(_wire_present(graph_script, 1, &"result", 3, &"message") and _wire_present(graph_script, 2, &"result", 4, &"message"),
		"Ctrl-click removes a wire highlight without removing the saved wire."):
		return false
	await _modified_pointer_gesture(graph_window, wire_centers[1], Vector2.ZERO, true)
	await _press_graph_key(graph_window, canvas, KEY_T, false, true)
	if not _require(_wire_present(graph_script, 1, &"result", 4, &"message") and _wire_present(graph_script, 2, &"result", 3, &"message") and
		not _wire_present(graph_script, 1, &"result", 3, &"message"), "Alt+T swaps the destinations of the two selected wires."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(_wire_present(graph_script, 1, &"result", 3, &"message") and _wire_present(graph_script, 2, &"result", 4, &"message"),
		"One Undo restores both original destinations.")


## Drag a node normally, then shake it loose with small motion events and one undo.
func _verify_shake_bypass(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var start_position := Vector2(256, 176)
	var original_graph := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(32, 176)},
		{"id": 2, "kind": &"print", "position": start_position},
		{"id": 3, "kind": &"print", "position": Vector2(496, 176)},
		{"id": 4, "kind": &"print", "position": Vector2(32, 384)},
		{"id": 5, "kind": &"print", "position": Vector2(496, 384)},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in"), _wire(4, &"then", 5, &"in")]}
	graph_script.call(&"set_graph", original_graph)
	await _settle_canvas(canvas)
	var dragged := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(dragged != null, "Find the middle node to shake off the wire."):
		return false
	var handle := await _center_shake_handle(canvas, dragged)
	await _drag_mouse(graph_window, handle, Vector2(0, 38))
	if not _require(_wire_present(graph_script, 1, &"then", 2, &"in") and _wire_present(graph_script, 2, &"then", 3, &"in"),
		"An ordinary left-button drag keeps the node's wires attached."):
		return false
	graph_script.call(&"set_graph", original_graph)
	await _settle_canvas(canvas)
	dragged = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	handle = await _center_shake_handle(canvas, dragged)
	var shake_release := await _hold_shake_mouse(graph_window, handle, Vector2(0, 5))
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0) and
		not canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and
		not canvas.is_node_connected(&"GraphNode_2", 0, &"GraphNode_3", 0) and
		graph_script.call(&"get_graph") == original_graph,
		"A recognized shake shows only the bypass wire while the mouse is held and keeps the saved graph untouched."):
		return false
	await _release_shake_mouse(graph_window, shake_release)
	var updated: Dictionary = graph_script.call(&"get_graph")
	if not _require(updated.nodes[1].position != start_position,
		"The native canvas moved the node during the vertical shake: %s." % [updated.nodes[1].position]):
		return false
	if not _require(updated.nodes.size() == 5 and
		updated.connections.size() == 2 and _wire_present(graph_script, 1, &"then", 3, &"in") and
		_wire_present(graph_script, 4, &"then", 5, &"in"),
		"An unmodified vertical shake of small events bypasses the dragged node and preserves the unrelated wire."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	updated = graph_script.call(&"get_graph")
	if not _require(updated.nodes[1].position == start_position and updated.connections == original_graph.connections,
		"One Undo restores the original node position and both execution wires."):
		return false
	dragged = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	handle = await _center_shake_handle(canvas, dragged)
	shake_release = await _hold_shake_mouse(graph_window, handle, Vector2(0, 5))
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0),
		"A new gesture after Undo previews a bypass again."):
		return false
	_notify_pointer_focus_loss(graph_window, canvas)
	await _release_shake_mouse(graph_window, shake_release)
	updated = graph_script.call(&"get_graph")
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and
		not canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0) and updated.connections == original_graph.connections,
		"Focus loss restores the pre-shake wires and the following stale release commits no bypass."):
		return false
	if not await _verify_group_shake(graph_window, canvas, graph_script):
		return false
	if not await _verify_branch_shake(graph_window, canvas, graph_script):
		return false
	if not await _verify_ambiguous_branch_shake(graph_window, canvas, graph_script):
		return false

	var text_pin := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	var data_graph := {"version": 1, "nodes": [
		{"id": 1, "kind": &"literal", "pin": text_pin, "value": "kept", "position": Vector2(32, 144)},
		{"id": 2, "kind": &"reroute", "mode": &"data", "pin": text_pin, "position": Vector2(256, 176)},
		{"id": 3, "kind": &"print", "position": Vector2(496, 112)},
		{"id": 4, "kind": &"print", "position": Vector2(496, 256)},
	], "connections": [_wire(1, &"result", 2, &"value"), _wire(2, &"value", 3, &"message"), _wire(2, &"value", 4, &"message")]}
	graph_script.call(&"set_graph", data_graph)
	await _settle_canvas(canvas)
	var dot := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(dot != null, "Find the data reroute to shake loose."):
		return false
	if not _select(canvas, [2]):
		return false
	await _shake_mouse(graph_window, await _center_shake_handle(canvas, dot, true), Vector2(5, 0))
	updated = graph_script.call(&"get_graph")
	return _require(updated.nodes.size() == 4 and updated.connections.size() == 2 and
		_wire_present(graph_script, 1, &"result", 3, &"message") and _wire_present(graph_script, 1, &"result", 4, &"message"),
		"Shaking a typed reroute reconnects both compatible data consumers without removing the dot. Position: %s. Links: %s." %
		[updated.nodes[1].position, updated.connections])


## Shake a node and commit on release.
func _shake_mouse(graph_window: Control, origin: Vector2, sample_delta: Vector2, samples_per_stroke: int = 10,
		lead_in: Vector2 = Vector2.ZERO) -> void:
	var point := await _hold_shake_mouse(graph_window, origin, sample_delta, samples_per_stroke, lead_in)
	await _release_shake_mouse(graph_window, point)


## Deliver a deliberate two-reversal gesture without releasing the mouse.
func _hold_shake_mouse(graph_window: Control, origin: Vector2, sample_delta: Vector2, samples_per_stroke: int = 10,
		lead_in: Vector2 = Vector2.ZERO) -> Vector2:
	graph_window.get_window().push_input(_mouse_button(origin, true), true)
	await get_tree().process_frame
	var point := origin
	if not lead_in.is_zero_approx():
		point += lead_in
		var travel := InputEventMouseMotion.new()
		travel.relative = lead_in
		travel.position = point
		travel.global_position = point
		travel.button_mask = MOUSE_BUTTON_MASK_LEFT
		graph_window.get_window().push_input(travel, true)
	for stroke: int in [1, -1, 1]:
		for sample: int in range(samples_per_stroke):
			var motion := InputEventMouseMotion.new()
			motion.relative = sample_delta * stroke
			point += motion.relative
			motion.position = point
			motion.global_position = point
			motion.button_mask = MOUSE_BUTTON_MASK_LEFT
			graph_window.get_window().push_input(motion, true)
		await get_tree().process_frame
	return point


## Let native movement and the resulting edit finish.
func _release_shake_mouse(graph_window: Control, point: Vector2) -> void:
	graph_window.get_window().push_input(_mouse_button(point, false), true)
	await get_tree().process_frame
	await get_tree().process_frame


## Move a contiguous group and a separate route in one native drag and one graph edit.
func _verify_group_shake(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var group_graph := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(32, 96)},
		{"id": 2, "kind": &"print", "position": Vector2(192, 96)},
		{"id": 3, "kind": &"print", "position": Vector2(352, 96)},
		{"id": 4, "kind": &"print", "position": Vector2(512, 96)},
		{"id": 5, "kind": &"print", "position": Vector2(32, 288)},
		{"id": 6, "kind": &"print", "position": Vector2(192, 288)},
		{"id": 7, "kind": &"print", "position": Vector2(512, 288)},
	], "connections": [
		_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in"), _wire(3, &"then", 4, &"in"),
		_wire(5, &"then", 6, &"in"), _wire(6, &"then", 7, &"in")]}
	graph_script.call(&"set_graph", group_graph)
	await _settle_canvas(canvas)
	if not _select(canvas, [2, 3, 6]):
		return false
	var selected_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var handle := await _center_shake_handle(canvas, selected_card)
	await _shake_mouse(graph_window, handle, Vector2(5, 0))
	var updated: Dictionary = graph_script.call(&"get_graph")
	if not _require(updated.nodes.size() == 7 and updated.connections.size() == 3 and
		_wire_present(graph_script, 1, &"then", 4, &"in") and _wire_present(graph_script, 5, &"then", 7, &"in") and
		_wire_present(graph_script, 2, &"then", 3, &"in") and _selected_ids(canvas) == [2, 3, 6] and
		updated.nodes[1].position != group_graph.nodes[1].position and
		updated.nodes[2].position != group_graph.nodes[2].position and updated.nodes[5].position != group_graph.nodes[5].position,
		"Shaking a multiselection repairs each route and keeps the group's internal wire and selection."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	updated = graph_script.call(&"get_graph")
	return _require(updated.nodes == group_graph.nodes and updated.connections == group_graph.connections,
		"One Undo restores every moved card and both original routes.")


## Shake a Branch along its one connected execution route using short strokes.
func _verify_branch_shake(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(32, 176)},
		{"id": 2, "kind": &"branch", "position": Vector2(256, 176)},
		{"id": 3, "kind": &"print", "position": Vector2(496, 176)},
		{"id": 4, "kind": &"literal", "pin": {"id": &"value", "type": TYPE_BOOL}, "position": Vector2(32, 370)},
	], "connections": [
		_wire(1, &"then", 2, &"in"), _wire(2, &"false", 3, &"in"), _wire(4, &"result", 2, &"condition"),
	]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var branch := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(branch != null, "Find the Branch to shake off the wire.") or not _select(canvas, [2]):
		return false
	var handle := await _center_shake_handle(canvas, branch)
	await _shake_mouse(graph_window, handle, Vector2(4, 0), 4, Vector2(0, 32))
	var changed: Dictionary = graph_script.call(&"get_graph")
	if not _require(changed.connections.size() == 2 and _wire_present(graph_script, 1, &"then", 3, &"in") and
		_wire_present(graph_script, 4, &"result", 2, &"condition"),
		"A short shake bypasses the Branch along its connected False path and keeps the Condition wire. Links %s." % [changed.connections]):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	changed = graph_script.call(&"get_graph")
	return _require(changed.nodes == initial.nodes and changed.connections == initial.connections,
		"Undo restores the original Branch position and all original wires.")


## An actual fork cannot be bypassed as a single execution route.
func _verify_ambiguous_branch_shake(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(32, 176)},
		{"id": 2, "kind": &"branch", "position": Vector2(256, 176)},
		{"id": 3, "kind": &"print", "position": Vector2(496, 142)},
		{"id": 4, "kind": &"print", "position": Vector2(496, 300)},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(2, &"true", 3, &"in"), _wire(2, &"false", 4, &"in")]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var branch := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(branch != null, "Find the Branch with both execution outputs connected.") or not _select(canvas, [2]):
		return false
	var handle := await _center_shake_handle(canvas, branch)
	var release_at := await _hold_shake_mouse(graph_window, handle, Vector2(4, 0), 4)
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and
		canvas.is_node_connected(&"GraphNode_2", 0, &"GraphNode_3", 0) and
		canvas.is_node_connected(&"GraphNode_2", 1, &"GraphNode_4", 0),
		"Shake Off preserves both outgoing routes on the canvas when their fork cannot be bypassed."):
		return false
	await _release_shake_mouse(graph_window, release_at)
	var changed: Dictionary = graph_script.call(&"get_graph")
	return _require(changed.connections == initial.connections and changed.nodes[1].position != initial.nodes[1].position,
		"An ambiguous shake saves only the moved card, keeping every wire.")


## Center the drag handle without assuming the host's editor window size or DPI.
func _center_shake_handle(canvas: GraphEdit, widget: GraphNode, dot: bool = false) -> Vector2:
	var handle: Control = widget if dot else widget.get_titlebar_hbox()
	canvas.scroll_offset += (handle.get_global_transform_with_canvas() * (handle.size * 0.5)) - canvas.get_global_rect().get_center()
	await get_tree().process_frame
	await get_tree().process_frame
	return handle.get_global_transform_with_canvas() * (handle.size * 0.5)


## Move a real graph node over a wire with Auto Insert enabled and undo the combined edit.
func _verify_auto_insert(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial_position := Vector2(94, 222)
	var graph_data := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(300, 64)},
		{"id": 3, "kind": &"print", "position": initial_position},
	], "connections": [_wire(1, &"then", 2, &"in")]}
	graph_script.call(&"set_graph", graph_data)
	await _settle_canvas(canvas)
	var first := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var second := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var inserted := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(first != null and second != null and inserted != null, "Find the execution wire and the node to drag over it."):
		return false
	var wire_center := (_socket_position(canvas, first, 0, true) + _socket_position(canvas, second, 0, false)) / 2.0
	var titlebar := inserted.get_titlebar_hbox()
	var handle := titlebar.get_global_rect().get_center()
	await _drag_mouse(graph_window, handle, wire_center - handle)
	await get_tree().process_frame
	var updated: Dictionary = graph_script.call(&"get_graph")
	if not _require(updated.nodes[2].position.distance_to(initial_position) >= 32.0 and
		_wire_present(graph_script, 1, &"then", 3, &"in") and _wire_present(graph_script, 3, &"then", 2, &"in"),
		"Dragging the card's title onto an eligible wire splices both sides without enabling a mode."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	updated = graph_script.call(&"get_graph")
	if not _require(updated.nodes[2].position == initial_position and updated.connections == graph_data.connections,
		"One Undo restores both the original node position and the original wire."):
		return false
	if not await _verify_insert_preview_transitions(graph_window, canvas, graph_script):
		return false
	if not await _verify_branch_auto_insert(graph_window, canvas, graph_script):
		return false
	if not await _verify_sequence_auto_insert(graph_window, canvas, graph_script, false):
		return false
	if not await _verify_sequence_auto_insert(graph_window, canvas, graph_script, true):
		return false
	if not await _verify_function_auto_insert(graph_window, canvas, graph_script):
		return false
	var tools_button := graph_window.find_child("GraphTools", true, false) as MenuButton
	if not _require(tools_button != null, "Find Graph Tools on the graph toolbar."):
		return false
	tools_button.show_popup()
	await get_tree().process_frame
	return await _choose_menu_item(tools_button.get_popup(), "Turn Off Auto Insert")


## Exercise a curved target, held Alt suppression, moving away and returning within one drag.
func _verify_insert_preview_transitions(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(395, 158)},
		{"id": 3, "kind": &"print", "position": Vector2(90, 253)},
	], "connections": [_wire(1, &"then", 2, &"in")]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [3]):
		return false
	var first := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var second := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var inserted := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var splice_point := (_socket_position(canvas, first, 0, true) + _socket_position(canvas, second, 0, false)) * 0.5
	var handle := inserted.get_titlebar_hbox().get_global_rect().get_center()
	graph_window.get_window().push_input(_mouse_button(handle, true), true)
	_pointer_motion(graph_window, handle, splice_point)
	await get_tree().process_frame
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0) and
		canvas.is_node_connected(&"GraphNode_3", 0, &"GraphNode_2", 0) and
		not canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and graph_script.call(&"get_graph") == initial,
		"Hovering a curved wire with a card previews its replacement topology without editing the resource."):
		return false
	var alt_modifier := InputEventKey.new()
	alt_modifier.keycode = KEY_ALT
	alt_modifier.alt_pressed = true
	alt_modifier.shift_pressed = true
	alt_modifier.pressed = true
	graph_window.get_window().push_input(alt_modifier, true)
	await get_tree().process_frame
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and
		not canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0),
		"Alt suppresses insertion and restores the original wire even with Shift held and no pointer movement."):
		return false
	alt_modifier.pressed = false
	alt_modifier.alt_pressed = false
	graph_window.get_window().push_input(alt_modifier, true)
	await get_tree().process_frame
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0),
		"Releasing Alt restores the pending splice at the same pointer location."):
		return false
	var away := splice_point + Vector2(0, 154)
	_pointer_motion(graph_window, splice_point, away)
	await get_tree().process_frame
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and
		not canvas.is_node_connected(&"GraphNode_3", 0, &"GraphNode_2", 0),
		"Moving clear of the target restores its original wire and removes both splice preview wires."):
		return false
	_pointer_motion(graph_window, away, splice_point)
	await get_tree().process_frame
	graph_window.get_window().push_input(_mouse_button(splice_point, false), true)
	await get_tree().process_frame
	if not _require(_wire_present(graph_script, 1, &"then", 3, &"in") and
		_wire_present(graph_script, 3, &"then", 2, &"in") and not _wire_present(graph_script, 1, &"then", 2, &"in"),
		"Returning to the same wire and releasing commits the last preview."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(graph_script.call(&"get_graph") == initial,
		"One Undo restores the original layout and curved wire after multiple preview changes.")


## Insert a Branch into a real execution wire without replacing its existing Condition.
func _verify_branch_auto_insert(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(300, 64)},
		{"id": 3, "kind": &"branch", "position": Vector2(94, 222)},
		{"id": 4, "kind": &"literal", "pin": {"id": &"value", "label": "Value", "type": TYPE_BOOL},
			"values": {&"value": true}, "position": Vector2(16, 360)},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(4, &"result", 3, &"condition")]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [3]):
		return false
	var first := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var next_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var branch := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var wire_center := (_socket_position(canvas, first, 0, true) + _socket_position(canvas, next_card, 0, false)) * 0.5
	var handle := branch.get_titlebar_hbox().get_global_rect().get_center()
	await _drag_mouse(graph_window, handle, wire_center - handle)
	await get_tree().process_frame
	var edited: Dictionary = graph_script.call(&"get_graph")
	if not _require(edited.connections.size() == 3 and _wire_present(graph_script, 1, &"then", 3, &"in") and
		_wire_present(graph_script, 3, &"true", 2, &"in") and _wire_present(graph_script, 4, &"result", 3, &"condition"),
		"Auto Insert routes a Branch through True, leaves False open, and retains the existing Condition wire."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	edited = graph_script.call(&"get_graph")
	return _require(edited.nodes == initial.nodes and edited.connections == initial.connections,
		"Undo restores the original Branch position, execution wire, and Condition wire.")


## Use the top available Sequence output and retain any wire on the first output.
func _verify_sequence_auto_insert(graph_window: Control, canvas: GraphEdit, graph_script: Script, top_occupied: bool) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(300, 64)},
		{"id": 3, "kind": &"sequence", "count": 3, "position": Vector2(94, 222)},
		{"id": 4, "kind": &"print", "position": Vector2(300, 354)},
	], "connections": [_wire(1, &"then", 2, &"in")]}
	if top_occupied:
		initial.connections.append(_wire(3, &"then_0", 4, &"in"))
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [3]):
		return false
	var start_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var target_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var sequence_card := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var wire_center := (_socket_position(canvas, start_card, 0, true) + _socket_position(canvas, target_card, 0, false)) * 0.5
	var handle := sequence_card.get_titlebar_hbox().get_global_rect().get_center()
	await _drag_mouse(graph_window, handle, wire_center - handle)
	await get_tree().process_frame
	var chosen: StringName = &"then_1" if top_occupied else &"then_0"
	var changed: Dictionary = graph_script.call(&"get_graph")
	if not _require(changed.connections.size() == initial.connections.size() + 1 and
		_wire_present(graph_script, 1, &"then", 3, &"in") and _wire_present(graph_script, 3, chosen, 2, &"in") and
		(not top_occupied or _wire_present(graph_script, 3, &"then_0", 4, &"in")),
		"Auto Insert uses the topmost available Sequence output and keeps occupied outputs. Links %s." % [changed.connections]):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	changed = graph_script.call(&"get_graph")
	return _require(changed.nodes == initial.nodes and changed.connections == initial.connections,
		"Undo restores the original Sequence position and wires.")


## Splice a function's wire without changing the event graph that shares its node IDs.
func _verify_function_auto_insert(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(16, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(300, 64)},
	], "connections": [_wire(1, &"then", 2, &"in")], "functions": [{
		"id": 1, "name": &"splice_probe", "inputs": [], "outputs": [], "graph": {"nodes": [
			{"id": 1, "kind": &"function_entry", "definition": 1, "position": Vector2(16, 64)},
			{"id": 2, "kind": &"return", "definition": 1, "position": Vector2(300, 64)},
			{"id": 3, "kind": &"print", "position": Vector2(94, 222)},
		], "connections": [_wire(1, &"then", 2, &"in")]},
	}]}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var outline := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var function_group := _find_entry(outline, "Functions") if outline != null else null
	var function_row := function_group.get_first_child() if function_group != null else null
	if not _require(function_row != null and function_row.get_text(0) == "splice_probe", "Find the function graph for nested Auto Insert."):
		return false
	function_row.select(0)
	outline.item_activated.emit()
	await _settle_canvas(canvas)
	if not _select(canvas, [3]):
		return false
	var entry := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var return_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var inserted := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var splice_point := (_socket_position(canvas, entry, 0, true) + _socket_position(canvas, return_card, 0, false)) * 0.5
	var handle := inserted.get_titlebar_hbox().get_global_rect().get_center()
	graph_window.get_window().push_input(_mouse_button(handle, true), true)
	_pointer_motion(graph_window, handle, splice_point)
	await get_tree().process_frame
	if not _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_3", 0) and
		canvas.is_node_connected(&"GraphNode_3", 0, &"GraphNode_2", 0) and
		not canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0) and graph_script.call(&"get_graph") == initial,
		"Preview the function splice while leaving the whole script intact."):
		return false
	graph_window.get_window().push_input(_mouse_button(splice_point, false), true)
	await get_tree().process_frame
	var changed: Dictionary = graph_script.call(&"get_graph")
	var function_connections: Array = changed.functions[0].graph.connections
	if not _require(changed.nodes == initial.nodes and changed.connections == initial.connections and
		function_connections.size() == 2 and function_connections.has(_wire(1, &"then", 3, &"in")) and
		function_connections.has(_wire(3, &"then", 2, &"in")), "Commit the splice inside the function only."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _require(graph_script.call(&"get_graph") == initial, "One Undo restores the nested position and topology."):
		return false
	if not await _open_event_graph(graph_window):
		return false
	await _settle_canvas(canvas)
	return _require(canvas.is_node_connected(&"GraphNode_1", 0, &"GraphNode_2", 0),
		"Returning to the event graph shows its original wire.")


## Reset zoom and scroll before using native socket and wire coordinates.
func _settle_canvas(canvas: GraphEdit) -> void:
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame


## Click at a position inside the graph viewport with the requested mouse button.
func _click_graph(graph_window: Control, point: Vector2, button: MouseButton, shift: bool = false) -> void:
	var pressed_event := _mouse_button(point, true, button)
	pressed_event.shift_pressed = shift
	graph_window.get_window().push_input(pressed_event, true)
	var released_event := _mouse_button(point, false, button)
	released_event.shift_pressed = shift
	graph_window.get_window().push_input(released_event, true)
	await get_tree().process_frame


## Draw a vertical cut stroke across the middle of the visible wire.
func _sweep_wire(graph_window: Control, midpoint: Vector2) -> void:
	var first := midpoint - Vector2(0, 22)
	var last := midpoint + Vector2(0, 22)
	graph_window.get_window().push_input(_mouse_button(first, true, MOUSE_BUTTON_MIDDLE, true), true)
	var motion := InputEventMouseMotion.new()
	motion.position = last
	motion.global_position = last
	motion.relative = last - first
	motion.button_mask = MOUSE_BUTTON_MASK_MIDDLE
	motion.alt_pressed = true
	graph_window.get_window().push_input(motion, true)
	graph_window.get_window().push_input(_mouse_button(last, false, MOUSE_BUTTON_MIDDLE, true), true)
	await get_tree().process_frame


## Collapse a call with exclusive and shared data sources, then verify multi-path macro extraction.
func _verify_extraction(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var function_graph := _rewire_graph()
	var message_pin := {"id": &"message", "label": "Message", "type": TYPE_STRING}
	var shared_pin := {"id": &"shared", "label": "Shared", "type": TYPE_STRING}
	function_graph["functions"] = [{"id": 1, "name": &"existing_function", "inputs": [message_pin, shared_pin], "outputs": [],
		"graph": {"nodes": [{"id": 1, "kind": &"function_entry"}, {"id": 2, "kind": &"return"}],
			"connections": [_wire(1, &"then", 2, &"in")]}}]
	function_graph["nodes"][1]["kind"] = &"call_function"
	function_graph["nodes"][1]["definition"] = 1
	function_graph["nodes"].append({"id": 5, "kind": &"print", "position": Vector2(704, 64), "message": "outside"})
	function_graph["nodes"].append({"id": 8, "kind": &"reroute", "pin": message_pin, "position": Vector2(240, 224)})
	function_graph["connections"] = [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 5, &"in"),
		_wire(3, &"result", 8, &"value"), _wire(8, &"value", 2, &"message"),
		_wire(4, &"result", 2, &"shared"), _wire(4, &"result", 5, &"message")]
	graph_script.call(&"set_graph", function_graph)
	await _settle_canvas(canvas)
	if not await _choose_node_command(graph_window, canvas, 2, "Collapse to Function"):
		return false
	var collapsed: Dictionary = graph_script.call(&"get_graph")
	var functions: Array = collapsed.get("functions", [])
	if not _require(functions.size() == 2 and functions[1].inputs.size() == 1 and functions[1].inputs[0].type == TYPE_STRING and
		functions[1].inputs[0].label == "Shared", "Function collapse exposes just the shared String parameter."):
		return false
	var body_wires: Array = functions[1].graph.connections
	if not _require(body_wires.has(_wire(3, &"result", 8, &"value")) and body_wires.has(_wire(8, &"value", 2, &"message")) and
		collapsed.nodes.all(func(node_record: Dictionary) -> bool: return node_record.id != 3 and node_record.id != 8),
		"The exclusive literal and reroute move inside the function and keep their original wires."):
		return false
	if not _require(_wire_present(graph_script, 1, &"then", 9, &"in") and _wire_present(graph_script, 4, &"result", 9, &"input_1") and
		_wire_present(graph_script, 9, &"then", 5, &"in") and _wire_present(graph_script, 4, &"result", 5, &"message"),
		"Function collapse preserves execution and leaves the shared source wired to both outer consumers."):
		return false
	await _settle_canvas(canvas)
	var replacement := canvas.get_node_or_null(^"GraphNode_9") as GraphNode
	if not _require(replacement != null and replacement.position_offset == Vector2(384, 64),
		"The replacement call stays at the selected node position."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _require(_wire_present(graph_script, 3, &"result", 8, &"value") and _wire_present(graph_script, 8, &"value", 2, &"message"),
		"Undo restores the extracted nodes and original wires."):
		return false
	await _shortcut(graph_window, canvas, KEY_Y)
	if not _require(_wire_present(graph_script, 4, &"result", 9, &"input_1"), "Redo restores the collapse and shared data pin."):
		return false
	graph_script.call(&"set_graph", _macro_graph())
	await _settle_canvas(canvas)
	if not await _choose_node_command(graph_window, canvas, 2, "Collapse to Function"):
		return false
	var unchanged: Dictionary = graph_script.call(&"get_graph")
	if not _require(unchanged.get("functions", []).is_empty() and _wire_present(graph_script, 2, &"true", 3, &"in") and
		_wire_present(graph_script, 2, &"false", 4, &"in"), "Function collapse leaves multi-path selections intact."):
		return false
	var problem_prompt := graph_window.find_child("VisualScriptProblem", true, false) as Control
	if not _require(problem_prompt != null and problem_prompt.visible, "Invalid collapse shows an acknowledgment inside the graph workbench."):
		return false
	var covered_shortcut := InputEventKey.new()
	covered_shortcut.keycode = KEY_SPACE
	covered_shortcut.pressed = true
	graph_window.get_window().push_input(covered_shortcut, true)
	await _click_graph(graph_window, canvas.get_global_rect().get_center(), MOUSE_BUTTON_RIGHT)
	var palette_prompt := graph_window.find_child("GraphNodePalette", true, false) as Window
	var menu_prompt := graph_window.find_child("GraphContextMenu", true, false) as Window
	if not _require(problem_prompt.visible and not palette_prompt.visible and not menu_prompt.visible and
		graph_script.call(&"get_graph") == unchanged, "An open graph prompt blocks viewport gestures and workbench shortcuts."):
		return false
	var dismiss_event := InputEventKey.new()
	dismiss_event.keycode = KEY_ESCAPE
	dismiss_event.physical_keycode = KEY_ESCAPE
	dismiss_event.pressed = true
	graph_window.get_window().push_input(dismiss_event, true)
	await get_tree().process_frame
	if not _require(not problem_prompt.visible and graph_window.get_window().gui_get_focus_owner() == canvas,
		"Escape dismisses the problem and restores the canvas for further editing."):
		return false
	if not await _choose_node_command(graph_window, canvas, 2, "Collapse to Macro"):
		return false
	var expanded: Dictionary = graph_script.call(&"get_graph")
	var macros: Array = expanded.get("macros", [])
	if not _require(macros.size() == 1 and macros[0].outputs.size() == 2 and macros[0].outputs[0].exec and macros[0].outputs[1].exec,
		"Macro collapse keeps both execution exits as separate pins."):
		return false
	return _require(_wire_present(graph_script, 5, &"output_1", 3, &"in") and _wire_present(graph_script, 5, &"output_2", 4, &"in"),
		"The two macro exits continue to their original separate nodes.")


## Create and rename a variable, create a function, and reopen the main graph.
func _verify_declarations(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", _operation_graph())
	await get_tree().process_frame
	var scene_outline := graph_window.find_child("VisualScriptSceneTree", true, false)
	var declaration_tree := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var details := graph_window.find_child("VisualScriptDetails", true, false) as Control
	if not _require(scene_outline == null and declaration_tree != null and details != null, "Declarations and Details remain available beside the native Scene dock."):
		return false
	var variable_group := _find_entry(declaration_tree, "Variables")
	if not _add_declaration(declaration_tree, variable_group, "Variables"):
		return false
	await get_tree().process_frame
	var name_field := details.find_child("VisualScriptDeclarationName", true, false) as LineEdit
	if not _require(name_field != null, "The new variable's name is editable from Details."):
		return false
	name_field.text = "show_tip"
	name_field.text_submitted.emit(name_field.text)
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.get("variables", []).size() == 1 and graph_data.variables[0].name == "show_tip" and graph_data.variables[0].pin.type == TYPE_BOOL,
		"Variable creation and rename persist a typed declaration."):
		return false
	if not await _verify_type_picker(graph_window, details, graph_script):
		return false
	var function_group := _find_entry(declaration_tree, "Functions")
	if not _add_declaration(declaration_tree, function_group, "Functions"):
		return false
	await get_tree().process_frame
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.get("functions", []).size() == 1 and _find_entry(declaration_tree, "Local Variables") != null,
		"Creating a function opens its own graph and shows Local Variables."):
		return false
	if not _require(_find_entry(declaration_tree, "Event Graph") == null, "The sidebar lists declarations without an Event Graph row."):
		return false
	if not await _open_event_graph(graph_window):
		return false
	await get_tree().process_frame
	var event_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	return _require(event_widget != null and event_widget.title == "On Ready" and _find_entry(declaration_tree, "Local Variables") == null,
		"Opening Event Graph restores its nodes and hides function-local declarations.")


## Filter the grouped type picker to a registered object class and select it.
func _verify_type_picker(graph_window: Control, details: Control, graph_script: Script) -> bool:
	var type_button := details.find_child("VisualScriptDeclarationType", true, false) as Button
	if not _require(type_button != null, "The variable editor opens a type picker."):
		return false
	type_button.pressed.emit()
	await get_tree().process_frame
	var type_search := graph_window.find_child("VisualScriptTypeSearch", true, false) as LineEdit
	var type_tree := graph_window.find_child("VisualScriptTypeTree", true, false) as Tree
	if not _require(type_search != null and type_tree != null, "The picker has a search field and type tree."):
		return false
	type_search.text = "Node3D"
	type_search.text_changed.emit(type_search.text)
	var selected_type := type_tree.get_selected()
	if not _require(selected_type != null and selected_type.get_text(0) == "Node3D" and selected_type.get_parent() != type_tree.get_root(),
		"Searching finds the exact native class in a category."):
		return false
	type_search.text_submitted.emit(type_search.text)
	await get_tree().process_frame
	var type_confirmation := graph_window.find_child("VisualScriptChangeVariableType", true, false) as Control
	var confirm_type := type_confirmation.find_child("ConfirmDialog", true, false) as Button if type_confirmation != null else null
	if not _require(confirm_type != null and confirm_type.is_visible_in_tree(),
			"Selecting a native type asks before applying the declaration change."):
		return false
	confirm_type.pressed.emit()
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	return _require(graph_data.variables[0].pin.type == TYPE_OBJECT and graph_data.variables[0].pin["class"] == &"Node3D",
		"Selecting Node3D persists a native object type on the declaration.")


## Promote an unwired typed input and restore the original graph with one undo.
func _verify_promotion(graph_window: Control, canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"print", "position": Vector2(176, 64), "message": "Original text"},
	], "connections": []})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame
	var print_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(print_widget != null, "Find the typed Print input for variable promotion."):
		return false
	var socket := canvas.get_global_transform_with_canvas() * (print_widget.position + print_widget.get_input_port_position(1) * canvas.zoom)
	await _drag_mouse(graph_window, socket, Vector2(-96, 80))
	var top_result := result_tree.get_root().get_first_child()
	if not _require(result_tree.is_visible_in_tree() and top_result != null and top_result.get_text(0) == "Promote to Variable" and
		result_tree.get_selected() == top_result, "Dropping the input wire on blank canvas immediately offers Promote to Variable at the top."):
		return false
	search_field.text_submitted.emit("")
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var declarations: Array = graph_data.get("variables", [])
	if not _require(declarations.size() == 1 and declarations[0].name == "message" and declarations[0].pin.type == TYPE_STRING and
		declarations[0].value == "Original text", "Promotion preserves the input type and original value."):
		return false
	if not _require(_wire_present(graph_script, 2, &"value", 1, &"message"), "Promotion inserts a variable getter and connects it to the source input."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.get("variables", []).is_empty() and graph_data.nodes.size() == 1 and graph_data.connections.is_empty(),
		"One undo removes the variable declaration, getter, and wire.")


## Promote both directions from actual right clicks on typed data sockets.
func _verify_pin_promotion(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial := _rewire_graph()
	initial.connections.pop_back()
	initial.nodes[1].message = "Starting message"
	initial.nodes[1].position = Vector2(184, 80)
	initial.nodes[2].position = Vector2(64, 244)
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	var recipient := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var context_menu := graph_window.find_child("GraphContextMenu", true, false) as PopupMenu
	if not _require(recipient != null and context_menu != null, "Find the input socket and native context menu."):
		return false
	await _click_graph(graph_window, _socket_position(canvas, recipient, 1, false), MOUSE_BUTTON_RIGHT)
	if not _require(context_menu.visible and _menu_item_index(context_menu, "Delete") < 0, "A typed socket opens its pin menu."):
		return false
	if not await _choose_menu_item(context_menu, "Promote to Variable"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var declarations: Array = graph_data.get("variables", [])
	if not _require(declarations.size() == 1 and declarations[0].pin.type == TYPE_STRING and declarations[0].value == "Starting message" and
		graph_data.nodes[4].kind == &"get_variable" and _wire_present(graph_script, 5, &"value", 2, &"message"),
		"Promoting an input preserves its type and literal and wires a getter to the socket."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data == initial, "Undo restores the typed input and its original literal."):
		return false

	var producer := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(producer != null, "Find the literal's typed output socket."):
		return false
	await _click_graph(graph_window, _socket_position(canvas, producer, 0, true), MOUSE_BUTTON_RIGHT)
	if not await _choose_menu_item(context_menu, "Promote to Variable"):
		return false
	graph_data = graph_script.call(&"get_graph")
	declarations = graph_data.get("variables", [])
	if not _require(declarations.size() == 1 and declarations[0].pin.type == TYPE_STRING and
		graph_data.nodes[4].kind == &"set_variable" and _wire_present(graph_script, 3, &"result", 5, &"value"),
		"Promoting an output creates a setter with a value input connected to the source."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data == initial, "Undo restores the original graph after output promotion.")


## Preserve a macro's already connected default lane when adding named routes.
func _verify_macro_signature(graph_window: Control, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready"}, {"id": 2, "kind": &"call_macro", "definition": 1}, {"id": 3, "kind": &"print"},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in")], "macros": [{
		"id": 1, "name": "route_once", "inputs": [], "outputs": [], "graph": {
			"nodes": [{"id": 1, "kind": &"macro_entry", "definition": 1}, {"id": 2, "kind": &"return", "definition": 1}],
			"connections": [_wire(1, &"then", 2, &"in")],
		},
	}]})
	await get_tree().process_frame
	var declaration_tree := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var details := graph_window.find_child("VisualScriptDetails", true, false) as Control
	var macro_group := _find_entry(declaration_tree, "Macros")
	if not _require(macro_group != null and macro_group.get_first_child() != null, "The wired macro is present in the declarations panel."):
		return false
	macro_group.get_first_child().select(0)
	declaration_tree.item_selected.emit()
	await get_tree().process_frame
	if not await _press_named_button(details, "+ Execution Input") or not await _press_named_button(details, "+ Execution Output"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var edited_macro: Dictionary = graph_data.macros[0]
	if not _require(edited_macro.inputs.size() == 2 and edited_macro.outputs.size() == 2, "Each added route keeps its original default lane."):
		return false
	var internal_edge: Dictionary = edited_macro.graph.connections[0]
	return _require(_wire_present(graph_script, 1, &"then", 2, &"input_default") and _wire_present(graph_script, 2, &"output_default", 3, &"in") and
		internal_edge.from_pin == &"input_default" and internal_edge.to_pin == &"output_default", "Macro signature migration keeps caller and internal wires connected.")


## Click a visible Details button and allow its commit to rebuild the panel.
func _press_named_button(details: Control, label: String) -> bool:
	for child: Node in details.find_children("*", "Button", true, false):
		var candidate := child as Button
		if candidate.text == label:
			candidate.pressed.emit()
			await get_tree().process_frame
			return true
	return _require(false, "Details shows the %s button." % label)


## A context opened from a typed node output offers and wires inherited native signals.
func _verify_wired_signal(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"create_node", "class": &"HSlider", "position": Vector2(64, 64)},
	], "connections": []})
	await get_tree().process_frame
	var slider_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(slider_widget != null, "Find the HSlider constructor with its typed output."):
		return false
	canvas.connection_to_empty.emit(slider_widget.name, 1, Vector2(384, 64))
	const QUERY := "On Value Changed"
	search_field.text = QUERY
	search_field.text_changed.emit(QUERY)
	var action_item := result_tree.get_selected()
	if not _require(action_item != null and action_item.get_text(0) == QUERY and
			action_item.get_parent().get_text(0) == "Range" and not action_item.get_parent().collapsed,
			"Dragging a typed HSlider output offers its inherited Range signal."):
		return false
	if not _require(action_item.get_tooltip_text(0).begins_with("Emitted when value changes."),
		"The picker shows the inherited signal description from Godot's class reference."):
		return false
	search_field.text_submitted.emit(QUERY)
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 2 and graph_data.nodes[1].kind == &"native_signal" and graph_data.nodes[1].get("wired_target", false),
		"Selecting a native signal creates an event with a wired target pin."):
		return false
	var event_record: Dictionary = graph_data.nodes[1]
	var event_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(event_card != null and event_card.tooltip_text.begins_with("Emitted when value changes."),
		"The native signal card keeps its description after insertion."):
		return false
	return _require(event_record["class"] == &"Range" and event_record.signal == &"value_changed" and event_record.inputs.size() == 1 and
		event_record.inputs[0].type == TYPE_FLOAT and _wire_present(graph_script, 1, &"node", 2, &"target"),
		"The event keeps the typed signal payload and auto-connects the original HSlider output.")


## Read the description of a native method in the picker and again on the graph card.
func _verify_method_help(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"create_node", "class": &"AudioStreamPlayer3D", "position": Vector2(64, 64)},
	], "connections": []})
	await get_tree().process_frame
	var construction := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(construction != null, "Find the AudioStreamPlayer3D constructor."):
		return false
	canvas.connection_to_empty.emit(construction.name, 1, Vector2(384, 64))
	search_field.text = "Play"
	search_field.text_changed.emit(search_field.text)
	var choice := result_tree.get_selected()
	if not _require(choice != null and choice.get_text(0) == "Play" and choice.get_tooltip_text(0).begins_with("Queues the audio to play"),
		"The native Play method shows descriptive prose rather than a signature."):
		return false
	search_field.text_submitted.emit(search_field.text)
	await get_tree().process_frame
	var call_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	return _require(call_card != null and call_card.tooltip_text.begins_with("Queues the audio to play"),
		"The Play card also displays the native method description.")


## Select methods from real typed outputs and check their receiver wire and native help.
func _verify_builtin_method_picker(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	var samples: Array[Dictionary] = [
		{"type": TYPE_STRING, "value": "door_exit", "owner": &"String", "query": "Begins With", "method": &"begins_with", "return_type": TYPE_BOOL},
		{"type": TYPE_STRING, "value": "door_exit", "owner": &"String", "query": "Ends With", "method": &"ends_with", "return_type": TYPE_BOOL},
		{"type": TYPE_STRING_NAME, "value": &"door_exit", "owner": &"StringName", "query": "Ends With", "method": &"ends_with", "return_type": TYPE_BOOL},
		{"type": TYPE_VECTOR2, "value": Vector2(4, 7), "owner": &"Vector2", "query": "Dot", "method": &"dot", "return_type": TYPE_FLOAT},
		{"type": TYPE_DICTIONARY, "value": {&"door": "Exit"}, "owner": &"Dictionary", "query": "Has", "method": &"has", "return_type": TYPE_BOOL},
	]
	for example: Dictionary in samples:
		var receiver_pin := {"id": &"value", "label": "Value", "type": example.type}
		graph_script.call(&"set_graph", {"version": 1, "variables": [
			{"id": 1, "name": "sample", "pin": receiver_pin, "value": example.value},
		], "nodes": [
			{"id": 1, "kind": &"get_variable", "variable": 1, "position": Vector2(64, 64)},
		], "connections": []})
		await get_tree().process_frame
		var source := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
		if not _require(source != null and source.get_output_port_count() == 1, "Find the typed variable card for %s." % example.owner):
			return false
		canvas.connection_to_empty.emit(source.name, 0, Vector2(384, 64))
		if not await _choose_palette_item(search_field, result_tree, example.query):
			return false
		var authored: Dictionary = graph_script.call(&"get_graph")
		var member: Dictionary = authored.nodes[1].member
		var call_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
		if not _require(member.class == example.owner and member.name == example.method and
				member.output.type == example.return_type and _wire_present(graph_script, 1, &"value", 2, &"target") and
				call_card != null and not call_card.tooltip_text.is_empty(),
				"%s.%s uses the exact built-in type, supplies its typed result, and connects the receiver automatically." % [example.owner, example.method]):
			return false
	for example: Dictionary in [
		{"query": "clampf", "name": &"clampf", "owner": &"@GlobalScope", "flag": "utility", "port_count": 4},
		{"query": "JSON.stringify", "name": &"stringify", "owner": &"JSON", "flag": "static", "port_count": 5},
	]:
		graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
		await get_tree().process_frame
		canvas.popup_request.emit(Vector2(384, 64))
		search_field.text = example.query
		search_field.text_changed.emit(search_field.text)
		if not _require(result_tree.get_selected() != null, "Find a receiver-free call for %s." % example.query):
			return false
		search_field.text_submitted.emit(search_field.text)
		await get_tree().process_frame
		var authored: Dictionary = graph_script.call(&"get_graph")
		var call_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
		if not _require(authored.nodes.size() == 1 and authored.nodes[0].member.get(example.flag, false) and
				authored.nodes[0].member.class == example.owner and authored.nodes[0].member.name == example.name and
				call_card != null and call_card.get_input_port_count() == example.port_count,
				"%s places the receiver-free method with its engine argument pins." % example.query):
			return false
	return true


## Select named global constants from free and typed pin pickers.
func _verify_global_constant_picker(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"for", "position": Vector2(180, 96)},
		{"id": 2, "kind": &"branch", "position": Vector2(180, 260)},
	], "connections": []})
	await get_tree().process_frame
	canvas.popup_request.emit(Vector2(68, 64))
	if not await _choose_palette_item(search_field, result_tree, "MOUSE_BUTTON_LEFT"):
		return false
	var loop_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(loop_card != null, "Find the For input before opening a compatible picker."):
		return false
	canvas.connection_from_empty.emit(loop_card.name, 1, Vector2(64, 120))
	if not await _choose_palette_item(search_field, result_tree, "KEY_ESCAPE"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var key_card := canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	if not _require(graph_data.nodes[2].get("global_constant") == &"MOUSE_BUTTON_LEFT" and
			graph_data.nodes[3].get("global_constant") == &"KEY_ESCAPE" and _wire_present(graph_script, 4, &"result", 1, &"first") and
			key_card != null and key_card.get_input_port_count() == 0 and key_card.get_output_port_count() == 1,
			"A searched constant creates one read-only output and auto-wires to the waiting integer input."):
		return false
	var branch_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	canvas.connection_from_empty.emit(branch_card.name, 1, Vector2(64, 280))
	search_field.text = "KEY_ESCAPE"
	search_field.text_changed.emit(search_field.text)
	var selected_item := result_tree.get_selected()
	var no_incompatible_action := selected_item == null or selected_item.get_text(0) != "KEY_ESCAPE"
	search_field.get_window().hide()
	return _require(no_incompatible_action, "A context-sensitive Boolean input filters out integer constants.")


## Check direct and inherited members, reversible filtering, and actual socket tooltips.
func _verify_context_sensitive_picker(graph_window: Control, canvas: GraphEdit, search_field: LineEdit,
		result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"create_node", "class": &"Camera3D", "position": Vector2(64, 64)},
		{"id": 2, "kind": &"branch", "position": Vector2(224, 210)},
	], "connections": []})
	canvas.scroll_offset = Vector2.ZERO
	canvas.zoom = 1.0
	await get_tree().process_frame
	var source_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var branch_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(source_card != null and branch_card != null, "Find fixture cards before hitting native sockets."):
		return false
	var source_socket := source_card.position + source_card.get_output_port_position(1) * canvas.zoom
	var branch_socket := branch_card.position + branch_card.get_input_port_position(1) * canvas.zoom
	if not _require(canvas.get_tooltip(source_socket).contains("Camera3D") and
			canvas.get_tooltip(branch_socket).to_lower().contains("bool"),
			"Hovering an output and an input socket shows their declared types."):
		return false
	canvas.connection_to_empty.emit(source_card.name, 1, Vector2(365, 64))
	var checkbox := graph_window.find_child("GraphPaletteContextSensitive", true, false) as BaseButton
	var picker_root := result_tree.get_root()
	var first_row := picker_root.get_first_child()
	if not _require(checkbox != null and checkbox.button_pressed and first_row != null and
			first_row.get_metadata(0) is Dictionary and result_tree.get_selected() == first_row,
			"Context Sensitive starts enabled and selects the first direct receiver action."):
		return false
	var row := first_row
	var direct_kinds: Dictionary = {}
	while row != null and row.get_metadata(0) is Dictionary:
		var record: Dictionary = row.get_metadata(0)
		var member: Dictionary = record.get("member", record)
		if member.get("class", &"") != &"Camera3D":
			break
		direct_kinds[record.kind] = true
		row = row.get_next()
	if not _require(direct_kinds.has(&"get_property") and direct_kinds.has(&"set_property") and direct_kinds.has(&"call_method") and
			row != null and row.get_text(0) == "Node3D" and _find_entry(result_tree, "Camera3D") == null,
			"Camera3D Get, Set, and method actions are top-level and precede the first inherited class."):
		return false
	for owner: String in ["Node3D", "Node", "Object"]:
		if not _require(row != null and row.get_text(0) == owner and row.collapsed and row.get_child_count() > 0,
				"Inherited %s members start under a collapsed declaring-class category in ancestry order." % owner):
			return false
		var inherited_kinds: Dictionary = {}
		for child in row.get_children():
			var record: Dictionary = child.get_metadata(0)
			var member: Dictionary = record.get("member", record)
			if not _require(child.get_child_count() == 0 and String(member.get("class", &"")) == owner,
					"%s contains no member subgroups and every action is declared by that class." % owner):
				return false
			inherited_kinds[record.kind] = true
		if owner == "Node3D" and not _require(inherited_kinds.has(&"get_property") and inherited_kinds.has(&"set_property") and
				inherited_kinds.has(&"call_method") and inherited_kinds.has(&"native_signal"),
				"The Node3D category mixes Get, Set, method and signal actions in one list."):
			return false
		row = row.get_next()
	var other_group := row
	while other_group != null:
		if not _require(other_group.get_child_count() == 0 or other_group.collapsed,
				"Other categories start collapsed during a contextual pin drag."):
			return false
		other_group = other_group.get_next()
	const QUERY := "JSON.parse_string"
	search_field.text = QUERY
	search_field.text_changed.emit(QUERY)
	if not _require(result_tree.get_selected() == null, "An unrelated static call stays hidden in Context Sensitive mode."):
		return false
	checkbox.button_pressed = false
	await get_tree().process_frame
	var selected_option := result_tree.get_selected()
	if not _require(search_field.text == QUERY and selected_option != null and selected_option.get_text(0) == "Parse String",
			"Turning Context Sensitive off preserves the query and reveals the unrelated call."):
		return false
	checkbox.button_pressed = true
	await get_tree().process_frame
	if not _require(search_field.text == QUERY and result_tree.get_selected() == null, "Re-enabling the filter keeps the query and hides the call again."):
		return false
	checkbox.button_pressed = false
	await get_tree().process_frame
	search_field.text_submitted.emit(QUERY)
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 3 and graph_data.nodes.back().member.name == &"parse_string" and
			graph_data.connections.is_empty() and graph_data.nodes.back().position == Vector2(368, 64),
			"An incompatible action is created at the original anchor without attaching an invalid wire."):
		return false
	source_card = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	canvas.connection_to_empty.emit(source_card.name, 1, Vector2(365, 150))
	if not await _choose_palette_item(search_field, result_tree, "Set Fov"):
		return false
	if not _require(_wire_present(graph_script, 1, &"node", 4, &"target"),
			"Turning the filter off still auto-connects a compatible receiver action."):
		return false
	canvas.popup_request.emit(Vector2(365, 64))
	if not _require(not checkbox.button_pressed, "The picker remembers the context setting when it reopens."):
		return false
	checkbox.button_pressed = true
	search_field.get_window().hide()
	return true


## Send a left mouse drag through the real GraphEdit viewport and check the saved reroute position.
func _verify_reroute_drag(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var dot_position := Vector2(256, 192)
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(32, 192)},
		{"id": 2, "kind": &"reroute", "mode": &"exec", "position": dot_position},
		{"id": 3, "kind": &"print", "position": Vector2(480, 192)},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in")]})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame
	var dot := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(dot != null, "Find the reroute widget after rebuilding its graph."):
		return false
	var origin := dot.get_global_transform_with_canvas() * (dot.size * 0.5)
	await _drag_mouse(graph_window, origin, Vector2(-64, 48))
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	return _require(graph_data.nodes[1].position.distance_to(dot_position) >= 32 and
		_wire_present(graph_script, 1, &"then", 2, &"in") and _wire_present(graph_script, 2, &"then", 3, &"in"),
		"Dragging the reroute's center moves its saved position while keeping both connections.")


## Edit a numeric literal beside its label, then wire the pin and restore the value with Undo.
func _verify_inline_pin_literals(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"for", "position": Vector2(36, 24)},
		{"id": 2, "kind": &"literal", "position": Vector2(36, 190),
			"pin": {"id": &"value", "label": "Value", "type": TYPE_INT}, "value": 9},
	], "connections": []})
	await _settle_canvas(canvas)
	var loop_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(loop_card != null and loop_card.get_input_port_count() == 4 and
			loop_card.get_output_port_count() == 3, "A For node exposes all of its named ports."):
		return false
	for input_port: int in range(1, 4):
		var pin_row := loop_card.get_child(loop_card.get_input_port_slot(input_port)) as HBoxContainer
		var fields := pin_row.find_child(["Literal_first", "Literal_last", "Literal_step"][input_port - 1], false, false)
		if not _require(fields != null and fields.find_children("*", "LineEdit", false, false).size() == 1,
				"Each numeric literal is editable within its own pin row."):
			return false
		if input_port < loop_card.get_output_port_count() and not _require(
				loop_card.get_output_port_slot(input_port) == loop_card.get_input_port_slot(input_port),
				"An inline editor preserves the paired output on the same row."):
			return false
	var first_field := loop_card.find_child("Literal_first", true, false).get_child(0) as LineEdit
	var field_center: Vector2 = first_field.get_global_transform_with_canvas() * (first_field.size * 0.5)
	await _click_graph(graph_window, field_center, MOUSE_BUTTON_LEFT)
	if not _require(first_field.has_focus(), "Clicking the inline field focuses it without starting a canvas gesture."):
		return false
	first_field.select_all()
	var digit := InputEventKey.new()
	digit.keycode = KEY_7
	digit.unicode = 55
	digit.pressed = true
	graph_window.get_window().push_input(digit, true)
	await get_tree().process_frame
	var saved_graph: Dictionary = graph_script.call(&"get_graph")
	if not _require(saved_graph.nodes[0].values.get(&"first") == 7, "Typing updates the graph's stored numeric literal."):
		return false
	canvas.connection_request.emit(&"GraphNode_2", 0, &"GraphNode_1", 1)
	await get_tree().process_frame
	loop_card = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(loop_card.find_child("Literal_first", true, false) == null and
			_wire_present(graph_script, 2, &"result", 1, &"first"), "A connected pin hides its inline editor."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	loop_card = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var restored := loop_card.find_child("Literal_first", true, false)
	return _require(restored != null and (restored.get_child(0) as LineEdit).text == "7" and
			not _wire_present(graph_script, 2, &"result", 1, &"first"), "Undo restores the unwired literal beside its label.")


## Drag one data reroute output to two compatible consumers using native hit testing.
func _verify_reroute_fanout(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var string_pin := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"literal", "pin": string_pin, "position": Vector2(32, 24), "values": {&"value": "Shared"}},
		{"id": 2, "kind": &"reroute", "mode": &"data", "pin": string_pin, "position": Vector2(48, 176)},
		{"id": 3, "kind": &"print", "position": Vector2(192, 16)},
		{"id": 4, "kind": &"print", "position": Vector2(192, 224)},
	], "connections": [_wire(1, &"result", 2, &"value")]})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame
	for target_id: int in [3, 4]:
		var dot := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
		var consumer := canvas.get_node_or_null(NodePath("GraphNode_%d" % target_id)) as GraphNode
		if not _require(dot != null and consumer != null, "Find the reroute and Print input for a data-wire drag."):
			return false
		var origin := _socket_position(canvas, dot, 0, true) + Vector2(6, 0)
		var target := _socket_position(canvas, consumer, 1, false)
		await _drag_mouse(graph_window, origin, target - origin)
		if not _require(_wire_present(graph_script, 2, &"value", target_id, &"message"),
			"Dragging the reroute output connects Print input %d." % target_id):
			return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	return _require(graph_data.connections.size() == 3 and _wire_present(graph_script, 1, &"result", 2, &"value") and
		_wire_present(graph_script, 2, &"value", 3, &"message") and _wire_present(graph_script, 2, &"value", 4, &"message"),
		"The reroute keeps one incoming data wire and both outgoing wires.")


## Convert a native port center to the graph window's viewport coordinates.
func _socket_position(canvas: GraphEdit, widget: GraphNode, port: int, outgoing: bool) -> Vector2:
	var port_position := widget.get_output_port_position(port) if outgoing else widget.get_input_port_position(port)
	return canvas.get_global_transform_with_canvas() * (widget.position + port_position * canvas.zoom)


## Straighten a staggered execution chain using actual port heights at reduced zoom.
func _verify_straighten_connections(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var initial_positions := [Vector2(32, 96), Vector2(192, 16), Vector2(352, 176), Vector2(96, 288)]
	var chain_wires := [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in")]
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": initial_positions[0]},
		{"id": 2, "kind": &"print", "position": initial_positions[1]},
		{"id": 3, "kind": &"add_child", "position": initial_positions[2]},
		{"id": 4, "kind": &"print", "position": initial_positions[3]},
	], "connections": chain_wires})
	canvas.zoom = 0.7
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame
	if not _select(canvas, [1, 2, 3, 4]):
		return false
	canvas.grab_focus()
	var key_event := InputEventKey.new()
	key_event.keycode = KEY_Q
	key_event.pressed = true
	graph_window.get_window().push_input(key_event, true)
	await get_tree().process_frame
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	for index in range(initial_positions.size()):
		var actual: Vector2 = graph_data.nodes[index].position
		var initial: Vector2 = initial_positions[index]
		if not _require(is_equal_approx(actual.x, initial.x), "Straightening keeps every selected node's horizontal position."):
			return false
	if not _require(graph_data.nodes[3].position == initial_positions[3] and graph_data.connections == chain_wires,
		"Straightening keeps the isolated selected node and existing wires unchanged."):
		return false
	var entry := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var middle := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var tail := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(entry != null and middle != null and tail != null, "Find the three connected widgets after straightening."):
		return false
	var first_gap := absf(_socket_position(canvas, entry, 0, true).y - _socket_position(canvas, middle, 0, false).y)
	var last_gap := absf(_socket_position(canvas, middle, 0, true).y - _socket_position(canvas, tail, 0, false).y)
	if not _require(first_gap <= 1.0 and last_gap <= 1.0, "Q makes both connected wire endpoints level at reduced zoom."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	for index in range(initial_positions.size()):
		if not _require(graph_data.nodes[index].position == initial_positions[index], "One Undo restores every original position."):
			return false
	canvas.zoom = 1.0
	return true


## Create a declaration and event entry directly from the Event Graph picker.
func _verify_create_custom_event(graph_window: Control, canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
	await _settle_canvas(canvas)
	await _click_graph(graph_window, canvas.get_global_transform_with_canvas() * Vector2(96, 112), MOUSE_BUTTON_RIGHT)
	if not _require(result_tree.is_visible_in_tree(), "Open the Event Graph picker by right clicking empty canvas."):
		return false
	if not await _choose_palette_item(search_field, result_tree, "Add Custom Event"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var declarations: Array = graph_data.get("events", [])
	if not _require(declarations.size() == 1 and graph_data.nodes.size() == 1 and graph_data.nodes[0].kind == &"custom_event" and
		graph_data.nodes[0].definition == declarations[0].id, "The palette action creates a declaration and its linked event entry."):
		return false
	var details := graph_window.find_child("VisualScriptDetails", true, false) as Control
	var name_field := details.find_child("VisualScriptDeclarationName", true, false) as LineEdit
	var add_input := details.find_child("VisualScriptAddInput", true, false) as Button
	if not _require(name_field != null and name_field.text == String(declarations[0].name) and add_input != null,
		"The newly added event immediately shows its Name and signature controls in Details."):
		return false
	if not await _insert(canvas, search_field, result_tree, "Print", Vector2(96, 270)):
		return false
	await _settle_canvas(canvas)
	var event_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(event_widget != null, "Find the new event for pointer movement."):
		return false
	var first_position := event_widget.position_offset
	await _drag_mouse(graph_window, event_widget.get_titlebar_hbox().get_global_rect().get_center(), Vector2(64, 48))
	var moved_position := event_widget.position_offset
	name_field = details.find_child("VisualScriptDeclarationName", true, false) as LineEdit
	if not _require(name_field != null and moved_position != first_position, "Moving a custom event keeps its declaration available in Details."):
		return false
	name_field.text = "training_started"
	name_field.grab_focus()
	var enter_event := InputEventKey.new()
	enter_event.keycode = KEY_ENTER
	enter_event.pressed = true
	graph_window.get_window().push_input(enter_event)
	await get_tree().process_frame
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.events[0].name == "training_started" and graph_data.nodes[0].position == moved_position,
		"Submitting a new event name preserves the committed card position."):
		return false
	var print_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(print_widget != null, "Find another card after the rename rebuild."):
		return false
	var print_position := print_widget.position_offset
	var print_handle := print_widget.get_titlebar_hbox().get_global_rect().get_center()
	await _click_graph(graph_window, print_handle, MOUSE_BUTTON_LEFT)
	if not _require(_selected_ids(canvas) == [2], "Another node can be selected immediately after submitting the event name."):
		return false
	await _drag_mouse(graph_window, print_handle, Vector2(48, 32))
	graph_data = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes[1].position != print_position, "The canvas accepts the next node drag after the rename."):
		return false
	for undo_index in range(4):
		await _shortcut(graph_window, canvas, KEY_Z)
	await _shortcut(graph_window, canvas, KEY_Z)
	graph_data = graph_script.call(&"get_graph")
	return _require(graph_data.get("events", []).is_empty() and graph_data.nodes.is_empty(),
		"One Undo removes both the Custom Event declaration and its graph entry.")


## Drag an enum output to insert its native Switch with the Selection wire attached.
func _verify_enum_switch(canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	const ENUM_TYPE := "Node.ProcessMode"
	graph_script.call(&"set_graph", {"version": 1, "nodes": [{
		"id": 1, "kind": &"get_property", "position": Vector2(32, 64), "member": {
			"class": &"Node", "name": &"process_mode", "inputs": [], "output": {
				"id": &"value", "label": "Value", "type": TYPE_INT, "enum_type": ENUM_TYPE,
			},
		},
	}], "connections": []})
	await get_tree().process_frame
	var enum_source := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(enum_source != null, "Find the native Process Mode getter."):
		return false
	canvas.connection_to_empty.emit(enum_source.name, 0, Vector2(240, 128))
	if not await _choose_palette_item(search_field, result_tree, "Switch on Process Mode"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph_data.nodes.size() == 2 and graph_data.nodes[1].kind == &"switch_enum" and graph_data.nodes[1].enum_type == ENUM_TYPE and
		_wire_present(graph_script, 1, &"value", 2, &"value"), "The typed Switch stores its enum and auto-connects Selection."):
		return false
	var has_named_case := false
	for case_record: Dictionary in graph_data.nodes[1].cases:
		if case_record.id == &"case_PROCESS_MODE_ALWAYS" and case_record.label == "Always" and case_record.value == Node.PROCESS_MODE_ALWAYS:
			has_named_case = true
	var switch_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	return _require(has_named_case and switch_widget != null and switch_widget.get_output_port_count() == graph_data.nodes[1].cases.size() + 1,
		"The Switch has stable named native cases and a separate Default output.")


## Connect a typed collection through a reroute, insert a compatible method from Item, then replace it.
func _verify_collection_loop_types(graph_window: Control, canvas: GraphEdit, search_field: LineEdit, result_tree: Tree, graph_script: Script) -> bool:
	var scenes_pin := {"id": &"value", "label": "Value", "type": TYPE_ARRAY, "element": {"type": TYPE_OBJECT, "class": &"PackedScene"}}
	var counts_pin := {"id": &"value", "label": "Value", "type": TYPE_DICTIONARY,
		"key": {"type": TYPE_STRING_NAME}, "value": {"type": TYPE_INT}}
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"get_variable", "variable": 1, "position": Vector2(32, 64)},
		{"id": 2, "kind": &"get_variable", "variable": 2, "position": Vector2(32, 200)},
		{"id": 3, "kind": &"foreach", "position": Vector2(270, 80)},
		{"id": 4, "kind": &"reroute", "position": Vector2(180, 100),
			"pin": {"id": &"value", "label": "Value", "type": TYPE_ARRAY}},
		{"id": 5, "kind": &"reroute", "position": Vector2(194, 125),
			"pin": {"id": &"value", "label": "Value", "type": TYPE_ARRAY}},
		{"id": 6, "kind": &"literal", "position": Vector2(32, 280),
			"pin": {"id": &"value", "label": "Value", "type": TYPE_INT}, "value": 4},
	], "connections": [_wire(1, &"value", 4, &"value"), _wire(4, &"value", 3, &"array")],
	"variables": [
		{"id": 1, "name": &"scenes", "pin": scenes_pin, "value": []},
		{"id": 2, "name": &"counts", "pin": counts_pin, "value": {}},
	]})
	await _settle_canvas(canvas)
	var loop := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(loop != null and loop.get_output_port_count() == 4, "Array loop retains Index and Item outputs."):
		return false
	canvas.connection_to_empty.emit(loop.name, 3, Vector2(340, 190))
	if not await _choose_palette_item(search_field, result_tree, "Instantiate"):
		return false
	if not _require(_wire_present(graph_script, 3, &"item", 7, &"target"),
			"Typed PackedScene survives an untyped reroute and lets Item connect to Instantiate."):
		return false
	await _settle_canvas(canvas)
	loop = canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var scalar := canvas.get_node_or_null(^"GraphNode_6") as GraphNode
	canvas.connection_request.emit(scalar.name, 0, loop.name, 1)
	await _settle_canvas(canvas)
	if not _require(not _wire_present(graph_script, 6, &"result", 3, &"array")
			and _wire_present(graph_script, 4, &"value", 3, &"array"),
			"A scalar source cannot replace a valid collection connection."):
		return false
	loop = canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var counts := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	canvas.connection_request.emit(counts.name, 0, loop.name, 1)
	await _settle_canvas(canvas)
	loop = canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(loop != null and loop.get_output_port_count() == 5
			and _wire_present(graph_script, 2, &"value", 3, &"array") and not _wire_present(graph_script, 3, &"item", 7, &"target"),
			"Replacing Array with Dictionary exposes Key and removes the now-incompatible Item wire."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	await _settle_canvas(canvas)
	loop = canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(loop != null and loop.get_output_port_count() == 4
			and _wire_present(graph_script, 4, &"value", 3, &"array") and _wire_present(graph_script, 3, &"item", 7, &"target"),
			"One Undo restores the previous shape and the Item wire."):
		return false
	var cycle: Dictionary = graph_script.call(&"get_graph")
	cycle.connections.erase(_wire(1, &"value", 4, &"value"))
	cycle.connections.append(_wire(4, &"value", 5, &"value"))
	cycle.connections.append(_wire(5, &"value", 4, &"value"))
	graph_script.call(&"set_graph", cycle)
	await _settle_canvas(canvas)
	loop = canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(loop != null and loop.get_output_port_count() == 4, "Cyclic reroutes leave unresolved loop outputs safe to draw."):
		return false
	canvas.connection_to_empty.emit(loop.name, 3, Vector2(340, 190))
	search_field.text = "Instantiate"
	search_field.text_changed.emit("Instantiate")
	await get_tree().process_frame
	var found := result_tree.get_selected()
	var has_instantiate := found != null and found.get_text(0) == "Instantiate"
	search_field.get_window().hide()
	return _require(not has_instantiate, "Cyclic reroutes do not falsely infer a PackedScene member.")


## Send the keyboard stream through the embedded popup with the canvas formerly focused.
func _verify_picker_keyboard(graph_window: Control, canvas: GraphEdit, search_field: LineEdit, graph_script: Script) -> bool:
	var initial := {"version": 1, "nodes": [{"id": 1, "kind": &"print", "position": Vector2(24, 32)}], "connections": []}
	graph_script.call(&"set_graph", initial)
	await _settle_canvas(canvas)
	if not _select(canvas, [1]):
		return false
	for location: Vector2 in [Vector2(240, 70), canvas.size - Vector2(28, 28)]:
		canvas.grab_focus()
		canvas.scroll_offset = Vector2(37, 61)
		await get_tree().process_frame
		var before_scroll := canvas.scroll_offset
		canvas.popup_request.emit(location)
		await get_tree().process_frame
		var popup := search_field.get_window()
		var result_tree := popup.find_child("GraphPaletteResults", true, false) as Tree
		if not _require(result_tree != null and result_tree.get_selected() == null,
				"Opening the picker on blank canvas does not preselect a result."):
			return false
		for letter: String in ["c", "a", "s", "t", " ", "t", "o"]:
			var key_event := InputEventKey.new()
			key_event.keycode = letter.to_upper().unicode_at(0) as Key
			key_event.unicode = letter.unicode_at(0)
			key_event.pressed = true
			graph_window.get_window().push_input(key_event, true)
			await get_tree().process_frame
		if not _require(popup.visible and search_field.text == "cast to" and popup.gui_get_focus_owner() == search_field,
				"Typing cast to keeps the complete query and focus, including beside the bottom-right edge. Actual query: " + search_field.text):
			return false
		if not _require(graph_script.call(&"get_graph") == initial and _selected_ids(canvas) == [1] and canvas.scroll_offset.is_equal_approx(before_scroll),
				"Picker keystrokes never create a comment, change graph selection, or frame the canvas."):
			return false
		if not _verify_picker_scrollbar(result_tree):
			return false
		popup.hide()
		await get_tree().process_frame
	return true


## Check that overflowing picker results expose a native vertical scrollbar on a headless editor.
func _verify_picker_scrollbar(result_tree: Tree) -> bool:
	var scrollbar: VScrollBar
	for child_node: Node in result_tree.get_children(true):
		if child_node is VScrollBar:
			scrollbar = child_node as VScrollBar
			break
	return _require(scrollbar != null and scrollbar.is_visible_in_tree() and scrollbar.page < scrollbar.max_value,
			"A long picker result list exposes a native vertical scrollbar.")


## Drop an imported WAV through the editor's native GUI drag-and-drop path.
func _verify_asset_field(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	const CHIME := "res://tests/visual_scripting/ready_chime.wav"
	graph_script.call(&"set_graph", {"version": 1, "nodes": [{
		"id": 1, "kind": &"set_property", "position": Vector2(64, 64), "member": {
			"class": &"AudioStreamPlayer2D", "name": &"stream", "inputs": [
				{"id": &"value", "label": "Value", "type": TYPE_OBJECT, "class": &"AudioStream"},
			],
		},
	}], "connections": []})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame
	var widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var filename_button := widget.find_child("GraphAssetName", true, false) as Button if widget != null else null
	if not _require(filename_button != null, "Set Stream exposes its compact resource filename field."):
		return false
	var target := filename_button.get_global_transform_with_canvas() * (filename_button.size * 0.5)
	canvas.force_drag({"type": "files", "files": PackedStringArray([CHIME])}, null)
	var motion := InputEventMouseMotion.new()
	motion.position = target
	motion.global_position = target
	graph_window.get_window().push_input(motion, true)
	graph_window.get_window().push_input(_mouse_button(target, true), true)
	graph_window.get_window().push_input(_mouse_button(target, false), true)
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var values: Dictionary = graph_data.nodes[0].get("values", {})
	widget = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	filename_button = widget.find_child("GraphAssetName", true, false) as Button if widget != null else null
	return _require(values.get(&"value", {}).get("path", "") == CHIME and filename_button != null and filename_button.text == CHIME.get_file(),
		"Dropping the WAV stores its project path and keeps its filename visible.")


## Generate assets and exercise a blank drop, an occupied drop, and each PackedScene choice.
func _verify_canvas_asset_drop(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	const AUDIO_PATH: String = "res://canvas_drop_audio.tres"
	const PACKED_PATH: String = "res://canvas_drop_pickup.tscn"
	var audio_clip := AudioStreamWAV.new()
	var scene_node := Node3D.new()
	var packed := PackedScene.new()
	var packed_result := packed.pack(scene_node)
	scene_node.free()
	if not _require(packed_result == OK and ResourceSaver.save(packed, PACKED_PATH) == OK and ResourceSaver.save(audio_clip, AUDIO_PATH) == OK,
			"Generate disposable audio and scene files for the FileSystem drag test."):
		return false
	var filesystem := EditorInterface.get_resource_filesystem()
	filesystem.update_file(PACKED_PATH)
	filesystem.update_file(AUDIO_PATH)
	graph_script.call(&"set_graph", {"version": 1, "nodes": [{"id": 1, "kind": &"print", "position": Vector2(32, 64)}], "connections": []})
	await _settle_canvas(canvas)
	var print_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(print_card != null, "Display a card that cannot accept the audio file."):
		return false
	await _drop_canvas_files(graph_window, canvas, print_card.position + print_card.size * 0.5, PackedStringArray([AUDIO_PATH]))
	var graph: Dictionary = graph_script.call(&"get_graph")
	if not _require(graph.nodes.size() == 1, "Dropping on an incompatible node does not insert a preload behind it. Authored nodes: " + str(graph.nodes)):
		return false
	await _drop_canvas_files(graph_window, canvas, Vector2(300, 175), PackedStringArray([AUDIO_PATH, PACKED_PATH]))
	graph = graph_script.call(&"get_graph")
	if not _require(graph.nodes.size() == 1, "A multi-file drop does not change the graph."):
		return false
	await _drop_canvas_files(graph_window, canvas, Vector2(300, 175), PackedStringArray([AUDIO_PATH]))
	graph = graph_script.call(&"get_graph")
	if not _require(graph.nodes.size() == 2 and graph.nodes[1].kind == &"preload_resource" and
			graph.nodes[1].asset_class == &"AudioStreamWAV" and graph.nodes[1].values.asset.path == AUDIO_PATH,
			"Dropping audio on blank canvas creates exactly one typed Preload with its saved file."):
		return false
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
	await _settle_canvas(canvas)
	for action_name: String in ["Preload Resource", "Instantiate"]:
		var before: Dictionary = graph_script.call(&"get_graph")
		await _drop_canvas_files(graph_window, canvas, canvas.size * 0.5, PackedStringArray([PACKED_PATH]))
		var overlay := graph_window.find_child("GraphDropChoiceOverlay", true, false) as Control
		if not _require(overlay != null and overlay.visible, "A PackedScene drop opens an action choice."):
			return false
		var action_kind := &"preload_resource" if action_name == "Preload Resource" else &"call_method"
		var action_button: Button
		for item: Node in overlay.find_children("*", "Button", true, false):
			if item.get_meta(&"drop_kind", &"") == action_kind:
				action_button = item as Button
				break
		if not _require(action_button != null and action_button.is_visible_in_tree(), "Find the chosen scene drop action."):
			return false
		await _click_graph(graph_window, action_button.get_global_rect().get_center(), MOUSE_BUTTON_LEFT)
		graph = graph_script.call(&"get_graph")
		if not _require(graph.nodes.size() == before.nodes.size() + 1, "A drop action creates one graph card."):
			return false
		var authored: Dictionary = graph.nodes.back()
		var action_ok: bool = authored.kind == &"preload_resource" and authored.asset_class == &"PackedScene" and authored.values.asset.path == PACKED_PATH \
			if action_name == "Preload Resource" else authored.kind == &"call_method" and authored.member.name == &"instantiate" and authored.values.target.path == PACKED_PATH
		if not _require(action_ok, "The chosen action has the correct typed PackedScene literal."):
			return false
		await _shortcut(graph_window, canvas, KEY_Z)
		if not _require(graph_script.call(&"get_graph") == before, "Undo removes the complete drop in one step."):
			return false
	return true


## Route a FileSystem payload through the normal viewport drop destination.
func _drop_canvas_files(graph_window: Control, canvas: GraphEdit, canvas_point: Vector2, paths: PackedStringArray) -> void:
	var target := canvas.get_global_transform_with_canvas() * canvas_point
	canvas.force_drag({"type": "files", "files": paths}, null)
	var motion := InputEventMouseMotion.new()
	motion.position = target
	motion.global_position = target
	# Godot queries the current pointer separately when it performs force_drag's drop.
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	graph_window.get_window().push_input(motion, true)
	graph_window.get_window().push_input(_mouse_button(target, true), true)
	graph_window.get_window().push_input(_mouse_button(target, false), true)
	await get_tree().process_frame
	await get_tree().process_frame


## Dispatch a complete mouse drag into the graph window instead of emitting editor signals.
func _drag_mouse(graph_window: Control, origin: Vector2, displacement: Vector2) -> void:
	graph_window.get_window().push_input(_mouse_button(origin, true), true)
	for increment in [displacement * 0.5, displacement]:
		var motion := InputEventMouseMotion.new()
		motion.position = origin + increment
		motion.global_position = motion.position
		motion.relative = displacement * 0.5
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT
		graph_window.get_window().push_input(motion, true)
		await get_tree().process_frame
	graph_window.get_window().push_input(_mouse_button(origin + displacement, false), true)
	await get_tree().process_frame


## Make a button event in the graph window's viewport coordinate space.
func _mouse_button(point: Vector2, pressed: bool, button: MouseButton = MOUSE_BUTTON_LEFT, alt: bool = false) -> InputEventMouseButton:
	var mouse_event := InputEventMouseButton.new()
	mouse_event.position = point
	mouse_event.global_position = point
	mouse_event.button_index = button
	mouse_event.button_mask = 1 << (button - 1) if pressed else 0
	mouse_event.alt_pressed = alt
	mouse_event.pressed = pressed
	return mouse_event


## Verify that the native node shapes route execution and data through separate ports.
func _verify_node_ports(canvas: GraphEdit, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(32, 64)},
		{"id": 2, "kind": &"set_property", "position": Vector2(288, 64), "member": {
			"class": &"Node3D", "name": &"visible", "inputs": [{"id": &"value", "label": "Visible", "type": TYPE_BOOL}],
		}},
		{"id": 3, "kind": &"branch", "position": Vector2(544, 64)},
		{"id": 4, "kind": &"get_property", "position": Vector2(288, 260), "member": {
			"class": &"Node3D", "name": &"visible", "output": {"id": &"value", "label": "Visible", "type": TYPE_BOOL},
		}},
	], "connections": []})
	await get_tree().process_frame
	var event_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var setter_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var flow_widget := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var getter_widget := canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	if not _require(event_widget != null and setter_widget != null and flow_widget != null and getter_widget != null,
			"Find the event, property, and flow widgets."):
		return false
	if not _require(event_widget.get_output_port_count() == 1
			and setter_widget.get_input_port_count() == 3 and setter_widget.get_output_port_count() == 1
			and flow_widget.get_input_port_count() == 2 and flow_widget.get_output_port_count() == 2
			and getter_widget.get_output_port_count() == 1,
			"Each node exposes the ports needed to author its operation."):
		return false
	var execution_type := event_widget.get_output_port_type(0)
	var value_type := getter_widget.get_output_port_type(0)
	return _require(execution_type != value_type and setter_widget.get_input_port_type(0) == execution_type
			and setter_widget.get_output_port_type(0) == execution_type
			and setter_widget.get_input_port_type(2) == value_type and flow_widget.get_input_port_type(0) == execution_type
			and flow_widget.get_input_port_type(1) == value_type
			and flow_widget.get_output_port_type(0) == execution_type and flow_widget.get_output_port_type(1) == execution_type,
			"Execution ports and property values expose the matching port categories.")


## Keep compact Self reads, wired receivers, and existing value links distinct.
func _verify_property_receiver(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var boolean_output := {"id": &"value", "label": "Visible", "type": TYPE_BOOL}
	var name_output := {"id": &"value", "label": "Name", "type": TYPE_STRING_NAME}
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"get_property", "position": Vector2(100, 90), "member": {"class": &"Node", "name": &"name", "output": name_output}},
		{"id": 2, "kind": &"create_node", "class": &"Node", "position": Vector2(100, 235)},
		{"id": 3, "kind": &"set_property", "position": Vector2(345, 90), "member": {"class": &"Node", "name": &"name", "inputs": [name_output]}},
		{"id": 4, "kind": &"get_property", "position": Vector2(400, 240), "member": {"class": &"Node3D", "name": &"visible", "output": boolean_output}},
		{"id": 5, "kind": &"get_property", "position": Vector2(655, 240), "member": {"class": &"Node", "name": &"name", "output": name_output}, "values": {"target": null}},
	], "connections": [{"from": 1, "from_pin": &"value", "to": 3, "to_pin": &"value"}]})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	var compact := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var required := canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	var explicit_receiver := canvas.get_node_or_null(^"GraphNode_5") as GraphNode
	if not _require(compact != null and required != null and explicit_receiver != null,
			"Find compatible, required and explicit property receiver cases."):
		return false
	var caption := compact.find_child("GraphPropertyName", true, false) as Label
	if not _require(caption != null and caption.text == "Name" and not compact.get_titlebar_hbox().visible and
			compact.get_input_port_count() == 0 and compact.get_output_port_count() == 1 and
			required.get_input_port_count() == 1 and explicit_receiver.get_input_port_count() == 1,
			"Only the compatible implicit Self receiver uses a compact caption."):
		return false
	if not await _choose_node_command(graph_window, canvas, 1, "Show Target"):
		return false
	var expanded := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var source := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(expanded.get_input_port_count() == 1 and expanded.get_titlebar_hbox().visible and
			_wire_present(graph_script, 1, &"value", 3, &"value"), "Revealing Target keeps the original output wire."):
		return false
	canvas.connection_request.emit(source.name, 1, expanded.name, 0)
	await get_tree().process_frame
	expanded = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(_wire_present(graph_script, 2, &"node", 1, &"target") and expanded.get_titlebar_hbox().visible,
			"Connecting an external receiver keeps Target on the full card."):
		return false
	await _right_click_card(graph_window, canvas, expanded)
	var menu := graph_window.find_child("GraphContextMenu", true, false) as PopupMenu
	if not _require(menu.visible and _menu_item_index(menu, "Hide Target") < 0, "A connected Target cannot be hidden."):
		return false
	menu.hide()
	source = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	canvas.disconnection_request.emit(source.name, 1, expanded.name, 0)
	await get_tree().process_frame
	if not await _choose_node_command(graph_window, canvas, 1, "Hide Target"):
		return false
	compact = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(compact.get_input_port_count() == 0 and _wire_present(graph_script, 1, &"value", 3, &"value"),
			"Hiding an unwired Target returns to the compact read and preserves Value."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	expanded = canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	return _require(expanded.get_input_port_count() == 1 and _wire_present(graph_script, 1, &"value", 3, &"value"),
		"Undo restores the optional Target UI without changing the Value wire.")


## Edit an instance declaration from the Details of a selected variable getter.
func _verify_variable_node_details(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"get_variable", "scope": &"instance", "variable": 1, "position": Vector2(96, 64)},
		{"id": 2, "kind": &"set_variable", "scope": &"instance", "variable": 1, "position": Vector2(320, 64)},
	], "connections": [{"from": 1, "from_pin": &"value", "to": 2, "to_pin": &"value"}], "variables": [{
		"id": 1, "name": "threshold", "pin": {"id": &"value", "label": "Value", "type": TYPE_INT}, "value": 7,
	}]})
	await get_tree().process_frame
	var getter_widget := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var setter_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var details := graph_window.find_child("VisualScriptDetails", true, false) as Control
	if not _require(getter_widget != null and setter_widget != null and details != null, "Find the variable cards and their Details panel."):
		return false
	if not _require(getter_widget.get_input_port_count() == 0 and getter_widget.get_output_port_count() == 1
			and setter_widget.get_input_port_count() == 2
			and getter_widget.get_output_port_type(0) == setter_widget.get_input_port_type(1),
			"The getter exposes its value to the matching setter input."):
		return false
	var original_zoom := canvas.zoom
	canvas.zoom = canvas.zoom_min
	await get_tree().process_frame
	var saved_connections: Array = (graph_script.call(&"get_graph") as Dictionary).connections
	var zoom_is_correct := is_equal_approx(getter_widget.scale.x, canvas.zoom)
	canvas.zoom = original_zoom
	if not _require(zoom_is_correct and canvas.get_connection_list().size() == 1
			and saved_connections.size() == 1 and saved_connections[0].from_pin == &"value"
			and saved_connections[0].to_pin == &"value",
			"Changing graph zoom preserves the visible connection and its saved pin identities."):
		return false
	getter_widget.selected = true
	canvas.node_selected.emit(getter_widget)
	await get_tree().process_frame
	var name_field := details.find_child("VisualScriptDeclarationName", true, false) as LineEdit
	var type_button := details.find_child("VisualScriptDeclarationType", true, false) as Button
	var default_field := details.find_child("VisualScriptVariableDefault", true, false) as LineEdit
	if not _require(name_field != null and name_field.text == "threshold" and type_button != null and type_button.text == "int" and
		default_field != null and default_field.text == "7", "The variable getter shows the declaration's current Name, Type, and Default."):
		return false
	default_field.text = "13"
	default_field.text_submitted.emit(default_field.text)
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	return _require(graph_data.variables[0].value == 13 and graph_data.nodes[0].variable == graph_data.variables[0].id,
		"Editing the getter's Default updates the shared declaration and keeps its reference intact.")


## Drop a real variable row twice, adding Ctrl only after the second drag has started.
func _verify_variable_drop(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": [], "variables": [{
		"id": 1, "name": "drop_speed", "pin": {"id": &"value", "label": "Value", "type": TYPE_INT}, "value": 3,
	}]})
	await _settle_canvas(canvas)
	var target := canvas.get_global_rect().get_center()
	for wants_setter: bool in [false, true]:
		if not await _drop_variable_row(graph_window, target, wants_setter, "drop_speed"):
			return false
		var changed: Dictionary = graph_script.call(&"get_graph")
		var expected_kind := "set_variable" if wants_setter else "get_variable"
		var palette_window := graph_window.find_child("GraphPaletteSearch", true, false).get_window()
		if not _require(changed.nodes.size() == (2 if wants_setter else 1) and
			changed.nodes.back().kind == expected_kind and changed.nodes.back().variable == 1 and not palette_window.visible,
			"Dropping a variable on blank canvas creates %s with no picker. Nodes: %s." % [expected_kind, changed.nodes]):
			return false
		target += Vector2(20, 72)
	return true


## Drop the same variable onto native pins, exercise replacement and reject incompatible sockets.
func _verify_variable_pin_drop(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var text_value := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"literal", "pin": text_value, "values": {&"value": "Previous source"},
			"position": Vector2(12, 12)},
		{"id": 2, "kind": &"print", "position": Vector2(172, 100)},
		{"id": 3, "kind": &"branch", "position": Vector2(100, 230)},
	], "connections": [_wire(1, &"result", 2, &"message")], "variables": [
		{"id": 1, "name": "drop_text", "pin": text_value, "value": "Current value"},
	]})
	await _settle_canvas(canvas)
	var initial: Dictionary = graph_script.call(&"get_graph")
	var print_widget := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(print_widget != null and _wire_present(graph_script, 1, &"result", 2, &"message"),
			"Find the occupied String input for the getter drop."):
		return false
	var target := _socket_position(canvas, print_widget, 1, false)
	if not _require(canvas.get_global_rect().has_point(target), "The occupied input is inside the displayed canvas."):
		return false
	if not await _drop_variable_row(graph_window, target, false, "drop_text"):
		return false
	var changed: Dictionary = graph_script.call(&"get_graph")
	if not _require(changed.nodes.size() == 4 and changed.nodes.back().kind == &"get_variable" and
			changed.nodes.back().variable == 1 and changed.connections.size() == 1 and
			_wire_present(graph_script, 4, &"value", 2, &"message"),
			"Dropping a String variable on a String input creates and connects its getter, replacing the previous edge. Nodes: %s. Wires: %s."
			% [changed.nodes.size(), changed.connections]):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _require(graph_script.call(&"get_graph") == initial, "One Undo removes the getter and restores the previous edge."):
		return false

	var producer := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	if not _require(producer != null, "Find the String output for the Ctrl drop."):
		return false
	target = _socket_position(canvas, producer, 0, true)
	if not _require(canvas.get_global_rect().has_point(target), "The source output is inside the displayed canvas."):
		return false
	if not await _drop_variable_row(graph_window, target, true, "drop_text"):
		return false
	changed = graph_script.call(&"get_graph")
	if not _require(changed.nodes.size() == 4 and changed.nodes.back().kind == &"set_variable" and
			changed.nodes.back().variable == 1 and changed.connections.size() == 2 and
			_wire_present(graph_script, 1, &"result", 4, &"value"),
			"Ctrl-dropping a String variable onto a String output creates a connected setter and keeps the preexisting edge. Nodes: %s. Wires: %s."
			% [changed.nodes.size(), changed.connections]):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _require(graph_script.call(&"get_graph") == initial, "One Undo removes the setter and its wire."):
		return false

	var branch_widget := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(branch_widget != null, "Find the incompatible condition and execution inputs."):
		return false
	for port_index: int in [0, 1]:
		target = _socket_position(canvas, branch_widget, port_index, false)
		if not _require(canvas.get_global_rect().has_point(target), "The incompatible input is inside the displayed canvas."):
			return false
		if not await _drop_variable_row(graph_window, target, false, "drop_text"):
			return false
		if not _require(graph_script.call(&"get_graph") == initial,
				"An incompatible drop on input %d creates neither a node nor a wire." % port_index):
			return false

	print_widget = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(print_widget != null, "Find the String input after undoing the drops."):
		return false
	var message_label: Label
	for pin_label: Node in print_widget.find_children("*", "Label", true, false):
		if (pin_label as Label).text == "Message":
			message_label = pin_label as Label
			break
	if not _require(message_label != null, "Find the String input's visible pin label."):
		return false
	target = message_label.get_global_rect().get_center()
	if not _require(canvas.get_global_rect().has_point(target), "The typed label is inside the displayed canvas."):
		return false
	if not await _drop_variable_row(graph_window, target, false, "drop_text"):
		return false
	if not _require(_wire_present(graph_script, 4, &"value", 2, &"message"),
			"The visible pin label targets the same input as its socket."):
		return false
	changed = graph_script.call(&"get_graph")
	if not _require(changed.nodes[1].position == initial.nodes[1].position,
			"Dropping a variable on a pin label leaves the existing card in place."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(graph_script.call(&"get_graph") == initial, "One Undo also restores the graph after a pin label drop.")


## Drop an enum variable on a compatible switch and preserve its enum identity and default.
func _verify_enum_variable_drop(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var door_type := &"GraphEnum.Enum_1"
	var other_type := &"GraphEnum.Enum_2"
	var door_pin := {"id": &"value", "label": "Value", "type": TYPE_INT, "enum_type": door_type}
	var switch_pin := {"id": &"value", "label": "Selection", "type": TYPE_INT, "enum_type": door_type, "default": -7}
	var other_pin := switch_pin.duplicate()
	other_pin.enum_type = other_type
	var switch_cases := [
		{"id": &"case_member_1", "label": "Locked", "value": -7},
		{"id": &"case_member_2", "label": "Open", "value": 0},
	]
	var enum_members := [
		{"id": 1, "name": "Locked", "value": -7},
		{"id": 2, "name": "Open", "value": 0},
	]
	graph_script.call(&"set_graph", {"version": 1, "nodes": [
		{"id": 1, "kind": &"switch_enum", "enum_type": door_type, "pin": switch_pin,
			"cases": switch_cases, "position": Vector2(164, 80)},
		{"id": 2, "kind": &"switch_enum", "enum_type": other_type, "pin": other_pin,
			"cases": switch_cases, "position": Vector2(164, 225)},
	], "connections": [], "variables": [
		{"id": 1, "name": "door_state", "pin": door_pin, "value": -7},
	], "enums": [
		{"id": 1, "name": "DoorState", "entries": enum_members, "next_entry_id": 3},
		{"id": 2, "name": "OtherState", "entries": enum_members, "next_entry_id": 3},
	]})
	await _settle_canvas(canvas)
	var original: Dictionary = graph_script.call(&"get_graph")
	for switch_id: int in [2, 1]:
		var switch_widget := canvas.get_node_or_null(NodePath("GraphNode_%d" % switch_id)) as GraphNode
		if not _require(switch_widget != null, "Find the enum switch and its Selection socket."):
			return false
		var target := _socket_position(canvas, switch_widget, 1, false)
		if not _require(canvas.get_global_rect().has_point(target), "The enum switch socket is inside the displayed canvas."):
			return false
		if not await _drop_variable_row(graph_window, target, false, "door_state"):
			return false
		var result: Dictionary = graph_script.call(&"get_graph")
		if switch_id == 2:
			if not _require(result == original, "Different enum identities reject a drop even with matching underlying integers."):
				return false
		elif not _require(result.nodes.size() == 3 and result.nodes.back().kind == &"get_variable"
				and result.nodes.back().variable == 1 and result.variables[0].pin.enum_type == door_type
				and result.variables[0].value == -7 and _wire_present(graph_script, 3, &"value", 1, &"value"),
				"Dropping an authored enum variable onto a matching switch creates a connected getter with the same default and enum identity."):
			return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(graph_script.call(&"get_graph") == original, "One Undo restores the graph before the enum variable drop.")


## Reorder declarations through their native tree, keeping graph references attached to stable IDs.
func _verify_declaration_reorder(graph_window: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var int_value := {"id": &"value", "label": "Value", "type": TYPE_INT}
	graph_script.call(&"set_graph", {"version": 1,
		"nodes": [{"id": 1, "kind": &"get_variable", "variable": 1}],
		"connections": [],
		"variables": [
			{"id": 1, "name": "alpha_value", "pin": int_value, "value": 10},
			{"id": 2, "name": "beta_value", "pin": int_value, "value": 20},
			{"id": 3, "name": "gamma_value", "pin": int_value, "value": 30},
		],
		"functions": [
			{"id": 4, "name": "first_call", "inputs": [], "outputs": [], "graph": {"nodes": [], "connections": []}},
			{"id": 5, "name": "other_call", "inputs": [], "outputs": [], "graph": {"nodes": [], "connections": []}},
		]})
	await _settle_canvas(canvas)
	var initial: Dictionary = graph_script.call(&"get_graph")
	var outline := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var variables := _find_entry(outline, "Variables")
	var functions := _find_entry(outline, "Functions")
	if not _require(variables != null and functions != null,
			"Find variable and function groups in the declaration tree."):
		return false
	variables.collapsed = false
	functions.collapsed = false
	await get_tree().process_frame
	var first_row := variables.get_first_child()
	var next_row := first_row.get_next()
	var next_bounds := _declaration_content_rect(outline, next_row)
	var after_beta := outline.get_global_transform_with_canvas() * Vector2(next_bounds.get_center().x, next_bounds.end.y - 2)
	if not await _drag_declaration_row(graph_window, outline, first_row, after_beta, false, "alpha_value"):
		return false
	var changed: Dictionary = graph_script.call(&"get_graph")
	if not _require(changed.variables[0].id == 2 and changed.variables[1].id == 1 and changed.variables[2].id == 3
			and changed.variables[1].value == 10 and changed.nodes == initial.nodes,
			"Dropping into the lower half of a declaration row reorders a variable without changing its value or graph reference."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	if not _require(graph_script.call(&"get_graph") == initial, "One Undo restores the declaration order."):
		return false

	variables = _find_entry(outline, "Variables")
	functions = _find_entry(outline, "Functions")
	var first_function := functions.get_first_child()
	var function_bounds := _declaration_content_rect(outline, first_function)
	var before_function := outline.get_global_transform_with_canvas() * Vector2(function_bounds.get_center().x, function_bounds.position.y + 2)
	if not await _drag_declaration_row(graph_window, outline, variables.get_first_child(), before_function, false, "alpha_value"):
		return false
	if not _require(graph_script.call(&"get_graph") == initial, "Dragging across sections leaves declarations unchanged."):
		return false
	variables.collapsed = true
	await get_tree().process_frame
	function_bounds = _declaration_content_rect(outline, first_function)
	before_function = outline.get_global_transform_with_canvas() * Vector2(function_bounds.get_center().x, function_bounds.position.y + 2)
	if not await _drag_declaration_row(graph_window, outline, first_function.get_next(), before_function, false, "other_call"):
		return false
	changed = graph_script.call(&"get_graph")
	if not _require(changed.functions[0].id == 5 and changed.functions[1].id == 4 and changed.variables == initial.variables,
			"Reordering a function changes only its section's order."):
		return false
	await _shortcut(graph_window, canvas, KEY_Z)
	return _require(graph_script.call(&"get_graph") == initial, "One Undo restores the function order.")


## Drive a real My Script drag and apply Ctrl only after Godot starts it.
func _drop_variable_row(graph_window: Control, target: Vector2, wants_setter: bool, variable_name: String) -> bool:
	var outline := graph_window.find_child("VisualScriptDeclarations", true, false) as Tree
	var category := _find_entry(outline, "Variables")
	if not _require(category != null and category.get_first_child() != null, "Find the variable in the My Script outliner."):
		return false
	category.collapsed = false
	await get_tree().process_frame
	return await _drag_declaration_row(graph_window, outline, category.get_first_child(), target, wants_setter, variable_name)


## Drive the authored declaration's real stock drag to a target in the graph window.
func _drag_declaration_row(graph_window: Control, outline: Tree, source_row: TreeItem, target: Vector2, wants_setter: bool, declaration_name: String) -> bool:
	var row_bounds := _declaration_content_rect(outline, source_row)
	var origin := outline.get_global_transform_with_canvas() * row_bounds.get_center()
	graph_window.get_window().push_input(_mouse_button(origin, true), true)
	var outbound := InputEventMouseMotion.new()
	outbound.position = origin + Vector2(24, 0)
	outbound.global_position = outbound.position
	outbound.relative = Vector2(24, 0)
	outbound.button_mask = MOUSE_BUTTON_MASK_LEFT
	graph_window.get_window().push_input(outbound, true)
	await get_tree().process_frame
	if not _require(graph_window.get_window().gui_is_dragging(), "Dragging the declaration row starts Godot's native drag."):
		return false
	var badge := graph_window.get_window().find_child("GraphDragPreview", true, false) as Control
	var badge_label := badge.find_child("GraphDragPreviewName", true, false) as Label if badge != null else null
	if not _require(badge_label != null and badge_label.text.contains(declaration_name),
			"The native declaration drag shows a badge with its name."):
		return false
	var ctrl_key := InputEventKey.new()
	ctrl_key.keycode = KEY_CTRL
	ctrl_key.physical_keycode = KEY_CTRL
	ctrl_key.pressed = true
	if wants_setter:
		Input.parse_input_event(ctrl_key)
		await get_tree().process_frame
	var over_canvas := InputEventMouseMotion.new()
	over_canvas.position = target
	over_canvas.global_position = target
	over_canvas.relative = target - outbound.position
	over_canvas.button_mask = MOUSE_BUTTON_MASK_LEFT
	over_canvas.ctrl_pressed = wants_setter
	if DisplayServer.get_name() != "headless":
		graph_window.get_window().warp_mouse(target)
	graph_window.get_window().push_input(over_canvas, true)
	if DisplayServer.get_name() == "headless":
		# Stock headless Viewport drops resolve local position from the tracked global mouse.
		var global_pointer := over_canvas.duplicate() as InputEventMouseMotion
		global_pointer.position = graph_window.get_window().get_screen_transform() * target
		global_pointer.global_position = global_pointer.position
		Input.parse_input_event(global_pointer)
		Input.flush_buffered_events()
	var drop := _mouse_button(target, false)
	drop.ctrl_pressed = wants_setter
	graph_window.get_window().push_input(drop, true)
	if wants_setter:
		ctrl_key.pressed = false
		Input.parse_input_event(ctrl_key)
	await get_tree().process_frame
	await get_tree().process_frame
	return _require(not graph_window.get_window().gui_is_dragging() and graph_window.get_window().find_child("GraphDragPreview", true, false) == null,
		"The declaration drop ends the native drag and removes its preview badge.")


## Find the declaration's content cell and avoid separate action buttons.
func _declaration_content_rect(outline: Tree, declaration: TreeItem) -> Rect2:
	if outline.columns > 1 and not declaration.get_tooltip_text(1).is_empty():
		return outline.get_item_area_rect(declaration, 1)
	for column: int in range(outline.columns):
		if not declaration.get_text(column).is_empty():
			return outline.get_item_area_rect(declaration, column)
	return outline.get_item_area_rect(declaration)


## Activate a section's add button regardless of which column carries the action.
func _add_declaration(outline: Tree, section: TreeItem, caption: String) -> bool:
	if section != null:
		for column: int in range(outline.columns):
			if section.get_button_by_id(column, 1) >= 0:
				outline.button_clicked.emit(section, column, 1, MOUSE_BUTTON_LEFT)
				return true
	return _require(false, "The %s section offers an add action." % caption)


## Find a top-level declaration or workspace entry by its visible label.
func _find_entry(declaration_tree: Tree, label: String) -> TreeItem:
	var top := declaration_tree.get_root()
	if top == null:
		return null
	var entry := top.get_first_child()
	while entry != null:
		for column: int in range(declaration_tree.columns):
			if entry.get_text(column) == label:
				return entry
		entry = entry.get_next()
	return null


## Model native selection without relying on desktop mouse coordinates.
func _select(canvas: GraphEdit, node_ids: Array[int]) -> bool:
	for graph_child: Node in canvas.get_children():
		if graph_child is GraphNode:
			graph_child.selected = false
	for node_id: int in node_ids:
		var widget := canvas.get_node_or_null(NodePath("GraphNode_%s" % node_id)) as GraphNode
		if not _require(widget != null, "Find a native widget for selected node %s." % node_id):
			return false
		widget.selected = true
	return true


## Send the same shortcut event the graph window consumes during authoring.
func _shortcut(graph_window: Control, canvas: GraphEdit, shortcut_code: Key) -> void:
	await _press_graph_key(graph_window, canvas, shortcut_code, true)


## Send a graph shortcut with its actual modifier state and leave focus on the canvas.
func _press_graph_key(graph_window: Control, canvas: GraphEdit, shortcut_code: Key, control: bool = false, alt: bool = false, shift: bool = false) -> void:
	canvas.grab_focus()
	var key_event := InputEventKey.new()
	key_event.keycode = shortcut_code
	key_event.ctrl_pressed = control
	key_event.alt_pressed = alt
	key_event.shift_pressed = shift
	key_event.pressed = true
	graph_window.get_window().push_input(key_event, true)
	await get_tree().process_frame


## Deliver native GraphEdit clipboard shortcuts through the viewport input pipeline.
func _push_graph_key(graph_window: Control, canvas: GraphEdit, shortcut_code: Key) -> void:
	canvas.grab_focus()
	var key_event := InputEventKey.new()
	key_event.keycode = shortcut_code
	key_event.physical_keycode = shortcut_code
	key_event.ctrl_pressed = true
	key_event.pressed = true
	graph_window.get_window().push_input(key_event)
	await get_tree().process_frame


## Read the selection by stored node ID rather than canvas child order.
func _selected_ids(canvas: GraphEdit) -> Array[int]:
	var ids: Array[int] = []
	for graph_child: Node in canvas.get_children():
		if graph_child is GraphNode and graph_child.selected:
			ids.append(int(String(graph_child.name).get_slice("_", 1)))
	ids.sort()
	return ids


## Identify a persisted edge by its endpoints and stable pin IDs.
func _wire_present(graph_script: Script, source: int, output_pin: StringName, destination: int, input_pin: StringName) -> bool:
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	for connection: Dictionary in graph_data.get("connections", []):
		if connection.from == source and connection.from_pin == output_pin and connection.to == destination and connection.to_pin == input_pin:
			return true
	return false


## Encode a connection that exercises the same stored IDs the editor writes.
func _wire(source: int, output_pin: StringName, destination: int, input_pin: StringName) -> Dictionary:
	return {"from": source, "from_pin": output_pin, "to": destination, "to_pin": input_pin}


## Mix a four-node execution chain with an external data producer.
func _operation_graph() -> Dictionary:
	var string_pin := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	return {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(24, 48)},
		{"id": 2, "kind": &"print", "position": Vector2(184, 80)},
		{"id": 3, "kind": &"print", "position": Vector2(352, 112)},
		{"id": 4, "kind": &"print", "position": Vector2(512, 144)},
		{"id": 5, "kind": &"literal", "pin": string_pin, "position": Vector2(48, 224), "values": {&"value": "Shared"}},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in"),
		_wire(3, &"then", 4, &"in"), _wire(5, &"result", 2, &"message")]}


## Provide a single execution path and two alternative typed data sources.
func _rewire_graph() -> Dictionary:
	var string_pin := {"id": &"value", "label": "Value", "type": TYPE_STRING}
	return {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(64, 64)},
		{"id": 2, "kind": &"print", "position": Vector2(384, 64)},
		{"id": 3, "kind": &"literal", "pin": string_pin, "position": Vector2(64, 256), "values": {&"value": "first"}},
		{"id": 4, "kind": &"literal", "pin": string_pin, "position": Vector2(64, 480), "values": {&"value": "second"}},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(3, &"result", 2, &"message")]}


## Give collapse a branch whose True and False continuations leave the selection.
func _macro_graph() -> Dictionary:
	return {"version": 1, "nodes": [
		{"id": 1, "kind": &"ready", "position": Vector2(64, 64)},
		{"id": 2, "kind": &"branch", "position": Vector2(384, 64)},
		{"id": 3, "kind": &"print", "position": Vector2(704, 64)},
		{"id": 4, "kind": &"print", "position": Vector2(704, 256)},
	], "connections": [_wire(1, &"then", 2, &"in"), _wire(2, &"true", 3, &"in"), _wire(2, &"false", 4, &"in")]}
