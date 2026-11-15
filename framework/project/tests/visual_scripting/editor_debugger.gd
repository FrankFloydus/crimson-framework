# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin
## Drives the embedded graph debugger against a game launched through Godot's editor.

const SCRIPT_PATH := "res://tests/visual_scripting/debugger_steps.crimsonscript"
const SCENE_PATH := "res://tests/visual_scripting/debugger_steps.tscn"
const GAME_LOG := "user://embedded-visual-debugger.log"
const PAUSE_TIMEOUT_SECONDS := 3.0
const READY_NODE := 10
const FUNCTION_CALL := 11
const AFTER_FUNCTION := 12
const MACRO_CALL := 13
const AFTER_MACRO := 14
const FUNCTION_ENTRY := 31
const FUNCTION_ASSIGN := 32
const FUNCTION_ADD := 33
const MACRO_ENTRY := 41
const CONTINUE := "VisualScriptDebugContinue"
const STEP_OVER := "VisualScriptDebugStepOver"
const STEP_INTO := "VisualScriptDebugStepIn"
const STEP_OUT := "VisualScriptDebugStepOut"
const DEBUG_SETTINGS := "crimson_visual_scripting"
const WATCH_SETTINGS := "watches"
const WATCH_PIN := "Watch Pin"
const REMOVE_WATCH := "Remove Watch"
const UNAVAILABLE := "Not evaluated"
const OUTSIDE_CALL_STACK := "Outside this call stack"

## The actual Crimson graph editor, opened through Godot's resource editor interface.
var _workspace: Control
## Native graph canvas rebuilt as stepping crosses graph boundaries.
var _canvas: GraphEdit
## Status badge and tree updated by the editor's remote debugger integration.
var _status: Label
var _values: Tree
var _debug_panel: Control
var _debug_toolbar: Control
## The paused node title and scene instance appear before the value rows.
var _debug_context: Label
var _debug_location: Label
## Each action is driven through the toolbar's Button signal.
var _actions: Dictionary[String, Button] = {}


## Wait for Godot and Crimson to finish registering their plugins.
func _enter_tree() -> void:
	_verify.call_deferred()


## Run to a startup breakpoint and follow both a nested function and a macro.
func _verify() -> void:
	await get_tree().process_frame
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().process_frame
	await get_tree().process_frame
	var graph_script := ResourceLoader.load(SCRIPT_PATH, "Script") as Script
	if not _require(graph_script != null, "The stepping graph is loadable."):
		return
	EditorInterface.edit_script(graph_script, -1, 0, true)
	await get_tree().process_frame
	_workspace = get_tree().root.find_child("CrimsonVisualScriptWorkspace", true, false) as Control
	if not _require(_workspace != null and _workspace.is_visible_in_tree(), "Godot opens the graph editor."):
		return
	_canvas = _workspace.find_child("GraphCanvas", true, false) as GraphEdit
	_status = _workspace.find_child("VisualScriptDebugStatus", true, false) as Label
	_values = _workspace.find_child("VisualScriptDebugValues", true, false) as Tree
	_debug_panel = _workspace.find_child("VisualScriptDebugPane", true, false) as Control
	_debug_toolbar = _workspace.find_child("VisualScriptDebugToolbar", true, false) as Control
	_debug_context = _workspace.find_child("VisualScriptDebugContext", true, false) as Label
	_debug_location = _workspace.find_child("VisualScriptDebugLocation", true, false) as Label
	for control_name: String in [CONTINUE, STEP_OVER, STEP_INTO, STEP_OUT]:
		_actions[control_name] = _workspace.find_child(control_name, true, false) as Button
	if not _require(_canvas != null and _status != null and _values != null and _debug_panel != null and
		_debug_toolbar != null and _debug_context != null and _debug_location != null and not _actions.values().has(null),
		"The workspace contains the toolbar and its live values pane."):
		return
	var ready_card := _card(READY_NODE)
	if not _require(ready_card != null and not _debug_panel.is_visible_in_tree(), "Debug controls are closed before play."):
		return
	_canvas.set_selected(ready_card)
	_canvas.grab_focus()
	var editor_settings := EditorInterface.get_editor_settings()
	var breakpoints: Dictionary = editor_settings.get_project_metadata(DEBUG_SETTINGS, "breakpoints", {})
	if breakpoints.get(SCRIPT_PATH, PackedInt32Array()).has(READY_NODE):
		_press_key(KEY_F9)
		await get_tree().process_frame
	_press_key(KEY_F9)
	await get_tree().process_frame
	breakpoints = editor_settings.get_project_metadata(DEBUG_SETTINGS, "breakpoints", {})
	if not _require(breakpoints.get(SCRIPT_PATH, PackedInt32Array()).has(READY_NODE), "F9 sets the launch breakpoint."):
		return

	EditorInterface.play_custom_scene(SCENE_PATH)
	if not await _wait_until(_paused_at.bind(READY_NODE), "The startup breakpoint opens the embedded debugger."):
		return
	if not _require(_debug_panel.is_visible_in_tree() and _has_value("Instance Variables", "starting_score", "17") and
		_has_value("Instance Variables", "sample_label", "Caff\u00E8 \u6771\u4EAC"),
		"The live pane displays instance variables, including text from the saved graph, without changing Unicode."):
		return
	if not await _advance(STEP_OVER, FUNCTION_CALL):
		return
	if not _require(_debug_context.text.contains("add_bonus") and _debug_location.text == "Event Graph \u00B7 DebuggerSteps" and
		_has_value("Inputs", "Amount", "42"), "The paused preview names the call and instance and shows its resolved argument."):
		return
	var null_tooltip := await _pin_tooltip(FUNCTION_CALL, &"optional_target", false)
	if not _require(_has_value("Inputs", "Optional Target", "null") and null_tooltip.begins_with("add_bonus \u00B7 Optional Target\n") and
		null_tooltip.contains("null") and
		not null_tooltip.contains(UNAVAILABLE), "A resolved null is displayed as a value rather than an unevaluated pin."):
		return
	if not await _pin_menu_action(FUNCTION_CALL, &"bonus", true, WATCH_PIN):
		return
	if not _require(_has_value("Watches", "Bonus", UNAVAILABLE), "A watched return pin without a result reports that it has yet to evaluate."):
		return
	if not await _advance(STEP_INTO, FUNCTION_ENTRY):
		return
	if not _require(_has_value("Inputs", "Amount", "42") and _row("Call Stack", "add_bonus") != null and
		_row("Call Stack", "Event Graph") != null, "Stepping into the function displays its argument and both stack frames."):
		return
	var unevaluated_tooltip := await _pin_tooltip(FUNCTION_ADD, &"result", true)
	if not _require(unevaluated_tooltip.contains(UNAVAILABLE), "Hovering an unreached data pin reports that it has yet to evaluate."):
		return
	if not await _pin_menu_action(FUNCTION_ASSIGN, &"value", false, WATCH_PIN):
		return
	if not _require(_has_value("Watches", "Value", UNAVAILABLE), "An input pin can also be watched before its node evaluates it."):
		return
	_press_key(KEY_F10)
	if not await _wait_until(_paused_at.bind(FUNCTION_ASSIGN), "F10 steps to the next instruction inside the function."):
		return
	if not _require(_has_value("Inputs", "Value", "50") and _has_value("Computed", "Result", "50") and
		_has_value("Local Variables", "running_total", "0"), "The computed sum is available before the assignment mutates its local."):
		return
	var result_tooltip := await _pin_tooltip(FUNCTION_ADD, &"result", true)
	var input_tooltip := await _pin_tooltip(FUNCTION_ASSIGN, &"value", false)
	var live_input := _row("Inputs", "Value")
	if not _require(result_tooltip.contains("50") and not result_tooltip.contains(UNAVAILABLE) and
		input_tooltip.contains("50") and input_tooltip.contains("Add \u2192 Result") and _has_value("Watches", "Value", "50") and
		live_input != null and live_input.get_tooltip_text(0).contains("Add \u2192 Result"),
		"The watch and input row show the evaluated sum, and the pin and row tooltips show its producer."):
		return
	if not await _advance(STEP_OUT, AFTER_FUNCTION):
		return
	if not _require(_has_value("Watches", "Bonus", "50") and _has_value("Watches", "Value", OUTSIDE_CALL_STACK),
		"Stepping out exposes the returned bonus and clears the captured callee input."):
		return
	_values.button_clicked.emit(_row("Watches", "Value"), 1, 0, MOUSE_BUTTON_LEFT)
	if not _require(_row("Watches", "Value") == null and _watch_saved(FUNCTION_CALL, &"bonus"),
		"Removing the callee watch clears its row and keeps the caller's saved watch."):
		return
	if not await _advance(STEP_OVER, MACRO_CALL) or not await _advance(STEP_INTO, MACRO_ENTRY):
		return
	if not await _advance(STEP_OUT, AFTER_MACRO):
		return
	if not _require(_press(CONTINUE), "Continue is available inside the graph."):
		return
	if not await _wait_until(_graph_running, "The toolbar reflects running state after Continue."):
		return
	if not _require(_actions.values().all(func(action: Button) -> bool: return action.disabled),
		"Running state disables every execution control."):
		return
	if not _require(_has_value("Watches", "Bonus", "Waiting for pause"), "A saved watch remains without showing the previous value while running."):
		return
	if not await _wait_until(_debug_closed, "The game exits on its frame limit and hides the stepping controls."):
		return
	if not await _wait_until(_last_action_ran, "Continue executes the caller's final action and the game flushes its log."):
		return
	if not _require(_debug_panel.is_visible_in_tree() and _has_value("Watches", "Bonus", "Waiting for pause") and
		_watch_saved(FUNCTION_CALL, &"bonus"), "The remaining watch stays visible and persisted after the game stops."):
		return
	if not await _verify_restart():
		return
	if not await _pin_menu_action(FUNCTION_CALL, &"bonus", true, REMOVE_WATCH):
		return
	if not _require(not _watch_saved(FUNCTION_CALL, &"bonus") and not _debug_panel.is_visible_in_tree(),
		"Removing the final watch while stopped closes the idle pane."):
		return
	print("CRIMSON_VISUAL_SCRIPT_EMBEDDED_DEBUGGER_PASSED")
	get_tree().quit()


## Verify a new game starts a new live session while preserving the authored watch and breakpoint.
func _verify_restart() -> bool:
	EditorInterface.play_custom_scene(SCENE_PATH)
	if not await _wait_until(_paused_at.bind(READY_NODE), "Relaunch stops at the same startup breakpoint."):
		return false
	if not _require(_has_value("Watches", "Bonus", UNAVAILABLE) and _watch_saved(FUNCTION_CALL, &"bonus"),
			"The persisted watch starts unevaluated instead of showing the previous session's computed result."):
		return false
	if not _require(_press(CONTINUE), "The restarted session can continue from the graph."):
		return false
	return await _wait_until(_debug_closed, "The restarted session stops and leaves no active stepping controls.")


## Return the current canvas widget for a saved graph node ID.
func _card(node_id: int) -> GraphNode:
	return _canvas.get_node_or_null(NodePath("GraphNode_%d" % node_id)) as GraphNode


## Resolve a port by saved pin ID and center it if the current pause framed a different node.
func _pin_point(node_id: int, pin_id: StringName, output: bool) -> Vector2:
	var widget := _card(node_id)
	if widget == null:
		return Vector2.INF
	var port_count := widget.get_output_port_count() if output else widget.get_input_port_count()
	for port_index in range(port_count):
		var slot := widget.get_output_port_slot(port_index) if output else widget.get_input_port_slot(port_index)
		var descriptor: Dictionary = widget.get_slot_metadata_right(slot) if output else widget.get_slot_metadata_left(slot)
		if StringName(descriptor.get("id", "")) != pin_id:
			continue
		var local_socket := widget.get_output_port_position(port_index) if output else widget.get_input_port_position(port_index)
		var canvas_socket := widget.position + local_socket * _canvas.zoom
		if not Rect2(Vector2.ZERO, _canvas.size).grow(-16.0).has_point(canvas_socket):
			_canvas.scroll_offset += canvas_socket - _canvas.size * 0.5
			await get_tree().process_frame
			await get_tree().process_frame
		return widget.position + local_socket * _canvas.zoom
	return Vector2.INF


## Ask the native tooltip provider exactly what it displays at the socket.
func _pin_tooltip(node_id: int, pin_id: StringName, output: bool) -> String:
	var point := await _pin_point(node_id, pin_id, output)
	return _canvas.get_tooltip(point) if point.is_finite() else ""


## Drive the real pin context menu and select an item through its native signal.
func _pin_menu_action(node_id: int, pin_id: StringName, output: bool, action_text: String) -> bool:
	var canvas_point := await _pin_point(node_id, pin_id, output)
	if not _require(canvas_point.is_finite(), "Find the authored pin socket for %s." % pin_id):
		return false
	var window_point := _canvas.get_global_transform_with_canvas() * canvas_point
	for pressed_state: bool in [true, false]:
		var click_event := InputEventMouseButton.new()
		click_event.button_index = MOUSE_BUTTON_RIGHT
		click_event.button_mask = MOUSE_BUTTON_MASK_RIGHT if pressed_state else 0
		click_event.position = window_point
		click_event.global_position = window_point
		click_event.pressed = pressed_state
		_workspace.get_window().push_input(click_event, true)
	await get_tree().process_frame
	var pin_menu := _workspace.find_child("GraphContextMenu", true, false) as PopupMenu
	if not _require(pin_menu != null and pin_menu.visible, "Right-clicking a data pin opens its native menu."):
		return false
	for item_index in range(pin_menu.item_count):
		if pin_menu.get_item_text(item_index) == action_text and not pin_menu.is_item_disabled(item_index):
			pin_menu.id_pressed.emit(pin_menu.get_item_id(item_index))
			if pin_menu.visible:
				pin_menu.hide()
			await get_tree().process_frame
			return true
	return _require(false, "The pin context menu offers %s." % action_text)


## Check that only the address, never the paused value, is persisted with this project.
func _watch_saved(node_id: int, pin_id: StringName) -> bool:
	var editor_settings := EditorInterface.get_editor_settings()
	var stored_watches: Dictionary = editor_settings.get_project_metadata(DEBUG_SETTINGS, WATCH_SETTINGS, {})
	for entry: Dictionary in stored_watches.get(SCRIPT_PATH, []):
		if entry.get("node") == node_id and entry.get("output") and StringName(entry.get("pin", "")) == pin_id:
			return not entry.has("value")
	return false


## Match paused status and the focused graph node together.
func _paused_at(node_id: int) -> bool:
	var card := _card(node_id)
	return _status != null and _status.text == "Paused" and card != null and card.selected and not _actions[CONTINUE].disabled


## Match the running state after the native debugger resumes the game.
func _graph_running() -> bool:
	return _status.text == "Running"


## Detect when the native game session stops and its stepping controls disappear.
func _debug_closed() -> bool:
	return not _debug_toolbar.is_visible_in_tree()


## Read the isolated child's log after the final action has executed.
func _last_action_ran() -> bool:
	if not FileAccess.file_exists(GAME_LOG):
		return false
	var output := FileAccess.get_file_as_string(GAME_LOG).replace("\r", "").split("\n")
	return output.count("Function returned.") == 1 and output.count("Macro ran.") == 1 and output.count("Caller resumed.") == 1


## Dispatch the same callback that runs when the toolbar button is clicked.
func _press(control_name: String) -> bool:
	var action := _actions[control_name]
	if not action.is_visible_in_tree() or action.disabled:
		return false
	action.pressed.emit()
	return true


## Deliver a graph-local shortcut with the canvas focused.
func _press_key(key_code: Key) -> void:
	_canvas.grab_focus()
	var key_event := InputEventKey.new()
	key_event.keycode = key_code
	key_event.pressed = true
	_workspace.get_window().push_input(key_event, true)


## Move execution using the selected toolbar action and wait for the next pause.
func _advance(control_name: String, node_id: int) -> bool:
	if not _require(_press(control_name), "The active graph toolbar accepts %s." % control_name):
		return false
	return await _wait_until(_paused_at.bind(node_id), "%s focuses the next expected node %d." % [control_name, node_id])


## Look up a live row inside its named group without relying on row order.
func _row(group_name: String, row_name: String) -> TreeItem:
	var root_item := _values.get_root()
	if root_item == null:
		return null
	for group: TreeItem in root_item.get_children():
		if group.get_text(0) == group_name:
			for child: TreeItem in group.get_children():
				if child.get_text(0).contains(row_name):
					return child
	return null


## Assert the live row's value independently from its display column metadata.
func _has_value(group_name: String, row_name: String, expected_value: String) -> bool:
	var matching := _row(group_name, row_name)
	return matching != null and matching.get_text(1) == expected_value


## Bound each asynchronous transition while allowing Godot to deliver debugger packets.
func _wait_until(predicate: Callable, expectation: String) -> bool:
	var deadline := get_tree().create_timer(PAUSE_TIMEOUT_SECONDS)
	while deadline.time_left > 0.0:
		if predicate.call():
			return true
		await get_tree().process_frame
	return _require(false, expectation)


## Stop the owned game and record the failed expectation before closing the editor.
func _require(condition: bool, expectation: String) -> bool:
	if not condition:
		if EditorInterface.is_playing_scene():
			EditorInterface.stop_playing_scene()
		push_error("[Crimson] Embedded visual debugger fixture: " + expectation)
		get_tree().quit(1)
	return condition
