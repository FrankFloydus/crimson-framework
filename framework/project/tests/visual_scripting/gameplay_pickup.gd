# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends Area3D

const HEALTH: StringName = &"health"
const HEAL_AMOUNT: int = 25
const HEAL_THRESHOLD: int = 75
const MAX_HEALTH: int = 100


## Handle a player reaching the pickup area.
func _on_body_entered(body: Node3D) -> void:
	apply_pickup(body)


## Apply the same native metadata read, branch, and write as the visual graphs.
func apply_pickup(body: Node3D) -> void:
	var current_health: int = body.get_meta(HEALTH, 0)
	if current_health < HEAL_THRESHOLD:
		body.set_meta(HEALTH, current_health + HEAL_AMOUNT)
	else:
		body.set_meta(HEALTH, MAX_HEALTH)
