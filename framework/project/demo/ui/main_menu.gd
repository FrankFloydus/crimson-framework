# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Control
## Binds Godot controls to the menu view model and owns presentation and input focus.

const MenuViewModel := preload("res://demo/ui/menu_view_model.gd")
const ScreenNavigator := preload("res://demo/ui/screen_navigator.gd")

## Relays the launch result to an optional command-line caller.
signal launch_finished(ok: bool, message: String)

## Persistent GameRuntime that starts and joins sessions for this menu.
@export var game: GameRuntime

@onready var _screens: ScreenNavigator = %Screens
@onready var _address: LineEdit = %Address
@onready var _cancel: Button = %Cancel
@onready var _status: Label = %Status

var _model: MenuViewModel


func _ready() -> void:
	_model = MenuViewModel.new(game)
	%Solo.pressed.connect(_model.start_solo)
	%OpenMultiplayer.pressed.connect(_open_multiplayer)
	%Host.pressed.connect(_model.host)
	%OpenJoin.pressed.connect(_open_join)
	%Connect.pressed.connect(_join)
	%MultiplayerBack.pressed.connect(_back)
	%JoinBack.pressed.connect(_back)
	_address.text_submitted.connect(func(_text: String) -> void: _join())
	_address.text_changed.connect(func(_text: String) -> void: _model.clear_validation())
	_cancel.pressed.connect(_model.cancel)
	_model.launch_finished.connect(launch_finished.emit)
	_model.changed.connect(_render)
	_screens.reset(%Main, %Solo)
	_render()


## Follow the normal host path, retaining its screen if startup fails.
func launch_host(bind_address: String) -> void:
	_open_multiplayer()
	_model.host(bind_address)


## Use the normal Join form and leave the address visible for a retry.
func launch_join(address: String) -> void:
	_open_multiplayer()
	_open_join()
	_address.text = address
	_join()


## Display a command-line configuration error in the menu feedback area.
func show_launch_error(message: String) -> void:
	_model.show_launch_error(message)


func _render() -> void:
	var was_visible := visible
	var was_pending := _cancel.visible
	visible = not _model.is_playing()
	if not visible:
		return
	var port := _model.port()
	%Port.text = (tr("Direct connection · UDP %d") % port) if port > 0 else ""
	var pending := _model.is_pending()
	_screens.focus_behavior_recursive = FOCUS_BEHAVIOR_DISABLED if pending else FOCUS_BEHAVIOR_INHERITED
	_screens.mouse_behavior_recursive = MOUSE_BEHAVIOR_DISABLED if pending else MOUSE_BEHAVIOR_INHERITED
	_screens.modulate.a = 0.4 if pending else 1.0
	_status.text = _model.status_text()
	_cancel.visible = pending
	if pending:
		_cancel.grab_focus()
	elif was_pending or not was_visible:
		_screens.restore_focus.call_deferred()


func _open_multiplayer() -> void:
	_screens.push(%Multiplayer, %Host)


func _open_join() -> void:
	_screens.push(%Join, %Connect)


func _join() -> void:
	if not _model.join(_address.text):
		_address.grab_focus()


func _back() -> void:
	_model.clear_validation()
	_screens.pop()


func _input(event: InputEvent) -> void:
	# Let Back end address editing before a later Back can leave the form.
	if is_visible_in_tree() and _address.has_focus() and _address.is_editing() and event.is_action_pressed(&"ui_cancel"):
		_address.unedit()
		get_viewport().set_input_as_handled()


func _unhandled_input(event: InputEvent) -> void:
	if not is_visible_in_tree() or not event.is_action_pressed(&"ui_cancel"):
		return
	if _model.is_pending():
		_model.cancel()
	else:
		_back()
	get_viewport().set_input_as_handled()
