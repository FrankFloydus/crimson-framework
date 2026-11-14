# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercise real loopback ENet movement fan-out with isolated multiplayer and physics scopes.

const Fixture := preload("res://tests/fixtures/network_fixture.gd")
const LOOPBACK: String = "127.0.0.1"
const DEFAULT_GUESTS: int = 32
const MAX_GUESTS: int = 64
const READY_TIMEOUT_MS: int = 18000
const DRIVE_TICKS: int = 45
const MINIMUM_TRAVEL: float = 0.8
const POSE_TOLERANCE: float = 0.10


## One ENet endpoint, with the same RPC-relative paths and isolated physics world.
class Endpoint extends RefCounted:
	var viewport: SubViewport
	var owner: Node
	var game: Game
	var result: GameResult
	var local_body: Node3D
	var controls: Controls
	var controller: PlayerControl
	var peer_id: int
	var start_position: Vector3

	## Join without blocking setup of the other clients.
	func join() -> void:
		result = await game.join(LOOPBACK)

	## Drive the fixture layout's forward key.
	func drive(active: bool) -> void:
		var event := InputEventKey.new()
		event.physical_keycode = KEY_W
		event.pressed = active
		viewport.push_input(event, true)


func _initialize() -> void:
	_run.call_deferred()


## Create bounded ENet traffic and verify that each peer sees every authority pose converge.
func _run() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() > 1 or (args.size() == 1 and not args[0].is_valid_int()):
		_fail("Supply an optional guest count from 1 to %d." % MAX_GUESTS)
		return
	var count: int = args[0].to_int() if not args.is_empty() else DEFAULT_GUESTS
	if count < 1 or count > MAX_GUESTS:
		_fail("Guest count must be between 1 and %d." % MAX_GUESTS)
		return
	Engine.max_fps = 120
	var socket := PacketPeerUDP.new()
	if socket.bind(0, LOOPBACK) != OK:
		_fail("Could not reserve a loopback UDP port.")
		return
	var port: int = socket.get_local_port()
	socket.close()
	var profile := PlayerControlProfile.configure(Fixture.layout())
	profile.predict_movement = true
	profile.capture_pointer_on_boot = false
	var config: GameConfig = Game.configure(_build_world(count), Fixture.player()).lan(count).on_port(port).player_controls(profile)
	var authority: Endpoint = _endpoint(config)
	var served: GameResult = await authority.game.serve(LOOPBACK)
	if not served.ok:
		_fail("Could not serve the movement scale fixture: %s" % served.message)
		return
	var clients: Array[Endpoint] = []
	for index: int in count:
		clients.append(_endpoint(config))
	var admit_start: int = Time.get_ticks_msec()
	for client: Endpoint in clients:
		client.join()
	if not await _wait(func() -> bool: return _admitted(clients)):
		for client: Endpoint in clients:
			if client.result != null and not client.result.ok:
				print(client.result.message)
		_fail("The authority did not admit all %d ENet guests." % count)
		return
	var admission_ms: int = Time.get_ticks_msec() - admit_start
	for client: Endpoint in clients:
		client.peer_id = client.game.network_diagnostics().peer_id
		client.local_body = client.game.get_local_player()
		if client.local_body == null:
			_fail("An admitted guest did not mount its local actor.")
			return
		client.start_position = client.local_body.global_position
		var component: ControlsComponent = ControlsComponent.for_body(client.local_body)
		var view: Camera3D = client.viewport.get_camera_3d()
		if component == null or view == null:
			_fail("An admitted guest did not mount its authored controls and camera.")
			return
		client.controls = Controls.boot(client.owner, component.input_layout)
		if not client.controls.is_ready():
			_fail("Could not start the guest's input scope.")
			return
		client.controls.set_pointer_available(false)
		client.controller = component.boot(view, client.controls)
		if not client.controller.is_ready():
			_fail("The authored controller could not boot: %s" % client.controller.get_message())
			return
	if not await _wait(func() -> bool: return _all_rosters(authority, clients)):
		for client: Endpoint in clients:
			var local_count: int = client.game.get_world().get_node(^"Players").get_child_count()
			print("Guest %d sees %d of %d actors. Authority sees %d." % [client.peer_id, local_count, count,
				authority.game.get_world().get_node(^"Players").get_child_count()])
		_fail("The authority and guests did not install the same %d-actor roster." % count)
		return
	var drive_started_at: int = Time.get_ticks_msec()
	for client: Endpoint in clients:
		client.drive(true)
	for tick: int in DRIVE_TICKS:
		await physics_frame
	for client: Endpoint in clients:
		client.drive(false)
	var released_at: int = Time.get_ticks_msec()
	if not await _wait(func() -> bool: return _converged(authority, clients)):
		_report_convergence(authority, clients)
		_fail("At least one actor failed to move, acknowledge input, or converge on every peer.")
		return
	var convergence_ms: int = Time.get_ticks_msec() - released_at
	var min_ack: int = clients[0].game.network_diagnostics().acknowledged_input
	for client: Endpoint in clients:
		min_ack = mini(min_ack, client.game.network_diagnostics().acknowledged_input)
		client.controls.close()
		await client.game.leave()
	await authority.game.leave()
	print("CRIMSON_MOVEMENT_SCALE %s" % JSON.stringify({
		"guests": count,
		"observed_guest_actor_pairs": count * count,
		"admission_ms": admission_ms,
		"drive_ticks": DRIVE_TICKS,
		"drive_elapsed_ms": released_at - drive_started_at,
		"convergence_ms": convergence_ms,
		"min_acknowledged_input": min_ack,
	}))
	print("CRIMSON_MOVEMENT_SCALE_PASSED")
	quit()


## Assign each endpoint its own transport and its own physics world.
func _endpoint(config: GameConfig) -> Endpoint:
	var endpoint := Endpoint.new()
	endpoint.viewport = SubViewport.new()
	endpoint.viewport.own_world_3d = true
	root.add_child(endpoint.viewport)
	var scope := SceneMultiplayer.new()
	scope.server_relay = false
	set_multiplayer(scope, endpoint.viewport.get_path())
	endpoint.owner = Node.new()
	endpoint.owner.name = "Scale"
	endpoint.viewport.add_child(endpoint.owner)
	endpoint.game = config.boot(endpoint.owner)
	return endpoint


## Package a small static floor and separated parallel spawn lanes.
func _build_world(count: int) -> PackedScene:
	var world := Node3D.new()
	world.name = "ScaleWorld"
	var floor_body := StaticBody3D.new()
	var floor_shape := CollisionShape3D.new()
	var floor_box := BoxShape3D.new()
	floor_box.size = Vector3(count * 3.0 + 10.0, 1.0, 100.0)
	floor_shape.shape = floor_box
	floor_shape.position = Vector3(count * 1.5, -0.5, 0.0)
	world.add_child(floor_body)
	floor_body.owner = world
	floor_body.add_child(floor_shape)
	floor_shape.owner = world
	var spawns := Node3D.new()
	spawns.name = "SpawnPoints"
	world.add_child(spawns)
	spawns.owner = world
	for index: int in count:
		var spawn := Marker3D.new()
		spawn.position = Vector3(index * 3.0, 0.0, 0.0)
		spawns.add_child(spawn)
		spawn.owner = world
	var scene := PackedScene.new()
	scene.pack(world)
	world.free()
	return scene


## Require a successful join and distinct actual ENet identity for each guest.
func _admitted(clients: Array[Endpoint]) -> bool:
	var ids: Dictionary = {}
	for client: Endpoint in clients:
		if client.result == null or not client.result.ok:
			return false
		var peer_id: int = client.game.network_diagnostics().peer_id
		if peer_id <= 1 or ids.has(peer_id):
			return false
		ids[peer_id] = true
	return true


## Require the authority and every guest to have mounted every current actor.
func _all_rosters(authority: Endpoint, clients: Array[Endpoint]) -> bool:
	for client: Endpoint in clients:
		if authority.game.get_player(client.peer_id) == null:
			return false
		for observer: Endpoint in clients:
			if observer.game.get_player(client.peer_id) == null:
				return false
	return true


## Compare the latest authority positions with both owners and remote interpolators.
func _converged(authority: Endpoint, clients: Array[Endpoint]) -> bool:
	for client: Endpoint in clients:
		var body: Node3D = authority.game.get_player(client.peer_id)
		if body == null or body.global_position.distance_to(client.start_position) < MINIMUM_TRAVEL \
				or client.game.network_diagnostics().acknowledged_input <= 0:
			return false
		for observer: Endpoint in clients:
			var actor: Node3D = observer.game.get_player(client.peer_id)
			if actor == null or actor.global_position.distance_to(body.global_position) > POSE_TOLERANCE:
				return false
	return true


## Report a bounded summary so a failing run distinguishes input from downlink convergence.
func _report_convergence(authority: Endpoint, clients: Array[Endpoint]) -> void:
	var short_motion: int = 0
	var missing_ack: int = 0
	var divergent_pairs: int = 0
	var max_error: float = 0.0
	for client: Endpoint in clients:
		var body: Node3D = authority.game.get_player(client.peer_id)
		if body == null or body.global_position.distance_to(client.start_position) < MINIMUM_TRAVEL:
			short_motion += 1
		if client.game.network_diagnostics().acknowledged_input <= 0:
			missing_ack += 1
		if body == null:
			continue
		for observer: Endpoint in clients:
			var actor: Node3D = observer.game.get_player(client.peer_id)
			if actor == null:
				divergent_pairs += 1
				continue
			var error: float = actor.global_position.distance_to(body.global_position)
			max_error = maxf(max_error, error)
			if error > POSE_TOLERANCE:
				divergent_pairs += 1
	print("Scale convergence: %d short travel, %d missing ack, %d divergent views, maximum %.3f m." % [
		short_motion, missing_ack, divergent_pairs, max_error])


## Let the ENet scopes poll and process snapshots until the fixture deadline.
func _wait(predicate: Callable) -> bool:
	var deadline: int = Time.get_ticks_msec() + READY_TIMEOUT_MS
	while Time.get_ticks_msec() < deadline:
		if predicate.call():
			return true
		await process_frame
	return false


## Signal one failing fixture with a nonzero process exit status.
func _fail(message: String) -> void:
	push_error(message)
	quit(1)
