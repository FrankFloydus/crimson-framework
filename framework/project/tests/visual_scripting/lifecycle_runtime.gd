# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const ENTERED := &"_enter_tree"
const READY := &"_ready"
const EXITED := &"_exit_tree"
const PROCESS := &"_process"
const PHYSICS := &"_physics_process"
const INPUT := &"_input"
const SHORTCUT := &"_shortcut_input"
const UNHANDLED_KEY := &"_unhandled_key_input"
const UNHANDLED := &"_unhandled_input"
const COMPLETE_MARKER := "CRIMSON_VISUAL_SCRIPT_LIFECYCLE_PASSED"


func _initialize() -> void:
	_verify.call_deferred()


func _verify() -> void:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	if not _need(graph_script != null, "Register the VisualScript resource."):
		quit(1)
		return
	graph_script.call(&"set_graph_base", &"Node")
	graph_script.call(&"set_graph", _graph())
	var graph_owner := Node.new()
	graph_owner.set_script(graph_script)
	root.add_child(graph_owner)
	await physics_frame
	await physics_frame
	await process_frame
	var passed := _verify_frames(graph_owner)
	if passed:
		passed = _verify_viewport_input(graph_owner)
	root.remove_child(graph_owner)
	if passed:
		passed = _verify_detached(graph_owner)
	graph_owner.free()
	if passed:
		print(COMPLETE_MARKER)
	quit(0 if passed else 1)


func _verify_frames(graph_owner: Node) -> bool:
	var frame_delta: Variant = graph_owner.get_meta(PROCESS, 0)
	var physics_delta: Variant = graph_owner.get_meta(PHYSICS, 0)
	return (_need(graph_owner.get_meta(ENTERED, false) and graph_owner.get_meta(READY, false),
			"Enter Tree and Ready run after the graph owner enters the scene.")
			and _need(frame_delta is float and frame_delta > 0.0 and physics_delta is float and physics_delta > 0.0,
					"Process and Physics Process receive positive native float deltas."))


func _verify_viewport_input(graph_owner: Node) -> bool:
	var key_event := InputEventKey.new()
	key_event.keycode = KEY_F7
	key_event.pressed = true
	root.push_input(key_event)
	for callback: StringName in [INPUT, SHORTCUT, UNHANDLED_KEY, UNHANDLED]:
		var received := graph_owner.get_meta(callback, false) as InputEventKey
		if not _need(received != null and received.keycode == KEY_F7 and received.pressed,
				"%s receives the stock viewport's typed keyboard event." % callback):
			return false
	var mouse_event := InputEventMouseButton.new()
	mouse_event.button_index = MOUSE_BUTTON_RIGHT
	mouse_event.pressed = true
	root.push_input(mouse_event)
	for callback: StringName in [INPUT, UNHANDLED]:
		var received := graph_owner.get_meta(callback, false) as InputEventMouseButton
		if not _need(received != null and received.button_index == MOUSE_BUTTON_RIGHT and received.pressed,
				"%s receives non-keyboard input through the stock viewport." % callback):
			return false
	return _need(graph_owner.get_meta(SHORTCUT, false) is InputEventKey
			and graph_owner.get_meta(UNHANDLED_KEY, false) is InputEventKey,
			"Keyboard-only viewport callbacks do not run for a mouse button.")


func _verify_detached(graph_owner: Node) -> bool:
	var last_input: Variant = graph_owner.get_meta(INPUT, false)
	var late_key := InputEventKey.new()
	late_key.keycode = KEY_F8
	late_key.pressed = true
	root.push_input(late_key)
	return (_need(graph_owner.get_meta(EXITED, false), "Exit Tree runs when the owner leaves the scene.")
			and _need(graph_owner.get_meta(INPUT, false) == last_input,
					"The detached owner does not receive later viewport events."))


func _graph() -> Dictionary:
	var lifecycle_pins: Dictionary[StringName, StringName] = {
		ENTERED: &"", READY: &"", EXITED: &"", PROCESS: &"delta", PHYSICS: &"delta",
		INPUT: &"event", SHORTCUT: &"event", UNHANDLED_KEY: &"event", UNHANDLED: &"event",
	}
	var nodes: Array[Dictionary] = []
	var connections: Array[Dictionary] = []
	for callback: StringName in lifecycle_pins:
		var event_id := nodes.size() + 1
		var write_id := event_id + 1
		var payload := lifecycle_pins[callback]
		nodes.append({"id": event_id, "kind": &"lifecycle", "callback": callback})
		nodes.append({
			"id": write_id, "kind": &"call_method",
			"member": {"class": &"Object", "name": &"set_meta", "inputs": [
				{"id": &"arg_0", "label": "Name", "type": TYPE_STRING_NAME},
				{"id": &"arg_1", "label": "Value", "type": TYPE_NIL},
			]},
			"values": {&"arg_0": callback, &"arg_1": true},
		})
		connections.append({"from": event_id, "from_pin": &"then", "to": write_id, "to_pin": &"in"})
		if payload != &"":
			connections.append({"from": event_id, "from_pin": payload, "to": write_id, "to_pin": &"arg_1"})
	return {"version": 1, "nodes": nodes, "connections": connections}


func _need(condition: bool, description: String) -> bool:
	if not condition:
		push_error("Visual Script lifecycle fixture: " + description)
	return condition
