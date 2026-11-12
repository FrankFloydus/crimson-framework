# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends Node


# Record the identity and committed score visible during the authority's mount callback.
func _ready() -> void:
	var game := get_parent().get_parent() as GameRuntime
	if game != null and game.is_authority():
		set_meta("world_at_mount", game.get_world_id())
		set_meta("score_at_mount", game.get_transactions().read_store("match_state").get("score", -1))
