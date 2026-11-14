# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends Node
## Shows the local player's body from a camera independent of the aim view.

const ControllerTestTarget = preload("res://demo/player/controller_test_target.gd")
const SHOULDER_OFFSET: Vector3 = Vector3(0.48, 0.0, 3.5)
const SHOULDER_HEIGHT: float = 0.35
const CAMERA_TILT: float = deg_to_rad(15.0)
const LOWEST_PITCH: float = deg_to_rad(-65.0)
const HIGHEST_PITCH: float = deg_to_rad(-8.0)
const WALL_MARGIN: float = 0.25
const WALL_FOV_BOOST: float = 18.0
const WALL_TILT: float = deg_to_rad(10.0)
const LEVEL_COLLISION_MASK: int = 1
const CAMERA_ACTION: StringName = &"preview_camera"
const LOCK_ACTION: StringName = &"preview_lock_on"

## The player's existing aim camera continues to drive input and replication.
var _aim_camera: Camera3D
## The visible model inherits the body's simulated heading.
var _visuals: Node3D
## This camera only renders on the machine controlling the body.
var _render_camera: Camera3D
## Local input owns shortcut availability while settings or tools are in front.
var _controls: Controls
## The live local controller accepts the demo's lock-on target.
var _player: PlayerControl
## The current world contains the demo's lock-on landmark.
var _world: Node
## The authored style selects the initial view and makes the test lock available.
var _style: MoverComponent.ControllerStyle = MoverComponent.FPS_AIM
## The test target is selected locally and submitted through the live controller.
var _test_target: ControllerTestTarget = ControllerTestTarget.new()
## Fraction of the camera offset clear of walls, or -1 until the first physics step.
var _clearance: float = -1.0


## Follow the new local player. Other clients' bodies keep their normal visibility.
func attach(aim_camera: Camera3D, visuals: Node3D, controls: Controls, player: PlayerControl,
		world: Node, style: MoverComponent.ControllerStyle) -> void:
	clear()
	_aim_camera = aim_camera
	_visuals = visuals
	_controls = controls
	_player = player
	_world = world
	_style = style
	if _controls.has_action_kind(CAMERA_ACTION, InputAction.BUTTON):
		_controls.on_press(CAMERA_ACTION, _toggle_camera)
	if _style == MoverComponent.LOCK_ON and _controls.has_action_kind(LOCK_ACTION, InputAction.BUTTON):
		_controls.on_press(LOCK_ACTION, _toggle_lock)
	if _style != MoverComponent.FPS_AIM:
		_enable_preview()


## Restore first-person visibility and forget the previous player's references.
func clear() -> void:
	_disable_preview()
	if _controls != null and _controls.is_ready():
		_controls.off_press(CAMERA_ACTION, _toggle_camera)
		_controls.off_press(LOCK_ACTION, _toggle_lock)
	if _player != null and _player.is_ready() and _style == MoverComponent.LOCK_ON:
		_player.clear_lock_on()
	_test_target.clear()
	_aim_camera = null
	_visuals = null
	_controls = null
	_player = null
	_world = null


func _ready() -> void:
	process_priority = 100
	process_physics_priority = 101
	set_process(false)
	set_physics_process(false)


## Switch camera when the layout's Toggle Camera action is pressed.
func _toggle_camera() -> void:
	if _render_camera == null:
		_enable_preview()
	else:
		_disable_preview()


## Select or release the doorway when the layout's Toggle Lock-On action is pressed.
func _toggle_lock() -> void:
	var result: Error = _test_target.toggle(_player, _world)
	if result != OK:
		push_warning("[Crimson] Could not toggle the demo lock-on target: %s" % error_string(result))


## Check the proposed position against level collision during the physics step.
func _physics_process(_delta: float) -> void:
	if not is_instance_valid(_aim_camera) or not _aim_camera.is_inside_tree():
		clear()
		return
	var orbit_basis: Basis = _orbit_basis()
	var shoulder: Vector3 = _aim_camera.global_position - Vector3.UP * SHOULDER_HEIGHT
	var offset: Vector3 = orbit_basis * SHOULDER_OFFSET
	var query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(
			shoulder, shoulder + offset, LEVEL_COLLISION_MASK)
	var hit: Dictionary = _aim_camera.get_world_3d().direct_space_state.intersect_ray(query)
	var activate_preview: bool = _clearance < 0.0
	_clearance = 1.0
	if not hit.is_empty():
		var hit_point: Vector3 = hit["position"]
		_clearance = clampf((shoulder.distance_to(hit_point) - WALL_MARGIN) / offset.length(), 0.0, 1.0)
	if activate_preview:
		_update_camera()
		_render_camera.make_current()
		_visuals.show()


func _process(_delta: float) -> void:
	if not is_instance_valid(_aim_camera) or not _aim_camera.is_inside_tree():
		clear()
		return
	if _clearance >= 0.0:
		_update_camera()


## Add a render camera. Activate it once its first position is clear of level walls.
func _enable_preview() -> void:
	if not is_instance_valid(_aim_camera) or not _aim_camera.is_inside_tree() or not is_instance_valid(_visuals):
		return
	_render_camera = Camera3D.new()
	_render_camera.name = &"ControllerPreviewCamera"
	_render_camera.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
	_render_camera.fov = _aim_camera.fov
	_render_camera.near = _aim_camera.near
	_render_camera.far = _aim_camera.far
	_render_camera.keep_aspect = _aim_camera.keep_aspect
	_render_camera.cull_mask = _aim_camera.cull_mask
	_render_camera.environment = _aim_camera.environment
	_render_camera.attributes = _aim_camera.attributes
	add_child(_render_camera)
	_clearance = -1.0
	set_process(true)
	set_physics_process(true)


## Restore the aim camera before removing its temporary replacement.
func _disable_preview() -> void:
	if _render_camera == null:
		return
	set_process(false)
	set_physics_process(false)
	if is_instance_valid(_visuals):
		_visuals.hide()
	if is_instance_valid(_aim_camera) and _aim_camera.is_inside_tree():
		_aim_camera.make_current()
	remove_child(_render_camera)
	_render_camera.queue_free()
	_render_camera = null


## Smooth the follow position using the existing aim view's presentation transform.
func _update_camera() -> void:
	var orbit_basis: Basis = _orbit_basis()
	var shoulder: Vector3 = _aim_camera.get_global_transform_interpolated().origin - Vector3.UP * SHOULDER_HEIGHT
	var wall_proximity: float = 1.0 - _clearance
	var camera_basis: Basis = orbit_basis * Basis(Vector3.RIGHT, -WALL_TILT * wall_proximity)
	_render_camera.fov = _aim_camera.fov + WALL_FOV_BOOST * wall_proximity
	_render_camera.global_transform = Transform3D(camera_basis, shoulder + orbit_basis * SHOULDER_OFFSET * _clearance)


## Orbit from the aim heading and keep the body framed even at steep aim pitches.
func _orbit_basis() -> Basis:
	var aim_angles: Vector3 = _aim_camera.global_rotation
	var pitch: float = clampf(aim_angles.x - CAMERA_TILT, LOWEST_PITCH, HIGHEST_PITCH)
	return Basis.from_euler(Vector3(pitch, aim_angles.y, 0.0))
