<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Networked player movement

The demo's player scene enables prediction and reconciliation for grounded capsule movement in a fixed-world session. The owning client responds to movement and look immediately. The listen host or dedicated server decides the committed pose and collision result. Other players interpolate between received samples.

## Configure the same game on each peer

Add one `MoverComponent` and one `ControlsComponent` directly under the player scene's `CharacterBody3D`. In the mover's Inspector, turn on **Predict Movement**. Assign the layout on **Controls**. Edit the layout's map resources in the Inspector to change bindings. Pass the same scene on every peer. [Player controls setup](PLAYER_CONTROLS.md)

```gdscript
var game: Game
var controls: Controls

func _ready() -> void:
    game = Game.configure(LEVEL, PLAYER).lan(4).player_controls().boot(self)
    var result: GameResult = game.boot_result()
    if not result.ok:
        push_error(result.message)
        return
    game.world_ready.connect(_attach_local_player)

func _attach_local_player(_world: StringName, _epoch: int) -> void:
    var body: CharacterBody3D = game.get_local_player() as CharacterBody3D
    if body == null:
        return
    var component: ControlsComponent = ControlsComponent.for_body(body)
    if component == null or component.input_layout == null:
        push_error("Assign an Input Layout to the player's ControlsComponent.")
        return
    var view: Camera3D = body.get_viewport().get_camera_3d()
    if controls == null:
        controls = Controls.boot(self, component.input_layout)
        if not controls.is_ready():
            push_error(error_string(controls.get_error()))
            controls = null
            return
    var player: PlayerControl = component.boot(view, controls)
    if player == null:
        push_error("Check the player's ControlsComponent.")
    elif not player.is_ready():
        push_error(player.get_message())
```

Here `LEVEL` and `PLAYER` are the game's level and upright character scenes. Pass the local player's selected camera as its view. Start a dedicated server with `Game.configure(LEVEL, PLAYER).lan(4).player_controls().boot_server(self)` to read the same movement settings. On a playable peer, pass the same `controls` handle to the game UI. Use `player_controls(profile)` to supply explicit movement settings that take precedence over the scene's mover for that game session. [Default player controls](PLAYER_CONTROLS.md)

## Prediction and reconciliation

- The server uses the admitted peer and its current actor to select which body receives an input. A movement request supplies steering, aim, an optional jump press, and an optional lock-on heading for that tick. The server chooses body heading from its configured controller style. It also chooses speed, physics time, and collision results, and applies a jump only when the actor is on the floor at that step.
- The client records input at the physics rate and sends a bounded tail of recent commands in replaceable packets. The server advances at its configured rate and can recover a lost upload from a later packet that still includes the jump's tick. It can reuse steering for a missing command, while a jump always requires its own received tick. A press can expire if the link drops it beyond the retained history. Each correction includes the authoritative movement state and the last input tick accounted for by that state. The server clears stale steering when input stops.
- The owning client restores the committed position, velocity, floor contact, and body heading, then replays inputs newer than the acknowledgment. The restored heading determines where the next replayed turn starts. The camera keeps the owner's current look direction. Small position corrections ease out on the camera, while the corrected capsule takes effect immediately. A full periodic update can recover from a missing snapshot.
- The join protocol publishes a reliable roster and initial pose before a guest becomes ready. Subsequent roster changes also use reliable ordered delivery. Frequent pose snapshots can be superseded by newer samples. The join protocol verifies that the owning client and server agree on the prediction settings. World and actor identity fence old packets when a player leaves or rejoins.

The live and predictive drivers share the same controller step and takeoff calculation. The motion solver's settings signature includes acceleration, deceleration, friction, air control, jump speed, and effective gravity. The reliable roster also verifies that the local controller style matches the authority's. Reboot the game after editing the player scene, and use matching settings on every predicting peer.

Input uploads, roster poses, and recurring pose batches use the [shared binary codec](../src/modules/network/README.md#typed-binary-messages). Each input has two `float32` movement axes, two `float32` view angles, one byte each for the jump and lock flags, and a `float32` lock heading. Up to 24 inputs fit in a 528-byte payload. Each 28-byte pose has an `int32` peer ID and six `float32` fields for position, view angles, and body yaw. A roster entry adds an `int32` spawn slot. Godot encodes the surrounding RPC and owner correction. Session protocol 4 rejects older layouts before a guest applies these messages.

Lock-on submits only a world yaw. The authority accepts it as bounded steering intent in **Lock-On** mode and clears the held lock heading when the input stream expires. It receives no target identity. Gameplay code that makes a lock affect hits, abilities, or visibility must resolve and validate the target on the server. [Godot multiplayer sender and delivery rules](https://docs.godotengine.org/en/4.7/tutorials/networking/high_level_multiplayer.html) · [Riot's server-authority design](https://www.riotgames.com/en/news/peeking-valorants-netcode)

## Collision authoring

Use one centered, upright capsule on an unscaled `CharacterBody3D`. Keep the capsule, its collision mask, and the level collision geometry fixed for the session. The replay driver sweeps against static physics bodies, including baked or immutable CSG collision. It uses the project's default gravity with the movement rule's gravity multiplier. Leave **Predict Movement** off on the authority for live collision with moving platforms, conveyors, other players, or rigid bodies, and for Area gravity overrides. The authority falls back to live collision when its player body cannot use the replay driver. If the authority offers prediction and a guest requests it, mismatched acceleration, deceleration, ground friction, air control, jump speed, effective gravity, collider settings, or physics tick rate fail the join with a diagnostic. A guest can leave prediction off and interpolate the server's snapshots.

Use the same physics tick rate and level collision assets on every movement peer. The replay driver supports rates up to 120 Hz. A guest that has lost too much input history follows authoritative state until it can resume from a contiguous set of commands. When a stale input stream returns without enough history to recover the gap, the authority resumes at the oldest retained command and skips the expired prefix.

## Inspect a running game

Call `game.network_diagnostics()` to read a typed `GameNetworkDiagnostics` snapshot from the local process. It reports the game phase, connection role, one-based local player slot, ENet round-trip estimate when available, and the movement path. On an owning guest it also reports the authoritative position, input acknowledgments, and recent snapshot age. A predicted guest can inspect its local position, replay count, and correction distance. The [demo network debugger](../project/demo/README.md#network-debugger) displays those values while you play.

Use the debugger's **Simulator** to apply a profile or a one-off disruption, **Scenarios** to run timed stages, **Monitor** to see current behavior, and **Events** to review changes. Compare the local and server positions after stopping, when they should converge. During motion, they represent different points in time. The reported correction distance measures a reconciliation. The [demo test steps](../project/demo/README.md#test-prediction-and-recovery) cover baseline movement, throttling, delay and loss, spikes, scenarios, outage recovery, timeout, and rejoin.

The [headless multiplayer harness](../scripts/Test-Multiplayer.ps1) presses Space through a test-owned input layout. It checks local takeoff before a blocked upload recovers, the server's jump and landing, and another guest's view of that jump. It also checks that pressing again in the air and holding the key through landing produces one arc. A separate listen-server case exercises jumping with live collision and prediction disabled.

The same harness checks free-orbit with live collision and with prediction, forward-only steering with prediction, and lock-on through a conditioned link to a dedicated server. The owner, authority, and a separate observer compare both body and view heading. The observer also checks that the demo's player model is visible and follows the committed body heading. Additional admission cases check that mismatched controller styles and older session protocols fail before the guest can play.

The server accepts a jump flag only from the connection that owns the actor. Command ticks are consumed once, even when several packets carry the same tick. The bounded input window and upload rate also apply to jump requests. The wire flag carries no height, velocity, or ground state. A custom client can submit repeated requests, and the grounded rule can still accept a new attempt after each landing. Games that need a jump cooldown should enforce that rule on the authority. [Godot multiplayer sender and delivery rules](https://docs.godotengine.org/en/4.7/tutorials/networking/high_level_multiplayer.html) · [Riot's server-authority design](https://www.riotgames.com/en/news/peeking-valorants-netcode)

## Unreal Engine 5.8 Mover research

The installed Mover plugin separates input production, fixed-step simulation, frame restoration, and presentation in `MoverNetworkPredictionLiaison.cpp`. Its Network Prediction backend sends a bounded history of recent commands, pairs authority state with the last consumed client command, and replays later inputs after correction. `FMoverDefaultSyncState` also serializes the movement base for based movement. That last part requires collision history for moving platforms and is outside the static-world driver's contract. [Epic's Mover replication overview](https://dev.epicgames.com/documentation/unreal-engine/comparing-mover-and-character-movement-component-in-unreal-engine?application_version=5.8)

## Unity Netcode research

Unity's Multiplayer Tools window brings its Simulator, Runtime Network Stats Monitor, and other inspection tools together. The Crimson debugger follows the same workflow of choosing conditions and then observing their effect on a running session. [Unity Multiplayer Tools window reference](https://docs.unity3d.com/Packages/com.unity.multiplayer.tools@2.2/manual/tools-window.html)

Unity's **Connections Cycle** runs a series of presets for configured intervals. **Random Connections Swap** selects a preset at each interval. Crimson's scenarios use timed profile stages so you can repeat a known sequence during movement tests. Unity 2.2.12 implements its lag spike as a timed disconnect in `NetworkEventsApi.cs`. Crimson exposes both a timed outage that blocks packets and a spike that adds temporary delivery delay. Chrome DevTools informed the named presets and independent upload and download fields. Crimson applies those fields to UDP datagram payloads on the demo's local relay. [Unity Network Simulator](https://docs.unity3d.com/Packages/com.unity.multiplayer.tools@2.2/manual/network-simulator.html#network-scenarios) · [Unity Boss Room example](https://github.com/Unity-Technologies/com.unity.multiplayer.samples.coop/blob/main/Assets/Scripts/Utils/NetworkSimulatorUIMediator.cs) · [Chrome throttling profiles](https://developer.chrome.com/docs/devtools/settings/throttling#set-up-custom-network-throttling-profile)

Linux `netem` documents delay, loss, and rate limits with finite packet queues. The demo relay bounds each direction to 256 waiting datagrams and a five-second scheduling horizon. Monitor includes drops from both limits. It measures UDP payload rates on Godot's process loop, so an interval can include several packets released together after a slow frame. [iproute2 netem reference](https://github.com/iproute2/iproute2/blob/main/man/man8/tc-netem.8)

Reviewed the 111 unique pages linked from the Unity Netcode for GameObjects 3.2 manual table of contents. These were the main design inputs.

| Unity topic | Application to Crimson |
| --- | --- |
| [Authority](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/terms-concepts/authority.html) and [ownership](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/terms-concepts/ownership.html) | A player owns their input. The server remains the only authority for the committed pose and collision. |
| [Network time and ticks](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/advanced-topics/networktime-ticks.html) and [physics timing](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/advanced-topics/physics.html) | Sample local input, apply it before the authority physics step, and publish the result afterward. Distinguish simulation rate from snapshot rate. |
| [NetworkTransform](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/components/helper/networktransform.html) and [reliability](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/advanced-topics/message-system/reliability.html) | Buffer received poses. Send ongoing state as replaceable snapshots and keep roster delivery reliable. Sequence updates and bound their sizes. |
| [Scene synchronization](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/basics/scenemanagement/timing-considerations.html) | Complete scene mount and the initial actor baseline before publishing local readiness. Reject traffic for an old session or actor. |
| [Client anticipation](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/advanced-topics/client-anticipation.html) | Separate local presentation from the authoritative state. Immediate look alone does not implement translation prediction. |
| [Testing with artificial conditions](https://docs.unity3d.com/Packages/com.unity.netcode.gameobjects@3.2/manual/tutorials/testing/testing_with_artificial_conditions.html) | Verify separate processes, late join, input expiry, and behavior when movement packets arrive late or are lost. |
