# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree


## Defer the detached checks until native initialization finishes.
func _initialize() -> void:
	_run.call_deferred()


## Verify typed errors and branch independence without a dedicated SDK or any Steam writes.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var mode := args[0] if args.size() == 1 else ""
	if mode not in ["missing", "sdk-off"]:
		_fail("The detached progression fixture accepts missing or sdk-off.")
		return
	var server := SteamDedicatedServer.new()
	var detached := SteamServerPlayerStats.new()
	if server.stats(0) != null or server.stats(1) != null or server.stats(2) != null \
			or server.stats(2147483648) != null or detached.is_available() or detached.is_loaded() \
			or detached.stat_int(&"Rounds").error != ERR_UNAVAILABLE \
			or detached.stat_float(&"AvgSpeed").error != ERR_UNAVAILABLE \
			or detached.achievement(&"FIRST_WIN").error != ERR_UNAVAILABLE:
		_fail("A detached server or player obtained progression access.")
		return
	var no_store := detached.store()
	if detached.load().result().error != ERR_UNAVAILABLE or no_store.result().error != ERR_UNAVAILABLE \
			or not no_store.is_finished() or no_store.cancel() != ERR_DOES_NOT_EXIST:
		_fail("Detached calls did not settle without issuing Steam work.")
		return
	var branch := detached.edit().set_int(&"Rounds", 3)
	var invalid := branch.set_float(&"Score", INF).submit().result()
	var invalid_duration := branch.update_average_rate(&"AvgSpeed", 2.0, 0.0).submit().result()
	var valid := branch.submit()
	if invalid.error != ERR_INVALID_PARAMETER or invalid.failed_at != 1 or invalid.applied != 0 \
			or invalid_duration.error != ERR_INVALID_PARAMETER or invalid_duration.failed_at != 1 \
			or valid.result().error != ERR_UNAVAILABLE or valid.result().store_submitted \
			or valid.result().steam_result != 0 or branch.submit() != valid:
		_fail("Invalid edits affected another branch or submission did not retain its exact result.")
		return
	var bounded := detached.edit()
	for index in range(33):
		bounded = bounded.set_int(StringName("Round_%d" % index), index)
	if bounded.submit().result().error != ERR_INVALID_PARAMETER or bounded.submit().result().failed_at != 32:
		_fail("The dedicated edit batch exceeded its operation bound.")
		return
	print("CRIMSON_STEAM_SERVER_PROGRESSION_PASSED ", mode)
	quit()


## Fail without issuing Steam actions or printing account data.
func _fail(message: String) -> void:
	push_error(message)
	quit(1)
