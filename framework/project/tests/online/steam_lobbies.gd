# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree

var received_chat: Array[String] = []
var expected_sender := ""
var wrong_chat_sender := false
var closed_reasons: Array[int] = []

func _initialize() -> void:
	_run.call_deferred()

## Exercise typed standalone lobby behavior without sending any invitations.
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var mode := args[0] if not args.is_empty() else "missing"
	if mode != "spacewar":
		var unavailable := SteamLobbies.new()
		var operation := unavailable.create()
		if unavailable.is_available() or unavailable.current() != null \
				or operation.result().error != ERR_UNAVAILABLE \
				or unavailable.query().social().fetch_operation().result().error != ERR_UNAVAILABLE:
			_fail("An unavailable scope did not settle through the typed APIs.")
			return
		print("CRIMSON_STEAM_LOBBIES_PASSED ", mode)
		quit()
		return

	var client := SteamClient.boot(root, 480)
	var other := SteamClient.boot(root, 480)
	if client == null or other == null or not client.is_ready() or not other.is_ready():
		_fail("Spacewar requires a signed-in Steam client.")
		return
	var lobbies := client.lobbies()
	var other_lobbies := other.lobbies()
	expected_sender = client.get_local_steam_id()
	if lobbies == null or other_lobbies == null or not lobbies.is_available() \
			or lobbies == other_lobbies or lobbies.current() != null:
		_fail("Steam clients must resolve distinct scoped facades.")
		return
	if lobbies.join(null).result().error != ERR_INVALID_PARAMETER:
		_fail("A null locator did not fail locally.")
		return
	var defaults := SteamLobbyOptions.configure()
	var invalid := defaults.for_players(0)
	if lobbies.create(invalid).result().error != ERR_INVALID_PARAMETER:
		_fail("An invalid capacity did not fail before SDK submission.")
		return
	var operation := lobbies.create(defaults)
	var created: SteamLobbyResult = await operation.wait()
	if not created.ok or created.value == null or not operation.is_finished() \
			or operation.result() != created or operation.cancel() != ERR_DOES_NOT_EXIST:
		_fail("Private lobby create did not settle with one typed membership.")
		return
	var lobby := created.value
	var listing := lobby.listing()
	var members := lobby.members()
	if lobbies.current() != lobby or other_lobbies.current() != null or listing == null \
			or not listing.is_social() or listing.capacity != 4 or listing.players != 1 \
			or members.size() != 1 or not members[0].is_owner or members[0].steam_id != expected_sender:
		_fail("The lobby did not publish the default capacity and exact local roster.")
		return
	if other_lobbies.create().result().error != ERR_BUSY or lobby.invite(expected_sender) != ERR_INVALID_PARAMETER:
		_fail("Process membership was not exclusive or self-invite was not rejected locally.")
		return

	var edit := lobby.edit()
	if edit.tag(&"CrImSoN_Port", "rejected").submit() != ERR_INVALID_PARAMETER \
			or lobby.tag(&"crimson_protocol") != "" \
			or edit.named("Crimson isolated test").tag(&"mode", "self").submit() != OK \
			or lobby.listing().name != "Crimson isolated test" or lobby.tag(&"mode") != "self" \
			or lobby.set_joinable(false) != OK or lobby.set_joinable(true) != OK:
		_fail("Owner edit, reserved keys or explicit joinability did not behave as documented.")
		return

	lobby.chat_received.connect(_chat)
	lobby.closed.connect(func(reason: int) -> void: closed_reasons.append(reason))
	var boundary_message := "é".repeat(512)
	if lobby.send_chat(boundary_message + "x") != ERR_INVALID_PARAMETER or lobby.send_chat(boundary_message) != OK \
			or lobby.send_chat("rate limited") != ERR_BUSY:
		_fail("Local chat validation or rate bound was not enforced.")
		return
	var deadline := Time.get_ticks_msec() + 4000
	while received_chat.is_empty() and Time.get_ticks_msec() < deadline:
		await process_frame
	if received_chat != [boundary_message] or wrong_chat_sender:
		_fail("Steam did not deliver the exact bounded self-chat payload and sender.")
		return

	if lobby.leave() != OK or closed_reasons != [OK] or lobbies.current() != null or not lobby.members().is_empty() \
			or lobby.send_chat("closed") != ERR_UNAVAILABLE or lobby.edit().named("closed").submit() != ERR_UNAVAILABLE:
		_fail("Leaving did not retire membership before notifying observers.")
		return
	var cancelled := other_lobbies.create()
	if cancelled.is_finished() or cancelled.cancel() != OK or cancelled.result().error != ERR_SKIP:
		_fail("Pending standalone create did not retire its caller observation.")
		return
	var replacement: SteamLobbyOperation
	deadline = Time.get_ticks_msec() + 8000
	while Time.get_ticks_msec() < deadline:
		replacement = lobbies.create()
		if not replacement.is_finished() or replacement.result().error != ERR_BUSY:
			break
		await create_timer(0.1).timeout
	if replacement == null:
		_fail("No replacement attempt was made.")
		return
	var recreated: SteamLobbyResult = await replacement.wait()
	if not recreated.ok or recreated.value == null:
		_fail("The cancelled native create did not release the exact account lease.")
		return
	client.stop()
	if lobbies.is_available() or lobbies.current() != null or not recreated.value.members().is_empty() \
			or not other.is_ready() or not other_lobbies.is_available():
		_fail("Stopping one Steam scope did not preserve the other scope.")
		return
	other.stop()
	print("CRIMSON_STEAM_LOBBIES_PASSED ", mode)
	quit()

## Record chat without emitting its contents or the Steam account identifier in logs.
func _chat(member: SteamLobbyMember, message: String) -> void:
	wrong_chat_sender = wrong_chat_sender or member.steam_id != expected_sender
	received_chat.append(message)

## Print a bounded failure with no Steam identifiers.
func _fail(message: String) -> void:
	push_error(message)
	quit(1)
