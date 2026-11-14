# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises facing and travel from rest for each standalone controller style.

const FLOOR_SIZE := Vector3(30.0, 1.0, 30.0)
const TARGET_POSITION := Vector3(10.0, 0.0, 0.0)
const GAMEPAD_DEVICE := 0
const SETTLE_TICKS := 8
const LOOK_TICKS := 12
const STEER_TICKS := 3
const MIN_LOOK_ANGLE := 0.2
const AIM_TOLERANCE := 0.02
const MIN_STEER_SPEED := 0.1
const ALIGNMENT := 0.95

## Count failures without relying on Debug-only assertions.
var _failures: int = 0


## Wait until the root accepts new scene nodes.
func _initialize() -> void:
	_run.call_deferred()


## Run all four styles against the same flat collision and input bindings.
func _run() -> void:
	var floor_body := StaticBody3D.new()
	var floor_collision := CollisionShape3D.new()
	var floor_box := BoxShape3D.new()
	floor_box.size = FLOOR_SIZE
	floor_collision.shape = floor_box
	floor_body.position.y = -FLOOR_SIZE.y * 0.5
	floor_body.add_child(floor_collision)
	root.add_child(floor_body)
	await _check_aim(MoverComponent.FPS_AIM)
	await _check_aim(MoverComponent.FREE_ORBIT)
	await _check_forward_only()
	await _check_lock_on()
	if _failures == 0:
		print("CRIMSON_CONTROLLER_STYLES_TESTS_PASSED")
	quit(int(_failures != 0))


## Create a player on the test floor with optional scene-authored movement.
func _spawn_character(style: MoverComponent.ControllerStyle, author_on_scene: bool = true) -> CharacterBody3D:
	var character := CharacterBody3D.new()
	var capsule_collision := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule_collision.position.y = capsule.height * 0.5
	capsule_collision.shape = capsule
	character.add_child(capsule_collision)
	var view_pivot := Node3D.new()
	view_pivot.name = &"View"
	character.add_child(view_pivot)
	if author_on_scene:
		var movement := MoverComponent.new()
		movement.controller_style = style
		character.add_child(movement)
	root.add_child(character)
	return character


## Boot controls through the ordinary standalone API and report errors early.
func _boot(character: CharacterBody3D, view_pivot: Node3D,
		style: MoverComponent.ControllerStyle = MoverComponent.FPS_AIM) -> PlayerControl:
	var input_layout := InputLayout.create().add_move(&"move").add_right_stick(&"stick_aim")
	var profile := PlayerControlProfile.configure(input_layout)
	profile.controller_style = style
	profile.mouse_look_action = &""
	var controller := profile.boot(character, view_pivot)
	if not _expect(controller.is_ready(), controller.get_message()):
		return null
	return controller


## Both styles strafe relative to the view. Only FPS turns the idle body with look input.
func _check_aim(style: MoverComponent.ControllerStyle) -> void:
	var character := _spawn_character(style)
	var view_pivot := character.get_node("View") as Node3D
	var controller := _boot(character, view_pivot)
	if controller == null:
		character.free()
		return
	await _ticks(SETTLE_TICKS)
	_expect(controller.lock_on(null) == ERR_UNAVAILABLE, "Lock-On is only available in the Lock-On style.")
	_stick_look(1.0)
	await _ticks(LOOK_TICKS)
	_stick_look(0.0)
	await _ticks(2)
	var camera_yaw := view_pivot.global_rotation.y
	_expect(absf(camera_yaw) > MIN_LOOK_ANGLE, "Right-stick input turns the idle view.")
	var expected_yaw := camera_yaw if style == MoverComponent.FPS_AIM else 0.0
	_expect(absf(angle_difference(character.global_rotation.y, expected_yaw)) < AIM_TOLERANCE,
		"FPS follows idle aim and Free Orbit keeps idle body facing.")
	_key(KEY_A, true)
	await _ticks(STEER_TICKS)
	_key(KEY_A, false)
	var travel := controller.mover().velocity
	var expected_left := Vector3.LEFT.rotated(Vector3.UP, camera_yaw)
	_expect(travel.length() > MIN_STEER_SPEED and travel.normalized().dot(expected_left) > ALIGNMENT,
		"Left steers in the view frame immediately.")
	_expect(absf(angle_difference(view_pivot.global_rotation.y, camera_yaw)) < AIM_TOLERANCE,
		"Steering preserves the independent view heading.")
	if style == MoverComponent.FREE_ORBIT:
		_expect(absf(character.global_rotation.y) > MIN_LOOK_ANGLE,
			"Free Orbit starts turning toward the requested direction.")
	character.free()
	await _ticks(1)


## Boot Forward Only from an explicit profile and verify forward travel from rest.
func _check_forward_only() -> void:
	var character := _spawn_character(MoverComponent.FORWARD_ONLY, false)
	var view_pivot := character.get_node("View") as Node3D
	var controller := _boot(character, view_pivot, MoverComponent.FORWARD_ONLY)
	if controller == null:
		character.free()
		return
	await _ticks(SETTLE_TICKS)
	_key(KEY_A, true)
	await _ticks(STEER_TICKS)
	_key(KEY_A, false)
	var travel := controller.mover().velocity
	var body_forward := -character.global_basis.z
	_expect(character.global_rotation.y > MIN_LOOK_ANGLE and travel.length() > MIN_STEER_SPEED,
		"Forward Only starts turning left and moving from rest.")
	_expect(travel.normalized().dot(body_forward) > ALIGNMENT and absf(travel.z) > absf(travel.x),
		"Forward Only travels forward into the turn instead of sidestepping.")
	_expect(absf(view_pivot.global_rotation.y) < AIM_TOLERANCE, "Forward Only leaves the view heading alone.")
	character.free()
	await _ticks(1)


## Locking faces the target, strafes in its frame, and returns to view aim after clear or target removal.
func _check_lock_on() -> void:
	var character := _spawn_character(MoverComponent.LOCK_ON)
	var view_pivot := character.get_node("View") as Node3D
	var controller := _boot(character, view_pivot)
	if controller == null:
		character.free()
		return
	await _ticks(SETTLE_TICKS)
	_expect(controller.lock_on(null) == ERR_INVALID_PARAMETER and controller.lock_on(view_pivot) == ERR_INVALID_PARAMETER,
		"Lock-On rejects missing and self targets.")
	var target := Node3D.new()
	target.position = TARGET_POSITION
	root.add_child(target)
	_expect(controller.lock_on(target) == OK, "Lock-On accepts a mounted target.")
	await _ticks(2)
	_expect(absf(angle_difference(character.global_rotation.y, -PI * 0.5)) < AIM_TOLERANCE,
		"Lock-On faces the target even without movement.")
	_key(KEY_A, true)
	await _ticks(STEER_TICKS)
	_key(KEY_A, false)
	var travel := controller.mover().velocity
	_expect(travel.length() > MIN_STEER_SPEED and travel.normalized().dot(Vector3.FORWARD) > ALIGNMENT,
		"Lock-On Left circles in the target frame.")
	_expect(absf(view_pivot.global_rotation.y) < AIM_TOLERANCE, "Lock-On preserves view aim.")
	controller.clear_lock_on()
	await _ticks(2)
	_expect(absf(character.global_rotation.y) < AIM_TOLERANCE, "Clearing Lock-On faces the view again.")
	_expect(controller.lock_on(target) == OK, "Lock-On can be selected again.")
	await _ticks(2)
	target.free()
	await _ticks(2)
	_expect(absf(character.global_rotation.y) < AIM_TOLERANCE, "Removing the target restores view facing.")
	character.free()


## Send a physical key through the same viewport path as real keyboard input.
func _key(code: Key, down: bool) -> void:
	var key_event := InputEventKey.new()
	key_event.keycode = code
	key_event.physical_keycode = code
	key_event.pressed = down
	root.push_input(key_event, true)


## Drive the horizontal look axis using the default right-stick binding.
func _stick_look(strength: float) -> void:
	var stick_event := InputEventJoypadMotion.new()
	stick_event.device = GAMEPAD_DEVICE
	stick_event.axis = JOY_AXIS_RIGHT_X
	stick_event.axis_value = strength
	root.push_input(stick_event, true)


## Wait for the requested number of physics-frame signals.
func _ticks(count: int) -> void:
	for tick_index in count:
		await physics_frame


## Record expectations in both native build variants.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		_failures += 1
		push_error(description)
	return condition
