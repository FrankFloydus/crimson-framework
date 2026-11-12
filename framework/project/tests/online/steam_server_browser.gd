# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree

func _initialize() -> void:
	_run.call_deferred()

## Observe a read-only Steam browser query, exact cancellation, and scoped release.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var mode := args[0] if not args.is_empty() else "missing"
	if mode != "spacewar":
		var unavailable := SteamServerBrowser.new()
		if unavailable.is_available() or unavailable.query().fetch_operation().result().error != ERR_UNAVAILABLE \
				or unavailable.inspect(null).result().error != ERR_UNAVAILABLE:
			_fail("An unavailable browser did not return typed local failures.")
			return
		print("CRIMSON_STEAM_SERVER_BROWSER_PASSED ", mode)
		quit()
		return

	var steam := SteamClient.boot(root, 480)
	if steam == null or not steam.is_ready() or steam.server_browser() == null:
		_fail("Spacewar could not start the signed-in Steam server browser.")
		return
	var browser := steam.server_browser()
	var namespace_text := StringName("crimson-browser-%s" % Time.get_ticks_usec())
	var query := browser.query(SteamServer.LAN, namespace_text)
	if not browser.is_available() or query.limit(0).fetch_operation().result().error != ERR_INVALID_PARAMETER \
			or query.fetch_operation(99).result().error != ERR_INVALID_PARAMETER \
			or browser.query(SteamServer.DIRECT).fetch_operation().result().error != ERR_INVALID_PARAMETER \
			or browser.inspect(null).result().error != ERR_INVALID_PARAMETER:
		_fail("Invalid Steam browser options did not fail before network submission.")
		return
	var pending := query.on_map(&"transfer_hall").with_room_for(1).under_latency(1000).limit(1).fetch_operation()
	if pending.is_finished() or query.fetch_operation().result().error != ERR_BUSY \
			or pending.cancel() != OK or pending.result().error != ERR_SKIP:
		_fail("An overlapping query or cancellation did not respect the exact native handle.")
		return
	var discovered: SteamListingsResult = await query.fetch(10000)
	if not discovered.ok or not discovered.value.is_empty():
		_fail("The empty isolated namespace did not complete with an empty typed result. " + str(discovered.error))
		return
	var retiring := browser.query(SteamServer.INTERNET, namespace_text).fetch_operation()
	if retiring.is_finished():
		_fail("The Steam Internet browser did not return a native request.")
		return
	steam.stop()
	if browser.is_available() or retiring.result().error != ERR_SKIP or steam.server_browser() != null:
		_fail("Stopping the client did not retire its native Steam query before completion.")
		return
	print("CRIMSON_STEAM_SERVER_BROWSER_PASSED ", mode)
	quit()

## Print a bounded failure without server or account identifiers.
func _fail(message: String) -> void:
	push_error(message)
	quit(1)
