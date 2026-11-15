# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const SCRIPT_PATH := "res://exported_variables_editor.crimsonscript"
const SCENE_PATH := "res://exported_variables_editor.tscn"
const HEAL_PROPERTY := &"heal_amount"
const ORIGINAL_DEFAULT := 25
const OVERRIDE_HEAL := 50
const NEW_DEFAULT := 30
const EXPORT_BUTTON_ID := 2


## Let the disposable Godot editor finish starting before opening a graph and scene.
func _enter_tree() -> void:
	_verify.call_deferred()


## Include the failing editor action in the harness log.
func _need(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("Visual Script Inspector export: " + requirement)
		get_tree().quit(1)
	return condition


## Find a declaration by name in its tree grouping.
func _find_declaration(item: TreeItem, declaration_name: String) -> TreeItem:
	if item.get_text(0) == declaration_name:
		return item
	var child_item := item.get_first_child()
	while child_item != null:
		var match_item := _find_declaration(child_item, declaration_name)
		if match_item != null:
			return match_item
		child_item = child_item.get_next()
	return null


## Wait for the stock Inspector to create the real EditorProperty for this selection.
func _inspect_heal(pickup: Node) -> EditorProperty:
	EditorInterface.edit_node(pickup)
	var inspector := EditorInterface.get_inspector()
	for attempt: int in range(20):
		await get_tree().process_frame
		for child_node: Node in inspector.find_children("*", "EditorProperty", true, false):
			var property_editor := child_node as EditorProperty
			if property_editor.get_edited_property() == HEAL_PROPERTY and property_editor.get_edited_object() == pickup:
				return property_editor
	return null


## Generate two pickups sharing one graph without relying on a demo scene.
func _save_pickup_scene(graph_script: Script) -> bool:
	var scene := Node.new()
	scene.name = &"PickupFixture"
	for pickup_name: StringName in [&"SmallPickup", &"LargePickup"]:
		var pickup := Node.new()
		pickup.name = pickup_name
		scene.add_child(pickup)
		pickup.owner = scene
		pickup.set_script(graph_script)
	var packed := PackedScene.new()
	var result := packed.pack(scene)
	scene.free()
	return result == OK and ResourceSaver.save(packed, SCENE_PATH) == OK


## A restored editor layout may still be changing scene tabs when the test plugin starts.
func _open_pickup_scene() -> Node:
	var stable_scene: Node
	for attempt: int in range(30):
		var edited_scene := EditorInterface.get_edited_scene_root()
		if edited_scene != null and edited_scene.scene_file_path == SCENE_PATH:
			if stable_scene == edited_scene and edited_scene.is_inside_tree():
				return edited_scene
			stable_scene = edited_scene
		else:
			stable_scene = null
			EditorInterface.open_scene_from_path(SCENE_PATH)
		await get_tree().process_frame
	return null


## Verify the declaration eye shares the checkbox state and supports cancelling a destructive edit.
func _verify_declaration_eye(workspace: Control, declarations: Tree, graph_script: Script) -> bool:
	for accept_change: bool in [false, true]:
		var entry := _find_declaration(declarations.get_root(), String(HEAL_PROPERTY))
		if not _need(entry != null and entry.get_button_by_id(0, EXPORT_BUTTON_ID) >= 0,
			"The instance variable row has a stock Tree button for export."):
			return false
		declarations.button_clicked.emit(entry, 0, EXPORT_BUTTON_ID, MOUSE_BUTTON_LEFT)
		await get_tree().process_frame
		var dialog := workspace.find_child("VisualScriptChangeVariableType", true, false) as Control
		var button_name := "ConfirmDialog" if accept_change else "CancelDialog"
		var response := dialog.find_child(button_name, true, false) as BaseButton if dialog != null else null
		if not _need(response != null and dialog.visible, "The export eye confirms removal of scene overrides."):
			return false
		response.pressed.emit()
		await get_tree().process_frame
		var graph_data: Dictionary = graph_script.call(&"get_graph")
		if not _need(graph_data.variables[0].get("exported", false) == not accept_change,
			"Only accepting the confirmation disables the export."):
			return false
	var private_entry := _find_declaration(declarations.get_root(), String(HEAL_PROPERTY))
	if not _need(private_entry != null and private_entry.get_button_by_id(0, EXPORT_BUTTON_ID) >= 0,
		"The unexported variable retains an eye action to show it again."):
		return false
	declarations.button_clicked.emit(private_entry, 0, EXPORT_BUTTON_ID, MOUSE_BUTTON_LEFT)
	await get_tree().process_frame
	var restored_data: Dictionary = graph_script.call(&"get_graph")
	var restored_checkbox := workspace.find_child("VisualScriptVariableExported", true, false) as BaseButton
	return _need(restored_data.variables[0].get("exported", false) == true and
		restored_checkbox != null and restored_checkbox.button_pressed,
		"The variable eye shows the property again and updates Details.")


## Check that a native class collision preserves the derived node and disappears when that owner detaches.
func _verify_derived_collision() -> bool:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(graph_script != null, "Create a shared editor graph that declares position."):
		return false
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", {
		"version": 1, "nodes": [], "connections": [], "variables": [
			{"id": 1, "name": &"position", "pin": {"id": &"value", "type": TYPE_INT}, "value": 7, "exported": true},
			{"id": 2, "name": &"ammo", "pin": {"id": &"value", "type": TYPE_INT}, "value": 20, "exported": true},
		],
	})
	var plain_owner := Node.new()
	plain_owner.set_script(graph_script)
	var initially_exported: bool = plain_owner.get(&"position") == 7
	var spatial_owner := Node3D.new()
	spatial_owner.position = Vector3(1, 2, 3)
	spatial_owner.set_script(graph_script)
	var native_fields_intact: bool = (
		plain_owner.get(&"position") == null and
		graph_script.get_property_default_value(&"position") == null and
		spatial_owner.get(&"position") == Vector3(1, 2, 3) and
		plain_owner.get(&"ammo") == 20 and spatial_owner.get(&"ammo") == 20
	)
	spatial_owner.set(&"position", Vector3(4, 5, 6))
	var native_field_editable: bool = spatial_owner.position == Vector3(4, 5, 6)
	spatial_owner.set_script(null)
	var export_restored: bool = plain_owner.get(&"position") == 7
	spatial_owner.free()
	plain_owner.free()
	return _need(initially_exported and native_fields_intact and native_field_editable and export_restored,
		"A derived owner hides the conflicting graph export while preserving the native position and other exports.")


## Author an export on an attached graph and change the override through Godot's own Inspector.
func _verify() -> void:
	await get_tree().process_frame
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await get_tree().process_frame
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(graph_script != null, "Create a native Visual Script resource."):
		return
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", {
		"version": 1, "nodes": [], "connections": [],
		"variables": [{
			"id": 1, "name": HEAL_PROPERTY, "value": ORIGINAL_DEFAULT,
			"pin": {"id": &"value", "label": "Value", "type": TYPE_INT},
		}],
	})
	if not _need(ResourceSaver.save(graph_script, SCRIPT_PATH) == OK, "Save the new graph."):
		return
	graph_script = ResourceLoader.load(SCRIPT_PATH, "Script") as Script
	if not _need(graph_script != null and _save_pickup_scene(graph_script), "Save a scene containing two graph owners."):
		return
	var edited_scene := await _open_pickup_scene()
	if not _need(edited_scene != null, "Open the generated pickup scene."):
		return
	var small_pickup := edited_scene.get_node("SmallPickup")
	var large_pickup := edited_scene.get_node("LargePickup")
	graph_script = small_pickup.get_script() as Script
	if not _need(graph_script != null and graph_script == large_pickup.get_script() and
		small_pickup.get(&"heal_amount") == null, "The shared graph starts with a private declaration."):
		return
	EditorInterface.edit_script(graph_script, -1, 0, true)
	await get_tree().process_frame
	var workspace := get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and workspace.is_visible_in_tree(), "Open the graph editor."):
		return
	var declarations := workspace.find_child("VisualScriptDeclarations", true, false) as Tree
	var entry := _find_declaration(declarations.get_root(), String(HEAL_PROPERTY)) if declarations != null else null
	if not _need(entry != null, "Find the authored heal_amount declaration."):
		return
	entry.select(0)
	declarations.item_selected.emit()
	await get_tree().process_frame
	var exported := workspace.find_child("VisualScriptVariableExported", true, false) as BaseButton
	if not _need(exported != null and not exported.button_pressed, "Expose an opt-in Exported toggle in Details."):
		return
	exported.button_pressed = true
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _need(graph_data.variables[0].get("exported", false) == true and
		small_pickup.get(HEAL_PROPERTY) == ORIGINAL_DEFAULT and large_pickup.get(HEAL_PROPERTY) == ORIGINAL_DEFAULT,
		"Exporting a graph variable refreshes both attached editor placeholders."):
		return
	if not await _verify_inspector_authoring(workspace, graph_script):
		return
	if not await _verify_declaration_eye(workspace, declarations, graph_script) or not _need(
		ResourceSaver.save(graph_script, SCRIPT_PATH) == OK, "Save the graph export declaration."):
		return
	EditorInterface.set_main_screen_editor("3D")
	edited_scene = await _open_pickup_scene()
	if not _need(edited_scene != null, "Select the disposable fixture scene after leaving the graph workspace."):
		return
	small_pickup = edited_scene.get_node("SmallPickup")
	large_pickup = edited_scene.get_node("LargePickup")
	graph_script = small_pickup.get_script() as Script
	graph_data = graph_script.call(&"get_graph")
	var large_editor := await _inspect_heal(large_pickup)
	if not _need(large_editor != null, "Godot creates an editable property row for the exported variable."):
		return
	var revert_states: Array[bool] = []
	large_editor.property_can_revert_changed.connect(func(edited_property: StringName, available: bool) -> void:
		if edited_property == HEAL_PROPERTY:
			revert_states.append(available)
	)
	large_editor.emit_changed(HEAL_PROPERTY, OVERRIDE_HEAL)
	await get_tree().process_frame
	if not _need(large_pickup.get(HEAL_PROPERTY) == OVERRIDE_HEAL and small_pickup.get(HEAL_PROPERTY) == ORIGINAL_DEFAULT,
		"Changing one native Inspector row leaves the other node untouched."):
		return
	if not _need(revert_states.has(true), "The stock Inspector enables the Revert affordance after assigning an override."):
		return
	if not _need(EditorInterface.save_scene() == OK and
		FileAccess.get_file_as_string(SCENE_PATH).count("heal_amount = 50") == 1,
		"Save exactly one Inspector override with the scene."):
		return
	graph_data.variables[0]["value"] = NEW_DEFAULT
	graph_script.call(&"set_graph", graph_data)
	if not _need(ResourceSaver.save(graph_script, SCRIPT_PATH) == OK and
		small_pickup.get(HEAL_PROPERTY) == NEW_DEFAULT and large_pickup.get(HEAL_PROPERTY) == OVERRIDE_HEAL,
		"Changing the declaration default immediately updates the unmodified editor instance."):
		return
	large_editor = await _inspect_heal(large_pickup)
	if not _need(large_editor != null, "The editable row survives declaration updates."):
		return
	large_editor.emit_changed(HEAL_PROPERTY, graph_script.get_property_default_value(HEAL_PROPERTY))
	await get_tree().process_frame
	if not _need(large_pickup.get(HEAL_PROPERTY) == NEW_DEFAULT and EditorInterface.save_scene() == OK and
		not FileAccess.get_file_as_string(SCENE_PATH).contains("heal_amount ="),
		"Restoring the Inspector value to its new default removes the stored override."):
		return
	if not _verify_derived_collision():
		return
	print("CRIMSON_VISUAL_SCRIPT_EXPORTED_VARIABLES_EDITOR_PASSED")
	get_tree().quit()


## Use the live Details controls to author group, units, and integer range options.
func _verify_inspector_authoring(workspace: Control, graph_script: Script) -> bool:
	var variable_group := workspace.find_child("VisualScriptInstanceVariableDisclosure", true, false) as Button
	var inspector_group := workspace.find_child("VisualScriptInspectorDisclosure", true, false) as Button
	var group := workspace.find_child("VisualScriptInspectorGroup", true, false) as LineEdit
	var units := workspace.find_child("VisualScriptInspectorUnits", true, false) as LineEdit
	var range_toggle := workspace.find_child("VisualScriptInspectorUseRange", true, false) as BaseButton
	var name_field := workspace.find_child("VisualScriptDeclarationName", true, false) as LineEdit
	if not _need(variable_group != null and variable_group.button_pressed and name_field != null and name_field.is_visible_in_tree() and
			inspector_group != null and not inspector_group.button_pressed and group != null and not group.is_visible_in_tree(),
			"Instance Variable begins expanded and the separate Inspector settings begin collapsed."):
		return false
	variable_group.button_pressed = false
	await get_tree().process_frame
	if not _need(not name_field.is_visible_in_tree() and inspector_group.is_visible_in_tree(),
			"Folding Instance Variable keeps the Inspector disclosure available."):
		return false
	variable_group.button_pressed = true
	inspector_group.grab_focus()
	var confirm := InputEventKey.new()
	confirm.keycode = KEY_SPACE
	confirm.pressed = true
	workspace.get_window().push_input(confirm)
	confirm = confirm.duplicate() as InputEventKey
	confirm.pressed = false
	workspace.get_window().push_input(confirm)
	await get_tree().process_frame
	if not _need(inspector_group.button_pressed and inspector_group.has_focus() and group.is_visible_in_tree(),
			"Keyboard Confirm opens Inspector settings and keeps focus on the disclosure."):
		return false
	if not _need(group != null and units != null and range_toggle != null and not range_toggle.button_pressed,
			"A newly exported numeric variable can set Group and Units without enabling a Range."):
		return false
	group.text = "Pickup Tuning"
	group.text_submitted.emit(group.text)
	await get_tree().process_frame
	units.text = "hp"
	units.text_submitted.emit(units.text)
	await get_tree().process_frame
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	if not _need(graph_data.variables[0].inspector == {"group": "Pickup Tuning", "suffix": "hp"},
			"Group and Units are saved independently of the numeric range."):
		return false
	range_toggle.button_pressed = true
	await get_tree().process_frame
	inspector_group = workspace.find_child("VisualScriptInspectorDisclosure", true, false) as Button
	var min_field := workspace.find_child("VisualScriptInspectorRange_min", true, false) as LineEdit
	var max_field := workspace.find_child("VisualScriptInspectorRange_max", true, false) as LineEdit
	var step_field := workspace.find_child("VisualScriptInspectorRange_step", true, false) as LineEdit
	var slider := workspace.find_child("VisualScriptInspectorSlider", true, false) as BaseButton
	if not _need(inspector_group != null and inspector_group.button_pressed and min_field != null and min_field.is_visible_in_tree() and
			max_field != null and step_field != null and slider != null and slider.button_pressed,
			"Use Range rebuilds its controls and keeps Inspector expanded."):
		return false
	var before: Dictionary = graph_script.call(&"get_graph")
	min_field.text = "-10"
	max_field.text = "60"
	inspector_group.button_pressed = false
	inspector_group.button_pressed = true
	if not _need(min_field.text == "-10" and max_field.text == "60", "Folding Inspector preserves unfinished edits."):
		return false
	step_field.text = "0.5"
	step_field.text_submitted.emit(step_field.text)
	await get_tree().process_frame
	if not _need(graph_script.call(&"get_graph") == before and min_field.text == "-10" and max_field.text == "60",
			"An invalid fractional integer step does not save or erase the other range drafts."):
		return false
	step_field.text = "5"
	step_field.text_submitted.emit(step_field.text)
	await get_tree().process_frame
	slider.button_pressed = false
	await get_tree().process_frame
	graph_data = graph_script.call(&"get_graph")
	return _need(graph_data.variables[0].inspector.range == {"min": -10, "max": 60, "step": 5, "slider": false} and
			graph_data.variables[0].value == ORIGINAL_DEFAULT,
			"The three validated range entries and number field style save together without changing the variable default.")
