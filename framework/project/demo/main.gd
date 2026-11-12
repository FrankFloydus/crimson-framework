# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Node
## Adapts optional command-line requests to the demo's normal menu actions.

const LaunchArguments := preload("res://demo/session/launch_arguments.gd")
const MainMenu := preload("res://demo/ui/main_menu.gd")

@onready var _game: GameRuntime = $Game
@onready var _menu: MainMenu = $MainMenu


func _ready() -> void:
	var launch := LaunchArguments.parse(OS.get_cmdline_user_args())
	if not launch.error.is_empty():
		_report_launch_error(launch.role, launch.error)
	elif not launch.role.is_empty():
		_start_launch.call_deferred(launch)


func _start_launch(launch: Dictionary) -> void:
	var port := LaunchArguments.configure_port(_game, launch.port)
	if port == 0:
		_report_launch_error(launch.role, "The demo needs multiplayer rules to launch a session.")
		return
	_menu.launch_finished.connect(_on_launch_finished.bind(launch.role, port), CONNECT_ONE_SHOT)
	if launch.role == "listen":
		_menu.launch_host(launch.bind)
	else:
		_menu.launch_join(launch.address)


func _on_launch_finished(ok: bool, message: String, role: String, port: int) -> void:
	if ok:
		print("[Crimson] READY %s %d" % [role, port])
	else:
		_print_launch_error(role, message)


func _report_launch_error(role: String, message: String) -> void:
	_menu.show_launch_error(message)
	_print_launch_error(role, message)


func _print_launch_error(role: String, message: String) -> void:
	var label := role if role in ["listen", "join"] else "launcher"
	print("[Crimson] ERROR %s %s" % [label, message.replace("\r", " ").replace("\n", " ")])
