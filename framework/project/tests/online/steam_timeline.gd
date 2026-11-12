# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies standalone Steam Timeline submission and recording queries.
extends SceneTree

## Valve's shared Steamworks development AppID.
const SPACEWAR := 480

## Counts fixture assertions that failed in this process.
var _failures := 0


## Start after the root can attach a Steam client node.
func _initialize() -> void:
	_run.call_deferred()


## Check a live, missing-runtime, or SDK-disabled Timeline service.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if not arguments.is_empty() and arguments[0] in ["graphical", "markers"]:
		await _check_opt_in(arguments)
		return
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose the Steam Timeline fixture mode spacewar, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.timeline() == null, "Timeline is detached before the client starts.")
	var detached := SteamTimeline.new()
	_check(not detached.is_available() and detached.mark("Test").error == ERR_UNAVAILABLE,
		"An unattached Timeline returns a typed unavailable result.")
	var started := steam.start(SPACEWAR)
	match mode:
		"spacewar":
			if started == OK:
				await _check_read_only(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.timeline() == null,
				"An SDK-enabled client keeps Timeline detached without its runtime.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.timeline() == null,
				"An SDK-free build registers Timeline and leaves the service unavailable.")

	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_TIMELINE_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Verify status and read-only recording lookups without creating Game Recording metadata.
func _check_read_only(steam: SteamClient) -> void:
	var timeline := steam.timeline()
	if timeline == null or not timeline.is_available():
		_check(false, "Spacewar exposes the Timeline interface.")
		return
	_check(timeline.mark("").error == ERR_INVALID_PARAMETER and
		timeline.recording_operation(null).result().error == ERR_INVALID_PARAMETER,
		"Reject invalid marker and recording inputs before native submission.")
	var phase: SteamTimelinePhaseResult = await timeline.phase_recording("crimson_fixture_no_recording")
	_check(phase.ok and phase.value != null and phase.value.recording_ms >= 0,
		"Steam answers a read-only phase recording lookup.")


## Run native marker writes or rendered recording only when the caller explicitly enables them.
func _check_opt_in(args: PackedStringArray) -> void:
	if args.size() < 2 or not args[1].is_valid_int() or int(args[1]) <= 0 or \
			OS.get_environment("CRIMSON_STEAM_TIMELINE_ALLOW_MARKERS") != "1":
		push_error("Use graphical or markers <AppID> with CRIMSON_STEAM_TIMELINE_ALLOW_MARKERS=1.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.timeline() == null or not steam.timeline().is_available():
		push_error("Steam Timeline is unavailable for this app.")
		quit(2)
		return
	if args[0] == "graphical":
		await _check_recording(steam)
	else:
		await _check_markers(steam)
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_TIMELINE_PASSED %s" % args[0])
	quit(int(_failures != 0))


## Observe recorded synthetic footage and the actual Steam overlay activation.
func _check_recording(steam: SteamClient) -> void:
	if DisplayServer.get_name() == "headless":
		_check(false, "The recording fixture requires a rendered Steam client.")
		return
	var overlay := steam.overlay()
	if overlay == null:
		_check(false, "The recording fixture requires the Steam overlay.")
		return
	var color := ColorRect.new()
	color.color = Color.DARK_SLATE_BLUE
	root.add_child(color)
	color.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var status := Label.new()
	status.text = "Enable Steam Game Recording for this test app and begin a recording if needed."
	root.add_child(status)
	var timeline := steam.timeline()
	var phase_id := "crimson_recorded_fixture_%d" % Time.get_ticks_usec()
	_check(timeline.set_game_mode(SteamTimeline.PLAYING) == OK and timeline.start_phase(phase_id) == OK,
		"Start a Game Recording fixture phase.")
	await create_timer(4.0).timeout
	color.color = Color.FOREST_GREEN
	var marker := timeline.start_range("Crimson recorded fixture").value
	if marker == null:
		_check(false, "Create the fixture's Timeline range.")
		return
	await create_timer(4.0).timeout
	_check(timeline.end_range(marker) == OK and timeline.end_phase() == OK, "Close the recorded range and phase.")
	var deadline := Time.get_ticks_msec() + 25000
	var recorded := false
	while not recorded and Time.get_ticks_msec() < deadline:
		var exists: SteamBoolResult = await timeline.recording_exists(marker)
		recorded = exists.ok and exists.value
		if not recorded:
			await create_timer(1.0).timeout
	var phase: SteamTimelinePhaseResult = await timeline.phase_recording(phase_id)
	_check(recorded and phase.ok and phase.value != null and phase.value.recording_ms > 0,
		"Steam confirms actual recording coverage for both the range and the phase.")
	if recorded:
		status.text = "Steam should open Game Recording at the test marker. Close the pane to finish."
		var event_observed := await _observe_recording_overlay(overlay, timeline.open_in_overlay(marker))
		_check(event_observed, "The event recording opened and closed in the Steam overlay.")
		if event_observed and phase.ok and phase.value != null and phase.value.recording_ms > 0:
			status.text = "Steam should now open the recorded phase. Close the pane to finish."
			var phase_observed := await _observe_recording_overlay(overlay, timeline.open_phase_in_overlay(phase_id))
			_check(phase_observed, "The phase recording opened and closed in the Steam overlay.")
	_check(timeline.remove_event(marker) == OK, "Remove the temporary test marker.")


## Observe actual activation and the operator closing one requested Steam recording pane.
func _observe_recording_overlay(overlay: SteamOverlay, submitted: Error) -> bool:
	if submitted != OK:
		return false
	var deadline := Time.get_ticks_msec() + 10000
	while not overlay.is_active() and Time.get_ticks_msec() < deadline:
		await process_frame
	if not overlay.is_active():
		return false
	deadline = Time.get_ticks_msec() + 20000
	while overlay.is_active() and Time.get_ticks_msec() < deadline:
		await process_frame
	return not overlay.is_active()


## Verify real SDK calls and report recording existence separately from marker submission.
func _check_markers(steam: SteamClient) -> void:
	var timeline := steam.timeline()
	if timeline == null or not timeline.is_available():
		_check(false, "Spacewar exposes the Timeline interface.")
		return

	_check(timeline.set_game_mode(SteamTimeline.PLAYING) == OK,
		"Steam accepts the playing Timeline mode.")
	_check(timeline.set_tooltip("Crimson Timeline fixture", -0.25) == OK,
		"Steam accepts a Timeline state description with a past offset.")
	_check(timeline.set_tooltip("Invalid", INF) == ERR_INVALID_PARAMETER,
		"Timeline rejects a nonfinite offset.")
	_check(timeline.mark("").error == ERR_INVALID_PARAMETER
		and timeline.mark_range("Invalid", 601.0).error == ERR_INVALID_PARAMETER,
		"Timeline rejects an empty title and a duration above Steam's supported maximum.")

	var options := TimelineMarkerOptions.new()
	options.priority = 1
	options.offset_seconds = -1.0
	var instant := timeline.mark("Crimson instant fixture").value
	var fixed_range := timeline.mark_range("Crimson fixed range fixture", 1.0, options).value
	var open_range := timeline.start_range("Crimson open range fixture").value
	_check(instant != null and fixed_range != null and open_range != null,
		"Steam returns handles for instant, fixed, and open range markers.")

	var second := SteamClient.new()
	root.add_child(second)
	_check(second.start() == OK, "A second client shares the process Steam runtime.")
	var other := second.timeline()
	if instant != null and fixed_range != null and open_range != null:
		_check(instant.is_valid() and not instant.is_open() and fixed_range.is_valid() and not fixed_range.is_open(),
			"Instant and fixed range events start completed.")
		_check(open_range.is_open() and timeline.recording_operation(open_range).result().error == ERR_INVALID_PARAMETER,
			"An open range must be ended before checking its recording.")
		_check(timeline.update_range(open_range, "Crimson updated range fixture") == OK
			and timeline.end_range(open_range) == OK and not open_range.is_open(),
			"An open range can be updated and ended.")
		if other != null:
			_check(other.remove_event(instant) == ERR_INVALID_PARAMETER,
				"A second facade cannot remove a marker it did not submit.")

		var query := timeline.recording_operation(instant)
		var result: SteamBoolResult = await query.wait()
		var other_result: SteamBoolResult = await timeline.recording_exists(open_range)
		var answered := (result == query.result() and result.ok and other_result.ok)
		_check(answered, "Steam answers both event recording queries.")
		if answered:
			print("CRIMSON_STEAM_TIMELINE_RECORDING instant=%s range=%s" %
				[str(result.value), str(other_result.value)])

		var cancelled := timeline.recording_operation(instant)
		_check(cancelled.cancel() == OK and cancelled.result().error == ERR_SKIP,
			"Local cancellation retires the exact pending recording call.")

	var invalid_phase: SteamTimelinePhaseResult = await timeline.phase_recording("")
	_check(invalid_phase.error == ERR_INVALID_PARAMETER, "A phase lookup requires a stable, valid game ID.")
	var phase_id := "crimson_fixture_%s" % Time.get_ticks_usec()
	_check(timeline.start_phase(phase_id) == OK, "Start a game phase with an explicit ID.")
	if other != null:
		_check(other.start_phase("blocked") == ERR_BUSY and other.end_phase() == ERR_UNCONFIGURED,
			"The exact phase owner prevents replacement and teardown by a sibling.")
	_check(timeline.set_phase_attribute("Map", "Transfer Hall") == OK
		and timeline.add_phase_tag("Fixture", "steam_marker", "Test") == OK
		and timeline.end_phase() == OK, "Set phase attributes and tags before closing the phase.")
	var phase: SteamTimelinePhaseResult = await timeline.phase_recording(phase_id)
	_check(phase.ok and phase.value != null, "Steam returns a typed snapshot for the fixture's phase.")
	if phase.ok and phase.value != null:
		print("CRIMSON_STEAM_PHASE_RECORDING duration_ms=%d clip_count=%d" %
			[phase.value.recording_ms, phase.value.clip_count])
	_check(timeline.start_phase(phase_id + "_cleanup") == OK, "Leave a phase open to exercise scope teardown.")

	var pending: SteamTimelineOperation
	if fixed_range != null:
		pending = timeline.recording_operation(fixed_range)
	for event in [instant, fixed_range, open_range]:
		if event != null:
			_check(timeline.remove_event(event) == OK and not event.is_valid(),
				"Removing a test marker retires its handle.")
	_check(timeline.clear_tooltip() == OK, "Clear the fixture's Timeline state description.")
	var abandoned := timeline.start_range("Crimson scope cleanup fixture").value
	_check(abandoned != null, "Submit an unfinished range for scope cleanup.")
	steam.stop()
	_check(not timeline.is_available() and steam.timeline() == null, "Stopping detaches the Timeline facade.")
	if abandoned != null:
		_check(not abandoned.is_open() and not abandoned.is_valid(), "Stopping retires unfinished ranges.")
	if pending != null:
		_check(pending.is_finished() and pending.result().error == ERR_SKIP,
			"Stopping cancels pending recording queries before leaving Steam active for another client.")
	if other != null:
		_check(other.is_available(), "The second client's Timeline remains available.")
		_check(other.start_phase(phase_id + "_other") == OK and other.end_phase() == OK,
			"Closing one scope releases phase ownership for another scope.")
	second.free()


## Report a failed expectation without logging account or recording details.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
