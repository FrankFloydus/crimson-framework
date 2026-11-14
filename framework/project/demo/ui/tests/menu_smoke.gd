# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Drive the production menu script with fixture-owned controls and game resources.

const MainMenu := preload("res://demo/ui/main_menu.gd")
const ScreenNavigator := preload("res://demo/ui/screen_navigator.gd")
const FixtureWorld: PackedScene = preload("res://tests/game_flow/fixtures/start_world.tscn")
const FixturePlayer: PackedScene = preload("res://tests/game_flow/fixtures/start_player.tscn")
const WAIT_MS := 5000

var _failures: Array[String] = []


## Construct only the controls required by MainMenu's script, independent of the playable scene.
static func menu_fixture() -> MainMenu:
	var menu := MainMenu.new()
	var navigator := ScreenNavigator.new()
	mount(menu, menu, navigator, &"Screens")
	var main_page := VBoxContainer.new()
	mount(menu, navigator, main_page, &"Main")
	var solo := Button.new()
	var multiplayer := Button.new()
	mount(menu, main_page, solo, &"Solo")
	mount(menu, main_page, multiplayer, &"OpenMultiplayer")
	solo.focus_neighbor_bottom = NodePath("../OpenMultiplayer")
	var network_page := VBoxContainer.new()
	mount(menu, navigator, network_page, &"Multiplayer")
	var host := Button.new()
	mount(menu, network_page, host, &"Host")
	mount(menu, network_page, Button.new(), &"OpenJoin")
	mount(menu, network_page, Button.new(), &"MultiplayerBack")
	mount(menu, network_page, Label.new(), &"Port")
	var join_page := VBoxContainer.new()
	mount(menu, navigator, join_page, &"Join")
	mount(menu, join_page, LineEdit.new(), &"Address")
	mount(menu, join_page, Button.new(), &"Connect")
	mount(menu, join_page, Button.new(), &"JoinBack")
	mount(menu, menu, Button.new(), &"Cancel")
	mount(menu, menu, Label.new(), &"Status")
	return menu


## Give a fresh node the unique name expected by the production script.
static func mount(owner_node: Node, parent_node: Node, child_node: Node, control_id: StringName) -> void:
	child_node.name = control_id
	parent_node.add_child(child_node)
	child_node.owner = owner_node
	child_node.unique_name_in_owner = true


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var owner_node := Node.new()
	root.add_child(owner_node)
	var menu := menu_fixture()
	owner_node.add_child(menu)
	var reports: Array[bool] = []
	menu.launch_finished.connect(func(ok: bool, _message: String) -> void: reports.append(ok))
	menu.launch_host("127.0.0.1")
	_expect(reports == [false] and menu.visible, "Launching without a game reports one failure and leaves the menu available.")

	var game := Game.configure(FixtureWorld, FixturePlayer).boot(owner_node)
	menu.bind_game(game)
	var solo := menu.get_node(^"%Solo") as Button
	var multiplayer := menu.get_node(^"%OpenMultiplayer") as Button
	var meetup := menu.get_node(^"%Multiplayer") as Control
	var host := menu.get_node(^"%Host") as Button
	var join := menu.get_node(^"%OpenJoin") as Button
	var connect_button := menu.get_node(^"%Connect") as Button
	var address_field := menu.get_node(^"%Address") as LineEdit
	var feedback := menu.get_node(^"%Status") as Label
	_action(&"ui_cancel")
	_expect(solo.has_focus(), "Back returns to the menu and restores focus.")
	_action(&"ui_down")
	_expect(multiplayer.has_focus(), "Directional input reaches the next configured action.")
	_action(&"ui_accept")
	_expect(meetup.visible and host.has_focus(), "Confirm opens the requested screen and moves focus into it.")
	var outcomes_before_host := reports.size()
	_action(&"ui_accept")
	_expect(await _wait_for(func() -> bool: return reports.size() > outcomes_before_host), "The unsupported host request settles.")
	_expect(not reports.back() and menu.visible and not feedback.text.is_empty(), "A rejected host request displays feedback and keeps the menu open.")
	join.grab_focus()
	_action(&"ui_accept")
	_expect(connect_button.has_focus(), "Opening Join puts focus inside its form.")
	_action(&"ui_accept")
	_expect(address_field.has_focus() and not feedback.text.is_empty() and game.get_phase() == Game.IDLE,
		"An empty join request returns focus to its required field and does not start the game.")
	address_field.edit()
	_action(&"ui_cancel")
	_expect(address_field.has_focus() and not address_field.is_editing(), "First Back ends editing without leaving the form.")
	# This fixture endpoint leaves a guest handshake pending until the menu cancels it.
	var handshake_sink := PacketPeerUDP.new()
	if handshake_sink.bind(0, "127.0.0.1") != OK:
		_expect(false, "Reserve a loopback endpoint for a cancellable join request.")
		quit(1)
		return
	var guest_owner := Node.new()
	root.add_child(guest_owner)
	var guest := Game.configure(FixtureWorld, FixturePlayer).lan(2).on_port(handshake_sink.get_local_port()).boot(guest_owner)
	menu.bind_game(guest)
	var outcomes_before_join := reports.size()
	address_field.text = "127.0.0.1"
	connect_button.grab_focus()
	_action(&"ui_accept")
	var cancel_button := menu.get_node(^"%Cancel") as Button
	_expect(guest.get_phase() == Game.TRANSITIONING and cancel_button.has_focus() and
		connect_button.get_focus_mode_with_override() == Control.FOCUS_NONE and
		connect_button.get_mouse_filter_with_override() == Control.MOUSE_FILTER_IGNORE,
		"A pending join focuses Cancel and blocks interaction with the address form.")
	menu.launch_join("127.0.0.1")
	_expect(reports.size() == outcomes_before_join and not feedback.text.is_empty(),
		"A second launch cannot take over the pending join result.")
	_action(&"ui_cancel")
	_expect(await _wait_for(func() -> bool: return reports.size() > outcomes_before_join and guest.get_phase() == Game.IDLE),
		"Back cancels the pending join and settles its result.")
	await process_frame
	_expect(reports.size() == outcomes_before_join + 1 and not reports.back() and feedback.text.is_empty(),
		"A canceled join reports one outcome and clears transient feedback.")
	_expect(connect_button.has_focus(), "Cancel restores focus to the Join form's initiating action.")
	menu.bind_game(game)
	guest_owner.queue_free()
	handshake_sink.close()
	await process_frame
	_action(&"ui_cancel")
	_expect(join.has_focus(), "Second Back restores the previous screen's focus.")
	_action(&"ui_cancel")
	_expect(multiplayer.has_focus(), "Backing out restores the focused title action.")
	menu.set_debugger_modal(true)
	_expect(solo.get_focus_mode_with_override() == Control.FOCUS_NONE and solo.get_mouse_filter_with_override() == Control.MOUSE_FILTER_IGNORE,
		"A covered menu rejects both focus and pointer interaction.")
	menu.set_debugger_modal(false)
	await process_frame
	_expect(multiplayer.has_focus(), "Closing the overlay restores the prior menu focus.")

	solo.grab_focus()
	_action(&"ui_accept")
	_expect(await _wait_for(func() -> bool: return game.get_phase() == Game.RUNNING), "A confirmed solo request starts gameplay.")
	_expect(game.get_local_player() != null and not menu.visible and reports.back(), "The successful launch spawns a local player and hides the menu.")
	var leave_result: GameResult = await game.leave()
	_expect(leave_result.ok and await _wait_for(func() -> bool: return menu.visible and solo.has_focus()),
		"Leaving solo restores menu visibility and focus.")
	if _failures.is_empty():
		print("CRIMSON_MENU_PASS")
	quit(0 if _failures.is_empty() else 1)


## Send a built-in Godot action without relying on the game's physical bindings.
func _action(action_name: StringName) -> void:
	for pressed: bool in [true, false]:
		var input_event := InputEventAction.new()
		input_event.action = action_name
		input_event.pressed = pressed
		root.push_input(input_event, true)


func _wait_for(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + WAIT_MS
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return condition.call()


func _expect(condition: bool, description: String) -> void:
	if not condition:
		_failures.append(description)
		push_error(description)
