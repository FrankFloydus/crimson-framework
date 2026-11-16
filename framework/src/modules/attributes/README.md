<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Gameplay Attributes

Use attributes for numeric gameplay state such as health, stamina, and movement speed. Define each set once in Project Settings and attach it to an AbilitySystemComponent on each character scene that needs its own values. A coordinated game can publish confirmed readings to the players allowed to see them.

## Author a set

Open **Project → Project Settings** and select **Gameplay Attributes**. Use **Add Set** to create `vitals`, then **Add Attribute** to create `health`. Set and display labels are optional. Use Replication to choose which players can see a match's values.

Save writes the registry under `crimson/gameplay_attributes/sets` in `project.godot`. The set ID and the attribute ID combine into `vitals.health`, which is used by runtime callers, saved state, and replication. Use lowercase IDs that start with a letter. Each segment supports up to 48 letters, digits, and single underscores. Retain published IDs when changing presentation labels or row order. Plan a versioned migration before renaming a shipped ID.

## Read and apply a hit

Make a `Button` scene with an `AbilitySystemComponent` child. Choose `vitals` in the child's Attribute Sets property and attach this script to the button.

```gdscript
extends Button

const VITALS_SET: StringName = &"vitals"
const HEALTH: StringName = &"health"
const STARTING_HEALTH: float = 100.0
const DAMAGE: float = 25.0

@onready var abilities: AbilitySystemComponent = $AbilitySystemComponent

var _vitals: GameplayAttributeSet
var _health_watch: GameplayAttributeWatch


func _ready() -> void:
    _vitals = abilities.get_attribute_set(VITALS_SET)
    if _vitals == null:
        text = "Choose the vitals set on AbilitySystemComponent."
        disabled = true
        return
    var status: Error = _vitals.set_base(HEALTH, STARTING_HEALTH)
    if status != OK:
        text = "Could not set starting health: " + error_string(status)
        disabled = true
        return
    _health_watch = _vitals.watch_attribute(HEALTH)
    _health_watch.changed.connect(_on_health_changed)
    _show_health(_health_watch.get_value())
    pressed.connect(_apply_hit)


func _apply_hit() -> void:
    var reading: GameplayAttributeValue = _health_watch.get_value()
    if reading == null:
        return
    var damage: float = minf(DAMAGE, reading.get_current())
    var status: Error = _vitals.add_base(HEALTH, -damage)
    if status != OK:
        push_error("Could not apply the training hit: " + error_string(status))


func _on_health_changed(_previous: GameplayAttributeValue, confirmed: GameplayAttributeValue) -> void:
    _show_health(confirmed)


func _show_health(reading: GameplayAttributeValue) -> void:
    if reading == null:
        text = "Add health to the vitals set."
        disabled = true
        return
    var health_points: float = reading.get_current()
    text = "Health %d · Take a hit" % roundi(health_points)
    disabled = health_points <= 0.0
```

Run the scene to see 100 health. Each press lowers the value by up to 25 until the button disables at zero. The local component starts every attached attribute at zero and this script sets the starting value. Use the scoped `GameplayAttributeSet` handle to access fields by their local IDs. Put rules such as damage limits in the game code that applies changes. Keep the watch reference for the lifetime of the subscription.

`GameplayAttributes.from_sets()` also creates an independent local owner for code without a scene node. This lower-level handle uses qualified IDs such as `vitals.health`.

## Generate C++ helpers

Click **Generate** in the Gameplay Attributes tab. For the `vitals` set, the editor saves the valid draft and generates `res://generated/gameplay_attributes/vitals_attributes.gen.hpp`. The header exposes `health_id`, `get_health()`, `get_health_base()`, `set_health_base()`, `add_health_base()`, and `bind_health_changed()` in `crimson::generated::vitals_attributes`. Pass the native component object to `find_set()` and check the returned `std::optional<Set>` before accessing its typed methods. Getters return `std::optional<double>`, and setters return Godot errors. The namespace functions also accept a lower-level `GameplayAttributes` handle. Keep the `Ref<RefCounted>` returned by the bind helper while listening.

The generated header calls the registered Godot API. Compile game code that includes it in the game's own extension. GDScript and Visual Script can call the generic bindings directly. When a set is removed or replaced, its retired header stays in the game project. Update its includes and uses before deleting it.

## Replication and ownership

The Attributes module owns the registry, standalone values, and typed readings. Enable the coordinated adapter with `GameConfig.attributes()` and get it from `Game.attributes()`. The adapter owns the match store, recipient views, and state updates. Choose Match in the component's Attribute Source property. Use `MatchAttributes.queue_add_component()` on the authority to attach sets and assign visibility by admitted player ID. Use `queue_base()` in the same transaction to set a starting value. Bind each peer's component to the same character ID through `bind_component()`. See [committed state and delivery bounds](../network/README.md#committed-state).

An authority view includes base and current. A client receives confirmed current values for the attributes in its view. Read a new display's value when binding it, then listen for changes. A value may become available or unavailable as the view changes.
