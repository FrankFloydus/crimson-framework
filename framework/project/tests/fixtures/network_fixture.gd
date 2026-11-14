# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends RefCounted
## Owns the authored content used by networking fixtures. This source has no dependency on demo assets.

const WORLD_ID: StringName = &"network_fixture"
const PORT: int = 24830
const LOCAL_VISUALS_GROUP: StringName = &"crimson_local_hidden"
const PLAYER_RADIUS: float = 0.3
const PLAYER_HEIGHT: float = 1.76
const WALL_FRONT_Z: float = 9.0
const SPAWNS: Array[Vector3] = [
	Vector3(0.0, 0.005, 7.65),
	Vector3(-1.9, 0.005, 6.25),
	Vector3(1.9, 0.005, 6.25),
	Vector3(0.0, 0.005, 4.85),
]


## Supply input events consumed by the network test driver.
static func layout() -> InputLayout:
	return InputLayout.create().add_move(&"move").add_button(&"jump", KEY_SPACE) \
		.add_mouse_motion(&"mouse_aim").add_right_stick(&"stick_aim")


## Provide an upright capsule, authored mover and controls, and a local view.
static func player() -> PackedScene:
	var actor := CharacterBody3D.new()
	actor.name = &"NetworkFixturePlayer"
	actor.collision_layer = 0

	var mover := MoverComponent.new()
	mover.name = &"Mover"
	mover.predict_movement = true
	_attach(actor, mover, actor)

	var controls := ControlsComponent.new()
	controls.name = &"Controls"
	controls.input_layout = layout()
	_attach(actor, controls, actor)

	var capsule := CapsuleShape3D.new()
	capsule.radius = PLAYER_RADIUS
	capsule.height = PLAYER_HEIGHT
	var collider := CollisionShape3D.new()
	collider.name = &"CollisionShape3D"
	collider.shape = capsule
	collider.position.y = PLAYER_HEIGHT * 0.5
	_attach(actor, collider, actor)

	var view := Camera3D.new()
	view.name = &"Camera3D"
	view.position.y = 1.67
	_attach(actor, view, actor)
	var model := Node3D.new()
	model.name = &"Visuals"
	model.add_to_group(LOCAL_VISUALS_GROUP, true)
	_attach(actor, model, actor)
	return _pack(actor)


## Provide deterministic geometry and spawn order for prediction and admission assertions.
static func world() -> PackedScene:
	var environment_root := Node3D.new()
	environment_root.name = &"NetworkFixtureWorld"
	_add_barrier(environment_root, &"Floor", Vector3(0.0, -0.25, 0.0), Vector3(24.0, 0.5, 30.0))
	_add_barrier(environment_root, &"SouthWall", Vector3(0.0, 1.5, WALL_FRONT_Z + 0.25),
		Vector3(24.0, 3.0, 0.5))
	var points := Node3D.new()
	points.name = &"SpawnPoints"
	_attach(environment_root, points, environment_root)
	for pose: Vector3 in SPAWNS:
		var marker := Marker3D.new()
		marker.position = pose
		_attach(points, marker, environment_root)
	return _pack(environment_root)


## Boot legacy session and provider assertions with independent authored defaults.
static func definition() -> GameDefinition:
	var rules := MultiplayerRules.listen_coop().on_port(PORT).with_max_players(SPAWNS.size())
	return GameRuntime.configure().with_starting_world(WORLD_ID, world()).with_player(player()).with_multiplayer(rules)


## Include a child when the temporary scene is packed.
static func _attach(parent: Node, child: Node, scene_root: Node) -> void:
	parent.add_child(child)
	child.owner = scene_root


## Add one fixed, axis-aligned collision surface.
static func _add_barrier(world_root: Node3D, barrier_name: StringName, center: Vector3, size: Vector3) -> void:
	var barrier := StaticBody3D.new()
	barrier.name = barrier_name
	barrier.position = center
	_attach(world_root, barrier, world_root)
	var geometry := BoxShape3D.new()
	geometry.size = size
	var collider := CollisionShape3D.new()
	collider.shape = geometry
	_attach(barrier, collider, world_root)


## Keep the packed resource independent of the temporary authoring nodes.
static func _pack(scene_root: Node) -> PackedScene:
	var result := PackedScene.new()
	var packed: Error = result.pack(scene_root)
	scene_root.free()
	return result if packed == OK else null
