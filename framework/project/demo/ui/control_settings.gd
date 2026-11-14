# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Control
## Demonstrates one binding edit using the same controls that drive the local player.

## Emitted when the settings overlay closes and gameplay regains input.
signal closed

const SETTINGS_ACTION := &"settings"
const FORWARD_KEY := &"move/key_up"

@onready var _change_button: Button = %ChangeBinding
@onready var _forward_label: Label = %ForwardLabel
@onready var _resume_button: Button = %Resume
@onready var _status: Label = %Status

var _controls: Controls
var _bindings: Rebinding
var _listening := false
var _attachment := 0


func _ready() -> void:
	_change_button.pressed.connect(_change_binding)
	_resume_button.pressed.connect(close)
	visibility_changed.connect(_cancel_hidden_capture)


## Wire this settings overlay to a live, local input scope.
func setup(controls: Controls) -> Error:
	clear()
	if controls == null or not controls.is_ready():
		return ERR_UNCONFIGURED
	if not controls.has_action_kind(SETTINGS_ACTION, InputAction.BUTTON):
		return OK
	var bindings := Rebinding.open(controls, self)
	if bindings == null:
		return ERR_UNCONFIGURED
	var error := controls.suspend_while_visible(self)
	if error != OK:
		return error
	error = controls.on_press(SETTINGS_ACTION, open)
	if error != OK:
		controls.stop_suspending_while_visible(self)
		return error
	_controls = controls
	_bindings = bindings
	_bindings.changed.connect(_on_binding_changed)
	_render_binding()
	return OK


## Close the overlay and detach from its previous input owner.
func clear() -> void:
	_attachment += 1
	if _bindings != null:
		_bindings.changed.disconnect(_on_binding_changed)
		_bindings.cancel()
		_bindings = null
	_listening = false
	close()
	if _controls != null:
		_controls.off_press(SETTINGS_ACTION, open)
		_controls.stop_suspending_while_visible(self)
		_controls = null


## Show settings and hand focus to the editable Move Forward binding.
func open() -> void:
	if _bindings == null or visible:
		return
	_status.text = ""
	_render_binding()
	show()
	if _change_button.visible:
		_change_button.grab_focus()
	else:
		_resume_button.grab_focus()


## Hide settings so the local Controls scope can restore its prior input state.
func close() -> void:
	if visible:
		hide()
		closed.emit()


func _change_binding() -> void:
	if _listening or _bindings == null:
		return
	var slot := _bindings.slot_by_id(FORWARD_KEY)
	if slot == null:
		return
	var attachment := _attachment
	_listening = true
	_status.text = tr("Press a key or mouse button for Move Forward. Escape or controller Back cancels.")
	_render_binding()
	var result: RebindResult = await _bindings.rebind(slot)
	if attachment != _attachment:
		return
	_listening = false
	_render_binding()
	if not is_visible_in_tree():
		return
	if result.ok:
		_status.text = tr("Move Forward updated.")
	elif result.code == &"Cancelled":
		_status.text = tr("Binding change cancelled.")
	else:
		_status.text = result.message
	_change_button.grab_focus()


func _render_binding() -> void:
	var slot := _bindings.slot_by_id(FORWARD_KEY) if _bindings != null else null
	_forward_label.visible = slot != null
	_change_button.visible = slot != null
	_change_button.disabled = _listening or slot == null
	_change_button.text = tr("Listening…") if _listening else (slot.label if slot != null else tr("Unassigned"))
	_resume_button.disabled = _listening


func _on_binding_changed(_slot: BindingSlot) -> void:
	_render_binding()


func _cancel_hidden_capture() -> void:
	if _listening and not is_visible_in_tree() and _bindings != null:
		_bindings.cancel()


func _unhandled_input(event: InputEvent) -> void:
	if not is_visible_in_tree():
		return
	var back := event.is_action_pressed(&"ui_cancel")
	if event is InputEventJoypadButton:
		back = back or event.is_pressed() and event.button_index in [JOY_BUTTON_BACK, JOY_BUTTON_START]
	if back:
		get_viewport().set_input_as_handled()
		if _listening:
			_bindings.cancel()
		else:
			close()
