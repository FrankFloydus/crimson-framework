# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises action map activation through real viewport input and rebinding.

const GAMEPLAY: StringName = &"Gameplay"
const VEHICLE: StringName = &"Vehicle"
const MISSING_MAP: StringName = &"Missing"
const INTERACT: StringName = &"interact"
const DRIVE: StringName = &"drive"
const HORN: StringName = &"horn"
const RADIO: StringName = &"radio"
const HORN_SLOT: StringName = &"horn/key"
const PRIMARY_PAD: int = 0
const SECONDARY_PAD: int = 1
const ANALOG_LEVEL: float = 0.8
const ANALOG_CHANGE: float = 0.9
const ANALOG_NEUTRAL_DRIFT: float = 0.04
const MIN_VECTOR_LEVEL: float = 0.5
const INTERACT_KEY: Key = KEY_E
const ALTERNATE_KEY: Key = KEY_F
const REBOUND_HORN_KEY: Key = KEY_H

## Count failures in either build variant.
var _failures: int = 0
## Count callback dispatches for the two actions sharing a physical key.
var _interact_calls: int = 0
var _horn_calls: int = 0
## Count later callbacks in the Gameplay map during a map transition.
var _radio_calls: int = 0
## Change maps from an action callback to exercise queued dispatch behavior.
var _disable_vehicle_on_interact: bool = false


## Wait until the root accepts children and input.
func _initialize() -> void:
	_run.call_deferred()


## Boot two local scopes from the same asset, then exercise map and binding lifetimes.
func _run() -> void:
	_check_map_validation()
	var input_layout := _create_layout()
	var primary_owner := Node.new()
	var secondary_owner := Node.new()
	root.add_child(primary_owner)
	root.add_child(secondary_owner)
	var primary := Controls.boot(primary_owner, input_layout, PRIMARY_PAD)
	var secondary := Controls.boot(secondary_owner, input_layout, SECONDARY_PAD, false)
	if _expect(primary.is_ready() and secondary.is_ready(), "Both device scopes can boot the shared layout."):
		_expect(primary.is_map_enabled(GAMEPLAY), "Legacy actions belong to enabled Gameplay.")
		_expect(not primary.is_map_enabled(VEHICLE) and not secondary.is_map_enabled(VEHICLE),
			"Additional maps start disabled for every local scope.")
		_expect(not primary.is_map_enabled(MISSING_MAP) and primary.set_map_enabled(MISSING_MAP, true) == ERR_DOES_NOT_EXIST,
			"Unknown maps fail without changing the active maps.")
		_expect(primary.on_press(INTERACT, _on_interact.bind(primary)) == OK
			and primary.on_press(HORN, _on_horn) == OK and primary.on_press(RADIO, _on_radio) == OK,
			"A disabled map's actions can be subscribed to before activation.")
		await _check_button_handoff(primary, secondary)
		await _check_stick_handoff(primary, secondary)
		await _check_callback_transition(primary)
		await _check_rebinding(primary)
	primary_owner.free()
	secondary_owner.free()
	await _check_custom_layout()
	if _failures == 0:
		print("CRIMSON_ACTION_MAPS_TESTS_PASSED")
	quit(int(_failures != 0))


## Give an authored action its own map without changing either action or binding IDs.
func _create_layout() -> InputLayout:
	var input_layout := InputLayout.create().add_button(INTERACT, INTERACT_KEY) \
		.add_button(HORN, INTERACT_KEY).add_button(RADIO, INTERACT_KEY).add_move(DRIVE)
	for action_entry: InputAction in input_layout.actions:
		if action_entry.id == HORN or action_entry.id == DRIVE:
			action_entry.map = VEHICLE
		if action_entry.id == DRIVE:
			action_entry.deadzone = 0.0
	return input_layout


## Maps preserve the layout's existing validation and ID scope.
func _check_map_validation() -> void:
	var duplicate_layout := InputLayout.create().add_button(INTERACT, INTERACT_KEY) \
		.add_button(INTERACT, ALTERNATE_KEY)
	var duplicate_action: InputAction = duplicate_layout.actions[1]
	duplicate_action.map = VEHICLE
	_expect(duplicate_layout.validate() == ERR_ALREADY_EXISTS, "Action IDs remain unique across maps.")
	var blank_layout := InputLayout.create().add_button(INTERACT, INTERACT_KEY)
	var blank_action: InputAction = blank_layout.actions[0]
	blank_action.map = &"  "
	_expect(blank_layout.validate() == ERR_INVALID_PARAMETER, "Reject a map name without visible characters.")


## A held key stays neutral across activation, then drives both enabled maps.
func _check_button_handoff(primary: Controls, secondary: Controls) -> void:
	_key(INTERACT_KEY, true)
	await physics_frame
	_expect(primary.held(INTERACT) and not primary.held(HORN)
		and _interact_calls == 1 and _horn_calls == 0,
		"Only the enabled map sees an input when two actions use the same key.")
	_expect(primary.set_map_enabled(VEHICLE, true) == OK and primary.is_map_enabled(VEHICLE),
		"Enable Vehicle on the primary device scope.")
	_expect(not secondary.is_map_enabled(VEHICLE) and primary.held(INTERACT),
		"Enabling a map preserves other maps' current input and other scopes' choices.")
	_key(INTERACT_KEY, true, true)
	await physics_frame
	_expect(not primary.pressed(HORN) and not primary.held(HORN) and _horn_calls == 0,
		"Activating a map does not trigger a key already held in another map.")
	_key(INTERACT_KEY, false)
	await physics_frame
	_expect(primary.released(INTERACT) and not primary.released(HORN),
		"The ignored press does not produce a release for the newly enabled action.")
	_key(INTERACT_KEY, true)
	await physics_frame
	_expect(primary.pressed(INTERACT) and primary.pressed(HORN)
		and _interact_calls == 2 and _horn_calls == 1,
		"A fresh key press drives both enabled maps.")
	_expect(primary.set_map_enabled(VEHICLE, true) == OK and primary.held(HORN),
		"Enabling an already enabled map preserves its current value.")
	_expect(primary.set_map_enabled(VEHICLE, false) == OK and not primary.held(HORN)
		and not primary.pressed(HORN) and primary.held(INTERACT),
		"Disabling Vehicle clears its published button flags without affecting Gameplay.")
	_key(INTERACT_KEY, false)
	await physics_frame


## A held analog binding remains blocked across activation until it returns to neutral.
func _check_stick_handoff(primary: Controls, secondary: Controls) -> void:
	_stick(PRIMARY_PAD, ANALOG_LEVEL)
	await physics_frame
	_expect(primary.vector(DRIVE).is_zero_approx(), "A disabled map publishes no stick value.")
	_expect(primary.set_map_enabled(VEHICLE, true) == OK, "Re-enable Vehicle while its stick is held.")
	_stick(PRIMARY_PAD, ANALOG_CHANGE)
	await physics_frame
	_expect(primary.vector(DRIVE).is_zero_approx(), "A held stick remains blocked as its deflection changes.")
	_stick(PRIMARY_PAD, -ANALOG_CHANGE)
	await physics_frame
	_expect(primary.vector(DRIVE).is_zero_approx(), "Crossing to the opposite sign still requires a neutral sample.")
	_stick(PRIMARY_PAD, ANALOG_NEUTRAL_DRIFT)
	await physics_frame
	_expect(primary.vector(DRIVE).is_zero_approx(),
		"The neutral sample clears the handoff without publishing stick drift at zero dead zone.")
	_stick(PRIMARY_PAD, ANALOG_LEVEL)
	_stick(SECONDARY_PAD, ANALOG_LEVEL)
	await physics_frame
	_expect(primary.vector(DRIVE).x > MIN_VECTOR_LEVEL and secondary.vector(DRIVE).is_zero_approx(),
		"Returning to neutral admits fresh stick input on the device with Vehicle enabled.")
	_expect(primary.set_map_enabled(VEHICLE, false) == OK and primary.vector(DRIVE).is_zero_approx(),
		"Disabling a map immediately clears its published analog levels.")
	_stick(PRIMARY_PAD, 0.0)
	_stick(SECONDARY_PAD, 0.0)
	await physics_frame


## A map disabled by the first callback must not dispatch its already queued button press.
func _check_callback_transition(primary: Controls) -> void:
	_expect(primary.set_map_enabled(VEHICLE, true) == OK, "Enable Vehicle for the callback test.")
	_disable_vehicle_on_interact = true
	_key(INTERACT_KEY, true)
	await physics_frame
	_disable_vehicle_on_interact = false
	_expect(not primary.is_map_enabled(VEHICLE) and _interact_calls == 3 and _horn_calls == 1
		and not primary.pressed(HORN),
		"Disabling a map inside a callback retires its queued press.")
	_expect(_radio_calls == 3 and primary.pressed(RADIO),
		"Disabling a map preserves the other map's later queued callback.")
	_key(INTERACT_KEY, false)
	await physics_frame


## An existing rebind slot stays editable across map transitions and keeps its override.
func _check_rebinding(primary: Controls) -> void:
	var bindings := Rebinding.open(primary)
	var original_slot := bindings.slot_by_id(HORN_SLOT)
	if not _expect(original_slot != null and original_slot.action == HORN,
		"Look up the authored slot even while its map is disabled."):
		return
	_key.call_deferred(REBOUND_HORN_KEY, true)
	var result: RebindResult = await bindings.rebind(original_slot)
	# Let the Viewport finish dispatching the capture event before sending more input.
	await process_frame
	_key(REBOUND_HORN_KEY, false)
	if not _expect(result.ok and result.slot.id == HORN_SLOT, "Rebinding preserves the original slot ID."):
		return
	_expect(primary.set_map_enabled(VEHICLE, true) == OK, "Activate the map after editing its binding.")
	_key(INTERACT_KEY, true)
	await physics_frame
	_expect(primary.held(INTERACT) and not primary.held(HORN), "The old key no longer drives the rebound action.")
	_key(INTERACT_KEY, false)
	_key(REBOUND_HORN_KEY, true)
	await physics_frame
	_expect(primary.pressed(HORN) and _horn_calls == 2,
		"The new key drives the action and its original callback after activation.")
	_key(REBOUND_HORN_KEY, false)
	await physics_frame


## A layout made only of custom maps starts neutral and can activate its first map.
func _check_custom_layout() -> void:
	var input_layout := InputLayout.create().add_button(HORN, INTERACT_KEY)
	var horn_action: InputAction = input_layout.actions[0]
	horn_action.map = VEHICLE
	var custom_owner := Node.new()
	root.add_child(custom_owner)
	var custom_controls := Controls.boot(custom_owner, input_layout, PRIMARY_PAD)
	if _expect(custom_controls.is_ready(), "A layout without Gameplay actions can boot."):
		_expect(custom_controls.is_map_enabled(GAMEPLAY), "The default Gameplay map is present even when empty.")
		_expect(not custom_controls.is_map_enabled(VEHICLE), "The only custom map still starts disabled.")
		_action(HORN, true)
		await physics_frame
		_expect(not custom_controls.held(HORN), "A custom-only layout starts with neutral input.")
		_expect(custom_controls.set_map_enabled(VEHICLE, true) == OK, "Activate the custom-only map.")
		_action(HORN, true)
		await physics_frame
		_expect(not custom_controls.held(HORN), "A held synthetic action must release across a map transition.")
		_action(HORN, false)
		_action(HORN, true)
		await physics_frame
		_expect(custom_controls.pressed(HORN), "The first enabled map receives input.")
		_action(HORN, false)
		await physics_frame
	custom_owner.free()


## Toggle Vehicle during a queued Gameplay press when requested by the fixture.
func _on_interact(primary: Controls) -> void:
	_interact_calls += 1
	if _disable_vehicle_on_interact:
		_expect(primary.set_map_enabled(VEHICLE, false) == OK, "A callback can disable an active map.")


## Count the dispatched Vehicle presses.
func _on_horn() -> void:
	_horn_calls += 1


## Count Gameplay presses dispatched after the Vehicle callback's place in the queue.
func _on_radio() -> void:
	_radio_calls += 1


## Send a physical keyboard input through Godot's regular viewport path.
func _key(code: Key, down: bool, repeat: bool = false) -> void:
	var key_event := InputEventKey.new()
	key_event.keycode = code
	key_event.physical_keycode = code
	key_event.pressed = down
	key_event.echo = repeat
	root.push_input(key_event, true)


## Send a horizontal left-stick event for one assigned gamepad.
func _stick(device_id: int, level: float) -> void:
	var stick_event := InputEventJoypadMotion.new()
	stick_event.device = device_id
	stick_event.axis = JOY_AXIS_LEFT_X
	stick_event.axis_value = level
	root.push_input(stick_event, true)


## Send the same action event a virtual button can emit.
func _action(action_id: StringName, down: bool) -> void:
	var action_event := InputEventAction.new()
	action_event.action = action_id
	action_event.pressed = down
	root.push_input(action_event, true)


## Record expectations without Debug-only assertions.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		_failures += 1
		push_error(description)
	return condition
