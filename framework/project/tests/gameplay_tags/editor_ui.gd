# Copyright Francesco Di Blasi 2026 - All Rights Reserved

@tool
extends EditorPlugin

const FIXTURE_SCRIPT: String = "res://gameplay_tag_editor_fields.gd"
const FIXTURE_SCENE: String = "res://gameplay_tag_editor_fields.tscn"
const GRAPH_SCRIPT: String = "res://gameplay_tag_editor_fields.crimsonscript"
const GRAPH_SCENE: String = "res://gameplay_tag_visual_fields.tscn"
const SINGLE_FIELD: StringName = &"effect_tag"
const SET_FIELD: StringName = &"blocked_tags"
const STATE: StringName = &"state"
const CONTROLLED: StringName = &"state.control"
const STUN: StringName = &"state.control.stun"
const SLOW: StringName = &"state.control.slow"
const DEEP: StringName = &"state.control.movement.slide"
const SPRINT: StringName = &"movement.sprint"
const DASH: StringName = &"movement.dash"
const RENAMED_DASH: StringName = &"movement.evade"
const DELETE_ME: StringName = &"test.obsolete"
const CHIP_A: StringName = &"layout.a"
const CHIP_B: StringName = &"layout.b"
const LONG_CHIP: StringName = &"layout.a_very_long_gameplay_tag_that_has_to_clip_to_the_available_width.so_its_remove_button_remains_visible"
const RENAME_COMMAND: int = 2
const DELETE_COMMAND: int = 3
const CLEAR_COMMAND: int = 7

## Scene root whose fields are changed through the native Inspector.
var edited_node: Node
## Shared dialog opened by either property row.
var picker: Window
## Search field that owns initial focus.
var search_field: LineEdit
## Native tag result hierarchy.
var results: Tree


## Start after the native Inspector plugin enters the editor tree.
func _enter_tree() -> void:
	_verify.call_deferred()


## Exercise only assets and catalog paths created in the isolated editor project.
func _verify() -> void:
	await _frames()
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	for path: StringName in [STUN, SLOW, DEEP, SPRINT, DELETE_ME, CHIP_A, CHIP_B, LONG_CHIP]:
		if not _require(GameplayTagCatalog.add_project_tag(path) == OK, "Declare the test tag " + String(path) + "."):
			return
	if not _require(_create_scene(), "Create the disposable scene with two typed fields."):
		return
	EditorInterface.open_scene_from_path(FIXTURE_SCENE)
	await _frames()
	edited_node = EditorInterface.get_edited_scene_root()
	if not _require(edited_node != null and edited_node.scene_file_path == FIXTURE_SCENE, "Open the two-field fixture."):
		return
	# Keep the desktop pointer outside the 3D viewport's focus-on-hover behavior.
	EditorInterface.set_main_screen_editor("Script")
	EditorInterface.inspect_object(edited_node)
	await _frames()
	picker = get_tree().root.find_child("GameplayTagPicker", true, false) as Window
	if not _require(picker != null and _property_row(SINGLE_FIELD) != null and _property_row(SET_FIELD) != null,
			"Both null typed exports use native gameplay tag Inspector rows."):
		return
	search_field = picker.find_child("GameplayTagSearch", true, false) as LineEdit
	results = picker.find_child("GameplayTagTree", true, false) as Tree
	if not await _verify_single() or not await _verify_container() or not await _verify_hierarchy() or not await _verify_catalog() or not await _verify_visual_exports():
		return
	print("CRIMSON_GAMEPLAY_TAG_EDITOR_UI_PASSED")
	get_tree().quit()


## Generate and save a tool script so Godot's actual export hints drive the Inspector.
func _create_scene() -> bool:
	var field_script := GDScript.new()
	field_script.source_code = """
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends Node

## Tag that identifies the current effect.
@export var effect_tag: GameplayTag
## Tags that block this action.
@export var blocked_tags: GameplayTagContainer
"""
	field_script.take_over_path(FIXTURE_SCRIPT)
	if field_script.reload() != OK or ResourceSaver.save(field_script, FIXTURE_SCRIPT) != OK:
		return false
	return _save_scene(field_script, FIXTURE_SCENE)


## Save the given script attachment so the scene editor owns its property history.
func _save_scene(field_script: Script, destination: String) -> bool:
	var scene_root := Node.new()
	scene_root.name = "GameplayTagEditorFields"
	scene_root.set_script(field_script)
	var packed := PackedScene.new()
	var pack_result := packed.pack(scene_root)
	scene_root.free()
	return pack_result == OK and ResourceSaver.save(packed, destination) == OK


## Search by several path terms, choose one tag with the keyboard, cancel, and clear.
func _verify_single() -> bool:
	var trigger := await _open_for(SINGLE_FIELD)
	if not _require(trigger != null and search_field.has_focus(), "The single chip opens the picker with search focused."):
		return false
	_search("control stun")
	if not _require(_tag_item(STUN) != null and _tag_item(CONTROLLED) != null and _tag_item(SLOW) == null,
			"Search keeps matching paths and their parent hierarchy."):
		return false
	_choose(STUN)
	if not _require(_tag_item(STUN).is_checked(0) and _tag_item(CONTROLLED).is_checked(0)
			and _tag_item(STATE).is_checked(0) and not _tag_item(STATE).is_indeterminate(0),
			"A single tag checks each visible ancestor, without a mixed state."):
		return false
	_press("GameplayTagApply")
	await _frames()
	var chosen := edited_node.get(SINGLE_FIELD) as GameplayTag
	if not _require(chosen != null and chosen.get_tag_name() == STUN and not picker.visible,
			"Apply assigns the selected tag to the actual field."):
		return false
	if not _require(trigger.has_focus(), "Closing the picker restores Inspector focus."):
		return false
	await _open_for(SINGLE_FIELD)
	_choose(SLOW)
	_key(KEY_ESCAPE)
	await _frames()
	if not _require(not picker.visible and edited_node.get(SINGLE_FIELD) == chosen,
			"Escape discards selection changes."):
		return false
	await _open_for(SINGLE_FIELD)
	_choose(STUN)
	_press("GameplayTagApply")
	await _frames()
	var fallback := edited_node.get(SINGLE_FIELD) as GameplayTag
	if not _require(fallback != null and fallback.get_tag_name() == CONTROLLED,
			"Unchecking the single leaf selects its direct parent."):
		return false
	await _open_for(SINGLE_FIELD)
	_choose(SLOW)
	if not _require(_tag_item(SLOW).is_checked(0) and not _tag_item(STUN).is_checked(0),
			"Selecting another tag replaces the single draft choice."):
		return false
	_command(CLEAR_COMMAND)
	_press("GameplayTagApply")
	await _frames()
	return _require(edited_node.get(SINGLE_FIELD) == null, "Apply an empty selection to clear the single tag.")


## Check explicit membership, detached edits, per-chip removal, and Godot Undo and Redo.
func _verify_container() -> bool:
	if await _open_for(SET_FIELD) == null:
		return false
	_choose(STATE)
	_choose(CONTROLLED)
	_choose(STUN)
	_choose(SPRINT)
	_press("GameplayTagApply")
	await _frames()
	var original := edited_node.get(SET_FIELD) as GameplayTagContainer
	if not _require(original != null and original.size() == 2 and original.has_exact(GameplayTag.find(STUN))
			and original.has(GameplayTag.find(STATE)) and not original.has_exact(GameplayTag.find(CONTROLLED)),
			"Checking a descendant replaces exact ancestor chips while keeping inherited matches."):
		return false
	if not await _verify_container_layout(original):
		return false
	var erase := _property_row(SET_FIELD).find_child("GameplayTagChipRemove", true, false) as Button
	if not _require(erase != null, "Each explicit tag has a removal action."):
		return false
	erase.pressed.emit()
	await _frames()
	var edited := edited_node.get(SET_FIELD) as GameplayTagContainer
	if not _require(edited != null and edited.size() == 1 and not edited.has_exact(GameplayTag.find(STUN))
			and original.size() == 2, "Remove one tag without mutating the original resource."):
		return false
	var history_manager := EditorInterface.get_editor_undo_redo()
	var history := history_manager.get_history_undo_redo(history_manager.get_object_history_id(edited_node))
	if not _require(history.undo(), "Undo the Inspector's chip deletion."):
		return false
	await _frames()
	if not _require(edited_node.get(SET_FIELD) == original and history.redo(),
			"Undo restores the previous resource and Redo is available."):
		return false
	await _frames()
	return _require(edited_node.get(SET_FIELD) == edited, "Redo restores the detached one-tag container.")


## Several explicit tags share one field, with room to wrap and click its background.
func _verify_container_layout(previous: GameplayTagContainer) -> bool:
	var sample := GameplayTagContainer.new()
	for path: StringName in [CHIP_A, CHIP_B, LONG_CHIP]:
		sample.add(GameplayTag.find(path))
	edited_node.set(SET_FIELD, sample)
	var row := _property_row(SET_FIELD)
	row.update_property()
	await _frames()
	var field := row.find_child("GameplayTagContainerField", true, false) as Control
	var flow := row.find_child("GameplayTagChips", true, false) as HFlowContainer
	if not _require(field != null and flow != null and flow.get_child_count() == 3,
			"Display the explicit tags in one native wrapping field."):
		return false
	var first := flow.get_child(0) as Control
	var second := flow.get_child(1) as Control
	var long_item := flow.get_child(2) as Control
	var long_remove := long_item.find_child("GameplayTagChipRemove", true, false) as Button
	if not _require(is_equal_approx(first.position.y, second.position.y) and long_item.position.y > second.position.y
			and flow.get_global_rect().grow(1.0).encloses(long_remove.get_global_rect()),
			"Short chips share a line and a long chip wraps with its remove action visible."):
		return false
	var inspector := EditorInterface.get_inspector()
	inspector.ensure_control_visible(field)
	await _frames()
	var field_bounds := field.get_global_rect()
	var padding_width := flow.get_global_rect().position.x - field_bounds.position.x
	var gutter := Rect2(field_bounds.position, Vector2(padding_width, field_bounds.size.y))
	# A saved headless editor layout can extend the Inspector past the viewport edge.
	var visible_background := gutter.intersection(inspector.get_global_rect()).intersection(field.get_window().get_visible_rect())
	if not _require(visible_background.has_area(), "Find visible blank space at the edge of the populated field."):
		return false
	await _click_at(field.get_window(), visible_background.get_center())
	await _frames()
	if not _require(picker.visible, "Clicking the populated field background opens its picker."):
		return false
	_press("GameplayTagCancel")
	edited_node.set(SET_FIELD, previous)
	row.update_property()
	await _frames()
	return true


## Promotion considers exact direct siblings and clearing reaches tags hidden by search.
func _verify_hierarchy() -> bool:
	if await _open_for(SET_FIELD) == null:
		return false
	_choose(STUN)
	_choose(SLOW)
	_search("stun")
	_choose(STUN)
	_press("GameplayTagApply")
	await _frames()
	var shallow := edited_node.get(SET_FIELD) as GameplayTagContainer
	var parent_tag := GameplayTag.find(CONTROLLED)
	if not _require(shallow.size() == 2 and shallow.has_exact(GameplayTag.find(SLOW)) and not shallow.has_exact(parent_tag),
			"A search-hidden exact direct sibling keeps the parent implicit."):
		return false

	await _open_for(SET_FIELD)
	_choose(DEEP)
	_choose(SLOW)
	_press("GameplayTagApply")
	await _frames()
	var deep_set := edited_node.get(SET_FIELD) as GameplayTagContainer
	if not _require(deep_set.has_exact(parent_tag) and deep_set.has_exact(GameplayTag.find(DEEP)),
			"A deeper tag in a sibling branch does not suppress direct parent promotion."):
		return false

	await _open_for(SET_FIELD)
	_search("stun")
	_choose(CONTROLLED)
	_press("GameplayTagApply")
	await _frames()
	var cleared := edited_node.get(SET_FIELD) as GameplayTagContainer
	return _require(cleared.size() == 2 and cleared.has_exact(GameplayTag.find(STATE)) and not cleared.has(parent_tag)
			and cleared.has_exact(GameplayTag.find(SPRINT)),
			"Unchecking a branch removes its search-hidden descendants and preserves unrelated tags.")


## Author, rename, and delete tags through the same project's shared picker.
func _verify_catalog() -> bool:
	if await _open_for(SINGLE_FIELD) == null:
		return false
	_press("GameplayTagNew")
	await _frames()
	var path_input := picker.find_child("GameplayTagPath", true, false) as LineEdit
	if not _require(path_input != null and path_input.has_focus(), "A new tag opens the catalog name form."):
		return false
	path_input.text = "movement..invalid"
	path_input.text_submitted.emit(path_input.text)
	await _frames()
	if not _require(path_input.is_visible_in_tree(), "An invalid name keeps the catalog form open."):
		return false
	path_input.text = String(DASH)
	path_input.text_submitted.emit(path_input.text)
	await _frames()
	if not _require(not path_input.is_visible_in_tree() and GameplayTag.find(DASH) != null and _tag_item(DASH) != null,
			"A valid declaration persists and appears in the picker."):
		return false
	_tag_item(DASH).select(0)
	_command(RENAME_COMMAND)
	await _frames()
	path_input.text = String(RENAMED_DASH)
	path_input.text_submitted.emit(path_input.text)
	await _frames()
	var redirected := GameplayTag.find(DASH)
	if not _require(redirected != null and redirected.get_tag_name() == RENAMED_DASH and _tag_item(RENAMED_DASH) != null,
			"Rename displays the new path and saves the old name as a redirect."):
		return false
	_tag_item(DELETE_ME).select(0)
	_command(DELETE_COMMAND)
	await _frames()
	var catalog_prompt := picker.find_child("GameplayTagCatalogPrompt", true, false) as Control
	var confirm := catalog_prompt.find_child("ConfirmDialog", true, false) as Button
	if not _require(catalog_prompt.visible and confirm != null, "Deleting a declaration requires confirmation."):
		return false
	confirm.pressed.emit()
	await _frames()
	if not _require(GameplayTag.find(DELETE_ME) == null and _tag_item(DELETE_ME) == null,
			"Confirmed deletion updates the project catalog and the picker."):
		return false
	_press("GameplayTagCancel")
	await _frames()
	return _require(not picker.visible and edited_node.get(SINGLE_FIELD) == null,
			"Catalog edits leave the field unchanged until its draft is applied.")


## Both declared Visual Script object types enter the same native Inspector path.
func _verify_visual_exports() -> bool:
	var graph := ClassDB.instantiate(&"VisualScript") as Script
	if not _require(graph != null, "Create a native Visual Script resource."):
		return false
	graph.call(&"set_graph_base", &"Node")
	graph.call(&"set_graph", {
		"version": 1,
		"nodes": [],
		"connections": [],
		"variables": [
			{"id": 1, "name": SINGLE_FIELD, "exported": true, "value": null,
				"pin": {"id": &"value", "type": TYPE_OBJECT, "class": &"GameplayTag"}},
			{"id": 2, "name": SET_FIELD, "exported": true, "value": null,
				"pin": {"id": &"value", "type": TYPE_OBJECT, "class": &"GameplayTagContainer"}},
		],
	})
	if not _require(ResourceSaver.save(graph, GRAPH_SCRIPT) == OK, "Save the exported Visual Script resource."):
		return false
	var saved_graph := ResourceLoader.load(GRAPH_SCRIPT, "Script") as Script
	if not _require(saved_graph != null and _save_scene(saved_graph, GRAPH_SCENE),
			"Attach the saved Visual Script to a disposable scene."):
		return false
	EditorInterface.open_scene_from_path(GRAPH_SCENE)
	await _frames()
	edited_node = EditorInterface.get_edited_scene_root()
	if not _require(edited_node != null and edited_node.scene_file_path == GRAPH_SCENE, "Open the Visual Script scene."):
		return false
	EditorInterface.inspect_object(edited_node)
	await _frames()
	if not _require(_property_row(SINGLE_FIELD) != null and _property_row(SET_FIELD) != null,
			"Visual Script exports use the gameplay tag Inspector controls."):
		return false
	if await _open_for(SINGLE_FIELD) == null:
		return false
	_choose(SLOW)
	_press("GameplayTagApply")
	await _frames()
	var tag := edited_node.get(SINGLE_FIELD) as GameplayTag
	return _require(tag != null and tag.get_tag_name() == SLOW, "The picker assigns a Visual Script export override.")


## Find the native Inspector row for a specific exported property.
func _property_row(field: StringName) -> EditorProperty:
	for candidate: Node in EditorInterface.get_inspector().find_children("*", "EditorProperty", true, false):
		var row := candidate as EditorProperty
		if row.get_edited_property() == field and row.find_child("GameplayTagPickerTrigger", true, false) != null:
			return row
	return null


## Open the shared picker as an Inspector user.
func _open_for(field: StringName) -> Button:
	var row := _property_row(field)
	var trigger := row.find_child("GameplayTagPickerTrigger", true, false) as Button if row != null else null
	if not _require(trigger != null, "Find the gameplay tag trigger for " + String(field) + "."):
		return null
	trigger.grab_focus()
	trigger.pressed.emit()
	await _frames()
	return trigger if _require(picker.visible, "The shared picker opens.") else null


## Find a visible result by its canonical metadata.
func _tag_item(path: StringName) -> TreeItem:
	return _find_item(results.get_root(), String(path))


## Walk tree data without depending on captions, scroll offsets, or theme.
func _find_item(item: TreeItem, path: String) -> TreeItem:
	if item == null:
		return null
	if item.get_metadata(0) == path:
		return item
	var child := item.get_first_child()
	while child != null:
		var found := _find_item(child, path)
		if found != null:
			return found
		child = child.get_next()
	return null


## Enter a query as though it came from the search field.
func _search(query: String) -> void:
	search_field.text = query
	search_field.text_changed.emit(query)


## Activate the selected tree row through the picker's native keyboard input path.
func _choose(path: StringName) -> void:
	var item := _tag_item(path)
	if _require(item != null, "Find the selectable path " + String(path) + "."):
		item.select(0)
		results.grab_focus()
		_key(KEY_ENTER)


## Dispatch one Options action by its command ID.
func _command(command_id: int) -> void:
	var options := picker.find_child("GameplayTagOptions", true, false) as MenuButton
	options.get_popup().id_pressed.emit(command_id)


## Press a named picker button.
func _press(control_name: String) -> void:
	var target := picker.find_child(control_name, true, false) as Button
	target.pressed.emit()


## Send a key to the current picker window using local coordinates.
func _key(key: Key) -> void:
	var input_event := InputEventKey.new()
	input_event.keycode = key
	input_event.pressed = true
	picker.push_input(input_event, true)


## Click a real editor control through the window's input routing.
func _click_at(window: Window, position_in_window: Vector2) -> void:
	var native_pointer := DisplayServer.get_name() != "headless"
	var previous_pointer := DisplayServer.mouse_get_position()
	if native_pointer:
		window.warp_mouse(position_in_window)
		await get_tree().process_frame
	for is_pressed: bool in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.button_mask = MOUSE_BUTTON_MASK_LEFT if is_pressed else 0
		click.position = position_in_window
		click.global_position = position_in_window
		click.pressed = is_pressed
		window.push_input(click, true)
	if native_pointer:
		DisplayServer.warp_mouse(previous_pointer)


## Yield until the editor has processed inspector and window changes.
func _frames() -> void:
	for frame_index: int in range(4):
		await get_tree().process_frame


## Fail the isolated editor with the action that did not complete.
func _require(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("[Crimson] Gameplay Tags editor fixture: " + requirement)
		get_tree().quit(1)
	return condition
