# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const GRAPH_PATH := "res://editor_node_actions.crimsonscript"
const TYPED_DICTIONARY_PATH := "res://editor_typed_dictionary.crimsonscript"
const RESOURCE_FIXTURE_PATH := "res://editor_node_actions_curve.tres"
const DROPPED_SCENE_PATH := "res://editor_node_actions_scene_drop.tscn"
const LOAD_FIXTURE_GRAPH := "res://editor_node_actions_load.crimsonscript"
const LOAD_FIXTURE_SCENE := "res://editor_node_actions_load.tscn"
const LOAD_SUCCESS_MESSAGE := "CRIMSON_EDITOR_AUTHORED_RESOURCE_LOADED"
const CONSTRUCTOR_SCRIPT_PATH := "res://editor_node_actions_constructor.gd"
const CONSTRUCTOR_GRAPH_PATH := "res://editor_node_actions_constructor.crimsonscript"
const CONSTRUCTOR_SCENE_PATH := "res://editor_node_actions_constructor.tscn"
const FOREIGN_SCRIPT_PATH := "res://editor_node_actions_foreign.gd"
const SCENE_AUTOLOAD_PATH := "res://editor_node_actions_autoload.tscn"
const ACTION_NAME := &"crimson_visual_action_probe"
const FOREIGN_CLASS := &"CrimsonVisualForeignProbe"
const REGISTERED_NAME := &"CrimsonVisualEditorService"
const SCRIPT_AUTOLOAD_NAME := &"CrimsonVisualScriptAutoload"
const SCENE_AUTOLOAD_NAME := &"CrimsonVisualSceneAutoload"
const PALETTE_POINT := Vector2(18, 20)

var authored_script: Script
var workspace: Control
var canvas: GraphEdit
var search_field: LineEdit
var results: Tree
var context_button: Button


## Start the fixture once Godot has entered its editor tree.
func _enter_tree() -> void:
	_verify.call_deferred()


## Emit the failed requirement in the editor harness log.
func _need(condition: bool, message: String) -> bool:
	if not condition:
		push_error("Visual Script node action fixture: " + message)
		get_tree().quit(1)
	return condition


## Attach a new standalone graph and run each authoring scenario against its real palette.
func _verify() -> void:
	await get_tree().process_frame
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await get_tree().process_frame
	var initial_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(initial_script != null and ResourceSaver.save(initial_script, GRAPH_PATH) == OK,
			"Create independent graph fixtures."):
		return
	authored_script = ResourceLoader.load(GRAPH_PATH, "Script") as Script
	EditorInterface.edit_script(authored_script, -1, 0, true)
	await get_tree().process_frame
	workspace = get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and workspace.is_visible_in_tree(), "Open the saved graph in Godot."):
		return
	canvas = workspace.find_child("GraphCanvas", true, false) as GraphEdit
	search_field = workspace.find_child("GraphPaletteSearch", true, false) as LineEdit
	results = workspace.find_child("GraphPaletteResults", true, false) as Tree
	context_button = workspace.find_child("GraphPaletteContextSensitive", true, false) as Button
	if not _need(canvas != null and search_field != null and results != null and context_button != null,
			"Open the real editable canvas and palette controls."):
		return
	if not await _delays_and_value_actions() or not await _sequence_actions() or not await _collection_actions() or not await _dictionary_inherited_pins() or \
			not await _dictionary_output_preservation() or not await _collection_type_propagation() or \
			not await _scoped_collection_projection() or not await _compatible_type_alternatives() or not await _resource_actions() or \
			not await _scene_drop_actions() or not await _load_resource_actions() or not await _script_constructor_actions() or \
			not await _cast_and_input_actions() or not await _singleton_actions():
		return
	print("CRIMSON_VISUAL_SCRIPT_EDITOR_NODE_ACTIONS_PASSED")
	get_tree().quit()


## Create stable node metadata without relying on an editable demo scene.
func _node(node_id: int, kind: StringName, fields: Dictionary = {}) -> Dictionary:
	var record := {"id": node_id, "kind": kind, "position": Vector2(20.0, 30.0 + node_id * 54.0)}
	record.merge(fields, true)
	return record


## Place authored fixture nodes and wait for graph rows and ports.
func _reset_graph(records: Array = [], edges: Array = []) -> void:
	authored_script.call(&"set_graph", {"version": 1, "nodes": records, "connections": edges})
	canvas.zoom = 1.0
	canvas.scroll_offset = Vector2.ZERO
	await get_tree().process_frame
	await get_tree().process_frame


## Read serialized node records after applying editor actions.
func _records() -> Array:
	var graph: Dictionary = authored_script.call(&"get_graph")
	return graph.nodes


## Locate a rendered graph card by its persisted ID.
func _card(node_id: int) -> GraphNode:
	return canvas.get_node_or_null("GraphNode_%d" % node_id) as GraphNode


## Return whether a graph edit produced the named connection.
func _wire(source_id: int, source_pin: StringName, target_id: int, target_pin: StringName) -> bool:
	var graph: Dictionary = authored_script.call(&"get_graph")
	for edge: Dictionary in graph.connections:
		if edge.from == source_id and edge.from_pin == source_pin and edge.to == target_id and edge.to_pin == target_pin:
			return true
	return false


## Filter the current palette through its public editor control signal.
func _query(text: String) -> void:
	search_field.text = text
	search_field.text_changed.emit(text)


## Select a visible result and let Godot dispatch the keyboard accept signal.
func _accept(label: String, kind: StringName) -> bool:
	_query(label)
	var hit := results.get_selected()
	var metadata: Dictionary = hit.get_metadata(0) if hit != null else {}
	if not _need(hit != null and hit.get_text(0) == label and metadata.get("kind") == kind,
			"Search selects %s as %s. Got %s." % [label, kind, hit.get_text(0) if hit != null else "no result"]):
		return false
	search_field.text_submitted.emit(label)
	await get_tree().process_frame
	await get_tree().process_frame
	return true


## Trigger the normal free palette and insert its selected node.
func _free(label: String, kind: StringName) -> bool:
	canvas.popup_request.emit(PALETTE_POINT)
	return await _accept(label, kind)


## Emit the same context signal as dragging a wire off its socket over empty space.
func _from_pin(node_id: int, port: int, outgoing: bool) -> void:
	var card := _card(node_id)
	if outgoing:
		canvas.connection_to_empty.emit(card.name, port, PALETTE_POINT)
	else:
		canvas.connection_from_empty.emit(card.name, port, PALETTE_POINT)


## Traverse matching rows by metadata instead of relying on a particular group position or style.
func _has_kind(tree_item: TreeItem, kind: StringName, label: String = "") -> bool:
	return _find_kind(tree_item, kind, label) != null


## Locate a result by its operation rather than by a fixed row number.
func _find_kind(tree_item: TreeItem, kind: StringName, label: String) -> TreeItem:
	if tree_item == null:
		return null
	var metadata: Variant = tree_item.get_metadata(0)
	if metadata is Dictionary and metadata.get("kind") == kind and (label.is_empty() or tree_item.get_text(0) == label):
		return tree_item
	for entry in tree_item.get_children():
		var matching_entry := _find_kind(entry, kind, label)
		if matching_entry != null:
			return matching_entry
	return null


## Find the active choice surface inside the graph workspace.
func _drop_choice_overlay() -> Control:
	var overlay := workspace.find_child("GraphDropChoiceOverlay", true, false) as Control
	return overlay if overlay != null and overlay.is_visible_in_tree() else null


## Find a drop action by its operation rather than its position or label.
func _drop_choice_button(kind: StringName) -> Button:
	var overlay := _drop_choice_overlay()
	if overlay != null:
		for choice_node: Node in overlay.find_children("*", "Button", true, false):
			var choice := choice_node as Button
			if choice.is_visible_in_tree() and choice.get_meta("drop_kind", &"") == kind:
				return choice
	return null


## Hover and click the drop choice through the editor viewport.
func _click_drop_action(label: String, kind: StringName) -> bool:
	var row := _drop_choice_button(kind)
	var input_window := workspace.get_window()
	if not _need(row != null and row.get_viewport() == input_window, "The graph offers %s." % label):
		return false
	var node_count := _records().size()
	var pointer := row.get_global_transform_with_canvas() * (row.size * 0.5)
	var motion := InputEventMouseMotion.new()
	motion.position = pointer
	motion.global_position = pointer
	input_window.push_input(motion, true)
	await get_tree().process_frame
	var hovered := input_window.gui_get_hovered_control()
	if not _need(hovered == row or (hovered != null and row.is_ancestor_of(hovered)),
			"The pointer reaches the %s button in the graph workspace." % label):
		return false
	for pressed: bool in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.position = pointer
		click.global_position = pointer
		click.pressed = pressed
		input_window.push_input(click, true)
		await get_tree().process_frame
	await get_tree().process_frame
	var nodes := _records()
	return _need(_drop_choice_overlay() == null and nodes.size() == node_count + 1 and nodes.back().get("kind") == kind,
			"Clicking %s closes the drop menu and adds exactly one node of that operation." % label)


## Cancel through the editor's keyboard input and preserve the document.
func _cancel_drop() -> bool:
	if not _need(_drop_choice_overlay() != null, "The drop menu is open before dismissal."):
		return false
	var before: Dictionary = authored_script.call(&"get_graph")
	for pressed: bool in [true, false]:
		var cancel_key := InputEventKey.new()
		cancel_key.keycode = KEY_ESCAPE
		cancel_key.pressed = pressed
		workspace.get_window().push_input(cancel_key, true)
		await get_tree().process_frame
	await get_tree().process_frame
	return _need(_drop_choice_overlay() == null and authored_script.call(&"get_graph") == before,
			"Pressing Escape dismisses the drop menu and leaves the graph unchanged.")


## Exercise native graph keyboard transactions while the canvas owns focus.
func _shortcut(code: Key, shift_held: bool = false) -> void:
	canvas.grab_focus()
	var event := InputEventKey.new()
	event.keycode = code
	event.ctrl_pressed = true
	event.shift_pressed = shift_held
	event.pressed = true
	workspace.get_window().push_input(event, true)
	await get_tree().process_frame
	await get_tree().process_frame


## Send the FileSystem drag payload through Godot's viewport and current pointer position.
func _drop_project_file(file_path: String, window_position: Vector2) -> void:
	var editor_window := workspace.get_window()
	canvas.force_drag({"type": "files", "files": PackedStringArray([file_path])}, null)
	var motion := InputEventMouseMotion.new()
	motion.position = window_position
	motion.global_position = window_position
	motion.button_mask = MOUSE_BUTTON_MASK_LEFT
	editor_window.push_input(motion, true)
	# force_drag already represents the press and drag. Deliver only the real drop release.
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.position = window_position
	release.global_position = window_position
	editor_window.push_input(release, true)
	await get_tree().process_frame
	await get_tree().process_frame


## Cover free suspension, native signal context, make/break data context, and a wrong-type output.
func _delays_and_value_actions() -> bool:
	await _reset_graph([_node(1, &"create_node", {"class": &"Timer"})])
	if not await _free("Delay", &"delay") or not await _free("Await Signal", &"await_signal"):
		return false
	_from_pin(1, 1, true)
	if not await _accept("Await Timeout", &"await_signal"):
		return false
	if not _need(_records()[3].signal == &"timeout" and _wire(1, &"node", 4, &"target"), "Typed signal awaits save and wire their receiver."):
		return false
	await _reset_graph([_node(1, &"break_value", {"value_type": TYPE_VECTOR3})])
	_from_pin(1, 0, false)
	if not await _accept("Make Vector3", &"make_value"):
		return false
	if not _need(_wire(2, &"value", 1, &"value"), "Make Vector3 connects to a typed input from the picker."):
		return false
	_from_pin(2, 0, true)
	if not await _accept("Break Vector3", &"break_value"):
		return false
	if not _need(_wire(2, &"value", 3, &"value"), "Break Vector3 accepts the authored output and preserves the first connection."):
		return false
	_from_pin(3, 0, true)
	_query("Break Vector3")
	if not _need(not _has_kind(results.get_root(), &"break_value"), "An incompatible scalar output hides Break Vector3 in context."):
		return false
	context_button.button_pressed = false
	if not _need(_has_kind(results.get_root(), &"break_value"), "Disabling context reveals the same action."):
		return false
	if not await _accept("Break Vector3", &"break_value"):
		return false
	if not _need(_records().size() == 4 and not _wire(3, &"x", 4, &"value"), "Incompatible free insertion never writes the invalid edge."):
		return false
	context_button.button_pressed = true
	return true


## Resolve the authored pin and invoke Remove Pin through its native context menu.
func _remove_graph_pin(node_id: int, pin_id: StringName, output: bool = false) -> bool:
	var card := _card(node_id)
	var socket := Vector2.INF
	var port_count: int = card.get_output_port_count() if output else card.get_input_port_count()
	for port_index: int in range(port_count):
		var descriptor: Dictionary = card.get_slot_metadata_right(card.get_output_port_slot(port_index)) if output else \
			card.get_slot_metadata_left(card.get_input_port_slot(port_index))
		if descriptor.get("id") == pin_id:
			socket = (card.get_output_port_position(port_index) if output else card.get_input_port_position(port_index)) * canvas.zoom
			break
	if not _need(socket.is_finite(), "Find the %s socket." % pin_id):
		return false
	var canvas_point := card.position + socket
	if not Rect2(Vector2.ZERO, canvas.size).grow(-16.0).has_point(canvas_point):
		canvas.scroll_offset += canvas_point - canvas.size * 0.5
		await get_tree().process_frame
		await get_tree().process_frame
	var point := canvas.get_global_transform_with_canvas() * (card.position + socket)
	for pressed_state: bool in [true, false]:
		var click_event := InputEventMouseButton.new()
		click_event.button_index = MOUSE_BUTTON_RIGHT
		click_event.button_mask = MOUSE_BUTTON_MASK_RIGHT if pressed_state else 0
		click_event.position = point
		click_event.global_position = point
		click_event.pressed = pressed_state
		workspace.get_window().push_input(click_event, true)
	await get_tree().process_frame
	var pin_menu := workspace.find_child("GraphContextMenu", true, false) as PopupMenu
	if not _need(pin_menu != null and pin_menu.visible, "Right-clicking a pin opens its context menu."):
		return false
	for item_index: int in range(pin_menu.item_count):
		if pin_menu.get_item_text(item_index) == "Remove Pin" and not pin_menu.is_item_disabled(item_index):
			pin_menu.id_pressed.emit(pin_menu.get_item_id(item_index))
			await get_tree().process_frame
			await get_tree().process_frame
			return _need(not pin_menu.visible, "Remove Pin closes the menu after applying its graph edit.")
	return _need(false, "The expandable pin offers Remove Pin.")


## Edit Sequence outputs on the card while preserving the existing execution route.
func _sequence_actions() -> bool:
	const SEQUENCE_ID := 1
	const FIRST_BRANCH_ID := 2
	const SECOND_BRANCH_ID := 3
	const THIRD_BRANCH_ID := 4
	const SEQUENCE_LIMIT := 64
	var sequence_record := _node(SEQUENCE_ID, &"sequence", {"position": Vector2(70.0, 32.0)})
	var branches: Array[Dictionary] = [
		_node(FIRST_BRANCH_ID, &"print", {"position": Vector2(390.0, 32.0), "message": "First"}),
		_node(SECOND_BRANCH_ID, &"print", {"position": Vector2(390.0, 160.0), "message": "Second"}),
		_node(THIRD_BRANCH_ID, &"print", {"position": Vector2(390.0, 288.0), "message": "Third"}),
	]
	await _reset_graph([sequence_record, branches[0], branches[1], branches[2]], [
		{"from": SEQUENCE_ID, "from_pin": &"then_0", "to": FIRST_BRANCH_ID, "to_pin": &"in"},
		{"from": SEQUENCE_ID, "from_pin": &"then_1", "to": SECOND_BRANCH_ID, "to_pin": &"in"},
	])
	var sequence_card := _card(SEQUENCE_ID)
	var add_output := sequence_card.find_child("SequenceAddPin", true, false) as Button
	if not _need(sequence_card.get_output_port_count() == 2 and add_output != null and not add_output.disabled,
			"A saved Sequence without an explicit count starts with two outputs and permits another."):
		return false
	canvas.set_selected(sequence_card)
	await get_tree().process_frame
	if not _need(workspace.find_child("VisualScriptSequenceCount", true, false) == null,
			"Selecting Sequence leaves output editing on the card."):
		return false
	add_output.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(_records()[0].count == 3 and _card(SEQUENCE_ID).get_output_port_count() == 3 and
			_wire(SEQUENCE_ID, &"then_0", FIRST_BRANCH_ID, &"in") and _wire(SEQUENCE_ID, &"then_1", SECOND_BRANCH_ID, &"in"),
			"Add Pin appends a Sequence output without altering existing wires."):
		return false
	await _shortcut(KEY_Z)
	if not _need(_card(SEQUENCE_ID).get_output_port_count() == 2 and not _records()[0].has("count"),
			"Undo restores the saved default Sequence shape."):
		return false
	await _shortcut(KEY_Y)
	canvas.connection_request.emit(_card(SEQUENCE_ID).name, 2, _card(THIRD_BRANCH_ID).name, 0)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(_wire(SEQUENCE_ID, &"then_2", THIRD_BRANCH_ID, &"in"), "Redo restores a connectable third output."):
		return false
	if not await _remove_graph_pin(SEQUENCE_ID, &"then_1", true):
		return false
	if not _need(_records()[0].count == 2 and _card(SEQUENCE_ID).get_output_port_count() == 2 and
			_wire(SEQUENCE_ID, &"then_0", FIRST_BRANCH_ID, &"in") and _wire(SEQUENCE_ID, &"then_1", THIRD_BRANCH_ID, &"in") and
			not _wire(SEQUENCE_ID, &"then_1", SECOND_BRANCH_ID, &"in"),
			"Removing a middle output drops its own wire and moves only later surviving branches."):
		return false
	await _shortcut(KEY_Z)
	if not _need(_records()[0].count == 3 and _wire(SEQUENCE_ID, &"then_1", SECOND_BRANCH_ID, &"in") and
			_wire(SEQUENCE_ID, &"then_2", THIRD_BRANCH_ID, &"in"), "Undo restores both reordered branch wires."):
		return false
	await _shortcut(KEY_Y)
	if not _need(_records()[0].count == 2 and _wire(SEQUENCE_ID, &"then_1", THIRD_BRANCH_ID, &"in"),
			"Redo restores the same surviving Sequence order."):
		return false
	sequence_record["count"] = SEQUENCE_LIMIT
	await _reset_graph([sequence_record])
	add_output = _card(SEQUENCE_ID).find_child("SequenceAddPin", true, false) as Button
	return _need(add_output != null and add_output.disabled, "The last supported output disables Add Pin.")


## Type a literal through the native focused field and its graph callbacks.
func _type_pin_text(field: LineEdit, literal_text: String) -> void:
	field.grab_focus()
	for character: String in literal_text:
		var typed_key := InputEventKey.new()
		typed_key.unicode = character.unicode_at(0)
		typed_key.pressed = true
		workspace.get_window().push_input(typed_key, true)
	await get_tree().process_frame


## Read the slot's assigned type used by graph wiring and inline editors.
func _constructor_input_type(card: GraphNode, pin_id: StringName) -> int:
	for port_index: int in range(card.get_input_port_count()):
		var descriptor: Dictionary = card.get_slot_metadata_left(card.get_input_port_slot(port_index))
		if descriptor.get("id") == pin_id:
			return descriptor.get("type", TYPE_NIL)
	return -1


## Cover the compact Self card, collection editing, and typed array lookup.
func _collection_actions() -> bool:
	await _reset_graph()
	if not await _free("Self", &"self"):
		return false
	var self_card := _card(1)
	var self_caption := self_card.find_child("GraphSelfName", true, false) as Label
	if not _need(self_caption != null and self_caption.text == "Self" and self_card.get_input_port_count() == 0 and
			self_card.get_output_port_count() == 1 and self_caption.tooltip_text.contains("Node"),
			"Self uses the compact caption and keeps its owner type on the output."):
		return false
	await _reset_graph()
	if not await _free("Make Array", &"make_array") or not await _free("Make Dictionary", &"make_dictionary"):
		return false
	var array_card := _card(1)
	var array_slot := array_card.get_input_port_slot(0)
	var array_output_slot := array_card.get_output_port_slot(0)
	var untyped_color := array_card.get_slot_color_left(array_slot)
	var item_field := array_card.find_child("Literal_item_1", true, false) as Control
	var item_text := item_field.find_children("*", "LineEdit", true, false).front() as LineEdit
	await _type_pin_text(item_text, "door")
	if not _need(_card(1) == array_card and item_text.has_focus() and _records()[0].values.item_1 == "door" and
			array_card.get_slot_color_left(array_slot) != untyped_color and
			array_card.get_slot_color_right(array_output_slot) == array_card.get_slot_color_left(array_slot),
			"A first literal tints the array input and output without interrupting typing."):
		return false
	for node_id: int in [1, 2]:
		var add_entry := _card(node_id).find_child("CollectionAddPin", true, false) as Button
		if not _need(add_entry != null and add_entry.text == "Add Pin", "Both constructors expose Add Pin."):
			return false
		add_entry.pressed.emit()
		await get_tree().process_frame
		await get_tree().process_frame
		if not _need(_records()[node_id - 1].get("collection_size") == 2, "Adding one entry persists the exact constructor arity."):
			return false
		await _shortcut(KEY_Z)
		if not _need(_records()[node_id - 1].get("collection_size", 1) == 1, "Undo restores the original constructor shape."):
			return false
		await _shortcut(KEY_Y)
		if not _need(_records()[node_id - 1].get("collection_size") == 2, "Redo restores the added entry."):
			return false
	_from_pin(1, 0, true)
	if not await _accept("Get Array Item", &"get_array_item"):
		return false
	if not _need(_wire(1, &"value", 3, &"array"), "Array lookup connects the collection and never selects its integer pin."):
		return false
	var producer := _node(3, &"literal", {"global_constant": &"KEY_ESCAPE", "position": Vector2(24.0, 32.0)})
	var collection := _node(4, &"make_array", {"collection_size": 3, "position": Vector2(250.0, 32.0),
		"values": {&"item_1": "first", &"item_2": "remove", &"item_3": "last"}})
	await _reset_graph([producer, collection], [
		{"from": 3, "from_pin": &"result", "to": 4, "to_pin": &"item_2"},
		{"from": 3, "from_pin": &"result", "to": 4, "to_pin": &"item_3"},
	])
	if not await _remove_graph_pin(4, &"item_2"):
		return false
	if not _need(_records()[1].collection_size == 2 and _records()[1].values == {&"item_1": "first", &"item_2": "last"} and
			_wire(3, &"result", 4, &"item_2") and not _wire(3, &"result", 4, &"item_3"),
			"Removing a wired middle item preserves and renumbers the remaining values and wire."):
		return false
	await _shortcut(KEY_Z)
	if not _need(_records()[1].values == collection.values and _wire(3, &"result", 4, &"item_2") and
			_wire(3, &"result", 4, &"item_3"), "Undo restores the removed item, values, and both wires."):
		return false
	await _shortcut(KEY_Y)
	if not _need(_records()[1].collection_size == 2 and _records()[1].values.item_2 == "last" and
			not _wire(3, &"result", 4, &"item_3"), "Redo removes the same item and restores the renumbering."):
		return false
	var dictionary := _node(4, &"make_dictionary", {"collection_size": 3, "position": Vector2(250.0, 32.0),
		"values": {&"key_1": "first", &"value_1": true, &"key_2": "remove", &"value_2": false,
			&"key_3": "last", &"value_3": true}})
	await _reset_graph([producer, dictionary], [
		{"from": 3, "from_pin": &"result", "to": 4, "to_pin": &"key_2"},
		{"from": 3, "from_pin": &"result", "to": 4, "to_pin": &"value_2"},
		{"from": 3, "from_pin": &"result", "to": 4, "to_pin": &"value_3"},
	])
	if not await _remove_graph_pin(4, &"value_2"):
		return false
	var retained_values := {&"key_1": "first", &"value_1": true, &"key_2": "last", &"value_2": true}
	if not _need(_records()[1].collection_size == 2 and _records()[1].values == retained_values and
			not _wire(3, &"result", 4, &"key_2") and _wire(3, &"result", 4, &"value_2") and
			not _wire(3, &"result", 4, &"value_3"),
			"Removing a dictionary value pin removes its pair and shifts both lanes of the surviving pair."):
		return false
	var add_legacy_pin := _card(4).find_child("CollectionAddPin", true, false) as Button
	add_legacy_pin.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	return _need(_records()[1].collection_size == 3 and _records()[1].values == retained_values and
			_wire(3, &"result", 4, &"value_2") and _constructor_input_type(_card(4), &"value_3") == TYPE_NIL,
			"A constructor with a legacy mixed value lane keeps its values and wires when adding a pair.")


## Verify lane acquisition, inherited fields, incompatible wires, and a saved constructor shape.
func _dictionary_inherited_pins() -> bool:
	await _reset_graph()
	if not await _free("Make Dictionary", &"make_dictionary"):
		return false
	var dictionary_card := _card(1)
	var first_key := dictionary_card.find_child("Literal_key_1", true, false) as Control
	var first_key_text := first_key.find_children("*", "LineEdit", true, false).front() as LineEdit
	await _type_pin_text(first_key_text, "wood")
	if not _need(_card(1) == dictionary_card and first_key_text.has_focus() and
			_constructor_input_type(dictionary_card, &"key_1") == TYPE_STRING,
			"A first key locks its lane while the author keeps typing in the same field."):
		return false
	var add_pin := dictionary_card.find_child("CollectionAddPin", true, false) as Button
	add_pin.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	dictionary_card = _card(1)
	if not _need(_constructor_input_type(dictionary_card, &"key_2") == TYPE_STRING and
			_constructor_input_type(dictionary_card, &"value_2") == TYPE_NIL and _records()[0].values == {&"key_1": "wood"},
			"An added pair inherits the known key type, keeps the value open, and authors no literals."):
		return false
	var dictionary: Dictionary = _records()[0]
	var integer_source := _node(2, &"literal", {"global_constant": &"KEY_ESCAPE", "position": Vector2(24.0, 260.0)})
	var object_source := _node(3, &"self", {"position": Vector2(24.0, 365.0)})
	await _reset_graph([dictionary, integer_source, object_source])
	canvas.connection_request.emit(_card(2).name, 0, _card(1).name, 1)
	await get_tree().process_frame
	await get_tree().process_frame
	dictionary_card = _card(1)
	if not _need(_wire(2, &"result", 1, &"value_1") and _constructor_input_type(dictionary_card, &"value_2") == TYPE_INT,
			"Wiring the first integer value sets the independently typed value lane."):
		return false
	add_pin = dictionary_card.find_child("CollectionAddPin", true, false) as Button
	add_pin.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	dictionary_card = _card(1)
	if not _need(_constructor_input_type(dictionary_card, &"key_3") == TYPE_STRING and
			_constructor_input_type(dictionary_card, &"value_3") == TYPE_INT,
			"A later pair inherits both previously established types."):
		return false
	await _shortcut(KEY_Z)
	if not _need(_records()[0].collection_size == 2 and _constructor_input_type(_card(1), &"value_2") == TYPE_INT,
			"Undo removes the last pair and retains the value lane type."):
		return false
	await _shortcut(KEY_Y)
	dictionary_card = _card(1)
	canvas.connection_request.emit(_card(3).name, 0, dictionary_card.name, 3)
	await get_tree().process_frame
	if not _need(not _wire(3, &"node", 1, &"value_2"), "The typed integer pin rejects a Self wire."):
		return false
	var trailing_value := dictionary_card.find_child("Literal_value_2", true, false) as Control
	var trailing_value_text := trailing_value.find_children("*", "LineEdit", true, false).front() as LineEdit
	await _type_pin_text(trailing_value_text, "27")
	var authored_value: Variant = _records()[0].values.get(&"value_2")
	if not _need(typeof(authored_value) == TYPE_INT and authored_value == 27,
			"The inherited numeric editor saves the value as an integer."):
		return false
	if not _need(ResourceSaver.save(authored_script, TYPED_DICTIONARY_PATH) == OK, "Save the typed collection constructor."):
		return false
	var reloaded := ResourceLoader.load(TYPED_DICTIONARY_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(reloaded != null, "Reload the typed collection constructor."):
		return false
	var saved_graph: Dictionary = reloaded.call(&"get_graph")
	await _reset_graph(saved_graph.nodes, saved_graph.connections)
	if not _need(_constructor_input_type(_card(1), &"key_3") == TYPE_STRING and
			_constructor_input_type(_card(1), &"value_3") == TYPE_INT and _records()[0].values.value_2 == 27,
			"Reload restores both lane types on unfilled pins and the authored integer."):
		return false
	var existing_literals := {&"key_1": "granite", &"value_1": 16}
	await _reset_graph([_node(1, &"make_dictionary", {"values": existing_literals})])
	add_pin = _card(1).find_child("CollectionAddPin", true, false) as Button
	add_pin.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	return _need(_constructor_input_type(_card(1), &"key_2") == TYPE_STRING and
			_constructor_input_type(_card(1), &"value_2") == TYPE_INT and _records()[0].values == existing_literals,
			"Add Pin also adopts the types on an existing homogeneous dictionary card.")


## Reject first typing or wiring that would drop a partially typed Dictionary's existing output.
func _dictionary_output_preservation() -> bool:
	var dictionary_card := _node(1, &"make_dictionary", {
		"dictionary_types": {"key": {"type": TYPE_STRING}},
		"values": {&"key_1": "granite"},
	})
	var typed_setter := _node(2, &"set_variable", {"variable": 1, "position": Vector2(325.0, 120.0)})
	var integer_source := _node(3, &"literal", {"global_constant": &"KEY_ESCAPE", "position": Vector2(24.0, 300.0)})
	var typed_variable := {"id": 1, "name": &"materials", "pin": {
		"id": &"value", "label": "Value", "type": TYPE_DICTIONARY,
		"key": {"type": TYPE_STRING}, "value": {"type": TYPE_NIL},
	}, "value": {}}
	authored_script.call(&"set_graph", {"version": 1, "nodes": [dictionary_card, typed_setter, integer_source],
		"connections": [], "variables": [typed_variable]})
	await get_tree().process_frame
	await get_tree().process_frame
	canvas.connection_request.emit(_card(1).name, 0, _card(2).name, 1)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(_wire(1, &"value", 2, &"value"), "An existing partially typed dictionary output connects to a matching variable."):
		return false
	var before: Dictionary = authored_script.call(&"get_graph")
	canvas.connection_request.emit(_card(3).name, 0, _card(1).name, 1)
	await get_tree().process_frame
	var dismiss := _visible_button_named(workspace, "ConfirmDialog")
	if not _need(dismiss != null and authored_script.call(&"get_graph") == before,
			"The first integer wire is refused without changing the existing output or saved schema."):
		return false
	dismiss.pressed.emit()
	await get_tree().process_frame
	var value_field := _card(1).find_child("Literal_value_1", true, false) as Control
	var value_text := value_field.find_children("*", "LineEdit", true, false).front() as LineEdit
	await _type_pin_text(value_text, "x")
	dismiss = _visible_button_named(workspace, "ConfirmDialog")
	if not _need(dismiss != null and value_text.text.is_empty() and authored_script.call(&"get_graph") == before,
			"The first literal is refused, the field reverts, and the existing output remains attached."):
		return false
	dismiss.pressed.emit()
	await get_tree().process_frame
	canvas.disconnection_request.emit(_card(1).name, 0, _card(2).name, 1)
	await get_tree().process_frame
	await get_tree().process_frame
	value_field = _card(1).find_child("Literal_value_1", true, false) as Control
	value_text = value_field.find_children("*", "LineEdit", true, false).front() as LineEdit
	await _type_pin_text(value_text, "note")
	return _need(not _wire(1, &"value", 2, &"value") and _records()[0].values.value_1 == "note" and
			_constructor_input_type(_card(1), &"value_1") == TYPE_STRING,
			"Disconnecting the old output lets the new literal establish the value lane.")


## Find a native graph port by its persisted ID so fixtures do not depend on slot order.
func _typed_port(node_id: int, pin_id: StringName, output: bool) -> int:
	var card := _card(node_id)
	if card == null:
		return -1
	var port_count := card.get_output_port_count() if output else card.get_input_port_count()
	for port_index: int in range(port_count):
		var slot := card.get_output_port_slot(port_index) if output else card.get_input_port_slot(port_index)
		var descriptor: Dictionary = card.get_slot_metadata_right(slot) if output else card.get_slot_metadata_left(slot)
		if descriptor.get("id") == pin_id:
			return port_index
	return -1


## Inspect the editor descriptor used for pin colors, tooltips, and the contextual picker.
func _resolved_pin(node_id: int, pin_id: StringName, output: bool) -> Dictionary:
	var port_index := _typed_port(node_id, pin_id, output)
	if port_index < 0:
		return {}
	var card := _card(node_id)
	var slot := card.get_output_port_slot(port_index) if output else card.get_input_port_slot(port_index)
	var presentation_key := &"crimson_output_presentation" if output else &"crimson_input_presentation"
	var port_contract: Dictionary = card.get_slot_metadata_right(slot) if output else card.get_slot_metadata_left(slot)
	return card.get_child(slot).get_meta(presentation_key, port_contract)


## Send the same connection request Godot emits after a wire drag.
func _connect_typed_ports(from_id: int, from_pin: StringName, to_id: int, to_pin: StringName) -> bool:
	var from_port := _typed_port(from_id, from_pin, true)
	var to_port := _typed_port(to_id, to_pin, false)
	if not _need(from_port >= 0 and to_port >= 0, "Find both named endpoints before wiring %s to %s." % [from_pin, to_pin]):
		return false
	canvas.connection_request.emit(_card(from_id).name, from_port, _card(to_id).name, to_port)
	await get_tree().process_frame
	await get_tree().process_frame
	return _need(_wire(from_id, from_pin, to_id, to_pin), "Keep the requested wire from %s to %s." % [from_pin, to_pin])


## Fill the constructor after connecting its consumers, then check each dependent pin and Undo/Redo.
func _collection_type_propagation() -> bool:
	const KEY_SOURCE := 1
	const VALUE_SOURCE := 2
	const CONSTRUCTOR := 3
	const LOOP := 4
	const REROUTE := 5
	const LOOKUP := 6
	await _reset_graph([
		_node(KEY_SOURCE, &"literal", {"pin": {"type": TYPE_STRING}, "values": {&"value": "entrance"}}),
		_node(VALUE_SOURCE, &"make_value", {"value_type": TYPE_VECTOR3}),
		_node(CONSTRUCTOR, &"make_dictionary"),
		_node(LOOP, &"foreach"),
		_node(REROUTE, &"reroute", {"pin": {"type": TYPE_DICTIONARY}}),
		_node(LOOKUP, &"get_dictionary_value"),
	], [
		{"from": CONSTRUCTOR, "from_pin": &"value", "to": LOOP, "to_pin": &"array"},
		{"from": CONSTRUCTOR, "from_pin": &"value", "to": REROUTE, "to_pin": &"value"},
		{"from": REROUTE, "from_pin": &"value", "to": LOOKUP, "to_pin": &"dictionary"},
	])
	if not _need(_resolved_pin(LOOP, &"array", false).get("type") == TYPE_DICTIONARY and
			_resolved_pin(LOOP, &"key", true).get("type") == TYPE_NIL and
			_resolved_pin(LOOP, &"item", true).get("type") == TYPE_NIL,
			"For Each knows the empty constructor's collection kind and leaves both unknown lanes gray."):
		return false
	if not await _connect_typed_ports(KEY_SOURCE, &"result", CONSTRUCTOR, &"key_1"):
		return false
	if not _need(_resolved_pin(LOOP, &"key", true).get("type") == TYPE_STRING and
			_resolved_pin(LOOP, &"item", true).get("type") == TYPE_NIL,
			"Wiring the first key types downstream Key while Item remains open."):
		return false
	if not await _connect_typed_ports(VALUE_SOURCE, &"value", CONSTRUCTOR, &"value_1"):
		return false
	var constructor_pin := _resolved_pin(CONSTRUCTOR, &"value", true)
	var loop_pin := _resolved_pin(LOOP, &"array", false)
	var loop_key: Dictionary = loop_pin.get("key", {})
	var loop_value: Dictionary = loop_pin.get("value", {})
	var constructor_key: Dictionary = constructor_pin.get("key", {})
	var constructor_value: Dictionary = constructor_pin.get("value", {})
	if not _need(not _records()[CONSTRUCTOR - 1].has("dictionary_types") and
			loop_key.get("type") == TYPE_STRING and loop_value.get("type") == TYPE_VECTOR3 and
			constructor_key.get("type") == TYPE_STRING and constructor_value.get("type") == TYPE_VECTOR3 and
			_resolved_pin(LOOP, &"item", true).get("type") == TYPE_VECTOR3,
			"For Each shows both legacy constructor lanes without saving a new schema."):
		return false
	var rerouted_value: Dictionary = _resolved_pin(REROUTE, &"value", true).get("value", {})
	if not _need(rerouted_value.get("type") == TYPE_VECTOR3 and
			_resolved_pin(LOOKUP, &"key", false).get("type") == TYPE_STRING and
			_resolved_pin(LOOKUP, &"value", true).get("type") == TYPE_VECTOR3,
			"A reroute and lookup carry the live key and value types beyond the constructor."):
		return false
	await _shortcut(KEY_Z)
	if not _need(_resolved_pin(LOOP, &"item", true).get("type") == TYPE_NIL and
			_resolved_pin(LOOKUP, &"value", true).get("type") == TYPE_NIL and
			_resolved_pin(LOOKUP, &"key", false).get("type") == TYPE_STRING,
			"Undoing the value wire returns dependent outputs to Variant while preserving Key."):
		return false
	await _shortcut(KEY_Y)
	var lookup_value_port := _typed_port(LOOKUP, &"value", true)
	if not _need(lookup_value_port >= 0 and _resolved_pin(LOOKUP, &"value", true).get("type") == TYPE_VECTOR3,
			"Redo restores the dependent typed value after a full graph refresh."):
		return false
	_from_pin(LOOKUP, lookup_value_port, true)
	if not await _accept("Break Vector3", &"break_value"):
		return false
	if not _need(_wire(LOOKUP, &"value", LOOKUP + 1, &"value"),
			"The context-sensitive picker connects Break Vector3 to the inferred value."):
		return false
	const RELOAD_PATH := "res://editor_node_actions_adaptive_pins.crimsonscript"
	if not _need(ResourceSaver.save(authored_script, RELOAD_PATH) == OK, "Save the legacy constructor and its consumers."):
		return false
	var reloaded := ResourceLoader.load(RELOAD_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(reloaded != null, "Reload the graph with the legacy constructor."):
		return false
	var saved_graph: Dictionary = reloaded.call(&"get_graph")
	await _reset_graph(saved_graph.nodes, saved_graph.connections)
	return _need(not _records()[CONSTRUCTOR - 1].has("dictionary_types") and
			_resolved_pin(LOOKUP, &"value", true).get("type") == TYPE_VECTOR3 and _wire(LOOKUP, &"value", LOOKUP + 1, &"value"),
			"Reload recomputes the inferred value and retains the typed consumer wire without saving a new schema.")


## Use identical node and variable IDs in two graph bodies to catch scope-blind pin resolution.
func _scoped_collection_projection() -> bool:
	const SCOPED_FUNCTION := &"resolve_local_scene"
	const SOURCE_ID := 1
	const LOOKUP_ID := 2
	const VARIABLE_ID := 1
	var lookup := _node(LOOKUP_ID, &"get_array_item")
	var connection := {"from": SOURCE_ID, "from_pin": &"value", "to": LOOKUP_ID, "to_pin": &"array"}
	var root_variable := {"id": VARIABLE_ID, "name": &"message_ids", "value": [],
		"pin": {"type": TYPE_ARRAY, "element": {"type": TYPE_INT}}}
	var scoped_variable := {"id": VARIABLE_ID, "name": &"rooms", "value": [],
		"pin": {"type": TYPE_ARRAY, "element": {"type": TYPE_OBJECT, "class": &"PackedScene"}}}
	authored_script.call(&"set_graph", {"version": 1,
		"nodes": [_node(SOURCE_ID, &"get_variable", {"variable": VARIABLE_ID}), lookup],
		"connections": [connection], "variables": [root_variable],
		"functions": [{"id": 1, "name": SCOPED_FUNCTION, "inputs": [], "outputs": [], "graph": {
			"nodes": [_node(SOURCE_ID, &"get_variable", {"variable": VARIABLE_ID, "scope": &"local"}), lookup],
			"connections": [connection], "locals": [scoped_variable],
		}}],
	})
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(_resolved_pin(LOOKUP_ID, &"value", true).get("type") == TYPE_INT,
			"The Event Graph lookup uses the instance Array's integer element."):
		return false
	var outline := workspace.find_child("VisualScriptDeclarations", true, false) as Tree
	var function_row: TreeItem
	if outline != null and outline.get_root() != null:
		for group_row: TreeItem in outline.get_root().get_children():
			for declaration_row: TreeItem in group_row.get_children():
				if declaration_row.get_text(0) == String(SCOPED_FUNCTION):
					function_row = declaration_row
	if not _need(function_row != null, "Find the function declaration in the real graph sidebar."):
		return false
	function_row.select(0)
	outline.item_activated.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	var local_value := _resolved_pin(LOOKUP_ID, &"value", true)
	if not _need(local_value.get("type") == TYPE_OBJECT and local_value.get("class") == &"PackedScene",
			"The same lookup ID in the function resolves to its local PackedScene type."):
		return false
	var local_value_port := _typed_port(LOOKUP_ID, &"value", true)
	_from_pin(LOOKUP_ID, local_value_port, true)
	if not await _accept("Instantiate", &"call_method"):
		return false
	var document: Dictionary = authored_script.call(&"get_graph")
	var function_graph: Dictionary = document.functions[0].graph
	var consumer_wire := false
	for edge: Dictionary in function_graph.connections:
		if edge.from == LOOKUP_ID and edge.from_pin == &"value" and edge.to_pin == &"target":
			consumer_wire = true
			break
	if not _need(consumer_wire and document.connections.size() == 1,
			"The picker wires PackedScene.instantiate in the function body only."):
		return false
	var event_root := workspace.find_child("GraphEventRoot", true, false) as Button
	if not _need(event_root != null and not event_root.disabled, "Find the Event Graph breadcrumb."):
		return false
	event_root.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	return _need(_resolved_pin(LOOKUP_ID, &"value", true).get("type") == TYPE_INT,
			"Returning to the Event Graph restores its independent inferred pin type.")


## Merge matching Select branches while leaving conflicting branches and mixed legacy elements open.
func _compatible_type_alternatives() -> bool:
	const TEXT_SOURCE := 1
	const INTEGER_SOURCE := 2
	const SELECTOR := 3
	const MIXED_ARRAY := 4
	const LOOP := 5
	await _reset_graph([
		_node(TEXT_SOURCE, &"literal", {"pin": {"type": TYPE_STRING}, "values": {&"value": "lantern"}}),
		_node(INTEGER_SOURCE, &"literal", {"global_constant": &"KEY_ESCAPE"}),
		_node(SELECTOR, &"select"),
		_node(MIXED_ARRAY, &"make_array", {"collection_size": 3,
			"values": {&"item_1": "hand lamp", &"item_2": 12}}),
		_node(LOOP, &"foreach"),
	], [
		{"from": TEXT_SOURCE, "from_pin": &"result", "to": SELECTOR, "to_pin": &"true"},
		{"from": TEXT_SOURCE, "from_pin": &"result", "to": SELECTOR, "to_pin": &"false"},
		{"from": MIXED_ARRAY, "from_pin": &"value", "to": LOOP, "to_pin": &"array"},
	])
	var element_pin: Dictionary = _resolved_pin(MIXED_ARRAY, &"value", true).get("element", {})
	if not _need(_resolved_pin(SELECTOR, &"result", true).get("type") == TYPE_STRING and
			element_pin.get("type", TYPE_NIL) == TYPE_NIL and _resolved_pin(LOOP, &"item", true).get("type") == TYPE_NIL,
			"Matching Select branches resolve to String. Mixed and unset legacy Array entries leave Item open."):
		return false
	if not await _connect_typed_ports(INTEGER_SOURCE, &"result", SELECTOR, &"false"):
		return false
	return _need(_resolved_pin(SELECTOR, &"result", true).get("type") == TYPE_NIL,
			"Replacing one Select branch with a different concrete type returns Result to Variant.")


## Select a generated resource, use its reflected path getter, and verify graph save.
func _resource_actions() -> bool:
	var asset_result := ResourceSaver.save(Curve.new(), RESOURCE_FIXTURE_PATH)
	if not _need(asset_result == OK, "Create a standalone resource to preload."):
		return false
	EditorInterface.get_resource_filesystem().update_file(RESOURCE_FIXTURE_PATH)
	await _reset_graph()
	if not await _free("Preload Resource", &"preload_resource"):
		return false
	var field := _card(1).find_child("GraphAssetName", true, false) as Button
	if not _need(field != null and _card(1).get_input_port_count() == 0, "Preload has a resource field and no misleading input socket."):
		return false
	var target := field.get_global_transform_with_canvas() * (field.size * 0.5)
	await _drop_project_file(RESOURCE_FIXTURE_PATH, target)
	if not _need(_records()[0].get("values", {}).has(&"asset") and _records()[0].get("asset_class") == &"Curve",
			"Dropping the generated asset selects its preload resource and refines the output to Curve."):
		return false
	_from_pin(1, 0, true)
	if not await _accept("Get Resource Path", &"get_property"):
		return false
	if not _need(_wire(1, &"value", 2, &"target"), "Get Resource Path connects the preloaded reference."):
		return false
	await _shortcut(KEY_S)
	if not _need(FileAccess.get_file_as_string(GRAPH_PATH).contains(RESOURCE_FIXTURE_PATH), "Saving persists the authored resource input."):
		return false
	var reloaded := ResourceLoader.load(GRAPH_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	var reopened: Dictionary = reloaded.call(&"get_graph")
	return _need(reopened.nodes[0].asset_class == &"Curve" and reopened.connections.size() == 1,
			"Reloading from disk preserves the selected concrete asset type and its connection.")


## Choose both actions from the PackedScene drop popup with ordinary mouse activation.
func _scene_drop_actions() -> bool:
	var scene_root := Node3D.new()
	var scene := PackedScene.new()
	var packed := scene.pack(scene_root)
	scene_root.free()
	if not _need(packed == OK and ResourceSaver.save(scene, DROPPED_SCENE_PATH) == OK,
			"Create an independent PackedScene for drop actions."):
		return false
	EditorInterface.get_resource_filesystem().update_file(DROPPED_SCENE_PATH)
	for label: String in ["Preload Resource", "Instantiate"]:
		await _reset_graph()
		var target := canvas.get_global_transform_with_canvas() * (canvas.size * 0.5)
		await _drop_project_file(DROPPED_SCENE_PATH, target)
		if not _need(_drop_choice_button(&"preload_resource") != null and _drop_choice_button(&"call_method") != null,
				"A scene drop offers both preload and instantiate."):
			return false
		var is_preload := label == "Preload Resource"
		if not await _click_drop_action(label, &"preload_resource" if is_preload else &"call_method"):
			return false
		var nodes := _records()
		if not _need(nodes.size() == 1, "A single click adds exactly one scene node operation."):
			return false
		var record: Dictionary = nodes[0]
		if is_preload:
			if not _need(record.get("asset_class") == &"PackedScene" and
					record.get("values", {}).get("asset", {}).get("path") == DROPPED_SCENE_PATH,
					"Scene preload stores the typed asset reference."):
				return false
		elif not _need(record.get("member", {}).get("name") == &"instantiate" and
				record.get("values", {}).get("target", {}).get("path") == DROPPED_SCENE_PATH,
				"Scene Instantiate targets the dropped PackedScene."):
			return false
	return true


## Find Load Resource freely and insert it from either end of a typed wire.
func _load_resource_actions() -> bool:
	await _reset_graph()
	if not await _free("Load Resource", &"load_resource"):
		return false
	var load_card := _card(1)
	if not _need(load_card != null and load_card.get_input_port_count() == 2 and load_card.get_output_port_count() == 3,
			"Load Resource exposes execution, Path, Success, Failure, and Resource sockets."):
		return false
	if not _need(_records()[0].get("class") == &"Resource", "Free insertion uses a generic Resource output."):
		return false

	var stream_pin := {"id": &"value", "label": "Stream", "type": TYPE_OBJECT, "class": &"AudioStream"}
	var stream_setter := {"class": &"AudioStreamPlayer2D", "name": &"stream", "inputs": [stream_pin]}
	await _reset_graph([_node(1, &"set_property", {"member": stream_setter})])
	_from_pin(1, 2, false)
	if not await _accept("Load Resource", &"load_resource"):
		return false
	if not _need(_records()[1].get("class") == &"AudioStream" and _wire(2, &"resource", 1, &"value"),
			"An AudioStream destination refines the new Resource output and connects only the compatible data wire."):
		return false

	var path_literal := _node(1, &"literal", {
		"pin": {"type": TYPE_STRING},
		"value": RESOURCE_FIXTURE_PATH,
	})
	var report_loaded := _node(3, &"print", {"message": LOAD_SUCCESS_MESSAGE})
	await _reset_graph([path_literal, _node(2, &"ready"), report_loaded])
	_from_pin(1, 0, true)
	if not await _accept("Load Resource", &"load_resource"):
		return false
	if not _need(_wire(1, &"result", 4, &"path"), "Dragging from a String output connects it to the Path socket."):
		return false
	canvas.connection_request.emit(_card(2).name, 0, _card(4).name, 0)
	canvas.connection_request.emit(_card(4).name, 0, _card(3).name, 0)
	await get_tree().process_frame
	if not _need(_wire(2, &"then", 4, &"in") and _wire(4, &"success", 3, &"in"),
			"The new node can execute from an event and continue through Success."):
		return false
	await _shortcut(KEY_S)
	var reloaded := ResourceLoader.load(GRAPH_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(reloaded != null, "Reload the graph authored through the picker."):
		return false
	var reopened: Dictionary = reloaded.call(&"get_graph")
	if not _need(reopened.nodes == _records() and reopened.connections.size() == 3 and reopened.connections[0].to_pin == &"path",
			"The saved graph retains the Load Resource node and both kinds of wire."):
		return false
	return _save_graph_scene(reloaded, LOAD_FIXTURE_GRAPH, LOAD_FIXTURE_SCENE)


## Save a copy of an editor-authored graph and attach it to a standalone scene for runtime verification.
func _save_graph_scene(graph_script: Script, graph_path: String, scene_path: String) -> bool:
	if not _need(ResourceSaver.save(graph_script, graph_path) == OK,
			"Preserve the authored graph while the editor continues with other cases."):
		return false
	# Saving a copy does not move the source graph's ResourceLoader path or UID.
	var saved_script := ResourceLoader.load(graph_path, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(saved_script != null and saved_script.resource_path == graph_path, "Load the copy at its own path."):
		return false
	var scene_owner := Node.new()
	scene_owner.name = &"AuthoredGraphFixture"
	scene_owner.set_script(saved_script)
	var fixture_scene := PackedScene.new()
	var pack_result := fixture_scene.pack(scene_owner)
	scene_owner.free()
	return _need(pack_result == OK and ResourceSaver.save(fixture_scene, scene_path) == OK,
			"Save a standalone scene with the editor-authored graph.")


## Drop a generated GDScript to preload the script resource or author a typed constructor.
func _script_constructor_actions() -> bool:
	var source_file := FileAccess.open(CONSTRUCTOR_SCRIPT_PATH, FileAccess.WRITE)
	if not _need(source_file != null, "Open the generated GDScript fixture for writing."):
		return false
	var fixture_source := """extends RefCounted

var _fixture_value: int

func _init(initial_value: int) -> void:
	_fixture_value = initial_value

func read_fixture_message() -> String:
	return "CRIMSON_EDITOR_AUTHORED_SCRIPT_CONSTRUCTED %d" % _fixture_value
"""
	source_file.store_string(fixture_source)
	source_file.close()
	var filesystem := EditorInterface.get_resource_filesystem()
	filesystem.update_file(CONSTRUCTOR_SCRIPT_PATH)
	if not await _browse_script_constructor():
		return false
	await _reset_graph()
	var target := canvas.get_global_transform_with_canvas() * (canvas.size * 0.5)
	await _drop_project_file(CONSTRUCTOR_SCRIPT_PATH, target)
	if not await _cancel_drop():
		return false
	await _drop_project_file(CONSTRUCTOR_SCRIPT_PATH, target)
	if not _need(_drop_choice_button(&"preload_resource") != null and _drop_choice_button(&"create_script_instance") != null,
			"A GDScript drop offers both preload and constructor actions."):
		return false
	if not await _click_drop_action("Preload Resource", &"preload_resource"):
		return false
	if not _need(_records().size() == 1 and _records()[0].get("asset_class") == &"GDScript" and
			_records()[0].get("values", {}).get("asset", {}).get("path") == CONSTRUCTOR_SCRIPT_PATH,
			"Preload stores the project path and preserves the GDScript resource type."):
		return false

	var argument := _node(1, &"literal", {"pin": {"type": TYPE_INT}, "value": 73})
	await _reset_graph([argument, _node(2, &"ready"), _node(3, &"print")])
	target = canvas.get_global_transform_with_canvas() * (canvas.size * 0.5)
	await _drop_project_file(CONSTRUCTOR_SCRIPT_PATH, target)
	if not await _click_drop_action("Create from Script", &"create_script_instance"):
		return false
	if not _need(_records().size() == 4, "A single click adds exactly one script constructor."):
		return false
	var constructor: Dictionary = _records()[3]
	var constructor_pins: Array = constructor.get("inputs", [])
	if not _need(constructor.get("script_path") == CONSTRUCTOR_SCRIPT_PATH and constructor.get("class") == &"RefCounted" and
			constructor_pins.size() == 1 and constructor_pins[0].get("type") == TYPE_INT,
			"Create from Script preserves the script type and reflects the required integer constructor argument."):
		return false
	_from_pin(4, 1, true)
	if not await _accept("Read Fixture Message", &"call_method"):
		return false
	var method: Dictionary = _records()[4].get("member", {})
	if not _need(method.get("script_path") == CONSTRUCTOR_SCRIPT_PATH and method.get("name") == &"read_fixture_message" and
			_wire(4, &"instance", 5, &"target"), "The instance wire offers and connects the script method as a typed call."):
		return false
	canvas.connection_request.emit(_card(1).name, 0, _card(4).name, 1)
	canvas.connection_request.emit(_card(2).name, 0, _card(4).name, 0)
	canvas.connection_request.emit(_card(4).name, 0, _card(5).name, 0)
	canvas.connection_request.emit(_card(5).name, 0, _card(3).name, 0)
	canvas.connection_request.emit(_card(5).name, 1, _card(3).name, 1)
	await get_tree().process_frame
	if not _need(_wire(1, &"result", 4, &"arg_0") and _wire(2, &"then", 4, &"in") and
			_wire(4, &"then", 5, &"in") and _wire(5, &"then", 3, &"in") and _wire(5, &"result", 3, &"message"),
			"The graph passes the constructor argument and prints the result of the typed call."):
		return false
	await _shortcut(KEY_S)
	var reloaded := ResourceLoader.load(GRAPH_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	return _need(reloaded != null, "Reload the editor-authored constructor graph.") and \
			_save_graph_scene(reloaded, CONSTRUCTOR_GRAPH_PATH, CONSTRUCTOR_SCENE_PATH)


## Invoke the standalone constructor action and select the generated source in Godot's own dialog.
func _browse_script_constructor() -> bool:
	await _reset_graph()
	canvas.popup_request.emit(PALETTE_POINT)
	_query("Create from Script")
	var hit := results.get_selected()
	var metadata: Dictionary = hit.get_metadata(0) if hit != null else {}
	if not _need(metadata.get("choose_script_constructor", false), "Picker search selects the standalone constructor command."):
		return false
	search_field.text_submitted.emit("Create from Script")
	await get_tree().process_frame
	await get_tree().process_frame
	var chooser: EditorFileDialog
	for candidate in workspace.find_children("*", "EditorFileDialog", false, false):
		if candidate.visible:
			chooser = candidate
			break
	if not _need(chooser != null and chooser.file_mode == FileDialog.FILE_MODE_OPEN_FILE and
			chooser.access == FileDialog.ACCESS_RESOURCES and ",".join(chooser.filters).contains("*.gd"),
			"Create from Script opens a project file picker filtered for GDScript."):
		return false
	chooser.current_path = CONSTRUCTOR_SCRIPT_PATH
	chooser.get_ok_button().pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame
	var records := _records()
	return _need(records.size() == 1 and records[0].get("kind") == &"create_script_instance" and
			records[0].get("script_path") == CONSTRUCTOR_SCRIPT_PATH,
			"Confirming the generated file in Godot's browser inserts its constructor.")


## Test cast filtering with related classes and a generated script type, then authored Input Map actions.
func _cast_and_input_actions() -> bool:
	var settings_key := "input/" + String(ACTION_NAME)
	ProjectSettings.set_setting(settings_key, {"deadzone": 0.2, "events": []})
	await _reset_graph([_node(1, &"self")])
	_from_pin(1, 0, true)
	_query("Cast To Texture2D")
	if not _need(not _has_kind(results.get_root(), &"cast"), "A Node cannot be cast to a Resource in context."):
		return false
	if not await _accept("Cast To Node3D", &"cast"):
		return false
	if not _need(_wire(1, &"node", 2, &"object") and _records()[1].pin.get("class") == &"Node3D",
			"A compatible cast records the target class and connects its Object pin."):
		return false
	if not await _free("Is Action Pressed", &"input_poll"):
		return false
	var poll_card := _card(3)
	var action_field := poll_card.find_child("Literal_action", true, false) as Control
	if not _need(action_field != null and poll_card.get_output_port_count() == 1,
			"Input Map polling exposes its action selector and Boolean output."):
		return false
	var trigger := action_field.find_child("SelectTrigger", true, false) as Button
	if not _need(trigger != null, "Find the Input Map selection trigger."):
		return false
	trigger.pressed.emit()
	await get_tree().process_frame
	var action_row := _button_with_text(action_field, String(ACTION_NAME))
	if not _need(action_row != null, "The selector lists actions from Project Settings."):
		return false
	action_row.pressed.emit()
	await get_tree().process_frame
	if not _need(_records()[2].get("values", {}).get(&"action") == ACTION_NAME, "Choosing the action authors a StringName literal."):
		return false
	ProjectSettings.set_setting(settings_key, null)
	var poll_record: Dictionary = _records()[2]
	await _reset_graph([poll_record])
	trigger = _card(3).find_child("SelectTrigger", true, false) as Button
	return _need(trigger != null and trigger.text.contains("(missing)") and _records()[0].values.action == ACTION_NAME,
			"Removing an action preserves its saved name and indicates the missing setting.")


## Locate a native picker option by its action name, independent of layout.
func _button_with_text(root: Node, wanted: String) -> Button:
	for entry in root.find_children("*", "Button", true, false):
		var button := entry as Button
		if button.text == wanted:
			return button
	return null


## Locate the active modal's action rather than a button inside an unrelated hidden dialog.
func _visible_button_named(root: Node, wanted: String) -> Button:
	for entry in root.find_children(wanted, "Button", true, false):
		var button := entry as Button
		if button.is_visible_in_tree():
			return button
	return null


## Wait for the editor's global class index, which refreshes after update_file returns.
func _wait_for_script_type(expected: bool) -> bool:
	for attempt in range(60):
		var found := false
		for entry: Dictionary in ProjectSettings.get_global_class_list():
			if entry.get("class") == FOREIGN_CLASS:
				found = true
				break
		if found == expected:
			return true
		await get_tree().process_frame
	return _need(false, "The ClassDB picker receives changes from the project global class index.")


## Test live custom Engine registration, script/scene Autoloads and receiver reflection.
func _singleton_actions() -> bool:
	var service := Node.new()
	Engine.register_singleton(REGISTERED_NAME, service)
	await _reset_graph()
	var made_service := await _free(String(REGISTERED_NAME), &"singleton")
	Engine.unregister_singleton(REGISTERED_NAME)
	service.free()
	if not made_service:
		return false
	if not _need(_records()[0].singleton == REGISTERED_NAME and _records()[0].source == &"engine", "The registered instance is authored by name and source."):
		return false
	canvas.popup_request.emit(PALETTE_POINT)
	_query(String(REGISTERED_NAME))
	if not _need(not _has_kind(results.get_root(), &"singleton", String(REGISTERED_NAME)), "A reopened palette reflects unregistration without changing the existing node."):
		return false
	search_field.get_window().hide()
	var foreign_file := FileAccess.open(FOREIGN_SCRIPT_PATH, FileAccess.WRITE)
	if not _need(foreign_file != null, "Write an isolated typed script fixture."):
		return false
	foreign_file.store_string("extends Node3D\nclass_name %s\n@warning_ignore(\"unused_signal\")\nsignal probe_updated(number: int)\nfunc get_probe_number() -> int:\n\treturn 3\n" % FOREIGN_CLASS)
	foreign_file.close()
	var filesystem := EditorInterface.get_resource_filesystem()
	filesystem.update_file(FOREIGN_SCRIPT_PATH)
	if not await _wait_for_script_type(true):
		return false
	var foreign_script := ResourceLoader.load(FOREIGN_SCRIPT_PATH, "Script") as Script
	if not _need(foreign_script != null, "Load the fixture's authored GDScript without running it."):
		return false
	var scene_root := Node3D.new()
	scene_root.set_script(foreign_script)
	var fixture_scene := PackedScene.new()
	var packed := fixture_scene.pack(scene_root)
	scene_root.free()
	if not _need(packed == OK and ResourceSaver.save(fixture_scene, SCENE_AUTOLOAD_PATH) == OK,
			"Create a scene-based Autoload with the generated script type."):
		return false
	ProjectSettings.set_setting("autoload/" + String(SCRIPT_AUTOLOAD_NAME), "*" + FOREIGN_SCRIPT_PATH)
	ProjectSettings.set_setting("autoload/" + String(SCENE_AUTOLOAD_NAME), "*" + SCENE_AUTOLOAD_PATH)
	await _reset_graph()
	for name_key: StringName in [SCRIPT_AUTOLOAD_NAME, SCENE_AUTOLOAD_NAME]:
		if not await _free(String(name_key), &"singleton"):
			return false
	for record: Dictionary in _records():
		if not _need(record.source == &"autoload" and record.pin.script_path == FOREIGN_SCRIPT_PATH and record.pin.class == &"Node3D",
				"Both Autoload sources preserve their attached object type."):
			return false
	_from_pin(2, 0, true)
	if not await _accept("Get Probe Number", &"call_method"):
		return false
	if not _need(_wire(2, &"value", 3, &"target") and _records()[2].member.script_path == FOREIGN_SCRIPT_PATH,
			"The scene Autoload exposes its actual script methods through its typed pin."):
		return false
	_from_pin(1, 0, true)
	if not await _accept("Await Probe Updated", &"await_signal"):
		return false
	if not _need(_wire(1, &"value", 4, &"target") and _records()[3].script_path == FOREIGN_SCRIPT_PATH and \
			_records()[3].inputs[0].type == TYPE_INT, "Awaiting a project script signal exposes and saves its typed payload."):
		return false
	ProjectSettings.set_setting("autoload/" + String(SCRIPT_AUTOLOAD_NAME), null)
	ProjectSettings.set_setting("autoload/" + String(SCENE_AUTOLOAD_NAME), null)
	await _reset_graph([_node(1, &"self")])
	_from_pin(1, 0, true)
	var cast_label := "Cast To " + String(FOREIGN_CLASS)
	if not await _accept(cast_label, &"cast"):
		return false
	if not _need(_records()[1].pin.script_path == FOREIGN_SCRIPT_PATH and _wire(1, &"node", 2, &"object"),
			"The contextual cast uses the authored class_name and wires the object reference."):
		return false
	# Let Godot finish queued reloads before invalidating the file index.
	for attempt in range(120):
		if not filesystem.is_scanning():
			break
		await get_tree().process_frame
	if not _need(not filesystem.is_scanning(), "Finish the editor file scan before removing the fixture."):
		return false
	var remove_result := DirAccess.remove_absolute(ProjectSettings.globalize_path(FOREIGN_SCRIPT_PATH))
	if not _need(remove_result == OK, "Remove only the temporary script fixture to invalidate the known type."):
		return false
	filesystem.update_file(FOREIGN_SCRIPT_PATH)
	if not await _wait_for_script_type(false):
		return false
	canvas.popup_request.emit(PALETTE_POINT)
	_query(cast_label)
	if not _need(not _has_kind(results.get_root(), &"cast", cast_label) and _records().size() == 2,
			"A removed script type is no longer offered and the authored graph is left intact."):
		return false
	search_field.get_window().hide()
	return true
