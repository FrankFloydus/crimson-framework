# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Lets the controller preview lock onto the north doorway in Transfer Hall.

const TARGET_PATH: NodePath = ^"CentralRoom/NorthThreshold"

## The landmark currently selected by the local preview.
var _target: Node3D


## Select or release the doorway using the same local player API as a game target.
func toggle(player: PlayerControl, world: Node) -> Error:
	if player == null or not player.is_ready():
		return ERR_UNCONFIGURED
	if is_instance_valid(_target):
		player.clear_lock_on()
		clear()
		return OK
	var doorway := world.get_node_or_null(TARGET_PATH) as Node3D if is_instance_valid(world) else null
	if doorway == null:
		return ERR_DOES_NOT_EXIST
	var status := player.lock_on(doorway)
	if status == OK:
		_target = doorway
	return status


## Forget the selected landmark when the local preview detaches.
func clear() -> void:
	_target = null
