# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Verifies Steam HTML policy and the rendered browser against deterministic loopback content.

const SPACEWAR := 480
const SIZE := Vector2i(480, 270)
const BLUE := Color("2367ae")
const GREEN := Color("2c9f57")
const ORANGE := Color("d17a29")
const OUTSIDE_RED := Color("d92222")
const PAGE_TITLE := "Crimson HTML fixture"

var _failures := 0
var _first := TCPServer.new()
var _other := TCPServer.new()
var _pending_peers: Array[StreamPeerTCP] = []
var _pending_headers: Dictionary[int, String] = {}
var _outside_peers: Array[int] = []
var _outside_requests := 0
var _origin := ""
var _other_origin := ""
var _page: SteamBrowserPage
var _blocked: Array[String] = []
var _last_pixel := Color.TRANSPARENT
var _loaded := false
var _saw_outside_pixels := false


## Run after the scene tree can attach a client.
func _initialize() -> void:
	_run.call_deferred()


## Exercise detached and runtime-unavailable paths in the standard CLI lane.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if not args.is_empty() and args[0] == "graphical":
		await _run_graphical(args)
		return
	if args.size() != 1 or args[0] not in ["missing", "sdk-off", "spacewar"]:
		push_error("Choose missing, sdk-off, spacewar, or graphical <AppID>.")
		quit(2)
		return
	var mode := args[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	var detached := SteamBrowser.new()
	var failed: SteamBrowserPageResult = await detached.open("https://example.com")
	_check(failed.error == ERR_UNAVAILABLE and failed.value == null and steam.browser() == null,
		"An unattached browser scope returns a typed unavailable result.")
	var started := steam.start(SPACEWAR)
	match mode:
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.browser() == null,
				"Missing Steam runtime leaves the browser service unavailable.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.browser() == null,
				"SDK-off registers typed browser bindings without starting the service.")
		"spacewar":
			_check(started == OK, "Initialize Spacewar. %s" % steam.get_initialization_error())
			if started == OK:
				await _check_policy(steam)
	steam.free()
	_finish(mode)


## Verify validation with a signed-in Steam context, before any browser can open.
func _check_policy(steam: SteamClient) -> void:
	var browser := steam.browser()
	_check(browser != null, "The signed-in scope supplies the lazy browser facade.")
	if browser == null:
		return
	for invalid in ["http://127.0.0.1:7777/", "https://trusted.invalid@other.invalid/",
			"https://trusted.invalid:0/", "https://trusted.invalid\\@other.invalid/", "file:///fixture.html"]:
		var rejected: SteamBrowserPageResult = await browser.open(invalid)
		_check(rejected.error == ERR_INVALID_PARAMETER and rejected.value == null,
			"Reject invalid URL and non-allowlisted loopback input.")
	var out_of_bounds: SteamBrowserPageResult = await browser.open("https://example.com", Vector2i(4096, 4096))
	_check(out_of_bounds.error == ERR_INVALID_PARAMETER, "Reject an over-budget surface.")
	var allowed := browser.allow_origins(PackedStringArray(["http://127.0.0.1:7777"]))
	var refused := browser.allow_origins(PackedStringArray(["http://127.0.0.1:7778"]))
	var valid_policy: SteamBrowserPageResult = await allowed.open("http://127.0.0.1:7777/index.html")
	var invalid_policy: SteamBrowserPageResult = await refused.open("http://127.0.0.1:7777/index.html")
	_check(valid_policy.error == ERR_UNAVAILABLE and invalid_policy.error == ERR_INVALID_PARAMETER,
		"Origin drafts remain independent and a valid policy cannot render in headless mode.")
	steam.stop()
	var stale: SteamBrowserPageResult = await allowed.open("http://127.0.0.1:7777/index.html")
	_check(stale.error == ERR_UNAVAILABLE and steam.start(SPACEWAR) == OK and steam.browser() != browser,
		"A retained policy cannot open in a replacement scope.")


## Render and exercise the native Chromium surface with local deterministic content.
func _run_graphical(args: PackedStringArray) -> void:
	if args.size() < 2 or not args[1].is_valid_int() or int(args[1]) <= 0 or DisplayServer.get_name() == "headless":
		push_error("Graphical verification requires a rendered client and its Steam AppID.")
		quit(2)
		return
	if _first.listen(0, "127.0.0.1") != OK or _other.listen(0, "127.0.0.1") != OK:
		push_error("The test page could not start its local listeners.")
		quit(2)
		return
	_origin = "http://127.0.0.1:%s" % _first.get_local_port()
	_other_origin = "http://127.0.0.1:%s" % _other.get_local_port()
	process_frame.connect(_serve_http)
	var steam := SteamClient.boot(root, int(args[1]))
	if steam == null or not steam.is_ready() or steam.browser() == null:
		push_error("Steam HTML fixture needs an initialized client.")
		quit(2)
		return
	var request := steam.browser().allow_origins(PackedStringArray([_origin]))
	var result: SteamBrowserPageResult = await request.open(_origin + "/", SIZE)
	if not result.ok or result.value == null:
		push_error("Steam could not open the fixture browser. %s" % result.message)
		quit(2)
		return
	_page = result.value
	_page.page_finished.connect(_on_finished)
	_page.frame_updated.connect(_on_frame)
	_page.navigation_blocked.connect(_on_blocked)
	var panel := TextureRect.new()
	panel.texture = _page.texture()
	panel.position = Vector2(30, 30)
	panel.size = Vector2(SIZE)
	panel.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	panel.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	root.add_child(panel)
	var first_frame := await _wait_for(func() -> bool: return _loaded and _near_color(BLUE), 12000)
	_check(first_frame, "Steam finished the local page and published its known blue frame.")
	var press := _button(MOUSE_BUTTON_LEFT, Vector2(45, 25), true)
	_check(_page.send_input(press) == ERR_UNAUTHORIZED and _page.set_focused(true) == OK,
		"Input requires explicit page focus.")
	_check(_page.send_input(_button(MOUSE_BUTTON_LEFT, Vector2(-1, -1), true)) == ERR_INVALID_PARAMETER,
		"Reject a pointer press outside the browser.")
	_check(_page.send_input(press) == OK and _page.send_input(_button(MOUSE_BUTTON_LEFT, Vector2(45, 25), false)) == OK,
		"Forward pointer press and release in browser coordinates.")
	_check(await _wait_for(func() -> bool: return _near_color(GREEN), 8000),
		"The click changed the rendered page to green.")
	_check(_page.send_input(_button(MOUSE_BUTTON_WHEEL_DOWN, Vector2(45, 25), true)) == OK,
		"Forward vertical wheel input.")
	_check(await _wait_for(func() -> bool: return _near_color(ORANGE), 8000),
		"The wheel changed the rendered page to orange.")
	_check(_page.set_focused(false) == OK, "Release browser input before removing the panel.")
	var blocked_url := _other_origin + "/outside"
	_check(_page.navigate(_origin + "/redirect") == OK, "Submit the same-origin redirect fixture.")
	var redirected := await _wait_for(func() -> bool: return _blocked.has(blocked_url), 8000)
	_check(redirected and not _saw_outside_pixels and (_outside_requests == 0 or _page.texture() == null),
		"Reject the outside-origin document before display. Blocked signal %s. Outside HTTP requests %s." % [redirected, _outside_requests])
	_page.close()
	_page.close()
	panel.queue_free()
	_check(_page.texture() == null and _page.navigate(_origin + "/") == ERR_UNAVAILABLE,
		"Closing the panel releases the exact native browser.")
	var second: SteamBrowserPageResult = await request.open(_origin + "/", SIZE)
	_check(second.ok and second.value != null, "Open a fresh page after explicit browser teardown.")
	steam.stop()
	_check(second.value != null and second.value.texture() == null,
		"Stopping the scope retires its retained page.")
	_finish("graphical")


## Serve only the fixture paths and count attempted navigation to the other origin.
func _serve_http() -> void:
	var outside := _other.take_connection()
	if outside != null:
		_pending_peers.append(outside)
		_pending_headers[outside.get_instance_id()] = ""
		_outside_peers.append(outside.get_instance_id())
	var new_peer := _first.take_connection()
	if new_peer != null:
		_pending_peers.append(new_peer)
		_pending_headers[new_peer.get_instance_id()] = ""
	for index in range(_pending_peers.size() - 1, -1, -1):
		var peer := _pending_peers[index]
		var key := peer.get_instance_id()
		peer.poll()
		if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
			_pending_peers.remove_at(index)
			_pending_headers.erase(key)
			_outside_peers.erase(key)
			continue
		var available := mini(peer.get_available_bytes(), 4096)
		if available > 0:
			_pending_headers[key] += peer.get_utf8_string(available)
		var header: String = _pending_headers[key]
		if header.contains("\r\n\r\n") or header.length() > 4096:
			if _outside_peers.has(key):
				_outside_requests += 1
				var body := "<!doctype html><title>Outside fixture</title><body style='background:#d92222'>"
				var reply := "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s" % \
					[body.to_utf8_buffer().size(), body]
				peer.put_data(reply.to_utf8_buffer())
				peer.disconnect_from_host()
			else:
				_reply(peer, header)
			_pending_peers.remove_at(index)
			_pending_headers.erase(key)
			_outside_peers.erase(key)


## Send a known visual page or an external-origin redirect, then close the socket.
func _reply(peer: StreamPeerTCP, request: String) -> void:
	var status := "404 Not Found"
	var body := ""
	var extra := ""
	if request.begins_with("GET / HTTP/"):
		status = "200 OK"
		body = ("<!doctype html><title>%s</title><style>body{margin:0;background:#2367ae;min-height:1400px;}"
			+ "button{position:absolute;left:10px;top:10px;width:100px;height:30px;}</style>"
			+ "<button onclick=\"document.body.style.background='#2c9f57'\">Click</button>"
			+ "<script>window.addEventListener('wheel',()=>document.body.style.background='#d17a29');</script>") % PAGE_TITLE
	elif request.begins_with("GET /redirect HTTP/"):
		status = "302 Found"
		extra = "Location: %s/outside\r\n" % _other_origin
	var response := "HTTP/1.1 %s\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Type: text/html\r\nContent-Length: %d\r\n%s\r\n%s" % \
		[status, body.to_utf8_buffer().size(), extra, body]
	peer.put_data(response.to_utf8_buffer())
	peer.disconnect_from_host()


## Build a fixture mouse event already in browser-local pixels.
func _button(index: MouseButton, position: Vector2, pressed: bool) -> InputEventMouseButton:
	var event := InputEventMouseButton.new()
	event.button_index = index
	event.position = position
	event.pressed = pressed
	event.factor = 1.0
	return event


## Record the allowed fixture document's title and final URL.
func _on_finished(url: String, title: String) -> void:
	if url == _origin + "/" and title == PAGE_TITLE:
		_loaded = true


## Sample a corner clear of the fixture button when Steam uploads a frame.
func _on_frame() -> void:
	var texture := _page.texture()
	if texture == null:
		return
	var image := texture.get_image()
	if image != null and image.get_size() == SIZE:
		_last_pixel = image.get_pixel(400, 200)
		_saw_outside_pixels = _saw_outside_pixels or _near_color(OUTSIDE_RED)


## Retain rejected targets for exact redirect verification.
func _on_blocked(url: String) -> void:
	_blocked.append(url)


## Compare Steam's captured texture to the fixture's expected fill.
func _near_color(expected: Color) -> bool:
	return Vector3(_last_pixel.r, _last_pixel.g, _last_pixel.b).distance_to(Vector3(expected.r, expected.g, expected.b)) < 0.08


## Keep the UI and Steam callback pump alive until a fixture condition or its deadline.
func _wait_for(condition: Callable, timeout_msec: int) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_msec
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return condition.call()


## Accumulate independent verification failures.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)


## Print the marker consumed by the isolated harness.
func _finish(mode: String) -> void:
	if _failures == 0:
		print("CRIMSON_STEAM_BROWSER_PASSED %s" % mode)
	quit(int(_failures != 0))
