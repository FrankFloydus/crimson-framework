# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

## A disposable script resource keeps the workspace test independent of the demo and inspector.
const GRAPH_PATH: String = "res://native_focus_probe.crimsonscript"
## Compare a graph attachment with Godot's ordinary GDScript Scene dock action.
const ICON_SCENE_PATH: String = "res://native_graph_icons.tscn"
const ICON_SCRIPT_PATH: String = "res://native_graph_icons_stock.gd"
const SCRIPT_BUTTON_ID: int = 2
const WORKSPACE_NAME: String = "CrimsonVisualScriptWorkspace"
const MAIN_SCREEN_TITLE: String = "Visual Script"

var _wire_starts: int = 0
var _wire_ends: int = 0
var _graph_focus_lost: int = 0
var _graph_focus_returned: int = 0

## Wait for the native editor workspace to enter the tree.
func _enter_tree() -> void:
	_check.call_deferred()

## Exercise the built-in workspace, dock drags, and wire cancellation on Windows.
func _check() -> void:
	await _frames(3)
	var graph_window := EditorInterface.get_base_control().get_window()
	# Keep the isolated fixture's docks and central graph visible even at higher editor scales.
	var work_area := DisplayServer.screen_get_usable_rect(graph_window.current_screen)
	graph_window.position = work_area.position + Vector2i(24, 48)
	graph_window.size = Vector2i(1440, 900).min(work_area.size - Vector2i(48, 96))
	var graph_asset := ClassDB.instantiate(&"VisualScript") as Script
	if not _require(graph_asset != null, "Create disposable Visual Script."):
		return
	var definition := {
		"version": 1,
		"nodes": [
			{"id": 1, "kind": &"ready", "position": Vector2(20, 72)},
			{"id": 2, "kind": &"print", "position": Vector2(270, 108)},
			{"id": 3, "kind": &"print", "position": Vector2(270, 248)},
		],
		"connections": [],
	}
	graph_asset.call(&"set_graph", definition)
	if not _require(ResourceSaver.save(graph_asset, GRAPH_PATH) == OK, "Save disposable graph."):
		return
	var saved_graph := ResourceLoader.load(GRAPH_PATH, "Script") as Script
	if not _require(saved_graph != null, "Load disposable graph."):
		return
	if not await _verify_scene_dock_icons(saved_graph):
		return
	var windows_before := DisplayServer.get_window_list()
	EditorInterface.edit_script(saved_graph, -1, 0, true)
	await _frames(3)
	var workspace := _workspace()
	if not _require(workspace != null and EditorInterface.get_editor_main_screen().is_ancestor_of(workspace) and
			workspace.is_visible_in_tree() and workspace.get_window() == graph_window and
			windows_before == DisplayServer.get_window_list(), "Open the Visual Script main screen without a separate native window."):
		return
	var canvas := workspace.find_child("GraphCanvas", true, false) as GraphEdit
	if not _require(canvas != null, "Display graph canvas."):
		return
	var original_topmost := graph_window.always_on_top
	# Native hover follows the desktop pointer. Keep the isolated test window above other open apps.
	graph_window.always_on_top = true
	graph_window.grab_focus()
	await _frames(2)
	var tabs_passed := await _check_main_screen_tabs(workspace, canvas, saved_graph)
	if not tabs_passed:
		graph_window.always_on_top = original_topmost
		return
	if OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-main-workspace.png")
		await _capture_declarations(graph_window, workspace, saved_graph)
	canvas.connection_drag_started.connect(_record_wire_start)
	canvas.connection_drag_ended.connect(_record_wire_end)
	graph_window.focus_exited.connect(_record_lost)
	graph_window.focus_entered.connect(_record_returned)
	var companion := Window.new()
	companion.hide()
	companion.force_native = true
	companion.always_on_top = true
	companion.title = "Crimson Focus Probe Companion"
	companion.size = Vector2i(250, 90)
	get_tree().root.add_child(companion)
	companion.show()
	await _frames(3)
	if not _require(not companion.is_embedded() and companion.get_window_id() != graph_window.get_window_id(), "Use two native Windows IDs."):
		return
	graph_window.grab_focus()
	if not await _wait_for_focus(graph_window, companion):
		return
	if not await _check_native_find(graph_window, workspace, canvas, saved_graph):
		return
	await _frames(3)
	if not await _check_preload_and_pin_feedback(graph_window, canvas, saved_graph):
		return
	saved_graph.call(&"set_graph", definition)
	await _frames(3)
	if not await _check_marquee_after_space_focus_loss(graph_window, companion, canvas):
		return
	if not await _check_middle_pan_focus_loss(graph_window, companion, canvas):
		return
	if not await _check_wire_picker_lifecycle(graph_window, canvas, saved_graph):
		return
	saved_graph.call(&"set_graph", definition)
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await _frames(3)
	var drop_passed := await _check_picker_and_file_drop(graph_window, canvas, saved_graph)
	if drop_passed:
		drop_passed = await _check_scene_dock_drop(graph_window, canvas, saved_graph)
	graph_window.always_on_top = original_topmost
	if not drop_passed:
		return
	for from_output: bool in [true, false]:
		saved_graph.call(&"set_graph", definition)
		canvas.zoom = 1.0
		canvas.scroll_offset = Vector2.ZERO
		await _frames(4)
		var source := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
		var receiver := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
		if not _require(source != null and receiver != null, "Resolve fresh graph nodes."):
			return
		var output_point := _socket(canvas, source, true)
		var input_point := _socket(canvas, receiver, false)
		var origin := output_point if from_output else input_point
		var target := input_point if from_output else output_point
		var starts_before := _wire_starts
		var ends_before := _wire_ends
		var lost_before := _graph_focus_lost
		var returned_before := _graph_focus_returned
		_button(graph_window, origin, true)
		_motion(graph_window, origin, target)
		await _frames(2)
		if not _require(_edge_count(saved_graph) == 0 and _wire_starts == starts_before + 1, "The wire drag is live and has not committed before release."):
			return
		if from_output and OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
			_capture(graph_window, "native-compatible-wire.png")
		companion.grab_focus()
		if not await _wait_for_focus(companion, graph_window):
			return
		if not _require(_graph_focus_lost > lost_before and _edge_count(saved_graph) == 0 and _wire_ends == ends_before + 1, "OS focus loss canceled the dragged wire."):
			return
		graph_window.grab_focus()
		if not await _wait_for_focus(graph_window, companion):
			return
		_button(graph_window, target, false)
		await _frames(2)
		if from_output and OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
			_capture(graph_window, "native-focus-cancelled-wire.png")
		if not _require(_graph_focus_returned > returned_before and _edge_count(saved_graph) == 0, "Late release after the OS focus return did not place a wire."):
			return
		var palette := graph_window.find_child("GraphPaletteSearch", true, false) as LineEdit
		if not _require(palette != null and not palette.is_visible_in_tree(), "The stale drag did not open the graph picker."):
			return
		_button(graph_window, origin, true)
		_motion(graph_window, origin, target)
		_button(graph_window, target, false)
		await _frames(3)
		if not _require(_edge_count(saved_graph) == 1, "A fresh compatible drag succeeds after focus returns."):
			return
		print("CRIMSON_NATIVE_FOCUS_DIRECTION_PASSED ", "output" if from_output else "input")
	print("CRIMSON_NATIVE_FOCUS_GRAPH_PASSED ", DisplayServer.get_name(), " ", _graph_focus_lost, " losses")
	get_tree().quit()

## Locate the production graph control inside the editor's dock hierarchy.
func _workspace() -> Control:
	return EditorInterface.get_base_control().find_child(WORKSPACE_NAME, true, false) as Control

## The native workspace selector shows Visual Script after Asset Store and restores the open document when reselected.
func _check_main_screen_tabs(workspace: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var graph_screen_button: Button
	for candidate: Button in EditorInterface.get_base_control().find_children("*", "Button", true, false):
		if candidate.text == MAIN_SCREEN_TITLE and candidate.is_visible_in_tree():
			graph_screen_button = candidate
			break
	if not _require(graph_screen_button != null, "Visual Script appears in the native editor main screen selector."):
		return false
	var left_screen: Button
	for sibling: Node in graph_screen_button.get_parent().get_children():
		if sibling == graph_screen_button:
			break
		if sibling is Button and sibling.is_visible_in_tree():
			left_screen = sibling as Button
	if not _require(left_screen != null and left_screen.text in ["Asset Store", "AssetLib"],
			"Visual Script follows Godot's asset screen in the native main screen selector."):
		return false
	var scene := EditorInterface.get_edited_scene_root()
	var before: Dictionary = graph_script.call(&"get_graph")
	left_screen.pressed.emit()
	await _frames(3)
	if not _require(not workspace.is_visible_in_tree(), "Selecting a stock screen hides the graph workspace."):
		return false
	graph_screen_button.pressed.emit()
	await _frames(3)
	return _require(workspace.is_visible_in_tree() and canvas.is_visible_in_tree() and
			EditorInterface.get_edited_scene_root() == scene and graph_script.call(&"get_graph") == before,
			"Returning to the Visual Script tab restores the same scene and graph.")

## Verify native search input dims unrelated cards, then restores them on both Escape and jumping.
func _check_native_find(graph_window: Window, workspace: Control, canvas: GraphEdit, graph_script: Script) -> bool:
	var finder := workspace.find_child("GraphFindQuery", true, false) as LineEdit
	var popup := workspace.find_child("GraphFindPopup", true, false) as Control
	var matching_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var nonmatching_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(finder != null and finder.is_visible_in_tree() and popup != null and not popup.visible and
			matching_card != null and nonmatching_card != null, "Find the directly editable toolbar and fixture cards."):
		return false
	var before: Dictionary = graph_script.call(&"get_graph")
	var original_match := matching_card.modulate
	var original_other := nonmatching_card.modulate
	if OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-graph-toolbar.png")
	for accept: bool in [false, true]:
		var field_center := finder.get_global_rect().get_center()
		_button(graph_window, field_center, true)
		_button(graph_window, field_center, false)
		await _frames(2)
		if not _require(popup.is_visible_in_tree() and graph_window.gui_get_focus_owner() == finder,
				"The results open while the toolbar input retains focus."):
			return false
		var field_rect := finder.get_global_rect()
		var result_rect := popup.get_global_rect()
		if not _require(popup.get_window() == graph_window and absf(result_rect.position.x - field_rect.position.x) < 1.0 and
				absf(result_rect.size.x - field_rect.size.x) < 1.0 and result_rect.position.y >= field_rect.end.y and
				result_rect.position.y - field_rect.end.y < field_rect.size.y * 0.5,
				"The results sit directly beneath the field and use its width."):
			return false
		for letter: String in ["r", "e", "a", "d", "y"]:
			_native_key(graph_window, letter.to_upper().unicode_at(0) as Key, letter.unicode_at(0))
			await _frames(1)
		if not _require(finder.text == "ready" and matching_card.modulate == original_match and
				nonmatching_card.modulate.a < original_other.a and graph_script.call(&"get_graph") == before,
				"Typing changes result emphasis without editing graph content or the matching card."):
			return false
		if not accept and OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
			_capture(graph_window, "native-graph-find.png")
		_native_key(graph_window, KEY_ENTER if accept else KEY_ESCAPE)
		await _frames(3)
		if not _require(not popup.visible and matching_card.modulate == original_match and nonmatching_card.modulate == original_other and
				graph_script.call(&"get_graph") == before and (not accept or matching_card.selected),
				"Escape and jumping restore the exact card colors. Enter selects the result."):
			return false
	print("CRIMSON_NATIVE_GRAPH_FIND_PASSED")
	return true

## Send a key press and release through the native display dispatch used by popup windows.
func _native_key(target: Window, key: Key, codepoint: int = 0) -> void:
	for pressed: bool in [true, false]:
		var key_event := InputEventKey.new()
		key_event.window_id = target.get_window_id()
		key_event.keycode = key
		key_event.unicode = codepoint
		key_event.pressed = pressed
		Input.parse_input_event(key_event)
		Input.flush_buffered_events()

## Capture the declarations rail and its real add affordance with disposable typed variables.
func _capture_declarations(graph_window: Window, workspace: Control, graph_script: Script) -> void:
	var authored: Dictionary = graph_script.call(&"get_graph")
	var preview: Dictionary = authored.duplicate(true)
	preview.variables = [
		{"id": 1, "name": "checkpoint_name", "pin": {"type": TYPE_STRING}, "value": "Bridge", "exported": true},
		{"id": 2, "name": "movement_speed", "pin": {"type": TYPE_FLOAT}, "value": 5.0},
		{"id": 3, "name": "target_position", "pin": {"type": TYPE_VECTOR3}, "value": Vector3.ZERO},
		{"id": 4, "name": "checkpoint_scene", "pin": {"type": TYPE_OBJECT, "class": &"PackedScene"}, "value": null},
	]
	graph_script.call(&"set_graph", preview)
	await _frames(3)
	var rail := workspace.find_child("VisualScriptDeclarations", true, false) as Tree
	if rail != null and rail.get_root() != null:
		for row: TreeItem in rail.get_root().get_children():
			if row.get_metadata(0) != &"variables":
				continue
			var row_y := rail.get_item_area_rect(row, 0).get_center().y
			for column_offset: int in range(int(rail.size.x)):
				var local := Vector2(rail.size.x - column_offset - 1, row_y)
				if rail.get_button_id_at_position(local) == 1:
					_move_hover(graph_window, rail.get_global_transform_with_canvas() * local)
					await _frames(2)
					break
			break
	_capture(graph_window, "native-variable-sidebar.png")
	graph_script.call(&"set_graph", authored)
	await _frames(3)

## Verify stock thumbnails, saved-scene fallback, resource browsing, and unified mouse hover on a typed card.
func _check_preload_and_pin_feedback(graph_window: Window, canvas: GraphEdit, graph_script: Script) -> bool:
	const TEXTURE_PATH := "res://native_preload_texture.tres"
	const SCRIPT_RESOURCE_PATH := "res://native_preload_source.gd"
	var bitmap := Image.create_empty(36, 18, false, Image.FORMAT_RGBA8)
	bitmap.fill(Color.CORNFLOWER_BLUE)
	var texture_asset := ImageTexture.create_from_image(bitmap)
	if not _require(ResourceSaver.save(texture_asset, TEXTURE_PATH) == OK, "Save the disposable texture preview asset."):
		return false
	var source_file := FileAccess.open(SCRIPT_RESOURCE_PATH, FileAccess.WRITE)
	if not _require(source_file != null, "Open a script resource fixture without loading it."):
		return false
	source_file.store_string("extends Resource\n")
	source_file.close()
	var filesystem := EditorInterface.get_resource_filesystem()
	filesystem.update_file(TEXTURE_PATH)
	filesystem.update_file(ICON_SCENE_PATH)
	graph_script.call(&"set_graph", {
		"version": 1,
		"nodes": [
			{"id": 1, "kind": &"preload_resource", "class": &"Resource", "asset_class": &"ImageTexture", "position": Vector2(26, 44),
				"values": {"asset": {"path": TEXTURE_PATH, "uid": ""}}},
			{"id": 2, "kind": &"preload_resource", "class": &"Resource", "asset_class": &"PackedScene", "position": Vector2(270, 44),
				"values": {"asset": {"path": ICON_SCENE_PATH, "uid": ""}}},
			{"id": 3, "kind": &"preload_resource", "class": &"Resource", "asset_class": &"GDScript", "position": Vector2(26, 250),
				"values": {"asset": {"path": SCRIPT_RESOURCE_PATH, "uid": ""}}},
		],
		"connections": [],
	})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await _frames(4)
	var texture_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var scene_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var script_card := canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	if not _require(texture_card != null and scene_card != null and script_card != null, "Create all authored Preload cards."):
		return false
	var script_asset_field := script_card.find_child("GraphAssetName", true, false) as Button
	if not _require(script_asset_field != null and script_asset_field.get_tooltip_text() == SCRIPT_RESOURCE_PATH,
			"A generic Preload shows a valid uncached Script resource."):
		return false
	var preview := texture_card.find_child("GraphAssetPreview", true, false) as TextureRect
	var scene_preview := scene_card.find_child("GraphAssetPreview", true, false) as TextureRect
	if not _require(preview != null and scene_preview != null and scene_preview.texture != null,
			"A saved scene has a thumbnail or class fallback without instantiation."):
		return false
	var stock_image := EditorInterface.get_editor_theme().get_icon(&"ImageTexture", &"EditorIcons")
	for attempt: int in range(45):
		if preview.texture != null and preview.texture != stock_image:
			break
		await _frames(1)
	if not _require(preview.texture != null and preview.texture != stock_image and
			preview.texture.get_width() > preview.texture.get_height(),
			"The texture card replaces its class icon with the generated landscape thumbnail."):
		return false

	var slot := texture_card.get_child(texture_card.get_output_port_slot(0))
	var caption: Label
	for child_node: Node in slot.get_children():
		if child_node is Label:
			caption = child_node as Label
			break
	if not _require(caption != null, "Find the native output label belonging to the texture pin."):
		return false
	var pin_point := _socket(canvas, texture_card, true)
	var label_point := caption.get_global_transform_with_canvas() * (caption.size * 0.5)
	_move_hover(graph_window, pin_point)
	for attempt: int in range(5):
		await _frames(1)
		if caption.has_theme_color_override(&"font_color"):
			break
		_os_order_motion(graph_window, pin_point, pin_point, false)
	if not _require(caption.has_theme_color_override(&"font_color"),
			"Hovering the socket highlights its label (socket %s, canvas %s, zoom %s)." %
			[pin_point, canvas.get_global_rect(), canvas.zoom]):
		return false
	_move_hover(graph_window, label_point)
	await _frames(1)
	if not _require(caption.has_theme_color_override(&"font_color"), "Hovering the label preserves the same pin highlight."):
		return false
	if OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-preload-pin-hover.png")
	_move_hover(graph_window, canvas.get_global_transform_with_canvas() * (canvas.size * Vector2(0.75, 0.85)))
	await _frames(1)
	if not _require(not caption.has_theme_color_override(&"font_color"), "Leaving the pin clears its transient highlight."):
		return false

	var options := texture_card.find_child("GraphAssetOptions", true, false) as MenuButton
	if not _require(options != null, "Find the typed asset field actions."):
		return false
	options.get_popup().id_pressed.emit(1)
	await _frames(2)
	var dialogs := texture_card.find_children("*", "EditorFileDialog", true, false)
	if not _require(dialogs.size() == 1, "Browse Files opens the native project file picker."):
		return false
	var chooser := dialogs.front() as EditorFileDialog
	var filters := ",".join(chooser.get_filters())
	if not _require(chooser.visible and filters.contains("*.tscn") and filters.contains("*.gd"),
			"An unconstrained Preload can browse saved scenes and GDScript sources."):
		return false
	if not _require(chooser.get_display_mode() == FileDialog.DISPLAY_LIST,
			"Browsing starts without enqueuing previews for every file in the directory."):
		return false
	chooser.file_selected.emit(ICON_SCENE_PATH)
	await _frames(4)
	var authored: Dictionary = graph_script.call(&"get_graph")
	var authored_resource: Dictionary = authored.nodes[0]
	if not _require(authored_resource.asset_class == &"PackedScene" and authored_resource.values.asset.path == ICON_SCENE_PATH,
			"Choosing a saved scene replaces a preload and updates the authored output class."):
		return false
	print("CRIMSON_NATIVE_PRELOAD_PREVIEW_AND_PIN_HOVER_PASSED")
	return true

## Move the pointer without a pressed button to exercise the ordinary hover path.
func _move_hover(graph_window: Window, point: Vector2) -> void:
	graph_window.warp_mouse(point)
	_os_order_motion(graph_window, point, point, false)

## Open a generated scene and inspect the native Scene dock's real button textures.
func _verify_scene_dock_icons(graph_script: Script) -> bool:
	var main_script := GDScript.new()
	main_script.source_code = "extends Node3D\n"
	if not _require(main_script.reload() == OK and ResourceSaver.save(main_script, ICON_SCRIPT_PATH) == OK, "Generate a regular GDScript attachment."):
		return false
	var stock_script := ResourceLoader.load(ICON_SCRIPT_PATH, "GDScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _require(stock_script != null and stock_script.resource_path == ICON_SCRIPT_PATH, "Attach a saved external GDScript resource."):
		return false
	var fixture := Node.new()
	fixture.name = &"SceneIconFixture"
	for label: StringName in [&"GraphOwner", &"GDScriptOwner"]:
		var owner_node := Node3D.new()
		owner_node.name = label
		owner_node.set_script(graph_script if label == &"GraphOwner" else stock_script)
		fixture.add_child(owner_node)
		owner_node.owner = fixture
	var scene_file := PackedScene.new()
	var packed := scene_file.pack(fixture)
	fixture.free()
	if not _require(packed == OK and ResourceSaver.save(scene_file, ICON_SCENE_PATH) == OK, "Save the native Scene dock icon fixture."):
		return false
	var found_details := "Scene dock was not ready."
	for attempt: int in range(60):
		var current := EditorInterface.get_edited_scene_root()
		if current == null or current.scene_file_path != ICON_SCENE_PATH:
			if not EditorInterface.get_resource_filesystem().is_scanning():
				EditorInterface.open_scene_from_path(ICON_SCENE_PATH)
			await _frames(1)
			continue
		for tree_node: Node in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
			var tree := tree_node as Tree
			if not tree.is_visible_in_tree() or tree.get_root() == null:
				continue
			var graph_button := -1
			var stock_button := -1
			var graph_row: TreeItem
			var stock_row: TreeItem
			for row: TreeItem in tree.get_root().get_children():
				if row.get_text(0) == "GraphOwner":
					graph_button = row.get_button_by_id(0, SCRIPT_BUTTON_ID)
					graph_row = row
				elif row.get_text(0) == "GDScriptOwner":
					stock_button = row.get_button_by_id(0, SCRIPT_BUTTON_ID)
					stock_row = row
			if graph_button < 0 or stock_button < 0:
				continue
			var stock := (tree.get_parent() as Control).get_theme_icon(&"Script", &"EditorIcons")
			var graph_icon := graph_row.get_button(0, graph_button)
			var normal_icon := stock_row.get_button(0, stock_button)
			if graph_icon != stock and normal_icon == stock:
				return true
			found_details = "Found both script buttons. Graph icon is stock: %s. GDScript icon is stock: %s." % [graph_icon == stock, normal_icon == stock]
		await _frames(1)
	return _require(false, "The Scene dock decorates the graph button and keeps GDScript stock. " + found_details)

## Route picker keys and FileSystem drops through the editor's native window.
func _check_picker_and_file_drop(graph_window: Window, canvas: GraphEdit, graph_script: Script) -> bool:
	const ASSET_PATH: String = "res://native_focus_drop.tres"
	var input_field := graph_window.find_child("GraphPaletteSearch", true, false) as LineEdit
	if not _require(input_field != null and ResourceSaver.save(AudioStreamWAV.new(), ASSET_PATH) == OK,
			"Generate a resource and resolve the picker for the native input test."):
		return false
	EditorInterface.get_resource_filesystem().update_file(ASSET_PATH)
	canvas.grab_focus()
	var before: Dictionary = graph_script.call(&"get_graph")
	canvas.popup_request.emit(canvas.size - Vector2(12, 12))
	await _frames(2)
	var picker_window := input_field.get_window()
	if not _require(picker_window.visible and picker_window.gui_get_focus_owner() == input_field,
			"Opening the node picker focuses its search field."):
		return false
	for letter: String in ["c", "a", "s", "t", " ", "t", "o"]:
		var key_event := InputEventKey.new()
		key_event.window_id = graph_window.get_window_id() if picker_window.is_embedded() else picker_window.get_window_id()
		key_event.keycode = letter.to_upper().unicode_at(0) as Key
		key_event.unicode = letter.unicode_at(0)
		key_event.pressed = true
		Input.parse_input_event(key_event)
		Input.flush_buffered_events()
		await _frames(1)
	if not _require(input_field.is_visible_in_tree() and input_field.text == "cast to" and graph_script.call(&"get_graph") == before,
			"The native graph routes the entire query to the picker and suppresses canvas commands (query '%s')." % input_field.text):
		return false
	if not await _check_picker_rail(graph_window, input_field.get_window()):
		return false
	if OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-picker-scrollbar.png")
	input_field.get_window().hide()
	return await _check_native_asset_drops(graph_window, canvas, graph_script, ASSET_PATH)

## Exercise immediate insertion and both choices exposed by a script file drop.
func _check_native_asset_drops(graph_window: Window, canvas: GraphEdit, graph_script: Script, audio_path: String) -> bool:
	var before: Dictionary = graph_script.call(&"get_graph")
	if not await _drop_file_from_native_dock(graph_window, canvas, audio_path):
		return false
	var after: Dictionary = graph_script.call(&"get_graph")
	var last_node: Dictionary = after.nodes.back()
	if not _require(after.nodes.size() == before.nodes.size() + 1 and last_node.kind == &"preload_resource" and
			last_node.asset_class == &"AudioStreamWAV" and last_node.values.asset.path == audio_path,
			"A FileSystem drop into the workspace inserts a typed resource."):
		return false
	var choices := {
		"Preload Resource": &"preload_resource",
		"Create from Script": &"create_script_instance",
	}
	EditorInterface.get_resource_filesystem().update_file(ICON_SCRIPT_PATH)
	for option: String in choices:
		graph_script.call(&"set_graph", before)
		await _frames(2)
		if not await _drop_file_from_native_dock(graph_window, canvas, ICON_SCRIPT_PATH):
			return false
		if not await _choose_native_script_drop_result(graph_window, choices[option]):
			return false
		after = graph_script.call(&"get_graph")
		if not _require(after.nodes.size() == before.nodes.size() + 1 and after.nodes.back().kind == choices[option],
				"Clicking the dropped script result inserts exactly one %s node." % option):
			return false
	graph_script.call(&"set_graph", before)
	await _frames(2)
	if not await _drop_file_from_native_dock(graph_window, canvas, ICON_SCRIPT_PATH):
		return false
	var overlay := graph_window.find_child("GraphDropChoiceOverlay", true, false) as Control
	if not _require(overlay != null and overlay.is_visible_in_tree(), "Open a fresh choice to check Escape."):
		return false
	if not _require(graph_window.has_focus(), "The editor keeps native focus on the workspace before Escape."):
		return false
	var escape_key := InputEventKey.new()
	escape_key.window_id = graph_window.get_window_id()
	escape_key.keycode = KEY_ESCAPE
	escape_key.pressed = true
	Input.parse_input_event(escape_key)
	Input.flush_buffered_events()
	escape_key = escape_key.duplicate() as InputEventKey
	escape_key.pressed = false
	Input.parse_input_event(escape_key)
	Input.flush_buffered_events()
	await _frames(2)
	return _require(not is_instance_valid(overlay) and graph_script.call(&"get_graph") == before,
			"Escape dismisses the drop choice and preserves the graph.")

## Verify both reference choices are reachable in the active workspace on the first click.
func _choose_native_script_drop_result(graph_window: Window, kind: StringName) -> bool:
	var overlay := graph_window.find_child("GraphDropChoiceOverlay", true, false) as Control
	if not _require(overlay != null and overlay.is_visible_in_tree(), "Dropping a script opens the reference choices."):
		return false
	var offered: Array[StringName] = []
	var target_button: Button
	for action_button: Button in overlay.find_children("*", "Button", true, false):
		if not action_button.is_visible_in_tree() or not action_button.has_meta(&"drop_kind"):
			continue
		var action_kind: StringName = action_button.get_meta(&"drop_kind")
		offered.append(action_kind)
		if not _require(_workspace().get_global_rect().encloses(action_button.get_global_rect()),
				"The %s choice fits inside the docked graph workspace." % action_kind):
			return false
		if action_kind == kind:
			target_button = action_button
	if not _require(offered.size() == 2 and &"preload_resource" in offered and &"create_script_instance" in offered and target_button != null,
			"The visible chooser offers both script reference actions."):
		return false
	for rail: ScrollBar in overlay.find_children("*", "ScrollBar", true, false):
		if not _require(not rail.is_visible_in_tree(), "The two script drop choices fit without a scrollbar."):
			return false
	var point := target_button.get_global_transform_with_canvas() * (target_button.size * 0.5)
	graph_window.warp_mouse(point)
	await _frames(1)
	_os_order_motion(graph_window, point, point, false)
	var hovered_control := graph_window.gui_get_hovered_control()
	if not _require(hovered_control != null and (hovered_control == target_button or target_button.is_ancestor_of(hovered_control)),
			"The docked graph receives hover on the %s choice." % kind):
		return false
	if kind == &"preload_resource" and OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-script-drop-picker.png")
	_os_order_button(graph_window, point, true)
	_os_order_button(graph_window, point, false)
	await _frames(3)
	return _require(not is_instance_valid(overlay), "The picker closes after choosing %s." % kind)

## Send the FileSystem payload from its dock to the graph in the same native window.
func _drop_file_from_native_dock(graph_window: Window, canvas: GraphEdit, asset_path: String) -> bool:
	var source_docks := EditorInterface.get_base_control().find_children("*", "FileSystemDock", true, false)
	if not _require(source_docks.size() == 1, "Find the source FileSystem dock in the host editor window."):
		return false
	var source_dock := source_docks.front() as Control
	if not _require(source_dock != null and source_dock.get_window() == graph_window and source_dock.is_visible_in_tree(),
			"The FileSystem dock stays visible in the same window as the workspace."):
		return false
	EditorInterface.select_file(asset_path)
	await _frames(2)
	var origin := Vector2.INF
	for file_tree: Tree in source_dock.find_children("*", "Tree", true, false):
		if not file_tree.is_visible_in_tree():
			continue
		var row := file_tree.get_selected()
		if row != null and row.get_metadata(0) == asset_path:
			file_tree.scroll_to_item(row)
			origin = _tree_row_point(file_tree, row)
			break
	if not origin.is_finite():
		for files: ItemList in source_dock.find_children("*", "ItemList", true, false):
			if not files.is_visible_in_tree():
				continue
			for file_index: int in files.get_selected_items():
				if files.get_item_metadata(file_index) == asset_path:
					files.ensure_current_is_visible()
					await _frames(1)
					origin = files.get_global_transform_with_canvas() * files.get_item_rect(file_index).get_center()
					break
	if not _require(origin.is_finite(), "The FileSystem dock exposes the selected file's actual drag row."):
		return false
	var target := await _native_canvas_drop_point(graph_window, canvas)
	var payload: Variant = await _native_dock_drag(graph_window, origin, target)
	if not _require(payload is Dictionary and payload.get("type") == "files" and
			payload.get("files") == PackedStringArray([asset_path]), "Godot's FileSystem dock issues the selected file payload."):
		return false
	return _require(graph_window.gui_is_drag_successful(), "The FileSystem payload reached the graph canvas for '%s'." % asset_path)

## Find a blank graph client position that Windows and Godot both route to the canvas.
func _native_canvas_drop_point(graph_window: Window, canvas: GraphEdit) -> Vector2:
	var ownership_notes := ""
	for blank: Vector2 in _blank_canvas_points(canvas):
		var candidate := canvas.get_global_transform_with_canvas() * blank
		var screen_point := graph_window.position + Vector2i(graph_window.get_final_transform() * candidate)
		var owner_id := DisplayServer.get_window_at_screen_position(screen_point)
		if owner_id != graph_window.get_window_id():
			ownership_notes = "Expected %s but destination (%s) is in %s." % [graph_window.get_window_id(), screen_point, owner_id]
			continue
		graph_window.warp_mouse(candidate)
		for attempt: int in range(5):
			await _frames(1)
			_os_order_motion(graph_window, candidate, candidate, false)
			if graph_window.gui_get_hovered_control() == canvas:
				return candidate
		ownership_notes = "Godot hover: %s at %s." % [graph_window.gui_get_hovered_control(), candidate]
	_require(false, "Find an unobstructed blank region in the main window's graph canvas. " + ownership_notes)
	return Vector2.INF

## Drag a node row from the built-in Scene dock and check the attachment-relative typed reference.
func _check_scene_dock_drop(graph_window: Window, canvas: GraphEdit, graph_script: Script) -> bool:
	var edited_scene := EditorInterface.get_edited_scene_root()
	if not _require(edited_scene != null and edited_scene.scene_file_path == ICON_SCENE_PATH,
			"The generated scene stays open in the workspace."):
		return false
	var source_node := edited_scene.get_node_or_null(^"GDScriptOwner")
	var script_owner := edited_scene.get_node_or_null(^"GraphOwner")
	if not _require(source_node != null and script_owner != null, "Resolve the source and graph attachment in the generated scene."):
		return false
	var scene_trees := EditorInterface.get_base_control().find_children("*", "SceneTreeEditor", true, false)
	var scene_tree: Tree
	var source_row: TreeItem
	for scene_tree_editor: Control in scene_trees:
		if not scene_tree_editor.is_visible_in_tree() or _workspace().is_ancestor_of(scene_tree_editor):
			continue
		var matching_trees := scene_tree_editor.find_children("*", "Tree", true, false)
		if matching_trees.is_empty():
			continue
		var tree := matching_trees.front() as Tree
		if tree == null or tree.get_root() == null:
			continue
		for row: TreeItem in tree.get_root().get_children():
			if row.get_metadata(0) == source_node.get_path():
				scene_tree = tree
				source_row = row
				break
		if source_row != null:
			break
	if not _require(scene_tree != null and source_row != null and scene_tree.get_window() == graph_window and
			_workspace().find_children("*", "SceneTreeEditor", true, false).is_empty(),
			"The node is available in Godot's Scene dock while the graph has no Scene Tree editor."):
		return false
	var target := await _native_canvas_drop_point(graph_window, canvas)
	if not target.is_finite():
		return false
	var origin := _tree_row_point(scene_tree, source_row)
	if not _require(origin.is_finite(),
			"Find the node name row rather than its icon buttons."):
		return false
	var before: Dictionary = graph_script.call(&"get_graph")
	var payload: Variant = await _native_dock_drag(graph_window, origin, target)
	if not _require(payload is Dictionary and payload.get("type") == "nodes" and payload.get("scene_root") == edited_scene and
			payload.get("nodes") == [source_node.get_path()], "The built-in Scene dock starts a drag with the exact selected node path."):
		return false
	var after: Dictionary = graph_script.call(&"get_graph")
	var last_node: Dictionary = after.nodes.back()
	if not _require(graph_window.gui_is_drag_successful() and after.nodes.size() == before.nodes.size() + 1 and
			last_node.get("kind") == &"scene_node" and last_node.get("source") == script_owner.get_path_to(source_node) and
			last_node.get("class") == source_node.get_class() and last_node.get("script_path") == ICON_SCRIPT_PATH,
			"The drop inserts one typed reference relative to the script owner (success %s, count %d, node %s)." %
			[graph_window.gui_is_drag_successful(), after.nodes.size() - before.nodes.size(), last_node]):
		return false
	print("CRIMSON_NATIVE_SCENE_DOCK_DROP_PASSED")
	return true

## Get a native Tree row's client-space point away from stock quick-action buttons.
func _tree_row_point(tree: Tree, row: TreeItem) -> Vector2:
	var body := tree.get_item_area_rect(row, 0)
	var row_point := Vector2(body.position.x + minf(62, body.size.x * 0.5), body.get_center().y)
	if tree.get_item_at_position(row_point) != row or tree.get_button_id_at_position(row_point) != -1:
		return Vector2.INF
	return tree.get_global_transform_with_canvas() * row_point

## Let the dock produce its own drag payload through stock pointer events.
func _native_dock_drag(graph_window: Window, origin: Vector2, target: Vector2) -> Variant:
	if not origin.is_finite() or not target.is_finite():
		return null
	graph_window.warp_mouse(origin)
	await _frames(1)
	_os_order_motion(graph_window, origin, origin, false)
	_os_order_button(graph_window, origin, true)
	var preview_point := origin.move_toward(target, 24.0)
	_os_order_motion(graph_window, origin, preview_point, true)
	var payload: Variant = graph_window.gui_get_drag_data()
	if payload == null:
		_os_order_button(graph_window, preview_point, false)
		return null
	graph_window.warp_mouse(target)
	await _frames(1)
	_os_order_motion(graph_window, preview_point, target, true)
	_os_order_button(graph_window, target, false)
	await _frames(3)
	return payload

## Use the mouse to page through a long result list without choosing a node.
func _check_picker_rail(graph_window: Window, popup: Window) -> bool:
	var scrollbar := popup.find_child("GraphPaletteScrollBar", true, false) as VScrollBar
	if not _require(scrollbar != null and scrollbar.is_visible_in_tree() and scrollbar.page > 0 and scrollbar.page < scrollbar.max_value,
			"A long search uses a visible scrollbar in the native picker."):
		return false
	scrollbar.value = scrollbar.min_value
	var rail_point := scrollbar.get_global_transform_with_canvas() * (scrollbar.size * Vector2(0.5, 0.92))
	var target_window: Window = graph_window if popup.is_embedded() else popup
	var target_point := rail_point + Vector2(popup.position) if popup.is_embedded() else rail_point
	target_window.warp_mouse(target_point)
	await _frames(1)
	_os_order_button(target_window, target_point, true)
	_os_order_button(target_window, target_point, false)
	await _frames(3)
	return _require(popup.visible and scrollbar.value > scrollbar.min_value,
			"Clicking the scrollbar rail pages the results while the picker stays open.")

## Retain the pending socket through picker navigation and clear it on cancellation.
func _check_wire_picker_lifecycle(graph_window: Window, canvas: GraphEdit, graph_asset: Script) -> bool:
	var origin_card := canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var picker := graph_window.find_child("GraphNodePalette", true, false) as PopupPanel
	var picker_search := graph_window.find_child("GraphPaletteSearch", true, false) as LineEdit
	if not _require(origin_card != null and picker != null and picker_search != null, "Find the wire source and picker."):
		return false
	var origin := _socket(canvas, origin_card, true)
	var blank := _blank_canvas_point(canvas)
	var drop := canvas.get_global_transform_with_canvas() * blank
	var before: Dictionary = graph_asset.call(&"get_graph")
	_button(graph_window, origin, true)
	_motion(graph_window, origin, drop)
	await _frames(1)
	_button(graph_window, drop, false)
	await _frames(3)
	if not _require(picker.visible and picker.gui_get_focus_owner() == picker_search and graph_asset.call(&"get_graph") == before,
			"Releasing a dragged wire opens and focuses the picker without changing the graph."):
		return false
	picker_search.text = "Branch"
	picker_search.text_changed.emit(picker_search.text)
	await _frames(2)
	if OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-wire-picker.png")
	picker.hide()
	await _frames(2)
	if not _require(not picker.visible and graph_window.gui_get_focus_owner() == canvas and graph_asset.call(&"get_graph") == before,
			"Closing a wire picker restores canvas focus and leaves the graph intact."):
		return false
	if OS.get_environment("CRIMSON_NATIVE_FOCUS_CAPTURE") == "1":
		_capture(graph_window, "native-wire-picker-closed.png")
	canvas.popup_request.emit(blank)
	picker_search.text = "Branch"
	picker_search.text_changed.emit(picker_search.text)
	picker_search.text_submitted.emit(picker_search.text)
	await _frames(2)
	var after: Dictionary = graph_asset.call(&"get_graph")
	return _require(after.nodes.size() == before.nodes.size() + 1 and _edge_count(graph_asset) == 0,
			"A later ordinary picker inserts a node without reconnecting the cancelled source.")

## Keep synthetic drops away from the fixture cards as their intrinsic control sizes change.
func _blank_canvas_point(canvas: GraphEdit) -> Vector2:
	var candidates := _blank_canvas_points(canvas)
	return candidates[0] if not candidates.is_empty() else canvas.size * Vector2(0.5, 0.9)

## Enumerate empty canvas positions so the native test can avoid other desktop windows.
func _blank_canvas_points(canvas: GraphEdit) -> Array[Vector2]:
	var candidates: Array[Vector2] = []
	for across: float in [0.82, 0.66, 0.50, 0.33, 0.18]:
		for down: float in [0.67, 0.74, 0.59, 0.82, 0.5, 0.42, 0.34, 0.26]:
			var candidate := canvas.size * Vector2(across, down)
			var covered := false
			for item: Node in canvas.get_children():
				var card := item as GraphNode
				if card != null and Rect2(card.position, card.size * canvas.zoom).grow(20.0).has_point(candidate):
					covered = true
					break
			if not covered:
				candidates.append(candidate)
	return candidates

## A Space repeat sent before a focus transfer must not turn plain selection into canvas panning.
func _check_marquee_after_space_focus_loss(graph_window: Window, companion: Window, canvas: GraphEdit) -> bool:
	var target_card := canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	if not _require(target_card != null, "Find the fixture node for box selection."):
		return false
	canvas.set_selected(null)
	canvas.grab_focus()
	await _frames(1)
	var space_repeat := InputEventKey.new()
	space_repeat.keycode = KEY_SPACE
	space_repeat.physical_keycode = KEY_SPACE
	space_repeat.pressed = true
	space_repeat.echo = true
	# Native Window dispatches this signal and then delivers the same event to its viewport.
	graph_window.window_input.emit(space_repeat)
	graph_window.push_input(space_repeat, true)
	companion.grab_focus()
	if not await _wait_for_focus(companion, graph_window):
		return false
	graph_window.grab_focus()
	if not await _wait_for_focus(graph_window, companion):
		return false
	# The old owner never received the release while the companion had focus.
	var bounds := Rect2(target_card.position, target_card.size * canvas.zoom)
	var canvas_to_window := canvas.get_global_transform_with_canvas()
	var origin := canvas_to_window * (bounds.position - Vector2(18, 18))
	var finish := canvas_to_window * (bounds.end + Vector2(18, 18))
	var previous_scroll := canvas.scroll_offset
	_button(graph_window, origin, true)
	_motion(graph_window, origin, finish)
	_button(graph_window, finish, false)
	await _frames(3)
	if not _require(target_card.selected and canvas.scroll_offset.is_equal_approx(previous_scroll),
			"A plain drag after losing the Space release still box-selects and does not pan."):
		return false
	print("CRIMSON_NATIVE_FOCUS_MARQUEE_PASSED")
	return true

## Interrupt native middle-button panning and deliver the first field click after focus returns.
func _check_middle_pan_focus_loss(graph_window: Window, companion: Window, canvas: GraphEdit) -> bool:
	canvas.scroll_offset = Vector2(120, 32)
	await _frames(2)
	var previous_scroll: Vector2 = canvas.scroll_offset
	var pan_origin: Vector2 = canvas.get_global_transform_with_canvas() * _blank_canvas_point(canvas)
	var pan_target: Vector2 = pan_origin + Vector2(24, 10)
	_button(graph_window, pan_origin, true, MOUSE_BUTTON_MIDDLE)
	_motion(graph_window, pan_origin, pan_target, MOUSE_BUTTON_MASK_MIDDLE)
	await _frames(1)
	if not _require(canvas.scroll_offset.distance_to(previous_scroll) > 2.0, "Begin a real middle-button pan."):
		return false
	companion.grab_focus()
	if not await _wait_for_focus(companion, graph_window):
		return false
	graph_window.grab_focus()
	if not await _wait_for_focus(graph_window, companion):
		return false
	_button(graph_window, pan_target, false, MOUSE_BUTTON_MIDDLE)
	var receiver_card: GraphNode = canvas.get_node_or_null(^"GraphNode_2") as GraphNode
	var message_field: LineEdit = receiver_card.find_child("PrintMessage", true, false) as LineEdit if receiver_card else null
	if not _require(message_field != null, "Find the inline field after interrupted panning."):
		return false
	var field_center: Vector2 = message_field.get_global_transform_with_canvas() * (message_field.size * 0.5)
	var settled_scroll: Vector2 = canvas.scroll_offset
	_button(graph_window, field_center, true)
	_button(graph_window, field_center, false)
	await _frames(1)
	if not _require(message_field.has_focus() and canvas.scroll_offset.is_equal_approx(settled_scroll),
			"The first inline-field click after focus returns does not pan or land on the canvas."):
		return false
	canvas.scroll_offset = Vector2.ZERO
	await _frames(2)
	print("CRIMSON_NATIVE_FOCUS_MIDDLE_PAN_PASSED")
	return true

## Save optional rendered evidence into the staged project.
func _capture(graph_window: Window, filename: String) -> void:
	var graph_image := graph_window.get_texture().get_image()
	if graph_image != null:
		var output := ProjectSettings.globalize_path("user://" + filename)
		if graph_image.save_png(output) == OK:
			print("CRIMSON_NATIVE_FOCUS_CAPTURE ", output)

## Count native GraphEdit callbacks rather than inferring a drag from mouse position.
func _record_wire_start(_widget: StringName, _port: int, _outgoing: bool) -> void:
	_wire_starts += 1

## Track cancellation before any late mouse release is delivered.
func _record_wire_end() -> void:
	_wire_ends += 1

## Track the Windows focus-out signal received by the graph window.
func _record_lost() -> void:
	_graph_focus_lost += 1

## Track the Windows focus-in signal received by the graph window.
func _record_returned() -> void:
	_graph_focus_returned += 1

## Wait for the native window messages and fail if the desktop rejects the focus request.
func _wait_for_focus(expected: Window, previous: Window) -> bool:
	for _tick: int in range(90):
		await get_tree().process_frame
		if expected.has_focus() and not previous.has_focus():
			return true
	return _require(false, "Windows did not transfer native focus to " + expected.title)

## Wait for a graph rebuild or deferred wire draw.
func _frames(count: int) -> void:
	for _frame_index: int in range(count):
		await get_tree().process_frame

## Project the execution socket to the tool window's viewport coordinates.
func _socket(canvas: GraphEdit, widget: GraphNode, outgoing: bool) -> Vector2:
	var port_point := widget.get_output_port_position(0) if outgoing else widget.get_input_port_position(0)
	return canvas.get_global_transform_with_canvas() * (widget.position + port_point * canvas.zoom)

## Send a mouse button event through the native graph window's viewport.
func _button(graph_window: Window, point: Vector2, down: bool, mouse_button: MouseButton = MOUSE_BUTTON_LEFT) -> void:
	var item := InputEventMouseButton.new()
	item.position = point
	item.global_position = point
	item.button_index = mouse_button
	var held_mask: MouseButtonMask = MOUSE_BUTTON_MASK_MIDDLE if mouse_button == MOUSE_BUTTON_MIDDLE else MOUSE_BUTTON_MASK_LEFT
	item.button_mask = held_mask if down else 0
	item.pressed = down
	graph_window.push_input(item, true)

## Pass a client-space click through the display server's event dispatch, after native focus has transferred.
func _os_order_button(graph_window: Window, point: Vector2, down: bool) -> void:
	var item := InputEventMouseButton.new()
	var client_point := graph_window.get_final_transform() * point
	item.position = client_point
	item.global_position = client_point
	item.window_id = graph_window.get_window_id()
	item.button_index = MOUSE_BUTTON_LEFT
	item.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
	item.pressed = down
	Input.parse_input_event(item)
	Input.flush_buffered_events()

## Send motion at a client-space position with the original pointer button state.
func _os_order_motion(graph_window: Window, origin: Vector2, point: Vector2, held: bool) -> void:
	var item := InputEventMouseMotion.new()
	var client_transform := graph_window.get_final_transform()
	item.position = client_transform * point
	item.global_position = item.position
	item.relative = client_transform.basis_xform(point - origin)
	item.window_id = graph_window.get_window_id()
	item.button_mask = MOUSE_BUTTON_MASK_LEFT if held else 0
	Input.parse_input_event(item)
	Input.flush_buffered_events()

## Move a held pointer from its starting socket to the compatible target.
func _motion(graph_window: Window, origin: Vector2, point: Vector2, held_mask: MouseButtonMask = MOUSE_BUTTON_MASK_LEFT) -> void:
	var item := InputEventMouseMotion.new()
	item.position = point
	item.global_position = point
	item.button_mask = held_mask
	item.relative = point - origin
	graph_window.push_input(item, true)

## Observe whether either release committed a graph connection.
func _edge_count(graph_asset: Script) -> int:
	var document: Dictionary = graph_asset.call(&"get_graph")
	var edges: Array = document.get("connections", [])
	return edges.size()

## Fail the isolated editor process with the condition that did not hold.
func _require(condition: bool, requirement: String) -> bool:
	if not condition:
		push_error("[Crimson native focus] " + requirement)
		get_tree().quit(1)
	return condition
