# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Node
## Boots the demo game and routes optional launch arguments through its menu.

const LaunchArguments := preload("res://demo/session/launch_arguments.gd")
const MainMenu := preload("res://demo/ui/main_menu.gd")
const ControlSettings := preload("res://demo/ui/control_settings.gd")
const NetworkDebugger := preload("res://demo/ui/network_debugger.gd")
const SimulatorSession := preload("res://demo/session/network_simulator_session.gd")
const LocalPlayerIdentity := preload("res://demo/ui/local_player_identity.gd")
const ControllerPreview := preload("res://demo/player/controller_preview.gd")

## Optional configuration assigned before attachment by a fixture.
var game_config: GameConfig
## Code-first game handle shared with the menu.
var game: Game
## The local input scope survives replacing the player body.
var _controls: Controls
## Mounted body currently using the local input scope.
var _controlled_body: CharacterBody3D
## Demo-only camera and target controls for inspecting the local body.
var _controller_preview: ControllerPreview = ControllerPreview.new()

@onready var _menu: MainMenu = $MainMenu
@onready var _settings: ControlSettings = $GameUI/Settings
@onready var _debugger: NetworkDebugger = $GameUI/NetworkDebugger
@onready var _identity: LocalPlayerIdentity = $GameUI/LocalPlayerIdentity


func _ready() -> void:
	var launch := LaunchArguments.parse(OS.get_cmdline_user_args())
	add_child(_controller_preview)
	var session := SimulatorSession.new()
	session.boot(self, launch.port if launch.error.is_empty() else 0, game_config)
	game = session.game
	_menu.bind_game(game)
	_debugger.setup(game, session.relay, _menu, _settings)
	_identity.setup(game)
	game.world_ready.connect(_start_local_player)
	game.session_ended.connect(_close_settings)
	if not launch.error.is_empty():
		_report_launch_error(launch.role, launch.error)
	elif not launch.role.is_empty():
		_start_launch.call_deferred(launch)


## Attach local input after the session publishes its body and camera.
func _start_local_player(_world: StringName, _epoch: int) -> void:
	if not game.is_player_controlled() or game.get_phase() != Game.RUNNING:
		return
	var body := game.get_local_player() as CharacterBody3D
	if body == null or body == _controlled_body:
		return
	var component := ControlsComponent.for_body(body)
	if component == null or component.input_layout == null:
		push_error("[Crimson] Assign an Input Layout to the player's Controls component.")
		return
	if _controls != null and _controls.get_layout() != component.input_layout:
		_controls.close()
		_controls = null
	if _controls == null:
		_controls = Controls.boot(self, component.input_layout)
		if not _controls.is_ready():
			push_error("[Crimson] Could not start local controls: %s" % error_string(_controls.get_error()))
			_controls = null
			return
		_controls.set_pointer_available(false)

	var view := body.get_viewport().get_camera_3d()
	if view == null or not body.is_ancestor_of(view):
		push_error("[Crimson] Could not find the local player's active camera.")
		return
	var player := component.boot(view, _controls)
	if player == null or not player.is_ready():
		var detail: String = player.get_message() if player != null else "Check the player's Controls component."
		push_error("[Crimson] Could not attach the local player: %s" % detail)
		return
	_controlled_body = body
	_debugger.set_controls(_controls)
	var setup_error: Error = _settings.setup(_controls)
	if setup_error != OK:
		push_error("[Crimson] Could not open local settings: %s" % error_string(setup_error))
	var mover_component: MoverComponent = body.get_node_or_null(^"Mover") as MoverComponent
	if mover_component != null:
		var body_visuals: Node3D = body.get_node_or_null(^"Visuals") as Node3D
		_controller_preview.attach(view, body_visuals, _controls, player, game.get_world(), mover_component.controller_style)


## Retire the menu when the game leaves the world.
func _close_settings(_reason: StringName) -> void:
	_controller_preview.clear()
	_controlled_body = null
	_settings.clear()
	_debugger.set_controls(null)


func _start_launch(launch: Dictionary) -> void:
	_menu.launch_finished.connect(_on_launch_finished.bind(launch.role, game.get_port()), CONNECT_ONE_SHOT)
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
