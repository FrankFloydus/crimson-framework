# Copyright Francesco Di Blasi 2026 - All Rights Reserved
@tool
extends EditorPlugin

const GRAPH_PATH: String = "res://canvas_stress.crimsonscript"
const WORKSPACE_NAME: String = "CrimsonVisualScriptWorkspace"
const ZOOM: float = 0.7
const PAN_START: Vector2 = Vector2(100.0, 60.0)
const FIXTURE_POINTER_DEVICE: int = 4507
const WARMUP_SAMPLES: int = 30
const TIMED_SAMPLES: int = 150
const STEPS_PER_LEG: int = 24
const PAN_SAMPLES: PackedInt32Array = [1, 8]
const NODE_COUNTS: PackedInt32Array = [48, 256, 1024]

var _sampling_pan: bool = false


## Wait until the native editor and the Crimson extension are initialized.
func _enter_tree() -> void:
	_measure.call_deferred()


## Keep physical mouse motion and the operating system's warp response out of the measured gesture.
func _input(input_event: InputEvent) -> void:
	if _sampling_pan and input_event is InputEventMouse and input_event.device != FIXTURE_POINTER_DEVICE:
		get_viewport().set_input_as_handled()


## Generate and open a disposable graph, then pan and validate its canvas for each workload.
func _measure() -> void:
	await _frames(8)
	OS.low_processor_usage_mode = false
	Engine.max_fps = 0
	var editor_window: Window = EditorInterface.get_base_control().get_window()
	var work_area: Rect2i = DisplayServer.screen_get_usable_rect(editor_window.current_screen)
	editor_window.mode = Window.MODE_WINDOWED
	editor_window.position = work_area.position + Vector2i(24, 48)
	editor_window.size = Vector2i(1440, 900).min(work_area.size - Vector2i(48, 96))
	var graph_asset: Script = ClassDB.instantiate(&"VisualScript") as Script
	if not _require(graph_asset != null and ResourceSaver.save(graph_asset, GRAPH_PATH, ResourceSaver.FLAG_CHANGE_PATH) == OK,
			"Create a disposable graph resource."):
		return
	EditorInterface.edit_script(graph_asset, -1, 0, true)
	await _frames(8)
	var workspace: Control = editor_window.find_child(WORKSPACE_NAME, true, false) as Control
	if not _require(workspace != null and workspace.is_visible_in_tree(), "Open the Visual Script main screen."):
		return
	var canvas: GraphEdit = workspace.find_child("GraphCanvas", true, false) as GraphEdit
	if not _require(canvas != null, "Find the graph canvas."):
		return
	var results: Array[Dictionary] = []
	for total_nodes: int in NODE_COUNTS:
		var definition: Dictionary = _graph(total_nodes)
		graph_asset.call(&"set_graph", definition)
		await _frames(8)
		canvas.zoom = ZOOM
		canvas.scroll_offset = Vector2(100.0, 80.0)
		await _frames(8)
		var expected_wires: Array = definition["connections"]
		if not _require(canvas.get_connection_list().size() == expected_wires.size(), "Display every generated wire."):
			return
		var original_document: Dictionary = graph_asset.call(&"get_graph")
		for motions_per_frame: int in PAN_SAMPLES:
			var timing: Dictionary = await _sample_pan(editor_window, canvas, motions_per_frame)
			if timing.is_empty():
				return
			timing["nodes"] = total_nodes
			timing["wires"] = expected_wires.size()
			results.append(timing)
		var current_document: Dictionary = graph_asset.call(&"get_graph")
		if not _require(current_document == original_document and not _has_selection(canvas),
				"Panning leaves all nodes, wires, and selection intact."):
			return
	print("CRIMSON_CANVAS_STRESS_PASSED ", JSON.stringify({
		"godot": Engine.get_version_info()["string"],
		"renderer": RenderingServer.get_current_rendering_method(),
		"viewport_size": str(editor_window.size),
		"canvas_size": str(canvas.size),
		"editor_scale": EditorInterface.get_editor_scale(),
		"accumulated_os_input": Input.use_accumulated_input,
		"results": results,
	}))
	get_tree().quit()


## Check whether the normal selection behavior ran during the pan.
func _has_selection(canvas: GraphEdit) -> bool:
	for child_node: Node in canvas.get_children():
		if child_node is GraphElement and (child_node as GraphElement).selected:
			return true
	return false


## Place Print cards in a small grid with one execution wire between adjacent cards.
func _graph(total_nodes: int) -> Dictionary:
	var cards: Array[Dictionary] = []
	var wires: Array[Dictionary] = []
	const COLUMNS: int = 12
	for card_index: int in range(total_nodes):
		var card_id: int = card_index + 1
		var card_position: Vector2 = Vector2(card_index % COLUMNS, floorf(float(card_index) / COLUMNS)) * Vector2(230.0, 150.0)
		card_position.y += 320.0
		cards.append({"id": card_id, "kind": &"print", "position": card_position, "message": "Canvas stress"})
		if card_index % COLUMNS != 0:
			wires.append({"from": card_id - 1, "from_pin": &"then", "to": card_id, "to_pin": &"in"})
	return {"version": 1, "nodes": cards, "connections": wires}


## Deliver actual middle-button input through the editor viewport and time both dispatch and completed frames.
func _sample_pan(editor_window: Window, canvas: GraphEdit, motions_per_frame: int) -> Dictionary:
	const DISTANCE: Vector2 = Vector2(3.0, 1.5)
	canvas.scroll_offset = Vector2(100.0, 80.0)
	await _frames(4)
	var start_scroll: Vector2 = canvas.scroll_offset
	if not _require(Rect2(Vector2.ZERO, canvas.size).has_point(PAN_START + DISTANCE * STEPS_PER_LEG),
			"Leave room for the gesture in the displayed graph canvas."):
		return {}
	var origin: Vector2 = canvas.get_global_transform_with_canvas() * PAN_START
	var pointer: Vector2 = origin
	var frame_samples: Array[int] = []
	var dispatch_samples: Array[int] = []
	var timing_marker: int = Time.get_ticks_usec()
	_sampling_pan = true
	_button(editor_window, origin, true)
	for sample_index: int in range(WARMUP_SAMPLES + TIMED_SAMPLES):
		var step: Vector2 = DISTANCE if sample_index % (STEPS_PER_LEG * 2) < STEPS_PER_LEG else -DISTANCE
		var frame_started: int = Time.get_ticks_usec()
		for _motion_index: int in range(motions_per_frame):
			_motion(editor_window, pointer, step / motions_per_frame)
			pointer += step / motions_per_frame
		var dispatched: int = Time.get_ticks_usec()
		await get_tree().process_frame
		if sample_index >= WARMUP_SAMPLES:
			dispatch_samples.append(dispatched - frame_started)
			frame_samples.append(Time.get_ticks_usec() - frame_started)
		if sample_index == STEPS_PER_LEG - 1 and not _require(canvas.scroll_offset.distance_to(start_scroll) >= 24.0,
				"Middle drag moves the actual GraphEdit viewport."):
			return {}
	_button(editor_window, pointer, false)
	_sampling_pan = false
	await _frames(4)
	if not _require(canvas.scroll_offset.distance_to(start_scroll - (pointer - origin)) <= 5.0,
			"Panning preserves the accumulated pointer translation."):
		return {}
	var settled_scroll: Vector2 = canvas.scroll_offset
	_motion(editor_window, pointer, Vector2(15.0, 0.0), false)
	await _frames(4)
	if not _require(canvas.scroll_offset.is_equal_approx(settled_scroll), "Middle release ends the pan."):
		return {}
	frame_samples.sort()
	dispatch_samples.sort()
	return {
		"motions_per_frame": motions_per_frame,
		"sample_count": frame_samples.size(),
		"elapsed_usec": Time.get_ticks_usec() - timing_marker,
		"frame_p50_usec": _percentile(frame_samples, 0.50),
		"frame_p95_usec": _percentile(frame_samples, 0.95),
		"input_p50_usec": _percentile(dispatch_samples, 0.50),
		"input_p95_usec": _percentile(dispatch_samples, 0.95),
	}


## Compute the nearest-rank percentile from sorted timing samples.
func _percentile(samples: Array[int], percentile: float) -> int:
	return samples[ceili(samples.size() * percentile) - 1]


## Send a middle-button event to the same viewport that owns the graph.
func _button(editor_window: Window, pointer: Vector2, pressed: bool) -> void:
	var mouse_button: InputEventMouseButton = InputEventMouseButton.new()
	mouse_button.position = pointer
	mouse_button.global_position = pointer
	mouse_button.button_index = MOUSE_BUTTON_MIDDLE
	mouse_button.button_mask = MOUSE_BUTTON_MASK_MIDDLE if pressed else 0
	mouse_button.pressed = pressed
	mouse_button.window_id = editor_window.get_window_id()
	mouse_button.device = FIXTURE_POINTER_DEVICE
	editor_window.push_input(mouse_button, true)


## Send a graph panning motion in viewport coordinates.
func _motion(editor_window: Window, pointer: Vector2, step: Vector2, held: bool = true) -> void:
	var mouse_motion: InputEventMouseMotion = InputEventMouseMotion.new()
	mouse_motion.position = pointer + step
	mouse_motion.global_position = mouse_motion.position
	mouse_motion.relative = step
	mouse_motion.button_mask = MOUSE_BUTTON_MASK_MIDDLE if held else 0
	mouse_motion.window_id = editor_window.get_window_id()
	mouse_motion.device = FIXTURE_POINTER_DEVICE
	editor_window.push_input(mouse_motion, true)


## Let layout, draw, and deferred graph updates settle.
func _frames(amount: int) -> void:
	for frame_index: int in range(amount):
		await get_tree().process_frame


## Fail the isolated editor and report the missing behavior.
func _require(passed: bool, description: String) -> bool:
	if not passed:
		push_error("[Crimson canvas stress] " + description)
		get_tree().quit(1)
	return passed
