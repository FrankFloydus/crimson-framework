# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends PanelContainer
## Displays the assigned player slot and role for the current playable window.

@onready var _label: Label = $Label

## The game owns assignment and publishes it as part of its diagnostic snapshot.
var _game: Game


## Observe the same world readiness and teardown used by local gameplay.
func setup(game: Game) -> void:
	_game = game
	game.world_ready.connect(_on_world_ready)
	game.session_ended.connect(_on_session_ended)
	_refresh()


## Refresh for a newly mounted local player.
func _on_world_ready(_world: StringName, _epoch: int) -> void:
	_refresh()


## Hide the previous identity when returning to the menu.
func _on_session_ended(_reason: StringName) -> void:
	hide()


## Keep spectator, menu and headless dedicated windows free of a player label.
func _refresh() -> void:
	var stats: GameNetworkDiagnostics = _game.network_diagnostics()
	visible = stats.player_number > 0 and _game.get_local_player() != null
	if not visible:
		return
	var role: String = tr("Solo")
	if stats.role == GameNetworkDiagnostics.HOST:
		role = tr("Server")
	elif stats.role == GameNetworkDiagnostics.GUEST:
		role = tr("Client")
	_label.text = tr("Player %d (%s)") % [stats.player_number, role]
