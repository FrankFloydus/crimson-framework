# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies Steam Apps metadata, detached typed DLC snapshots, and optional authorized installation.
extends SceneTree

## Valve's shared development AppID. The fixture only reads it.
const SPACEWAR := 480
## Largest AppID representable by Steam.
const APP_ID_MAX := 4294967295
## Bound the opt-in download observation within the fixture's process timeout.
const INSTALL_TIMEOUT_MSEC := 25000

## Assertion failures observed in this process.
var _failures := 0
## Facade retained after its owner stops so stale callbacks can be detected.
var _retired_apps: SteamApps
## Number of DLC callbacks delivered to the retired facade.
var _retired_callbacks := 0
## AppID selected for the explicitly authorized installation test.
var _observed_dlc_id := 0
## Whether the active facade received the selected DLC's installation event.
var _install_observed := false


## Start after the root window can own the Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Choose a read-only or explicitly configured app lane.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or args[0] not in ["missing", "sdk-off", "spacewar", "app"]:
		push_error("Choose missing, sdk-off, spacewar, or app for the Steam Apps fixture.")
		quit(2)
		return
	var mode := args[0]
	var app_id := SPACEWAR
	var dlc_id := SPACEWAR
	if mode == "app":
		app_id = _configured_id("CRIMSON_STEAM_APPS_APP_ID")
		dlc_id = _configured_id("CRIMSON_STEAM_APPS_DLC_ID")
		var expected_owned := OS.get_environment("CRIMSON_STEAM_APPS_EXPECT_OWNED")
		var expected_installed := OS.get_environment("CRIMSON_STEAM_APPS_EXPECT_INSTALLED")
		if (app_id <= 0 or app_id == SPACEWAR or dlc_id <= 0 or dlc_id in [app_id, SPACEWAR] or
				expected_owned not in ["0", "1"] or expected_installed not in ["", "0", "1"]):
			push_error("App mode requires distinct authorized app and DLC IDs and an expected ownership state.")
			quit(2)
			return

	var detached := SteamApps.new()
	_check_detached(detached)
	_check_invalid_ids(detached)
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.apps() == null, "An idle client exposes no Apps facade.")
	var started := steam.start(app_id)
	match mode:
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.apps() == null,
				"The SDK-enabled client exposes no Apps facade without its runtime.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.apps() == null,
				"The SDK-off build registers Apps and reports client startup unavailable.")
		_:
			if started == OK:
				await _check_started(steam, mode, app_id, dlc_id)
			else:
				_check(false, "Steam initialization failed. %s" % steam.get_initialization_error())
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_APPS_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Check every public operation that must reject an inactive facade.
func _check_detached(apps: SteamApps) -> void:
	_check(not apps.is_available() and apps.current_language().is_empty() and
		apps.available_languages().is_empty() and apps.beta_branch().is_empty() and apps.build_id() == 0,
		"An inactive facade has no app metadata.")
	_check(not apps.is_subscribed_to_app(SPACEWAR) and not apps.owns_dlc(SPACEWAR) and
		not apps.is_dlc_installed(SPACEWAR), "An inactive facade reports no local entitlement or install state.")
	var catalog: SteamDlcListResult = apps.dlc()
	_check(not catalog.ok and catalog.error == ERR_UNAVAILABLE and catalog.value.is_empty(),
		"An inactive facade returns a typed catalog error with an empty payload.")
	var progress: SteamDlcProgressResult = apps.download_progress(SPACEWAR)
	_check(not progress.ok and progress.error == ERR_UNAVAILABLE and progress.value == null,
		"An inactive facade returns a typed progress error with no snapshot.")
	_check(apps.install_dlc(SPACEWAR) == ERR_UNAVAILABLE and apps.uninstall_dlc(SPACEWAR) == ERR_UNAVAILABLE,
		"An inactive facade cannot issue installation or removal requests.")


## Reject invalid IDs before any native read or mutation.
func _check_invalid_ids(apps: SteamApps) -> void:
	for invalid_id: int in [-1, 0, APP_ID_MAX + 1]:
		_check(not apps.is_subscribed_to_app(invalid_id) and not apps.owns_dlc(invalid_id) and
			not apps.is_dlc_installed(invalid_id), "Invalid AppIDs report no entitlement or install state.")
		var progress: SteamDlcProgressResult = apps.download_progress(invalid_id)
		_check(progress.error == ERR_INVALID_PARAMETER and progress.value == null,
			"Invalid DLC AppIDs reject progress reads.")
		_check(apps.install_dlc(invalid_id) == ERR_INVALID_PARAMETER and
			apps.uninstall_dlc(invalid_id) == ERR_INVALID_PARAMETER, "Invalid DLC AppIDs reject mutations.")


## Verify signed-in metadata and retire an exact scope while another keeps Steam active.
func _check_started(steam: SteamClient, mode: String, app_id: int, dlc_id: int) -> void:
	var apps := steam.apps()
	_check(steam.get_app_id() == app_id and apps != null and apps.is_available(),
		"The signed-in scope exposes Apps for the expected AppID.")
	if apps == null or not apps.is_available():
		return
	var language := apps.current_language()
	_check(not language.is_empty() and language in apps.available_languages(),
		"Steam supplies the selected app language and its configured language list.")
	_check(apps.is_subscribed_to_app(app_id) and apps.build_id() >= 0,
		"The launched app is locally subscribed and reports a nonnegative build ID.")
	var branch := apps.beta_branch()
	print("CRIMSON_STEAM_APPS_METADATA build=%d beta=%s" % [apps.build_id(), "default" if branch.is_empty() else "named"])
	_check_invalid_ids(apps)
	var entries := _check_catalog(apps)
	var progress := _check_progress(apps, dlc_id)
	if mode == "spacewar" and progress != null:
		_check(not progress.active, "Spacewar has no active optional download for the checked AppID.")

	var sibling := SteamClient.boot(root, app_id)
	_check(sibling != null and sibling.is_ready(), "A sibling client retains the process runtime.")
	if sibling == null:
		return
	_retired_apps = apps
	_retired_apps.dlc_installed.connect(_on_retired_installed)
	steam.stop()
	_check_detached(_retired_apps)
	_check(sibling.apps() != null and sibling.apps().is_available(),
		"The retired Apps facade cannot invalidate a sibling client scope.")
	_check(steam.start(app_id) == OK, "A restarted owner creates a fresh Apps facade.")
	apps = steam.apps()
	_check(apps != null and apps != _retired_apps and not _retired_apps.is_available(),
		"Restart cannot reactivate the previously retained Apps facade.")
	sibling.free()
	if mode == "app" and apps != null and apps.is_available():
		await _check_configured_dlc(apps, entries, dlc_id)


## Check typed row properties and copy isolation for the published catalog.
func _check_catalog(apps: SteamApps) -> Array[SteamDlcInfo]:
	var catalog: SteamDlcListResult = apps.dlc()
	_check(catalog.ok, "The live Apps service can read its DLC catalog. %s" % catalog.message)
	var entries: Array[SteamDlcInfo] = catalog.value
	_check(entries.is_typed() and entries.size() <= 256, "Catalog results use a bounded typed array.")
	for row in entries:
		_check(row.app_id > 0 and row.app_id <= APP_ID_MAX and row.app_id == row.get_app_id() and
			row.display_name == row.get_display_name() and row.available == row.is_available(),
			"A catalog row exposes detached metadata through its typed properties.")
	var copied: Array[SteamDlcInfo] = catalog.value
	copied.append(SteamDlcInfo.new())
	_check(catalog.value.size() == entries.size(), "A caller's array changes do not alter the catalog result.")
	return entries


## Verify zero inactive counts and nonnegative byte counts on a successful query.
func _check_progress(apps: SteamApps, dlc_id: int) -> SteamDlcProgress:
	var result: SteamDlcProgressResult = apps.download_progress(dlc_id)
	_check(result.ok and result.value != null, "Steam supplies a typed progress snapshot. %s" % result.message)
	if result.ok and result.value != null:
		var progress := result.value
		_check(progress.downloaded_bytes >= 0 and progress.total_bytes >= 0 and
			(progress.active or (progress.downloaded_bytes == 0 and progress.total_bytes == 0)),
			"An inactive snapshot has zero counters and all counters fit signed nonnegative integers.")
		return progress
	return null


## Verify configured entitlements and metadata before any opt-in installation.
func _check_configured_dlc(apps: SteamApps, entries: Array[SteamDlcInfo], dlc_id: int) -> void:
	var expected_owned := OS.get_environment("CRIMSON_STEAM_APPS_EXPECT_OWNED") == "1"
	var expected_installed := OS.get_environment("CRIMSON_STEAM_APPS_EXPECT_INSTALLED")
	var owned := apps.owns_dlc(dlc_id)
	var installed := apps.is_dlc_installed(dlc_id)
	_check(owned == expected_owned, "The signed-in account has the configured local DLC ownership state.")
	_check(owned or not installed, "Steam's installed state requires ownership.")
	if not expected_installed.is_empty():
		_check(installed == (expected_installed == "1"), "The configured DLC installation state matches.")
	if OS.has_environment("CRIMSON_STEAM_APPS_EXPECT_BETA"):
		_check(apps.beta_branch() == OS.get_environment("CRIMSON_STEAM_APPS_EXPECT_BETA"),
			"The selected Steam beta branch matches the configured branch.")
	if OS.has_environment("CRIMSON_STEAM_APPS_EXPECT_BUILD_ID"):
		var build := OS.get_environment("CRIMSON_STEAM_APPS_EXPECT_BUILD_ID")
		_check(build.is_valid_int() and str(apps.build_id()) == build, "The active Steam build ID matches.")
	if OS.get_environment("CRIMSON_STEAM_APPS_ALLOW_INSTALL") == "1":
		var listed := entries.any(func(row: SteamDlcInfo) -> bool: return row.app_id == dlc_id)
		if not (listed and expected_owned and owned and not installed and _failures == 0):
			_check(false, "The installation lane requires an owned, absent test DLC in the active app catalog.")
			return
		await _observe_install(apps, dlc_id)


## Issue the authorized install and observe the separate completion event and installed state.
func _observe_install(apps: SteamApps, dlc_id: int) -> void:
	_observed_dlc_id = dlc_id
	apps.dlc_installed.connect(_on_installed)
	var issued := apps.install_dlc(dlc_id)
	_check(issued == OK, "Steam received the explicitly authorized DLC installation request.")
	if issued != OK:
		return
	var deadline := Time.get_ticks_msec() + INSTALL_TIMEOUT_MSEC
	var saw_active_progress := false
	while Time.get_ticks_msec() < deadline:
		var progress := _check_progress(apps, dlc_id)
		if progress == null:
			return
		saw_active_progress = saw_active_progress or progress.active
		if _install_observed and apps.is_dlc_installed(dlc_id):
			break
		await create_timer(0.05).timeout
	_check(_install_observed and apps.is_dlc_installed(dlc_id),
		"Steam confirms DLC installation with the matching callback and installed state.")
	_check(_retired_callbacks == 0, "The retired Apps facade does not observe the new scope's install.")
	print("CRIMSON_STEAM_APPS_INSTALL_STATE active_progress=%s" % str(saw_active_progress))


## Capture only the callback for the selected DLC AppID.
func _on_installed(dlc_id: int) -> void:
	if dlc_id == _observed_dlc_id:
		_install_observed = true


## Record unexpected delivery after the previous scope was retired.
func _on_retired_installed(_dlc_id: int) -> void:
	_retired_callbacks += 1


## Read a canonical positive AppID from the explicitly configured test environment.
func _configured_id(key: String) -> int:
	var raw := OS.get_environment(key)
	if raw.is_valid_int():
		var value := raw.to_int()
		if value > 0 and value <= APP_ID_MAX and str(value) == raw:
			return value
	return 0


## Collect assertion failures without exposing account or publisher credentials.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
