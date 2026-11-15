# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const GRAPH_PATH: String = "res://gameplay_tag_pin_fixture.crimsonscript"
const STUN: StringName = &"state.control.stun"
const SPRINT: StringName = &"movement.sprint"
const PICKER_POINT: Vector2 = Vector2(60, 80)

## The fixture's standalone graph asset.
var authored_script: Script
## The workbench canvas, and its native node-palette controls.
var canvas: GraphEdit
var palette_search: LineEdit
var palette_results: Tree
## The catalog window shared by the Inspector and graph.
var tag_picker: Window
var tag_tree: Tree


## Wait for the editor to load its extensions and native workspaces.
func _enter_tree() -> void:
	_verify.call_deferred()


## Run the real graph palette and the real Gameplay Tags picker.
func _verify() -> void:
	await _settle()
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	for tag_path: StringName in [STUN, SPRINT]:
		if not _need(GameplayTagCatalog.add_project_tag(tag_path) == OK, "Declare independent catalog tags."):
			return
	var draft := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(draft != null and ResourceSaver.save(draft, GRAPH_PATH) == OK, "Save an isolated graph."):
		return
	authored_script = ResourceLoader.load(GRAPH_PATH, "Script") as Script
	EditorInterface.edit_script(authored_script, -1, 0, true)
	await _settle()
	var workspace := get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and workspace.is_visible_in_tree(), "Open the editable graph workspace."):
		return
	canvas = workspace.find_child("GraphCanvas", true, false) as GraphEdit
	palette_search = workspace.find_child("GraphPaletteSearch", true, false) as LineEdit
	palette_results = workspace.find_child("GraphPaletteResults", true, false) as Tree
	tag_picker = get_tree().root.find_child("GameplayTagPicker", true, false) as Window
	if not _need(canvas != null and palette_search != null and palette_results != null and tag_picker != null,
			"Find the native graph controls and shared Gameplay Tags picker."):
		return
	tag_tree = tag_picker.find_child("GameplayTagTree", true, false) as Tree
	if not _need(tag_tree != null, "Find the picker tree."):
		return
	if not _verify_category() or not await _verify_literal_defaults():
		return
	print("CRIMSON_VISUAL_SCRIPT_GAMEPLAY_TAG_PICKER_PASSED")
	get_tree().quit()


## Check that the free palette groups declared tag APIs and keeps inherited APIs out.
func _verify_category() -> bool:
	canvas.popup_request.emit(PICKER_POINT)
	_search("Gameplay Tags")
	var category := _child_named(palette_results.get_root(), "Gameplay Tags")
	if not _need(category != null, "Find the top-level Gameplay Tags category."):
		return false
	var expected_owners := {
		"Tag": &"GameplayTag",
		"Container": &"GameplayTagContainer",
		"Catalog": &"GameplayTagCatalog",
		"Query": &"GameplayTagQuery",
	}
	for scope: String in expected_owners:
		var group := _child_named(category, scope)
		if not _need(group != null and group.get_first_child() != null, "List declared methods under " + scope + "."):
			return false
		for option: TreeItem in group.get_children():
			var entry: Dictionary = option.get_metadata(0)
			var native_method: Dictionary = entry.get("member", {})
			if not _need(native_method.get("class") == expected_owners[scope], "Exclude inherited methods from " + scope + "."):
				return false
	return _need(_child_named(_child_named(category, "Tag"), "Find Tag") != null, "Expose a discoverable Find Tag node.")


## Author defaults for raw name, tag Resource, container Resource, and typed tag array inputs.
func _verify_literal_defaults() -> bool:
	var find_id := await _insert_method(&"GameplayTag", &"find")
	if not _need(find_id > 0, "Insert Find Tag through the palette."):
		return false
	if not await _choose(find_id, &"arg_0", [STUN]) or not _need(_stored(find_id, &"arg_0") == STUN,
			"Find Tag stores the selected name as its normal literal."):
		return false
	if not await _choose(find_id, &"arg_0", [SPRINT], false) or not _need(_stored(find_id, &"arg_0") == STUN,
			"Cancelling the shared picker preserves the previous literal."):
		return false
	if not await _choose(find_id, &"arg_0", [SPRINT]) or not _need(_stored(find_id, &"arg_0") == SPRINT,
			"Apply a second value to the same node field."):
		return false
	await _shortcut(KEY_Z)
	if not _need(_stored(find_id, &"arg_0") == STUN, "Each Apply gets a separate graph Undo step."):
		return false
	await _shortcut(KEY_Y)
	if not _need(_stored(find_id, &"arg_0") == SPRINT, "Graph Redo restores only the last applied tag."):
		return false
	var match_id := await _insert_method(&"GameplayTag", &"matches")
	if not _need(match_id > 0, "Insert a node with a typed GameplayTag input."):
		return false
	if not await _choose(match_id, &"arg_0", [STUN]) or not _need(_stored(match_id, &"arg_0") == STUN,
			"A typed GameplayTag input saves an inline path."):
		return false
	var query_id := await _insert_method(&"GameplayTagQuery", &"all_tags")
	if not _need(query_id > 0, "Insert a node with a typed GameplayTagContainer input."):
		return false
	if not await _choose(query_id, &"arg_0", [STUN, SPRINT]) or not _need(
			_stored(query_id, &"arg_0") == PackedStringArray([STUN, SPRINT]),
			"A container input saves only the explicitly selected paths."):
		return false
	var array_id := await _insert_method(&"GameplayTagContainer", &"from_tags")
	if not _need(array_id > 0, "Insert a node with a typed tag Array input."):
		return false
	if not await _choose(array_id, &"arg_0", []) or not _need(
			_stored(array_id, &"arg_0") == PackedStringArray([]), "Apply can create an explicitly empty typed tag collection."):
		return false
	if not await _choose(array_id, &"arg_0", [SPRINT]) or not _need(
			_stored(array_id, &"arg_0") == PackedStringArray([SPRINT]), "A typed tag Array uses the shared picker."):
		return false
	return await _verify_wired_default(find_id, match_id)


## Check an authored path stays saved when a compatible wire takes over the input.
func _verify_wired_default(find_id: int, match_id: int) -> bool:
	var source_card := _card(find_id)
	var target_card := _card(match_id)
	var source_port := _port(source_card, &"result", true)
	var destination_port := _port(target_card, &"arg_0", false)
	if not _need(source_port >= 0 and destination_port >= 0, "Find the typed source and destination data sockets."):
		return false
	canvas.connection_to_empty.emit(source_card.name, source_port, PICKER_POINT)
	_search("GameplayTag.matches")
	var selected_option := palette_results.get_selected()
	var selected_entry: Dictionary = selected_option.get_metadata(0) if selected_option != null else {}
	var selected_method: Dictionary = selected_entry.get("member", {})
	if not _need(selected_method.get("class") == &"GameplayTag" and selected_method.get("name") == &"matches",
			"Show compatible Gameplay Tags methods when dragging from a tag output."):
		return false
	var palette_panel := get_tree().root.find_child("GraphNodePalette", true, false) as PopupPanel
	if not _need(palette_panel != null, "Find the contextual node palette window."):
		return false
	palette_panel.hide()
	canvas.connection_request.emit(source_card.name, source_port, target_card.name, destination_port)
	await _settle()
	return _need(_card(match_id).find_child("Literal_arg_0", true, false) == null and _stored(match_id, &"arg_0") == STUN,
			"A wired input hides its picker and keeps its saved fallback.")


## Drive one unwired graph input through the shared chooser.
func _choose(node_id: int, pin_id: StringName, paths: Array[StringName], apply_choice: bool = true) -> bool:
	var card := _card(node_id)
	var button := card.find_child("Literal_" + String(pin_id), true, false) as Button if card != null else null
	if not _need(button != null, "An unwired tag input renders its picker button."):
		return false
	button.grab_focus()
	button.pressed.emit()
	await _settle()
	if not _need(tag_picker.visible, "The graph button opens the shared Gameplay Tags picker."):
		return false
	for tag_path: StringName in paths:
		var tag_item := _tag_item(tag_tree.get_root(), tag_path)
		if not _need(tag_item != null, "Find the catalog entry by path."):
			return false
		tag_item.select(0)
		tag_tree.item_activated.emit()
	var action_name := "GameplayTagApply" if apply_choice else "GameplayTagCancel"
	var action_button := tag_picker.find_child(action_name, true, false) as Button
	if not _need(action_button != null, "Find the picker action."):
		return false
	action_button.pressed.emit()
	await _settle()
	return _need(not tag_picker.visible, "Close the picker when the graph accepts or cancels the edit.")


## Add one real native method rather than constructing its signature in the fixture.
func _insert_method(native_owner: StringName, method_name: StringName) -> int:
	canvas.popup_request.emit(PICKER_POINT)
	var qualified := String(native_owner) + "." + String(method_name)
	_search(qualified)
	var selection := palette_results.get_selected()
	var entry: Dictionary = selection.get_metadata(0) if selection != null else {}
	var native_method: Dictionary = entry.get("member", {})
	if not _need(native_method.get("class") == native_owner and native_method.get("name") == method_name,
			"Search finds " + qualified + "."):
		return -1
	palette_search.text_submitted.emit(qualified)
	await _settle()
	var graph_data: Dictionary = authored_script.call(&"get_graph")
	for node_record: Dictionary in graph_data.nodes:
		var method_record: Dictionary = node_record.get("member", {})
		if method_record.get("class") == native_owner and method_record.get("name") == method_name:
			return node_record.id
	return -1


## Read the saved default from the graph document.
func _stored(node_id: int, pin_id: StringName) -> Variant:
	var graph_data: Dictionary = authored_script.call(&"get_graph")
	for node_record: Dictionary in graph_data.nodes:
		if node_record.id == node_id:
			return node_record.get("values", {}).get(pin_id)
	return null


## Locate one GraphNode by persisted identity.
func _card(node_id: int) -> GraphNode:
	return canvas.get_node_or_null("GraphNode_%d" % node_id) as GraphNode


## Resolve the displayed port by its native descriptor's ID.
func _port(card: GraphNode, pin_id: StringName, outgoing: bool) -> int:
	var count: int = card.get_output_port_count() if outgoing else card.get_input_port_count()
	for port_index: int in range(count):
		var slot: int = card.get_output_port_slot(port_index) if outgoing else card.get_input_port_slot(port_index)
		var descriptor: Dictionary = card.get_slot_metadata_right(slot) if outgoing else card.get_slot_metadata_left(slot)
		if descriptor.get("id") == pin_id:
			return port_index
	return -1


## Filter the actual node palette.
func _search(query: String) -> void:
	palette_search.text = query
	palette_search.text_changed.emit(query)


## Find an immediate category or action in the native palette.
func _child_named(parent_item: TreeItem, caption: String) -> TreeItem:
	for entry: TreeItem in parent_item.get_children():
		if entry.get_text(0) == caption:
			return entry
	return null


## Match stable tag path metadata across expanded and collapsed branches.
func _tag_item(parent_item: TreeItem, tag_path: StringName) -> TreeItem:
	for entry: TreeItem in parent_item.get_children():
		if entry.get_metadata(0) == String(tag_path):
			return entry
		var nested := _tag_item(entry, tag_path)
		if nested != null:
			return nested
	return null


## Allow deferred editor notifications and graph layout to complete.
func _settle() -> void:
	await get_tree().process_frame
	await get_tree().process_frame


## Exercise the graph's ordinary keyboard history while the canvas owns focus.
func _shortcut(code: Key) -> void:
	canvas.grab_focus()
	var key_event := InputEventKey.new()
	key_event.keycode = code
	key_event.ctrl_pressed = true
	key_event.pressed = true
	canvas.get_window().push_input(key_event, true)
	await _settle()


## Report the exact failed editor action to the harness.
func _need(condition: bool, description: String) -> bool:
	if not condition:
		push_error("Gameplay Tags graph editor fixture: " + description)
		get_tree().quit(1)
	return condition
