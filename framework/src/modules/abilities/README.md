<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Gameplay Abilities

Use an ability script for an action an actor can start, complete, or cancel. An `AbilitySystemComponent` child of the actor grants a `GameplayAbility` script at a positive level and returns a grant handle. Pass that handle to `try_activate_ability()`. Each run has a fresh script instance and a `GameplayAbilityActivation` context with the component, actor, and level.

| Callback | Purpose |
| --- | --- |
| `on_can_activate(activation)` | Optional synchronous gate. Return a Boolean. |
| `on_activated(activation)` | Required action. Call `activation.end()` when the action completes. |
| `on_canceled(activation)` | Optional callback for interruption. Runs before the end callback. |
| `on_ended(activation, canceled)` | Optional cleanup. The flag is false on normal completion and true when interrupted. |

`cancel_ability()` ends a running ability and retains its grant. `clear_ability()` removes the grant and cancels any run it owns. A delayed action can check its context with `activation.is_active()` before applying a result. Actor teardown cancels active runs. The activation is already inactive in `on_canceled()` and `on_ended()`. Complete terminal cleanup before returning.

Save the definition as a GDScript or Crimson Visual Script based on `GameplayAbility`. In Crimson Visual Script, use the Gameplay Abilities picker entries to create the typed callback functions. See the [timed guard tutorial](../../../../docs/gameplay-abilities.mdx) for working authoring steps.

Open Ability Defaults from the saved script's Inspector or from its Crimson Visual Script toolbar. The editor uses the script UID to save its tag fields in the project. The grant captures a copy when `give_ability()` runs. Ability code can ask `activation.get_default_tags()` for a field and use the detached tag container in its gameplay rules. A valid field with no saved tags returns an empty container.
