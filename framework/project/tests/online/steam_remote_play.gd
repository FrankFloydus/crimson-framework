# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercises scoped Remote Play enumeration without opening a panel or sending a Spacewar invite.

const SPACEWAR := 480

var _failures := 0
var _guest_session := 0
var _guest_disconnected := false


## Run after the scene tree can attach a client.
func _initialize() -> void:
	_run.call_deferred()


## Validate the detached, missing-runtime, and signed-in interface paths.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if not args.is_empty() and args[0] == "graphical":
		await _run_graphical(args)
		return
	if args.size() != 1 or args[0] not in ["missing", "sdk-off", "spacewar"]:
		push_error("Choose missing, sdk-off, spacewar, or graphical <authorized non-480 AppID>.")
		quit(2)
		return
	var mode := args[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.remote_play() == null, "Remote Play is absent before startup.")
	_check_detached(SteamRemotePlay.new())
	var started := steam.start(SPACEWAR)
	match mode:
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.remote_play() == null,
				"Missing Steam runtime keeps the Remote Play service unavailable.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.remote_play() == null,
				"SDK-off registers typed Remote Play classes without starting the service.")
		"spacewar":
			_check(started == OK, "Initialize Spacewar. %s" % steam.get_initialization_error())
			if started == OK:
				_check_live(steam)
	steam.free()
	_finish(mode)


## Verify inactive services cannot issue effects or enumerate live input.
func _check_detached(remote: SteamRemotePlay) -> void:
	_check(not remote.is_available() and remote.sessions().is_empty() and remote.poll_input().is_empty(),
		"Detached Remote Play services provide only empty snapshots.")
	_check(remote.open_together_overlay() == ERR_UNAVAILABLE and remote.send_invite("") == ERR_UNAVAILABLE and
		remote.set_separate_input(true) == ERR_UNAVAILABLE, "Detached services reject Remote Play effects.")


## Exercise read-only enumeration, invalid invitations, and exact-scope lifetime.
func _check_live(steam: SteamClient) -> void:
	var remote := steam.remote_play()
	_check(remote != null and remote.is_available(), "Spacewar exposes the Remote Play interface.")
	if remote == null or not remote.is_available():
		return
	var sessions := remote.sessions()
	_check(sessions.is_read_only(), "Remote Play enumeration is a typed read-only snapshot.")
	for session in sessions:
		_check(session.session_id > 0 and session.client_size.x >= 0 and session.client_size.y >= 0,
			"Connected streams expose a valid ID and copied dimensions.")
	_check(remote.poll_input().is_empty() and remote.poll_input(0).is_read_only(),
		"The default merged mode has no separately owned input.")
	for invalid in ["", "invalid", "18446744073709551616", steam.get_local_steam_id()]:
		_check(remote.send_invite(invalid) == ERR_INVALID_PARAMETER, "Reject invalid and local invitation targets.")
	steam.stop()
	_check_detached(remote)
	_check(steam.start(SPACEWAR) == OK and steam.remote_play() != remote,
		"Restart supplies a different Remote Play scope.")


## Wait for a human to invite a controlled device and exercise separate keyboard or mouse input.
func _run_graphical(args: PackedStringArray) -> void:
	if args.size() < 2 or not args[1].is_valid_int() or int(args[1]) <= 0 or int(args[1]) == SPACEWAR or \
			DisplayServer.get_name() == "headless":
		push_error("Graphical verification needs a rendered non-480 app configured for Remote Play Together.")
		quit(2)
		return
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.remote_play() == null or not steam.remote_play().is_available():
		push_error("Steam Remote Play is unavailable in the configured app.")
		quit(2)
		return
	var remote := steam.remote_play()
	remote.session_connected.connect(_assign_guest)
	remote.session_disconnected.connect(_release_guest)
	var label := Label.new()
	label.text = "Invite your test device in Steam, then press a key or move the mouse from the guest."
	root.add_child(label)
	var overlay := steam.overlay()
	var deadline := Time.get_ticks_msec() + 10000
	while overlay != null and not overlay.is_ready() and Time.get_ticks_msec() < deadline:
		await process_frame
	if remote.open_together_overlay() != OK:
		_check(false, "The configured app opened the Remote Play Together pane.")
		_finish("graphical")
		return
	deadline = Time.get_ticks_msec() + 120000
	while _guest_session == 0 and Time.get_ticks_msec() < deadline:
		await process_frame
	if _guest_session == 0 or remote.set_separate_input(true) != OK:
		_check(false, "Steam connected a test guest and enabled separate input.")
		_finish("graphical")
		return
	var sibling := SteamClient.boot(root, int(args[1]))
	_check(sibling != null and sibling.is_ready() and sibling.remote_play().set_separate_input(true) == ERR_BUSY,
		"A sibling cannot claim the process input mode while the first scope owns it.")
	var got_input := false
	deadline = Time.get_ticks_msec() + 30000
	while not got_input and not _guest_disconnected and Time.get_ticks_msec() < deadline:
		for sample in remote.poll_input():
			if sample.session_id == _guest_session and (sample.event is InputEventKey or sample.event is InputEventMouse):
				got_input = true
				break
		await process_frame
	_check(got_input, "A real guest keyboard or mouse sample reached its own seat.")
	label.text = "Disconnect the test guest from Steam Remote Play."
	deadline = Time.get_ticks_msec() + 45000
	while not _guest_disconnected and Time.get_ticks_msec() < deadline:
		await process_frame
	_check(_guest_disconnected, "Disconnect released the test seat.")
	steam.stop()
	if sibling != null and sibling.is_ready():
		var sibling_remote := sibling.remote_play()
		_check(sibling_remote.set_separate_input(true) == OK and sibling_remote.set_separate_input(false) == OK,
			"Stopping the first scope releases separate input for a sibling.")
	_finish("graphical")


## Assign the first observed Together guest to the fixture's local seat.
func _assign_guest(session: SteamRemotePlaySession) -> void:
	if _guest_session == 0 and session.together:
		_guest_session = session.session_id
		print("CRIMSON_STEAM_REMOTE_PLAY_TEST_GUEST_CONNECTED")


## Release only the fixture's selected seat.
func _release_guest(session_id: int) -> void:
	if session_id == _guest_session:
		_guest_disconnected = true


## Accumulate independent verification failures.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)


## Print the marker consumed by the isolated harness.
func _finish(mode: String) -> void:
	if _failures == 0:
		print("CRIMSON_STEAM_REMOTE_PLAY_PASSED %s" % mode)
	quit(int(_failures != 0))
