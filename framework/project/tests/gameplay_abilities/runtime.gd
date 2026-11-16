# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends SceneTree

const TRAINING_ABILITY: Script = preload("res://tests/gameplay_abilities/training_ability.gd")
const VITALS: String = "vitals"
const HEALTH: StringName = &"health"
const ABILITY_DEFAULTS: String = "crimson/gameplay_abilities/defaults"


## Starts after the addon and native ability bindings are loaded.
func _initialize() -> void:
	_verify.call_deferred()


## Creates one independent actor with a scene-configured Ability System component.
func _make_actor() -> AbilitySystemComponent:
	var actor: Node = Node.new()
	var abilities: AbilitySystemComponent = AbilitySystemComponent.new()
	abilities.attribute_sets = PackedStringArray([VITALS])
	actor.add_child(abilities)
	root.add_child(actor)
	return abilities


## Reports the assertion that failed in the isolated runner.
func _expect(condition: bool, problem: String) -> bool:
	if not condition:
		push_error("[Crimson] Gameplay Abilities fixture. " + problem)
	return condition


## Tests grant isolation, eligibility, explicit terminal transitions, and instancing.
func _exercise() -> bool:
	ProjectSettings.set_setting("crimson/gameplay_attributes/sets", {
		"version": 1, "sets": [{"id": VITALS, "attributes": [{"id": "health"}]}],
	})
	var abilities: AbilitySystemComponent = _make_actor()
	var sibling: AbilitySystemComponent = _make_actor()
	var actor: Node = abilities.get_parent()
	var sibling_actor: Node = sibling.get_parent()
	var stats: GameplayAttributeSet = abilities.get_attribute_set(&"vitals")
	var grant: int = abilities.give_ability(TRAINING_ABILITY, 3)
	var separate_grant: int = abilities.give_ability(TRAINING_ABILITY)
	var other_grant: int = sibling.give_ability(TRAINING_ABILITY)
	if not _expect(grant > 0 and separate_grant > 0 and separate_grant != grant and other_grant > 0
			and abilities.get_ability_level(grant) == 3 and abilities.get_ability_level(separate_grant) == 1
			and sibling.get_ability_level(grant) == 0 and sibling.try_activate_ability(grant) == ERR_DOES_NOT_EXIST
			and abilities.give_ability(TRAINING_ABILITY, 0) == 0 and abilities.give_ability(get_script()) == 0
			and abilities.get_child_count() == 0 and not actor.has_meta(&"last_ability"),
			"Grant creates an isolated spec and validates the script and level before execution."):
		return false
	actor.set_meta(&"allow_ability", false)
	if not _expect(abilities.try_activate_ability(grant) == ERR_UNAUTHORIZED
			and actor.get_meta(&"inert_preflight", false) and not abilities.is_ability_active(grant)
			and not actor.has_meta(&"ability_mounted") and not actor.has_meta(&"last_ability"),
			"A rejected preflight never mounts the ability or publishes an activation."):
		return false
	actor.set_meta(&"allow_ability", true)
	actor.set_meta(&"finish_ability", true)
	if not _expect(stats.set_base(HEALTH, 100.0) == OK and abilities.try_activate_ability(grant) == OK
			and not abilities.is_ability_active(grant) and actor.get_meta(&"active_in_callback", false)
			and actor.get_meta(&"activation_level", 0) == 3 and actor.get_meta(&"ended_count", 0) == 1
			and actor.get_meta(&"cleanup_callbacks", []) == [&"ended"]
			and actor.get_meta(&"inert_in_cleanup", false) and not actor.get_meta(&"last_canceled", true)
			and actor.get_meta(&"hit_status", FAILED) == OK and actor.get_meta(&"finish_status", FAILED) == OK
			and stats.get_attribute(HEALTH).get_current() == 75.0 and not sibling_actor.has_meta(&"last_ability"),
			"A fresh mounted ability can use its actor's set and end synchronously exactly once."):
		return false
	var first_instance: int = actor.get_meta(&"last_ability")
	var first_activation: GameplayAbilityActivation = actor.get_meta(&"last_activation")
	actor.set_meta(&"finish_ability", false)
	if not _expect(abilities.try_activate_ability(grant) == OK and abilities.is_ability_active(grant)
			and actor.get_meta(&"last_ability") != first_instance and first_activation.end() == ERR_UNAVAILABLE
			and abilities.try_activate_ability(grant) == ERR_ALREADY_IN_USE
			and abilities.try_activate_ability(separate_grant) == OK and abilities.is_ability_active(separate_grant),
			"Each activation gets a new instance and token, and a separate grant may run independently."):
		return false
	var revoked_activation: GameplayAbilityActivation = actor.get_meta(&"last_activation")
	if not _expect(abilities.cancel_ability(grant) == OK and not abilities.is_ability_active(grant)
			and actor.get_meta(&"last_canceled", false) and actor.get_meta(&"ended_count", 0) == 2
			and actor.get_meta(&"inert_in_cancel", false)
			and actor.get_meta(&"cleanup_callbacks", []) == [&"ended", &"canceled", &"ended"]
			and abilities.clear_ability(separate_grant) == OK and abilities.get_ability_level(separate_grant) == 0
			and actor.get_meta(&"ended_count", 0) == 3 and revoked_activation.end() == ERR_UNAVAILABLE
			and actor.get_meta(&"cleanup_callbacks", []) == [&"ended", &"canceled", &"ended", &"canceled", &"ended"],
			"Cancellation preserves the grant and revocation retires its active execution once."):
		return false
	if not _expect(sibling.try_activate_ability(other_grant) == OK and sibling.is_ability_active(other_grant),
			"A second actor can independently activate the same script class."):
		return false
	var orphaned: GameplayAbilityActivation = sibling_actor.get_meta(&"last_activation")
	sibling_actor.free()
	actor.free()
	return _expect(not orphaned.is_active() and orphaned.get_component() == null and orphaned.end() == ERR_UNAVAILABLE,
		"An execution token cannot access a destroyed actor.")


## Writes a versioned definition row using an obsolete path and the live script's UID.
func _write_defaults(script_uid: String, ability_tag: String) -> void:
	ProjectSettings.set_setting(ABILITY_DEFAULTS, {"version": 1, "entries": {
		script_uid: {"path": "res://previous_training_ability.gd", "tags": {
			"ability_tags": PackedStringArray([ability_tag]),
			"activation_blocked_tags": PackedStringArray(["state.control.stun"]),
		}},
	}})


## Verifies UID lookup, per-grant lifetime, and a typed detached read of a saved field.
func _exercise_default_tags() -> bool:
	var script_uid: String = FileAccess.get_file_as_string(TRAINING_ABILITY.resource_path + ".uid").strip_edges()
	if not _expect(ResourceUID.text_to_id(script_uid) != ResourceUID.INVALID_ID, "The training script has a stable UID."):
		return false
	ProjectSettings.set_setting("crimson/gameplay_tags/definitions", PackedStringArray([
		"ability.training.guard", "ability.training.hit", "state.control.stun",
	]))
	var abilities: AbilitySystemComponent = _make_actor()
	var actor: Node = abilities.get_parent()
	_write_defaults(script_uid, "ability.training.guard")
	var guard_grant: int = abilities.give_ability(TRAINING_ABILITY)
	_write_defaults(script_uid, "ability.training.hit")
	var hit_grant: int = abilities.give_ability(TRAINING_ABILITY)
	if not _expect(guard_grant > 0 and hit_grant > 0 and abilities.try_activate_ability(guard_grant) == OK,
			"A stale display path cannot prevent granting the script that owns the stored UID."):
		return false
	var guard_run: GameplayAbilityActivation = actor.get_meta(&"last_activation")
	var guard_tag: GameplayTag = GameplayTag.find(&"ability.training.guard")
	var hit_tag: GameplayTag = GameplayTag.find(&"ability.training.hit")
	var blocked_tag: GameplayTag = GameplayTag.find(&"state.control.stun")
	var detached: GameplayTagContainer = guard_run.get_default_tags(GameplayAbilityActivation.ABILITY_TAGS)
	if not _expect(detached.size() == 1 and detached.has_exact(guard_tag)
			and guard_run.get_default_tags(GameplayAbilityActivation.ACTIVATION_BLOCKED_TAGS).has_exact(blocked_tag)
			and guard_run.get_default_tags(GameplayAbilityActivation.TARGET_REQUIRED_TAGS).is_empty(),
			"The activation reads the configured fields and an empty container for an unset field."):
		return false
	detached.clear()
	if not _expect(guard_run.end() == OK and abilities.try_activate_ability(hit_grant) == OK,
			"Another grant can activate with the newly saved defaults."):
		return false
	var hit_run: GameplayAbilityActivation = actor.get_meta(&"last_activation")
	var latest: GameplayTagContainer = hit_run.get_default_tags(GameplayAbilityActivation.ABILITY_TAGS)
	var frozen: GameplayTagContainer = guard_run.get_default_tags(GameplayAbilityActivation.ABILITY_TAGS)
	actor.free()
	return _expect(latest.has_exact(hit_tag) and not latest.has_exact(guard_tag) and frozen.has_exact(guard_tag),
			"Changing editor data and modifying a detached container leave older grants intact.")


## Exits with a marker consumed by both native build variants.
func _verify() -> void:
	if not _exercise() or not _exercise_default_tags():
		quit(1)
		return
	print("CRIMSON_GAMEPLAY_ABILITIES_RUNTIME_PASSED")
	quit()
