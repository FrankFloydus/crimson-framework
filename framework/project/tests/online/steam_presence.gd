# Copyright Francesco Di Blasi 2026 - All Rights Reserved

## Exercises typed Friends snapshots and opt-in scoped Rich Presence through the public client.
extends SceneTree

## Valve's shared development app, used for read-only verification.
const SPACEWAR := 480

## Counts assertion failures in the current fixture.
var _failures := 0


## Defer until the root can own the test clients.
func _initialize() -> void:
	_run.call_deferred()


## Start the selected fixture lane and leave no active client behind.
func _run() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or arguments[0] not in ["spacewar", "app", "missing", "sdk-off"]:
		push_error("Choose presence fixture mode spacewar, app, missing, or sdk-off.")
		quit(2)
		return
	var mode := arguments[0]
	var app_id := SPACEWAR
	var token := StringName()
	if mode == "app":
		var configured_id := OS.get_environment("CRIMSON_STEAM_PRESENCE_APP_ID")
		app_id = configured_id.to_int()
		var configured_token := OS.get_environment("CRIMSON_STEAM_PRESENCE_DISPLAY_TOKEN")
		if (OS.get_environment("CRIMSON_STEAM_PRESENCE_ALLOW_WRITES") != "1"
				or not configured_id.is_valid_int() or app_id <= 0 or app_id > 4294967295 or app_id == SPACEWAR
				or not configured_token.begins_with("#") or configured_token.length() < 2):
			push_error("App mode requires explicit write opt-in, a non-Spacewar AppID, and a configured display token.")
			quit(2)
			return
		token = StringName(configured_token)

	var steam := SteamClient.new()
	root.add_child(steam)
	_check(steam.friends() == null and steam.presence() == null, "An idle client has no Friends or Presence service.")
	var started := steam.start(app_id)
	match mode:
		"spacewar", "app":
			if started != OK:
				_check(false, "Steam client initialization failed. %s" % steam.get_initialization_error())
			elif mode == "spacewar":
				_check_read_only(steam)
			else:
				_check_status(steam, app_id, token)
		"missing":
			_check(started == ERR_CANT_CONNECT and steam.presence() == null,
				"Missing Steam leaves Presence detached.")
		"sdk-off":
			var detached := SteamPresence.new()
			_check(started == ERR_UNAVAILABLE and not detached.is_available()
				and detached.show(&"#Unavailable") == ERR_UNAVAILABLE and detached.clear() == ERR_UNAVAILABLE,
				"SDK-free Presence reports unavailable.")
			var listed := SteamFriends.new().list()
			_check(listed.error == ERR_UNAVAILABLE and listed.value.is_empty()
				and listed.value.get_typed_class_name() == &"SteamFriend",
				"SDK-free Friends returns a typed empty unavailable result.")
	steam.stop()
	steam.free()
	if _failures == 0:
		print("CRIMSON_STEAM_PRESENCE_PASSED %s" % mode)
	quit(0 if _failures == 0 else 1)


## Read cached friend data without printing names or Steam IDs.
func _check_read_only(steam: SteamClient) -> void:
	var friends := steam.friends()
	var presence := steam.presence()
	_check(friends.is_available() and presence.is_available(), "The signed-in runtime exposes both local services.")
	var listed := friends.list()
	var entries := listed.value
	_check(listed.ok and entries.is_read_only() and entries.get_typed_class_name() == &"SteamFriend",
		"Friends list is a typed read-only snapshot even if there are no friends.")
	for entry in entries:
		var steam_id := entry.steam_id
		var display_name := entry.display_name
		var online := entry.online
		_check(steam_id.is_valid_int() and typeof(display_name) == TYPE_STRING and typeof(online) == TYPE_BOOL,
			"Every inferred friend entry has its typed ID, display name and online state.")
	var original_count := entries.size()
	steam.stop()
	_check(not friends.is_available() and not presence.is_available() and friends.list().error == ERR_UNAVAILABLE,
		"Retained services become unavailable when their scope stops.")
	_check(entries.size() == original_count and entries.is_read_only(),
		"The detached friend snapshot stays readable after stop.")


## Verify accepted status keys and cleanup on an authorized app, then restore the previous cache.
func _check_status(steam: SteamClient, app_id: int, token: StringName) -> void:
	var observer := SteamClient.new()
	root.add_child(observer)
	if observer.start(app_id) != OK:
		_check(false, "A second scope is required to verify runtime ownership.")
		observer.free()
		return

	var managed := steam.presence()
	var competing := observer.presence()
	var friends := observer.friends()
	var substitution_key := "crimson_presence_probe"
	var unrelated_key := "crimson_presence_unrelated"
	var party_id := "crimson_presence_%d" % OS.get_process_id()
	var touched_keys: Array[String] = ["steam_display", "steam_player_group", "steam_player_group_size", substitution_key, unrelated_key]
	var previous: Dictionary[String, String] = {}
	for key in touched_keys:
		previous[key] = friends.rich_presence(key)
	var route_before := friends.rich_presence("connect")
	var replacements: Dictionary[String, String] = {substitution_key: "Transfer Hall"}
	var reserved: Dictionary[String, String] = {"Connect": "not a route"}
	var oversized: Dictionary[String, String] = {substitution_key: "é".repeat(130)}
	var too_many: Dictionary[String, String] = {}
	for index in 30:
		too_many["fixture_%d" % index] = "test"
	_check(managed.show(&"MissingPrefix") == ERR_INVALID_PARAMETER
		and managed.show(token, replacements, party_id, 0) == ERR_INVALID_PARAMETER
		and managed.show(token, reserved) == ERR_INVALID_PARAMETER
		and managed.show(token, oversized) == ERR_INVALID_PARAMETER
		and managed.show(token, too_many) == ERR_INVALID_PARAMETER,
		"Managed status rejects invalid tokens, grouping, reserved keys, UTF-8 byte length and key count.")
	_check(friends.set_rich_presence("CONNECT", "not a route") == ERR_INVALID_PARAMETER
		and friends.clear_rich_presence("CONNECT") == ERR_INVALID_PARAMETER
		and friends.rich_presence("connect") == route_before, "Direct custom-key operations preserve the provider route.")
	_check(friends.set_rich_presence(unrelated_key, "unrelated") == OK, "An unrelated custom key can be set explicitly.")
	var status := managed.show(token, replacements, party_id, 2)
	_check(status == OK, "Steam accepts the configured status and party locally. Error %s." % status)
	if status == OK:
		_check(friends.rich_presence("steam_display") == String(token)
			and friends.rich_presence(substitution_key) == "Transfer Hall"
			and friends.rich_presence("steam_player_group") == party_id
			and friends.rich_presence("steam_player_group_size") == "2",
			"The accepted display, substitution and party keys are visible in the local cache.")
		_check(competing.show(token) == ERR_BUSY and competing.clear() == OK
			and friends.rich_presence("steam_display") == String(token),
			"A competing scope cannot replace or clear the active managed status.")
		_check(managed.show(token) == OK and friends.rich_presence(substitution_key).is_empty()
			and friends.rich_presence("steam_player_group").is_empty()
			and friends.rich_presence("steam_player_group_size").is_empty(),
			"Replacing status removes its omitted substitution and grouping keys.")
		_check(managed.show(token, replacements) == OK, "The owner can publish another managed substitution.")
		_check(friends.set_rich_presence(substitution_key, "Transfer Hall") == OK,
			"A direct custom-key update can take ownership even when the value stays the same.")
		_check(managed.clear() == OK and friends.rich_presence("steam_display").is_empty()
			and friends.rich_presence(substitution_key) == "Transfer Hall"
			and friends.rich_presence(unrelated_key) == "unrelated",
			"Managed clear preserves a direct override and an unrelated custom key.")
		_check(managed.show(token, replacements, party_id, 2) == OK, "The same scope can own status again after clearing.")
	steam.stop()
	if status == OK:
		_check(not managed.is_available() and friends.rich_presence("steam_display").is_empty()
			and friends.rich_presence(substitution_key).is_empty() and friends.rich_presence("steam_player_group").is_empty()
			and friends.rich_presence("steam_player_group_size").is_empty(),
			"Stopping the exact owner removes its remaining managed status and party keys.")
		_check(competing.show(token) == OK and competing.clear() == OK,
			"The surviving scope can claim and release status after the owner stops.")
	_check(friends.rich_presence("connect") == route_before and friends.rich_presence(unrelated_key) == "unrelated",
		"Standalone status leaves the provider route and unrelated key alone.")
	for key in touched_keys:
		var restored := friends.clear_rich_presence(key) if previous[key].is_empty() else friends.set_rich_presence(key, previous[key])
		_check(restored == OK, "The previous test cache value can be restored.")
	observer.free()


## Report an assertion failure without printing friend identities.
func _check(condition: bool, message: String) -> void:
	if not condition:
		_failures += 1
		push_error(message)
