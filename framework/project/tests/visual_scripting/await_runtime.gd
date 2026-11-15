# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

## The graph uses this explicitly named custom event as its test entry point.
const START := &"begin_wait_fixture"
## The fixture-owned nominal emitter, including in selected-resource packs.
const EMITTER_PATH := "res://tests/visual_scripting/await_emitter.gd"
## Stable test event declaration ID.
const EVENT_ID := 1


## Begin after the scene tree is initialized.
func _initialize() -> void:
	_verify.call_deferred()


## Build a Visual Script backed by an Event Graph fixture.
func _fixture(nodes: Array, edges: Array, input_pins: Array = [], base_class: StringName = &"Node") -> Node:
	var graph_script := ClassDB.instantiate(&"VisualScript") as Script
	graph_script.call(&"set_graph_base", base_class)
	graph_script.call(&"set_graph", {"version": 1, "nodes": nodes, "connections": edges,
		"events": [{"id": EVENT_ID, "name": START, "inputs": input_pins}]})
	var owner_node := ClassDB.instantiate(base_class) as Node
	owner_node.set_script(graph_script)
	root.add_child(owner_node)
	return owner_node


## Describe a graph pin using its known static type.
func _pin(pin_id: StringName, type_id: int, node_class: StringName = &"") -> Dictionary:
	var descriptor := {"id": pin_id, "label": String(pin_id), "type": type_id}
	if node_class != &"":
		descriptor["class"] = node_class
	return descriptor


## Start a custom event and expose its typed arguments as outputs.
func _entry() -> Dictionary:
	return {"id": 1, "kind": &"custom_event", "definition": EVENT_ID}


## Forward an owned value to metadata for assertions without running another graph.
func _capture(node_id: int, meta_key: StringName, default_value: Variant = null) -> Dictionary:
	return {"id": node_id, "kind": &"call_method", "member": {"class": &"Object", "name": &"set_meta",
		"inputs": [_pin(&"arg_0", TYPE_STRING_NAME), _pin(&"arg_1", TYPE_NIL)]},
		"values": {&"arg_0": meta_key, &"arg_1": default_value}}


## Connect two stable graph endpoints.
func _wire(from_id: int, from_port: StringName, to_id: int, to_port: StringName) -> Dictionary:
	return {"from": from_id, "from_pin": from_port, "to": to_id, "to_pin": to_port}


## Verify a boolean and tell the main fixture exactly which scenario failed.
func _check(passed: bool, description: String) -> bool:
	if not passed:
		push_error("[Crimson] Visual Script wait fixture: " + description)
	return passed


## Allow the timer, deferred signal, and temporary liveness monitor to run.
func _flush_waits() -> void:
	for _frame_index in 3:
		await process_frame


## Preserve event data, nested Sequence order, and repeated waits in the same resumed branch.
func _delay_sequence() -> bool:
	var owner_node := _fixture([
		_entry(), {"id": 2, "kind": &"sequence"}, {"id": 3, "kind": &"sequence"},
		{"id": 4, "kind": &"delay", "values": {&"duration": 0.0}}, _capture(5, &"value_before"),
		{"id": 6, "kind": &"delay", "values": {&"duration": 0.0}}, _capture(7, &"inner_completed", true),
		{"id": 8, "kind": &"call_method", "member": {"class": &"Object", "name": &"has_meta",
			"inputs": [_pin(&"arg_0", TYPE_STRING_NAME)], "output": _pin(&"result", TYPE_BOOL)},
			"values": {&"arg_0": &"inner_completed"}}, _capture(9, &"sequence_order"),
	], [
		_wire(1, &"then", 2, &"in"), _wire(2, &"then_0", 3, &"in"), _wire(3, &"then_0", 4, &"in"),
		_wire(4, &"then", 5, &"in"), _wire(1, &"payload", 5, &"arg_1"), _wire(5, &"then", 6, &"in"),
		_wire(6, &"then", 7, &"in"), _wire(2, &"then_1", 8, &"in"), _wire(8, &"then", 9, &"in"),
		_wire(8, &"result", 9, &"arg_1"),
	], [_pin(&"payload", TYPE_INT)])
	owner_node.call(START, 37)
	var deferred := not owner_node.has_meta(&"value_before") and not owner_node.has_meta(&"sequence_order")
	await _flush_waits()
	var resumed: bool = owner_node.get_meta(&"value_before", 0) == 37 and owner_node.get_meta(&"sequence_order", false) == true
	owner_node.free()
	return _check(deferred and resumed, "Delay must resume in order, retaining event payload through consecutive waits.")


## A nonzero game-time Delay must wait across a paused scene tree.
func _paused_delay() -> bool:
	const DURATION := 0.015
	var owner_node := _fixture([_entry(), {"id": 2, "kind": &"delay", "values": {&"duration": DURATION}},
		_capture(3, &"finished", true)], [_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in")])
	paused = true
	owner_node.call(START)
	await _flush_waits()
	var stayed_paused := not owner_node.has_meta(&"finished")
	paused = false
	await _flush_waits()
	var finished: bool = owner_node.get_meta(&"finished", false)
	owner_node.free()
	return _check(stayed_paused and finished, "A nonzero Delay must advance after the scene tree is unpaused.")


## Set up a read-compatible generic await with actual emitted signal arguments.
func _generic_owner() -> Node:
	return _fixture([_entry(), {"id": 2, "kind": &"await_signal"}, _capture(3, &"arguments")], [
		_wire(1, &"then", 2, &"in"), _wire(1, &"pulse", 2, &"signal"),
		_wire(2, &"then", 3, &"in"), _wire(2, &"arguments", 3, &"arg_1"),
	], [_pin(&"pulse", TYPE_SIGNAL)])


## Two outstanding waits consume their own first emissions and release their connections.
func _generic_concurrent() -> bool:
	var emitter := Node.new()
	emitter.add_user_signal(&"pulse", [{"name": "first", "type": TYPE_INT}, {"name": "second", "type": TYPE_STRING}])
	root.add_child(emitter)
	var other_source := Timer.new()
	root.add_child(other_source)
	var owner_node := _generic_owner()
	owner_node.call(START, Signal(emitter, &"pulse"))
	owner_node.call(START, other_source.timeout)
	emitter.emit_signal(&"pulse", 4, "first")
	emitter.emit_signal(&"pulse", 5, "second")
	await _flush_waits()
	var first_only: bool = owner_node.get_meta(&"arguments", []) == [4, "first"] and emitter.get_signal_connection_list(&"pulse").is_empty()
	other_source.timeout.emit()
	await _flush_waits()
	var second_delivered: bool = owner_node.get_meta(&"arguments", [1]) == []
	owner_node.free()
	emitter.free()
	other_source.free()
	return _check(first_only and second_delivered, "Concurrent generic waits must consume exactly the next emission from their own source.")


## Verify a typed native signal's data pins and Sender on a self reference.
func _typed_self() -> bool:
	var value_pin := _pin(&"arg_0", TYPE_FLOAT)
	var owner_node := _fixture([
		_entry(), {"id": 2, "kind": &"self"},
		{"id": 3, "kind": &"await_signal", "class": &"Range", "target_class": &"HSlider",
			"signal": &"value_changed", "inputs": [value_pin]},
		_capture(4, &"changed_value"), _capture(5, &"sender"),
	], [
		_wire(1, &"then", 3, &"in"), _wire(2, &"node", 3, &"target"),
		_wire(3, &"then", 4, &"in"), _wire(3, &"arg_0", 4, &"arg_1"),
		_wire(4, &"then", 5, &"in"), _wire(3, &"sender", 5, &"arg_1"),
	], [], &"HSlider") as HSlider
	owner_node.call(START)
	owner_node.value = 27.0
	await _flush_waits()
	var valid: bool = owner_node.get_meta(&"changed_value", 0.0) == 27.0 and owner_node.get_meta(&"sender", null) == owner_node
	owner_node.free()
	return _check(valid, "A typed native self-signal must publish its payload and same owner as Sender.")


## Verify a typed external scripted signal through the existing nominal script descriptors.
func _typed_script() -> bool:
	var emitter := Node.new()
	emitter.set_script(ResourceLoader.load(EMITTER_PATH, "Script") as Script)
	root.add_child(emitter)
	var descriptor := _pin(&"source", TYPE_OBJECT, &"Node")
	descriptor["script_path"] = EMITTER_PATH
	descriptor["script_name"] = "FixtureEmitter"
	var owner_node := _fixture([
		_entry(), {"id": 2, "kind": &"await_signal", "class": &"Node", "target_class": &"Node", "signal": &"receipt",
			"script_path": EMITTER_PATH, "script_name": "FixtureEmitter", "inputs": [_pin(&"arg_0", TYPE_INT)]},
		_capture(3, &"quantity"),
	], [
		_wire(1, &"then", 2, &"in"), _wire(1, &"source", 2, &"target"),
		_wire(2, &"then", 3, &"in"), _wire(2, &"arg_0", 3, &"arg_1"),
	], [descriptor])
	owner_node.call(START, emitter)
	emitter.emit_signal(&"receipt", 9)
	await _flush_waits()
	var valid: bool = owner_node.get_meta(&"quantity", 0) == 9
	owner_node.free()
	emitter.free()
	return _check(valid, "Await on a typed external script signal must preserve the declared quantity.")


## Check owner/emitter lifecycle interruption and native callback removal.
func _cancel_pending() -> bool:
	for mode: String in ["exit", "detach", "free", "queued_resume", "queued_owner", "source_exit", "source_free"]:
		var emitter := Node.new()
		emitter.add_user_signal(&"pulse")
		root.add_child(emitter)
		var owner_node := _generic_owner()
		var observed_exit: Array[bool] = []
		owner_node.tree_exiting.connect(func() -> void: observed_exit.append(owner_node.has_meta(&"arguments")))
		owner_node.call(START, Signal(emitter, &"pulse"))
		var connected := emitter.get_signal_connection_list(&"pulse").size() == 1
		match mode:
			"exit": root.remove_child(owner_node)
			"detach": owner_node.set_script(null)
			"free": owner_node.free()
			"queued_resume":
				emitter.emit_signal(&"pulse")
				owner_node.set_script(null)
			"queued_owner":
				owner_node.queue_free()
				emitter.emit_signal(&"pulse")
			"source_exit": root.remove_child(emitter)
			"source_free": emitter.free()
		await _flush_waits()
		var cleared := not is_instance_valid(emitter) or emitter.get_signal_connection_list(&"pulse").is_empty()
		var no_resume := not observed_exit.has(true) and (not is_instance_valid(owner_node) or not owner_node.has_meta(&"arguments"))
		if is_instance_valid(owner_node): owner_node.free()
		if is_instance_valid(emitter): emitter.free()
		if not _check(connected and cleared and no_resume, "Wait must cancel after " + mode + "."):
			return false
	return true


## Exiting and freed sources can still finish an emission already delivered to the wait.
func _terminal_signal() -> bool:
	for terminal_name: StringName in [&"tree_exiting", &"tree_exited", &"pulse"]:
		var emitter := Node.new()
		emitter.add_user_signal(&"pulse")
		root.add_child(emitter)
		var owner_node := _generic_owner()
		owner_node.call(START, Signal(emitter, terminal_name))
		if terminal_name == &"pulse":
			emitter.emit_signal(&"pulse")
		emitter.free()
		await _flush_waits()
		var delivered: bool = owner_node.get_meta(&"arguments", [1]) == []
		owner_node.free()
		if not _check(delivered, "An emitted signal must finish even if its source has left the tree (%s)." % terminal_name):
			return false
	var typed_emitter := Node.new()
	root.add_child(typed_emitter)
	var typed_owner := _fixture([_entry(), {"id": 2, "kind": &"await_signal", "class": &"Node", "signal": &"tree_exited", "inputs": []},
			_capture(3, &"sender"), _capture(4, &"finished", true)], [
		_wire(1, &"then", 2, &"in"), _wire(1, &"source", 2, &"target"), _wire(2, &"then", 3, &"in"),
		_wire(2, &"sender", 3, &"arg_1"), _wire(3, &"then", 4, &"in"),
	], [_pin(&"source", TYPE_OBJECT, &"Node")])
	typed_owner.call(START, typed_emitter)
	typed_emitter.free()
	await _flush_waits()
	var sender_expired: bool = typed_owner.get_meta(&"finished", false) and typed_owner.get_meta(&"sender", false) == null
	typed_owner.free()
	if not _check(sender_expired, "Typed tree_exited must expose a null Sender when its source is already freed."):
		return false
	return true


## Native signals emitted off the main thread may publish data, but never run graph actions there.
func _worker_emission() -> bool:
	var emitter := RefCounted.new()
	emitter.add_user_signal(&"pulse", [{"name": "sequence", "type": TYPE_INT}])
	var owner_node := _generic_owner()
	owner_node.call(START, Signal(emitter, &"pulse"))
	var worker := Thread.new()
	if not _check(worker.start(emitter.emit_signal.bind(&"pulse", 6)) == OK, "Could not start the test signal worker."):
		owner_node.free()
		return false
	worker.wait_to_finish()
	var deferred := not owner_node.has_meta(&"arguments")
	await _flush_waits()
	var delivered: bool = owner_node.get_meta(&"arguments", []) == [6]
	owner_node.free()
	return _check(deferred and delivered, "A worker's native emission must resume on the main thread with its argument.")


## Free a RefCounted emitter without emitting and ensure the pending frame releases its unparented node.
func _disappearing_ref_counted() -> bool:
	const DETACHED_NAME := &"unparented_wait_fixture"
	var owner_node := _fixture([_entry(), {"id": 2, "kind": &"create_node", "class": &"Node"},
		_capture(3, DETACHED_NAME), {"id": 4, "kind": &"await_signal"}, _capture(5, &"resumed", true)], [
		_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in"), _wire(2, &"node", 3, &"arg_1"),
		_wire(3, &"then", 4, &"in"), _wire(1, &"pulse", 4, &"signal"), _wire(4, &"then", 5, &"in"),
	], [_pin(&"pulse", TYPE_SIGNAL)])
	var emitter := RefCounted.new()
	emitter.add_user_signal(&"pulse")
	owner_node.call(START, Signal(emitter, &"pulse"))
	var detached_object: Object = owner_node.get_meta(DETACHED_NAME, null)
	var held_during_wait := is_instance_valid(detached_object)
	emitter = null
	await _flush_waits()
	var expired := not is_instance_valid(detached_object) and not owner_node.has_meta(&"resumed")
	owner_node.free()
	return _check(held_during_wait and expired, "A vanished non-Node emitter must cancel and release the suspended cleanup list.")


## Exercise deliberate wait failures separately so the positive matrix stays free of Godot errors.
func _verify_rejection(mode: String) -> void:
	var owner_node: Node
	if mode == "negative-delay" or mode == "nonfinite-delay":
		var seconds := -1.0 if mode == "negative-delay" else INF
		owner_node = _fixture([_entry(), {"id": 2, "kind": &"delay"}, _capture(3, &"ran", true)], [
			_wire(1, &"then", 2, &"in"), _wire(1, &"input_seconds", 2, &"duration"), _wire(2, &"then", 3, &"in"),
		], [_pin(&"input_seconds", TYPE_FLOAT)])
		owner_node.call(START, seconds)
	elif mode == "loop-body":
		owner_node = _fixture([_entry(), {"id": 2, "kind": &"for"}, {"id": 3, "kind": &"delay"}, _capture(4, &"ran", true)], [
			_wire(1, &"then", 2, &"in"), _wire(2, &"body", 3, &"in"), _wire(3, &"then", 4, &"in"),
		])
		owner_node.call(START)
	elif mode == "missing-signal":
		owner_node = _generic_owner()
		owner_node.call(START, Signal())
	else:
		quit(1)
		return
	await _flush_waits()
	var never_resumed := not owner_node.has_meta(&"ran") and not owner_node.has_meta(&"arguments")
	owner_node.free()
	if not _check(never_resumed, "An invalid wait must not reach Then."):
		quit(1)
		return
	print("CRIMSON_VISUAL_SCRIPT_AWAIT_REJECTION_PASSED ", mode)
	quit()


## Execute the positive, concurrent and interrupted ways an event may wait.
func _verify() -> void:
	var modes := OS.get_cmdline_user_args()
	if not modes.is_empty():
		await _verify_rejection(modes[0])
		return
	Engine.max_fps = 60
	if not await _delay_sequence() or not await _paused_delay() or not await _generic_concurrent() or not await _typed_self() \
			or not await _typed_script() or not await _cancel_pending() or not await _terminal_signal() \
			or not await _worker_emission() or not await _disappearing_ref_counted():
		quit(1)
		return
	print("CRIMSON_VISUAL_SCRIPT_AWAIT_PASSED")
	quit()
