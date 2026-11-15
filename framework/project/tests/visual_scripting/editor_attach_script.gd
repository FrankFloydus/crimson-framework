# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const SCRIPT_PATH := "res://attach_script_fixture.crimsonscript"
const INCOMPATIBLE_SCRIPT_PATH := "res://attach_script_incompatible.crimsonscript"
const SCENE_PATH := "res://attach_script_fixture.tscn"
const TARGET_NAME := &"AttachScriptTarget"
const MISMATCH_TARGET_NAME := &"WrongNodeType"


## Start once the native editor can display a generated scene.
func _enter_tree() -> void:
	_verify.call_deferred()


## Report the editor action that failed in the isolated project.
func _need(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("Visual Script attach: " + requirement)
		get_tree().quit(1)
	return condition


## Generate a plain scene and a compatible Visual Script through Godot's saver.
func _save_fixtures() -> bool:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if graph_script == null:
		return false
	graph_script.call(&"set_graph_base", &"Node")
	if ResourceSaver.save(graph_script, SCRIPT_PATH) != OK:
		return false
	var incompatible_script := ClassDB.instantiate(&"VisualScript") as Script
	incompatible_script.call(&"set_graph_base", &"Control")
	if ResourceSaver.save(incompatible_script, INCOMPATIBLE_SCRIPT_PATH) != OK:
		return false
	var scene_root := Node.new()
	scene_root.name = &"AttachScriptFixture"
	for target_name: StringName in [TARGET_NAME, MISMATCH_TARGET_NAME]:
		var target := Node3D.new()
		target.name = target_name
		scene_root.add_child(target)
		target.owner = scene_root
	var packed_scene := PackedScene.new()
	var pack_result := packed_scene.pack(scene_root)
	scene_root.free()
	return pack_result == OK and ResourceSaver.save(packed_scene, SCENE_PATH) == OK


## Wait for restored tabs and the filesystem scan before opening the live target.
func _open_fixture() -> Node:
	var filesystem := EditorInterface.get_resource_filesystem()
	for attempt: int in range(60):
		var current := EditorInterface.get_edited_scene_root()
		if current != null and current.scene_file_path == SCENE_PATH:
			return current
		if not filesystem.is_scanning():
			EditorInterface.open_scene_from_path(SCENE_PATH)
		await get_tree().process_frame
	return null


## Locate the stock Scene Tree with the generated nodes.
func _native_scene_tree() -> Tree:
	for tree_node: Node in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
		var tree := tree_node as Tree
		if not tree.is_visible_in_tree():
			continue
		var row := tree.get_root()
		if row != null and row.get_first_child() != null and row.get_first_child().get_text(0) == TARGET_NAME:
			return tree
	return null


## Look up a child row without assuming Godot's scene sort order.
func _scene_row(tree: Tree, target_name: StringName) -> TreeItem:
	for row: TreeItem in tree.get_root().get_children():
		if row.get_text(0) == target_name:
			return row
	return null


## Pass the FileSystem payload through Godot's drag routing and click the native Scene Tree row.
func _drop_file(tree: Tree, row: TreeItem, path: String) -> bool:
	var row_center := tree.get_item_area_rect(row, 0).get_center()
	var point: Vector2 = tree.get_global_transform_with_canvas() * row_center
	var window := tree.get_window()
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	# The drop queries Input's current pointer position even when the click itself is synthetic.
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	tree.force_drag({"type": "files", "files": PackedStringArray([path])}, null)
	window.push_input(motion, true)
	for pressed: bool in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = point
		click.global_position = point
		window.push_input(click, true)
	return tree.get_viewport().gui_is_drag_successful()


## Attach through the editor, then undo, redo, and verify scene serialization.
func _verify() -> void:
	if not _need(_save_fixtures(), "Save the attachable graph and target scene."):
		return
	var fixture_scene := await _open_fixture()
	var owner_node := fixture_scene.get_node_or_null(NodePath(TARGET_NAME)) if fixture_scene != null else null
	if not _need(owner_node != null and owner_node.get_script() == null, "Open the target scene without a script."):
		return
	var filesystem := EditorInterface.get_resource_filesystem()
	filesystem.update_file(SCRIPT_PATH)
	filesystem.update_file(INCOMPATIBLE_SCRIPT_PATH)
	await get_tree().process_frame
	var scene_tree := _native_scene_tree()
	var target_row := _scene_row(scene_tree, TARGET_NAME) if scene_tree != null else null
	if not _need(target_row != null, "Find the generated node in the native Scene Tree."):
		return
	var history := get_undo_redo().get_history_undo_redo(get_undo_redo().get_object_history_id(owner_node))
	var accepted := _drop_file(scene_tree, target_row, SCRIPT_PATH)
	var assigned_script := owner_node.get_script() as Script
	var advertised_type := filesystem.get_file_type(SCRIPT_PATH)
	if not _need(accepted and assigned_script != null and assigned_script.get_path() == SCRIPT_PATH and
		assigned_script.get_class() == &"VisualScript", "Drop the graph file onto the compatible node (indexed type: " +
		String(advertised_type) + ")."):
		return
	if not _need(history != null and history.undo() and owner_node.get_script() == null,
		"Godot Undo removes the attached script."):
		return
	if not _need(history.redo() and owner_node.get_script() == assigned_script, "Godot Redo reattaches the same script."):
		return
	EditorInterface.save_scene_as(SCENE_PATH, false)
	if not _need(FileAccess.get_file_as_string(SCENE_PATH).contains(SCRIPT_PATH),
		"Saving the scene persists the attachment."):
		return
	await get_tree().process_frame
	var mismatched_owner := fixture_scene.get_node_or_null(NodePath(MISMATCH_TARGET_NAME))
	var mismatch_row := _scene_row(scene_tree, MISMATCH_TARGET_NAME)
	if not _need(mismatched_owner != null and mismatch_row != null,
		"Find the incompatible node in the native Scene Tree."):
		return
	var mismatch_drop := _drop_file(scene_tree, mismatch_row, INCOMPATIBLE_SCRIPT_PATH)
	if not _need(mismatch_drop and mismatched_owner.get_script() == null,
		"Dropping a Control graph onto a Node3D does not attach the graph."):
		return
	await get_tree().process_frame
	var toaster := EditorInterface.get_editor_toaster()
	var warning_found := false
	for child_node: Node in toaster.get_parent().find_children("*", "Label", true, false):
		var label := child_node as Label
		if (label.text.begins_with("[Crimson] Cannot attach: Control required, got Node3D.") and
				label.text.contains(INCOMPATIBLE_SCRIPT_PATH) and label.text.contains("Use Undo")):
			warning_found = true
			break
	if not _need(warning_found, "The editor presents the incompatible base in its native warning toast."):
		return
	print("CRIMSON_VISUAL_SCRIPT_EDITOR_ATTACH_PASSED")
	get_tree().quit()
