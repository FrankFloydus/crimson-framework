# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Provide one minimal two-seat world for the Steam session fixtures.

const WORLD_ID: StringName = &"steam_fixture"
const SEATS: int = 2


## Attach the same fixed-world GameRuntime on host and guest.
static func boot(owner: Node, provider: SteamProvider = null) -> GameRuntime:
	var actor_scene := _pack(Node3D.new())
	var world := Node3D.new()
	for seat: int in range(SEATS):
		var spawn := PlayerSpawnPoint.new()
		spawn.position.x = float(seat)
		world.add_child(spawn)
		spawn.owner = world
	var world_scene := _pack(world)
	if world_scene == null or actor_scene == null:
		return null
	var definition := (GameRuntime.configure()
		.with_starting_world(WORLD_ID, world_scene)
		.with_player(actor_scene)
		.with_multiplayer(MultiplayerRules.listen_coop().with_max_players(SEATS)))
	if provider != null:
		definition = definition.with_provider(provider)
	return definition.boot(owner)


## Return a packed scene and retire its detached source nodes.
static func _pack(scene_root: Node3D) -> PackedScene:
	var scene := PackedScene.new()
	var packed := scene.pack(scene_root)
	scene_root.free()
	return scene if packed == OK else null
