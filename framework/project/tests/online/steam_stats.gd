# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Verifies Steam stats and leaderboard behavior through standalone client scopes.
extends SceneTree

## Valve's shared AppID, used exclusively for reads in this fixture.
const SPACEWAR := 480
## The success value carried by Steam's app-wide result notifications.
const STEAM_OK := 1

## Counts assertion failures in the current run.
var _failures := 0
## Captures app-wide persistence outcomes in the isolated private fixture.
var _stored_results: Array[int] = []
## Counts popups Steam reported for the configured private test achievement.
var _achievement_events := 0


## Run after the scene root can own the Steam client.
func _initialize() -> void:
	_run.call_deferred()


## Run the selected lane without logging Steam accounts or leaderboard rows.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "missing", "sdk-off", "app"]:
		push_error("Choose the Steam stats fixture mode spacewar, missing, sdk-off, or app.")
		quit(2)
		return
	var mode := arguments[0]
	var app_id := SPACEWAR
	if mode == "app":
		var configured_app := OS.get_environment("CRIMSON_STEAM_STATS_APP_ID")
		app_id = configured_app.to_int()
		if (OS.get_environment("CRIMSON_STEAM_STATS_ALLOW_WRITES") != "1" or not configured_app.is_valid_int()
				or app_id <= 0 or app_id > 4294967295 or app_id == SPACEWAR):
			push_error("Private stats tests require an explicit non-Spacewar AppID and write opt-in.")
			quit(2)
			return
	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.stats() == null and steam.leaderboards() == null, "New Steam client services start detached.")
	var started := steam.start(app_id)
	match mode:
		"spacewar", "app":
			if started == OK:
				if mode == "spacewar":
					await _check_spacewar(steam)
				else:
					await _check_private(steam)
			else:
				_check(false, "Steam client initialization failed. %s" % steam.get_initialization_error())
		"missing":
			_check(started == ERR_CANT_CONNECT, "An SDK-enabled build reports the missing runtime.")
		"sdk-off":
			_check(started == ERR_UNAVAILABLE, "An SDK-free build registers stats and reports unavailable.")
			var inactive_stats := SteamStats.new()
			_check(inactive_stats.store() == ERR_UNAVAILABLE, "SDK-free stat persistence is unavailable.")
			_check(inactive_stats.stat_int("NumGames").error == ERR_UNAVAILABLE
				and inactive_stats.stat_float("AverageSpeed").error == ERR_UNAVAILABLE,
				"SDK-free numeric reads return typed unavailable results.")
			_check(inactive_stats.achievements().is_empty() and inactive_stats.achievement(&"ACH_WIN_ONE_GAME") == null,
				"SDK-free achievement reads return an empty array or null.")
			_check_edit(inactive_stats.edit().set_int("NumGames", 0).submit(), ERR_UNAVAILABLE, 0, -1,
				"SDK-free stat drafts cannot submit.")
			var inactive_boards := SteamLeaderboards.new()
			var inactive: SteamLeaderboardResult = await inactive_boards.find("Quickest Win").wait()
			_check(inactive.error == ERR_UNAVAILABLE and inactive.value == null, "SDK-free leaderboard lookup is unavailable.")
			var inactive_top: Variant = inactive_boards.top("Quickest Win")
			_check(inactive_top is SteamLeaderboardPageResult and inactive_top.error == ERR_UNAVAILABLE,
				"SDK-free top scores return unavailable immediately.")
			var inactive_write: SteamLeaderboardSubmissionResult = await inactive_boards.submit_score("Quickest Win", 1)
			_check(inactive_write.error == ERR_UNAVAILABLE and inactive_write.value == null,
				"SDK-free score submission returns an immediate typed unavailable result.")
	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_STATS_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Read Valve's published Spacewar definitions and exercise local operation ownership.
func _check_spacewar(steam: SteamClient) -> void:
	var stats := steam.stats()
	var leaderboards := steam.leaderboards()
	if not stats.is_available() or not leaderboards.is_available():
		_check(false, "Spacewar provides both standalone User Stats services.")
		return
	var games := stats.stat_int("NumGames")
	var average_speed := stats.stat_float("AverageSpeed")
	var games_played := games.value
	var speed := average_speed.value
	_check(games.ok and average_speed.ok and games_played >= 0 and is_finite(speed),
		"Steam returns typed values for the Spacewar integer and average-rate stats.")
	_check(stats.stat_int("").error == ERR_INVALID_PARAMETER and stats.stat_float("NumGames").error == ERR_DOES_NOT_EXIST,
		"Numeric reads distinguish invalid names and mismatched stat types from valid zero values.")
	_check_edit(stats.edit().submit(), ERR_INVALID_PARAMETER, 0, -1, "An empty draft is rejected before applying edits.")
	var draft := stats.edit().set_int("NumGames", games_played)
	_check_edit(draft.set_int("", 0).submit(), ERR_INVALID_PARAMETER, 0, 1,
		"An invalid second draft edit prevents all edits.")
	_check_edit(draft.set_int("CrimsonFixtureUndefinedStat", 0).submit(), ERR_DOES_NOT_EXIST, 0, 1,
		"A separate draft branch independently preflights published definitions.")
	_check_edit(draft.update_average_rate("AverageSpeed", 1.0, 0.0).submit(), ERR_INVALID_PARAMETER, 0, 1,
		"An average-rate draft rejects a zero duration without applying earlier edits.")
	_check(stats.show_progress("ACH_WIN_ONE_GAME", 0, 2) == ERR_INVALID_PARAMETER,
		"Zero achievement progress is rejected before requesting a popup.")
	var entries := stats.achievements()
	var victory_from_list: SteamAchievement
	_check(entries.get_typed_class_name() == &"SteamAchievement", "The achievement list retains its native element class.")
	for entry in entries:
		var api_name := entry.api_name
		var unlocked := entry.unlocked
		var unlocked_at := entry.unlocked_at_unix
		_check(not api_name.is_empty() and (unlocked_at >= 0 if unlocked else unlocked_at == 0),
			"A typed achievement contains its stable identifier and a valid unlock timestamp.")
		if api_name == &"ACH_WIN_ONE_GAME":
			victory_from_list = entry
	var achievement := stats.achievement(&"ACH_WIN_ONE_GAME")
	var snapshot_before_stop: Array = []
	_check(achievement != null and victory_from_list != null,
		"Spacewar's published achievement is readable by name and in the list. Lookup/list: %s/%s."
		% [achievement != null, victory_from_list != null])
	if achievement != null and victory_from_list != null:
		snapshot_before_stop = _achievement_fields(achievement)
		_check(_achievement_fields(victory_from_list) == snapshot_before_stop,
			"Spacewar's listed achievement and direct lookup expose the same fields.")
		_check(not achievement.display_name.is_empty() and not achievement.hidden,
			"Spacewar's Winner achievement has a usable display name and remains visible when its hidden flag is omitted.")
	_check(stats.achievement(&"CrimsonFixtureUndefinedAchievement") == null, "An unknown achievement returns null.")

	var invalid := leaderboards.find("").result()
	_check(invalid.error == ERR_INVALID_PARAMETER and invalid.value == null, "Leaderboard lookup rejects an empty name.")
	var invalid_top: Variant = leaderboards.top("")
	_check(invalid_top is SteamLeaderboardPageResult and invalid_top.error == ERR_INVALID_PARAMETER,
		"Top scores reject an empty board name immediately.")
	for invalid_count in [0, 101]:
		var invalid_limit: Variant = leaderboards.top("Quickest Win", invalid_count)
		_check(invalid_limit is SteamLeaderboardPageResult and invalid_limit.error == ERR_INVALID_PARAMETER,
			"Top scores reject counts outside the supported range immediately.")
	var top_page: SteamLeaderboardPageResult = await leaderboards.top("Quickest Win", 3)
	_check_page(top_page, 3, "Top scores find the board and download a global page in one await.")
	if top_page.ok:
		var last_rank := 0
		for entry in top_page.value.entries:
			_check(entry.rank > last_rank, "Top scores are sorted in increasing global rank.")
			last_rank = entry.rank
	var missing_top: SteamLeaderboardPageResult = await leaderboards.top("Crimson Fixture Undefined Leaderboard")
	_check(missing_top.error == ERR_DOES_NOT_EXIST and missing_top.value == null,
		"Top scores propagate a missing board without creating one.")
	var score_options := leaderboards.with_details(PackedInt32Array([1]))
	var malformed_replay: SteamLeaderboardSubmissionResult = await score_options.with_replay("").submit_score("Quickest Win", 1)
	_check(malformed_replay.error == ERR_INVALID_PARAMETER and malformed_replay.value == null,
		"An explicit empty replay handle fails before attempting a score write.")
	var missing_write: SteamLeaderboardSubmissionResult = await score_options.submit_score("Crimson Fixture Undefined Leaderboard", 1)
	_check(missing_write.error == ERR_DOES_NOT_EXIST and missing_write.value == null,
		"The original score options branch can resolve a missing board without attempting a score write.")
	var invalid_score: SteamLeaderboardSubmissionResult = await leaderboards.submit_score("Quickest Win", 2147483648)
	_check(invalid_score.error == ERR_INVALID_PARAMETER and invalid_score.value == null,
		"An out-of-range score fails before attempting a score write.")
	var direct_friends: SteamLeaderboardPageResult = await leaderboards.friends("Quickest Win", 2)
	_check_page(direct_friends, 2, "The direct friends read returns a typed page.")
	var query := leaderboards.query("Quickest Win")
	var small_query := query.global_ranks(1, 1)
	var local_user := steam.get_local_steam_id()
	var invalid_users := query.users(PackedStringArray([local_user, local_user])).fetch_operation().result()
	_check(invalid_users != null and invalid_users.error == ERR_INVALID_PARAMETER,
		"Duplicate Steam IDs fail selected-user validation before starting a lookup.")
	var selected: SteamLeaderboardPageResult = await query.users(PackedStringArray([local_user])).fetch()
	_check_page(selected, 1, "A selected-user query returns a typed page for one Steam ID.")
	if selected.ok:
		for entry in selected.value.entries:
			_check(entry.steam_id == local_user, "A selected-user row belongs to the requested Steam ID.")
	var branch_operation := small_query.fetch_operation()
	_check(branch_operation.result() == null and not branch_operation.is_finished(), "A retained query starts pending.")
	var branch_page: SteamLeaderboardPageResult = await branch_operation.wait()
	_check_page(branch_page, 1, "The original global branch remains bounded after creating a selected-user branch.")
	_check(branch_operation.result() == branch_page and branch_operation.is_finished(),
		"A retained query exposes the same typed result after settlement.")
	var around_page: SteamLeaderboardPageResult = await query.around_me(1, 1).fetch()
	_check_page(around_page, 3, "The around-me branch returns at most one row on either side of the center.")
	var abandoned_query := query.around_me(1, 1).fetch_operation()
	_check(abandoned_query.cancel() == OK and abandoned_query.result().error == ERR_SKIP,
		"Cancelling a composed query settles its pending lookup stage.")
	var cancelled := leaderboards.find("Quickest Win")
	_check(cancelled.cancel() == OK, "Cancel one exact leaderboard search before its callback.")
	var cancelled_result: SteamLeaderboardResult = await cancelled.wait()
	_check(cancelled_result.error == ERR_SKIP and cancelled.cancel() == ERR_DOES_NOT_EXIST,
		"A cancelled operation is terminal and reports cancellation consistently.")
	var lookup: SteamLeaderboardResult = await leaderboards.find("Quickest Win").wait()
	var board := lookup.value
	if not lookup.ok or board == null:
		_check(false, "The published Spacewar Quickest Win board is discoverable.")
		return
	var metadata := board.metadata()
	_check(board.is_available() and metadata.ok and metadata.value.name == "Quickest Win"
		and metadata.value.entry_count >= 0, "The returned board exposes its typed published metadata.")
	_check(board.upload(2147483648).result().error == ERR_INVALID_PARAMETER
		and board.attach_ugc("").result().error == ERR_INVALID_PARAMETER,
		"Low-level validation rejects malformed writes before reaching Steam.")
	var abandoned := board.entries(1, 3)
	_check(abandoned.cancel() == OK, "Cancel a submitted leaderboard download locally.")
	var abandoned_result: SteamLeaderboardPageResult = await abandoned.wait()
	_check(abandoned_result.error == ERR_SKIP, "A cancelled download settles before the SDK rows are drained.")
	var global_page: SteamLeaderboardPageResult = await board.entries(1, 3).wait()
	_check_page(global_page, 3, "The global Spacewar page returns detached rows.")
	var friends_page: SteamLeaderboardPageResult = await board.friends(3).wait()
	_check_page(friends_page, 3, "The friends Spacewar page returns a bounded result.")

	var second := SteamClient.new()
	root.add_child(second)
	_check(second.start() == OK, "A second client keeps the same SDK runtime alive.")
	var pending := leaderboards.find("Quickest Win")
	var top_results: Array[SteamLeaderboardPageResult] = []
	var pending_top: Variant = leaderboards.top("Quickest Win")
	if typeof(pending_top) == TYPE_SIGNAL:
		var completion: Signal = pending_top
		completion.connect(func(result: SteamLeaderboardPageResult) -> void: top_results.append(result), CONNECT_ONE_SHOT)
	else:
		_check(false, "A new top scores request exposes its pending completion signal.")
	steam.stop()
	_check(top_results.size() == 1 and top_results[0].error == ERR_SKIP,
		"Stopping one scope settles its pending top scores request exactly once.")
	var stopped: SteamLeaderboardResult = await pending.wait()
	_check(stopped.error == ERR_SKIP, "Stopping one scope cancels its pending lookup. Error %s." % stopped.error)
	_check(not stats.is_available() and not leaderboards.is_available() and not board.is_available(),
		"Stopping one scope retires stats, leaderboards and boards. Availability was %s/%s/%s."
		% [stats.is_available(), leaderboards.is_available(), board.is_available()])
	_check_edit(draft.submit(), ERR_UNAVAILABLE, 0, -1, "A retained draft cannot submit after its Steam scope stops.")
	_check(stats.stat_int("NumGames").error == ERR_UNAVAILABLE and stats.achievements().is_empty(),
		"A stopped scope returns unavailable stat reads and no live achievement entries.")
	_check(achievement != null and not snapshot_before_stop.is_empty()
		and _achievement_fields(achievement) == snapshot_before_stop,
		"A retained achievement snapshot keeps every field after its Steam scope stops.")
	var stopped_top_again: Variant = leaderboards.top("Quickest Win")
	_check(stopped_top_again is SteamLeaderboardPageResult and stopped_top_again.error == ERR_UNAVAILABLE,
		"A stopped scope reports top scores unavailable immediately.")
	var stopped_query := query.fetch_operation().result()
	_check(stopped_query != null and stopped_query.error == ERR_UNAVAILABLE,
		"A retained query cannot attach to a different active Steam scope.")
	var stopped_write: SteamLeaderboardSubmissionResult = await score_options.submit_score("Quickest Win", 1)
	_check(stopped_write.error == ERR_UNAVAILABLE and board.metadata().error == ERR_UNAVAILABLE,
		"Retained score options and board handles cannot revive a stopped Steam scope.")
	if top_page.ok:
		_check_page(top_page, 3, "A detached typed page remains readable after its Steam scope stops.")
	_check(second.is_ready() and second.leaderboards().is_available(), "Stopping one client preserves the other SDK lease.")
	var surviving: SteamLeaderboardPageResult = await second.leaderboards().top("Quickest Win")
	_check_page(surviving, 10, "The surviving scope receives both callbacks with the default top scores count.")
	second.free()


## Persist changes on app-owned test definitions and a test board. Reruns require a locked test achievement.
func _check_private(steam: SteamClient) -> void:
	var int_name := OS.get_environment("CRIMSON_STEAM_STATS_INT")
	var float_name := OS.get_environment("CRIMSON_STEAM_STATS_FLOAT")
	var achievement_name := OS.get_environment("CRIMSON_STEAM_STATS_ACHIEVEMENT")
	var board_name := OS.get_environment("CRIMSON_STEAM_STATS_BOARD")
	var ugc_handle := OS.get_environment("CRIMSON_STEAM_STATS_UGC_HANDLE")
	if int_name.is_empty() or float_name.is_empty() or achievement_name.is_empty() or board_name.is_empty() or ugc_handle.is_empty():
		_check(false, "Supply the private stat names, achievement, board, and Steam shared-file UGC handle.")
		return
	var stats := steam.stats()
	var initial_int := stats.stat_int(int_name)
	var initial_float := stats.stat_float(float_name)
	var initial_achievement := stats.achievement(StringName(achievement_name))
	if not initial_int.ok or not initial_float.ok or initial_achievement == null or initial_achievement.unlocked:
		_check(false, "Use published INT and FLOAT stats and an initially locked achievement on a test account.")
		return
	stats.stats_stored.connect(_record_store_result)
	stats.achievement_stored.connect(_observe_achievement.bind(achievement_name))
	if stats.show_progress(achievement_name, 1, 2) != OK or not await _wait_for_events(1, 1):
		_check(false, "Steam acknowledged the private progress popup and its app-wide result.")
		return
	var changed_int := 1 if initial_int.value == 0 else 0
	var changed_float := 1.0 if initial_float.value == 0.0 else 0.0
	var submitted := stats.edit().set_int(int_name, changed_int).set_float(float_name, changed_float).unlock(achievement_name).submit()
	_check_edit(submitted, OK, 3, -1, "One private draft submitted both stats and the achievement for persistence.")
	if not submitted.ok or not await _wait_for_events(2, 2):
		_check(false, "Steam acknowledged the private stat and achievement submission.")
		return
	var updated_int := stats.stat_int(int_name)
	var updated_float := stats.stat_float(float_name)
	var updated_achievement := stats.achievement(StringName(achievement_name))
	_check(updated_int.ok and updated_float.ok and updated_int.value == changed_int
		and is_equal_approx(updated_float.value, changed_float) and updated_achievement != null and updated_achievement.unlocked,
		"The submitted values remain readable.")

	var leaderboards := steam.leaderboards()
	var lookup: SteamLeaderboardResult = await leaderboards.find_or_create(board_name).wait()
	var board := lookup.value
	if not lookup.ok or board == null:
		_check(false, "The app-owned leaderboard can be created or found.")
		return
	var metadata := board.metadata()
	if not metadata.ok:
		_check(false, "The private board has readable sort and display metadata.")
		return
	var ascending := metadata.value.sort == SteamLeaderboards.ASCENDING
	var worse_score := 18 if ascending else 16
	var better_score := 16 if ascending else 18
	var uploaded: SteamScoreUploadResult = await board.upload(17, PackedInt32Array([3, 7]), true).wait()
	if not uploaded.ok or uploaded.value.score != 17:
		_check(false, "Steam accepted the forced private baseline score.")
		return
	var attached: SteamResult = await board.attach_ugc(ugc_handle).wait()
	_check(attached.ok, "Steam attached the existing shared file through the explicit board action.")
	var kept: SteamLeaderboardSubmissionResult = await leaderboards.submit_score(board_name, worse_score)
	_check(kept.ok and not kept.value.upload.changed and kept.value.replay_state == SteamLeaderboardSubmission.NONE,
		"A direct keep-best submission leaves the better score in place.")
	var replay_options := leaderboards.with_details(PackedInt32Array([3, 7])).with_replay(ugc_handle)
	var skipped: SteamLeaderboardSubmissionResult = await replay_options.submit_score(board_name, worse_score)
	_check(skipped.ok and not skipped.value.upload.changed and skipped.value.replay_state == SteamLeaderboardSubmission.SKIPPED,
		"A replay is skipped when keep-best leaves the old score in place.")
	var submitted_score: SteamLeaderboardSubmissionResult = await replay_options.submit_score(board_name, better_score)
	_check(submitted_score.ok and submitted_score.value.upload.changed
		and submitted_score.value.replay_state == SteamLeaderboardSubmission.ATTACHED,
		"A changed score receives its shared replay through the fluent submission.")
	var page: SteamLeaderboardPageResult = await board.around_user(0, 0).wait()
	_check_page(page, 1, "The signed-in score can be retrieved with its details and shared file.")
	if page.ok:
		var rows := page.value.entries
		_check(rows.size() == 1 and rows[0].score == better_score
			and rows[0].details == PackedInt32Array([3, 7])
			and rows[0].ugc_handle == ugc_handle, "Steam returned the score and the exact attached UGC handle.")
	var rejected_ugc := OS.get_environment("CRIMSON_STEAM_STATS_REJECTED_UGC_HANDLE")
	if not rejected_ugc.is_empty():
		var later_score := better_score - 1 if ascending else better_score + 1
		var partial: SteamLeaderboardSubmissionResult = await replay_options.with_replay(rejected_ugc).submit_score(board_name, later_score)
		_check(not partial.ok and partial.value != null and partial.value.upload.changed
			and partial.value.upload.score == later_score and partial.value.replay_state == SteamLeaderboardSubmission.FAILED,
			"A backend-rejected replay preserves the accepted score receipt for retry.")


## Copy a snapshot's fields for comparisons without logging player values.
func _achievement_fields(achievement: SteamAchievement) -> Array:
	return [achievement.api_name, achievement.display_name, achievement.description, achievement.hidden,
		achievement.unlocked, achievement.unlocked_at_unix]


## Validate the submission outcome without logging any player values.
func _check_edit(result: SteamStatsSubmissionResult, error: int, applied: int, failed_at: int, description: String) -> void:
	var counters := result.value
	_check(result.ok == (error == OK) and result.error == error and counters != null
		and counters.applied == applied and counters.failed_at == failed_at, description)


## Validate the documented detached DTO without emitting account identifiers.
func _check_page(result: SteamLeaderboardPageResult, limit: int, description: String) -> void:
	if not result.ok or result.value == null:
		_check(false, "%s Error %s." % [description, result.error])
		return
	var page := result.value
	var rows := page.entries
	_check(rows.size() <= limit and page.total_count >= rows.size(), description)
	_check(rows.get_typed_class_name() == &"SteamLeaderboardEntry" and rows.is_read_only(),
		"Published leaderboard pages expose a read-only array with a concrete entry type.")
	for row in rows:
		var steam_id := row.steam_id
		var rank := row.rank
		var score := row.score
		var details := row.details
		var ugc := row.ugc_handle
		_check(steam_id.is_valid_int() and rank > 0 and score >= -2147483648 and score <= 2147483647
			and details.size() <= 64 and (ugc.is_empty() or ugc.is_valid_int()),
			"Leaderboard rows preserve typed values and identifiers.")


## Count only the selected achievement's progress and completion notifications.
func _observe_achievement(name: String, _current: int, _maximum: int, expected: String) -> void:
	if name == expected:
		_achievement_events += 1


## Retain the app-wide Steam result without logging user data.
func _record_store_result(steam_result: int) -> void:
	_stored_results.append(steam_result)


## Wait for app-wide Steam outcomes in the isolated private test process.
func _wait_for_events(stored: int, achievements: int) -> bool:
	var deadline := Time.get_ticks_msec() + 8000
	while _stored_results.size() < stored or _achievement_events < achievements:
		if Time.get_ticks_msec() >= deadline:
			return false
		await process_frame
	return _stored_results[stored - 1] == STEAM_OK


## Report a fixture failure without identifying Steam accounts.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
