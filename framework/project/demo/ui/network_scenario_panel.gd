# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends ScrollContainer
## Authors and runs a bounded, ordered sequence of link profiles in the debugger.

const NetworkDebugModel := preload("res://demo/ui/network_debug_model.gd")
const NetworkEmulator := preload("res://demo/session/network_emulator.gd")
const Profiles := preload("res://demo/session/network_simulator_profiles.gd")
const NO_OUTAGE: int = -1

## Report an operator action to the debugger's shared event timeline.
signal reported(message: String)

@onready var _templates: OptionButton = %Templates
@onready var _steps_view: ItemList = %Steps
@onready var _profiles_view: OptionButton = %StageProfile
@onready var _duration: SpinBox = %StageDuration
@onready var _outage: OptionButton = %StageOutage
@onready var _load: Button = %LoadTemplate
@onready var _add: Button = %AddStage
@onready var _replace: Button = %ReplaceStage
@onready var _remove: Button = %RemoveStage
@onready var _up: Button = %MoveStageUp
@onready var _down: Button = %MoveStageDown
@onready var _run: Button = %RunScenario
@onready var _stop: Button = %StopScenario
@onready var _loop: CheckBox = %LoopScenario
@onready var _start_delay: SpinBox = %ScenarioStartDelay
@onready var _status: Label = %ScenarioStatus

## Route authority and runner status stay with the demo relay.
var _model: NetworkDebugModel
## Latest connection snapshot drives editor and transport-action availability.
var _relay: NetworkEmulator.Stats
## Named presets currently available for copying into the draft.
var _profiles: Array[Profiles.Profile] = []
## Editable steps belong to this tool until Start copies them to the relay.
var _steps: Array[NetworkEmulator.ScenarioStep] = []
## Selected row in the draft, separate from the running stage.
var _selected: int = -1


func _ready() -> void:
	for index: int in range(Profiles.Template.size()):
		_templates.add_item(Profiles.template_label(index), index)
	_templates.select(0)
	_outage.add_item(tr("None"))
	_outage.add_item(tr("Uplink"))
	_outage.add_item(tr("Downlink"))
	_outage.add_item(tr("Both directions"))
	_outage.select(0)
	_load.pressed.connect(_load_template)
	_steps_view.item_selected.connect(_select_stage)
	_profiles_view.item_selected.connect(func(_index: int) -> void: _refresh_controls())
	_add.pressed.connect(_add_stage)
	_replace.pressed.connect(_replace_stage)
	_remove.pressed.connect(_remove_stage)
	_up.pressed.connect(_move_stage.bind(-1))
	_down.pressed.connect(_move_stage.bind(1))
	_run.pressed.connect(_start)
	_stop.pressed.connect(_end)
	_duration.min_value = NetworkEmulator.MIN_SCENARIO_STEP_MS
	_duration.max_value = NetworkEmulator.MAX_SCENARIO_STEP_MS
	_start_delay.max_value = NetworkEmulator.MAX_OUTAGE_START_DELAY_MS


## Bind the debugger's command model and load the starter timeline.
func setup(model: NetworkDebugModel) -> void:
	_model = model
	_load_template()


## Refresh named profiles without changing the current draft or run.
func update_profiles(profiles: Array[Profiles.Profile], builtin_count: int) -> void:
	var selected_index: int = _profiles_view.selected
	var previous: Profiles.Profile = _profiles[selected_index] if selected_index >= 0 else null
	var had_saved: bool = selected_index >= builtin_count
	_profiles = profiles
	_profiles_view.clear()
	var restored: int = -1
	for index: int in range(profiles.size()):
		var profile: Profiles.Profile = profiles[index]
		var saved: bool = index >= builtin_count
		var label: String = tr("Saved · %s") % profile.label if saved else profile.label
		_profiles_view.add_item(label)
		if previous != null and saved == had_saved and profile.label == previous.label \
				and profile.matches(previous):
			restored = index
	_profiles_view.select(restored)
	_refresh_controls()


## Reflect the actual countdown or active stage from the detached relay snapshot.
func refresh(relay: NetworkEmulator.Stats) -> void:
	_relay = relay
	if _running():
		if relay.scenario_starts_in_ms > 0:
			_status.text = tr("Starts in %.1f s. Focus the game to play through the scenario.") % (
				relay.scenario_starts_in_ms * 0.001)
		else:
			_status.text = tr("Stage %d / %d · %s · %.1f s remaining") % [
				relay.scenario_step + 1, relay.scenario_step_count, relay.scenario_label,
				relay.scenario_remaining_ms * 0.001]
	elif relay == null or not relay.active:
		_status.text = tr("Join a loopback ENet server to run a scenario.")
	elif _manual_effect_active():
		_status.text = tr("End the manual spike or outage before starting a scenario.")
	else:
		_status.text = tr("%d stages · %.1f s per pass. The applied profile returns when the scenario ends.") % [
			_steps.size(), _total_ms() * 0.001]
	_refresh_controls()


## Build a reusable starting point. The resulting steps stay editable.
func _load_template() -> void:
	if _running() or _model == null:
		return
	_steps = Profiles.scenario_template(_templates.get_selected_id() as Profiles.Template)
	_render_steps(0)


## Set the edit target and show the selected stage's values.
func _select_stage(index: int) -> void:
	_selected = index
	var stage: NetworkEmulator.ScenarioStep = _steps[index]
	_duration.set_value_no_signal(stage.duration_ms)
	_outage.select(_outage_index(stage.outage_direction))
	_profiles_view.select(-1)
	for profile_index: int in range(_profiles.size()):
		var profile: Profiles.Profile = _profiles[profile_index]
		if profile.label == stage.label and profile.matches(stage):
			_profiles_view.select(profile_index)
			break
	_refresh_controls()


## Snapshot the selected profile and controls into a stage.
func _draft_stage() -> NetworkEmulator.ScenarioStep:
	_duration.apply()
	var directions: Array[int] = [NO_OUTAGE, NetworkEmulator.Direction.UPLINK,
		NetworkEmulator.Direction.DOWNLINK, NetworkEmulator.Direction.BOTH]
	return _profiles[_profiles_view.selected].stage(int(_duration.value), directions[_outage.selected])


## Append a stage without mutating a reusable preset.
func _add_stage() -> void:
	_steps.append(_draft_stage())
	_render_steps(_steps.size() - 1)


## Apply the draft to the selected row.
func _replace_stage() -> void:
	_steps[_selected] = _draft_stage()
	_render_steps(_selected)


## Remove the selected row and keep a valid selection when one exists.
func _remove_stage() -> void:
	_steps.remove_at(_selected)
	_render_steps(mini(_selected, _steps.size() - 1))


## Move the selected stage one row earlier or later.
func _move_stage(offset: int) -> void:
	var stage: NetworkEmulator.ScenarioStep = _steps.pop_at(_selected)
	var next: int = _selected + offset
	_steps.insert(next, stage)
	_render_steps(next)


## Rebuild the displayed sequence after a draft edit.
func _render_steps(selected: int) -> void:
	_steps_view.clear()
	for index: int in range(_steps.size()):
		var stage: NetworkEmulator.ScenarioStep = _steps[index]
		var effect: String = "" if stage.outage_direction == NO_OUTAGE else tr(" · %s outage") % (
			_outage.get_item_text(_outage_index(stage.outage_direction)))
		var row: int = _steps_view.add_item(tr("%d. %s · %.1f s%s") % [
			index + 1, stage.label, stage.duration_ms * 0.001, effect])
		_steps_view.set_item_tooltip(row, tr("One-way delay %d ms · jitter ±%d ms · loss %.1f%%\nUplink %s · downlink %s") % [
			stage.delay_ms, stage.jitter_ms, stage.loss_percent, _rate(stage.uplink_kbps), _rate(stage.downlink_kbps)])
	_selected = selected
	if selected >= 0:
		_steps_view.select(selected)
		_select_stage(selected)
		_steps_view.ensure_current_is_visible()
	refresh(_relay)


## Convert relay directions to the order displayed in the editor.
func _outage_index(direction: int) -> int:
	match direction:
		NetworkEmulator.Direction.UPLINK: return 1
		NetworkEmulator.Direction.DOWNLINK: return 2
		NetworkEmulator.Direction.BOTH: return 3
	return 0


## Format a bandwidth cap without presenting zero as a blocked link.
func _rate(rate_kbps: int) -> String:
	return tr("Unlimited") if rate_kbps == 0 else tr("%d kbps") % rate_kbps


## Start a detached run so UI draft edits cannot affect the active timeline.
func _start() -> void:
	_start_delay.apply()
	if _model.start_scenario(_steps, _loop.button_pressed, int(_start_delay.value)):
		reported.emit(tr("Scenario started with %d stages (%s).") % [
			_steps.size(), tr("loop") if _loop.button_pressed else tr("once")])
	else:
		reported.emit(tr("Could not start the scenario. Check the link and stage durations."))
	refresh(_model.conditions())


## Stop an armed or running timeline and restore the prior profile.
func _end() -> void:
	_model.stop_scenario()
	refresh(_model.conditions())


## Report both armed and playing scenarios as owned by the runner.
func _running() -> bool:
	return _relay != null and _relay.scenario_step_count > 0


## Manual impairments own the route until they complete or are canceled.
func _manual_effect_active() -> bool:
	return _relay != null and (_relay.link_dropped or _relay.outage_starts_in_ms > 0 or _relay.spike_remaining_ms > 0)


## Sum the configured durations for the relay's maximum total budget.
func _total_ms() -> int:
	var total: int = 0
	for stage: NetworkEmulator.ScenarioStep in _steps:
		total += stage.duration_ms
	return total


## Keep authoring usable offline and focus on valid actions when the run changes.
func _refresh_controls() -> void:
	var running: bool = _running()
	var editing: bool = not running and not _profiles.is_empty()
	_templates.disabled = running
	_load.disabled = running
	_profiles_view.disabled = not editing
	_duration.editable = editing
	_outage.disabled = not editing
	_add.disabled = not editing or _profiles_view.selected < 0 or _steps.size() >= NetworkEmulator.MAX_SCENARIO_STEPS
	_replace.disabled = not editing or _profiles_view.selected < 0 or _selected < 0
	_remove.disabled = running or _selected < 0
	_up.disabled = running or _selected <= 0
	_down.disabled = running or _selected < 0 or _selected >= _steps.size() - 1
	_run.disabled = running or _relay == null or not _relay.active or _manual_effect_active() \
		or _steps.is_empty() or _total_ms() > NetworkEmulator.MAX_SCENARIO_TOTAL_MS
	_stop.disabled = not running
	_loop.disabled = running
	_start_delay.editable = not running
	if _run.has_focus() and _run.disabled and not _stop.disabled:
		_stop.grab_focus()
	elif _stop.has_focus() and _stop.disabled and not _run.disabled:
		_run.grab_focus()
