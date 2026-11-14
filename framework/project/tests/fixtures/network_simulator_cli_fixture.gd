# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends "res://demo/session/network_simulator_cli.gd"
## Runs the production CLI and relay against test-owned game content.

const Fixture := preload("res://tests/fixtures/network_fixture.gd")


## Compose the matching standalone simulator authority and guest.
func _game_configuration() -> GameConfig:
	return GameConfig.from_definition(Fixture.definition()).player_controls()
