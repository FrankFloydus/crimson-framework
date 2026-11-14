# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises authoritative movement, local steering, and late admission in separate Godot processes.

const Fixture := preload("res://tests/fixtures/network_fixture.gd")
const DefaultPlayerProfile := preload("res://demo/tests/player_profile_fixture.gd")

const ADDRESS := "127.0.0.1"
const AUTHORITY_ID := 1
const WAIT_MS := 10000
const LATE_JOIN_SETUP_MS := 20000
const DRIVE_TICKS := 26
const MIN_TRAVEL := 0.9
const POSITION_TOLERANCE := 0.25
const LOOK_TOLERANCE := 0.12
const INPUT_EXPIRY_MARGIN_MS := 850
const STABILITY_MS := 140
const STABLE_POSITION := 0.03
const STABLE_ANGLE := 0.02
const IMMEDIATE_TICKS := 3
const IMMEDIATE_TRAVEL := 0.01
const RECONCILED_POSITION := 0.10
const WALL_DRIVE_TICKS := 100
const WALL_REACH_Z := Fixture.WALL_FRONT_Z - Fixture.PLAYER_RADIUS - 0.25
const WALL_LIMIT_Z := Fixture.WALL_FRONT_Z - Fixture.PLAYER_RADIUS + 0.05
const RAMP_DRIVE_TICKS := 150
const RAMP_RISE := 0.70
const RAMP_LANDING_X := 7.0
const FLOOR_MARGIN := 0.12
const RECOVERY_MS := 1500
const RECOVERY_PENDING_MAX := 48
const AUTHORED_SPEED := 3.0
const AUTHORED_SPEED_TOLERANCE := 0.05
const AUTHORED_ACCELERATION := 9.0
const AUTHORED_DECELERATION := 5.0
const AUTHORED_FRICTION := 4.0
const AUTHORED_JUMP_SPEED := 3.0
const AUTHORED_DRIVE_TICKS := 36
const AUTHORED_AIR_TICKS := 20
const AUTHORED_AIR_HEIGHT := 0.05
const AUTHORED_AIR_DRIFT_LIMIT := 0.03
const JUMP_IMMEDIATE_RISE := 0.05
const JUMP_MIN_RISE := 0.75
const JUMP_MAX_RISE := 1.65
const JUMP_LANDING_MARGIN := 0.08
const JUMP_HELD_AFTER_LANDING_MS := 400
const STYLE_LOOK_OFFSET := 0.45
const STYLE_STEER_TICKS := 30
const STYLE_SIDESTEP_LIMIT := 0.003
const STYLE_LOCK_DISTANCE := 10.0
const STYLE_SETTLE_SECONDS := 0.7
const STYLE_MODES := ["free-orbit", "free-orbit-live", "forward-only", "lock-on"]
const MOVEMENT_MODES := ["conditioned", "long-outage", "authority-opt-out", "guest-opt-out", "incompatible-shape",
	"incompatible-tuning", "incompatible-style", "authored-components", "jump", "jump-live", "free-orbit",
	"free-orbit-live", "forward-only", "lock-on"]

var _game: Game
var _owner: Node
var _profile: PlayerControlProfile
var _markers: String
var _conditioned := false
var _mode := ""


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() not in [3, 4] or args[0] not in ["listen", "dedicated", "guest", "late"] \
			or (args.size() == 4 and args[3] not in MOVEMENT_MODES):
		push_error("Usage: movement_network.gd -- listen|dedicated|guest|late PORT MARKER_DIRECTORY [MODE]")
		quit(1)
		return
	Engine.max_fps = 120
	_markers = args[2]
	_mode = args[3] if args.size() == 4 else ""
	_conditioned = _mode in ["conditioned", "long-outage", "jump"]
	_profile = DefaultPlayerProfile.build()
	_profile.predict_movement = _mode != "authority-opt-out" or args[0] == "guest"
	if _mode == "guest-opt-out" and args[0] == "guest":
		_profile.predict_movement = false
	if _mode in ["jump-live", "free-orbit-live"]:
		_profile.predict_movement = false
	match _mode:
		"free-orbit", "free-orbit-live":
			_profile.controller_style = MoverComponent.FREE_ORBIT
		"forward-only":
			_profile.controller_style = MoverComponent.FORWARD_ONLY
		"lock-on":
			_profile.controller_style = MoverComponent.LOCK_ON
		"incompatible-style":
			if args[0] == "guest":
				_profile.controller_style = MoverComponent.FREE_ORBIT
	if _mode in ["jump", "jump-live"]:
		# Use the framework's default gravity and takeoff for both fixtures, independently of demo tuning.
		_profile.movement_rule = Locomotion.ground()
	if _conditioned:
		# Backward follows world +Z throughout the wall test, regardless of aim.
		_profile.forward_direction = PlayerControlProfile.WORLD
	_owner = Node.new()
	_owner.name = &"MovementFixture"
	root.add_child(_owner)
	var player_scene := Fixture.player()
	if _mode == "incompatible-shape" and args[0] == "guest":
		player_scene = _player_with_different_capsule()
	var use_authored_motion := _mode in ["authored-components", "incompatible-tuning"]
	if use_authored_motion:
		player_scene = _player_with_authored_motion(_mode == "incompatible-tuning" and args[0] == "guest")
	var config := Game.configure(Fixture.world(), player_scene).world_id(Fixture.WORLD_ID).lan(4).on_port(args[1].to_int())
	config = config.player_controls() if use_authored_motion else config.player_controls(_profile)
	_game = config.boot(_owner)
	if not _check(_game.boot_result().ok and _game.is_player_controlled(), "Boot the game with the default movement profile."):
		quit(1)
		return

	var passed := false
	if _mode in ["incompatible-shape", "incompatible-tuning", "incompatible-style"]:
		passed = await _check_incompatible_movement(args[0])
	elif _mode == "authored-components":
		passed = await _check_authored_motion(args[0])
	elif _mode in ["jump", "jump-live"]:
		match args[0]:
			"listen", "dedicated":
				passed = await _jump_authority(args[0] == "dedicated")
			"guest":
				passed = await _jump_guest()
			"late":
				passed = await _jump_observer()
	elif _mode in STYLE_MODES:
		match args[0]:
			"listen", "dedicated":
				passed = await _style_authority(args[0] == "dedicated")
			"guest":
				passed = await _style_guest()
			"late":
				passed = await _style_observer()
	else:
		match args[0]:
			"listen", "dedicated":
				passed = await _authority(args[0] == "dedicated")
			"guest":
				passed = await _guest()
			"late":
				passed = await _late_guest()
	if passed and args[0] == "guest" and (_conditioned or _uses_snapshot_fallback() or _mode == "lock-on"):
		# ENet sends an unacknowledged notification on close. Isolate clean leave from intentional packet loss.
		passed = _publish("relay-clean-leave-request") \
			and await _wait_until(_marked.bind("relay-clean-leave-ready"), "the conditioned link to allow a clean leave")
	if _mode not in ["incompatible-shape", "incompatible-tuning", "incompatible-style"] or args[0] == "listen":
		var left: GameResult = await _game.leave()
		passed = passed and _check(left.ok, "Leave the movement fixture session.")
	if passed and args[0] in ["guest", "late"]:
		passed = _publish(args[0] + "-left")
	if passed:
		print("CRIMSON_DEMO_MOVEMENT_PASSED %s" % args[0])
	quit(0 if passed else 1)


## Test the listen host's own input and monitor the exact admitted guest on the authority.
func _jump_authority(dedicated: bool) -> bool:
	var started: GameResult
	if dedicated:
		started = await _game.serve(ADDRESS)
	else:
		started = await _game.host(ADDRESS)
	if not _check(started.ok, started.message):
		return false
	if not dedicated:
		var local := _game.get_local_player()
		if not _check(_boot_local_controller() != null, "Boot the listen host's controls."):
			return false
		await _settle_jump_spawn()
		var floor_y := local.global_position.y
		_key(KEY_SPACE, true)
		var jumped := await _watch_jump(local, floor_y, "listen host")
		_key(KEY_SPACE, false)
		if not jumped:
			return false
	if not _publish("authority-ready") \
			or not await _wait_until(_marked.bind("guest-ready"), "the jumping guest"):
		return false
	var peer: int = _read("guest-ready")["peer"]
	var guest := _game.get_player(peer)
	if not _check(guest != null and Mover.for_node(guest) != null, "The server owns the admitted jumper's mover.") \
			or not _publish("authority-jump-watching") \
			or not await _watch_jump(guest, guest.global_position.y, "authority") \
			or not _publish("authority-jump-finished", _sample(guest)):
		return false
	if not await _wait_until(_marked.bind("late-left"), "the observer to leave"):
		return false
	var observer_id: int = _read("observer-jump-watching")["peer"]
	return await _wait_until(func() -> bool: return _game.get_player(observer_id) == null, "the observer's retirement") \
		and _publish("observer-retired") \
		and await _wait_until(_marked.bind("guest-left"), "the jumper to leave") \
		and await _wait_until(func() -> bool: return _game.get_player(peer) == null, "the jumper's retirement")


## Drive the authored Space binding. Press again airborne and hold through landing.
func _jump_guest() -> bool:
	if not await _join():
		return false
	var local := _game.get_local_player()
	if not _check(_boot_local_controller() != null, "Boot the jumping guest's controls.") \
			or not _publish("guest-ready", {"peer": local.multiplayer.get_unique_id()}) \
			or not await _wait_until(_marked.bind("authority-jump-watching"), "the authority jump watch") \
			or not await _wait_until(_marked.bind("observer-jump-watching"), "the remote proxy jump watch"):
		return false
	await _settle_jump_spawn()
	var floor_y := local.global_position.y
	if _conditioned and (not _publish("burst-request") \
			or not await _wait_until(_marked.bind("burst-active"), "the uplink packet loss to start")):
		return false
	_key(KEY_SPACE, true)
	if _conditioned:
		for tick in IMMEDIATE_TICKS:
			await physics_frame
		var rise := local.global_position.y - floor_y
		if not _check(rise >= JUMP_IMMEDIATE_RISE and not _marked("burst-complete"),
				"Space predicted %.3f m of takeoff while uplink packets were still blocked." % rise):
			return false
		print("CRIMSON_JUMP_PREDICTED %.3f m in %d ticks with uplink blocked" % [rise, IMMEDIATE_TICKS])
	if not await _wait_until(func() -> bool: return local.global_position.y - floor_y >= 0.4, "the guest to be airborne"):
		return false
	_key(KEY_SPACE, false)
	for tick in 2:
		await physics_frame
	_key(KEY_SPACE, true)
	var landed := await _watch_jump(local, floor_y, "owning guest")
	_key(KEY_SPACE, false)
	if not landed \
			or not await _wait_until(_marked.bind("authority-jump-finished"), "the authority's landing") \
			or not await _wait_until(_marked.bind("observer-jump-finished"), "the other client's view of the landing"):
		return false
	var committed: Vector3 = _read("authority-jump-finished")["position"]
	return await _wait_until(_matches.bind(local, {"position": committed}, false), "owner and authority to converge after landing") \
		and _publish("owner-jump-verified") \
		and await _wait_until(_marked.bind("observer-retired"), "the observer to leave before the jumper")


## Check another admitted client's interpolation of the jumper's ascent and descent.
func _jump_observer() -> bool:
	if not await _wait_until(_marked.bind("guest-ready"), "the jumper's admission") or not await _join():
		return false
	var peer: int = _read("guest-ready")["peer"]
	var proxy := _game.get_player(peer)
	if not _check(proxy != null and Mover.for_node(proxy) == null, "The observer sees a snapshot-driven proxy.") \
			or not _publish("observer-jump-watching", {"peer": _game.get_local_player().multiplayer.get_unique_id()}) \
			or not await _watch_jump(proxy, proxy.global_position.y, "other client") \
			or not _publish("observer-jump-finished"):
		return false
	return await _wait_until(_marked.bind("owner-jump-verified"), "the jumper to finish verifying")


## Observe one bounded arc and keep checking long enough to detect an auto-repeat on held Space.
func _watch_jump(body: Node3D, floor_y: float, role: String) -> bool:
	var peak := 0.0
	var landed_at := 0
	var deadline := Time.get_ticks_msec() + WAIT_MS
	while Time.get_ticks_msec() < deadline:
		var rise := body.global_position.y - floor_y
		peak = maxf(peak, rise)
		if not _check(peak <= JUMP_MAX_RISE, "%s gained too much height from an airborne press: %.3f m." % [role, peak]):
			return false
		if peak >= JUMP_MIN_RISE:
			if rise <= JUMP_LANDING_MARGIN and landed_at == 0:
				landed_at = Time.get_ticks_msec()
			elif landed_at != 0:
				if not _check(absf(rise) <= JUMP_LANDING_MARGIN, "%s jumped again without a new grounded press." % role):
					return false
				if Time.get_ticks_msec() - landed_at >= JUMP_HELD_AFTER_LANDING_MS:
					print("CRIMSON_JUMP_ARC %s peak %.3f m, landed without repeat" % [role, peak])
					return true
		await process_frame
	return _check(false, "%s did not complete a jump arc. Peak rise was %.3f m." % [role, peak])


## Let the grounded solver establish floor contact before delivering a press.
## Observe the admitted owner and publish the position, body heading, and view from authority.
func _style_authority(dedicated: bool) -> bool:
	var started: GameResult = await (_game.serve(ADDRESS) if dedicated else _game.host(ADDRESS))
	if not _check(started.ok, started.message) or not _publish("authority-ready") \
			or not await _wait_until(_marked.bind("guest-ready"), "the controller-style guest to join"):
		return false
	var peer_id: int = _read("guest-ready")["peer"]
	var body := _game.get_player(peer_id)
	if not _check(body != null, "The authority has the controller-style guest.") \
			or not await _wait_until(_marked.bind("guest-style-stop"), "the style guest's input to finish", LATE_JOIN_SETUP_MS):
		return false
	var expected := _read("guest-style-stop")
	if not await _wait_until(_style_matches.bind(body, expected), "authority to commit the guest's movement and facing"):
		return false
	var observer_id: int = _read("style-observer-ready")["peer"]
	return _publish("authority-style-stop", _style_pose(body)) \
		and await _wait_until(func() -> bool: return _game.get_player(observer_id) == null,
			"the authority to retire the style observer") \
		and _publish("style-observer-retired") \
		and await _wait_until(func() -> bool: return _game.get_player(peer_id) == null,
			"the authority to retire the style guest") \
		and await _wait_until(_marked.bind("guest-left"), "the style guest to finish leaving")


## Drive the actual authored action, then compare the owner's settled pose with authority.
func _style_guest() -> bool:
	if not await _join():
		return false
	var body := _game.get_local_player()
	var controller := _boot_local_controller()
	var owner_mode := GameNetworkDiagnostics.GUEST_INTERPOLATED if _mode == "free-orbit-live" \
		else GameNetworkDiagnostics.GUEST_PREDICTED
	if not _check(controller != null and _game.network_diagnostics().movement_mode == owner_mode,
			"The owning guest uses the configured movement path.") \
			or not _publish("guest-ready", {"peer": body.multiplayer.get_unique_id()}) \
			or not await _wait_until(_marked.bind("style-observer-ready"), "the observer to receive the initial pose"):
		return false
	await _settle_jump_spawn()
	var view := body.get_node(^"Camera3D") as Camera3D
	var initial_yaw := body.global_rotation.y
	var desired_view := wrapf(initial_yaw + STYLE_LOOK_OFFSET, -PI, PI)
	view.global_rotation = Vector3(0.0, desired_view, 0.0)
	await physics_frame
	await physics_frame
	var expected_facing: float
	var desired_direction: Vector3
	var movement_key: Key
	if _mode == "lock-on":
		var target := Marker3D.new()
		_game.get_world().add_child(target)
		expected_facing = wrapf(initial_yaw - PI / 2.0, -PI, PI)
		desired_direction = _style_forward(expected_facing)
		target.global_position = body.global_position + desired_direction * STYLE_LOCK_DISTANCE
		if not _publish("burst-request") or not await _wait_until(_marked.bind("burst-active"), "the upload outage") \
				or not _check(controller.lock_on(target) == OK, "Lock onto the target in this guest's world."):
			return false
		await physics_frame
		await physics_frame
		if not _check(_angle_distance(body.global_rotation.y, expected_facing) < LOOK_TOLERANCE,
				"The owner turns toward the lock target before the upload outage recovers."):
			return false
		movement_key = KEY_W
	else:
		if not _check(_angle_distance(body.global_rotation.y, initial_yaw) < STABLE_ANGLE,
				"Looking around at rest leaves the exploration character's heading unchanged."):
			return false
		expected_facing = wrapf(desired_view - PI / 2.0, -PI, PI)
		desired_direction = _style_forward(expected_facing)
		movement_key = KEY_D
	var start := body.global_position
	_key(movement_key, true)
	for tick: int in STYLE_STEER_TICKS:
		await physics_frame
		if _mode == "forward-only" and tick == 0:
			var facing := _style_forward(body.global_rotation.y)
			var travel := (body.global_position - start).slide(Vector3.UP)
			if not _check(absf(travel.dot(facing.cross(Vector3.UP))) < STYLE_SIDESTEP_LIMIT,
					"Forward-only's first steering step has no sideways travel."):
				_key(movement_key, false)
				return false
	_key(movement_key, false)
	# Inputs keep streaming during round trips. Wait for the authority to retire the release tick.
	var released := _game.network_diagnostics()
	var release_tick: int = released.acknowledged_input + released.pending_inputs + 1
	if not await _wait_until(func() -> bool: return _game.network_diagnostics().acknowledged_input >= release_tick,
			"the authority to confirm the guest's steering release"):
		return false
	await create_timer(STYLE_SETTLE_SECONDS, true, false, true).timeout
	if not _check(is_instance_valid(body), "The owner is still connected after settling."):
		return false
	var traveled := (body.global_position - start).slide(Vector3.UP)
	if not _check(traveled.dot(desired_direction) >= MIN_TRAVEL,
		"The controller follows its steering frame. Travel %s expected %s." % [traveled, desired_direction]) \
			or not _check(_angle_distance(body.global_rotation.y, expected_facing) < LOOK_TOLERANCE \
			and _angle_distance(view.global_rotation.y, desired_view) < LOOK_TOLERANCE,
				"Body and view settle at their independently expected headings.") \
			or not _publish("guest-style-stop", _style_pose(body)) \
			or not await _wait_until(_marked.bind("authority-style-stop"), "the committed style pose"):
		return false
	var committed := _read("authority-style-stop")
	return await _wait_until(_style_matches.bind(body, committed), "the owner to converge to authority") \
		and await _wait_until(_marked.bind("style-observer-retired"), "the observer to receive the style snapshot and leave")


## A non-owning guest reconstructs body and view headings from independent snapshot fields.
func _style_observer() -> bool:
	if not await _wait_until(_marked.bind("guest-ready"), "the style owner to join") or not await _join():
		return false
	var peer_id: int = _read("guest-ready")["peer"]
	var body := _game.get_player(peer_id)
	if not _check(body != null and Mover.for_node(body) == null, "The observer has a proxy from the reliable roster.") \
			or not _publish("style-observer-ready", {"peer": _game.get_local_player().multiplayer.get_unique_id()}) \
			or not await _wait_until(_marked.bind("authority-style-stop"), "the settled authority pose", LATE_JOIN_SETUP_MS):
		return false
	var committed := _read("authority-style-stop")
	if not await _wait_until(_style_matches.bind(body, committed), "the observer to see the authoritative body and view headings"):
		return false
	var model := body.get_node_or_null(^"Visuals") as Node3D
	return _check(model != null and model.is_visible_in_tree()
		and _angle_distance(model.global_rotation.y, committed["yaw"]) < LOOK_TOLERANCE,
		"The observer sees the authored player model follow the authoritative body heading.")


## Convert a world yaw to Godot's horizontal negative-Z forward direction.
func _style_forward(heading: float) -> Vector3:
	return Vector3(-sin(heading), 0.0, -cos(heading))


## Capture world-space view and body heading independently.
func _style_pose(body: Node3D) -> Dictionary:
	var view := body.get_node(^"Camera3D") as Camera3D
	return {"position": body.global_position, "yaw": body.global_rotation.y, "view_yaw": view.global_rotation.y}


## Compare the settled owner or observer with the committed pose, including both headings.
func _style_matches(body: Node3D, expected: Dictionary) -> bool:
	if not is_instance_valid(body):
		return false
	var pose := _style_pose(body)
	return pose["position"].distance_to(expected["position"]) < POSITION_TOLERANCE \
		and _angle_distance(pose["yaw"], expected["yaw"]) < LOOK_TOLERANCE \
		and _angle_distance(pose["view_yaw"], expected["view_yaw"]) < LOOK_TOLERANCE


func _settle_jump_spawn() -> void:
	for tick in 12:
		await physics_frame


## Start the listen host or playerless authority, then observe each guest through its admitted ID.
func _authority(dedicated: bool) -> bool:
	var started: GameResult
	if dedicated:
		started = await _game.serve(ADDRESS)
	else:
		started = await _game.host(ADDRESS)
	if not _check(started.ok and _game.get_phase() == Game.RUNNING, started.message):
		return false
	if _conditioned:
		_add_ramp()

	var local := _game.get_local_player()
	if dedicated:
		if not _check(local == null and root.get_camera_3d() == null, "Dedicated authority starts without a local body or camera."):
			return false
	else:
		var controller := _boot_local_controller()
		if not _check(controller != null and Mover.for_node(local) != null, "Listen host uses the authoritative mover."):
			return false

	if not _publish("authority-ready", {"dedicated": dedicated}) \
			or not await _wait_until(_marked.bind("guest-ready"), "the first guest"):
		return false
	var guest_id: int = _read("guest-ready")["peer"]
	var guest := _game.get_player(guest_id)
	if not _check(guest != null and Mover.for_node(guest) != null, "The admitted guest has a server-owned mover."):
		return false
	var guest_start := _sample(guest)

	var host_stop: Dictionary
	if not dedicated:
		var host_start := _sample(local)
		await _drive(DRIVE_TICKS)
		if not await _wait_for_stop(local, host_start, "the listen host to move, look, and stop"):
			return false
		host_stop = _sample(local)
		if not _check(_horizontal_distance(guest.global_position, guest_start["position"]) < POSITION_TOLERANCE,
				"Listen input moves only the host's actor."):
			return false
	if not _publish("host-settled", host_stop):
		return false

	if _conditioned:
		if not await _observe_prediction(guest, guest_start):
			return false
	else:
		if _uses_snapshot_fallback() and not await _wait_until(_marked.bind("guest-fallback-ready"),
				"the opt-out guest's local response check"):
			return false
		if not await _wait_until(_has_horizontal_motion.bind(guest), "the first guest to drive on the authority") \
				or not await _wait_until(_marked.bind("guest-input-silent"), "the guest to stop sending input") \
				or not await _wait_for_stop(guest, guest_start, "the authority to expire guest steering",
					INPUT_EXPIRY_MARGIN_MS):
			return false
	var guest_stop := _sample(guest)
	if local != null and not _check(_matches(local, host_stop), "Guest input does not move or aim the host's actor."):
		return false
	guest_stop["peer"] = guest_id
	if not _publish("guest-settled", guest_stop) \
			or not await _wait_until(_marked.bind("late-ready"), "the late guest's baseline check"):
		return false

	var late_ready := _read("late-ready")
	var late_id: int = late_ready["peer"]
	var late := _game.get_player(late_id)
	if not _check(late != null and Mover.for_node(late) != null, "The late guest has an independent authority mover.") \
			or not await _wait_for_stop(late, late_ready, "the late guest to move, look, and release"):
		return false
	if not _check(_matches(guest, guest_stop), "The late guest's input leaves the first guest's actor stopped."):
		return false
	if not _publish("late-settled", _sample(late)):
		return false
	if not await _wait_until(_marked.bind("late-left"), "the late guest to verify and leave"):
		print("Authority late guest pose at timeout. Actual %s. Published %s." %
			[_sample(late) if is_instance_valid(late) else {}, _read("late-settled")])
		return false
	if not await _wait_until(func() -> bool: return _game.get_player(late_id) == null,
				"the authority to retire the late actor") \
			or not _publish("late-retired"):
		return false
	return await _wait_until(_marked.bind("guest-left"), "the first guest to verify and leave") \
		and await _wait_until(func() -> bool: return _game.get_player(guest_id) == null,
			"the authority to retire the first guest actor")


## Exercise the first guest and verify each other admitted actor is observed.
func _guest() -> bool:
	if not await _join():
		return false
	var controller := _boot_local_controller()
	var local := _game.get_local_player()
	if not _check(controller != null and Mover.for_node(local) == null, "Guest controls do not attach a second physics mover."):
		return false
	var start := _sample(local)
	if not _publish("guest-ready", {"peer": local.multiplayer.get_unique_id()}) \
			or not await _wait_until(_marked.bind("host-settled"), "the host's movement check"):
		return false
	if not _read("authority-ready")["dedicated"]:
		var host := _game.get_player(AUTHORITY_ID)
		var host_stop := _read("host-settled")
		if not await _wait_until(_matches.bind(host, host_stop), "the guest to see the host's movement and aim"):
			return false

	if _conditioned:
		if not await _predict_and_stop(local):
			return false
	else:
		if _uses_snapshot_fallback() and not await _check_fallback(local):
			return false
		if not await _silence_input(local, controller):
			return false
	if not await _wait_until(_marked.bind("guest-settled"), "the guest's committed pose"):
		return false
	var guest_stop := _read("guest-settled")
	if not await _wait_until(_matches.bind(local, guest_stop, false), "the guest to converge to its server position") \
			or not _check(_horizontal_distance(local.global_position, start["position"]) >= MIN_TRAVEL,
				"The first guest moves through its scoped controls."):
		return false

	if not await _wait_until(_marked.bind("late-settled"), "the late guest's committed pose"):
		return false
	var late_id: int = _read("late-ready")["peer"]
	var late_pose := _read("late-settled")
	if not await _wait_until(func() -> bool: return _matches(_game.get_player(late_id), late_pose),
			"the first guest to see the late guest's movement and aim"):
		var late := _game.get_player(late_id)
		var world := _game.get_world()
		print("Late guest timeout. Phase %s. Actor present %s. Initial %s. Actual %s. Expected %s. World children %s." %
			[_game.get_phase(), late != null, _read("late-ready"), _sample(late) if late != null else {}, late_pose,
				world.get_children().map(func(child: Node) -> StringName: return child.name) if world != null else []])
		return false
	return _publish("late-observed") \
		and await _wait_until(_marked.bind("late-retired"), "the authority to finish the other guest's departure")


func _uses_snapshot_fallback() -> bool:
	return _mode in ["authority-opt-out", "guest-opt-out"]


## An opt-out keeps remote steering and snapshots while waiting for authority before translation.
func _check_fallback(local: Node3D) -> bool:
	var before := local.global_position
	_key(KEY_W, true)
	for tick in IMMEDIATE_TICKS:
		await physics_frame
	_key(KEY_W, false)
	var movement := _horizontal_distance(local.global_position, before)
	print("Guest opt-out travel after %d physics ticks was %.3f m (%s)." % [IMMEDIATE_TICKS, movement, _mode])
	return _check(movement < IMMEDIATE_TRAVEL, "An opt-out guest moved ahead of snapshots.") \
		and _publish("guest-fallback-ready")


func _player_with_different_capsule() -> PackedScene:
	var player := Fixture.player().instantiate() as CharacterBody3D
	var collider := player.get_node(^"CollisionShape3D") as CollisionShape3D
	var capsule := collider.shape.duplicate() as CapsuleShape3D
	capsule.radius += 0.05
	collider.shape = capsule
	var packed := PackedScene.new()
	var packed_ok := packed.pack(player)
	player.free()
	return packed if packed_ok == OK else null


## Author the test player's inline Mover settings before boot.
func _player_with_authored_motion(different_acceleration: bool) -> PackedScene:
	var player := Fixture.player().instantiate() as CharacterBody3D
	var motion := player.get_node_or_null(^"Mover") as MoverComponent
	if motion == null:
		player.free()
		return null
	motion.move_speed = AUTHORED_SPEED
	motion.forward_direction = MoverComponent.WORLD
	motion.predict_movement = true
	motion.gravity_scale = 1.0
	motion.acceleration = AUTHORED_ACCELERATION * 2.0 if different_acceleration else AUTHORED_ACCELERATION
	motion.deceleration = AUTHORED_DECELERATION
	motion.ground_friction = AUTHORED_FRICTION
	motion.air_control = 0.0
	motion.jump_speed = AUTHORED_JUMP_SPEED
	var packed := PackedScene.new()
	var packed_ok := packed.pack(player)
	player.free()
	return packed if packed_ok == OK else null


## Verify inline movement tuning travels through the no-profile game and client prediction.
func _check_authored_motion(role: String) -> bool:
	if role == "listen":
		var hosted: GameResult = await _game.host(ADDRESS)
		if not _check(hosted.ok, hosted.message) or not _publish("authority-ready") \
				or not await _wait_until(_marked.bind("guest-ready"), "the authored guest to join"):
			return false
		var guest_ready := _read("guest-ready")
		var guest_id: int = guest_ready["peer"]
		var guest := _game.get_player(guest_id)
		if not _check(guest != null, "The authority has the scene-configured guest.") \
				or not _publish("authority-watching") \
				or not await _watch_authored_motion(guest, guest_ready["position"]):
			return false
		return _publish("guest-settled", _sample(guest)) \
			and await _wait_until(_marked.bind("guest-left"), "the authored guest to leave") \
			and await _wait_until(func() -> bool: return _game.get_player(guest_id) == null, "the authored actor to retire")
	if not await _join():
		return false
	var controller := _boot_local_controller()
	var local := _game.get_local_player()
	await _settle_jump_spawn()
	var start := local.global_position
	if not _check(controller != null and _game.network_diagnostics().movement_mode == GameNetworkDiagnostics.GUEST_PREDICTED,
			"The scene configured movement prediction and supplies local input.") \
			or not _publish("guest-ready", {"peer": local.multiplayer.get_unique_id(), "position": start}) \
			or not await _wait_until(_marked.bind("authority-watching"), "the authority to sample movement"):
		return false
	_key(KEY_W, true)
	for tick in AUTHORED_DRIVE_TICKS:
		await physics_frame
		if tick == 9:
			var travel := start.z - local.global_position.z
			if not _check(travel > 0.08 and travel < 0.20, "Owner predicted %.3f m in its first ten acceleration ticks." % travel):
				return false
	_key(KEY_SPACE, true)
	for tick in 5:
		await physics_frame
	var airborne := local.global_position
	if not _check(airborne.y - start.y > AUTHORED_AIR_HEIGHT, "The owner predicts the authored jump."):
		return false
	_key(KEY_SPACE, false)
	_key(KEY_W, false)
	_key(KEY_D, true)
	for tick in AUTHORED_AIR_TICKS:
		await physics_frame
	_key(KEY_D, false)
	var coast := local.global_position - airborne
	if not _check(absf(coast.x) < AUTHORED_AIR_DRIFT_LIMIT and coast.z < -0.8,
			"Zero air control keeps takeoff momentum while airborne input points right. Observed %s." % coast):
		return false
	if not await _wait_until(_marked.bind("guest-settled"), "the scene-configured server stop"):
		return false
	var settled := _read("guest-settled")
	if not _check(settled.has("position"), "Read the authority's completed movement observation."):
		return false
	return await _wait_for_reconciliation(local, settled["position"])


## Measure the authority's acceleration, air momentum, jump height, and braking on flat floor.
func _watch_authored_motion(body: Node3D, start: Vector3) -> bool:
	var peak_speed := 0.0
	var peak_height := 0.0
	var landing := Vector3.ZERO
	var landed := false
	var first_speed := 0.0
	var deadline := Time.get_ticks_msec() + WAIT_MS
	while Time.get_ticks_msec() < deadline:
		var velocity := Mover.for_node(body).velocity
		var position := body.global_position
		var rise := position.y - start.y
		var speed := maxf(-velocity.z, 0.0)
		if first_speed == 0.0 and speed > 0.05:
			first_speed = speed
		peak_speed = maxf(peak_speed, speed)
		peak_height = maxf(peak_height, rise)
		if not _check(absf(position.x - start.x) < AUTHORED_AIR_DRIFT_LIMIT,
				"Air input did not redirect the authority. X drift %.3f m." % (position.x - start.x)):
			return false
		if not landed and peak_height > AUTHORED_AIR_HEIGHT and rise <= AUTHORED_AIR_HEIGHT:
			landed = true
			landing = position
		if landed and speed < 0.02:
			var braking_distance := landing.z - position.z
			print("CRIMSON_AUTHORED_MOVER first %.3f m/s, top %.3f m/s, jump %.3f m, braking %.3f m" %
				[first_speed, peak_speed, peak_height, braking_distance])
			return _check(first_speed > 0.05 and first_speed < 0.25
				and absf(peak_speed - AUTHORED_SPEED) < AUTHORED_SPEED_TOLERANCE
				and peak_height > 0.40 and peak_height < 0.53
				and braking_distance > 0.19 and braking_distance < 0.55,
				"The authority uses the scene's acceleration, top speed, jump speed, friction and deceleration.")
		await physics_frame
	return _check(false, "The authority did not complete the authored movement. Peak speed %.3f m/s, height %.3f m." %
		[peak_speed, peak_height])


## A solver settings mismatch refuses the join before handing out a playable actor.
func _check_incompatible_movement(role: String) -> bool:
	if role == "listen":
		var hosted: GameResult = await _game.host(ADDRESS)
		return _check(hosted.ok, hosted.message) and _publish("authority-ready") \
			and await _wait_until(_marked.bind("guest-left"), "the mismatched guest's rejection") \
			and _check(_game.get_phase() == Game.RUNNING and _game.get_local_player() != null,
				"The failed join keeps the host playing.")
	if not await _wait_until(_marked.bind("authority-ready"), "the authority"):
		return false
	var joined: GameResult = await _game.join(ADDRESS)
	var reason := "different controller styles" if _mode == "incompatible-style" else "different movement solver settings"
	return _check(not joined.ok and joined.code == "IncompatibleRules" \
			and reason in joined.message and _game.get_local_player() == null,
			"A mismatched movement setting should fail before handing out a guest actor. Got %s. %s" % [joined.code, joined.message])


## Stop the normal transport producer without sending a neutral movement sample.
func _silence_input(local: Node3D, controller: PlayerControl) -> bool:
	await _drive(DRIVE_TICKS, false)
	var driver := local.get_node_or_null(^"_PlayerControl")
	if not _check(driver != null and controller.controls().vector(&"move").y < -0.5,
			"Stop the transport producer while forward input is still held."):
		return false
	driver.set_physics_process(false)
	_key(KEY_W, false)
	return _publish("guest-input-silent")


## Observe delayed steering, release during a loss burst, and a sustained push into authored collision.
func _observe_prediction(guest: Node3D, start: Dictionary) -> bool:
	if not await _wait_until(_has_horizontal_motion.bind(guest), "the delayed guest input to reach the authority") \
			or not _publish("prediction-authority-started") \
			or not await _wait_until(_marked.bind("prediction-immediate"), "the guest's immediate response check") \
			or not await _wait_until(_marked.bind("burst-complete"), "the configured packet-loss burst") \
			or not await _wait_for_stop(guest, start, "the guest to stop after releasing during packet loss") \
			or not _publish("prediction-settled", _sample(guest)) \
			or not await _wait_until(_marked.bind("prediction-wall-start"), "the guest's wall approach"):
		return false
	var deadline := Time.get_ticks_msec() + WAIT_MS
	while Time.get_ticks_msec() < deadline:
		if not _check(is_instance_valid(guest), "The guest departed before the wall check completed."):
			return false
		if not _check(guest.global_position.z <= WALL_LIMIT_Z,
				"The authoritative guest body crossed the south wall. Z was %s." % guest.global_position.z):
			return false
		if _marked("prediction-wall-released") and guest.global_position.z >= WALL_REACH_Z \
				and not _has_horizontal_motion(guest):
			return _publish("prediction-wall-settled", _sample(guest)) and await _observe_ramp(guest)
		await physics_frame
	return _check(false, "The authoritative guest never settled against the south wall. Z was %s." % guest.global_position.z)


## Check the player's visible motion before the authority can react, then check correction and collision.
func _predict_and_stop(local: Node3D) -> bool:
	var start := local.global_position
	var started := Time.get_ticks_msec()
	var immediate_travel := 0.0
	var first_motion_tick := 0
	_key(KEY_W, true)
	_stick(JOY_AXIS_RIGHT_X, 0.7)
	_stick(JOY_AXIS_RIGHT_Y, -0.55)
	for tick in DRIVE_TICKS:
		await physics_frame
		if first_motion_tick == 0:
			immediate_travel = _horizontal_distance(local.global_position, start)
			if immediate_travel >= IMMEDIATE_TRAVEL and not _marked("prediction-authority-started"):
				first_motion_tick = tick + 1
				if not _publish("prediction-immediate"):
					return false
				print("Guest predicted %.3f m of travel in %d physics ticks (%d ms), before authority motion." %
					[immediate_travel, first_motion_tick, Time.get_ticks_msec() - started])
			elif not _check(tick + 1 < IMMEDIATE_TICKS,
					"Predicted guest did not move within three ticks before the authority. Travel %.3f m." % immediate_travel):
				return false
	_stick(JOY_AXIS_RIGHT_X, 0.0)
	_stick(JOY_AXIS_RIGHT_Y, 0.0)
	if not _publish("burst-request") \
			or not await _wait_until(_marked.bind("burst-active"), "the conditioner to begin dropping guest datagrams"):
		return false
	_key(KEY_W, false)
	if not _check(not _marked("burst-complete"), "Release the movement key while the loss burst is active.") \
			or not await _wait_until(_marked.bind("burst-complete"), "the relay to restore the guest's input link"):
		return false
	if _mode == "long-outage" and not await _check_history_recovery():
		return false
	if not await _wait_until(_marked.bind("prediction-settled"), "the authority's stop after the loss burst"):
		return false
	var expected: Vector3 = _read("prediction-settled")["position"]
	if not await _wait_for_reconciliation(local, expected):
		return false
	print("Guest stayed within %.3f m of the authority after release and packet loss." %
		local.global_position.distance_to(expected))
	if not _publish("prediction-wall-start"):
		return false
	_key(KEY_S, true)
	for tick in WALL_DRIVE_TICKS:
		await physics_frame
		if not _check(local.global_position.z <= WALL_LIMIT_Z,
				"The predicted guest body crossed the south wall. Z was %s." % local.global_position.z):
			return false
	_key(KEY_S, false)
	if not _check(local.global_position.z >= WALL_REACH_Z,
			"The predicted guest never reached the south wall. Z was %s." % local.global_position.z) \
			or not _publish("prediction-wall-released") \
			or not await _wait_until(_marked.bind("prediction-wall-settled"), "the authority's wall stop"):
		return false
	var wall_stop: Vector3 = _read("prediction-wall-settled")["position"]
	if not await _wait_for_reconciliation(local, wall_stop):
		return false
	print("Guest and authority stopped at south wall Z %.3f m and %.3f m. Pose difference %.3f m." %
		[local.global_position.z, wall_stop.z, local.global_position.distance_to(wall_stop)])
	return await _predict_ramp(local)


## A restored uplink must retire the stale command backlog so prediction can replay fresh input.
func _check_history_recovery() -> bool:
	var before := _game.network_diagnostics()
	var started := Time.get_ticks_msec()
	while Time.get_ticks_msec() - started < RECOVERY_MS:
		var after := _game.network_diagnostics()
		if after.acknowledged_input > before.acknowledged_input and after.pending_inputs <= RECOVERY_PENDING_MAX:
			print("Input history recovered in %d ms. Pending %d to %d, acknowledged %d to %d." %
				[Time.get_ticks_msec() - started, before.pending_inputs, after.pending_inputs,
					before.acknowledged_input, after.acknowledged_input])
			return true
		await process_frame
	var expired := _game.network_diagnostics()
	return _check(false, "Input history did not recover after the restored uplink. Pending %d to %d, acknowledged %d to %d." %
		[before.pending_inputs, expired.pending_inputs, before.acknowledged_input, expired.acknowledged_input])


func _wait_for_reconciliation(local: Node3D, expected: Vector3) -> bool:
	var stable_since := Time.get_ticks_msec()
	var deadline := stable_since + WAIT_MS
	while Time.get_ticks_msec() < deadline:
		var now := Time.get_ticks_msec()
		if local.global_position.distance_to(expected) > RECONCILED_POSITION:
			stable_since = now
		elif now - stable_since >= STABILITY_MS:
			return true
		await physics_frame
	return _check(false, "Prediction did not converge to the stopped authority. Actual %s, expected %s." %
		[local.global_position, expected])


## Add the same static incline to every copy of the test world.
func _add_ramp() -> void:
	var incline := Basis(Vector3.BACK, deg_to_rad(20.0))
	var ramp_size := Vector3(3.3, 0.30, 3.0)
	var low_edge := Vector3(3.10, -0.04, 8.0)
	var offset := incline * Vector3(ramp_size.x * 0.5, -ramp_size.y * 0.5, 0.0)
	var ramp := StaticBody3D.new()
	ramp.name = &"MovementFixtureRamp"
	ramp.transform = Transform3D(incline, low_edge + offset)
	var shape := BoxShape3D.new()
	shape.size = ramp_size
	var collider := CollisionShape3D.new()
	collider.shape = shape
	ramp.add_child(collider)
	_game.get_world().add_child(ramp)


func _observe_ramp(guest: Node3D) -> bool:
	if not await _wait_until(_marked.bind("prediction-ramp-start"), "the guest's ramp approach"):
		return false
	var peak := 0.0
	var stable_since := Time.get_ticks_msec()
	var deadline := stable_since + WAIT_MS
	while Time.get_ticks_msec() < deadline:
		if not _check(is_instance_valid(guest), "The guest departed before the ramp check completed."):
			return false
		var now := Time.get_ticks_msec()
		var position := guest.global_position
		peak = maxf(peak, position.y)
		if not _marked("prediction-ramp-released") or absf(position.y) >= FLOOR_MARGIN \
				or position.x < RAMP_LANDING_X or _has_horizontal_motion(guest):
			stable_since = now
		elif now - stable_since >= STABILITY_MS:
			return _check(peak >= RAMP_RISE, "Authority skipped the ramp rise. Peak was %.3f m." % peak)
		await physics_frame
	return _check(false, "Authority did not land after walking off the ramp. Peak %.3f m. Pose %s." %
		[peak, guest.global_position])


func _predict_ramp(local: Node3D) -> bool:
	if not _publish("prediction-ramp-start"):
		return false
	_key(KEY_D, true)
	var peak := 0.0
	for tick in RAMP_DRIVE_TICKS:
		await physics_frame
		var position := local.global_position
		peak = maxf(peak, position.y)
		if position.x >= RAMP_LANDING_X and absf(position.y) < FLOOR_MARGIN:
			_key(KEY_D, false)
			print("Guest climbed the ramp to %.3f m and landed beyond its edge." % peak)
			return _check(peak >= RAMP_RISE, "Predicted movement skipped the ramp rise. Peak was %.3f m." % peak) \
				and _publish("prediction-ramp-released")
	_key(KEY_D, false)
	return _check(false, "Predicted movement did not land after walking off the ramp. Peak %.3f m. Pose %s." %
		[peak, local.global_position])


## Check the first guest's current pose immediately after joining, then move the late guest itself.
func _late_guest() -> bool:
	if not await _wait_until(_marked.bind("guest-settled"), "a changed pose before joining", LATE_JOIN_SETUP_MS) \
			or not await _join():
		return false
	var guest_stop := _read("guest-settled")
	var guest_id: int = guest_stop["peer"]
	var guest := _game.get_player(guest_id)
	if not _check(_matches(guest, guest_stop) and Mover.for_node(guest) == null,
			"Join publishes the current remote pose and aim before reporting ready."):
		return false
	var remote_view := guest.get_node(^"Camera3D") as Camera3D
	var denied := _profile.boot(guest, remote_view)
	if not _check(not denied.is_ready() and Controls.for_node(guest) == null and Mover.for_node(guest) == null,
			"An admitted guest cannot install local controls or a mover on someone else's actor."):
		return false
	if not _read("authority-ready")["dedicated"]:
		var host := _game.get_player(AUTHORITY_ID)
		if not _check(_matches(host, _read("host-settled")), "The late baseline includes the moved listen host."):
			return false

	var controller := _boot_local_controller()
	var local := _game.get_local_player()
	if not _check(controller != null and Mover.for_node(local) == null, "The late guest uses the same local input path."):
		return false
	var start := _sample(local)
	start["peer"] = local.multiplayer.get_unique_id()
	if not _publish("late-ready", start):
		return false
	await _drive(DRIVE_TICKS)
	if not await _wait_until(_marked.bind("late-settled"), "the late guest's authoritative stop"):
		return false
	return await _wait_until(_matches.bind(local, _read("late-settled"), false), "the late guest to converge to its server position") \
		and await _wait_until(_marked.bind("late-observed"), "the first guest to observe the final pose")


## Join the already-started authority through the standard facade.
func _join() -> bool:
	if not await _wait_until(_marked.bind("authority-ready"), "the authority"):
		return false
	var joined: GameResult = await _game.join(ADDRESS)
	if _conditioned and _mode != "jump" and joined.ok:
		_add_ramp()
	return _check(joined.ok and _game.get_phase() == Game.RUNNING and _game.get_local_player() != null,
		joined.message if not joined.ok else "Join publishes the local actor.")


## Attach the authored Controls component or the fixture's explicit input profile to the active camera.
func _boot_local_controller() -> PlayerControl:
	var body := _game.get_local_player()
	var camera := root.get_camera_3d()
	if body == null or camera == null or not body.is_ancestor_of(camera):
		push_error("The active camera does not belong to the local body.")
		return null
	var controller: PlayerControl
	if _mode in ["authored-components", "incompatible-tuning"]:
		var component := ControlsComponent.for_body(body)
		if not _check(component != null, "The authored player exposes its Controls component."):
			return null
		controller = component.boot(camera)
	else:
		controller = _profile.boot(body, camera)
	if not _check(controller.is_ready(), controller.get_message()):
		return null
	return controller


## Drive the authored keyboard and controller actions through the root viewport.
func _drive(ticks: int, release_forward: bool = true) -> void:
	_key(KEY_W, true)
	_stick(JOY_AXIS_RIGHT_X, 0.7)
	_stick(JOY_AXIS_RIGHT_Y, -0.55)
	for tick in ticks:
		await physics_frame
	_stick(JOY_AXIS_RIGHT_X, 0.0)
	_stick(JOY_AXIS_RIGHT_Y, 0.0)
	if release_forward:
		_key(KEY_W, false)


func _key(code: Key, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	root.push_input(event, true)


func _stick(axis: JoyAxis, strength: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = 0
	event.axis = axis
	event.axis_value = strength
	root.push_input(event, true)


## Observe translation separately from replicated root heading and the view's pitch.
func _sample(body: Node3D) -> Dictionary:
	var view := body.get_node(^"Camera3D") as Camera3D
	return {"position": body.global_position, "yaw": body.global_rotation.y, "pitch": view.rotation.x}


func _horizontal_distance(first: Vector3, second: Vector3) -> float:
	return Vector2(first.x, first.z).distance_to(Vector2(second.x, second.z))


func _angle_distance(first: float, second: float) -> float:
	return absf(wrapf(first - second, -PI, PI))


func _has_horizontal_motion(body: Variant) -> bool:
	if not is_instance_valid(body):
		return false
	var mover := Mover.for_node(body)
	return mover != null and Vector2(mover.velocity.x, mover.velocity.z).length() > 0.5


## Authority has applied steering and look and has since stopped horizontal movement.
func _settled(body: Node3D, initial: Dictionary) -> bool:
	if not is_instance_valid(body):
		return false
	var mover := Mover.for_node(body)
	var now := _sample(body)
	return mover != null and Vector2(mover.velocity.x, mover.velocity.z).length() < 0.1 \
		and _horizontal_distance(now["position"], initial["position"]) >= MIN_TRAVEL \
		and _angle_distance(now["yaw"], initial["yaw"]) > LOOK_TOLERANCE \
		and _angle_distance(now["pitch"], initial["pitch"]) > LOOK_TOLERANCE


## Publish a reference pose only after steering and aim remain stable across multiple snapshots.
func _wait_for_stop(body: Node3D, initial: Dictionary, description: String, timeout_ms: int = WAIT_MS) -> bool:
	var candidate: Dictionary
	var stable_since := 0
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		var now := Time.get_ticks_msec()
		if _settled(body, initial):
			var sample := _sample(body)
			if candidate.is_empty() or not _same_sample(sample, candidate, STABLE_POSITION, STABLE_ANGLE):
				candidate = sample
				stable_since = now
			elif now - stable_since >= STABILITY_MS:
				return true
		else:
			candidate.clear()
		await process_frame
	return _check(false, "Timed out waiting for %s." % description)


## Local camera presentation can have immediate aim independent of its position snapshot.
func _matches(body: Node3D, expected: Dictionary, check_look: bool = true) -> bool:
	if not is_instance_valid(body) or not expected.has("position"):
		return false
	var actual := _sample(body)
	var position: Vector3 = actual["position"]
	return _same_sample(actual, expected, POSITION_TOLERANCE, LOOK_TOLERANCE) if check_look \
		else position.distance_to(expected["position"]) < POSITION_TOLERANCE


func _same_sample(actual: Dictionary, expected: Dictionary, position_tolerance: float, look_tolerance: float) -> bool:
	var position: Vector3 = actual["position"]
	return position.distance_to(expected["position"]) < position_tolerance \
		and _angle_distance(actual["yaw"], expected["yaw"]) < look_tolerance \
		and _angle_distance(actual["pitch"], expected["pitch"]) < look_tolerance


## Publish a fixture observation only after its complete data is on disk.
func _publish(event: String, payload: Dictionary = {}) -> bool:
	var path := _markers.path_join(event)
	var staged := path + ".pending"
	var file := FileAccess.open(staged, FileAccess.WRITE)
	if not _check(file != null, "Stage marker %s." % event):
		return false
	file.store_var(payload)
	file.close()
	return _check(DirAccess.rename_absolute(staged, path) == OK, "Publish marker %s." % event)


func _read(event: String) -> Dictionary:
	var file := FileAccess.open(_markers.path_join(event), FileAccess.READ)
	if file == null:
		return {}
	var payload: Dictionary = file.get_var(false)
	return payload


## Wait for a readable marker because Windows can report the rename before a reader can open it.
func _marked(event: String) -> bool:
	return FileAccess.open(_markers.path_join(event), FileAccess.READ) != null


func _wait_until(predicate: Callable, description: String, timeout_ms: int = WAIT_MS) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while not predicate.call():
		if Time.get_ticks_msec() >= deadline:
			return _check(false, "Timed out waiting for %s." % description)
		await process_frame
	return true


func _check(condition: bool, description: String) -> bool:
	if not condition:
		push_error(description)
	return condition
