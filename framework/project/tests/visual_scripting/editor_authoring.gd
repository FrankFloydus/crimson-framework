# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const SCRIPT_PATH := "res://authoring_probe.crimsonscript"
var workspace: Control
var outline_tree: Tree
var details_pane: Control
var graph_script: Script
## The active matrix case and step are printed with any failed assertion.
var matrix_context: String = ""

## Let the isolated editor finish plugin initialization before opening a graph.
func _enter_tree() -> void:
	_verify.call_deferred()

## Report assertion failures to the PowerShell editor harness.
func _need(condition: bool, label: String) -> bool:
	if not condition:
		if matrix_context.is_empty():
			push_error("Authoring probe: " + label)
			get_tree().quit(1)
		else:
			print("COLLECTION_MATRIX_FAIL ", matrix_context, " ", label)
	return condition

## Find a declaration tree row by the text the author sees.
func _find_item(item: TreeItem, label: String) -> TreeItem:
	if item.get_text(0) == label:
		return item
	var child_item := item.get_first_child()
	while child_item != null:
		var match_item := _find_item(child_item, label)
		if match_item != null:
			return match_item
		child_item = child_item.get_next()
	return null

## Submit a Details text field through the same signal as Enter.
func _edit_text(field_name: String, text_value: String) -> bool:
	var text_field := details_pane.find_child(field_name, true, false) as LineEdit
	if not _need(text_field != null, "Missing field " + field_name):
		return false
	text_field.text = text_value
	text_field.text_submitted.emit(text_value)
	await get_tree().process_frame
	return true

## Type into a real LineEdit and leave the text pending until the test presses Enter.
func _type_line_draft(field: LineEdit, value_text: String) -> bool:
	if not _need(field != null and field.is_visible_in_tree(), "Find the editable row field."):
		return false
	field.edit()
	field.select_all()
	for index: int in range(value_text.length()):
		var letter := InputEventKey.new()
		letter.pressed = true
		letter.unicode = value_text.unicode_at(index)
		workspace.get_window().push_input(letter, true)
	await get_tree().process_frame
	return _need(field.text == value_text, "Focused text input enters the requested draft.")

## Submit text using the engine's normal Enter route while the field owns focus.
func _submit_line_draft(field: LineEdit) -> bool:
	if not _need(field != null and field.is_visible_in_tree(), "Find the row field to submit."):
		return false
	field.edit()
	var confirm := InputEventKey.new()
	confirm.pressed = true
	confirm.keycode = KEY_ENTER
	confirm.physical_keycode = KEY_ENTER
	workspace.get_window().push_input(confirm, true)
	await get_tree().process_frame
	return true

## Activate a Details action without relying on its viewport position.
func _press(field_name: String) -> bool:
	var button_field := details_pane.find_child(field_name, true, false) as Button
	if not _need(button_field != null, "Missing button " + field_name):
		return false
	button_field.pressed.emit()
	await get_tree().process_frame
	return true

## Open the actual shared Select and look up an enabled option by label.
func _open_select(field: Control, option_text: String) -> Button:
	var anchor := field.find_child("SelectTrigger", true, false) as Button
	var popup := field.find_child("SelectPopup", true, false) as PopupPanel
	if not _need(anchor != null and popup != null and not anchor.disabled, "Find the shared Select trigger and popup."):
		return null
	anchor.pressed.emit()
	await get_tree().process_frame
	for candidate: Node in popup.find_children("*", "Button", true, false):
		var option := candidate as Button
		if option.text == option_text and option.visible and not option.disabled:
			return option
	_need(false, "Missing select option " + option_text)
	return null

## Find generated collection rows without relying on sibling names or geometry.
func _collection_rows() -> Array[PanelContainer]:
	var collection := details_pane.find_child("VisualScriptVariableDefault", true, false) as Control
	var rows: Array[PanelContainer] = []
	if collection != null:
		for child: Node in collection.get_children():
			if child is PanelContainer:
				rows.append(child as PanelContainer)
	return rows

## Resolve the current rendered editor for a row index after any graph change.
func _collection_cell(index: int, field_name: String) -> LineEdit:
	var rows := _collection_rows()
	if index >= 0 and index < rows.size():
		return rows[index].find_child(field_name, true, false) as LineEdit
	return null


## Submit a cell in an existing collection row without relying on its layout.
func _edit_collection_cell(row_index: int, field_name: String, value_text: String) -> bool:
	var rows := _collection_rows()
	if not _need(row_index >= 0 and row_index < rows.size(), "Find the collection entry to edit."):
		return false
	var field := rows[row_index].find_child(field_name, true, false) as LineEdit
	if not _need(field != null, "Find the entry's editable value."):
		return false
	field.text = value_text
	field.text_submitted.emit(value_text)
	await get_tree().process_frame
	return true


## Open a Details group before interacting with the controls it owns.
func _expand_details(disclosure_name: String) -> bool:
	var disclosure := details_pane.find_child(disclosure_name, true, false) as Button
	if not _need(disclosure != null and disclosure.is_visible_in_tree(), "Find available section " + disclosure_name):
		return false
	disclosure.button_pressed = true
	await get_tree().process_frame
	return true


## Exercise the native reorder command on the focused row grip.
func _move_row(grip: Button, keycode: Key) -> bool:
	if not _need(grip != null, "Find the reorder grip when multiple rows exist."):
		return false
	grip.grab_focus()
	var key_event := InputEventKey.new()
	key_event.keycode = keycode
	key_event.pressed = true
	key_event.alt_pressed = true
	workspace.get_window().push_input(key_event, true)
	await get_tree().process_frame
	return true

## Find a collection grip by current order rather than rendered screen coordinates.
func _move_collection_row(row_index: int, keycode: Key) -> bool:
	var rows := _collection_rows()
	if not _need(row_index >= 0 and row_index < rows.size(), "Find the collection entry to reorder."):
		return false
	return await _move_row(rows[row_index].find_child("VisualScriptReorderCollectionEntry", true, false) as Button, keycode)

## Drag a collection or enum grip onto another row through the editor viewport.
func _drag_authored_row(grip: Button, target: Control, place_after: bool) -> bool:
	if not _need(grip != null and target != null, "Find both native reorder controls."):
		return false
	await get_tree().process_frame
	var clipping_control := target.get_parent()
	while clipping_control != null and not clipping_control is ScrollContainer:
		clipping_control = clipping_control.get_parent()
	var scroll := clipping_control as ScrollContainer
	if not _need(scroll != null and scroll.is_ancestor_of(grip), "Find the common Details scroll area."):
		return false
	var visible_area := scroll.get_global_rect().intersection(workspace.get_global_rect())
	var origin := grip.get_global_transform_with_canvas() * (grip.size * 0.5)
	var local_destination := Vector2(target.size.x * 0.5, target.size.y * (0.8 if place_after else 0.2))
	var destination := target.get_global_transform_with_canvas() * local_destination
	# Offset the view enough to include both points without requiring a fixed pane height.
	var lower_scroll := ceili(maxf(origin.y, destination.y) - visible_area.end.y) + 1
	var upper_scroll := floori(minf(origin.y, destination.y) - visible_area.position.y)
	if lower_scroll <= upper_scroll:
		scroll.scroll_vertical += clampi(0, lower_scroll, upper_scroll)
	else:
		scroll.ensure_control_visible(grip)
	await get_tree().process_frame
	origin = grip.get_global_transform_with_canvas() * (grip.size * 0.5)
	if not visible_area.has_point(origin):
		scroll.ensure_control_visible(grip)
		await get_tree().process_frame
		origin = grip.get_global_transform_with_canvas() * (grip.size * 0.5)
	if not _need(visible_area.has_point(origin), "The authored row grip is reachable by scrolling Details."):
		return false
	if not await _start_authored_drag(origin):
		return false
	destination = target.get_global_transform_with_canvas() * local_destination
	if not visible_area.has_point(destination):
		scroll.ensure_control_visible(target)
		await get_tree().process_frame
		destination = target.get_global_transform_with_canvas() * local_destination
	if not _need(visible_area.has_point(destination), "The destination row is reachable by scrolling Details."):
		workspace.get_window().gui_cancel_drag()
		return false
	return await _release_authored_drag(origin, destination)


## Send a left-button drag across the editor viewport using actual control positions.
func _drag_to_point(origin: Vector2, destination: Vector2, duplicate_release: bool = false) -> bool:
	if not await _start_authored_drag(origin):
		return false
	return await _release_authored_drag(origin, destination, duplicate_release)


## Start an actual drag so the scroll position can change before dropping.
func _start_authored_drag(origin: Vector2) -> bool:
	var click := InputEventMouseButton.new()
	click.position = origin
	click.global_position = origin
	click.button_index = MOUSE_BUTTON_LEFT
	click.button_mask = MOUSE_BUTTON_MASK_LEFT
	click.pressed = true
	workspace.get_window().push_input(click, true)
	var tracking := InputEventMouseMotion.new()
	tracking.position = origin + Vector2(24, 0)
	tracking.global_position = tracking.position
	tracking.relative = tracking.position - origin
	tracking.button_mask = MOUSE_BUTTON_MASK_LEFT
	workspace.get_window().push_input(tracking, true)
	await get_tree().process_frame
	return _need(workspace.get_window().gui_is_dragging(), "The authored row grip starts a native drag.")


## Complete the active drag using the measured destination position.
func _release_authored_drag(origin: Vector2, destination: Vector2, duplicate_release: bool = false) -> bool:
	var move := InputEventMouseMotion.new()
	move.position = destination
	move.global_position = destination
	move.relative = destination - (origin + Vector2(24, 0))
	move.button_mask = MOUSE_BUTTON_MASK_LEFT
	workspace.get_window().push_input(move, true)
	await get_tree().process_frame
	var click := InputEventMouseButton.new()
	click.position = destination
	click.global_position = destination
	click.button_index = MOUSE_BUTTON_LEFT
	workspace.get_window().push_input(click, true)
	if duplicate_release:
		workspace.get_window().push_input(click, true)
	await get_tree().process_frame
	await get_tree().process_frame
	return _need(not workspace.get_window().gui_is_dragging(), "The editor finishes the drag after the release.")


## Reorder via the handle and release directly over the destination value editor.
func _drag_collection_row(from_row: int, to_row: int, receiver_name: String = "VisualScriptCollectionValue") -> bool:
	var rows := _collection_rows()
	var grip := rows[from_row].find_child("VisualScriptReorderCollectionEntry", true, false) as Button
	var target := rows[to_row].find_child(receiver_name, true, false) as Control
	return await _drag_authored_row(grip, target, from_row < to_row)


## A drag released in the canvas cancels without applying to the source collection.
func _cancel_collection_drag(from_row: int) -> bool:
	var rows := _collection_rows()
	var grip := rows[from_row].find_child("VisualScriptReorderCollectionEntry", true, false) as Button
	var canvas := workspace.find_child("GraphCanvas", true, false) as GraphEdit
	if not _need(grip != null and canvas != null, "Find the authored grip and the unrelated graph canvas."):
		return false
	var clipping_control := grip.get_parent()
	while clipping_control != null and not clipping_control is ScrollContainer:
		clipping_control = clipping_control.get_parent()
	if not _need(clipping_control is ScrollContainer, "Find the grip's Details scroll area."):
		return false
	(clipping_control as ScrollContainer).ensure_control_visible(grip)
	await get_tree().process_frame
	var origin := grip.get_global_transform_with_canvas() * (grip.size * 0.5)
	return await _drag_to_point(origin, canvas.get_global_rect().get_center(), true)

## Use the add button on a named declaration group.
func _add_declaration(label: String) -> bool:
	var group_item := _find_item(outline_tree.get_root(), label)
	if not _need(group_item != null, "Missing declaration group " + label):
		return false
	for column: int in range(outline_tree.columns):
		if group_item.get_button_by_id(column, 1) >= 0:
			outline_tree.button_clicked.emit(group_item, column, 1, MOUSE_BUTTON_LEFT)
			await get_tree().process_frame
			return true
	return _need(false, "Missing add action in declaration group " + label)

## Select an existing row and wait for Details to replace its controls.
func _select_declaration(label: String) -> bool:
	var listed_item := _find_item(outline_tree.get_root(), label)
	if not _need(listed_item != null, "Missing declaration " + label):
		return false
	listed_item.select(0)
	outline_tree.item_selected.emit()
	await get_tree().process_frame
	return true

## Choose the exact type from the real picker and answer a variable type warning when shown.
func _choose_type(label: String, allow_change: bool = true, check_warning: bool = false,
		trigger_name: String = "VisualScriptDeclarationType") -> bool:
	if not await _press(trigger_name):
		return false
	var filter := workspace.find_child("VisualScriptTypeSearch", true, false) as LineEdit
	var results := workspace.find_child("VisualScriptTypeTree", true, false) as Tree
	if not _need(filter != null and results != null, "Type chooser must expose filter and results"):
		return false
	filter.text = label
	filter.text_changed.emit(label)
	if not _need(results.get_selected() != null and results.get_selected().get_text(0) == label, "Exact type " + label):
		return false
	filter.text_submitted.emit(label)
	await get_tree().process_frame
	return await _answer_type_warning(allow_change, check_warning)


## Answer the real change dialog, optionally requiring one to be shown for this edit.
func _answer_type_warning(allow_change: bool, check_warning: bool) -> bool:
	var warning_panel := workspace.find_child("VisualScriptChangeVariableType", true, false) as Control
	if warning_panel == null or not warning_panel.is_visible_in_tree():
		return _need(not check_warning, "Changing a variable type opens a confirmation.")
	var caption := "Change Type" if allow_change else "Cancel"
	for candidate: Node in warning_panel.find_children("*", "Button", true, false):
		var action := candidate as Button
		if action.text == caption and action.is_visible_in_tree():
			action.pressed.emit()
			await get_tree().process_frame
			return true
	return _need(false, "Confirm or cancel the variable type edit with " + caption + ".")


## Change the variable's container without altering its key or element type choice.
func _choose_container(caption: String) -> bool:
	var picker := details_pane.find_child("VisualScriptVariableContainerPicker", true, false) as Control
	var trigger := picker.find_child("VisualScriptVariableContainer", true, false) as Button if picker != null else null
	var popup := picker.find_child("SelectPopup", true, false) as PopupPanel if picker != null else null
	if not _need(trigger != null and popup != null, "Find the dedicated Container selector."):
		return false
	trigger.pressed.emit()
	await get_tree().process_frame
	for candidate: Node in popup.find_children("*", "Button", true, false):
		var option := candidate as Button
		if option.text == caption and option.is_visible_in_tree() and not option.disabled:
			option.pressed.emit()
			await get_tree().process_frame
			return await _answer_type_warning(true, true)
	return _need(false, "Find the requested Container option " + caption + ".")

## Press a shortcut in the declaration tree through the editor viewport.
func _declaration_key(key_code: Key, with_command: bool = false) -> void:
	outline_tree.grab_focus()
	var shortcut_event := InputEventKey.new()
	shortcut_event.keycode = key_code
	shortcut_event.physical_keycode = key_code
	shortcut_event.ctrl_pressed = with_command
	shortcut_event.pressed = true
	workspace.get_window().push_input(shortcut_event, true)
	await get_tree().process_frame

## Right-click an authored row and pick the named action from the real context menu.
func _declaration_menu_command(label: String, command: String) -> bool:
	var declaration_item := _find_item(outline_tree.get_root(), label)
	if not _need(declaration_item != null, "Missing context row " + label):
		return false
	outline_tree.scroll_to_item(declaration_item, true)
	await get_tree().process_frame
	var content_column := 1 if outline_tree.columns > 1 and not declaration_item.get_tooltip_text(1).is_empty() else 0
	var point := outline_tree.get_global_transform_with_canvas() * outline_tree.get_item_area_rect(declaration_item, content_column).get_center()
	var pointer_event := InputEventMouseButton.new()
	pointer_event.button_index = MOUSE_BUTTON_RIGHT
	pointer_event.position = point
	pointer_event.global_position = point
	pointer_event.pressed = true
	workspace.get_window().push_input(pointer_event, true)
	pointer_event = pointer_event.duplicate() as InputEventMouseButton
	pointer_event.pressed = false
	workspace.get_window().push_input(pointer_event, true)
	await get_tree().process_frame
	var context := workspace.find_child("VisualScriptDeclarationMenu", true, false) as PopupMenu
	if not _need(context != null and context.visible, "Open declaration context menu with right click"):
		return false
	for item_index in context.item_count:
		if context.get_item_text(item_index) == command:
			context.hide()
			context.id_pressed.emit(context.get_item_id(item_index))
			await get_tree().process_frame
			return true
	return _need(false, "Missing context command " + command)

## Exercise declaration descriptions, clipboard, and deletion using public editor controls.
func _verify_declaration_commands(graph_canvas: GraphEdit) -> bool:
	var state: Dictionary = graph_script.call(&"get_graph")
	state["functions"] = [{"id": 9, "name": "run_charge", "inputs": [], "outputs": [], "graph": {"nodes": [
		{"id": 1, "kind": "function_entry", "definition": 9},
		{"id": 2, "kind": "return", "definition": 9},
		{"id": 3, "kind": "call_function", "definition": 9},
		{"id": 4, "kind": "get_variable", "variable": 1, "scope": "local"},
		{"id": 5, "kind": "get_variable", "variable": 1, "scope": "instance"}
	], "connections": [], "locals": [{"id": 1, "name": "local_charge", "pin": {"id": &"value", "label": "Value", "type": TYPE_BOOL}, "value": false}]}}]
	state["events"] = [{"id": 12, "name": "charge_ready", "inputs": [{"id": &"amount", "label": "Amount", "type": TYPE_INT}]}]
	state["nodes"] = [
		{"id": 1, "kind": "ready"}, {"id": 2, "kind": "call_function", "definition": 9},
		{"id": 3, "kind": "custom_event", "definition": 12}, {"id": 4, "kind": "emit_event", "definition": 12},
		{"id": 5, "kind": "get_variable", "variable": 1, "scope": "instance"}, {"id": 6, "kind": "print"}
	]
	state["connections"] = [{"from": 1, "from_pin": &"then", "to": 2, "to_pin": &"in"}]
	graph_script.call(&"set_graph", state)
	await get_tree().process_frame
	if not await _select_declaration("variable") or not await _edit_text("VisualScriptDeclarationDescription", "Tracks the charge state."):
		return false
	if not await _select_declaration("charge_ready") or not await _edit_text("VisualScriptDeclarationDescription", "Runs after the charge is ready."):
		return false
	var getter := graph_canvas.get_node_or_null(^"GraphNode_5") as GraphNode
	var event_entry := graph_canvas.get_node_or_null(^"GraphNode_3") as GraphNode
	var event_emitter := graph_canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	if not _need(getter != null and getter.tooltip_text == "Tracks the charge state." and event_entry != null and event_emitter != null and event_entry.tooltip_text == "Runs after the charge is ready." and event_emitter.tooltip_text == event_entry.tooltip_text, "Descriptions appear verbatim on variable and event cards"):
		return false
	if not await _declaration_menu_command("run_charge", "Rename"):
		return false
	var rename_field := details_pane.find_child("VisualScriptDeclarationName", true, false) as LineEdit
	if not _need(rename_field != null and rename_field.has_focus() and rename_field.get_selected_text() == "run_charge", "Rename focuses the Name field and selects its text"):
		return false
	if not await _edit_text("VisualScriptDeclarationName", "charge_weapon") or not await _edit_text("VisualScriptFunctionDescription", "Charges the active weapon."):
		return false
	if not await _declaration_menu_command("charge_weapon", "Copy"):
		return false
	await _declaration_key(KEY_V, true)
	state = graph_script.call(&"get_graph")
	var copied_function: Dictionary = state.functions[1] if state.functions.size() == 2 else {}
	if not _need(copied_function.get("name") == "charge_weapon_copy" and copied_function.description == "Charges the active weapon." and copied_function.graph.nodes[0].definition == copied_function.id and copied_function.graph.nodes[2].definition == copied_function.id and copied_function.graph.locals[0].name == "local_charge", "Paste copies the function and retargets its boundary and recursive call"):
		return false
	if not await _declaration_menu_command("charge_ready", "Duplicate"):
		return false
	state = graph_script.call(&"get_graph")
	if not _need(state.events.size() == 2 and state.events[1].name == "charge_ready_copy" and state.events[1].inputs == state.events[0].inputs and state.events[1].description == state.events[0].description, "Duplicate preserves an event signature and description"):
		return false
	if not await _declaration_menu_command("charge_ready_copy", "Delete") or not await _declaration_menu_command("charge_ready", "Cut"):
		return false
	state = graph_script.call(&"get_graph")
	if not _need(state.events.is_empty() and state.nodes.all(func(record: Dictionary) -> bool: return record.id != 3 and record.id != 4), "Cut removes the event entry and emit node"):
		return false
	await _declaration_key(KEY_V, true)
	state = graph_script.call(&"get_graph")
	if not _need(state.events.size() == 1 and state.events[0].name == "charge_ready" and state.events[0].description == "Runs after the charge is ready.", "Paste restores the cut declaration and its metadata"):
		return false
	if not await _declaration_menu_command("charge_weapon", "Copy") or not await _select_declaration("variable"):
		return false
	await _declaration_key(KEY_DELETE)
	state = graph_script.call(&"get_graph")
	if not _need(state.variables.size() == 1 and state.nodes.all(func(record: Dictionary) -> bool: return record.id != 5) and state.functions.all(func(declaration: Dictionary) -> bool: return declaration.graph.nodes.size() == 4 and declaration.graph.nodes[3].scope == "local"), "Delete removes instance references from every graph and preserves overlapping local IDs"):
		return false
	state.variables.append({"id": 1, "name": "different_variable", "pin": {"id": &"value", "label": "Value", "type": TYPE_BOOL}, "value": false})
	graph_script.call(&"set_graph", state)
	await get_tree().process_frame
	await _declaration_key(KEY_V, true)
	state = graph_script.call(&"get_graph")
	if not _need(state.functions.size() == 2, "Cannot paste a function after an unrelated variable reuses its deleted dependency ID"):
		return false
	if not await _select_declaration("charge_weapon"):
		return false
	outline_tree.item_activated.emit()
	await get_tree().process_frame
	if not await _select_declaration("local_charge"):
		return false
	await _declaration_key(KEY_DELETE)
	state = graph_script.call(&"get_graph")
	if not _need(state.functions[0].graph.locals.is_empty() and state.functions[0].graph.nodes.size() == 3 and state.functions[1].graph.locals.size() == 1, "Deleting a local changes one function only"):
		return false
	if not await _select_declaration("charge_weapon"):
		return false
	await _declaration_key(KEY_DELETE)
	state = graph_script.call(&"get_graph")
	if not _need(state.functions.size() == 1 and state.nodes.all(func(record: Dictionary) -> bool: return record.id != 2) and state.connections.is_empty() and graph_canvas.get_node_or_null(^"GraphNode_6") != null, "Deleting the active function removes calls and wires, then returns to the Event Graph"):
		return false
	await _declaration_key(KEY_Z, true)
	state = graph_script.call(&"get_graph")
	if not _need(state.functions.size() == 2 and state.nodes.any(func(record: Dictionary) -> bool: return record.id == 2) and state.connections.size() == 1, "Undo restores the function, call and wire together"):
		return false
	if not _need(ResourceSaver.save(graph_script, SCRIPT_PATH) == OK, "Save declaration descriptions with the resource"):
		return false
	var reloaded := ResourceLoader.load(SCRIPT_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(reloaded != null, "Reload declarations from the saved resource"):
		return false
	state = reloaded.call(&"get_graph")
	return _need(state.events[0].description == "Runs after the charge is ready." and state.functions[0].description == "Charges the active weapon.", "Loading restores the event and function descriptions")

## Confirm a wired type change removes only incompatible data and can be undone in one step.
func _verify_wired_type_change() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "target_open", "pin": {"id": &"value", "label": "Value", "type": TYPE_BOOL}, "value": true},
	], "enums": [
		{"id": 1, "name": "DoorState", "entries": [{"id": 1, "name": "Locked", "value": -7}], "next_entry_id": 2},
	], "nodes": [
		{"id": 1, "kind": &"ready"},
		{"id": 2, "kind": &"set_variable", "scope": &"instance", "variable": 1},
		{"id": 3, "kind": &"get_variable", "scope": &"instance", "variable": 1},
		{"id": 4, "kind": &"branch"},
	], "connections": [
		{"from": 1, "from_pin": &"then", "to": 2, "to_pin": &"in"},
		{"from": 2, "from_pin": &"then", "to": 4, "to_pin": &"in"},
		{"from": 3, "from_pin": &"value", "to": 4, "to_pin": &"condition"},
	]})
	await get_tree().process_frame
	if not await _select_declaration("target_open"):
		return false
	var original: Dictionary = graph_script.call(&"get_graph")
	if not await _choose_type("DoorState", false, true):
		return false
	if not _need(graph_script.call(&"get_graph") == original, "Cancel preserves the variable and its wires."):
		return false
	if not await _choose_type("DoorState", true, true):
		return false
	var changed: Dictionary = graph_script.call(&"get_graph")
	if not _need(changed.nodes == original.nodes
		and changed.variables[0].id == original.variables[0].id
		and changed.variables[0].pin.enum_type == &"GraphEnum.Enum_1"
		and changed.variables[0].value == -7
		and changed.connections == original.connections.slice(0, 2),
		"Changing the placed variable's type preserves execution and removes the incompatible Boolean wire."):
		return false
	await _declaration_key(KEY_Z, true)
	return _need(graph_script.call(&"get_graph") == original, "One Undo restores the original type, value, and wires.")


## Converting a concrete scalar to either container preserves the member schema across save and load.
func _verify_typed_container_conversion() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "checkpoint", "pin": {"id": &"value", "label": "Value", "type": TYPE_STRING_NAME}, "value": &"gate"},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("checkpoint"):
		return false
	var original: Dictionary = graph_script.call(&"get_graph")
	if not await _choose_type("Array", true, true) or not await _press("VisualScriptAddCollectionEntry"):
		return false
	var changed: Dictionary = graph_script.call(&"get_graph")
	var array_default: Array = changed.variables[0].value
	if not _need(changed.variables[0].pin.type == TYPE_ARRAY and changed.variables[0].pin.element.type == TYPE_STRING_NAME
		and array_default.is_typed() and array_default.get_typed_builtin() == TYPE_STRING_NAME and array_default.size() == 1,
		"Converting a StringName scalar to Array preserves StringName as its element type."):
		return false
	await _declaration_key(KEY_Z, true)
	await _declaration_key(KEY_Z, true)
	if not _need(graph_script.call(&"get_graph") == original, "Undo restores the scalar and its authored default."):
		return false
	if not await _choose_container("Dictionary") or not await _choose_type("int", true, true, "VisualScriptDictionaryValueType") \
		or not await _press("VisualScriptAddCollectionEntry"):
		return false
	changed = graph_script.call(&"get_graph")
	var dictionary_default: Dictionary = changed.variables[0].value
	if not _need(changed.variables[0].pin.type == TYPE_DICTIONARY and changed.variables[0].pin.key.type == TYPE_STRING_NAME
		and changed.variables[0].pin.value.type == TYPE_INT and dictionary_default.is_typed_key()
		and dictionary_default.get_typed_key_builtin() == TYPE_STRING_NAME and dictionary_default.is_typed_value()
		and dictionary_default.get_typed_value_builtin() == TYPE_INT and dictionary_default.size() == 1,
		"Converting a StringName scalar to Dictionary preserves its key type when assigning an int value type."):
		return false
	const TYPED_COLLECTION_PATH := "res://typed_dictionary_probe.crimsonscript"
	if not _need(ResourceSaver.save(graph_script, TYPED_COLLECTION_PATH) == OK, "Save reproducible typed Dictionary."):
		return false
	var restored_script := ResourceLoader.load(TYPED_COLLECTION_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(restored_script != null, "Load the authored typed Dictionary."):
		return false
	var restored_graph: Dictionary = restored_script.call(&"get_graph")
	var restored_default: Dictionary = restored_graph.variables[0].value
	return _need(restored_graph.variables[0].pin.key.type == TYPE_STRING_NAME and
		restored_default.is_typed_key() and restored_default.get_typed_key_builtin() == TYPE_STRING_NAME and
		restored_graph.variables[0].pin.value.type == TYPE_INT and restored_default.get_typed_value_builtin() == TYPE_INT and
		restored_default == dictionary_default, "Saving preserves the Dictionary key schema and value.")


## A reflected Godot enum can be selected for a variable and supplies named default values.
func _verify_native_enum_variable() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "processing_mode", "pin": {"id": &"value", "label": "Value", "type": TYPE_BOOL}, "value": false},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("processing_mode") or not await _choose_type("Node.ProcessMode", true, true):
		return false
	var document: Dictionary = graph_script.call(&"get_graph")
	var default_field := details_pane.find_child("VisualScriptVariableDefault", true, false) as Control
	if not _need(document.variables[0].pin.enum_type == &"Node.ProcessMode" and default_field != null,
		"Godot enum type is stored by its qualified identity and offers a default editor."):
		return false
	var popup := default_field.find_child("SelectPopup", true, false) as PopupPanel
	var trigger := default_field.find_child("SelectTrigger", true, false) as Button
	if not _need(popup != null and trigger != null, "Find the named Godot enum default picker."):
		return false
	trigger.pressed.emit()
	await get_tree().process_frame
	var current_value: Variant = document.variables[0].value
	for candidate: Node in popup.find_children("*", "Button", true, false):
		var option := candidate as Button
		if option.is_visible_in_tree() and not option.disabled and option.text != trigger.text:
			option.pressed.emit()
			await get_tree().process_frame
			document = graph_script.call(&"get_graph")
			return _need(document.variables[0].value != current_value, "Selecting another named native enum member changes the default.")
	return _need(false, "Find another enabled named default value in the Godot enum.")

## Editing two Array values before Enter preserves both drafts and supports repeated drag and Undo.
func _verify_array_cell_edits() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "routes", "pin": {"id": &"value", "label": "Value", "type": TYPE_ARRAY},
			"value": ["", "", "exit"]},
		{"id": 2, "name": "status", "pin": {"id": &"value", "label": "Value", "type": TYPE_STRING}, "value": "idle"},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("routes"):
		return false
	var value_field := "VisualScriptCollectionValue"
	if not await _type_line_draft(_collection_cell(0, value_field), "hallway") or \
		not await _type_line_draft(_collection_cell(1, value_field), "workshop") or \
		not await _submit_line_draft(_collection_cell(1, value_field)):
		return false
	var state: Dictionary = graph_script.call(&"get_graph")
	var pending_first := _collection_cell(0, value_field)
	if not _need(state.variables[0].value[1] == "workshop" and state.variables[0].value[2] == "exit"
		and pending_first != null and pending_first.text == "hallway", "Enter keeps the sibling Array draft and committed value."):
		return false
	if not await _submit_line_draft(pending_first):
		return false
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[0].value == ["hallway", "workshop", "exit"], "Both independently submitted Array values are stored."):
		return false
	if not await _type_line_draft(_collection_cell(0, value_field), "shortcut") or not await _drag_collection_row(0, 2):
		return false
	state = graph_script.call(&"get_graph")
	var moved_draft := _collection_cell(2, value_field)
	if not _need(state.variables[0].value == ["workshop", "exit", "hallway"] and moved_draft != null and moved_draft.text == "shortcut",
		"The unsubmitted draft follows its Array entry while dragging."):
		return false
	await _declaration_key(KEY_Z, true)
	if not await _select_declaration("status") or not await _select_declaration("routes"):
		return false
	if not _need(_collection_cell(0, value_field).text == "hallway" and _collection_cell(1, value_field).text == "workshop",
		"Switching the active declaration and back reloads all committed Array values."):
		return false
	if not await _cancel_collection_drag(1):
		return false
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[0].value == ["hallway", "workshop", "exit"], "A duplicate release outside the list does not move or clear an Array row."):
		return false
	var expected: Array[String] = ["hallway", "workshop", "exit"]
	var snapshots: Array = [expected.duplicate()]
	var randomizer := RandomNumberGenerator.new()
	randomizer.seed = 451279
	for reorder_index: int in range(8):
		var from := randomizer.randi_range(0, 2)
		var to := (from + randomizer.randi_range(1, 2)) % 3
		if not await _drag_collection_row(from, to):
			return false
		var moved: String = expected.pop_at(from)
		expected.insert(to, moved)
		snapshots.append(expected.duplicate())
		state = graph_script.call(&"get_graph")
		if not _need(state.variables[0].value == expected, "Array pointer drag %s preserves every value and applies one move." % (reorder_index + 1)):
			return false
	for undo_index: int in range(snapshots.size() - 2, -1, -1):
		await _declaration_key(KEY_Z, true)
		state = graph_script.call(&"get_graph")
		if not _need(state.variables[0].value == snapshots[undo_index], "Each Undo restores one Array reorder with its submitted values."):
			return false
	await _declaration_key(KEY_Y, true)
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[0].value == snapshots[1], "Redo reapplies the first Array reorder without changing other values."):
		return false
	const ROUTES_PATH := "res://routes_probe.crimsonscript"
	if not _need(ResourceSaver.save(graph_script, ROUTES_PATH) == OK, "Write the reordered Array to disk."):
		return false
	var loaded_script := ResourceLoader.load(ROUTES_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(loaded_script != null, "Reopen the saved Array fixture."):
		return false
	var restored: Dictionary = loaded_script.call(&"get_graph")
	return _need(restored.variables[0].value == snapshots[1], "Reopening keeps every Array value in the saved order.")


## Renaming a Dictionary key and submitting its value preserve unsaved text next to the change.
func _verify_dictionary_cell_edits() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "rooms", "pin": {"id": &"value", "label": "Value", "type": TYPE_DICTIONARY},
			"value": {"oldkey": "", "exit": "outside"}, "default_key_order": ["oldkey", "exit"]},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("rooms"):
		return false
	var key_field := "VisualScriptCollectionKey"
	var value_field := "VisualScriptCollectionValue"
	if not await _type_line_draft(_collection_cell(0, value_field), "inside") or \
		not await _type_line_draft(_collection_cell(0, key_field), "workshop") or \
		not await _submit_line_draft(_collection_cell(0, key_field)):
		return false
	var state: Dictionary = graph_script.call(&"get_graph")
	var pending_value := _collection_cell(0, value_field)
	if not _need(state.variables[0].value.has("workshop") and not state.variables[0].value.has("oldkey")
		and state.variables[0].value.exit == "outside" and pending_value != null and pending_value.text == "inside",
		"Renaming a Dictionary key preserves its pending value and the other pair."):
		return false
	if not await _submit_line_draft(pending_value):
		return false
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[0].value == {"workshop": "inside", "exit": "outside"}
		and state.variables[0].default_key_order == ["workshop", "exit"], "Independent key and value edits commit to the same Dictionary pair."):
		return false
	if not await _type_line_draft(_collection_cell(0, key_field), "exit") or not await _submit_line_draft(_collection_cell(0, key_field)):
		return false
	if not _need(graph_script.call(&"get_graph") == state, "A duplicate Dictionary key cannot change the saved entries or order."):
		return false
	if not await _drag_collection_row(0, 1):
		return false
	var reordered: Dictionary = graph_script.call(&"get_graph")
	if not _need(reordered.variables[0].value == state.variables[0].value
		and reordered.variables[0].default_key_order == ["exit", "workshop"],
		"Dragging a Dictionary after a rejected entry preserves its mapping and moves one pair."):
		return false
	await _declaration_key(KEY_Z, true)
	if not _need(graph_script.call(&"get_graph") == state, "Undo restores Dictionary display order without changing the values."):
		return false
	await _declaration_key(KEY_Y, true)
	return _need(graph_script.call(&"get_graph") == reordered, "Redo restores the reordered Dictionary.")


## Enum label and integer drafts stay with the member while its sibling is committed.
func _verify_enum_cell_edits() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [], "enums": [
		{"id": 1, "name": "DoorState", "next_entry_id": 3, "entries": [
			{"id": 1, "name": "Locked", "value": 1},
			{"id": 2, "name": "Open", "value": 2},
		]},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("DoorState") or not await _expand_details("VisualScriptMembersDisclosure"):
		return false
	var members := details_pane.find_child("VisualScriptEnumMembers", true, false)
	if not _need(members != null and members.get_child_count() == 2, "Find two authored enum member rows."):
		return false
	var label_field := "VisualScriptEnumMemberName"
	var value_field := "VisualScriptEnumMemberValue"
	var first_value := members.get_child(0).find_child(value_field, true, false) as LineEdit
	var second_label := members.get_child(1).find_child(label_field, true, false) as LineEdit
	if not await _type_line_draft(first_value, "-409") or not await _type_line_draft(second_label, "Opening") or \
		not await _submit_line_draft(second_label):
		return false
	var state: Dictionary = graph_script.call(&"get_graph")
	members = details_pane.find_child("VisualScriptEnumMembers", true, false)
	first_value = members.get_child(0).find_child(value_field, true, false) as LineEdit if members != null else null
	if not _need(state.enums[0].entries[1].name == "Opening" and state.enums[0].entries[0].name == "Locked" and
		first_value != null and first_value.text == "-409", "Submitting a member label preserves the neighboring integer draft."):
		return false
	if not await _submit_line_draft(first_value):
		return false
	state = graph_script.call(&"get_graph")
	if not _need(state.enums[0].entries[0] == {"id": 1, "name": "Locked", "value": -409} and
		state.enums[0].entries[1] == {"id": 2, "name": "Opening", "value": 2}, "Both enum edits keep their identities and sparse signed values."):
		return false
	members = details_pane.find_child("VisualScriptEnumMembers", true, false)
	var second_value := members.get_child(1).find_child(value_field, true, false) as LineEdit if members != null else null
	if not await _type_line_draft(second_value, "-409") or not await _submit_line_draft(second_value):
		return false
	if not _need(graph_script.call(&"get_graph") == state, "A duplicate signed enum value leaves both stored values and IDs intact."):
		return false
	for invalid: String in ["9223372036854775808", "-9223372036854775809", "14abc"]:
		if not await _type_line_draft(second_value, invalid) or not await _submit_line_draft(second_value) or \
			not _need(graph_script.call(&"get_graph") == state, "Reject malformed or overflowing enum input: " + invalid):
			return false
	return true

## Save a self-contained scene that can be assigned in the resource picker fixture.
func _make_collection_scene(path: String, scene_name: String) -> bool:
	var scene_root := Node3D.new()
	scene_root.name = scene_name
	var packed_scene := PackedScene.new()
	var packed := packed_scene.pack(scene_root)
	scene_root.free()
	return _need(packed == OK and ResourceSaver.save(packed_scene, path) == OK, "Generate a PackedScene for the collection picker.")


## Choose a file through the row's browser action and native file selection signal.
func _choose_collection_asset(index: int, path: String) -> bool:
	const BROWSE_ACTION_ID := 1
	var rows := _collection_rows()
	if not _need(index >= 0 and index < rows.size(), "Find the collection asset row."):
		return false
	var filename := rows[index].find_child("GraphAssetName", true, false) as Button
	if not _need(filename != null, "The collection element uses an asset picker."):
		return false
	var field := filename.get_parent()
	var menus := field.find_children("*", "MenuButton", false, false)
	var menu := menus[0] as MenuButton if not menus.is_empty() else null
	if not _need(menu != null, "Find the resource actions for this collection element."):
		return false
	var options := menu.get_popup()
	if not _need(options.get_item_index(BROWSE_ACTION_ID) >= 0, "The collection row offers its browse action."):
		return false
	options.id_pressed.emit(BROWSE_ACTION_ID)
	await get_tree().process_frame
	var dialogs := field.find_children("*", "EditorFileDialog", false, false)
	var chooser := dialogs[0] as FileDialog if not dialogs.is_empty() else null
	if not _need(chooser != null and chooser.visible, "Browse opens the row's actual project file chooser."):
		return false
	chooser.file_selected.emit(path)
	chooser.hide()
	await get_tree().process_frame
	await get_tree().process_frame
	return true


## Pack, author, and reopen saved resources through the same Details controls a user operates.
func _verify_asset_collection_details() -> bool:
	const FIRST_PATH := "res://details_collection_spawn.tscn"
	const SECOND_PATH := "res://details_collection_exit.tscn"
	const MISSING_PATH := "res://details_collection_missing.tscn"
	const GRAPH_ASSET_PATH := "res://details_collection_asset_probe.crimsonscript"
	if not _make_collection_scene(FIRST_PATH, "Spawn") or not _make_collection_scene(SECOND_PATH, "Exit"):
		return false
	EditorInterface.get_resource_filesystem().scan()
	var scene_pin := {"id": &"value", "label": "Value", "type": TYPE_OBJECT, "class": &"PackedScene"}
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "scenes", "pin": {"id": &"value", "label": "Value", "type": TYPE_ARRAY, "element": scene_pin},
			"value": [{"path": MISSING_PATH, "uid": ""}]},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("scenes"):
		return false
	var default_editor := details_pane.find_child("VisualScriptVariableDefault", true, false) as Control
	var options := default_editor.find_child("GraphAssetOptions", true, false) as MenuButton if default_editor != null else null
	if not _need(options != null, "The resource row can be edited when its saved scene is missing."):
		return false
	options.show_popup()
	await get_tree().process_frame
	var menu := options.get_popup()
	var clear_index := menu.get_item_index(2)
	if not _need(clear_index != -1 and not menu.is_item_disabled(clear_index), "A missing asset has a usable Clear action."):
		return false
	menu.id_pressed.emit(2)
	menu.hide()
	await get_tree().process_frame
	var cleared: Dictionary = graph_script.call(&"get_graph")
	if not _need(cleared.variables[0].value == [null], "Clear removes the missing resource token without dropping its row.") or \
		not await _press("VisualScriptAddCollectionEntry"):
		return false
	if not await _choose_collection_asset(0, FIRST_PATH) or not await _choose_collection_asset(1, SECOND_PATH):
		return false
	var state: Dictionary = graph_script.call(&"get_graph")
	if not _need(state.variables[0].value.size() == 2 and state.variables[0].value[0].get("path") == FIRST_PATH
		and state.variables[0].value[1].get("path") == SECOND_PATH, "Each element keeps its own selected PackedScene path."):
		return false
	if not await _drag_collection_row(0, 1, "GraphAssetName"):
		return false
	var moved: Dictionary = graph_script.call(&"get_graph")
	if not _need(moved.variables[0].value[0].get("path") == SECOND_PATH and moved.variables[0].value[1].get("path") == FIRST_PATH,
		"Dropping over a resource field moves only the dragged asset row."):
		return false
	await _declaration_key(KEY_Z, true)
	if not _need(graph_script.call(&"get_graph") == state, "Undo restores asset order while keeping both references."):
		return false
	if not _need(ResourceSaver.save(graph_script, GRAPH_ASSET_PATH) == OK, "Save the typed resource collection."):
		return false
	var reloaded := ResourceLoader.load(GRAPH_ASSET_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(reloaded != null, "Reopen resource collection graph."):
		return false
	return _need((reloaded.call(&"get_graph") as Dictionary).variables == state.variables,
		"Saving preserves PackedScene type and both selected resources.")


## Exercise consecutive color adjustments in one native popup and one Undo.
func _verify_color_default() -> bool:
	var original := Color(0.25, 0.5, 0.75, 1.0)
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": [], "variables": [
		{"id": 1, "name": "team_color", "pin": {"type": TYPE_COLOR, "id": &"value", "label": "Value"}, "value": original},
	]})
	await get_tree().process_frame
	if not await _select_declaration("team_color"):
		return false
	var control := details_pane.find_child("VisualScriptVariableDefault", true, false) as ColorPickerButton
	if not _need(control != null, "A Color variable opens a color picker."):
		return false
	var popup := control.get_popup()
	popup.popup()
	await get_tree().process_frame
	if not _need(popup != null and popup.visible, "The native color popup opens."):
		return false
	var picker := control.get_picker()
	var choice := Color(0.875, 0.125, 0.5, 0.75)
	for sample: Color in [Color.WHITE, choice]:
		picker.color_changed.emit(sample)
		await get_tree().process_frame
		if not _need(is_instance_valid(popup) and popup.visible and graph_script.call(&"get_graph").variables[0].value == original,
				"Editing a color keeps the popup and declaration alive until close."):
			return false
	popup.hide()
	await get_tree().process_frame
	if not _need(graph_script.call(&"get_graph").variables[0].value == choice, "Closing the picker commits the final color."):
		return false
	await _declaration_key(KEY_Z, true)
	return _need(graph_script.call(&"get_graph").variables[0].value == original, "One Undo restores the original color.")


## Exercise the visible numeric default fields without rounding or clamping submitted values.
func _verify_numeric_defaults() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": [], "variables": [
		{"id": 1, "name": "integer_cap", "pin": {"type": TYPE_INT, "id": &"value", "label": "Value"}, "value": 0},
		{"id": 2, "name": "number_cap", "pin": {"type": TYPE_FLOAT, "id": &"value", "label": "Value"}, "value": 0.0},
		{"id": 3, "name": "grid_size", "pin": {"type": TYPE_VECTOR3I, "id": &"value", "label": "Value"}, "value": Vector3i(1, 4, 8)},
		{"id": 4, "name": "direction", "pin": {"type": TYPE_VECTOR2, "id": &"value", "label": "Value"}, "value": Vector2(1.5, 2.5)},
	]})
	await get_tree().process_frame
	var cases: Array[Dictionary] = [
		{"name": "integer_cap", "field": "VisualScriptVariableDefault", "valid": "+9223372036854775807",
			"invalid": "9223372036854775808", "expected": 9223372036854775807},
		{"name": "integer_cap", "field": "VisualScriptVariableDefault", "valid": "-9223372036854775808",
			"invalid": "-9223372036854775809", "expected": -9223372036854775807 - 1},
		{"name": "number_cap", "field": "VisualScriptVariableDefault", "valid": "6e38",
			"invalid": "1e310", "expected": 6e38},
		{"name": "grid_size", "field": "VisualScriptVariableAxisX", "valid": "2147483647",
			"invalid": "2147483648", "expected": Vector3i(2147483647, 4, 8)},
		{"name": "direction", "field": "VisualScriptVariableAxisY", "valid": "-2.75",
			"invalid": "6e38", "expected": Vector2(1.5, -2.75)},
		{"name": "direction", "field": "VisualScriptVariableAxisY", "valid": "0.125e+1",
			"invalid": "1e+", "expected": Vector2(1.5, 1.25)},
	]
	for example: Dictionary in cases:
		if not await _select_declaration(example.name) or not await _edit_text(example.field, example.valid):
			return false
		var state: Dictionary = graph_script.call(&"get_graph")
		var matches: Array = state.variables.filter(func(record: Dictionary) -> bool: return record.name == example.name)
		if not _need(matches.size() == 1 and matches[0].value == example.expected,
				"Numeric editing preserves the exact value and other vector axes for " + String(example.name)):
			return false
		if not await _edit_text(example.field, example.invalid):
			return false
		var after_invalid: Dictionary = graph_script.call(&"get_graph")
		if not _need(after_invalid == state, "Reject malformed or out-of-range input without changing " + String(example.name)):
			return false
	return true


## Common types stay directly selectable without opening a category.
func _verify_quick_types() -> bool:
	if not await _press("VisualScriptDeclarationType"):
		return false
	var results := workspace.find_child("VisualScriptTypeTree", true, false) as Tree
	if not _need(results != null and results.get_root() != null, "Find the unfiltered value type tree."):
		return false
	var root_options: PackedStringArray = []
	var choice := results.get_root().get_first_child()
	while choice != null:
		if choice.get_metadata(0) is Dictionary:
			root_options.append(choice.get_text(0))
		choice = choice.get_next()
	var popup := results.get_window()
	popup.hide()
	for label: String in ["float", "int", "String", "StringName", "bool", "Dictionary", "Array", "Vector2", "Vector3", "PackedScene"]:
		if not _need(root_options.has(label), "A common type is directly available in the picker: " + label):
			return false
	return true


## Return the visible row for the active collection or enum declaration.
func _matrix_row(kind: String, index: int) -> Control:
	if kind == "enum":
		var members := details_pane.find_child("VisualScriptEnumMembers", true, false) as VBoxContainer
		return members.get_child(index) as Control if members != null and index >= 0 and index < members.get_child_count() else null
	var rows := _collection_rows()
	return rows[index] if index >= 0 and index < rows.size() else null


## Map a matrix column to the native editable field that owns its draft.
func _matrix_field(kind: String, row_view: Control, column: String) -> LineEdit:
	if row_view == null:
		return null
	var field_name: String
	if kind == "enum":
		field_name = "VisualScriptEnumMemberName" if column == "name" else "VisualScriptEnumMemberValue"
	else:
		field_name = "VisualScriptCollectionKey" if column == "key" else "VisualScriptCollectionValue"
	return row_view.find_child(field_name, true, false) as LineEdit


## Seed a declaration in the disposable graph, with stable enum IDs and explicit map order.
func _matrix_seed(kind: String, name_text: String, initial: Variant, key_order: Array) -> bool:
	var document := {"version": 1, "variables": [], "enums": [], "nodes": [], "connections": []}
	if kind == "enum":
		var members: Array[Dictionary] = []
		for index: int in range(initial.size()):
			members.append({"id": index + 1, "name": initial[index][0], "value": initial[index][1]})
		document.enums.append({"id": 1, "name": name_text, "next_entry_id": members.size() + 1, "entries": members})
	else:
		var variable_data := {"id": 1, "name": name_text, "pin": {"id": &"value", "label": "Value",
			"type": TYPE_ARRAY if kind == "array" else TYPE_DICTIONARY}, "value": initial}
		if kind == "dictionary":
			variable_data["default_key_order"] = key_order
		document.variables.append(variable_data)
	graph_script.call(&"set_graph", document)
	await get_tree().process_frame
	if not await _select_declaration(name_text):
		return false
	return kind != "enum" or await _expand_details("VisualScriptMembersDisclosure")


## Compare the saved declaration with expected value, order and optional enum IDs.
func _matrix_saved(kind: String, expected: Variant, auxiliary: Variant) -> bool:
	return _matrix_check_document(graph_script.call(&"get_graph"), kind, expected, auxiliary)


## Inspect a document's committed data before or after serialization.
func _matrix_check_document(document: Dictionary, kind: String, expected: Variant, auxiliary: Variant) -> bool:
	if kind == "enum":
		var actual_values: Array = []
		var actual_ids: Array[int] = []
		for member: Dictionary in document.enums[0].entries:
			actual_values.append([member.name, member.value])
			actual_ids.append(member.id)
		return _need(actual_values == expected and (auxiliary == null or actual_ids == auxiliary),
			"Expected members %s (IDs %s), got %s (IDs %s)." % [expected, auxiliary, actual_values, actual_ids])
	var authored: Dictionary = document.variables[0]
	if not _need(authored.value == expected, "Expected values %s, got %s." % [expected, authored.value]):
		return false
	if kind == "dictionary" and auxiliary != null:
		var actual_order: Array = authored.get("default_key_order", [])
		if not _need(actual_order == auxiliary, "Expected pair order %s, got %s." % [auxiliary, actual_order]):
			return false
		var mapped_keys: Array = authored.value.keys()
		for index: int in range(actual_order.size()):
			if not _need(typeof(actual_order[index]) == typeof(auxiliary[index]), "Preserve the key's exact type at position %d." % index):
				return false
			var key_index := mapped_keys.find(auxiliary[index])
			if not _need(key_index >= 0 and typeof(mapped_keys[key_index]) == typeof(auxiliary[index]), "The stored Dictionary key matches the order's key type."):
				return false
	return true


## Activate the existing Details button after moving focus away from a pending LineEdit.
func _matrix_press(button: Button) -> bool:
	if not _need(button != null and button.is_visible_in_tree() and not button.disabled, "Find the enabled Details button."):
		return false
	var focus_owner := workspace.get_window().gui_get_focus_owner()
	if focus_owner is LineEdit:
		focus_owner.release_focus()
	button.pressed.emit()
	await get_tree().process_frame
	return true


## Execute one recorded action against the real Details controls or assert the resulting state.
func _matrix_step(kind: String, command: Array) -> bool:
	var action: String = command[0]
	if action == "saved":
		return _matrix_saved(kind, command[1], command[2] if command.size() > 2 else null)
	if action == "roundtrip":
		const ROUNDTRIP_PATH := "res://collection_matrix_roundtrip.crimsonscript"
		if not _need(ResourceSaver.save(graph_script, ROUNDTRIP_PATH) == OK, "Save the fixture with pending text still open."):
			return false
		var reopened := ResourceLoader.load(ROUNDTRIP_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
		return _need(reopened != null, "Reload the fixture without sharing its in-memory data.") and \
			_matrix_check_document(reopened.call(&"get_graph"), kind, command[1], command[2] if command.size() > 2 else null)
	if action == "add":
		var button_name := "VisualScriptAddEnumMember" if kind == "enum" else "VisualScriptAddCollectionEntry"
		return await _matrix_press(details_pane.find_child(button_name, true, false) as Button)
	if action == "undo" or action == "redo":
		await _declaration_key(KEY_Z if action == "undo" else KEY_Y, true)
		return true
	var row_view := _matrix_row(kind, command[1])
	if not _need(row_view != null, "Find row %s." % command[1]):
		return false
	if action == "remove" or action == "remove_disabled":
		var button_name := "VisualScriptRemoveEnumMember" if kind == "enum" else "VisualScriptRemoveCollectionEntry"
		var remove_button := row_view.find_child(button_name, true, false) as Button
		if action == "remove_disabled":
			return _need(remove_button != null and remove_button.disabled, "Removing the final enum member is disabled.")
		return await _matrix_press(remove_button)
	if action == "move" or action == "drag":
		var grip_name := "VisualScriptReorderEnumMember" if kind == "enum" else "VisualScriptReorderCollectionEntry"
		var grip := row_view.find_child(grip_name, true, false) as Button
		if action == "move":
			return await _move_row(grip, KEY_UP if command[2] < 0 else KEY_DOWN)
		var destination := _matrix_field(kind, _matrix_row(kind, command[2]), "value")
		return await _drag_authored_row(grip, destination, command[1] < command[2])
	if action == "type" or action == "key_type":
		var field_boxes := row_view.find_children("VisualScriptCollectionType", "Control", true, false)
		var box_index := 1 if kind == "dictionary" and action == "type" else 0
		var field_box := field_boxes[box_index] as Control if field_boxes.size() > box_index else null
		if not _need(field_box != null, "Find the row type selector."):
			return false
		var option := await _open_select(field_box, command[2])
		if option == null:
			return false
		option.pressed.emit()
		await get_tree().process_frame
		return true
	var field := _matrix_field(kind, row_view, command[2])
	if action == "draft" or action == "replace":
		if not await _type_line_draft(field, command[3]):
			return false
		return true if action == "draft" else await _submit_line_draft(field)
	if action == "enter":
		return await _submit_line_draft(field)
	if action == "text":
		var actual_text := field.text if field != null else "<no field>"
		return _need(actual_text == command[3], "Expected draft '%s', got '%s'." % [command[3], actual_text])
	return _need(false, "Unknown matrix action " + action)


## Array sequences mix pending edits, structural actions and history.
func _matrix_array_cases() -> Dictionary:
	return {
		"delete_between_drafts": [
			["draft", 0, "value", "west"], ["draft", 2, "value", "east"], ["remove", 1],
			["saved", ["a", "c"]], ["text", 0, "value", "west"], ["text", 1, "value", "east"],
			["enter", 1, "value"], ["enter", 0, "value"], ["saved", ["west", "east"]],
		],
		"append_then_move_draft": [
			["draft", 0, "value", "west"], ["add"], ["saved", ["a", "b", "c", ""]],
			["move", 0, 1], ["text", 1, "value", "west"], ["enter", 1, "value"],
			["replace", 3, "value", "exit"], ["saved", ["b", "west", "c", "exit"]],
		],
		"drag_delete_reverse_move": [
			["draft", 0, "value", "west"], ["drag", 0, 2], ["saved", ["b", "c", "a"]],
			["remove", 0], ["move", 1, -1], ["text", 0, "value", "west"],
			["roundtrip", ["a", "c"]], ["text", 0, "value", "west"], ["enter", 0, "value"], ["roundtrip", ["west", "c"]],
		],
		"remove_undo_redo": [
			["draft", 2, "value", "east"], ["remove", 0], ["undo"], ["saved", ["a", "b", "c"]],
			["text", 2, "value", "east"], ["redo"], ["saved", ["b", "c"]],
			["text", 1, "value", "east"], ["enter", 1, "value"], ["saved", ["b", "east"]],
		],
		"equal_entries_move_undo_redo": [
			["replace", 1, "value", "a"], ["draft", 0, "value", "first"], ["draft", 1, "value", "second"],
			["move", 0, 1], ["saved", ["a", "a", "c"]], ["text", 0, "value", "second"], ["text", 1, "value", "first"],
			["undo"], ["saved", ["a", "a", "c"]], ["text", 0, "value", "first"], ["text", 1, "value", "second"],
			["redo"], ["text", 0, "value", "second"], ["text", 1, "value", "first"],
			["enter", 1, "value"], ["enter", 0, "value"], ["saved", ["second", "first", "c"]],
		],
		"pending_type_delete_then_enter": [
			["type", 2, "int"], ["draft", 2, "value", "32"], ["remove", 1], ["saved", ["a", "c"]],
			["text", 1, "value", "32"], ["enter", 1, "value"], ["saved", ["a", 32]],
		],
		"pending_type_move_and_reject": [
			["type", 0, "int"], ["draft", 0, "value", "9223372036854775808"], ["move", 0, 1],
			["enter", 1, "value"], ["saved", ["b", "a", "c"]], ["text", 1, "value", "9223372036854775808"],
			["replace", 1, "value", "-29"], ["saved", ["b", -29, "c"]],
		],
		"empty_then_add": [
			["remove", 2], ["remove", 1], ["draft", 0, "value", "discard-me"], ["remove", 0], ["saved", []],
			["add"], ["text", 0, "value", ""], ["replace", 0, "value", "fresh"], ["saved", ["fresh"]],
		],
	}


## Dictionary sequences also check pair identity across key changes and display-order history.
func _matrix_dictionary_cases() -> Dictionary:
	return {
		"delete_preserves_opposite_drafts": [
			["draft", 0, "value", "new-a"], ["draft", 2, "key", "exit"], ["remove", 1],
			["saved", {"a": "A", "c": "C"}, ["a", "c"]], ["text", 0, "value", "new-a"], ["text", 1, "key", "exit"],
			["enter", 0, "value"], ["enter", 1, "key"], ["saved", {"a": "new-a", "exit": "C"}, ["a", "exit"]],
		],
		"add_preserves_two_cells": [
			["draft", 1, "key", "gate"], ["draft", 1, "value", "blocked"], ["add"],
			["saved", {"a": "A", "b": "B", "c": "C", "key": ""}, ["a", "b", "c", "key"]],
			["text", 1, "key", "gate"], ["text", 1, "value", "blocked"], ["enter", 1, "key"], ["enter", 1, "value"],
			["saved", {"a": "A", "gate": "blocked", "c": "C", "key": ""}, ["a", "gate", "c", "key"]],
		],
		"rename_undo_redo_keeps_value_draft": [
			["draft", 0, "value", "inside"], ["replace", 0, "key", "room"], ["undo"],
			["saved", {"a": "A", "b": "B", "c": "C"}, ["a", "b", "c"]], ["text", 0, "value", "inside"],
			["redo"], ["text", 0, "value", "inside"], ["enter", 0, "value"],
			["saved", {"room": "inside", "b": "B", "c": "C"}, ["room", "b", "c"]],
		],
		"drag_then_remove_and_move_back": [
			["draft", 2, "value", "moved"], ["drag", 2, 0], ["saved", {"a": "A", "b": "B", "c": "C"}, ["c", "a", "b"]],
			["remove", 1], ["move", 0, 1], ["text", 1, "value", "moved"], ["enter", 1, "value"],
			["saved", {"b": "B", "c": "moved"}, ["b", "c"]],
		],
		"rejected_key_then_delete_and_retry": [
			["draft", 0, "value", "keep-me"], ["replace", 0, "key", "b"],
			["saved", {"a": "A", "b": "B", "c": "C"}, ["a", "b", "c"]], ["remove", 1],
			["text", 0, "key", "b"], ["text", 0, "value", "keep-me"], ["enter", 0, "key"], ["enter", 0, "value"],
			["saved", {"b": "keep-me", "c": "C"}, ["b", "c"]],
		],
		"undo_redo_remove_keeps_pair_draft": [
			["draft", 2, "key", "exit"], ["remove", 0], ["undo"], ["text", 0, "key", "a"], ["text", 2, "key", "exit"],
			["redo"], ["text", 1, "key", "exit"], ["enter", 1, "key"], ["saved", {"b": "B", "exit": "C"}, ["b", "exit"]],
		],
		"same_spelling_key_type_then_reject_other_key": [
			["draft", 1, "value", "open"], ["key_type", 1, "StringName"], ["replace", 1, "key", "b"],
			["saved", {"a": "A", &"b": "B", "c": "C"}, ["a", &"b", "c"]],
			["undo"], ["saved", {"a": "A", "b": "B", "c": "C"}, ["a", "b", "c"]],
			["redo"], ["saved", {"a": "A", &"b": "B", "c": "C"}, ["a", &"b", "c"]],
			["replace", 1, "key", "c"], ["saved", {"a": "A", &"b": "B", "c": "C"}, ["a", &"b", "c"]],
			["move", 1, -1], ["text", 0, "value", "open"], ["enter", 0, "value"],
			["saved", {"a": "A", &"b": "open", "c": "C"}, [&"b", "a", "c"]],
		],
		"delete_to_empty_then_reuse_key": [
			["remove", 2], ["remove", 1], ["draft", 0, "value", "old draft"], ["remove", 0], ["saved", {}, []],
			["add"], ["text", 0, "value", ""], ["replace", 0, "key", "a"], ["replace", 0, "value", "new"],
			["saved", {"a": "new"}, ["a"]],
		],
	}


## Enum sequences assert member ordering and stable IDs as well as edited values.
func _matrix_enum_cases() -> Dictionary:
	return {
		"delete_preserves_label_and_number": [
			["draft", 0, "name", "Opening"], ["draft", 2, "value", "-19"], ["remove", 1],
			["saved", [["Open", 10], ["Closed", 30]], [1, 3]], ["text", 0, "name", "Opening"], ["text", 1, "value", "-19"],
			["enter", 1, "value"], ["enter", 0, "name"], ["saved", [["Opening", 10], ["Closed", -19]], [1, 3]],
		],
		"add_move_and_enter": [
			["draft", 1, "name", "Held"], ["draft", 1, "value", "-12"], ["add"],
			["move", 1, -1], ["text", 0, "name", "Held"], ["text", 0, "value", "-12"], ["enter", 0, "value"], ["enter", 0, "name"],
			["saved", [["Held", -12], ["Open", 10], ["Closed", 30], ["Value 1", 0]], [2, 1, 3, 4]],
		],
		"remove_undo_redo": [
			["draft", 2, "name", "Locked"], ["remove", 0], ["undo"], ["text", 2, "name", "Locked"],
			["redo"], ["text", 1, "name", "Locked"], ["enter", 1, "name"],
			["saved", [["Busy", 20], ["Locked", 30]], [2, 3]],
		],
		"rename_undo_redo_with_value_draft": [
			["draft", 1, "value", "24"], ["replace", 1, "name", "Occupied"], ["undo"],
			["text", 1, "value", "24"], ["text", 1, "name", "Busy"], ["redo"], ["text", 1, "name", "Occupied"],
			["draft", 1, "name", "Occupied again"], ["undo"], ["saved", [["Open", 10], ["Busy", 20], ["Closed", 30]], [1, 2, 3]],
			["text", 1, "name", "Occupied again"], ["redo"], ["text", 1, "name", "Occupied again"],
			["text", 1, "value", "24"], ["enter", 1, "value"], ["enter", 1, "name"],
			["saved", [["Open", 10], ["Occupied again", 24], ["Closed", 30]], [1, 2, 3]],
		],
		"duplicate_value_then_delete_and_retry": [
			["draft", 2, "name", "Locked"], ["replace", 2, "value", "20"],
			["saved", [["Open", 10], ["Busy", 20], ["Closed", 30]], [1, 2, 3]], ["remove", 1],
			["text", 1, "value", "20"], ["text", 1, "name", "Locked"], ["enter", 1, "value"], ["enter", 1, "name"],
			["roundtrip", [["Open", 10], ["Locked", 20]], [1, 3]],
		],
		"delete_drag_back_and_enter": [
			["draft", 2, "value", "41"], ["remove", 1], ["drag", 1, 0], ["text", 0, "value", "41"],
			["move", 0, 1], ["text", 1, "value", "41"], ["enter", 1, "value"], ["saved", [["Open", 10], ["Closed", 41]], [1, 3]],
		],
		"add_undo_redo_while_member_has_draft": [
			["draft", 0, "name", "Opening"], ["add"], ["undo"],
			["saved", [["Open", 10], ["Busy", 20], ["Closed", 30]], [1, 2, 3]], ["text", 0, "name", "Opening"],
			["redo"], ["text", 0, "name", "Opening"], ["enter", 0, "name"],
			["saved", [["Opening", 10], ["Busy", 20], ["Closed", 30], ["Value 1", 0]], [1, 2, 3, 4]],
		],
		"protect_last_member_and_use_fresh_id": [
			["remove", 1], ["remove", 1], ["draft", 0, "name", "Opening"], ["remove_disabled", 0], ["add"],
			["text", 0, "name", "Opening"], ["enter", 0, "name"], ["replace", 1, "value", "-40"],
			["saved", [["Opening", 10], ["Value 1", -40]], [1, 4]],
		],
	}


## Locate a signature's collection by stable pin ID. Several defaults may be visible together.
func _matrix_signature_default(pin_id: String) -> Control:
	var pin_name := details_pane.find_child("VisualScriptSignature_" + pin_id, true, false)
	if pin_name == null:
		return null
	var fields := pin_name.get_parent()
	while fields != null and fields.find_child("VisualScriptParameterDefault*", false, false) == null:
		fields = fields.get_parent()
	return fields.find_child("VisualScriptParameterDefault*", false, false) as Control if fields != null else null


## Find rows and editable values inside a specific parameter's default.
func _matrix_signature_rows(pin_id: String) -> Array[PanelContainer]:
	var rows: Array[PanelContainer] = []
	var collection := _matrix_signature_default(pin_id)
	if collection != null:
		for child: Node in collection.get_children():
			if child is PanelContainer:
				rows.append(child as PanelContainer)
	return rows


## Read the exact row values currently shown by each signature's default editor.
func _matrix_signature_shown(pin_id: String, expected: Array) -> bool:
	var rows := _matrix_signature_rows(pin_id)
	var actual_values: Array[String] = []
	for row_view: PanelContainer in rows:
		var field := _matrix_field("array", row_view, "value")
		actual_values.append(field.text if field != null else "<missing field>")
	return _need(actual_values == expected, "Input '%s' expected %s, got %s." % [pin_id, expected, actual_values])


## Read a persisted signature default without adopting in-progress editor text.
func _matrix_signature_saved(section: String, pin_index: int, expected: Variant) -> bool:
	var document: Dictionary = graph_script.call(&"get_graph")
	var signature: Dictionary = document[section][0].inputs[pin_index]
	var actual: Variant = signature.get("default")
	return _need(actual == expected, "Expected signature default %s, got %s." % [expected, actual])


## Exercise independent pending text in two signature defaults and declaration-scoped isolation.
func _matrix_signature_case(scenario: String) -> bool:
	var section := "macros" if scenario == "delete_and_history" else "functions"
	var first_pin := {"id": &"first", "label": "Input A", "type": TYPE_ARRAY, "default": ["a1", "a2", "a3"]}
	var second_pin := {"id": &"second", "label": "Input B", "type": TYPE_DICTIONARY}
	if scenario != "absent_default":
		second_pin["default"] = {"old": "r1", "other": "r2"}
		second_pin["default_key_order"] = ["old", "other"]
	var primary := {"id": 10, "name": "primary", "inputs": [first_pin, second_pin], "outputs": [],
		"graph": {"nodes": [], "connections": []}}
	var secondary := primary.duplicate(true)
	secondary.id = 20
	secondary.name = "secondary"
	secondary.inputs[0].default = ["new-a1", "new-a2", "new-a3"]
	secondary.inputs[1].default = {"new": "new-r1"}
	secondary.inputs[1].default_key_order = ["new"]
	var document := {"version": 1, "variables": [], "enums": [], "nodes": [], "connections": [], section: [primary, secondary]}
	graph_script.call(&"set_graph", document)
	await get_tree().process_frame
	if not await _select_declaration("primary") or not await _expand_details("VisualScriptInputsDisclosure"):
		return false
	var first_rows := _matrix_signature_rows("first")
	var second_rows := _matrix_signature_rows("second")
	if not _need(first_rows.size() == 3 and second_rows.size() == (0 if scenario == "absent_default" else 2),
			"Find the owned signature row fixtures."):
		return false
	if not await _type_line_draft(_matrix_field("array", first_rows[0], "value"), "draft-a1") or \
		not await _type_line_draft(_matrix_field("array", first_rows[2], "value"), "draft-a3"):
		return false
	if not second_rows.is_empty() and not await _type_line_draft(_matrix_field("dictionary", second_rows[0], "value"), "draft-r1"):
		return false
	var first_shown := ["draft-a1", "a2", "draft-a3"]
	var second_original := {"old": "r1", "other": "r2"}
	if scenario == "delete_and_history":
		var remove_button := first_rows[1].find_child("VisualScriptRemoveCollectionEntry", true, false) as Button
		if not await _matrix_press(remove_button):
			return false
		if not _matrix_signature_saved(section, 0, ["a1", "a3"]) or \
			not _matrix_signature_shown("first", ["draft-a1", "draft-a3"]) or not _matrix_signature_shown("second", ["draft-r1", "r2"]):
			return false
		await _declaration_key(KEY_Z, true)
		if not _matrix_signature_saved(section, 0, ["a1", "a2", "a3"]) or not _matrix_signature_shown("first", first_shown):
			return false
		await _declaration_key(KEY_Y, true)
		return _matrix_signature_saved(section, 0, ["a1", "a3"]) and \
			_matrix_signature_shown("first", ["draft-a1", "draft-a3"]) and _matrix_signature_shown("second", ["draft-r1", "r2"])
	if scenario == "declaration_scope":
		return await _select_declaration("secondary") and _matrix_signature_shown("first", ["new-a1", "new-a2", "new-a3"]) and \
			_matrix_signature_shown("second", ["new-r1"])
	var command_button: Button
	var second_editor := _matrix_signature_default("second")
	if scenario == "absent_default":
		command_button = second_editor.find_child("VisualScriptAddCollectionEntry", true, false) as Button
	else:
		for item: Node in second_editor.find_children("*", "Button", true, false):
			var candidate := item as Button
			if candidate.accessibility_name == "Clear default":
				command_button = candidate
				break
	if not await _matrix_press(command_button):
		return false
	var expected_new: Variant = {"key": ""} if scenario == "absent_default" else null
	var expected_old: Variant = null if scenario == "absent_default" else second_original
	if not _matrix_signature_saved(section, 1, expected_new) or not _matrix_signature_shown("first", first_shown):
		return false
	await _declaration_key(KEY_Z, true)
	if not _matrix_signature_saved(section, 1, expected_old) or not _matrix_signature_shown("first", first_shown):
		return false
	await _declaration_key(KEY_Y, true)
	return _matrix_signature_saved(section, 1, expected_new) and _matrix_signature_shown("first", first_shown)


## Reset each case independently so one regression cannot hide later interaction failures.
func _verify_collection_interaction_matrix() -> void:
	var groups := {
		"array": {"initial": ["a", "b", "c"], "cases": _matrix_array_cases()},
		"dictionary": {"initial": {"a": "A", "b": "B", "c": "C"}, "cases": _matrix_dictionary_cases()},
		"enum": {"initial": [["Open", 10], ["Busy", 20], ["Closed", 30]], "cases": _matrix_enum_cases()},
	}
	var passed_cases := 0
	var total_cases := 0
	for kind: String in groups:
		var group: Dictionary = groups[kind]
		var scenarios: Dictionary = group.cases
		for scenario: String in scenarios:
			var case_name := kind + "/" + scenario
			matrix_context = case_name + "/setup"
			total_cases += 1
			if not await _matrix_seed(kind, "Case" + str(total_cases), group.initial, ["a", "b", "c"]):
				continue
			var passed := true
			var steps: Array = scenarios[scenario]
			for step_index: int in range(steps.size()):
				var step: Array = steps[step_index]
				matrix_context = "%s/%d/%s" % [case_name, step_index + 1, step[0]]
				if not await _matrix_step(kind, step):
					passed = false
					break
			if passed:
				passed_cases += 1
				print("COLLECTION_MATRIX_PASS ", case_name)
	for scenario: String in ["delete_and_history", "absent_default", "clear_default", "declaration_scope"]:
		matrix_context = "signature/" + scenario
		total_cases += 1
		if await _matrix_signature_case(scenario):
			passed_cases += 1
			print("COLLECTION_MATRIX_PASS ", matrix_context)
	matrix_context = ""
	if passed_cases == total_cases:
		print("CRIMSON_COLLECTION_INTERACTION_MATRIX_PASSED ", passed_cases)
		get_tree().quit()
	else:
		push_error("Collection interaction matrix passed %d of %d cases." % [passed_cases, total_cases])
		get_tree().quit(1)


## Reject a second input addressed to a row from before an accepted mutation.
func _verify_buffered_collection_remove() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "buffered_routes", "pin": {"id": &"value", "label": "Value", "type": TYPE_ARRAY},
			"value": ["a", "b", "c"]},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("buffered_routes"):
		return false
	if not await _type_line_draft(_collection_cell(2, "VisualScriptCollectionValue"), "pending c"):
		return false
	var rows := _collection_rows()
	var first_remove := rows[0].find_child("VisualScriptRemoveCollectionEntry", true, false) as Button
	var second_remove := rows[1].find_child("VisualScriptRemoveCollectionEntry", true, false) as Button
	if not _need(first_remove != null and second_remove != null, "Find two distinct row actions."):
		return false
	first_remove.pressed.emit()
	second_remove.pressed.emit()
	await get_tree().process_frame
	var current: Dictionary = graph_script.call(&"get_graph")
	if not _need(current.variables[0].value == ["b", "c"] and
		_collection_cell(1, "VisualScriptCollectionValue").text == "pending c",
		"A stale row action cannot overwrite the accepted removal or surviving draft."):
		return false
	var next_remove := _collection_rows()[0].find_child("VisualScriptRemoveCollectionEntry", true, false) as Button
	if not _need(next_remove != null, "Find the new Details row after the redraw."):
		return false
	next_remove.pressed.emit()
	await get_tree().process_frame
	current = graph_script.call(&"get_graph")
	if not _need(current.variables[0].value == ["c"] and
		_collection_cell(0, "VisualScriptCollectionValue").text == "pending c",
		"The refreshed row can be edited after a stale action was rejected."):
		return false
	await _declaration_key(KEY_Z, true)
	current = graph_script.call(&"get_graph")
	return _need(current.variables[0].value == ["b", "c"], "Undo reverses only the last accepted row action.")


## Confirm an explicit reload discards pending text even when committed values already match disk.
func _verify_reload_discards_collection_draft() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "variables": [
		{"id": 1, "name": "reload_routes", "pin": {"id": &"value", "label": "Value", "type": TYPE_ARRAY},
			"value": ["west", "east"]},
	], "nodes": [], "connections": []})
	await get_tree().process_frame
	if not await _select_declaration("reload_routes"):
		return false
	var authoring_canvas := workspace.find_child("GraphCanvas", true, false) as GraphEdit
	var graph_tools := workspace.find_child("GraphTools", true, false) as MenuButton
	if not _need(authoring_canvas != null and graph_tools != null, "Find the canvas and Graph Tools."):
		return false
	authoring_canvas.grab_focus()
	var save_key := InputEventKey.new()
	save_key.keycode = KEY_S
	save_key.ctrl_pressed = true
	save_key.pressed = true
	workspace.get_window().push_input(save_key, true)
	await get_tree().process_frame
	var saved_document: Dictionary = graph_script.call(&"get_graph")
	var draft := _collection_cell(0, "VisualScriptCollectionValue")
	if not await _type_line_draft(draft, "unsubmitted detour"):
		return false
	graph_tools.show_popup()
	var tools_popup := graph_tools.get_popup()
	var reload_id := -1
	for menu_index: int in range(tools_popup.item_count):
		var operation: Variant = tools_popup.get_item_metadata(menu_index)
		if operation is Dictionary and operation.get("reload_graph", false):
			reload_id = tools_popup.get_item_id(menu_index)
			break
	if not _need(reload_id >= 0, "Graph Tools offers Reload from Disk."):
		return false
	tools_popup.id_pressed.emit(reload_id)
	tools_popup.hide()
	await get_tree().process_frame
	var reloaded_cell := _collection_cell(0, "VisualScriptCollectionValue")
	if not _need(reloaded_cell != null and reloaded_cell.text == "west" and
		graph_script.call(&"get_graph") == saved_document, "Reload restores the saved field even when only its draft differs."):
		return false
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": []})
	await get_tree().process_frame
	return true


## Pick an imported Godot script type and confirm the graph keeps its nominal identity.
func _verify_registered_script_type() -> bool:
	graph_script.call(&"set_graph", {"version": 1, "nodes": [], "connections": [], "variables": [
		{"id": 1, "name": "controller", "pin": {"id": &"value", "label": "Value", "type": TYPE_OBJECT, "class": &"Node"}, "value": null},
	]})
	await get_tree().process_frame
	if not await _select_declaration("controller") or not await _choose_type("CrimsonTestForeignController"):
		return false
	var graph_data: Dictionary = graph_script.call(&"get_graph")
	var pin: Dictionary = graph_data.variables[0].pin
	return _need(pin.type == TYPE_OBJECT and pin.class == &"Node" and
		pin.script_path == "res://tests/visual_scripting/foreign_controller.gd" and pin.script_name == "CrimsonTestForeignController",
		"The selected class_name retains its native base and the exact project Script.")


## Resize a populated Details view while a real field owns an unsubmitted draft.
func _verify_details_reflow() -> bool:
	var sidebar := workspace.find_child("VisualScriptRightSplit", true, false) as SplitContainer
	var split := sidebar.get_parent() as SplitContainer if sidebar != null else null
	var scroll_rail := workspace.find_child("VisualScriptDetailsScroll", true, false) as ScrollContainer
	var category := details_pane.find_child("VisualScriptFunctionCategory", true, false) as LineEdit
	if not _need(split != null and scroll_rail != null and category != null and category.is_visible_in_tree(),
			"Find the real inspector splitter and expanded function fields."):
		return false
	var initial_split := split.split_offset
	var initial_text := category.text
	var initial_document: Dictionary = graph_script.call(&"get_graph")
	var wide_offset := initial_split - ceili(split.size.x)
	split.split_offset = wide_offset
	await get_tree().process_frame
	await get_tree().process_frame
	var wide_width := sidebar.size.x
	if not await _type_line_draft(category, "Unsubmitted category"):
		return false
	split.split_offset = initial_split + ceili(split.size.x)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(sidebar.size.x < wide_width and category.has_focus() and category.text == "Unsubmitted category" and
			graph_script.call(&"get_graph") == initial_document, "Resizing keeps the same focused draft and saved graph."):
		return false
	var right_edge := scroll_rail.get_global_rect().end.x
	for candidate: Node in details_pane.find_children("*", "Control", true, false):
		var field := candidate as Control
		if field.get_viewport() == details_pane.get_viewport() and field.is_visible_in_tree() and (field is LineEdit or field is BaseButton):
			if not _need(field.get_global_rect().end.x <= right_edge + 1.0, "Narrow Details fits %s inside its viewport." % field.name):
				return false
	if not _need(not scroll_rail.get_h_scroll_bar().visible, "Narrow Details needs only vertical scrolling."):
		return false
	category.text = initial_text
	category.release_focus()
	split.split_offset = initial_split
	await get_tree().process_frame
	await get_tree().process_frame
	return true

## Verify the enum, collection, and function authoring workflows against the saved graph.
func _verify() -> void:
	await get_tree().process_frame
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await get_tree().process_frame
	await get_tree().process_frame
	var instance := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(instance != null and ResourceSaver.save(instance, SCRIPT_PATH) == OK, "Create editable graph"):
		return
	graph_script = ResourceLoader.load(SCRIPT_PATH, "Script") as Script
	EditorInterface.edit_script(graph_script, -1, 0, true)
	await get_tree().process_frame
	workspace = get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and workspace.is_visible_in_tree(), "Open the Visual Script workspace"):
		return
	await get_tree().process_frame
	outline_tree = workspace.find_child("VisualScriptDeclarations", true, false) as Tree
	details_pane = workspace.find_child("VisualScriptDetails", true, false) as Control
	var graph_canvas := workspace.find_child("GraphCanvas", true, false) as GraphEdit
	if not _need(outline_tree != null and details_pane != null and graph_canvas != null, "Find authoring panels"):
		return
	if "--collection-interaction-matrix-only" in OS.get_cmdline_user_args():
		await _verify_collection_interaction_matrix()
		return
	if not await _verify_reload_discards_collection_draft():
		return
	if not await _add_declaration("Enums") or not await _edit_text("VisualScriptDeclarationName", "DoorState"):
		return
	if not await _expand_details("VisualScriptMembersDisclosure"):
		return
	if not await _edit_text("VisualScriptEnumMemberName", "Locked") or not await _edit_text("VisualScriptEnumMemberValue", "-7"):
		return
	if not await _press("VisualScriptAddEnumMember"):
		return
	var state: Dictionary = graph_script.call(&"get_graph")
	if not _need(state.enums.size() == 1 and state.enums[0].name == "DoorState" and state.enums[0].entries.size() == 2 and state.enums[0].entries[0].value == -7, "Create enum with signed and distinct members"):
		return
	if not await _add_declaration("Variables") or not await _verify_quick_types() or not await _choose_type("DoorState"):
		return
	var enum_default := details_pane.find_child("VisualScriptVariableDefault", true, false) as Control
	if not _need(enum_default != null, "Choose named enum defaults using shared Select."):
		return
	var second_member: String = state.enums[0].entries[1].name
	var default_member := await _open_select(enum_default, second_member)
	if default_member == null:
		return
	default_member.pressed.emit()
	await get_tree().process_frame
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[0].pin.enum_type == &"GraphEnum.Enum_1" and state.variables[0].value == 0, "Enum variable stores identity and chosen integer"):
		return
	graph_canvas.popup_request.emit(Vector2(450, 220))
	await get_tree().process_frame
	var graph_filter := workspace.find_child("GraphPaletteSearch", true, false) as LineEdit
	var graph_results := workspace.find_child("GraphPaletteResults", true, false) as Tree
	var query := "DoorState"
	graph_filter.text = query
	graph_filter.text_changed.emit(query)
	var item := graph_results.get_selected()
	if not _need(item != null and item.get_text(0).begins_with("Switch on"), "Find authored enum switch by name"):
		return
	graph_filter.text_submitted.emit(query)
	await get_tree().process_frame
	state = graph_script.call(&"get_graph")
	if not _need(state.nodes.size() == 1 and state.nodes[0].kind == "switch_enum" and state.nodes[0].cases[0].id == &"case_member_1", "Place authored enum switch with stable case ID"):
		return
	var enum_switch := graph_canvas.get_node_or_null(^"GraphNode_1") as GraphNode
	var selection_field := enum_switch.find_child("Literal_value", true, false) as Control if enum_switch != null else null
	if not _need(selection_field != null, "Unwired Selection offers named enum values on the canvas."):
		return
	var second_option := await _open_select(selection_field, second_member)
	if second_option == null:
		return
	second_option.pressed.emit()
	await get_tree().process_frame
	var locked_option := await _open_select(selection_field, "Locked")
	if locked_option == null:
		return
	locked_option.pressed.emit()
	await get_tree().process_frame
	state = graph_script.call(&"get_graph")
	if not _need(state.nodes[0].values.get(&"value") == -7, "Inline enum selection stores the signed member value."):
		return
	if not await _select_declaration("DoorState") or not await _edit_text("VisualScriptEnumMemberName", "Sealed"):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.nodes[0].cases[0].label == "Sealed" and state.nodes[0].cases[0].id == &"case_member_1", "Renaming refreshes switch labels and preserves case IDs"):
		return
	var member_list := details_pane.find_child("VisualScriptEnumMembers", true, false) as VBoxContainer
	var second_grip := member_list.get_child(1).find_child("VisualScriptReorderEnumMember", true, false) as Button if member_list != null else null
	if not await _move_row(second_grip, KEY_UP):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.enums[0].entries[0].value == 0 and state.enums[0].entries[1].value == -7 and
		state.nodes[0].cases[1].id == &"case_member_1" and state.nodes[0].cases[1].label == "Sealed" and
		state.nodes[0].values.get(&"value") == -7, "Moving enum members preserves numeric values and stable switch cases."):
		return
	await _declaration_key(KEY_Z, true)
	state = graph_script.call(&"get_graph")
	if not _need(state.enums[0].entries[0].value == -7 and state.nodes[0].cases[0].id == &"case_member_1", "One Undo restores enum member and switch order."):
		return
	member_list = details_pane.find_child("VisualScriptEnumMembers", true, false) as VBoxContainer
	if not _need(member_list != null and member_list.get_child_count() == 2, "Find the reordered enum member editors."):
		return
	var first_grip := member_list.get_child(0).find_child("VisualScriptReorderEnumMember", true, false) as Button
	var last_member := member_list.get_child(1)
	if not await _drag_authored_row(first_grip, last_member.find_child("VisualScriptEnumMemberValue", true, false) as Control, true):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.enums[0].entries[0].value == 0 and state.nodes[0].cases[1].id == &"case_member_1",
		"Pointer drag reorders an enum over its integer editor and keeps switch case identity."):
		return
	await _declaration_key(KEY_Z, true)
	if not await _press("VisualScriptCreateEnumVariable"):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.variables.size() == 2 and state.variables[1].pin.enum_type == &"GraphEnum.Enum_1"
		and state.variables[1].value == -7, "Creating a variable from the enum assigns its type and first member in one edit."):
		return
	await _declaration_key(KEY_Z, true)
	if not await _add_declaration("Variables") or not await _choose_type("Variant") or not await _choose_type("Array"):
		return
	if not await _press("VisualScriptAddCollectionEntry") or not await _edit_text("VisualScriptCollectionValue", "first"):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[1].value == ["first"], "A row editor stores an Array value"):
		return
	if not await _press("VisualScriptAddCollectionEntry") or not await _edit_collection_cell(1, "VisualScriptCollectionValue", "second"):
		return
	if not await _move_collection_row(1, KEY_UP):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[1].value == ["second", "first"], "Reorder changes the actual Array values."):
		return
	if not await _drag_collection_row(0, 1):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[1].value == ["first", "second"], "Pointer drag reorders the Array over its editor."):
		return
	if not await _choose_container("Dictionary") or not await _press("VisualScriptAddCollectionEntry"):
		return
	if not await _edit_text("VisualScriptCollectionKey", "speed") or not await _edit_text("VisualScriptCollectionValue", "fast"):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[1].value == {"speed": "fast"}, "Separate key and value cells store a Dictionary pair"):
		return
	if not await _press("VisualScriptAddCollectionEntry") or not await _edit_collection_cell(1, "VisualScriptCollectionKey", "zone") or \
		not await _drag_collection_row(1, 0):
		return
	state = graph_script.call(&"get_graph")
	if not _need(state.variables[1].value == {"speed": "fast", "zone": ""} and state.variables[1].default_key_order == ["zone", "speed"],
		"Reorder changes the Dictionary display order and preserves its key-value mapping."):
		return
	var collection_path := "res://collection_order_probe.crimsonscript"
	if not _need(ResourceSaver.save(graph_script, collection_path) == OK, "Write reordered collection to a graph file."):
		return
	var collection_copy := ResourceLoader.load(collection_path, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(collection_copy != null, "Reopen the graph with reordered collection."):
		return
	var roundtrip: Dictionary = collection_copy.call(&"get_graph")
	if not _need(roundtrip.variables[1].default_key_order == ["zone", "speed"] and roundtrip.variables[1].value == state.variables[1].value,
		"Round trip retains the editor's deliberately unsorted keys and preserves the runtime Dictionary."):
		return
	state = graph_script.call(&"get_graph")
	state["functions"] = [{"id": 9, "name": "apply_effect", "inputs": [], "outputs": [], "graph": {"nodes": [
		{"id": 1, "kind": "function_entry", "definition": 9}, {"id": 2, "kind": "return", "definition": 9}
	], "connections": [{"from": 1, "from_pin": "then", "to": 2, "to_pin": "in"}]}}]
	state["nodes"] = [{"id": 4, "kind": "call_function", "definition": 9, "position": Vector2(80, 80)}]
	graph_script.call(&"set_graph", state)
	await get_tree().process_frame
	var function_card := graph_canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	if not _need(function_card != null, "Find placed function call"):
		return
	graph_canvas.set_selected(function_card)
	await get_tree().process_frame
	if not await _edit_text("VisualScriptDeclarationName", "apply_force") or not await _edit_text("VisualScriptFunctionDescription", "Moves a body"):
		return
	var settings_disclosure := details_pane.find_child("VisualScriptSettingsDisclosure", true, false) as Button
	var category_field := details_pane.find_child("VisualScriptFunctionCategory", true, false) as LineEdit
	var inputs_disclosure := details_pane.find_child("VisualScriptInputsDisclosure", true, false) as Button
	var outputs_disclosure := details_pane.find_child("VisualScriptOutputsDisclosure", true, false) as Button
	if not _need(settings_disclosure != null and not settings_disclosure.button_pressed and
		category_field != null and not category_field.is_visible_in_tree() and
		inputs_disclosure != null and not inputs_disclosure.button_pressed and
		outputs_disclosure != null and not outputs_disclosure.button_pressed,
		"Function Details start with Settings, Inputs and Outputs collapsed."):
		return
	if not await _expand_details("VisualScriptSettingsDisclosure") or not await _expand_details("VisualScriptInputsDisclosure") or \
		not await _expand_details("VisualScriptOutputsDisclosure"):
		return
	if not await _edit_text("VisualScriptFunctionCategory", "Physics") or not await _edit_text("VisualScriptFunctionKeywords", "push pull"):
		return
	if not await _edit_text("VisualScriptFunctionCompactTitle", "Force") or not await _press("VisualScriptAddInput") or not await _press("VisualScriptAddOutput"):
		return
	state = graph_script.call(&"get_graph")
	var definition: Dictionary = state.functions[0]
	if not _need(definition.name == "apply_force" and definition.description == "Moves a body" and definition.category == "Physics" and definition.keywords == "push pull" and definition.compact_title == "Force", "Editing the selected call persists all metadata"):
		return
	if not _need(definition.inputs.size() == 1 and definition.outputs.size() == 1 and state.nodes[0].definition == 9, "Editing signature from call preserves definition reference"):
		return
	if not await _verify_details_reflow():
		return
	function_card = graph_canvas.get_node_or_null(^"GraphNode_4") as GraphNode
	if not _need(function_card != null and function_card.title == "Force" and function_card.tooltip_text.contains("Moves a body"), "Call card displays Compact Title and Description"):
		return
	if not await _verify_declaration_commands(graph_canvas):
		return
	if not await _verify_wired_type_change() or not await _verify_typed_container_conversion() or not await _verify_native_enum_variable():
		return
	if not await _verify_array_cell_edits() or not await _verify_dictionary_cell_edits() or not await _verify_enum_cell_edits():
		return
	if not await _verify_asset_collection_details():
		return
	if not await _verify_numeric_defaults() or not await _verify_color_default():
		return
	if not await _verify_buffered_collection_remove():
		return
	if not await _verify_registered_script_type():
		return
	print("CRIMSON_AUTHORED_ENUM_COLLECTION_FUNCTION_PASSED")
	get_tree().quit()



