# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends GameplayAbility

const SET_ID: StringName = &"vitals"
const HEALTH_ID: StringName = &"health"
const HIT_AMOUNT: float = 25.0


## A rejected candidate should never publish an in-tree lifecycle callback.
func _ready() -> void:
	get_parent().get_parent().set_meta(&"ability_mounted", true)


## Preflight runs against the actor before the execution is published.
func on_can_activate(activation: GameplayAbilityActivation) -> bool:
	var actor: Node = activation.get_actor()
	actor.set_meta(&"inert_preflight", not is_inside_tree() and not activation.is_active() and activation.end() == ERR_UNAVAILABLE)
	return actor.get_meta(&"allow_ability", true)


## Record the instance and optionally apply a hit and complete in the same callback.
func on_activated(activation: GameplayAbilityActivation) -> void:
	var actor: Node = activation.get_actor()
	actor.set_meta(&"last_ability", get_instance_id())
	actor.set_meta(&"last_activation", activation)
	actor.set_meta(&"activation_level", activation.get_level())
	actor.set_meta(&"active_in_callback", activation.is_active() and is_inside_tree())
	if actor.get_meta(&"finish_ability", false):
		var stats: GameplayAttributeSet = activation.get_component().get_attribute_set(SET_ID)
		actor.set_meta(&"hit_status", stats.add_base(HEALTH_ID, -HIT_AMOUNT))
		actor.set_meta(&"finish_status", activation.end())


## Records the distinct cancellation notification before shared cleanup.
func on_canceled(activation: GameplayAbilityActivation) -> void:
	var actor: Node = activation.get_actor()
	if actor == null:
		return
	var callbacks: Array = actor.get_meta(&"cleanup_callbacks", [])
	callbacks.append(&"canceled")
	actor.set_meta(&"cleanup_callbacks", callbacks)
	actor.set_meta(&"inert_in_cancel", not activation.is_active() and activation.end() == ERR_UNAVAILABLE)


## A retired token is inert even during its own cleanup callback.
func on_ended(activation: GameplayAbilityActivation, canceled: bool) -> void:
	var actor: Node = activation.get_actor()
	if actor == null:
		return
	var callbacks: Array = actor.get_meta(&"cleanup_callbacks", [])
	callbacks.append(&"ended")
	actor.set_meta(&"cleanup_callbacks", callbacks)
	actor.set_meta(&"ended_count", int(actor.get_meta(&"ended_count", 0)) + 1)
	actor.set_meta(&"last_canceled", canceled)
	actor.set_meta(&"inert_in_cleanup", not activation.is_active() and activation.end() == ERR_UNAVAILABLE)
