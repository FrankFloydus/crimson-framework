<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Mover

## Configure movement on a player scene

Add a `MoverComponent` child directly under the player's `CharacterBody3D` and select it in the scene tree. Edit these fields in the Inspector.

| Field | Behavior |
| --- | --- |
| Controller | **Controller Style** selects how the player body turns in response to look, movement, or a game-selected target. **FPS / Shoulder Aim** is the default and exposes **Forward Direction**. **View Heading** follows the camera or view pivot. **Character Heading** uses the body heading in standalone Player Control or the spawn heading in a Game session. **World Axes** uses world negative Z. [Controller styles](../../../docs/PLAYER_CONTROLS.md#choose-how-the-body-turns) |
| Ground Movement | **Move Speed** defaults to 6 m/s. **Acceleration** and **Deceleration** default to 40 m/s². **Ground Friction** defaults to 8/s. **Gravity Scale** defaults to 1. |
| Air Movement | **Air Control** defaults to 0.4 of ground steering acceleration. Zero preserves horizontal momentum until collision. Releasing the move input while airborne lets the character coast. |
| Jumping | **Jump Speed** defaults to 5 m/s upward. Set it to zero to disable jumping. The resulting height depends on gravity. |
| Network | **Predict Movement** enables client prediction for fixed-world play with static collision. Defaults to off. |

Hover a property label or its value control for help while tuning the component. Open **Controller Style** and hover an option to see how it responds to movement and look input.

Enable movement in a fixed-world game with `Game.configure(world_scene, player_scene).player_controls().boot(self)`. Attach the local input profile to the mounted player with `profile.boot(body, view)` after `world_ready`. Standalone `PlayerControlProfile.boot()` also reads a `MoverComponent` on its body. For code-first setup, a profile passed to `player_controls(profile)` can supply movement settings for a scene without the component.

The component owns the authored settings. `component.mover()` returns the existing live handle after movement attaches, or `null` when the body has no local runtime Mover. The [networked movement guide](../../../docs/MOVEMENT_NETWORKING.md) covers prediction and collision setup.

## Drive a body from code

Attach Mover to a body and supply the world-space velocity in metres per second. The body advances on the physics tick. Call `stop()` to clear steering, or submit another `move()` to replace it. A source can issue a command once or update it as its own direction changes.

```gdscript
@onready var mover := Mover.attach(self)

func start_travel() -> void:
    mover.move(Vector3.RIGHT * 3.0)
```

A `CharacterBody3D` uses its normal slide collision settings. A plain `Node3D` moves by transform. Other collision body types return `null` during attachment. For a grounded character, supply `Locomotion.ground()`.

```gdscript
@onready var mover := Mover.attach(self, Locomotion.ground())

func steer(world_direction: Vector3, speed: float) -> void:
    mover.move(world_direction * speed)
```

Grounded steering stays perpendicular to the character's configured up direction. It approaches the requested velocity using acceleration and ground friction. Releasing input on the floor brakes using deceleration plus speed-dependent friction. Gravity advances along the up axis, using the body's gravity and the rule's `gravity_scale`. Tune the same fields on a `MotionRule` for a standalone attachment. Call `use_rule(rule)` to apply a new snapshot at runtime.

Call `mover.jump()` once to request takeoff in a standalone grounded physics step. The request is consumed on that step and takes effect only if the body has floor support. In a fixed-world player session, the local Controls profile submits the jump action to the session's movement simulation.

The private driver advances at physics priority 100 in the body's main-thread process group. Submit steering earlier in that group to affect the same tick. `velocity` reports actual world-space travel from the latest completed step. Repeated `attach()` calls on the same body return its existing handle. `Mover.for_node(body)` resolves that handle without creating one. When the body exits the tree, its steering and motion state reset. The handle does not keep the scene body alive.
