# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Verifies map assets, their runtime modifiers, and existing saved binding IDs.

const ASSET_PATH: String = "user://asset_migration_input_layout.tres"
const PROFILE_PATH: String = "user://asset_migration_controls.json"
const GAMEPLAY: StringName = &"Gameplay"
const VEHICLE: StringName = &"Vehicle"
const MOVE: StringName = &"move"
const FORWARD_SLOT: StringName = &"move/key_up"
const STICK_SLOT: StringName = &"move/stick_up"
const JUMP_SLOT: StringName = &"jump/key"
const STEER: StringName = &"steer"
const STEER_ALTERNATE: StringName = &"steer_alternate"
const STEER_SLOT: StringName = &"steer/left_stick"
const ORDER_FIRST: StringName = &"order_first"
const ORDER_REVERSED: StringName = &"order_reversed"
const DIGITAL_UP: StringName = &"digital_up"
const STICK_SHAPED: StringName = &"stick_shaped"
const STICK_NORMALIZED: StringName = &"stick_normalized"
const POINTER_HORIZONTAL: StringName = &"pointer_horizontal"
const POINTER_VERTICAL: StringName = &"pointer_vertical"
const POINTER_BOTH: StringName = &"pointer_both"
const POINTER_TRANSFORMED: StringName = &"pointer_transformed"
const POINTER_LEGACY: StringName = &"pointer_legacy"
const TEST_GAMEPAD: int = 0
const OTHER_GAMEPAD: int = 1
const REPLACEMENT_KEY: Key = KEY_R
const INVALID_PROFILE_KEY: Key = KEY_T
const REPLACEMENT_AXIS: JoyAxis = JOY_AXIS_TRIGGER_RIGHT
const DEAD_ZONE_LOWER: float = 0.25
const DEAD_ZONE_UPPER: float = 0.75
const BELOW_DEAD_ZONE: float = 0.2
const BETWEEN_THRESHOLDS: float = 0.5
const ABOVE_DEAD_ZONE: float = 0.8

## Number of failed expectations in the current run.
var _failures: int = 0


## Wait for the tree before attaching controls.
func _initialize() -> void:
	_run.call_deferred()


## Check an existing override and a separate authored map in successive scopes.
func _run() -> void:
	await _check_saved_profile()
	await _check_map_asset()
	await _check_modifier_order()
	await _check_pointer_sources()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(PROFILE_PATH))
	DirAccess.remove_absolute(ProjectSettings.globalize_path(ASSET_PATH))
	if _failures == 0:
		print("CRIMSON_INPUT_ASSET_MIGRATION_PASSED")
	quit(int(_failures != 0))


## Build a standalone map with the stable IDs referenced by the old profile.
func _migration_layout() -> InputLayout:
	var fixture_map := InputActionMap.create(GAMEPLAY)
	var swap_axes := InputSwapAxis.new()
	swap_axes.order = InputSwapAxis.YX
	var invert_x := InputInvert.new()
	invert_x.x = true
	invert_x.y = false
	var invert_y := InputInvert.new()
	invert_y.x = false
	invert_y.y = true
	var movement := fixture_map.add_action(MOVE, InputAction.create(InputAction.VECTOR))
	movement.add_binding(_digital_binding(FORWARD_SLOT, KEY_W, [swap_axes, invert_y]))
	movement.add_binding(_digital_binding(&"move/key_left", KEY_A, [invert_x]))
	movement.add_binding(_digital_binding(&"move/key_down", KEY_S, [swap_axes]))
	movement.add_binding(_digital_binding(&"move/key_right", KEY_D, []))
	var stick_event := InputEventJoypadMotion.new()
	stick_event.device = -1
	stick_event.axis = JOY_AXIS_LEFT_Y
	stick_event.axis_value = -1.0
	var signed_binding := InputBinding.new()
	signed_binding.id = STICK_SLOT
	signed_binding.event = stick_event
	signed_binding.modifiers = [swap_axes, invert_y]
	movement.add_binding(signed_binding)
	fixture_map.add_action(&"jump", InputAction.create(InputAction.BUTTON)).add_binding(
		_digital_binding(JUMP_SLOT, KEY_SPACE, []))
	return InputLayout.create().with_map(fixture_map)


## Load a pre-change v1 override into the fixture layout through the public API.
func _check_saved_profile() -> void:
	var fixture_layout := _migration_layout()
	if not _expect(fixture_layout.validate() == OK, "The migration layout validates."):
		return
	_expect(not fixture_layout.maps.is_empty() and fixture_layout.maps[0].id == GAMEPLAY,
		"The layout uses a standalone Gameplay map resource.")
	var scope_owner := Node.new()
	root.add_child(scope_owner)
	var controls := Controls.boot(scope_owner, fixture_layout, TEST_GAMEPAD)
	if not _expect(controls != null and controls.is_ready(), "Boot controls from the migration map."):
		scope_owner.free()
		return

	var rebinding := Rebinding.open(controls)
	var stick_slot := rebinding.slot_by_id(STICK_SLOT)
	var jump_slot := rebinding.slot_by_id(JUMP_SLOT)
	_expect(_slot_uses_key(rebinding, &"move/key_up", KEY_W)
		and _slot_uses_key(rebinding, &"move/key_left", KEY_A)
		and _slot_uses_key(rebinding, &"move/key_down", KEY_S)
		and _slot_uses_key(rebinding, &"move/key_right", KEY_D),
		"All four WASD bindings keep their stable slot IDs.")
	_expect((await _key_movement(controls, KEY_W)).is_equal_approx(Vector2.UP)
		and (await _key_movement(controls, KEY_A)).is_equal_approx(Vector2.LEFT)
		and (await _key_movement(controls, KEY_S)).is_equal_approx(Vector2.DOWN)
		and (await _key_movement(controls, KEY_D)).is_equal_approx(Vector2.RIGHT),
		"All four WASD bindings retain their cardinal directions.")
	_expect(stick_slot != null and stick_slot.action == MOVE,
		"The existing signed gamepad binding keeps its editable slot ID.")
	_expect(jump_slot != null and jump_slot.action == &"jump",
		"The existing jump binding keeps its slot ID.")

	if _write_profile({
		String(FORWARD_SLOT): _key_override(REPLACEMENT_KEY),
		String(STICK_SLOT): {"kind": "axis", "axis": int(REPLACEMENT_AXIS), "sign": 1},
	}):
		_expect(rebinding.load(PROFILE_PATH) == OK, "Load the existing v1 override format.")
		_expect(_slot_uses_key(rebinding, FORWARD_SLOT, REPLACEMENT_KEY),
			"The override addresses the same stable Move Forward slot.")
		_expect(await _key_movement(controls, KEY_W) == Vector2.ZERO,
			"The old physical key no longer moves the player.")
		_expect(await _key_movement(controls, REPLACEMENT_KEY) == Vector2.UP,
			"The saved replacement retains the original forward contribution.")
		_expect((await _axis_movement(controls, JOY_AXIS_LEFT_Y, -1.0)).is_zero_approx(),
			"The old signed axis no longer drives its overridden direction.")
		_expect((await _axis_movement(controls, REPLACEMENT_AXIS, 1.0)).is_equal_approx(Vector2.UP),
			"The saved signed gamepad override keeps the authored direction.")
		if _write_profile({
			String(FORWARD_SLOT): _key_override(INVALID_PROFILE_KEY),
			"unknown/slot": _key_override(KEY_Y),
		}):
			_expect(rebinding.load(PROFILE_PATH) == ERR_DOES_NOT_EXIST,
				"Reject an unknown slot in a complete profile.")
			_expect(_slot_uses_key(rebinding, FORWARD_SLOT, REPLACEMENT_KEY),
				"A rejected profile does not replace an existing valid override.")
		_expect(rebinding.reset_all().ok and _slot_uses_key(rebinding, FORWARD_SLOT, KEY_W),
			"Reset restores the authored event in the new map.")
	scope_owner.free()
	await process_frame


## Round-trip the standalone map through Godot and exercise its mapped stick.
func _check_map_asset() -> void:
	var definition := InputAction.create(InputAction.VECTOR)
	definition.display_name = "Steer"

	var swap_axes := InputSwapAxis.new()
	swap_axes.order = InputSwapAxis.YX
	var dead_zone := InputDeadZone.new()
	dead_zone.mode = InputDeadZone.RADIAL
	dead_zone.lower_threshold = DEAD_ZONE_LOWER
	dead_zone.upper_threshold = DEAD_ZONE_UPPER

	var stick_binding := InputBinding.new()
	stick_binding.id = STEER_SLOT
	stick_binding.stick = InputBinding.LEFT_STICK
	var ordered_modifiers: Array[InputModifier] = [dead_zone, swap_axes]
	stick_binding.modifiers = ordered_modifiers

	var vehicle_map := InputActionMap.create(VEHICLE)
	vehicle_map.display_name = "Vehicle"
	var mapping := vehicle_map.add_action(STEER, definition).add_binding(stick_binding)
	var alternate_mapping := vehicle_map.add_action(STEER_ALTERNATE, definition)
	var input_layout := InputLayout.create().with_map(vehicle_map)
	_expect(mapping.action == alternate_mapping.action and mapping.id != alternate_mapping.id
		and definition.id.is_empty(), "One reusable definition supports distinct map-local action IDs.")
	dead_zone.upper_threshold = DEAD_ZONE_LOWER
	_expect(input_layout.validate() == ERR_INVALID_PARAMETER, "Reject a zero-width dead zone before boot.")
	dead_zone.upper_threshold = DEAD_ZONE_UPPER
	if not _expect(input_layout.validate() == OK and vehicle_map.validate() == OK,
			"A standalone action defines the value kind and the map supplies the action ID."):
		return
	if not _expect(ResourceSaver.save(input_layout, ASSET_PATH) == OK, "Save the authored layout."):
		return
	var loaded_layout := ResourceLoader.load(ASSET_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as InputLayout
	if not _expect(loaded_layout != null and loaded_layout.validate() == OK,
			"Reload the map and its nested resources as a fresh asset."):
		return
	var loaded_binding: InputBinding = loaded_layout.maps[0].mappings[0].bindings[0]
	_expect(loaded_binding.id == STEER_SLOT and loaded_binding.stick == InputBinding.LEFT_STICK
		and loaded_binding.modifiers.size() == 2 and loaded_binding.modifiers[0] is InputDeadZone
		and loaded_binding.modifiers[1] is InputSwapAxis,
		"Round-tripping preserves the binding identity and ordered modifiers.")

	var scope_owner := Node.new()
	root.add_child(scope_owner)
	var controls := Controls.boot(scope_owner, loaded_layout, TEST_GAMEPAD)
	if not _expect(controls != null and controls.is_ready(), "Boot controls from the round-tripped map."):
		scope_owner.free()
		return
	_expect(not controls.is_map_enabled(VEHICLE), "A non-Gameplay map starts disabled.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, ABOVE_DEAD_ZONE)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_Y, -ABOVE_DEAD_ZONE)
	await physics_frame
	_expect(controls.vector(STEER).is_zero_approx(), "The inactive map does not publish stick input.")
	_expect(controls.set_map_enabled(VEHICLE, true) == OK, "Activate the map through the existing scope API.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, 0.0)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, BETWEEN_THRESHOLDS)
	await physics_frame
	_expect(controls.vector(STEER).is_zero_approx(), "Releasing only X cannot unlock a stick with Y still deflected.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, 0.0)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_Y, 0.0)
	await physics_frame
	_stick(OTHER_GAMEPAD, JOY_AXIS_LEFT_X, ABOVE_DEAD_ZONE)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, BELOW_DEAD_ZONE)
	await physics_frame
	_expect(controls.vector(STEER).is_zero_approx(),
		"The dead zone filters local drift and the other gamepad cannot drive this scope.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, BETWEEN_THRESHOLDS)
	await physics_frame
	_expect(controls.vector(STEER).is_equal_approx(Vector2(0.0, 0.5)),
		"The dead zone remaps the magnitude and Swap Axis moves X into Y.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, ABOVE_DEAD_ZONE)
	await physics_frame
	_expect(controls.vector(STEER).is_equal_approx(Vector2.DOWN),
		"The upper dead-zone threshold saturates the remapped stick magnitude.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_Y, -ABOVE_DEAD_ZONE)
	await physics_frame
	var diagonal: Vector2 = controls.vector(STEER)
	_expect(diagonal.x < -0.6 and diagonal.y > 0.6 and is_equal_approx(diagonal.length(), 1.0),
		"Both physical axes contribute to the same whole-stick binding.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, 0.0)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_Y, 0.0)
	_stick(OTHER_GAMEPAD, JOY_AXIS_LEFT_X, 0.0)
	await physics_frame
	_expect(controls.vector(STEER).is_zero_approx(), "Releasing the stick clears both output axes.")
	scope_owner.free()


## Verify source transforms are evaluated in authored order before normalization and aggregation.
func _check_modifier_order() -> void:
	var definition := InputAction.create(InputAction.VECTOR)
	var gameplay_map := InputActionMap.create(GAMEPLAY)
	var swap_axes := InputSwapAxis.new()
	var scale_axes := InputScale.new()
	scale_axes.factor = Vector2(0.25, 0.75)
	var invert_y := InputInvert.new()
	invert_y.x = false
	invert_y.y = true
	var curve := InputResponseCurve.new()
	curve.exponent = 2.0
	var normalize_axes := InputNormalize.new()
	gameplay_map.add_action(ORDER_FIRST, definition).add_binding(
		_digital_binding(&"order_first/key", KEY_U, [scale_axes, swap_axes]))
	gameplay_map.add_action(ORDER_REVERSED, definition).add_binding(
		_digital_binding(&"order_reversed/key", KEY_U, [swap_axes, scale_axes]))
	gameplay_map.add_action(DIGITAL_UP, definition).add_binding(
		_digital_binding(&"digital_up/key", KEY_U, [swap_axes, invert_y]))
	gameplay_map.add_action(STICK_SHAPED, definition).add_binding(
		_whole_stick_binding(&"stick_shaped/left_stick", [curve]))
	gameplay_map.add_action(STICK_NORMALIZED, definition).add_binding(
		_whole_stick_binding(&"stick_normalized/left_stick", [curve, normalize_axes]))
	var scope_owner := Node.new()
	root.add_child(scope_owner)
	var controls := Controls.boot(scope_owner, InputLayout.create().with_map(gameplay_map), TEST_GAMEPAD)
	if not _expect(controls.is_ready(), "Boot the ordered modifiers fixture."):
		scope_owner.free()
		return
	_key(KEY_U, true)
	await physics_frame
	_expect(controls.vector(ORDER_FIRST).is_equal_approx(Vector2(0.0, 0.25))
		and controls.vector(ORDER_REVERSED).is_equal_approx(Vector2(0.0, 0.75)),
		"Asymmetric Scale and Swap Axis produce different results when reordered.")
	_expect(controls.vector(DIGITAL_UP).is_equal_approx(Vector2.UP),
		"A digital key starts on positive X and reaches negative Y through modifiers.")
	_key(KEY_U, false)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, -0.5)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_Y, 0.25)
	await physics_frame
	var shaped := Vector2(-0.25, 0.0625)
	_expect(controls.vector(STICK_SHAPED).is_equal_approx(shaped),
		"The stick response curve shapes both components while retaining their signs.")
	_expect(controls.vector(STICK_NORMALIZED).is_equal_approx(shaped.normalized()),
		"Normalize runs after the curve and preserves the resulting direction.")
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_X, 0.0)
	_stick(TEST_GAMEPAD, JOY_AXIS_LEFT_Y, 0.0)
	await physics_frame
	scope_owner.free()
	await process_frame


## Preserve motion in pixels and isolate the selected mouse axes before applying modifiers.
func _check_pointer_sources() -> void:
	var definition := InputAction.create(InputAction.MOTION)
	var gameplay_map := InputActionMap.create(GAMEPLAY)
	var swap_axes := InputSwapAxis.new()
	var scale_axes := InputScale.new()
	scale_axes.factor = Vector2(0.5, 2.0)
	gameplay_map.add_action(POINTER_HORIZONTAL, definition).add_binding(
		_mouse_binding(&"pointer_horizontal/mouse", InputBinding.MOUSE_X, []))
	gameplay_map.add_action(POINTER_VERTICAL, definition).add_binding(
		_mouse_binding(&"pointer_vertical/mouse", InputBinding.MOUSE_Y, []))
	gameplay_map.add_action(POINTER_BOTH, definition).add_binding(
		_mouse_binding(&"pointer_both/mouse", InputBinding.MOUSE_XY, []))
	gameplay_map.add_action(POINTER_TRANSFORMED, definition).add_binding(
		_mouse_binding(&"pointer_transformed/mouse", InputBinding.MOUSE_Y, [swap_axes, scale_axes]))
	var legacy_mouse := InputBinding.new()
	legacy_mouse.id = &"pointer_legacy/mouse"
	legacy_mouse.event = InputEventMouseMotion.new()
	gameplay_map.add_action(POINTER_LEGACY, definition).add_binding(legacy_mouse)
	var input_layout := InputLayout.create().with_map(gameplay_map)
	if not _expect(ResourceSaver.save(input_layout, ASSET_PATH) == OK, "Save mouse source variants."):
		return
	var loaded_layout := ResourceLoader.load(ASSET_PATH, "", ResourceLoader.CACHE_MODE_IGNORE) as InputLayout
	if not _expect(loaded_layout != null and loaded_layout.validate() == OK,
			"Load the authored mouse source variants back from disk."):
		return
	var authored_bindings: Array[InputActionMapping] = loaded_layout.maps[0].mappings
	_expect(authored_bindings[0].bindings[0].mouse_axes == InputBinding.MOUSE_X
		and authored_bindings[1].bindings[0].mouse_axes == InputBinding.MOUSE_Y
		and authored_bindings[2].bindings[0].mouse_axes == InputBinding.MOUSE_XY,
		"Serializing a layout preserves each mouse source mask.")
	var scope_owner := Node.new()
	root.add_child(scope_owner)
	var controls := Controls.boot(scope_owner, loaded_layout, TEST_GAMEPAD)
	if not _expect(controls.is_ready(), "Boot the mouse-axis bindings."):
		scope_owner.free()
		return
	_pointer_sample(Vector2(6.0, -4.0))
	_pointer_sample(Vector2(10.0, 12.0))
	await physics_frame
	_expect(controls.motion(POINTER_HORIZONTAL).is_equal_approx(Vector2(16.0, 0.0))
		and controls.motion(POINTER_VERTICAL).is_equal_approx(Vector2(0.0, 8.0))
		and controls.motion(POINTER_BOTH).is_equal_approx(Vector2(16.0, 8.0)),
		"Mouse X, Y and XY preserve selected unscaled screen deltas across both samples.")
	_expect(controls.motion(POINTER_TRANSFORMED).is_equal_approx(Vector2(4.0, 0.0)),
		"The motion mask runs before the authored Swap Axis and Scale modifiers.")
	_expect(controls.motion(POINTER_LEGACY).is_equal_approx(Vector2(16.0, 0.0)),
		"A serialized legacy motion event with implicit gain keeps its old X-only behavior.")
	_expect(controls.vector(POINTER_BOTH).is_zero_approx(), "Pixel deltas do not enter normalized vector reads.")
	await physics_frame
	_expect(controls.motion(POINTER_BOTH).is_zero_approx(), "A later tick does not reuse consumed pointer motion.")
	scope_owner.free()


## Make a keyboard source with the authored transform chain.
func _digital_binding(slot_id: StringName, code: Key, modifiers: Array[InputModifier]) -> InputBinding:
	var key_event := InputEventKey.new()
	key_event.physical_keycode = code
	var binding := InputBinding.new()
	binding.id = slot_id
	binding.event = key_event
	binding.modifiers = modifiers
	return binding


## Make a paired stick source with the authored transform chain.
func _whole_stick_binding(slot_id: StringName, modifiers: Array[InputModifier]) -> InputBinding:
	var binding := InputBinding.new()
	binding.id = slot_id
	binding.stick = InputBinding.LEFT_STICK
	binding.modifiers = modifiers
	return binding


## Make a relative motion source with the requested physical axes.
func _mouse_binding(slot_id: StringName, axes: InputBinding.MouseAxes, modifiers: Array[InputModifier]) -> InputBinding:
	var binding := InputBinding.new()
	binding.id = slot_id
	binding.set_mouse_motion(axes)
	binding.modifiers = modifiers
	return binding


## Emit screen and scaled-relative deltas that differ, through the real Viewport path.
func _pointer_sample(screen_delta: Vector2) -> void:
	var pointer_event := InputEventMouseMotion.new()
	pointer_event.relative = screen_delta * 0.5
	pointer_event.screen_relative = screen_delta
	root.push_input(pointer_event, true)


## Publish a physical key for one physics sample, then release it.
func _key_movement(controls: Controls, code: Key) -> Vector2:
	_key(code, true)
	await physics_frame
	var value: Vector2 = controls.vector(MOVE)
	_key(code, false)
	await physics_frame
	return value


## Publish one physical axis sample and return that action's value before releasing it.
func _axis_movement(controls: Controls, axis_id: JoyAxis, level: float) -> Vector2:
	_stick(TEST_GAMEPAD, axis_id, level)
	await physics_frame
	var value: Vector2 = controls.vector(MOVE)
	_stick(TEST_GAMEPAD, axis_id, 0.0)
	await physics_frame
	return value


## Check the slot snapshot without retaining its mutable input event.
func _slot_uses_key(rebinding: Rebinding, slot_id: StringName, code: Key) -> bool:
	var slot := rebinding.slot_by_id(slot_id)
	return slot != null and slot.event is InputEventKey and slot.event.physical_keycode == code


## Emit a physical key through the same Viewport input path as gameplay.
func _key(code: Key, down: bool) -> void:
	var key_event := InputEventKey.new()
	key_event.physical_keycode = code
	key_event.pressed = down
	root.push_input(key_event, true)


## Emit one axis sample for a specific gamepad through the Viewport.
func _stick(device_id: int, axis_id: JoyAxis, level: float) -> void:
	var stick_event := InputEventJoypadMotion.new()
	stick_event.device = device_id
	stick_event.axis = axis_id
	stick_event.axis_value = level
	root.push_input(stick_event, true)


## Reproduce the old bounded profile's keyboard entry.
func _key_override(code: Key) -> Dictionary:
	return {"kind": "key", "physical": int(code), "logical": 0, "flags": 0}


## Store the fixture profile inside the isolated Godot user directory.
func _write_profile(overrides: Dictionary) -> bool:
	var profile_file := FileAccess.open(PROFILE_PATH, FileAccess.WRITE)
	if not _expect(profile_file != null, "Open the isolated profile fixture."):
		return false
	profile_file.store_string(JSON.stringify({"version": 1, "overrides": overrides}))
	profile_file.close()
	return true


## Record errors in both debug and release builds.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		_failures += 1
		push_error(description)
	return condition
