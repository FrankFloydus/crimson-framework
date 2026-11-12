# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Translates CrimsonGame launch operations and session lifecycle into menu presentation.

signal changed
## Reports one settled launch for the demo boot adapter.
signal launch_finished(ok: bool, message: String)

var _game: CrimsonGame
var _operation: CrimsonGameOperation
var _pending_message := ""
var _validation_message := ""
var _session_message := ""
var _cancel_requested := false
var _entered_game := false


func _init(game: CrimsonGame) -> void:
	_game = game
	if _game != null:
		_entered_game = is_playing()
		_game.phase_changed.connect(_on_phase_changed)
		_game.session_ended.connect(_on_session_ended)


func port() -> int:
	if _game == null or _game.definition == null or _game.definition.multiplayer_rules == null:
		return 0
	return _game.definition.multiplayer_rules.port


func is_playing() -> bool:
	return _game != null and _game.get_phase() == CrimsonGame.RUNNING


func is_pending() -> bool:
	return _operation != null and not _operation.is_finished()


func status_text() -> String:
	if is_pending():
		return _pending_message
	if not _validation_message.is_empty():
		return _validation_message
	return _session_message


func start_solo() -> void:
	if _can_start():
		_launch(_game.play_solo, tr("Starting single player…"), tr("Could not start single player."))


func host(bind_address: String = "*") -> void:
	if _can_start():
		_launch(_game.host_game.bind(bind_address), tr("Starting multiplayer…"), tr("Could not host a game."))


## Return false when the form needs an address before the request can be sent.
func join(address: String) -> bool:
	if not _can_start():
		return true
	var endpoint := address.strip_edges()
	if endpoint.is_empty():
		_validation_message = tr("Enter the host address.")
		changed.emit()
		return false
	_launch(_game.join_game.bind(endpoint), tr("Connecting to host…"), tr("Could not connect. Check the host address and port."))
	return true


func cancel() -> void:
	if is_pending():
		_cancel_requested = true
		if _game.leave() != OK:
			_cancel_requested = false


func clear_validation() -> void:
	if not _validation_message.is_empty():
		_validation_message = ""
		changed.emit()


## Show a launch configuration error before an operation can be submitted.
func show_launch_error(message: String) -> void:
	_validation_message = ""
	_session_message = message
	changed.emit()


func _can_start() -> bool:
	return _game != null and _operation == null and _game.get_phase() == CrimsonGame.IDLE


func _launch(action: Callable, pending: String, fallback: String) -> void:
	_pending_message = pending
	_validation_message = ""
	_session_message = ""
	_operation = action.call()
	if is_pending():
		changed.emit()
	var result: Dictionary = await _operation.wait()
	_operation = null
	var ok: bool = result.get("ok", false)
	var message := ""
	if not ok:
		if _cancel_requested:
			message = tr("Launch cancelled.")
		else:
			message = _launch_failure(result, fallback)
			_session_message = message
	_cancel_requested = false
	changed.emit()
	launch_finished.emit(ok, message)


func _launch_failure(result: Dictionary, fallback: String) -> String:
	match StringName(result.get("reason", "")):
		&"spawn_unavailable":
			return tr("The host has no available player spawn.")
		&"invalid_roster", &"protocol_mismatch":
			return tr("The host is running an incompatible version of the game.")
		&"incompatible_world", &"incompatible_rules":
			return tr("The host is using a different world or multiplayer setup.")
		&"connection_failed":
			return fallback
		&"connection_timeout":
			return tr("The connection to the host timed out.")
		&"spawn_timeout", &"spawn_failed":
			return tr("Could not enter the starting area.")
	var detail := String(result.get("message", ""))
	return detail if not detail.is_empty() else fallback


func _on_phase_changed(phase: int) -> void:
	if phase == CrimsonGame.RUNNING:
		_entered_game = true
	changed.emit()


func _on_session_ended(reason: StringName) -> void:
	# Launch failures are delivered by the launch operation, including failed admissions.
	if not _entered_game:
		return
	_entered_game = false
	match reason:
		&"left":
			_session_message = ""
		&"server_disconnected":
			_session_message = tr("Disconnected from the host.")
		&"invalid_roster", &"protocol_mismatch":
			_session_message = tr("The host is running an incompatible version of the game.")
		_:
			_session_message = tr("The session connection closed.")
	changed.emit()
