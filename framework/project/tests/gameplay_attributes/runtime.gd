# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const SETTING: StringName = &"crimson/gameplay_attributes/sets"
const HEALTH: StringName = &"vitals.health"
const STAMINA: StringName = &"vitals.stamina"
const SET_ID: String = "vitals"

## Owns the standalone record whose committed changes the fixture observes.
var first: GameplayAttributes
## Old and new readings received by the filtered health watch.
var changes: Array[Vector2] = []
## Records that the new numeric value is already published inside each callback.
var callbacks_read_committed: bool = true
## Records edits attempted during change notification.
var callback_edit: Error = OK


## Models an actor reading its configured child component from its own Ready callback.
class AttributeActor extends Node:
	var health_set: GameplayAttributeSet
	var initial_health: GameplayAttributeValue

	func _ready() -> void:
		var abilities: AbilitySystemComponent = $AbilitySystemComponent
		health_set = abilities.get_attribute_set(&"vitals")
		if health_set != null:
			initial_health = health_set.get_attribute(&"health")


## Starts after scene-level native classes and the registry default are registered.
func _initialize() -> void:
	call_deferred(&"_verify")


## Builds one isolated authored definition independent of the demo project.
func _definitions(include_health: bool) -> Dictionary:
	return {
		"version": 1,
		"sets": [{
			"id": SET_ID,
			"attributes": [
				{"id": "health" if include_health else "armor", "label": "Vital Reading",
					"initial": 100.0, "minimum": 0.0, "maximum": 100.0},
				{"id": "stamina", "visibility": "owner"},
			],
		}],
	}


## Captures a filtered transition and tries a reentrant write.
func _attribute_changed(previous: GameplayAttributeValue, current: GameplayAttributeValue) -> void:
	changes.append(Vector2(previous.get_current(), current.get_current()))
	callbacks_read_committed = callbacks_read_committed and first.get_attribute(HEALTH).get_current() == current.get_current()
	callback_edit = first.set_base(HEALTH, 1.0)


## Reports a contract failure to the strict Godot fixture runner.
func _expect(condition: bool, description: String) -> bool:
	if not condition:
		push_error("[Crimson] Attributes runtime fixture: " + description)
	return condition


## Tests finite native edits, fixed membership, detached reads, and filtered watches.
func _exercise() -> bool:
	ProjectSettings.set_setting(SETTING, _definitions(true))
	first = GameplayAttributes.from_sets(PackedStringArray([SET_ID]))
	var second: GameplayAttributes = GameplayAttributes.from_sets(PackedStringArray([SET_ID]))
	if not _expect(first != null and second != null, "create independent owners from one project registry"):
		return false
	var starting: GameplayAttributeValue = first.get_attribute(HEALTH)
	var watch: GameplayAttributeWatch = first.watch_attribute(HEALTH)
	if not _expect(starting != null and starting.has_base() and starting.get_base() == 0.0
			and first.set_base(HEALTH, 100.0) == OK
			and watch != null and watch.changed.connect(_attribute_changed) == OK, "resolve a base reading and typed watch"):
		return false
	if not _expect(first.add_base(HEALTH, -25.0) == OK and watch.get_value().get_current() == 75.0
			and second.get_attribute(HEALTH).get_current() == 0.0 and starting.get_current() == 0.0,
			"commit 75 without changing a sibling owner or earlier reading"):
		return false
	if not _expect(first.set_base(HEALTH, 75.0) == OK and first.add_base(STAMINA, 1.0) == OK
			and first.set_base(STAMINA, 1.0e308) == OK and first.add_base(STAMINA, 1.0e308) == ERR_INVALID_PARAMETER
			and first.get_attribute(STAMINA).get_current() == 1.0e308
			and first.set_base(HEALTH, NAN) == ERR_INVALID_PARAMETER
			and first.add_base(&"vitals.absent", 1.0) == ERR_DOES_NOT_EXIST
			and first.get_attribute(&"vitals.absent") == null and changes == [Vector2(100.0, 75.0)],
			"skip no-op, unrelated, invalid, and missing values"):
		return false
	if not _expect(first.add_base(HEALTH, -100.0) == OK and first.set_base(HEALTH, 250.0) == OK
			and changes == [Vector2(100.0, 75.0), Vector2(75.0, -25.0), Vector2(-25.0, 250.0)]
			and callbacks_read_committed and callback_edit == ERR_BUSY,
			"allow finite values beyond legacy bounds and reject reentrant writes"):
		return false
	ProjectSettings.set_setting(SETTING, _definitions(false))
	var third: GameplayAttributes = GameplayAttributes.from_sets(PackedStringArray([SET_ID]))
	if not _expect(third != null and third.get_attribute(HEALTH) == null
			and third.get_attribute(&"vitals.armor").get_base() == 0.0 and first.get_attribute(HEALTH).get_base() == 250.0,
			"later settings affect only newly constructed owners"):
		return false
	watch = null
	return _expect(first.add_base(HEALTH, -1.0) == OK and changes.size() == 3, "releasing a watch ends its subscription")


## Builds one actor using the same component configuration stored by the Inspector.
func _spawn_actor() -> AttributeActor:
	var actor: AttributeActor = AttributeActor.new()
	var abilities: AbilitySystemComponent = AbilitySystemComponent.new()
	abilities.name = "AbilitySystemComponent"
	abilities.attribute_sets = PackedStringArray([SET_ID])
	actor.add_child(abilities)
	root.add_child(actor)
	return actor


## Verifies scoped scene access, independent state, and retained handles after freeing the actor.
func _exercise_component() -> bool:
	ProjectSettings.set_setting(SETTING, _definitions(true))
	var actor: AttributeActor = _spawn_actor()
	var sibling: AttributeActor = _spawn_actor()
	var stats: GameplayAttributeSet = actor.health_set
	if not _expect(stats != null and stats.get_id() == &"vitals" and actor.initial_health != null
			and actor.initial_health.get_current() == 0.0 and sibling.initial_health != null,
			"the actor can obtain its configured set during Ready"):
		return false
	var monitor: GameplayAttributeWatch = stats.watch_attribute(&"health")
	if not _expect(monitor != null and stats.set_base(&"health", 100.0) == OK
			and stats.add_base(&"health", -25.0) == OK and monitor.get_value().get_current() == 75.0
			and sibling.health_set.get_attribute(&"health").get_current() == 0.0
			and stats.get_attribute(HEALTH) == null and stats.set_base(HEALTH, 5.0) == ERR_INVALID_PARAMETER,
			"seed actor health in code, keep sibling state isolated, and scope set lookups to local IDs"):
		return false
	if not _verify_watch_release(stats):
		return false
	actor.free()
	sibling.free()
	return _expect(stats.get_attribute(&"health") == null and monitor.get_value() == null
			and stats.set_base(&"health", 1.0) == ERR_UNAUTHORIZED, "retained views stop accessing a freed actor")


## Either watch can free the other while the publisher is iterating its listeners.
func _verify_watch_release(stats: GameplayAttributeSet) -> bool:
	var watches: Array[GameplayAttributeWatch] = [stats.watch_attribute(&"health"), stats.watch_attribute(&"health")]
	var deliveries: Array[int] = []
	for index: int in range(watches.size()):
		watches[index].changed.connect(func(_before: GameplayAttributeValue, _after: GameplayAttributeValue) -> void:
			deliveries.append(index)
			watches[1 - index] = null
		)
	var status: Error = stats.set_base(&"health", 70.0)
	watches.clear()
	return _expect(status == OK and deliveries.size() == 1,
			"releasing one copied watch during publication leaves the other watch and its edit valid")


## Exits with an observable pass marker or a failure code.
func _verify() -> void:
	if not _exercise() or not _exercise_component():
		quit(1)
		return
	print("CRIMSON_GAMEPLAY_ATTRIBUTES_RUNTIME_PASSED")
	quit()
