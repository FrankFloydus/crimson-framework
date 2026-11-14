# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Translates game actions and session lifecycle into menu presentation.

signal changed
## Reports one settled launch for the demo boot adapter.
signal launch_finished(ok: bool, message: String)

var _game: Game
var _pending := false
var _launch_generation := 0
var _pending_message := ""
var _validation_message := ""
var _session_message := ""
var _cancel_requested := false
var _entered_game := false
## Original address from the last successful direct join, including launcher requests.
var _last_join_address := ""


## Observe the supplied scope after the scene boots it. Rebind only between launches.
func bind_game(game: Game) -> void:
	if _game == game or is_pending():
		return
	if _game != null:
		_game.phase_changed.disconnect(_on_phase_changed)
		_game.session_ended.disconnect(_on_session_ended)
	_game = game
	_last_join_address = ""
	_entered_game = is_playing()
	if _game != null:
		_game.phase_changed.connect(_on_phase_changed)
		_game.session_ended.connect(_on_session_ended)
	changed.emit()


func port() -> int:
	return _game.get_port() if _game != null else 0


func is_playing() -> bool:
	return _game != null and _game.get_phase() == Game.RUNNING


func is_pending() -> bool:
	return _pending


## Address available to reconnect through the ordinary Join action.
func last_join_address() -> String:
	return _last_join_address


func status_text() -> String:
	if is_pending():
		return _pending_message
	if not _validation_message.is_empty():
		return _validation_message
	return _session_message


func start_solo() -> void:
	if _can_start():
		_launch(_game.play, tr("Starting single player…"), tr("Could not start single player."))


func host(bind_address: String = "*") -> void:
	if _can_start():
		_launch(_game.host.bind(bind_address), tr("Starting multiplayer…"), tr("Could not host a game."))


## Return false when the form needs an address before the request can be sent.
func join(address: String) -> bool:
	if not _can_start():
		return true
	var endpoint := address.strip_edges()
	if endpoint.is_empty():
		_validation_message = tr("Enter the host address.")
		changed.emit()
		return false
	_launch(_game.join.bind(endpoint), tr("Connecting to host…"), tr("Could not connect. Check the host address and port."), endpoint)
	return true


func cancel() -> void:
	if not is_pending() or _cancel_requested:
		return
	var generation := _launch_generation
	_cancel_requested = true
	var result: GameResult = await _game.leave()
	if not result.ok and is_pending() and generation == _launch_generation:
		_cancel_requested = false
		_pending_message = result.message if not result.message.is_empty() else tr("Could not cancel the launch. Try cancelling again.")
		changed.emit()


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
	if is_pending():
		_pending_message = tr("A launch is already in progress. Wait or cancel it before starting again.")
		changed.emit()
		return false
	var message := ""
	if _game == null:
		message = tr("Bind a game to the menu before starting.")
	elif _game.get_phase() != Game.IDLE:
		message = tr("Leave the current session before starting another game.")
	else:
		return true
	show_launch_error(message)
	launch_finished.emit(false, message)
	return false


func _launch(action: Callable, pending: String, fallback: String, join_address: String = "") -> void:
	_launch_generation += 1
	_pending = true
	_pending_message = pending
	_validation_message = ""
	_session_message = ""
	# The game phase signal refreshes pending presentation while this action waits.
	var result: GameResult = await action.call()
	_pending = false
	var message := ""
	if not result.ok:
		if _cancel_requested and result.code == "Cancelled":
			message = tr("Launch cancelled.")
		else:
			message = _launch_failure(result, fallback)
			_session_message = message
	elif not join_address.is_empty():
		_last_join_address = join_address
	_cancel_requested = false
	changed.emit()
	launch_finished.emit(result.ok, message)


func _launch_failure(result: GameResult, fallback: String) -> String:
	match result.reason:
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
	return result.message if not result.message.is_empty() else fallback


func _on_phase_changed(phase: int) -> void:
	if phase == Game.RUNNING:
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
