# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const SOURCE_SCRIPT: Script = preload("res://tests/visual_scripting/custom_script.crimsonscript")
const AUTHORED_SCRIPT_PATH := "res://authored_custom_script.crimsonscript"
const CODE_NODE_ID := 2
const AMMO_EVENT_ID := 1
const OUTPUT_OBSERVER_ID := 8
const COMPLETION_ATTEMPTS := 80
const DEFAULT_SCRIPT_TITLE := "Custom Script"
const AUTHORED_SCRIPT_TITLE := "Update Magazine"
const VALID_HELPERS := "func hello() -> void:\n    pass\n\nfunc volume_for(level: float) -> float:\n    return level - absf(owner.pitch_scale * 6.0)\n\n"
const INVALID_HELPER := "func volume_for(level: float) -> float:\n    return \"nonnumeric\"\n\n"
const DEFAULT_INPUT := "#@input\nvar default_bonus: int = 1\n"
const WIRED_INPUT := "#@input\nvar wired_bonus: int = 400\n"
const SOURCE_PIN_DECLARATIONS := DEFAULT_INPUT + WIRED_INPUT \
	+ "#@input\nvar unset_bonus: int\n#@input\nvar alarm_default: AudioStream = alarm\n#@input\nvar selected_alarm: AudioStream = alarm\n#@output\nvar calculated: int = 100\n\n"
const SOURCE_PIN_EXPRESSION := "calculated += default_bonus + wired_bonus + unset_bonus\nowner.stream = alarm_default\nowner.set_meta(&\"explicit_null\", selected_alarm == null)"
const QUOTED_DIRECTIVE := "owner.set_meta(&\"example\", \"\"\"\n#@input\nvar inside_string: int\n\"\"\")"
const AUTOSAVE_TIMEOUT_SECONDS := 4.0

var workspace: Control
var graph_canvas: GraphEdit
var graph_script: Script


func _enter_tree() -> void:
	_verify.call_deferred()


func _verify() -> void:
	await get_tree().process_frame
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await get_tree().process_frame
	graph_script = ClassDB.instantiate(&"VisualScript") as Script
	graph_script.call(&"set_graph_base", &"AudioStreamPlayer3D")
	var fixture_graph: Dictionary = SOURCE_SCRIPT.call(&"get_graph")
	fixture_graph.events[0].inputs.append({"id": &"nullable", "label": "Nullable", "type": TYPE_OBJECT, "class": &"AudioStream"})
	fixture_graph.nodes.append({
		"id": OUTPUT_OBSERVER_ID, "kind": "call_method", "position": Vector2(1020, 0),
		"member": {"class": &"Object", "name": &"set_meta", "inputs": [
			{"id": &"arg_0", "label": "Name", "type": TYPE_STRING_NAME},
			{"id": &"arg_1", "label": "Value", "type": TYPE_NIL},
		]},
		"values": {&"arg_0": &"calculated"},
	})
	fixture_graph.connections.append({"from": 4, "from_pin": &"then", "to": OUTPUT_OBSERVER_ID, "to_pin": &"in"})
	graph_script.call(&"set_graph", fixture_graph)
	if not _need(ResourceSaver.save(graph_script, AUTHORED_SCRIPT_PATH) == OK, "Stage a runnable visual script"):
		return
	graph_script = ResourceLoader.load(AUTHORED_SCRIPT_PATH, "Script") as Script
	if not _need(graph_script != null and graph_script.resource_path == AUTHORED_SCRIPT_PATH, "Open the staged graph from disk"):
		return
	EditorInterface.edit_script(graph_script, -1, 0, true)
	await get_tree().process_frame
	workspace = get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _need(workspace != null and workspace.is_visible_in_tree(), "Open the Visual Script workspace"):
		return
	graph_canvas = workspace.find_child("GraphCanvas", true, false) as GraphEdit
	if not await _verify_palette_and_pins():
		return
	if not await _verify_custom_script_title():
		return
	if not await _select_node(CODE_NODE_ID) or not await _press("VisualScriptEditCustomCode"):
		return
	var code_edit := workspace.find_child("VisualScriptCustomCodeEdit", true, false) as CodeEdit
	if not _need(code_edit != null and code_edit.is_visible_in_tree(), "Open the node's actual CodeEdit"):
		return
	var initial_body: String = _node_record(CODE_NODE_ID).get("code", "")
	if not _need(code_edit.text.strip_edges() == initial_body, "CodeEdit opens the saved snippet"):
		return
	if not await _verify_screen_switch_draft(code_edit, initial_body):
		return
	if not await _verify_local_declaration(code_edit):
		return
	if not await _verify_diagnostics(code_edit, initial_body):
		return
	var body := VALID_HELPERS + "hello()\n" + initial_body.replace("-12.0", "volume_for(-12.0)")
	if not await _expect_completion(code_edit, body, "owner.volume_db", "owner.", ["play", "stop", "volume_db"]):
		return
	if not await _expect_completion(code_edit, body, "graph.ammo", "graph.", ["ammo", "cap_rounds", "magazine_changed"]):
		return
	if not await _expect_completion(code_edit, body, "owner.volume_db", "owner.volume_db = vo", ["volume_for"]):
		return
	if not await _expect_completion(code_edit, body, "    return level", "    return level - absf(owner.", ["pitch_scale"]):
		return
	if not await _expect_completion(code_edit, "owner.st", "owner.st", "owner.st", ["stop"], "stop"):
		return
	if not _need(code_edit.text.begins_with("owner.stop("), "Confirming native completion inserts the callable"):
		return
	body = await _verify_source_directives(code_edit, body)
	if body.is_empty() or not _need(await _wait_for_autosaved_body(body), "Applying Custom Script code saves the graph automatically"):
		return
	var reloaded := ResourceLoader.load(AUTHORED_SCRIPT_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _need(reloaded != null, "Reload the edited graph from disk"):
		return
	var document: Dictionary = reloaded.call(&"get_graph")
	var code_node: Dictionary = document.nodes[1]
	if not _need(code_node.get("code") == body and code_node.get("title") == AUTHORED_SCRIPT_TITLE,
			"Autosave and reload keep the complete CodeEdit text and per-node title"):
		return
	var wired_pin_index := _pin_port_index(code_node.get("inputs", []), &"source_wired_bonus")
	var default_pin_index := _pin_port_index(code_node.get("inputs", []), &"source_default_bonus")
	if not _need(wired_pin_index > 0 and wired_pin_index < default_pin_index
			and _source_pin(code_node, "outputs", "calculated").get("id") == &"source_calculated"
			and document.connections.any(func(wire: Dictionary) -> bool: return wire.get("to_pin") == &"source_wired_bonus"),
			"Reload keeps stable source-owned pin IDs and their wires"):
		return
	EditorInterface.edit_script(reloaded, -1, 0, true)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(_code_widget() != null and _code_widget().title == AUTHORED_SCRIPT_TITLE,
			"Reopening the file redraws the titled Custom Script card"):
		return
	if not await _verify_code_ports_at_zoom():
		return
	print("CRIMSON_VISUAL_SCRIPT_CUSTOM_EDITOR_PASSED")
	get_tree().quit()


func _verify_custom_script_title() -> bool:
	if not await _select_node(CODE_NODE_ID):
		return false
	if not _need(_code_widget() != null and _code_widget().title == DEFAULT_SCRIPT_TITLE,
			"A Custom Script card without a title uses the default"):
		return false
	if not await _verify_code_ports_at_zoom():
		return false
	# Details can name and clear a card independently of the node's code and wires.
	var original_document: Dictionary = graph_script.call(&"get_graph")
	var other_node: Dictionary = original_document.nodes.back()
	if not await _submit_title(" " + AUTHORED_SCRIPT_TITLE + " "):
		return false
	var other_widget := graph_canvas.get_node_or_null(NodePath("GraphNode_%s" % other_node["id"])) as GraphNode
	if not _need(_code_widget().title == AUTHORED_SCRIPT_TITLE
			and other_widget != null and other_widget.title == DEFAULT_SCRIPT_TITLE,
			"Details applies the trimmed title to the selected Custom Script card only"):
		return false
	if not await _find_custom_script_by_title():
		return false
	if not await _submit_title("  "):
		return false
	if not _need(_code_widget().title == DEFAULT_SCRIPT_TITLE and graph_script.call(&"get_graph") == original_document,
			"Clearing the title restores the default without editing code or wires"):
		return false
	return await _submit_title(AUTHORED_SCRIPT_TITLE)


func _submit_title(title_text: String) -> bool:
	# A title edit rebuilds Details. Re-select the actual card before retrieving its new field.
	if not await _select_node(CODE_NODE_ID):
		return false
	var title_field := workspace.find_child("VisualScriptCustomScriptTitle", true, false) as LineEdit
	if not _need(title_field != null and title_field.editable, "Details provides a Custom Script title field"):
		return false
	title_field.text = title_text
	title_field.text_submitted.emit(title_field.text)
	await get_tree().process_frame
	return _need(_code_widget() != null, "The title applies to the existing Custom Script card")


## Wait for the applied Custom Script body to appear on disk.
func _wait_for_autosaved_body(expected_body: String) -> bool:
	var deadline := Time.get_ticks_msec() + int(AUTOSAVE_TIMEOUT_SECONDS * 1000.0)
	while Time.get_ticks_msec() < deadline:
		var disk_script := ResourceLoader.load(AUTHORED_SCRIPT_PATH, "Script", ResourceLoader.CACHE_MODE_IGNORE) as Script
		if disk_script != null:
			var disk_graph: Dictionary = disk_script.call(&"get_graph")
			for disk_node: Dictionary in disk_graph.get("nodes", []):
				if disk_node.id == CODE_NODE_ID and disk_node.get("code") == expected_body:
					return true
		await get_tree().create_timer(0.025).timeout
	return false


func _code_widget() -> GraphNode:
	return graph_canvas.get_node_or_null(NodePath("GraphNode_%s" % CODE_NODE_ID)) as GraphNode


func _find_custom_script_by_title() -> bool:
	graph_canvas.grab_focus()
	var find_shortcut := InputEventKey.new()
	find_shortcut.keycode = KEY_F
	find_shortcut.ctrl_pressed = true
	find_shortcut.pressed = true
	workspace.get_window().push_input(find_shortcut, true)
	await get_tree().process_frame
	var query := workspace.find_child("GraphFindQuery", true, false) as LineEdit
	var matches := workspace.find_child("GraphFindResults", true, false) as Tree
	if not _need(query != null and query.has_focus() and matches != null and matches.is_visible_in_tree(),
			"Ctrl+F opens Find Nodes from the graph canvas"):
		return false
	query.text = AUTHORED_SCRIPT_TITLE
	query.text_changed.emit(query.text)
	var result := matches.get_selected()
	var result_title := result.get_text(0) if result != null else ""
	var dismiss := InputEventKey.new()
	dismiss.keycode = KEY_ESCAPE
	dismiss.pressed = true
	workspace.get_window().push_input(dismiss, true)
	await get_tree().process_frame
	return _need(result_title.contains(AUTHORED_SCRIPT_TITLE), "Find Nodes indexes the authored title")


func _verify_code_ports_at_zoom() -> bool:
	var code_node := _node_record(CODE_NODE_ID)
	var original_zoom := graph_canvas.zoom
	var original_scroll := graph_canvas.scroll_offset
	for zoom_level in [graph_canvas.zoom_min, original_zoom, graph_canvas.zoom_max]:
		graph_canvas.zoom = zoom_level
		await get_tree().process_frame
		var widget := _code_widget()
		if not _need(widget != null and widget.is_visible_in_tree(),
				"The Custom Script card remains present at zoom %s" % zoom_level):
			return false
		if not _need(widget.get_input_port_count() == code_node.inputs.size() + 1
				and widget.get_output_port_count() == code_node.outputs.size() + 1,
				"Zooming the card leaves control flow and typed ports intact"):
			return false
		var execution_type := widget.get_input_port_type(0)
		if not _need(widget.get_output_port_type(0) == execution_type
				and (code_node.inputs.is_empty() or widget.get_input_port_type(1) != execution_type)
				and (code_node.outputs.is_empty() or widget.get_output_port_type(1) != execution_type),
				"Execution and data remain distinct at every tested zoom"):
			return false
	graph_canvas.zoom = original_zoom
	graph_canvas.scroll_offset = original_scroll
	await get_tree().process_frame
	return true


func _verify_palette_and_pins() -> bool:
	var search_field := workspace.find_child("GraphPaletteSearch", true, false) as LineEdit
	var result_tree := workspace.find_child("GraphPaletteResults", true, false) as Tree
	if not _need(graph_canvas != null and search_field != null and result_tree != null, "Find searchable graph palette"):
		return false
	graph_canvas.popup_request.emit(Vector2(360, 480))
	search_field.text = "Custom Script"
	search_field.text_changed.emit(search_field.text)
	var result := result_tree.get_selected()
	if not _need(result != null and result.get_text(0) == "Custom Script", "Picker finds exact Custom Script node"):
		return false
	search_field.text_submitted.emit(search_field.text)
	await get_tree().process_frame
	var document: Dictionary = graph_script.call(&"get_graph")
	var added_node: Dictionary = document.nodes.back()
	if not _need(document.nodes.size() == 8 and added_node.get("kind") == &"custom_script", "Picker creates exactly one node"):
		return false
	var node_id := int(added_node["id"])
	if not await _select_node(node_id):
		return false
	for group_name: String in ["VisualScriptCustomInputsDisclosure", "VisualScriptCustomOutputsDisclosure"]:
		var disclosure := workspace.find_child(group_name, true, false) as Button
		if not _need(disclosure != null and not disclosure.button_pressed, "Custom Script pin groups start collapsed."):
			return false
		disclosure.button_pressed = true
	if not await _press("VisualScriptAddCustomInput") or not await _press("VisualScriptAddCustomOutput"):
		return false
	var input_name := workspace.find_child("VisualScriptCustomPinName_input_1", true, false) as LineEdit
	if not _need(input_name != null, "Details has a first Custom Script input"):
		return false
	input_name.text = "duration"
	input_name.text_submitted.emit(input_name.text)
	await get_tree().process_frame
	var inputs := workspace.find_child("VisualScriptCustomInputs", true, false)
	var type_choice := inputs.find_child("VisualScriptDeclarationType", true, false) as BaseButton
	if not _need(type_choice != null, "Custom input has its own type picker"):
		return false
	type_choice.pressed.emit()
	await get_tree().process_frame
	var type_search := workspace.find_child("VisualScriptTypeSearch", true, false) as LineEdit
	var type_results := workspace.find_child("VisualScriptTypeTree", true, false) as Tree
	if not _need(type_search != null and type_results != null, "Type popup is searchable"):
		return false
	type_search.text = "int"
	type_search.text_changed.emit(type_search.text)
	var exact_type := type_results.get_selected()
	if not _need(exact_type != null and exact_type.get_text(0) == "int", "Type popup offers typed int"):
		return false
	type_search.text_submitted.emit(type_search.text)
	await get_tree().process_frame
	var node_record := _node_record(node_id)
	var input_pin: Dictionary = node_record.inputs[0]
	var output_pin: Dictionary = node_record.outputs[0]
	return _need(input_pin.id == &"input_1" and input_pin.label == "duration" and input_pin.type == TYPE_INT
		and output_pin.id == &"output_1" and output_pin.type == TYPE_FLOAT,
		"Details adds typed pins and keeps the input's wire ID on rename")


func _verify_source_directives(code_edit: CodeEdit, base_body: String) -> String:
	var draft := SOURCE_PIN_DECLARATIONS + base_body + "\n" + SOURCE_PIN_EXPRESSION + "\n" + QUOTED_DIRECTIVE
	if not await _expect_completion(code_edit, draft, "owner.stream = alarm_default", "owner.stream = al", ["alarm_default", "alarm"]):
		return ""
	var current_node := _node_record(CODE_NODE_ID)
	if not _need(_source_pin(current_node, "inputs", "default_bonus").is_empty(),
			"Completion includes draft source pins before mutating the graph"):
		return ""
	if not await _verify_manual_pin_collision(code_edit, base_body):
		return ""
	var diagnostics := workspace.find_child("VisualScriptCustomCodeDiagnostics", true, false) as Label
	if not _need(diagnostics != null, "Source directives expose diagnostics in CodeEdit"):
		return ""
	code_edit.text = "#@input\nvar spelling: int = \"bad\"\n" + base_body
	if not await _expect_type_error(diagnostics, 2, "int"):
		return ""
	code_edit.text = draft
	await get_tree().process_frame
	if not await _press("VisualScriptApplyCustomCode"):
		return ""
	current_node = _node_record(CODE_NODE_ID)
	var wired_pin := _source_pin(current_node, "inputs", "wired_bonus")
	var default_pin := _source_pin(current_node, "inputs", "default_bonus")
	var output_pin := _source_pin(current_node, "outputs", "calculated")
	if not _need(wired_pin.get("id") == &"source_wired_bonus" and default_pin.get("id") == &"source_default_bonus"
			and output_pin.get("id") == &"source_calculated" and wired_pin.get("type") == TYPE_INT
			and default_pin.get("type") == TYPE_INT and output_pin.get("type") == TYPE_INT
			and _source_pin(current_node, "inputs", "inside_string").is_empty(),
			"Apply generates typed, source-owned pins with stable identifiers"):
		return ""
	if not await _select_node(CODE_NODE_ID):
		return ""
	var source_name := workspace.find_child("VisualScriptCustomPinName_source_default_bonus", true, false) as LineEdit
	var manual_name := workspace.find_child("VisualScriptCustomPinName_input_1", true, false) as LineEdit
	var source_hint := workspace.find_child("VisualScriptSourcePin_source_default_bonus", true, false) as Label
	if not _need(source_name != null and not source_name.editable and manual_name != null and manual_name.editable,
			"Details identifies source-owned rows as read-only and leaves manual pins editable"):
		return ""
	if not _need(source_hint != null and source_hint.text == "In code"
			and graph_canvas.find_child("Literal_source_default_bonus", true, false) == null,
			"Source defaults are marked In code with no competing editor on the graph card"):
		return ""
	if not await _wire_source_pins(current_node):
		return ""
	var wired_document: Dictionary = graph_script.call(&"get_graph")
	if not await _verify_cancel_after_focus_loss(code_edit, wired_document):
		return ""

	if not await _select_node(CODE_NODE_ID) or not await _press("VisualScriptEditCustomCode"):
		return ""
	code_edit.text = "#@output\npass\n" + draft
	await get_tree().process_frame
	if not _attempt_apply():
		return ""
	if not _need(code_edit.is_visible_in_tree() and graph_script.call(&"get_graph") == wired_document,
			"Malformed directive cannot partially replace a signature with active wires"):
		return ""
	code_edit.text = draft.replace(WIRED_INPUT, "").replace(" + wired_bonus", "")
	await get_tree().process_frame
	if not _attempt_apply():
		return ""
	for attempt in range(COMPLETION_ATTEMPTS):
		if diagnostics.text.contains("Disconnect the affected wires"):
			break
		await get_tree().create_timer(0.10).timeout
	if not _need(diagnostics.text.contains("Disconnect the affected wires") and code_edit.is_visible_in_tree()
			and graph_script.call(&"get_graph") == wired_document,
			"Removing a wired source pin keeps the editor open and preserves the saved body and wires"):
		return ""
	var revised := draft.replace(DEFAULT_INPUT + WIRED_INPUT, WIRED_INPUT + DEFAULT_INPUT.replace("= 1", "= 3"))
	code_edit.text = revised
	await get_tree().process_frame
	if not await _press("VisualScriptApplyCustomCode"):
		return ""
	return revised


## The editor can switch screens without discarding or applying a Custom Script edit.
func _verify_screen_switch_draft(code_edit: CodeEdit, initial_body: String) -> bool:
	var pending_body := initial_body + "\n# Draft while switching editor tabs"
	code_edit.text = pending_body
	await get_tree().process_frame
	EditorInterface.set_main_screen_editor("3D")
	await get_tree().process_frame
	if not _need(not workspace.is_visible_in_tree() and not code_edit.get_window().visible,
			"Leaving Visual Script hides its workspace and the Custom Script editor"):
		return false
	EditorInterface.set_main_screen_editor("Visual Script")
	await get_tree().process_frame
	await get_tree().process_frame
	if not _need(workspace.is_visible_in_tree() and code_edit.is_visible_in_tree() and code_edit.text == pending_body and
			_node_record(CODE_NODE_ID).get("code") == initial_body,
			"Returning to Visual Script restores the draft while keeping the saved node body"):
		return false
	code_edit.text = initial_body
	return true


func _verify_cancel_after_focus_loss(code_edit: CodeEdit, wired_document: Dictionary) -> bool:
	if not await _select_node(CODE_NODE_ID) or not await _press("VisualScriptEditCustomCode"):
		return false
	var code_dialog := code_edit.get_window()
	code_dialog.focus_exited.emit()
	code_edit.text = "pass"
	await get_tree().process_frame
	if not await _press("VisualScriptCancelCustomCode"):
		return false
	return _need(not code_dialog.visible and graph_script.call(&"get_graph") == wired_document,
		"Leaving focus and cancelling an unapplied draft preserves all source-pin wires")


func _attempt_apply() -> bool:
	var apply_button := workspace.find_child("VisualScriptApplyCustomCode", true, false) as BaseButton
	if not _need(apply_button != null, "Find the source editor's Apply button"):
		return false
	# The editor can report an incompatible draft before Apply or when clicked.
	if not apply_button.disabled:
		apply_button.pressed.emit()
	return true


func _verify_manual_pin_collision(code_edit: CodeEdit, base_body: String) -> bool:
	var diagnostics := workspace.find_child("VisualScriptCustomCodeDiagnostics", true, false) as Label
	if not _need(diagnostics != null, "Find the source directive diagnostic label"):
		return false
	code_edit.text = "#@input\nvar rounds: int = 1\n" + base_body
	for attempt in range(COMPLETION_ATTEMPTS):
		var summary := diagnostics.text.to_lower()
		if summary != "checking gdscript..." and summary.contains("rounds"):
			return _need(_source_pin(_node_record(CODE_NODE_ID), "inputs", "rounds").is_empty(),
				"A source directive sharing a manual pin name is diagnosed before mutating the graph")
		await get_tree().create_timer(0.10).timeout
	return _need(false, "Diagnose source and Details pin name collision. Got: " + diagnostics.text)


func _wire_source_pins(current_node: Dictionary) -> bool:
	var event_widget := graph_canvas.get_node_or_null(NodePath("GraphNode_%s" % AMMO_EVENT_ID)) as GraphNode
	var code_widget := graph_canvas.get_node_or_null(NodePath("GraphNode_%s" % CODE_NODE_ID)) as GraphNode
	var observer := graph_canvas.get_node_or_null(NodePath("GraphNode_%s" % OUTPUT_OBSERVER_ID)) as GraphNode
	if not _need(event_widget != null and code_widget != null and observer != null, "Find the normal GraphEdit widgets to connect"):
		return false
	var event_name := event_widget.name
	var code_name := code_widget.name
	var observer_name := observer.name
	var wired_input_index := _pin_port_index(current_node.inputs, &"source_wired_bonus")
	var nullable_input_index := _pin_port_index(current_node.inputs, &"source_selected_alarm")
	var source_output_index := _pin_port_index(current_node.outputs, &"source_calculated")
	if not _need(wired_input_index > 0 and nullable_input_index > 0 and source_output_index > 0, "Find the added typed data ports"):
		return false
	graph_canvas.connection_request.emit(event_name, 1, code_name, wired_input_index)
	await get_tree().process_frame
	graph_canvas.connection_request.emit(event_name, 2, code_name, nullable_input_index)
	await get_tree().process_frame
	graph_canvas.connection_request.emit(code_name, source_output_index, observer_name, 3)
	await get_tree().process_frame
	var document: Dictionary = graph_script.call(&"get_graph")
	return _need(document.connections.any(func(wire: Dictionary) -> bool:
			return wire.get("from_pin") == &"rounds" and wire.get("to_pin") == &"source_wired_bonus")
		and document.connections.any(func(wire: Dictionary) -> bool:
			return wire.get("from_pin") == &"nullable" and wire.get("to_pin") == &"source_selected_alarm")
		and document.connections.any(func(wire: Dictionary) -> bool:
			return wire.get("from_pin") == &"source_calculated" and wire.get("to_pin") == &"arg_1"),
		"GraphEdit wires the typed source pin and output")


func _pin_port_index(pins: Array, pin_id: StringName) -> int:
	for pin_index in range(pins.size()):
		if pins[pin_index].get("id") == pin_id:
			# Control flow occupies port 0 on the node.
			return pin_index + 1
	return -1


func _source_pin(record: Dictionary, direction: String, pin_label: String) -> Dictionary:
	for pin: Dictionary in record.get(direction, []):
		if pin.get("label") == pin_label and pin.get("source_owned", false):
			return pin
	return {}


func _select_node(node_id: int) -> bool:
	var widget := graph_canvas.get_node_or_null(NodePath("GraphNode_%s" % node_id)) as GraphNode
	if not _need(widget != null, "Find canvas widget %s" % node_id):
		return false
	graph_canvas.set_selected(widget)
	graph_canvas.node_selected.emit(widget)
	await get_tree().process_frame
	return true


func _press(control_name: String) -> bool:
	var control := workspace.find_child(control_name, true, false) as BaseButton
	if not _need(control != null and not control.disabled, "Find enabled %s control" % control_name):
		return false
	control.pressed.emit()
	await get_tree().process_frame
	return true


func _node_record(node_id: int) -> Dictionary:
	var document: Dictionary = graph_script.call(&"get_graph")
	for entry: Dictionary in document.nodes:
		if entry.id == node_id:
			return entry
	return {}


func _expect_completion(code_edit: CodeEdit, body: String, line_prefix: String, caret_prefix: String,
		expected: Array[String], confirm_name: String = "") -> bool:
	code_edit.text = body
	var caret_line := -1
	for line_index in range(code_edit.get_line_count()):
		if code_edit.get_line(line_index).begins_with(line_prefix):
			caret_line = line_index
			break
	if not _need(caret_line >= 0 and code_edit.get_line(caret_line).begins_with(caret_prefix), "Find completion cursor " + caret_prefix):
		return false
	code_edit.set_caret_line(caret_line)
	code_edit.set_caret_column(caret_prefix.length())
	code_edit.grab_focus()
	await get_tree().process_frame
	code_edit.request_code_completion(true)
	var options: Array[Dictionary] = []
	for attempt in range(COMPLETION_ATTEMPTS):
		await get_tree().create_timer(0.10).timeout
		options = code_edit.get_code_completion_options()
		var found := true
		for member_name in expected:
			if not options.any(func(option: Dictionary) -> bool: return _matches_completion(option, member_name)):
				found = false
				break
		if found:
			if confirm_name.is_empty():
				code_edit.cancel_code_completion()
			else:
				for option_index in range(options.size()):
					if _matches_completion(options[option_index], confirm_name):
						code_edit.set_code_completion_selected_index(option_index)
						code_edit.confirm_code_completion()
						break
			return true
		# The initial editor scan can briefly delay the stock language server listener.
		if (attempt + 1) % 15 == 0:
			code_edit.request_code_completion(true)
	return _need(false, "The real CodeEdit completion menu contains %s. Result: %s" % [expected, str(options).left(360)])


func _matches_completion(option: Dictionary, member_name: String) -> bool:
	var display: String = option.get("display_text", "")
	return display == member_name or display.begins_with(member_name + "(")


func _verify_local_declaration(code_edit: CodeEdit) -> bool:
	var diagnostics := workspace.find_child("VisualScriptCustomCodeDiagnostics", true, false) as Label
	if not _need(diagnostics != null, "Find the diagnostics label"):
		return false
	code_edit.text = VALID_HELPERS + 'var my_var: String = ""'
	for attempt in range(COMPLETION_ATTEMPTS):
		if diagnostics.text != "Checking GDScript...":
			return _need(not diagnostics.text.contains("Script context:") and not diagnostics.text.contains("Error ")
					and (diagnostics.text == "No GDScript diagnostics." or diagnostics.text.begins_with("Warning ")),
					"Godot accepts a valid String local and named helpers with no context error. Got: " + diagnostics.text)
		await get_tree().create_timer(0.10).timeout
	return _need(false, "Valid local declaration was analyzed. Got: " + diagnostics.text)


func _verify_diagnostics(code_edit: CodeEdit, valid_body: String) -> bool:
	var diagnostics := workspace.find_child("VisualScriptCustomCodeDiagnostics", true, false) as Label
	var popup := workspace.find_child("VisualScriptCustomCodeDiagnosticsPopup", true, false) as PopupPanel
	var trigger := workspace.find_child("VisualScriptCustomCodeDiagnosticsTrigger", true, false) as Button
	if not _need(diagnostics != null and popup != null and trigger != null and not diagnostics.is_visible_in_tree(),
			"Diagnostics start behind a toolbar button rather than taking editor space"):
		return false
	code_edit.text = "owner.play(\"nonnumeric\")\n" + valid_body
	if not await _expect_type_error(diagnostics, 1):
		return false
	trigger.pressed.emit()
	await get_tree().process_frame
	if not _need(popup.visible and diagnostics.is_visible_in_tree() and trigger.text.contains("error") and
			not trigger.text.begins_with("0 error") and
			not (code_edit.get_window() as ConfirmationDialog).dialog_close_on_escape,
			"The button opens diagnostics with a count and temporarily owns Escape"):
		return false
	var native_escape := OS.get_environment("CRIMSON_CAPTURE_NATIVE_ESCAPE")
	if not native_escape.is_empty():
		var target := popup.get_window_id()
		if popup.is_embedded():
			var embedder := code_edit.get_window()
			while embedder.is_embedded():
				embedder = embedder.get_parent().get_window()
			target = embedder.get_window_id()
		var marker := FileAccess.open(native_escape, FileAccess.WRITE)
		marker.store_string(str(DisplayServer.window_get_native_handle(DisplayServer.WINDOW_HANDLE, target)))
		marker.close()
		for attempt in range(COMPLETION_ATTEMPTS):
			if not popup.visible:
				break
			await get_tree().create_timer(0.10).timeout
		if not _need(not popup.visible and code_edit.text.begins_with("owner.play(\"nonnumeric\")"),
				"A real Windows Escape dismisses only diagnostics and preserves the code draft"):
			return false
		print("CRIMSON_CUSTOM_CODE_NATIVE_ESCAPE_PASSED")
	else:
		popup.hide()
	await get_tree().process_frame
	if not _need(code_edit.is_visible_in_tree() and trigger.has_focus(), "Dismiss only diagnostics and restore the toolbar focus"):
		return false
	if not await _verify_diagnostics_reopen(code_edit, popup, trigger, diagnostics):
		return false
	code_edit.text = INVALID_HELPER + valid_body
	return await _expect_type_error(diagnostics, 2)


func _verify_diagnostics_reopen(code_edit: CodeEdit, popup: PopupPanel, trigger: Button, diagnostics: Label) -> bool:
	const WARNING_DRAFT := "var unused_draft_count: int = 1\nowner.play()"
	const CLEAN_DRAFT := "owner.set_meta(&\"preview\", true)"
	for draft: String in [WARNING_DRAFT, CLEAN_DRAFT]:
		code_edit.text = draft
		var expects_warning := draft == WARNING_DRAFT
		for attempt in range(COMPLETION_ATTEMPTS):
			if (expects_warning and diagnostics.text.contains("unused_draft_count")) or (
					not expects_warning and diagnostics.text == "No GDScript diagnostics."):
				break
			await get_tree().create_timer(0.10).timeout
		if not _need(not popup.visible, "A new diagnostics response does not reopen a dismissed popup"):
			return false
		trigger.pressed.emit()
		await get_tree().process_frame
		var current_message := diagnostics.text
		var fresh := current_message.begins_with("Warning ") and current_message.contains("unused_draft_count") if expects_warning \
				else current_message == "No GDScript diagnostics."
		if not _need(popup.visible and diagnostics.is_visible_in_tree() and fresh and not current_message.contains("Error "),
				"Reopening diagnostics shows only the current draft's results. Got: " + current_message):
			return false
		popup.hide()
		await get_tree().process_frame
		if not _need(code_edit.is_visible_in_tree() and trigger.has_focus() and code_edit.text == draft,
				"Repeated diagnostics dismissal restores focus and preserves the full draft"):
			return false
	return true


func _expect_type_error(diagnostics: Label, visible_line: int, type_name: String = "float") -> bool:
	for attempt in range(COMPLETION_ATTEMPTS):
		var location := "Error %s:" % visible_line
		if diagnostics.text.contains(location) and diagnostics.text.contains("String") and diagnostics.text.contains(type_name):
			return true
		await get_tree().create_timer(0.10).timeout
	return _need(false, "Godot maps a native type error to visible line %s. Got: %s" % [visible_line, diagnostics.text])


func _need(condition: bool, description: String) -> bool:
	if not condition:
		push_error("Custom Script editor fixture: " + description)
		get_tree().quit(1)
	return condition
