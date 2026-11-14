# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Builds an explicit movement profile for fixtures exercising the script API.

const Fixture := preload("res://tests/fixtures/network_fixture.gd")

## Use test-owned actions and movement settings.
static func build() -> PlayerControlProfile:
	var profile := PlayerControlProfile.configure(Fixture.layout())
	profile.predict_movement = true
	profile.capture_pointer_on_boot = DisplayServer.get_name() != "headless"
	return profile
