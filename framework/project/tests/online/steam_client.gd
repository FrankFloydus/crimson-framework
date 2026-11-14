# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies standalone Steam services without constructing a GameRuntime.
extends SceneTree

## Spacewar is Valve's shared development AppID.
const SPACEWAR := 480

## Counts assertions that failed in this process.
var _failures := 0


## Owns the Steam scope for the lifetime of the application node.
class SteamOwner extends Node:
	## The scope booted after this node joins the tree.
	var steam: SteamClient

	## Boot using the AppID from Steam's launch context.
	func _ready() -> void:
		steam = SteamClient.boot(self)


## Run after the tree root is ready to own the Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Exercise the public client lifecycle for the selected build/runtime combination.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "offline", "runtime-mock", "missing", "sdk-off"]:
		push_error("Choose the Steam client fixture mode spacewar, offline, runtime-mock, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	var detached := Node.new()
	_check(SteamClient.boot(detached) == null, "Boot requires a live scene-tree owner.")
	detached.free()
	if DisplayServer.get_name() == "headless":
		_check(SteamClient.restart_through_steam_if_needed(SPACEWAR + 1) == ERR_UNAVAILABLE,
			"Headless fixtures cannot ask Steam to relaunch a shipping client.")
	var owner := SteamOwner.new()
	root.add_child(owner)
	var steam := owner.steam
	if steam == null:
		_check(false, "Boot should attach a client even when the Steam runtime fails.")
		quit(1)
		return
	_check(steam.get_parent() == owner, "Boot attaches the client to the supplied owner.")
	_check(SteamClient.restart_through_steam_if_needed(SPACEWAR) == ERR_INVALID_PARAMETER,
		"The shipping launcher rejects the shared test AppID before calling the SDK.")
	match mode:
		"spacewar":
			if steam.is_ready():
				_check_spacewar(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(not steam.is_ready() and not steam.is_online() and "steam_api64.dll" in steam.get_initialization_error()
				and "Windows error" in steam.get_initialization_error(),
				"An SDK-enabled build reports the Windows failure when loading the packaged Steam DLL.")
		"sdk-off":
			_check(not steam.is_ready() and steam.start(SPACEWAR) == ERR_UNAVAILABLE
				and not steam.get_initialization_error().is_empty(),
				"An SDK-free build registers the Steam types and reports unavailable.")
		"offline":
			var stats := steam.stats()
			_check(steam.is_ready() and not steam.is_online() and stats != null and
				stats.achievement(&"ACH_WIN_ONE_GAME") != null,
				"Start Steam in Offline Mode with cached Spacewar stats before running this fixture.")
		"runtime-mock":
			await _check_mock(steam)

	_check_code_first(mode, steam)

	var apps := steam.apps()
	owner.free()
	_check(not is_instance_valid(steam) and (apps == null or not apps.is_available()),
		"Freeing the owner frees the client and invalidates its retained services.")
	if _failures == 0:
		print("CRIMSON_STEAM_CLIENT_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Exercise the hidden client owner and preserve startup diagnostics through the new actions.
func _check_code_first(mode: String, existing: SteamClient) -> void:
	var test_settings := SteamConfig.spacewar("code-first-fixture")
	var public_settings := test_settings.public_games()
	_check(test_settings.is_valid() and public_settings.is_valid() and not test_settings.discoverable
		and public_settings.discoverable and public_settings.test_group == test_settings.test_group,
		"Steam configuration branches without changing the original lobby policy.")

	var detached := Node.new()
	var failed := Steamworks.boot(detached, test_settings)
	detached.free()
	_check(not failed.boot_result().ok and failed.open_achievements().error == ERR_INVALID_PARAMETER,
		"The code-first action returns the owning scene attachment error.")

	var owner := Node.new()
	root.add_child(owner)
	var invalid := Steamworks.boot(owner, SteamConfig.spacewar(""))
	_check(invalid.boot_result().error == ERR_INVALID_PARAMETER
		and invalid.achievements().unlock("").error == ERR_INVALID_PARAMETER,
		"Invalid code-first settings fail before SDK access and preserve the diagnostic.")
	var steam := Steamworks.boot(owner, test_settings)
	_check(owner.get_child_count() == 0, "The code-first callback client is internal to its scene owner.")
	if mode == "missing" or mode == "sdk-off":
		var result := steam.boot_result()
		var opened := steam.open_achievements()
		_check(not result.ok and not result.message.is_empty() and opened.error == result.error
			and opened.message == result.message and not steam.is_ready(),
			"Unavailable Steam services retain their actual startup failure on the direct action.")
	else:
		_check(steam.boot_result().ok and steam.get_app_id() == SPACEWAR and steam.apps() != existing.apps(),
			"The code-first scope shares the process AppID with independent service handles.")
		var achievements := steam.achievements()
		owner.free()
		_check(not steam.is_ready() and steam.apps() == null and existing.is_ready()
			and achievements.unlock("").error == ERR_UNAVAILABLE,
			"Releasing the owner invalidates only its generation, including retained achievement actions.")
		return
	owner.free()


## Check app metadata, a scoped presence key, and service leases sharing one runtime.
func _check_spacewar(steam: SteamClient) -> void:
	_check(steam.is_ready() and steam.is_online() and steam.get_app_id() == SPACEWAR and steam.get_local_steam_id().is_valid_int(),
		"Steam identifies the signed-in Spacewar client.")
	_check(SteamClient.restart_through_steam_if_needed(SPACEWAR + 1) == ERR_BUSY,
		"The shipping check is refused after Steam client initialization.")
	var apps := steam.apps()
	var friends := steam.friends()
	if not apps.is_available() or not friends.is_available():
		_check(false, "Spacewar exposes both standalone services.")
		return
	_check(apps.notification_session_id().is_empty(), "An ordinary launch has no pending Game Notification session.")

	var language := apps.current_language()
	_check(not language.is_empty() and language in apps.available_languages(),
		"Steam returns the current language and the available language list.")
	_check(apps.is_subscribed_to_app(SPACEWAR) and not apps.is_subscribed_to_app(0) and not apps.is_dlc_installed(0),
		"Steam recognizes the active AppID and rejects invalid app and DLC IDs.")
	var branch: String = apps.beta_branch()
	print("CRIMSON_STEAM_APP_METADATA language=%s beta=%s" % [language, "named" if not branch.is_empty() else "default"])

	var key := "crimson_smoke_%d" % OS.get_process_id()
	var previous := friends.rich_presence(key)
	_check(friends.set_rich_presence("", "invalid") == ERR_INVALID_PARAMETER,
		"Steam rejects empty presence keys before an SDK write.")
	_check(friends.set_rich_presence(key, "roundtrip") == OK and friends.rich_presence(key) == "roundtrip",
		"The temporary key is written to and read from Steam's local presence cache.")
	_check(friends.clear_rich_presence(key) == OK and friends.rich_presence(key).is_empty(),
		"Clearing the temporary key removes only that key.")
	if not previous.is_empty():
		_check(friends.set_rich_presence(key, previous) == OK, "Restore any previous value for the temporary key.")

	var second := SteamClient.new()
	_check(not second.is_ready() and second.apps() == null and second.friends() == null, "A manually created client starts idle.")
	root.add_child(second)
	_check(second.start() == OK and second.get_app_id() == SPACEWAR, "A second scope reuses the selected process AppID.")
	_check(steam.start(SPACEWAR) == OK and steam.apps() == apps, "Repeating the same start is idempotent.")
	_check(steam.start(SPACEWAR + 1) == ERR_ALREADY_IN_USE, "An active scope cannot be reconfigured in place.")
	steam.stop()
	_check(not apps.is_available() and not friends.is_available() and steam.apps() == null and second.is_ready(),
		"Stopping one client invalidates its retained services while the other client stays ready.")
	_check(steam.start() == OK and steam.apps() != apps and not apps.is_available(),
		"Restarting a client gives it fresh service handles.")
	second.free()
	_check(steam.is_ready(), "Removing a second client leaves the first scope active.")


## Prove local startup, typed launch hints, and cross-scope recovery against the isolated native stub.
func _check_mock(steam: SteamClient) -> void:
	if not steam.is_ready() or steam.is_online():
		_check(false, "The offline fixture must load its isolated no-network SDK stub.")
		return
	var old_apps := steam.apps()
	var old_stats := steam.stats()
	var expected_id := "18446744073709551615"
	_check(old_stats.stat_int("NumGames").value == 7 and old_apps.notification_session_id() == expected_id,
		"The cached stat and exact unsigned launch ID work while Steam is offline.")
	var query_events: Array[String] = []
	old_apps.notification_session_requested.connect(func(value: String) -> void: query_events.append(value))
	OS.set_environment("CRIMSON_STEAM_TEST_INVALID_SESSION", "1")
	OS.set_environment("CRIMSON_STEAM_TEST_QUERY_EVENT", "1")
	await process_frame
	await process_frame
	_check(old_apps.notification_session_id().is_empty() and query_events.is_empty(),
		"Malformed notification IDs do not reach game code.")
	OS.unset_environment("CRIMSON_STEAM_TEST_INVALID_SESSION")
	OS.set_environment("CRIMSON_STEAM_TEST_QUERY_EVENT", "1")
	await process_frame
	await process_frame
	_check(query_events == [expected_id], "Warm notification events preserve the exact Steam session ID.")
	OS.set_environment("CRIMSON_STEAM_TEST_QUERY_EVENT", "1")
	await process_frame
	await process_frame
	_check(query_events.size() == 1, "Repeated Steam URL callbacks do not reopen the same notification session.")

	var second := SteamClient.boot(root, SPACEWAR)
	if second == null or not second.is_ready():
		_check(false, "Both scopes must share the offline native pipe.")
		return
	var mismatched := SteamClient.boot(root, SPACEWAR + 1)
	_check(mismatched != null and not mismatched.is_ready()
		and "AppID 480" in mismatched.get_initialization_error() and "AppID 481" in mismatched.get_initialization_error(),
		"Startup diagnostics identify both the active and the requested AppID.")
	if mismatched != null:
		mismatched.free()
	var old_friends := second.friends()
	var old_utils := second.utils()
	var code_first_owner := Node.new()
	root.add_child(code_first_owner)
	var code_first := Steamworks.boot(code_first_owner)
	var code_first_lost: Array[bool] = []
	var code_first_weak: WeakRef = weakref(code_first)
	code_first.runtime_lost.connect(func() -> void:
		var retired := code_first_weak.get_ref() as Steamworks
		code_first_lost.append(retired != null and not retired.is_ready() and retired.stats() == null))
	_check(code_first.boot_result().ok, "The hidden owner joins the same mocked Steam pipe.")
	OS.set_environment("CRIMSON_STEAM_TEST_STORE_REJECT", "1")
	var rejected_unlock := code_first.achievements().unlock("CRIMSON_FIXTURE_ACH")
	OS.unset_environment("CRIMSON_STEAM_TEST_STORE_REJECT")
	_check(not rejected_unlock.ok and rejected_unlock.value.applied == 1
		and code_first.stats().achievement(&"CRIMSON_FIXTURE_ACH").unlocked
		and OS.get_environment("CRIMSON_STEAM_TEST_STORE_REQUESTS") == "1",
		"One unlock submits once and reports a cached edit even when persistence is rejected.")
	var submitted_unlock := code_first.achievements().unlock("CRIMSON_FIXTURE_ACH")
	_check(submitted_unlock.ok and submitted_unlock.value.applied == 1
		and OS.get_environment("CRIMSON_STEAM_TEST_STORE_REQUESTS") == "2",
		"The successful action acknowledges submission of its single edit.")
	var lost: Array[int] = []
	steam.runtime_lost.connect(func() -> void:
		_check(OS.get_environment("CRIMSON_STEAM_TEST_BUFFER_FREED") == "1",
			"The SDK callback buffer is freed before scene code sees the failure.")
		_check(not second.is_ready() and not old_friends.is_available() and not old_utils.is_available(),
			"Every sibling stops exposing SDK functions before any loss signal.")
		_check(steam.start(SPACEWAR) == ERR_CANT_CONNECT,
			"Reentry cannot initialize a fresh native pipe during failure cleanup.")
		lost.append(1))
	second.runtime_lost.connect(func() -> void: lost.append(2))
	OS.unset_environment("CRIMSON_STEAM_TEST_BUFFER_FREED")
	OS.set_environment("CRIMSON_STEAM_TEST_FAIL_IPC", "1")
	await process_frame
	await process_frame
	_check(lost.size() == 2 and not steam.is_ready() and not second.is_ready() and
		OS.get_environment("CRIMSON_STEAM_TEST_SHUTDOWN") == "1" and not old_stats.is_available(),
		"The IPC failure retires both clients and shuts down the native pipe.")
	_check(code_first_lost == [true] and not code_first.open_achievements().ok,
		"The code-first runtime loss signal fires once after the exact generation stops.")
	code_first_owner.free()
	OS.set_environment("CRIMSON_STEAM_TEST_INIT_FAIL", "1")
	var failed_start := steam.start(SPACEWAR)
	OS.unset_environment("CRIMSON_STEAM_TEST_INIT_FAIL")
	var sdk_diagnostic := steam.get_initialization_error()
	_check(failed_start == ERR_CANT_CONNECT and not steam.is_ready() and "The fixture declined startup." in sdk_diagnostic
		and "Check the fixture configuration." in sdk_diagnostic and "\r" not in sdk_diagnostic and "\n" not in sdk_diagnostic,
		"An SDK startup failure preserves its reason as one readable line.")
	OS.set_environment("CRIMSON_STEAM_TEST_ONLINE", "1")
	_check(steam.start(SPACEWAR) == OK and steam.is_online() and steam.apps() != old_apps and
		not old_apps.is_available() and not old_friends.is_available(),
		"Reacquiring creates a fresh scope while all old handles remain invalid.")
	second.free()


## Report the assertion without logging Steam account or presence data.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
