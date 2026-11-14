# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree
## Exercise the debugger's view bindings against fixture-owned controls and a loopback relay.

const MenuFixture := preload("res://demo/ui/tests/menu_smoke.gd")
const Debugger := preload("res://demo/ui/network_debugger.gd")
const ToolContent := preload("res://demo/ui/network_tool_content.gd")
const ScenarioPanel := preload("res://demo/ui/network_scenario_panel.gd")
const MetricGraph := preload("res://demo/ui/network_latency_graph.gd")
const Emulator := preload("res://demo/session/network_emulator.gd")
const Profiles := preload("res://demo/session/network_simulator_profiles.gd")
const FixturePlayer: PackedScene = preload("res://tests/game_flow/fixtures/start_player.tscn")
const LOOPBACK := "127.0.0.1"
const PROFILE_NAME := "Debugger UI fixture"
const PROFILE_DELAY_MS := 20
const PROFILE_UPLINK := 48
const PROFILE_DOWNLINK := 128
const SPIKE_DELAY_MS := 250
const EFFECT_DURATION_MS := 180
const WAIT_MS := 8000

var _debugger: Debugger
var _tool: Window
var _relay: Emulator
var _failures: Array[String] = []


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var store := OS.get_environment(Profiles.PROFILE_STORE_ENV)
	if not store.is_absolute_path():
		_expect(false, "Set the profile-store environment variable to a fixture-owned absolute path.")
		_finish("guest")
		return
	if "--verify-profile" in OS.get_cmdline_user_args():
		_verify_profile()
		_finish("profile")
		return
	var port := _free_port()
	if port <= 0:
		_expect(false, "Reserve a free loopback port for the fixture.")
		_finish("guest")
		return
	var host_owner := _branch("DebuggerHost")
	var guest_owner := _branch("DebuggerGuest")
	_relay = Emulator.new()
	guest_owner.add_child(_relay)
	var fixture_world := _world()
	var host := Game.configure(fixture_world, FixturePlayer).lan(2).on_port(port).boot(host_owner)
	var guest := Game.configure(fixture_world, FixturePlayer).lan(2).on_port(port).provider(_relay.provider()).boot(guest_owner)
	var menu := MenuFixture.menu_fixture()
	guest_owner.add_child(menu)
	menu.bind_game(guest)
	var settings := Control.new()
	settings.hide()
	guest_owner.add_child(settings)
	guest_owner.add_child(_debugger_fixture())
	_debugger.setup(guest, _relay, menu, settings)
	var hosted: GameResult = await host.host(LOOPBACK)
	if not hosted.ok:
		_expect(false, "The fixture can host a loopback session.")
		_finish("guest")
		return
	var join_results: Array[String] = []
	menu.launch_finished.connect(func(ok: bool, message: String) -> void: join_results.append("" if ok else message))
	menu.launch_join(LOOPBACK)
	if not await _wait(func() -> bool: return not join_results.is_empty()) or guest.get_phase() != Game.RUNNING:
		_expect(false, "The fixture joins the loopback session through its real menu. Result: %s" % join_results)
		await host.leave()
		_finish("guest")
		return
	_debugger._open()
	_expect(_tool.visible and _button(&"FocusGame").has_focus(), "Opening the debugger gives focus to its first usable action.")
	_expect(menu.get_focus_behavior_recursive() == Control.FOCUS_BEHAVIOR_DISABLED, "The tool suspends menu interaction while focused.")
	await _exercise_profiles()
	await _exercise_spike()
	await _exercise_scenario()
	await _exercise_reconnect(guest)
	_button(&"FocusGame").pressed.emit()
	_expect(_tool.visible and menu.get_focus_behavior_recursive() != Control.FOCUS_BEHAVIOR_DISABLED,
		"Focus game returns input while keeping metrics available.")
	_debugger._open()
	_action(_tool, &"ui_cancel")
	_expect(not _tool.visible, "The standard Back action closes the debugger.")
	await guest.leave()
	await host.leave()
	_finish("guest")


## Give the host and guest separate deterministic spawn points without an authored world asset.
func _world() -> PackedScene:
	var world := Node3D.new()
	world.name = &"DebuggerFixtureWorld"
	for spawn_index: int in range(2):
		var spawn := PlayerSpawnPoint.new()
		spawn.position.x = float(spawn_index)
		world.add_child(spawn)
		spawn.owner = world
	var fixture := PackedScene.new()
	fixture.pack(world)
	world.free()
	return fixture


## Use an independent SceneMultiplayer for each endpoint in the same test process.
func _branch(branch_name: StringName) -> Node:
	var endpoint := Node.new()
	endpoint.name = branch_name
	root.add_child(endpoint)
	var scope := SceneMultiplayer.new()
	scope.server_relay = false
	set_multiplayer(scope, endpoint.get_path())
	return endpoint


## Obtain a loopback-only temporary port without relying on project settings.
func _free_port() -> int:
	var probe := PacketPeerUDP.new()
	if probe.bind(0, LOOPBACK) != OK:
		return 0
	var allocated_port := probe.get_local_port()
	probe.close()
	return allocated_port


## Construct script-required UI roles without loading an authored UI scene, theme, or layout.
func _debugger_fixture() -> Debugger:
	_debugger = Debugger.new()
	_tool = Window.new()
	_tool.hide()
	MenuFixture.mount(_debugger, _debugger, _tool, &"Tool")
	var ui_root := ToolContent.new()
	MenuFixture.mount(_debugger, _tool, ui_root, &"Content")
	var input_gate := Control.new()
	input_gate.hide()
	MenuFixture.mount(_debugger, ui_root, input_gate, &"InputGate")
	var scenario := ScenarioPanel.new()
	MenuFixture.mount(_debugger, ui_root, scenario, &"Scenarios")
	var timeline := VBoxContainer.new()
	scenario.add_child(timeline)
	var controls := VBoxContainer.new()
	ui_root.add_child(controls)
	for control_id: StringName in [&"FocusGame", &"SaveProfile", &"Apply", &"StartSpike", &"CancelSpike", &"StartOutage", &"DropLink",
		&"ResumeLink", &"Disconnect", &"Reconnect", &"ResetCounters", &"Pause", &"ClearCharts", &"CopyEvents", &"ClearEvents"]:
		MenuFixture.mount(_debugger, controls, Button.new(), control_id)
	for control_id: StringName in [&"Presets", &"SpikeDirection", &"Direction"]:
		MenuFixture.mount(_debugger, controls, OptionButton.new(), control_id)
	for control_id: StringName in [&"Delay", &"Jitter", &"Loss", &"Uplink", &"Downlink", &"SpikeExtra", &"SpikeDuration",
		&"SpikeCountdown", &"Duration", &"Countdown"]:
		_spin(_debugger, controls, control_id)
	for control_id: StringName in [&"Feedback", &"RTTValue", &"AgeValue", &"PendingValue", &"CorrectionValue", &"UpSent", &"UpDrop",
		&"UpQueue", &"UpBytes", &"UpLimit", &"DownSent", &"DownDrop", &"DownQueue", &"DownBytes", &"DownLimit", &"History",
		&"Session", &"Connection", &"Movement", &"Positions", &"Relay"]:
		MenuFixture.mount(_debugger, controls, Label.new(), control_id)
	for control_id: StringName in [&"RTTGraph", &"AgeGraph", &"PendingGraph", &"CorrectionGraph"]:
		MenuFixture.mount(_debugger, controls, MetricGraph.new(), control_id)
	MenuFixture.mount(_debugger, controls, TextEdit.new(), &"EventLog")
	MenuFixture.mount(_debugger, controls, LineEdit.new(), &"ProfileName")
	for control_id: StringName in [&"LoadTemplate", &"AddStage", &"ReplaceStage", &"RemoveStage", &"MoveStageUp", &"MoveStageDown",
		&"RunScenario", &"StopScenario"]:
		MenuFixture.mount(scenario, timeline, Button.new(), control_id)
	for control_id: StringName in [&"Templates", &"StageProfile", &"StageOutage"]:
		MenuFixture.mount(scenario, timeline, OptionButton.new(), control_id)
	for control_id: StringName in [&"StageDuration", &"ScenarioStartDelay"]:
		_spin(scenario, timeline, control_id)
	MenuFixture.mount(scenario, timeline, ItemList.new(), &"Steps")
	MenuFixture.mount(scenario, timeline, CheckBox.new(), &"LoopScenario")
	MenuFixture.mount(scenario, timeline, Label.new(), &"ScenarioStatus")
	return _debugger


## Use a permissive fixture range and let each production control validate its operation.
func _spin(owner_node: Node, parent_node: Node, control_id: StringName) -> void:
	var field := SpinBox.new()
	field.allow_greater = true
	MenuFixture.mount(owner_node, parent_node, field, control_id)


func _exercise_profiles() -> void:
	var profiles := Profiles.presets()
	var presets := _debugger.get_node(^"%Presets") as OptionButton
	if not profiles.is_empty():
		preset_selected(presets, presets.get_item_index(0))
		var known: Profiles.Profile = profiles[0]
		_expect(_number(&"Uplink").value == known.uplink_kbps and _number(&"Downlink").value == known.downlink_kbps,
			"Selecting a preset fills the controls from its current model data.")
	var name_field := _debugger.get_node(^"%ProfileName") as LineEdit
	name_field.text = PROFILE_NAME
	name_field.text_changed.emit(PROFILE_NAME)
	_number(&"Delay").get_line_edit().text = str(PROFILE_DELAY_MS)
	_number(&"Jitter").get_line_edit().text = "0"
	_number(&"Loss").get_line_edit().text = "0"
	_number(&"Downlink").get_line_edit().text = str(PROFILE_DOWNLINK)
	_number(&"Uplink").get_line_edit().text = str(PROFILE_UPLINK - 1)
	_button(&"SaveProfile").pressed.emit()
	_number(&"Uplink").get_line_edit().text = str(PROFILE_UPLINK)
	_button(&"SaveProfile").pressed.emit()
	_verify_profile()
	_button(&"Apply").pressed.emit()
	_expect(await _wait(_matches_profile), "Apply updates the active relay from the draft's fixture values.")


func _exercise_spike() -> void:
	var direction := _debugger.get_node(^"%SpikeDirection") as OptionButton
	preset_selected(direction, direction.get_item_index(Emulator.Direction.UPLINK))
	_number(&"SpikeExtra").get_line_edit().text = str(SPIKE_DELAY_MS)
	_number(&"SpikeDuration").get_line_edit().text = str(EFFECT_DURATION_MS)
	_number(&"SpikeCountdown").get_line_edit().text = "0"
	var transitions: Array[StringName] = []
	_relay.spike_changed.connect(func(next_state: StringName, lane: int, delay_ms: int) -> void:
		if lane == Emulator.Direction.UPLINK and delay_ms == SPIKE_DELAY_MS:
			transitions.append(next_state)
	)
	_button(&"StartSpike").pressed.emit()
	_expect(await _wait(func() -> bool: return transitions.has(&"active")), "Lag spike sends the configured direction and delay to the relay.")
	_expect(await _wait(func() -> bool: return _relay.get_stats().spike_remaining_ms == 0), "The spike completes without replacing the applied profile.")


func _exercise_scenario() -> void:
	var scenario := _debugger.get_node(^"%Scenarios") as ScenarioPanel
	var profiles := scenario.get_node(^"%StageProfile") as OptionButton
	var draft_list := scenario.get_node(^"%Steps") as ItemList
	var add_stage := scenario.get_node(^"%AddStage") as Button
	var start := scenario.get_node(^"%RunScenario") as Button
	var stop := scenario.get_node(^"%StopScenario") as Button
	var remove_stage := scenario.get_node(^"%RemoveStage") as Button
	while draft_list.item_count > 0 and not remove_stage.disabled:
		remove_stage.pressed.emit()
	var saved := Profiles.saved_profiles()
	var saved_index := saved.find_custom(func(profile: Profiles.Profile) -> bool: return profile.label == PROFILE_NAME)
	if saved_index < 0:
		_expect(false, "Scenario can select the fixture profile.")
		return
	preset_selected(profiles, Profiles.presets().size() + saved_index)
	(scenario.get_node(^"%StageDuration") as SpinBox).get_line_edit().text = str(WAIT_MS)
	var initial_count := draft_list.item_count
	add_stage.pressed.emit()
	_expect(draft_list.item_count == initial_count + 1, "Adding a profile adds one stage to the editable draft.")
	_expect(await _wait(func() -> bool: return not start.disabled), "The draft can start on the live relay.")
	start.grab_focus()
	_action(_tool, &"ui_accept")
	_expect(await _wait(func() -> bool: return _relay.get_stats().scenario_label == PROFILE_NAME), "The relay runs the authored scenario stage.")
	_expect(stop.has_focus(), "A running scenario focuses its Stop action.")
	_action(_tool, &"ui_accept")
	_expect(await _wait(func() -> bool: return _relay.get_stats().scenario_step_count == 0 and _matches_profile()),
		"Stopping through Confirm restores the profile.")


func _exercise_reconnect(game: Game) -> void:
	var previous_peer := game.network_diagnostics().peer_id
	var reconnect := _button(&"Reconnect")
	_button(&"Disconnect").pressed.emit()
	var left := await _wait(func() -> bool: return game.get_phase() == Game.IDLE and not reconnect.disabled)
	_expect(left, "Disconnect retires the session and enables reconnect.")
	if left:
		reconnect.pressed.emit()
		_expect(await _wait(func() -> bool: return game.get_phase() == Game.RUNNING and game.network_diagnostics().peer_id != previous_peer),
			"Reconnect uses the previously successful endpoint and gets a new peer.")


func _verify_profile() -> void:
	var matching := Profiles.saved_profiles().filter(func(profile: Profiles.Profile) -> bool: return profile.label == PROFILE_NAME)
	_expect(matching.size() == 1, "The fixture's named profile has one persisted entry.")
	if matching.size() == 1:
		var saved := matching[0] as Profiles.Profile
		_expect(saved.delay_ms == PROFILE_DELAY_MS and saved.uplink_kbps == PROFILE_UPLINK and saved.downlink_kbps == PROFILE_DOWNLINK,
			"The persisted profile holds the new delay and both bandwidth values.")


func _matches_profile() -> bool:
	var stats := _relay.get_stats()
	return stats.one_way_delay_ms == PROFILE_DELAY_MS and stats.uplink_kbps == PROFILE_UPLINK and stats.downlink_kbps == PROFILE_DOWNLINK


func preset_selected(picker: OptionButton, index: int) -> void:
	if index >= 0 and index < picker.item_count:
		picker.select(index)
		picker.item_selected.emit(index)


func _button(control_id: StringName) -> Button:
	return _debugger.get_node(NodePath("%%%s" % control_id)) as Button


func _number(control_id: StringName) -> SpinBox:
	return _debugger.get_node(NodePath("%%%s" % control_id)) as SpinBox


func _action(viewport: Viewport, action_name: StringName) -> void:
	for pressed: bool in [true, false]:
		var input_event := InputEventAction.new()
		input_event.action = action_name
		input_event.pressed = pressed
		viewport.push_input(input_event, true)


func _wait(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + WAIT_MS
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return condition.call()


func _expect(condition: bool, message: String) -> void:
	if not condition:
		_failures.append(message)
		push_error(message)


func _finish(part: String) -> void:
	if _failures.is_empty():
		print("CRIMSON_NETWORK_SIMULATOR_UI_PASS %s" % part)
	quit(0 if _failures.is_empty() else 1)
