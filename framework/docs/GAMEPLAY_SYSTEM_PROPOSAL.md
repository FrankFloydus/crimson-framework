<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Gameplay system proposal

Use the gameplay system to share rules between actions, sustained effects, numeric attributes, and presentation. For example, a stun effect can grant `state.control.stun`. A dash ability checks `state.control` before it starts, an attribute modifier can check the same condition, and a cue can play the stun presentation while the effect is active. The game owns those definitions and rules.

The [Gameplay Tags module](../src/modules/gameplay_tags/README.md) provides native values, Godot bindings, and an Inspector picker. Unreal Engine 5.8.4 Gameplay Tags and Gameplay Ability System are behavioral references. Crimson provides its own C++ implementation and API. Unreal's tag types also distinguish exact membership from inherited matching, and its abilities use tags to identify, require, block, and trigger actions. [Epic Gameplay Tags](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-tags-in-unreal-engine) · [Epic Gameplay Abilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine)

## Native tag foundation

Each game builds a catalog of valid tag names. `GameplayTag` is a detached value that holds a canonical path. `GameplayTagContainer` is a copyable set of explicitly added tags with directional ancestor matching. The catalog additionally distinguishes declared names from parents implied by those declarations. C++ callers can use the module directly with no game or scene lifecycle.

Canonical paths are the durable identity. Godot Resources store explicit names and resolve project rename redirects on load. Future checkpoint and replication adapters should also carry explicit names. Derived parent membership follows from those names. Build any replacement catalog or restored tag set before publishing it so invalid data can fail without partially changing gameplay state.

## Queries and authoring

Gameplay tag queries are reusable conditions with nested any, all, and none groups, including exact membership checks. An empty any group fails, and empty all and none groups pass. A query holds a snapshot of its terms and checks a source or target container without mutating it. [Epic Gameplay Tag Query](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/GameplayTags/FGameplayTagQuery?application_version=5.8)

The Godot bindings expose one tag or container as an exported Resource. The Inspector uses a searchable shared picker with multi-select for containers and single selection for tags. The project catalog stores declared names and redirects. Scripts use the same classes and operations through their Godot bindings.

## Owned tag state

Give the entity runtime a way to grant and revoke tags by source. If two effects grant the same tag, removing one effect leaves the tag present. Track explicit and inherited membership transitions independently so a predicate can react when an explicit parent is granted even if a child already implies it. Publish a detached snapshot to readers after a complete mutation. Keep the underlying container suitable for static sets such as ability requirements and effect identifiers.

## Ability system sequence

1. **Attributes and effects.** Start with the [Gameplay Attributes proposal](GAMEPLAY_ATTRIBUTES_PROPOSAL.md) for a Project Settings registry, generated C++ helpers, native base and current values, and authoritative replication. Then apply instant or sustained effects through a runtime entry point. Define application, ongoing, and removal conditions against source and target tags. Use ownership grants for sustained state and use separate tags to identify or classify the effect itself. Track duration, periods, and stacks as effect state. Attribute modifiers may use tag conditions when evaluating numeric values.
2. **Abilities.** The [Gameplay Abilities module](../src/modules/abilities/README.md) separates script definitions, actor-owned grants, and per-execution instances. Each activation receives a context and an optional script eligibility check. The component supports completion, cancellation, and revocation. Add costs and cooldowns through Gameplay Effects. Extend eligibility with actor, source, and target tags, add cancel and block policies, and route tagged gameplay events to eligible grants.
3. **Cues.** Route tagged activation, persistent presentation, one-shot execution, and removal events to game-owned audio, animation, or visual handlers. Resolve a more general registered handler through the tag's direct parents when the cue definition allows it. Bind the cue lifecycle to runtime results so a rejected activation has no presentation side effects.
4. **Session integration.** Route offline and multiplayer actions through the same authority rules. Use Crimson's match and network integrations to validate commands, publish recipient-scoped state, and restore state on reconnect and travel. Start with bounded validated tag names as the replication contract. Consider catalog indexes after measuring bandwidth and defining version and catalog agreement. Plan save migrations around canonical names and redirects.

Each milestone should have an isolated fixture with its own definitions. The first gameplay example should start an effect, observe that a second overlapping grant keeps the character stunned after the first expires, and show the dash becoming available after the last grant ends.
