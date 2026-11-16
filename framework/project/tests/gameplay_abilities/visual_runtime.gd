# Copyright Francesco Di Blasi 2026 - All Rights Reserved

extends SceneTree

const ABILITY_TYPE := &"GameplayAbility"
const ACTIVATION_TYPE := &"GameplayAbilityActivation"
const APPROVED_GRAPH := "user://visual_ability_approved.crimsonscript"
const DENIED_GRAPH := "user://visual_ability_denied.crimsonscript"
const ABILITY_LEVEL := 3
const WAIT_SECONDS := 0.05
const WAIT_DEADLINE_SECONDS := 2.0
const STARTED := &"fixture_started"
const RESUMED := &"fixture_resumed"
const ENDED := &"fixture_ended"
const CANCELED := &"fixture_canceled"
const CANCELED_TOKEN := &"fixture_canceled_token"
const CANCELED_BEFORE_END := &"fixture_canceled_before_end"
const INSTANCE_ID := &"fixture_instance_id"


## Starts after the scene tree is available to the ability and its Delay node.
func _initialize() -> void:
	_verify.call_deferred()


## Records an assertion without relying on the build's assert configuration.
func _expect(condition: bool, message: String) -> bool:
	if not condition:
		push_error("Gameplay Ability Visual Script: " + message)
	return condition


## Describes a typed data port in the saved graph.
func _pin(pin_id: StringName, value_type: int, native_type: StringName = &"") -> Dictionary:
	var info: Dictionary = {"id": pin_id, "type": value_type, "label": String(pin_id)}
	if not native_type.is_empty():
		info["class"] = native_type
	return info


## Describes a native method node exposed by the Godot class bindings.
func _call(node_id: int, native_type: StringName, method_name: StringName, arguments: Array,
		output: Dictionary = {}, values: Dictionary = {}) -> Dictionary:
	var member_info: Dictionary = {"class": native_type, "name": method_name, "inputs": arguments}
	if not output.is_empty():
		member_info["output"] = output
	return {"id": node_id, "kind": &"call_method", "member": member_info, "values": values}


## Connects two execution or data ports by their stable IDs.
func _wire(start_id: int, output_pin: StringName, end_id: int, input_pin: StringName) -> Dictionary:
	return {"from": start_id, "from_pin": output_pin, "to": end_id, "to_pin": input_pin}


## Writes a marker to the fixture root so it survives disposal of the ability instance.
func _capture(node_id: int, marker: StringName) -> Dictionary:
	return _call(node_id, &"Object", &"set_meta",
		[_pin(&"arg_0", TYPE_STRING_NAME), _pin(&"arg_1", TYPE_NIL)], {}, {&"arg_0": marker})


## Builds the same typed lifecycle declarations used by the graph editor.
func _ability_graph(permitted: bool) -> Dictionary:
	var activation_pin: Dictionary = _pin(&"activation", TYPE_OBJECT, ACTIVATION_TYPE)
	var tree_result: Dictionary = _pin(&"result", TYPE_OBJECT, &"SceneTree")
	var root_result: Dictionary = _pin(&"result", TYPE_OBJECT, &"Window")
	return {
		"version": 1,
		"events": [
			{"id": 1, "name": &"on_activated", "inputs": [activation_pin]},
			{"id": 2, "name": &"on_ended", "inputs": [activation_pin, _pin(&"canceled", TYPE_BOOL)]},
			{"id": 4, "name": &"on_canceled", "inputs": [activation_pin]},
		],
		"functions": [{
			"id": 3, "name": &"on_can_activate", "inputs": [activation_pin],
			"outputs": [_pin(&"allowed", TYPE_BOOL)],
			"graph": {
				"version": 1,
				"nodes": [
					{"id": 1, "kind": &"function_entry"},
					{"id": 2, "kind": &"return", "values": {&"allowed": permitted}},
				],
				"connections": [_wire(1, &"then", 2, &"in")],
			},
		}],
		"nodes": [
			{"id": 1, "kind": &"custom_event", "definition": 1},
			_call(2, &"Node", &"get_tree", [], tree_result),
			_call(3, &"SceneTree", &"get_root", [], root_result),
			_call(4, &"Object", &"get_instance_id", [], _pin(&"result", TYPE_INT)),
			_capture(5, INSTANCE_ID),
			_capture(6, STARTED),
			{"id": 7, "kind": &"delay", "values": {&"duration": WAIT_SECONDS}},
			_capture(8, RESUMED),
			_call(9, ACTIVATION_TYPE, &"end", [], _pin(&"result", TYPE_INT)),
			{"id": 10, "kind": &"custom_event", "definition": 2},
			_call(11, &"Node", &"get_tree", [], tree_result),
			_call(12, &"SceneTree", &"get_root", [], root_result),
			_capture(13, ENDED),
			_capture(14, CANCELED),
			# On Ended records whether this token already reached On Canceled.
			_call(15, &"Object", &"get_meta", [_pin(&"arg_0", TYPE_STRING_NAME), _pin(&"arg_1", TYPE_NIL)],
				_pin(&"result", TYPE_NIL), {&"arg_0": CANCELED_TOKEN, &"arg_1": false}),
			_capture(16, CANCELED_BEFORE_END),
			{"id": 17, "kind": &"custom_event", "definition": 4},
			_call(18, &"Node", &"get_tree", [], tree_result),
			_call(19, &"SceneTree", &"get_root", [], root_result),
			_capture(20, CANCELED_TOKEN),
		],
		"connections": [
			_wire(1, &"then", 2, &"in"), _wire(2, &"then", 3, &"in"),
			_wire(3, &"then", 4, &"in"), _wire(4, &"then", 5, &"in"),
			_wire(5, &"then", 6, &"in"), _wire(6, &"then", 7, &"in"),
			_wire(7, &"then", 8, &"in"), _wire(8, &"then", 9, &"in"),
			_wire(2, &"result", 3, &"target"), _wire(3, &"result", 5, &"target"),
			_wire(3, &"result", 6, &"target"), _wire(3, &"result", 8, &"target"),
			_wire(4, &"result", 5, &"arg_1"), _wire(1, &"activation", 6, &"arg_1"),
			_wire(1, &"activation", 8, &"arg_1"), _wire(1, &"activation", 9, &"target"),
			_wire(10, &"then", 11, &"in"), _wire(11, &"then", 12, &"in"),
			_wire(12, &"then", 13, &"in"), _wire(13, &"then", 14, &"in"),
			_wire(11, &"result", 12, &"target"), _wire(12, &"result", 13, &"target"),
			_wire(12, &"result", 14, &"target"), _wire(10, &"activation", 13, &"arg_1"),
			_wire(10, &"canceled", 14, &"arg_1"),
			_wire(14, &"then", 15, &"in"), _wire(15, &"then", 16, &"in"),
			_wire(12, &"result", 15, &"target"), _wire(12, &"result", 16, &"target"),
			_wire(15, &"result", 16, &"arg_1"),
			_wire(17, &"then", 18, &"in"), _wire(18, &"then", 19, &"in"), _wire(19, &"then", 20, &"in"),
			_wire(18, &"result", 19, &"target"), _wire(19, &"result", 20, &"target"),
			_wire(17, &"activation", 20, &"arg_1"),
		],
	}


## Saves and reloads a graph independently of Godot's resource cache.
func _load_ability(path: String, permitted: bool) -> Script:
	var graph_script: Script = ClassDB.instantiate(&"VisualScript") as Script
	graph_script.call(&"set_graph_base", ABILITY_TYPE)
	graph_script.call(&"set_graph", _ability_graph(permitted))
	if not _expect(graph_script.can_instantiate(), "The authored ability graph must compile."):
		return null
	if not _expect(ResourceSaver.save(graph_script, path) == OK, "The ability graph must save."):
		return null
	var saved_script: Script = ResourceLoader.load(path, "VisualScript", ResourceLoader.CACHE_MODE_IGNORE) as Script
	if not _expect(saved_script != null and saved_script.can_instantiate(), "The saved ability graph must reload."):
		return null
	if not _expect(saved_script.get_instance_base_type() == ABILITY_TYPE, "The saved ability graph must retain its native base."):
		return null
	return saved_script


## Clears observations between activations of the same grant.
func _clear_markers() -> void:
	for marker: StringName in [STARTED, RESUMED, ENDED, CANCELED, CANCELED_TOKEN, CANCELED_BEFORE_END, INSTANCE_ID]:
		root.remove_meta(marker)


## Reads the captured activation token if the graph reached the start marker.
func _started_activation() -> GameplayAbilityActivation:
	var captured: Variant = root.get_meta(STARTED, false)
	if captured is GameplayAbilityActivation:
		return captured
	return null


## Confirms a graph function can reject activation without firing lifecycle events.
func _verify_rejection(component: AbilitySystemComponent, denied: Script) -> bool:
	var grant_id: int = component.give_ability(denied)
	return _expect(grant_id > 0, "The denied graph must still be grantable.") \
		and _expect(component.try_activate_ability(grant_id) != OK, "The graph's false eligibility result must reject activation.") \
		and _expect(not component.is_ability_active(grant_id), "The rejected grant must remain inactive.") \
		and _expect(not root.has_meta(STARTED) and not root.has_meta(ENDED) and not root.has_meta(CANCELED_TOKEN),
			"Rejected activation must leave the lifecycle events untouched.")


## Confirms both terminal events report the same token in callback order.
func _verify_cancel_events(token: GameplayAbilityActivation) -> bool:
	return _expect(token != null and is_same(root.get_meta(ENDED, false), token) and root.get_meta(CANCELED, false) == true,
			"On Ended must report the canceled token.") \
		and _expect(is_same(root.get_meta(CANCELED_BEFORE_END, false), token),
			"On Canceled must receive the same token before On Ended runs.")


## Confirms latent completion retains its token and cancellation suppresses a pending continuation.
func _verify_activation(component: AbilitySystemComponent, approved: Script) -> bool:
	var grant_id: int = component.give_ability(approved, ABILITY_LEVEL)
	if not _expect(grant_id > 0 and component.get_ability_level(grant_id) == ABILITY_LEVEL, "The approved graph must retain its grant level."):
		return false
	if not _expect(component.try_activate_ability(grant_id) == OK, "The graph's true eligibility result must start the activation."):
		return false
	var first_token: GameplayAbilityActivation = _started_activation()
	var first_id: int = root.get_meta(INSTANCE_ID, 0)
	if not _expect(first_id > 0 and first_token != null and first_token.is_active(), "The activation event must receive a live token from an ability instance."):
		return false
	if not _expect(component.is_ability_active(grant_id) and not root.has_meta(RESUMED), "Delay must suspend a still-active graph."):
		return false
	var deadline: SceneTreeTimer = create_timer(WAIT_DEADLINE_SECONDS)
	while not root.has_meta(ENDED) and deadline.time_left > 0.0:
		await process_frame
	if not _expect(is_same(root.get_meta(RESUMED, false), first_token) and is_same(root.get_meta(ENDED, false), first_token),
			"Delay must resume with the same token and the graph's End call must fire the end event."):
		return false
	if not _expect(root.get_meta(CANCELED, true) == false and not first_token.is_active() and not component.is_ability_active(grant_id),
			"Natural completion must report false for canceled and invalidate the token."):
		return false
	if not _expect(not root.has_meta(CANCELED_TOKEN) and root.get_meta(CANCELED_BEFORE_END, true) == false,
			"Natural completion must skip On Canceled."):
		return false

	_clear_markers()
	if not _expect(component.try_activate_ability(grant_id) == OK, "A finished grant must be reusable."):
		return false
	var second_token: GameplayAbilityActivation = _started_activation()
	var second_id: int = root.get_meta(INSTANCE_ID, 0)
	if not _expect(second_id > 0 and second_id != first_id and second_token != null and second_token != first_token,
			"Reactivation must give the graph a new ability instance and token."):
		return false
	if not _expect(component.cancel_ability(grant_id) == OK, "The ability system must cancel the suspended graph."):
		return false
	if not _verify_cancel_events(second_token):
		return false
	await create_timer(WAIT_SECONDS * 2.0).timeout
	if not _expect(not root.has_meta(RESUMED) and not second_token.is_active() and not component.is_ability_active(grant_id),
			"Cancellation must invalidate the token and discard the pending Delay continuation."):
		return false

	_clear_markers()
	if not _expect(component.try_activate_ability(grant_id) == OK, "Cancellation must keep the grant available."):
		return false
	var removed_token: GameplayAbilityActivation = _started_activation()
	if not _expect(component.clear_ability(grant_id) == OK, "Removing the grant must end the running graph."):
		return false
	return _verify_cancel_events(removed_token) \
		and _expect(component.try_activate_ability(grant_id) != OK, "The removed grant must reject further activation.")


## Runs the saved Visual Script abilities against a standalone gameplay component.
func _verify() -> void:
	var approved: Script = _load_ability(APPROVED_GRAPH, true)
	var denied: Script = _load_ability(DENIED_GRAPH, false)
	if approved == null or denied == null:
		quit(1)
		return
	var component := AbilitySystemComponent.new()
	root.add_child(component)
	var passed := _verify_rejection(component, denied)
	if passed:
		passed = await _verify_activation(component, approved)
	_clear_markers()
	component.free()
	if not passed:
		quit(1)
		return
	print("CRIMSON_VISUAL_SCRIPT_GAMEPLAY_ABILITIES_PASSED")
	quit()
