<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Scene Load Manager

`CrimsonSceneLoadManager` is a native Godot node for replacing a managed world offline or across connected multiplayer peers. It uses the `MultiplayerAPI` for its own scene-tree branch. `CrimsonGameSession` or the game can supply that transport.

## Setup

1. Keep one manager per multiplayer scope at the same relative node path on every peer. Use a persistent scene-tree branch for the manager, session, and world container. Keep that branch in the main process thread group.
2. Add the world container to the tree and call `set_world_container(container)`. The manager owns the container's child named `World`. Keep the container and manager outside that child and reserve the name for the manager.
3. Call `register_scene(scene_id, path)` on every peer before the session starts. IDs are case-sensitive and use one to 64 ASCII letters, digits, dots, hyphens, or underscores, with an alphanumeric first character. Paths refer to local PackedScenes under `res://`. Include them in the exported build.
4. In multiplayer, connect the players in the persistent lobby. Once the host has the intended group, call `request_load(scene_id)` on the host. In an offline game, call it locally.

## API

| Method | Behavior |
| --- | --- |
| `set_world_container(container) -> Error` | Assigns a live node in the same tree. An active world must stay with its existing container. |
| `get_world_container() -> Node` | Returns the configured container while it remains in the same tree. |
| `register_scene(scene_id, path) -> Error` | Adds a local scene mapping. Use canonical `res://` paths ending in `.tscn` or `.scn`. Duplicate IDs return `ERR_ALREADY_EXISTS`. |
| `request_load(scene_id) -> Error` | Starts one asynchronous load. In multiplayer, only peer 1 can start it. A client receives `ERR_UNAUTHORIZED`, overlapping loads receive `ERR_BUSY`, and an unregistered ID receives `ERR_DOES_NOT_EXIST`. |
| `request_coordinated_load(scene_id, publish_state) -> Error` | Starts the same transition with an authority-only state publication callback. `OK` means admission. The transition signals report its outcome. |
| `cancel_load() -> Error` | Cancels an active offline or host request before the commit decision. A client receives `ERR_UNAUTHORIZED`. |
| `clear_world() -> Error` | Unmounts the local world and releases loader-owned admission. An active transition returns `ERR_BUSY`. For network teardown, call this on each peer as it leaves or ends its session. |
| `is_loading() -> bool` | Reports whether a load or multiplayer activation confirmation is still pending. |
| `get_current_scene_id() -> StringName` | Returns the active managed scene's ID, or an empty ID when no managed world is present. |
| `get_current_scene() -> Node` | Returns the active managed world root while it belongs to the container. |
| `set_load_timeout_msec(timeout) -> Error` | Sets the preparation and confirmation deadlines for future requests. The range is 1 to 120,000 ms. The default is 15,000 ms per phase. Online clients use the host's deadline with a small allowance for delivery. |
| `get_load_timeout_msec() -> int` | Returns the configured timeout in milliseconds. |
| `set_manage_admission(enabled) -> Error` | Selects whether the loader manages host admission, defaulting to `true`. Changes require an idle loader with no owned gate. If `false`, the orchestrator must close admission before requesting a host transition and keep it closed through confirmation. |
| `get_manage_admission() -> bool` | Reports whether the loader manages host admission. |

| Signal | Behavior |
| --- | --- |
| `load_started(scene_id)` | A transition has been admitted. Gate gameplay and show loading feedback here. |
| `load_completed(scene_id)` | The offline scene has been attached, or the host has confirmed that all participants attached the multiplayer world. An orchestrator that synchronizes gameplay state can wait for its own baseline acknowledgments before resuming commands. |
| `load_failed(scene_id, error, message)` | Preparation failed, the transition was canceled, or peer membership changed before commit. The current world stays in place. |
| `load_sync_failed(scene_id, error, message)` | A failure occurred after the multiplayer commit decision or after a coordinated authority state publication. Keep gameplay gated while the game recovers the world or ends the session. |

## Coordinated state publication

Prepare the state candidate before requesting the transition and give the loader a zero-argument `Callable` that returns a Godot `Error` integer. The loader calls it once on the offline authority or host, after the local candidate and mount have passed validation and all captured participants have prepared the scene. A rejected callback must leave the current authority state intact. The current scene stays mounted, the staged scene is discarded, and `load_failed` reports the rejection.

An `OK` callback result means that state is published. The loader then broadcasts the commit decision and attaches the prepared scene, whose tree callbacks can read the new state. The callback and its synchronous observers must leave the scene tree and transport intact. Godot tree callbacks and network delivery can still fail after publication. `load_sync_failed` reports that recovery is needed, including in offline mode if the scene cannot mount after state publication.

Scene synchronization and gameplay baseline synchronization are separate. The orchestrator owns command admission and any baseline required by the game. With `set_manage_admission(false)`, it also owns when the transport accepts new connections.

## Multiplayer behavior

The host records the connected group at the start. Each participant asynchronously loads the local PackedScene and stages a detached instance on the main thread. Reliable RPCs carry a transition token and the allowlisted scene ID. Authority messages require peer 1. Readiness and activation reports require a peer in the transition group and the current token. Once every participant is ready, the host commits. Each peer replaces its own world on the main thread. The host confirms completion after all peers acknowledge their local activation.

While a network transition runs, the host transport refuses new connections. Admission stays closed for the active managed world. A first transition that fails before commit restores the previous admission setting. A post-commit failure keeps admission closed for recovery. Ending the transport, exiting the manager, or removing a world with no pending recovery releases the gate. Connect the intended group before loading and keep admission closed while the manager owns the world. Late join hydration and application state replication belong to the game.

CrimsonGame's immediate-play integration uses a native-only local request on this loader. The integration selects a packaged scene from its own definition, configures the detached candidate, and uses the same mount and cancellation lifecycle. On a late join it authorizes that request for the entering guest only. The running authority and other guests keep their current mounts while the integration confirms the entrant's scene and spawn baseline. The standalone methods above retain their coordinated behavior.

PackedScene scripts should defer gameplay initialization that requires remote peers until the manager confirms completion. Scene selection synchronizes asset IDs and world activation. The game supplies matching content on every peer and owns player state, scene-specific authority, spawn rules, and save/restore orchestration.

Canceled resource loads are collected when Godot's loader finishes. Instantiation and publication run on the main thread. See [Godot 4.7 ResourceLoader](https://docs.godotengine.org/en/4.7/classes/class_resourceloader.html) and [Godot 4.7 high-level multiplayer](https://docs.godotengine.org/en/4.7/tutorials/networking/high_level_multiplayer.html).
