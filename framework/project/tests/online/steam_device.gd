# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies the headless Steam Device API and leaves headset interaction to the GUI fixture.
extends SceneTree

## Valve's shared development AppID.
const SPACEWAR := 480

## Number of failed assertions in this run.
var _failures := 0


## Defer setup until the root can own the Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Verify device queries for the selected runtime lane.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if not arguments.is_empty() and arguments[0] == "graphical":
		await _check_openxr(arguments)
		return
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose the Steam device fixture mode spacewar, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	_check_detached(SteamDevice.new())
	var steam := SteamClient.new()
	root.add_child(steam)
	var started := steam.start(SPACEWAR)
	match mode:
		"spacewar":
			if started == OK:
				_check_live(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.device() == null,
				"A missing runtime leaves Device detached.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.device() == null,
				"The SDK-off build retains Device bindings and has no live scope.")

	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_DEVICE_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Observe a rendered OpenXR session, a real controller action, and SteamVR dashboard submission.
func _check_openxr(args: PackedStringArray) -> void:
	if args.size() < 3 or not args[1].is_valid_int() or int(args[1]) <= 0 or args[2].is_empty() or \
			DisplayServer.get_name() == "headless":
		push_error("Use graphical <AppID> <OpenXR boolean action> with OpenXR enabled and SteamVR selected as the runtime.")
		quit(2)
		return
	var xr := XRServer.find_interface("OpenXR") as OpenXRInterface
	if xr == null or not xr.is_initialized():
		push_error("Enable OpenXR at project startup and connect the test headset.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.device() == null or not steam.device().is_running_in_vr():
		push_error("SteamVR must be running before launching the fixture.")
		quit(2)
		return
	var rig := XROrigin3D.new()
	root.add_child(rig)
	var camera := XRCamera3D.new()
	rig.add_child(camera)
	camera.make_current()
	var controller := XRController3D.new()
	controller.tracker = &"/user/hand/right"
	rig.add_child(controller)
	var marker := MeshInstance3D.new()
	marker.mesh = BoxMesh.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color.CORNFLOWER_BLUE
	marker.material_override = material
	rig.add_child(marker)
	marker.position = Vector3(0.0, 1.45, -3.0)
	root.use_xr = true
	var status := Label.new()
	status.text = "Look for the blue cube in your headset and press the configured right-hand action."
	root.add_child(status)
	var pressed: Array[StringName] = []
	controller.button_pressed.connect(func(action: StringName) -> void: pressed.append(action))
	var deadline := Time.get_ticks_msec() + 30000
	var action := StringName(args[2])
	while Time.get_ticks_msec() < deadline and action not in pressed:
		await process_frame
	_check(xr.get_session_state() == OpenXRInterface.SESSION_STATE_FOCUSED and
		controller.get_has_tracking_data() and action in pressed,
		"Observe a focused OpenXR session and a tracked test action.")
	var stopping: Array[bool] = []
	xr.session_stopping.connect(func() -> void: stopping.append(true), CONNECT_ONE_SHOT)
	_check(steam.device().open_vr_dashboard() == OK, "Submit the SteamVR dashboard request from the running session.")
	status.text = "Confirm the SteamVR dashboard appeared, then stop the test OpenXR session."
	deadline = Time.get_ticks_msec() + 45000
	while stopping.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	_check(not stopping.is_empty(), "Observe OpenXR's native session-stopping signal.")
	root.use_xr = false
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_DEVICE_PASSED graphical")
	quit(int(_failures != 0))


## Verify defaults on a detached or retired Device facade.
func _check_detached(device: SteamDevice) -> void:
	_check(not device.is_available() and device.hardware() == SteamDevice.HARDWARE_UNKNOWN and
		device.recommended_preset() == SteamDevice.PRESET_UNKNOWN,
		"Detached Device reports unknown hardware and preset.")
	_check(not device.is_steam_deck() and not device.is_proton() and not device.is_running_in_vr() and
		not device.is_headset_streaming_enabled() and device.open_vr_dashboard() == ERR_UNAVAILABLE,
		"Detached Device reports inactive platform flags and rejects dashboard requests.")


## Compare signed Device queries against the shared Steam Utils mappings.
func _check_live(steam: SteamClient) -> void:
	var device := steam.device()
	if device == null:
		_check(false, "A signed-in client exposes Device.")
		return
	var utils := steam.utils()
	_check(device.is_available() and utils.is_available(), "Steam Utils is available to the signed-in client.")
	var hardware := device.hardware()
	var preset := device.recommended_preset()
	var legacy_hardware := utils.hardware_type()
	var legacy_preset := utils.hardware_default_config()
	var expected_hardware := int(legacy_hardware)
	if legacy_hardware == SteamUtils.HARDWARE_OTHER:
		expected_hardware = SteamDevice.HARDWARE_UNKNOWN
	var expected_preset := int(legacy_preset)
	if legacy_preset == SteamUtils.CONFIG_OTHER:
		expected_preset = SteamDevice.PRESET_UNKNOWN
	_check(int(hardware) == expected_hardware and int(preset) == expected_preset,
		"Device shares hardware and preset mapping with Utils.")
	_check(device.is_steam_deck() == (hardware == SteamDevice.HARDWARE_STEAM_DECK) and
		device.is_proton() == utils.is_running_under_proton() and
		device.is_running_in_vr() == utils.is_running_in_vr(),
		"Device flags match the shared Steam queries.")
	if not device.is_running_in_vr():
		_check(device.open_vr_dashboard() == ERR_UNAVAILABLE,
			"The desktop path declines the VR dashboard request.")
	print("CRIMSON_STEAM_DEVICE_STATE hardware=%d preset=%d proton=%s vr=%s streaming=%s" %
		[hardware, preset, device.is_proton(), device.is_running_in_vr(), device.is_headset_streaming_enabled()])

	steam.stop()
	_check(steam.device() == null, "The client drops its Device facade on stop.")
	_check_detached(device)
	_check(steam.start(SPACEWAR) == OK and steam.device() != null and steam.device() != device,
		"Restart creates a fresh scoped Device facade.")
	_check_detached(device)


## Report failed assertions using public platform state only.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
