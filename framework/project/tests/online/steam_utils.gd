# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies the standalone Utils facade without changing Steam client settings.
extends SceneTree

## Spacewar is Valve's shared development AppID.
const SPACEWAR := 480

## Counts assertions that failed in this process.
var _failures := 0


## Run after the root can own the Steam client nodes.
func _initialize() -> void:
	_run.call_deferred()


## Exercise live and unavailable Utils through the public Steam client.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off"]:
		push_error("Choose the Steam Utils fixture mode spacewar, missing, or sdk-off.")
		quit(2)
		return

	var mode := arguments[0]
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.utils() == null, "Utils is detached before the client starts.")
	_check(_reports_unavailable(SteamUtils.new()), "A detached Utils facade reports unavailable defaults.")
	var started := steam.start(SPACEWAR)
	match mode:
		"spacewar":
			if started == OK:
				_check_spacewar(steam)
			else:
				_check(false, "Spacewar initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.utils() == null,
				"An SDK-enabled build keeps Utils detached without its runtime.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE and steam.utils() == null,
				"An SDK-free build registers Utils and keeps the client service detached.")

	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_UTILS_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Check live results and confirm a retained facade cannot outlive its client scope.
func _check_spacewar(steam: SteamClient) -> void:
	var utils := steam.utils()
	if utils == null or not utils.is_available():
		_check(false, "The started Spacewar client exposes Utils.")
		return

	var hardware := utils.hardware_type()
	var config := utils.hardware_default_config()
	_check(not utils.ui_language().is_empty(), "Steam returns the client interface language.")
	_check(hardware >= SteamUtils.HARDWARE_NONE and hardware <= SteamUtils.HARDWARE_OTHER,
		"Steam returns a device result distinct from an unavailable query.")
	_check(config >= SteamUtils.CONFIG_NONE and config <= SteamUtils.CONFIG_OTHER,
		"Steam returns a preset result distinct from an unavailable query.")
	print("CRIMSON_STEAM_UTILS_STATE hardware=%d config=%d overlay_ready=%s proton=%s vr=%s" % [
		hardware, config, str(utils.is_overlay_ready()), str(utils.is_running_under_proton()), str(utils.is_running_in_vr())])

	var second := SteamClient.new()
	root.add_child(second)
	_check(second.start() == OK, "A second client holds the same process runtime.")
	steam.stop()
	_check(steam.utils() == null and _reports_unavailable(utils),
		"Stopping the owning client invalidates all reads on its retained Utils facade.")
	var other_utils := second.utils()
	_check(other_utils != null and other_utils.is_available(), "The second client remains ready after the first stops.")
	_check(steam.start() == OK and steam.utils() != utils and _reports_unavailable(utils),
		"Restarting the client creates a fresh Utils facade without reviving its old lease.")
	second.free()


## Check every query's fallback on a detached or invalidated service.
func _reports_unavailable(utils: SteamUtils) -> bool:
	return not utils.is_available() and not utils.is_overlay_ready() and utils.ui_language().is_empty() and \
		utils.hardware_type() == SteamUtils.HARDWARE_UNAVAILABLE and \
		utils.hardware_default_config() == SteamUtils.CONFIG_UNAVAILABLE and \
		not utils.is_running_under_proton() and not utils.is_running_in_vr()


## Report assertion failures without disclosing Steam account data.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
