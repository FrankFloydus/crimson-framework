<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Default player controls

## Edit the demo player in Godot

Open the [demo's On Foot map](../project/demo/player/on_foot.tres) in the Inspector. In **Mappings**, expand the entry whose **ID** is `jump`. In **Bindings**, expand the keyboard binding and edit its **Event** to change the jump key. Save and start a new session to try it. Select **Controls** in the [player scene](../project/demo/player/stationary_player.tscn) and expand **Player Setup** to change action assignments, look sensitivity, or pointer capture.

Select **Mover** and open **Controller → Controller Style** to choose how movement input and camera look turn the body. Hover any option in the dropdown to see what it does. **Forward Direction** appears in the same group for **FPS / Shoulder Aim**. Expand **Ground Movement** for **Move Speed**, acceleration, deceleration, friction, and gravity. **Air Movement** contains air control, **Jumping** contains takeoff speed, and **Network** contains **Predict Movement**. The client and dedicated server use the same authored movement settings.

For another scene, add one `MoverComponent` and one `ControlsComponent` directly under its `CharacterBody3D` root. Assign an `InputLayout` to **Controls**. For fixed-world sessions, enable `.player_controls()` on the Game setup. When the local actor is ready, call `ControlsComponent.for_body(body).boot(view)` with the selected camera. The boot call also accepts the existing `Controls` handle when the game UI and the new body need to share bindings.

## Choose how the body turns

These styles are for upright characters. Place the camera at eye height for first person, or use your own camera pivot or arm for third person. Give the player a visible body mesh to see it turn.

In the demo, the third-person and Lock-On styles open with the local model visible. Press **F6** or click the **right stick** to switch views. In Lock-On, press **L** or click the **left stick** to select or release the north doorway. In LAN play, the other window shows your model's replicated facing. [Demo walkthrough](../project/demo/README.md#configure-the-multiplayer-player)

| Controller Style | What the player sees |
| --- | --- |
| **FPS / Shoulder Aim** | With **Forward Direction** set to the default **View Heading**, the body immediately faces where the camera looks horizontally. W moves that way and A sidesteps left. |
| **Free-Orbit Third Person** | Turn the camera while standing still and the body keeps its facing. Hold A and the character starts moving to the camera's left while the body turns to face that direction. |
| **Forward-Only Third Person** | Hold A and the body turns toward the camera's left, then moves forward along the body's facing. A turn more than 90° away requests zero speed. Forward speed increases as the body lines up. Existing momentum still follows the mover's braking and air control settings. |
| **Lock-On** | With a target selected, the body keeps facing it. Hold A or D to circle it, and S to back away. Camera look remains independent. With no target, the body faces the view and A or D sidesteps relative to it. |

The two third-person turning styles turn at up to 540° per second. **Forward Direction** applies to **FPS / Shoulder Aim**. A standalone controller set to **Character Heading** lets your game set the body rotation. The other controller styles use the view heading or, during a lock, the target heading.

For Lock-On, keep the `PlayerControl` returned by boot. When your game's targeting logic selects a node, call `player.lock_on(target)` and check the returned `Error`. Call `player.clear_lock_on()` when the player cancels. The controller returns to view-facing movement if the target leaves the scene. The network carries the owning player's requested facing angle. The game owns target selection, visibility checks, and any authoritative combat rules.

## Attach a profile directly

Use an ordinary `CharacterBody3D` with a collision shape and a child view. The view can be the camera or a pivot containing the camera. The profile handles look and steering before the mover advances physics.

```gdscript
extends CharacterBody3D

## Movement and look settings for this local player.
@export var profile: PlayerControlProfile

@onready var player := profile.boot(self, $View)
```

Boot on a mounted body. `player.is_ready()` reports whether the controller is live, and `player.get_message()` gives the cause of a failed boot. A game should boot this profile for its local player. An AI or scripted object can use [Mover](../src/modules/mover/README.md) directly.

## Author the profile in Godot

Create an **InputLayout** resource. Add the addon's `addons/crimson-framework/input/fps.tres` to **Maps**. It supplies `move` (WASD and left stick), `jump` (Space), `mouse_aim` (Mouse XY), and `stick_aim` (right stick). Save a copy of the map in your project to customize its mappings and bindings in the Inspector. The W binding uses **Swap Axis** and **Invert** for negative Y. A mouse-motion binding can read Mouse X, Mouse Y, or both. [Input guide](../src/modules/input/README.md)

Create a **PlayerControlProfile** resource and assign the layout under **Input Layout**. Customize the action assignments or look settings only when the defaults need to change. Use the movement fields on the profile when it supplies movement settings directly.

| Inspector field | Default value and meaning |
| --- | --- |
| Input Layout | Your InputLayout resource |
| Movement Rule | Empty for default grounded locomotion. A profile used as an explicit game movement override can use a grounded MotionRule for custom tuning. When a standalone controller uses a scene's MoverComponent, the scene component supplies movement tuning. |
| Move Action | Select **Move** from the layout's Vector2 actions. |
| Jump Action | **Jump** when the layout supplies it. Choose a Button action, or **None** to disable jump input. |
| Controller Style | **FPS / Shoulder Aim** by default. Hover an option in the dropdown to see its movement and facing behavior. |
| Forward Direction | Used with **FPS / Shoulder Aim**. **View Heading** follows the camera or view pivot. **Character Heading** uses the body's heading in standalone Player Control and its spawn heading in Game sessions, including solo. **World Axes** uses the world's negative Z axis for forward. |
| Move Speed | `6.0 m/s` at full input |
| Predict Movement | Off. Turn it on in compatible fixed-world games to predict grounded capsule movement against static geometry. The demo enables it on the server and the client. |
| Mouse Look Action | Select **Mouse Aim** from the relative mouse actions. Choose **None** to use a separate mouse look implementation. |
| Mouse Sensitivity | `0.002 rad/px` |
| Stick Look Action | Select **Stick Aim** from the Vector2 actions. Choose **None** to use a separate stick look implementation. |
| Stick Look Speed | `2.5 rad/s` at full deflection |
| Capture Pointer on Boot | Off. Turn it on for a captured first-person pointer when the window has focus. |

Assign the profile to your ordinary player script's exported property.

## Author the same defaults in code

```gdscript
var layout := InputLayout.create().add_move(&"move") \
    .add_button(&"jump", KEY_SPACE) \
    .add_mouse_motion(&"mouse_aim").add_right_stick(&"stick_aim") \
    .add_button(&"settings", KEY_ESCAPE, JOY_BUTTON_START)
layout.click_to_capture = true

var profile := PlayerControlProfile.configure(layout)
profile.controller_style = MoverComponent.FPS_AIM
profile.forward_direction = PlayerControlProfile.VIEW
profile.move_speed = 6.0
profile.capture_pointer_on_boot = true
var player := profile.boot(body, view)
```

## Share input with the game UI

For a player who can respawn, keep the controls on a persistent local owner and supply them on each body attachment.

```gdscript
var controls := Controls.boot(self, profile.input_layout)
var player := profile.boot(body, view, controls)
settings.setup(controls)
```

The settings screen can call `controls.suspend_while_visible(self)` to suspend local steering and release the pointer while it is open. It obtains the same live binding store through `Rebinding.open(controls, self)`. Use `keys.slots()` to build rows. The row's capture handler calls `await keys.rebind(slot)` and shows the typed result. The demo's [Settings scene](../project/demo/ui/control_settings.tscn) uses the stable `move/key_up` slot to demonstrate rebinding W while leaving the stick binding available.

See [Networked player movement](MOVEMENT_NETWORKING.md) for the authority configuration and how clients receive movement.
