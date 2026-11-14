<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Input

`Controls.boot(owner)` exposes the project's Godot Input Map as local button actions. Attach it to an existing scene owner and use typed reads or an action callback. Owners retain their own state and binding overrides.

```gdscript
@onready var controls := Controls.boot(self)

func _ready() -> void:
    controls.on_press(&"interact", interact)
```

For composite actions, add standalone `InputActionMap` resources to an `InputLayout`. Select a map resource in Godot's FileSystem and edit **Mappings** in the Inspector. Expand a mapping to assign an action and edit **Bindings**. To start with first-person defaults, add `res://addons/crimson-framework/input/fps.tres` to the layout's **Maps**.

```gdscript
var layout := InputLayout.create().add_move(&"move").add_mouse_motion(&"look")
var controls := Controls.boot(self, layout)
controls.capture_pointer()
```

An `InputAction` resource describes a value, such as a button, one-dimensional axis, two-dimensional vector, or relative pointer motion. An `InputActionMapping` assigns that resource a script-facing ID and a list of physical bindings in one `InputActionMap`. Each `InputBinding` has a stable slot ID and can apply an ordered modifier chain. For example, a stick binding can apply Dead Zone followed by Swap Axis. For code-built layouts, `add_move()` supplies WASD and left-stick defaults, `add_right_stick()` supplies a normalized stick vector, and `add_mouse_motion()` supplies captured relative motion in unscaled screen pixels.

A 2D action reads an X and a Y component. A whole stick supplies both. A key, button, or one direction of a gamepad axis starts on positive X. Apply **Swap Axis** to send it to Y, then **Invert** on Y if it should point in the negative direction. Mouse motion bindings offer **Mouse X** for horizontal movement, **Mouse Y** for vertical movement, and **Mouse XY** for both. In code, `binding.set_mouse_motion(InputBinding.MOUSE_Y)` assigns vertical motion in screen pixels before modifiers run.

Use separate maps for situations such as on-foot play and vehicle controls. `Gameplay` is active at boot. For example, use `controls.set_map_enabled(&"Vehicle", true)` when a player enters a vehicle and pass `false` when they leave. Check the returned `Error` for an unknown map or inactive controls. `controls.is_map_enabled(&"Vehicle")` reports that local scope's current state. Enabled maps work together, so a shared key can drive actions in more than one map. Keep map, action, and binding IDs unique within their respective sets across the layout. Saved rebinds use the original binding slot IDs. Existing layouts authored with the `actions` array and code-built defaults continue to work. Use `resolved_actions()` to read the effective actions from either authoring model.

Switching a map clears the pending and published values for its actions. Held inputs wait for release or neutral before they can activate a newly enabled map.

Read `pressed()`, `released()`, `held()`, `axis()`, `vector()`, and `motion()` during physics. The router publishes values before `_physics_process()` and repeated reads use the same tick's values. Godot UI has the first opportunity to consume gameplay presses. Visible menus can suspend input without suspending simulation.

```gdscript
controls.suspend_while_visible(settings_panel)
settings_panel.show()
```

`capture_pointer()` and `release_pointer()` change the pointer immediately. Focus loss releases capture and clears pending input. Layouts can opt into capturing on an unhandled left click and can designate a button to release. The capture click and its release do not activate gameplay bindings.

Use an explicit device ID and `keyboard_and_mouse = false` for an additional gamepad-only player. Device ownership is checked at boot. An unassigned default scope automatically selects an unclaimed gamepad. `Controls.for_node()` finds an ancestor's attachment. Call `close()` to retire it before the owner leaves the tree.

Use [Rebinding](rebinding/README.md) to list editable slots, capture replacement inputs, reset to authored bindings, and save a player's overrides.
