# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Drives the Godot debugger against a separate game process using the editor's native messages.

const GRAPH_PATH: String = "res://tests/visual_scripting/ready_print.crimsonscript"
const SCENE_PATH: String = "res://tests/visual_scripting/ready_print.tscn"
const READY_NODE: int = 1
const PRINT_NODE: int = 2
const PRINT_PIN: String = "Pin: Message"
const PRINT_MESSAGE: String = "Hello World"
const SIGNAL_GRAPH: String = "res://tests/visual_scripting/debugger_signal.crimsonscript"
const SIGNAL_SCENE: String = "res://tests/visual_scripting/debugger_signal.tscn"
const SIGNAL_NODE: int = 4
const SIGNAL_PRINT_NODE: int = 5
const SIGNAL_PIN: String = "Pin: Value"
const SIGNAL_VALUE: float = 27.0
const SIGNAL_PRINT_MESSAGE: String = "Signal graph resumed."
const WAIT_GRAPH: String = "res://tests/visual_scripting/debugger_wait.crimsonscript"
const WAIT_SCENE: String = "res://tests/visual_scripting/debugger_wait.tscn"
const WAIT_PRINT_MESSAGE: String = "Delay resumed."
const WAIT_SECONDS: float = 0.01
const LOOPBACK: String = "127.0.0.1"
const MAX_TEST_MSEC: int = 15000

## The debugger listens on an ephemeral port and accepts only the local game.
var _listener: TCPServer = TCPServer.new()
## Godot debugger messages are Variants prefixed by their byte count, as in PacketPeerStream.
var _packets: PacketPeerStream = PacketPeerStream.new()
## Child process owned by the fixture until it exits and flushes its log.
var _game_pid: int = 0
## Separate log captures output even if the connection closes before it can forward it.
var _game_log: String = ""
## One deadline for the connection, pause, variable requests, step, and exit.
var _deadline: int = 0
## Counts graph side effects forwarded by the game while it runs or pauses.
var _print_count: int = 0
## Exact graph output expected from the current debugger phase.
var _expected_print: String = PRINT_MESSAGE
## Records the debugger exchange for diagnosis on failure.
var _transcript: PackedStringArray = PackedStringArray()


## Defer process startup until the fixture's scene tree can process timers.
func _initialize() -> void:
	_verify.call_deferred()


## Exercise initial, live, and native-signal breakpoints in independent game processes.
func _verify() -> void:
	if not await _verify_run(true):
		return
	if not await _verify_run(false):
		return
	if not await _verify_run(true, &"signal") or not await _verify_run(true, &"wait"):
		return
	_finish("")


## Pause at a launch breakpoint, then exercise native stepping or a breakpoint added while running.
func _verify_run(step_over: bool, scenario: StringName = &"ready") -> bool:
	_packets.stream_peer = null
	_game_pid = 0
	_transcript.clear()
	_print_count = 0
	_expected_print = PRINT_MESSAGE
	var graph_path: String = GRAPH_PATH
	var scene_path: String = SCENE_PATH
	var instance_name: String = "ReadyPrint"
	var first_node: int = READY_NODE
	var next_node: int = PRINT_NODE
	var phase_name: String = "step" if step_over else "live"
	if scenario == &"signal":
		_expected_print = SIGNAL_PRINT_MESSAGE
		graph_path = SIGNAL_GRAPH
		scene_path = SIGNAL_SCENE
		instance_name = "DebuggerSignal"
		first_node = SIGNAL_NODE
		next_node = SIGNAL_PRINT_NODE
		phase_name = "signal"
	elif scenario == &"wait":
		_expected_print = WAIT_PRINT_MESSAGE
		graph_path = WAIT_GRAPH
		scene_path = WAIT_SCENE
		instance_name = "DebuggerWait"
		first_node = 2
		next_node = 3
		phase_name = "wait"
	_deadline = Time.get_ticks_msec() + MAX_TEST_MSEC
	_game_log = ProjectSettings.globalize_path("user://visual-script-debugger-%s.log" % phase_name)
	if _listener.listen(0, LOOPBACK) != OK:
		_finish("Could not start the loopback debugger.")
		return false
	var project_path: String = ProjectSettings.globalize_path("res://")
	var debug_uri: String = "tcp://%s:%d" % [LOOPBACK, _listener.get_local_port()]
	var launch_args: PackedStringArray = PackedStringArray([
		"--headless", "--path", project_path, "--log-file", _game_log,
		"--remote-debug", debug_uri, "--breakpoints", "%s:%d" % [graph_path, first_node],
		"--max-fps", "60", "--quit-after", "60", scene_path,
	])
	_game_pid = OS.create_process(OS.get_executable_path(), launch_args)
	if _game_pid <= 0:
		_finish("Could not start the game process.")
		return false
	while not _listener.is_connection_available() and _game_is_live():
		await create_timer(0.01).timeout
	var game_peer: StreamPeerTCP = _listener.take_connection()
	if game_peer == null:
		_finish("The game did not connect to the debugger.")
		return false
	_packets.stream_peer = game_peer
	_listener.stop()

	var ready_thread: int = await _assert_pause(graph_path, instance_name, first_node)
	if ready_thread == 0:
		return false
	if scenario == &"signal" and not await _assert_pin(ready_thread, SIGNAL_PIN, SIGNAL_VALUE):
		return false
	if scenario == &"wait" and not await _assert_pin(ready_thread, "Pin: Seconds", WAIT_SECONDS):
		return false
	if not step_over and not _send_command("crimson_visual:breakpoint", ready_thread, [graph_path, next_node, true]):
		return false
	if not _send_command("next" if step_over else "continue", ready_thread):
		return false
	if (await _receive_message("debug_exit")).is_empty():
		return false
	var print_thread: int = await _assert_pause(graph_path, instance_name, next_node)
	if print_thread == 0:
		return false
	if not await _assert_pin(print_thread, PRINT_PIN, _expected_print):
		return false

	if not step_over and not _send_command("crimson_visual:breakpoint", print_thread, [graph_path, next_node, false]):
		return false
	if not _send_command("continue", print_thread):
		return false
	if (await _receive_message("debug_exit")).is_empty():
		return false
	while _game_is_live():
		await create_timer(0.01).timeout
	if OS.is_process_running(_game_pid) or OS.get_process_exit_code(_game_pid) != 0:
		_finish("The resumed game did not exit successfully.")
		return false
	var game_output: String = FileAccess.get_file_as_string(_game_log)
	var graph_prints: int = game_output.replace("\r", "").split("\n").count(_expected_print)
	if graph_prints != 1 or game_output.contains("ERROR:"):
		_finish("The game should print the graph message once and report no errors after Continue.")
		return false
	return true


## Read a typed pin value from the native debugger stack before the action's side effect.
func _assert_pin(thread_id: int, pin_name: String, expected_value: Variant) -> bool:
	if not _send_command("get_stack_frame_vars", thread_id, [0]):
		return false
	var frame_message: Array = await _receive_message("stack_frame_vars")
	if frame_message.is_empty():
		return false
	var frame_data: Array = frame_message[2]
	var variable_count: int = int(frame_data[0])
	var found_pin: bool = false
	for variable_index: int in range(variable_count):
		var variable_packet: Array = await _receive_message("stack_frame_var")
		if variable_packet.is_empty():
			return false
		var variable_fields: Array = variable_packet[2]
		if variable_fields[0] == pin_name and variable_fields[1] == 0 and \
				typeof(variable_fields[3]) == typeof(expected_value) and variable_fields[3] == expected_value:
			found_pin = true
	if not found_pin or _print_count != 0:
		_finish("The paused pin %s did not contain %s before execution." % [pin_name, expected_value])
		return false
	return true


## Validate both the graph-specific location and the native debugger's stop and stack frames.
func _assert_pause(graph_path: String, instance_name: String, node_id: int) -> int:
	var hit_message: Array = await _receive_message("crimson_visual:hit")
	if hit_message.is_empty():
		return 0
	var hit_data: Array = hit_message[2]
	if hit_data.size() != 1 or not hit_data[0] is Dictionary:
		_finish("The debugger returned a malformed Visual Script location.")
		return 0
	var hit_location: Dictionary = hit_data[0]
	if hit_location.get("script") != graph_path or hit_location.get("graph") != 0 \
			or hit_location.get("node") != node_id or not str(hit_location.get("instance")).contains(instance_name):
		_finish("The debugger paused at the wrong graph location: %s" % [hit_location])
		return 0
	var stop_message: Array = await _receive_message("debug_enter")
	if stop_message.is_empty():
		return 0
	var stop_data: Array = stop_message[2]
	if stop_data.size() < 4 or not stop_data[0] or not stop_data[2] or _print_count != 0:
		_finish("The game did not pause before executing the node with an inspectable stack.")
		return 0
	var thread_id: int = int(stop_data[3])
	if not _send_command("get_stack_dump", thread_id):
		return 0
	var stack_message: Array = await _receive_message("stack_dump")
	if stack_message.is_empty():
		return 0
	var stack_data: Array = stack_message[2]
	if stack_data.size() < 4 or stack_data[1] != graph_path or stack_data[2] != node_id:
		_finish("The native debugger stack did not identify node %d: %s" % [node_id, stack_data])
		return 0
	return thread_id


## Require a given message while also observing asynchronous graph output.
func _receive_message(expected_kind: String) -> Array:
	while Time.get_ticks_msec() < _deadline:
		var pending_messages: int = _packets.get_available_packet_count()
		for message_index: int in range(pending_messages):
			var decoded: Variant = _packets.get_var()
			if not decoded is Array or decoded.size() != 3:
				_finish("Received a malformed debugger message.")
				return []
			var message_kind: String = decoded[0]
			if not message_kind.begins_with("performance:"):
				_transcript.append(message_kind)
			if message_kind == expected_kind:
				return decoded
			if message_kind == "output" and not decoded[2].is_empty():
				for output_chunk: String in decoded[2][0]:
					_print_count += output_chunk.split("\n").count(_expected_print)
			elif message_kind in ["crimson_visual:hit", "debug_enter", "debug_exit", "error"]:
				_finish("Unexpected debugger message %s while waiting for %s." % [message_kind, expected_kind])
				return []
		if not OS.is_process_running(_game_pid):
			_finish("The game exited while waiting for debugger message %s." % expected_kind)
			return []
		await create_timer(0.01).timeout
	_finish("Timed out waiting for debugger message %s." % expected_kind)
	return []


## Use the native debugger command and its paused thread instead of a test-only runtime hook.
func _send_command(command: String, thread_id: int, arguments: Array = []) -> bool:
	if _packets.put_var([command, thread_id, arguments]) == OK:
		_transcript.append("sent:" + command)
		return true
	_finish("Could not send debugger command %s." % command)
	return false


## Bound the test even when the subprocess fails to stop or disconnect.
func _game_is_live() -> bool:
	return Time.get_ticks_msec() < _deadline and OS.is_process_running(_game_pid)


## Stop owned resources and record exactly one outcome for the PowerShell harness.
func _finish(failure: String) -> void:
	if _game_pid > 0 and OS.is_process_running(_game_pid):
		OS.kill(_game_pid)
	if _listener.is_listening():
		_listener.stop()
	if failure.is_empty():
		print("CRIMSON_VISUAL_SCRIPT_DEBUGGER_PASSED")
		quit(0)
		return
	var game_output: String = FileAccess.get_file_as_string(_game_log) if FileAccess.file_exists(_game_log) else ""
	push_error("%s\nDebugger messages: %s\nChild log: %s\n%s" % [failure, ", ".join(_transcript), _game_log, game_output])
	quit(1)
