<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson public API

Use `CrimsonGame` to start local play, host a listen game, or join a host. The same entry point also supports coordinated matches with registered worlds, travel, and checkpoints. Configure it in the Inspector or through a fluent definition. Framework services and orchestration run in the native Godot 4.7 GDExtension.

## Public API design guidelines

Crimson takes its API style from Laravel's fluent configuration, focused facades, and composable queries. These rules apply to customer-facing changes across the framework and demo.

- Start with the code a game developer should write for the common case. Use domain verbs and defaults. A facade carries context forward and handles wiring, authority, and coordination. Each fluent step should express a real choice, such as naming a save or selecting a destination.
- Keep setup declarative and scoped to the game, match, or player that owns it. Inspector setup and code setup use the same configuration and validation. Keep reusable builders safe to branch.
- Make reads and effects predictable. Queries and previews read state. A fluent mutation starts at an explicit terminal such as `submit()`. Document when it takes effect, what success means, and how callers receive a result, failure, or cancellation. Reuse the operation handle for asynchronous workflows where it fits.
- Preserve direct module APIs and explicit extension points for fine-grained control. The facade and direct calls use the same underlying rules and validation. Expose typed Godot bindings and use documented DTOs for flexible payloads.
- Show the smallest common call site and how a game can customize it. Verify both through their normal entry points and update the affected public documentation. Mark planned signatures as proposals until the bindings exist.

[Laravel application bootstrap](https://laravel.com/framework/docs/routing#routing-customization) · [Laravel service providers](https://laravel.com/docs/13.x/providers) · [Laravel query builder](https://laravel.com/docs/13.x/queries) · [Laravel fluent HTTP client](https://laravel.com/docs/13.x/http-client)

## Start a game

Use `Crimson.game()` to get the active game when the process has one registered `CrimsonGame`. A process with several games can select a primary with `Crimson.set_default_game(game)` or call `Crimson.for_node(node)` from within a game's subtree. These lookups do not extend a game's lifetime.

`Crimson.scenes().get_world()` reads the primary game's current mount. In a coordinated game, `await Crimson.scenes().load(world_id).wait()` travels through the same authority checks and Scene Loader as `game.travel_to(world_id)`. A scene service handle expires when its owning game scope leaves the tree.

Game setup supplies native Session and Scene Loader services by default. To customize either service, subclass `CrimsonServiceProvider` and override `_create_session()` or `_create_scene_loader()` to return a fresh detached node. Set **Service Provider** on the definition or call `definition.with_provider(provider)` before attaching the game. The game injects the returned nodes into its normal lifecycle and owns them after attachment.

Use the Session factory to select a custom gameplay transport. The adapter creates the peers. Crimson still owns admission, readiness, and teardown.

```gdscript
class_name MyGameProvider
extends CrimsonServiceProvider

func _create_session() -> CrimsonGameSession:
    var session := CrimsonGameSession.new()
    if session.set_transport_adapter(MyTransport.new()) != OK:
        session.free()
        return null
    return session
```

`MyTransport` extends `CrimsonSessionTransport`. Its server and client factories return a typed `CrimsonSessionTransportResult` carrying either an initialized peer or the opening error. The default is `CrimsonENetSessionTransport`. See the [Session transport contract](../src/modules/session/README.md) and the [demo's scripted ENet adapter](../project/demo/tests/session_smoke.gd) for a provider that exercises real host and join through the public game actions.

Add `CrimsonGame` to a persistent main scene. In its **Definition**, set **Starting World**, **Starting World Scene**, and **Player Scene**. Place at least one `CrimsonSpawnPoint` in the world. For listen co-op, add **Multiplayer Rules**. The [demo boot scene](../project/demo/main.tscn) uses this Inspector setup with the [Transfer Hall definition](../project/demo/session/transfer_hall.tres).

The same configuration in code uses a branchable definition. `create()` returns a detached node named `Game` with an independent definition. Add it to the persistent scene, then start it from a menu action.

```gdscript
var game: CrimsonGame

func _ready() -> void:
    game = (
        CrimsonGame.configure()
        .with_starting_world(&"transfer_hall", preload("res://demo/hub/starting_hub.tscn"))
        .with_player(preload("res://demo/player/stationary_player.tscn"))
        .with_multiplayer(
            CrimsonMultiplayerRules.listen_coop()
            .with_max_players(4)
            .allow_join_in_progress()
            .on_port(24830)
        )
        .create()
    )
    add_child(game)
```

| Game action | Contract |
| --- | --- |
| `await game.play_solo().wait()` | Mount the starting world and spawn the local player offline. |
| `await game.host_game(bind_address).wait()` | Mount the world and spawn the listen host, then open admission. The bind address defaults to `*`. |
| `await game.start_dedicated(bind_address).wait()` | Mount the authoritative world and open admission with every seat available to guests. The bind address defaults to `*`. |
| `await game.join_game(address).wait()` | Connect at the configured port, mount the authority's active world, and apply the acknowledged player roster and local spawn. |
| `game.leave()` | Cancel a pending start or retire the mounted session. |

Each start returns a `CrimsonGameOperation`. Await `wait()` because Scene Load prepares and mounts the world asynchronously. Inspect `result.ok`, `result.code`, and `result.message`. A successful start reaches `RUNNING`. Read the world with `get_world()`, the local actor with `get_local_player()`, or any actor by connection with `get_player(peer_id)`. A dedicated server has no local actor. Observe `session_ended(reason)` after teardown to return to the menu.

### Multiplayer rules

Use `CrimsonMultiplayerRules.listen_coop()` for drop-in play. It defaults to four total players and UDP port `7000`. `max_players` includes the listen host, so four players permit three guests. Dedicated hosting makes all four seats remote. Listen starts support 2 through 64 players. Dedicated starts support 1 through 64. An empty rules resource reference gives the definition solo play. Drop-in hosting and joining require join in progress to be enabled. Use the coordinated lobby flow for a game whose admission closes before the first match starts.

Peers use the same `CrimsonGame` node path relative to the multiplayer root, starting world ID, packaged scene layout, and player cap. The authority owns admission, the world epoch, and spawn assignment. Each late guest mounts the current world through its own Scene Loader. The host and already joined guests retain their active world. These actions cover a single shared 3D world. Use [coordinated game setup](#start-a-coordinated-game) for registered destinations, travel, checkpoints, and game-owned replicated state.

### Spawn and player components

Place `CrimsonSpawnPoint` nodes inside the world, anywhere beneath spatial parents. Each supplies an authored position and facing direction. Direct `Marker3D` children can supply additional placements from one spawn component. Author clear, distinct poses for the expected number of simultaneous players. Crimson automatically chooses an unassigned placement and keeps it assigned until that actor departs. If every placement is assigned, the entering player receives a placement failure and the current session continues.

Add an optional `CrimsonLocalView` inside the player scene. Use the Inspector there to assign its **Camera** and **Hidden Visuals** references from the same scene. The framework activates the local camera and hides the selected visuals on the local actor. It can select a sole camera automatically when a local-view component is absent.

## Use Steam or Epic Online Services

Both providers are part of the same Crimson extension. Build and stage the corresponding Windows SDK runtime using the [platform SDK guide](../vendor/README.md). Use the fixed-world game definition from [Start a game](#start-a-game) with the same scenes and rules on both peers.

### Steam

Set the provider's App ID, shared lobby namespace, and optional virtual port before creating the game. The default virtual port is `0`. The attached game initializes the provider through the signed-in Steam client.

```gdscript
var steam := CrimsonSteamProvider.new()
steam.app_id = 480 # Valve's Spacewar App ID for development.
steam.lobby_namespace = "my-studio-crimson-demo"
var game := definition.with_provider(steam).create()
add_child(game)
if not steam.is_ready():
    push_error(steam.get_initialization_error())
    return

var hosted: Dictionary = await game.host_online().wait()
if hosted.ok:
    steam.open_invite_overlay()
```

`open_invite_overlay()` returns `ERR_BUSY` while Steam reports that the overlay is unavailable or still loading. A graphical caller can retry for a bounded interval. `OK` means the request was passed to Steam. For a direct windowed development launch, pass `--crimson-steam-overlay=<AppID>` after Godot's `--` separator. Crimson initializes Steam before Godot opens its display and the matching provider reuses that process runtime. [Steam overlay readiness](https://partner.steamgames.com/doc/features/overlay#activating-the-overlay-from-your-app)

To invite someone selected in your game's own UI, call `steam.invite_player(selected_steam_id)` after hosting succeeds. Pass the recipient's decimal SteamID64 as a `String`. The provider requires its current private lobby and confirms local ownership with Steam. `OK` means Steam accepted the submission. The guest still accepts through Steam. [Steam private lobby invitations](https://partner.steamgames.com/doc/api/ISteamMatchmaking#InviteUserToLobby)

On the guest, connect `steam.invite_received` before starting the game and pass the received listing ID to `await game.join_online(listing_id).wait()` when the player accepts. The Steam host publishes a private lobby, so guests need a Steam invitation to become members. Use the same App ID, namespace, and virtual port on both peers. [Steam lobby API](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

### Epic Online Services

Use a private test deployment and the EOS Developer Authentication Tool for local development. Read the deployment values and client credential from local configuration. Keep the owner node alive for the duration of the online game so it can pump EOS callbacks.

```gdscript
var eos := CrimsonEOSProvider.configure(
    product_id, sandbox_id, deployment_id, client_id, client_secret
)
var login: Dictionary = await eos.login_developer(self, "127.0.0.1:6547", credential_name).wait()
if not login.ok:
    push_error(login.message)
    return

var game := definition.with_provider(eos).create()
add_child(game)
var hosted: Dictionary = await game.host_online().wait()
```

The host passes the returned `listing_id` to a guest signed into the same deployment. The guest uses `await game.join_online(listing_id).wait()`. EOS advertises the lobby within the configured deployment. Configure the optional sixth argument of `configure()` to force relay for a test. [Private deployment and live verification](../vendor/README.md#private-eos-deployment)

Call `game.leave()` to release the online session and its listing or membership. Both built-in providers currently cover listen-host sessions. The local dedicated launcher uses ENet. See the [live verification guide](../vendor/README.md#live-provider-verification) for the tests and account requirements.

## Use a custom online provider

Add a `CrimsonServiceProvider` before attaching the game. Return a detached session with a `CrimsonSessionAuthenticator` from `_create_session()`, and an independent `CrimsonOnlineDirectory` from `_create_online_directory()`. The provider can use the default ENet transport or install its own adapter on that session. The [demo online fixture](../project/demo/tests/online_smoke.gd) shows a directory and verifier implemented in the game.

```gdscript
var hosted: Dictionary = await game.host_online("127.0.0.1", "127.0.0.1").wait()
if hosted.ok:
    print(hosted.listing_id)

# In the guest process with the same game provider.
var joined: Dictionary = await game.join_online(listing_id).wait()
```

The first host argument is the address advertised to guests. Pass an empty address for a provider that supplies its own route. The second argument is the local bind address and defaults to `*`. `start_dedicated_online(public_address, bind_address)` uses the same directory with all seats available to remote players.

- The directory's `_publish(endpoint, world_id, request)` reports a unique listing ID through `request.publish(id)`. The authority opens admission after the listing succeeds. `_unpublish(id)` retires the listing when the game leaves.
- The directory's `_resolve(id, request)` completes with `request.resolve(endpoint)`. Both hooks can complete later on the scene thread and fail through `request.fail()`. Requests expire after 15 seconds. A directory should release its resources when `request.is_cancelled()` becomes true.
- The session authenticator answers and verifies challenges for the pending connection. The host receives the verified provider and subject through `CrimsonGameSession.get_peer_identity(peer_id)` by the time `peer_joined` fires. The guest reaches game readiness after the normal Scene Loader and roster acknowledgment.

Use a transport that protects credentials in transit whenever a verifier exchanges sensitive proofs. The demo fixture exercises the public contracts with test proofs on local loopback. [Session authentication and transport](../src/modules/session/README.md)

## Start a world session

Existing standalone `CrimsonWorldSession` integrations can continue to assign their world and player scenes in the **Definition** resource and call `start_offline()`, `host()`, or `join(address)`. This entry point uses a `SpawnPoints` container with direct `Marker3D` children in slot order.

The same setup in code uses a branchable resource. `create()` returns a detached node named `WorldSession` with a copy of the definition. Add it to a persistent parent before starting play. Configuration takes effect through the same startup checks as an Inspector resource.

```gdscript
var session: CrimsonWorldSession

func _ready() -> void:
    session = (
        CrimsonWorldSession.configure()
        .with_world(preload("res://levels/hub.tscn"))
        .with_players(preload("res://actors/player.tscn"))
        .create()
    )
    add_child(session)
```

Before `create()`, add `.with_local_view(^"Camera3D", ^"Visuals")` to select the local camera and hide its body mesh, or `.on_port(24830)` to set a game-specific port.

| Definition property | Contract |
| --- | --- |
| `world_scene`, `player_scene` | Locally available scenes rooted at `Node3D`. Each participant uses matching assets and spawn order. |
| `spawn_points_path` | World-relative path to the spawn container. Defaults to `SpawnPoints`. The container and its ancestors inside the world are `Node3D` nodes. Its direct markers determine the capacity, including the authority's first slot. Use 1–64 markers for offline play and 2–64 for multiplayer. |
| `local_camera_path`, `local_hidden_path` | Optional player-relative paths to a `Camera3D` and a `Node3D`. The session activates the camera and hides the selected node on the local avatar. Set these with `with_local_view()`. |
| `port` | The UDP port used for host and join. Defaults to `7000`. Set it with `on_port()`. |

| World session method | Contract |
| --- | --- |
| `start_offline() -> Error` | Mount the world, place the local player, and enter `RUNNING` on success. |
| `host() -> Error` | Start the listen host and enter the world. The remaining spawn slots stay open for admitted guests. |
| `join(address) -> Error` | Begin direct-address connection. `OK` means the request started. Wait for `RUNNING` before using the local player. |
| `leave() -> Error` | Cancel a pending join or leave an active world session. |
| `get_state()`, `get_port()` | Read `IDLE`, `STARTING`, or `RUNNING` and the configured UDP port. |
| `get_world()`, `get_local_player()`, `get_player(peer_id)` | Access mounted scene nodes for the active session. The session owns their lifetime. |

Observe `state_changed(state)` for readiness. `session_ended(reason)` arrives after teardown and permits the next start. Startup methods return immediate errors directly. Asynchronous end reasons include the [native session reasons](../src/modules/session/README.md#signals) and `spawn_timeout`, `invalid_roster`, or `spawn_failed`.

Each participant keeps the same session node path relative to its MultiplayerAPI root. The authority assigns slots and publishes the roster to admitted peers. The session reserves a child named `World` under itself and a child named `Players` under the mounted world. Gameplay movement and state remain game-owned. Use the coordinated game flow below for registered destinations, travel, and checkpoints.

## Start a coordinated game

Add `CrimsonGame` to a persistent scene such as your main menu or application root. Keep HUD and other persistent UI outside the world mounted by Crimson. For ENet, place the node at the same path relative to the multiplayer root in the host and client builds and include all catalog scenes in the exported game.

This example assumes the main script has an exported reference to its `CrimsonGame` node. The setup chain returns a `CrimsonGameDefinition` with the same fields available in the Inspector. Run this setup once for a new node, then start and restore matches through that node.

```gdscript
@export var game: CrimsonGame

func start_campaign() -> void:
    game.definition = (
        CrimsonGame.configure()
        .with_worlds({
            "harbor": {"scene_id": "harbor_map", "path": "res://levels/harbor.tscn"},
            "foundry": {"scene_id": "foundry_map", "path": "res://levels/foundry.tscn"},
        })
        .starting_in(&"harbor")
        .with_saves("my_game.campaign")
    )

    var error := register_gameplay()
    if error == OK:
        error = register_authority_state()
    if error != OK:
        push_error("Crimson setup failed: %s" % error_string(error))
        return

    var result: Dictionary = await game.start_offline().wait()
    if not result.ok:
        push_error(result.message)
```

Use a stable, game-owned world ID as the catalog key. Multiple worlds may refer to the same scene ID and resource path. The definition copies its catalog on assignment and retrieval. Assign the full dictionary after editing a detached copy. Each setup modifier returns a new definition, so a project can branch from a shared catalog. `with_saves(app_id, root = "user://crimson", schema_version = 1)` sets the checkpoint fields. The first valid configuration takes effect when opening a session or reading save history.

| `CrimsonGameDefinition` property | Contract |
| --- | --- |
| `app_id: String` | Stable checkpoint application identifier. The default is `"crimson.game"`. Set a distinct ID for your project. |
| `save_root: String` | Checkpoint root under `user://`. The default is `"user://crimson"`. |
| `schema_version: int` | Positive integer version of the integrated match checkpoint. The default is `1`. |
| `worlds: Dictionary` | Map from world IDs to dictionaries with `scene_id` and `path`. IDs are case-sensitive, 1–64 ASCII characters, start with a letter or digit, and can also contain periods, hyphens, and underscores. A scene ID consistently resolves to one canonical `res://` path ending in `.tscn` or `.scn`. |
| `starting_world: StringName` | A registered world to start when the caller omits a destination. |

## Game operations

`CrimsonGameOperation` exposes `is_finished()`, a read-only `result` property, and `completed(result)`. In GDScript, `await operation.wait()` returns the stored result for an operation that has already finished or awaits the result of pending work. Inspect `result.ok`, `result.code`, and any operation-specific fields. The coordinated `host()` and `join()` methods return a Godot `Error` immediately. Use `phase_changed` and `session_ended` to observe asynchronous join and disconnect.

| `CrimsonGame` method | Contract |
| --- | --- |
| `start_offline()` | Open the local session, bind the local player, and start the configured world. Returns an operation. |
| `open_offline() -> Error` | Open a local authority lobby. Use it before `restore()` to continue directly from a title screen. |
| `host(port, max_clients = 32, bind_address = "*") -> Error` | Open an ENet lobby. Start the match after the game has admitted and bound the intended participants. |
| `join(address, port, timeout_seconds = 10.0) -> Error` | Begin connection to an ENet lobby. `OK` means the connection attempt started. |
| `bind_player(peer_id, player_id) -> Error` | From the authority lobby, assign an admitted connection a stable game-owned player ID. The game supplies that identity from trusted code. |
| `start_match(world = &"")` | Start a world from the authority lobby. Uses `starting_world` when omitted. Returns an operation. |
| `travel_to(world)` | Activate a registered destination while keeping the live match records. Returns an operation. |
| `save_slot(profile, slot) -> CrimsonSaveSlot` | Address one slot for fluent save options, history, and restore. |
| `save(profile, slot, metadata = {}, autosave = false)` | Capture the committed state of a running authority and write a generation. Use `autosave = true` for a checkpoint slot. Returns an operation. |
| `save_history(profile, slot, before_sequence = "", before_id = "", limit = 16)` | Inspect a page of generations in a known slot on this instance's storage. Can be used from a title screen. Returns an operation. |
| `restore(profile, slot, sequence = "", generation_id = "")` | Resolve the destination from the checkpoint and activate the saved scene and stores. Omit both generation arguments for the current head. Returns an operation. |
| `leave() -> Error` | Begin releasing the transport, match, and mounted world. The game emits `session_ended(reason)` and returns to `IDLE` after cleanup, when it can be reused. |

Read readiness with `get_phase()` and the `IDLE`, `LOBBY`, `TRANSITIONING`, `RUNNING`, and `RECOVERING` constants. Use `world_ready(world_id, world_epoch)` when scene code needs a playable world. `get_world_id()` and `get_world_epoch()` report the last published world. `get_world()` returns the mounted root, which is valid only until replacement or teardown.

### Save, travel, and restore

This example saves the current Harbor match, travels to Foundry, and restores the exact Harbor generation returned by the save. A menu can call the same methods independently.

```gdscript
func checkpoint_round_trip(screenshot: Image) -> Dictionary:
    var quick := game.save_slot("campaign", "quick")
    var request := (
        quick.save()
        .named("Harbor beacon")
        .with_context({"region": "Harbor"})
        .with_preview(screenshot)
    )
    var saved: Dictionary = await request.submit().wait()
    if not saved.ok:
        return saved

    var travel: Dictionary = await game.travel_to(&"foundry").wait()
    if not travel.ok:
        return travel

    var generation: Dictionary = saved.value
    return await quick.restore(generation.sequence, generation.id).wait()
```

`CrimsonSaveSlot.save()` creates a `CrimsonSaveRequest`. `named(label)`, `with_context(fields)`, `with_preview(image)`, and `as_autosave()` each return a new request. `submit()` timestamps and schedules the save. For defaults, call `quick.save().submit()`. Use `quick.restore_latest()` to load the slot head and `quick.history(before_sequence = "", before_id = "", limit = 16)` to inspect generations. These calls use the same implementation as the direct methods on `CrimsonGame`.

A successful save has `value = {sequence, id}` once storage publishes the generation. Save history returns newest-first rows in `value`, a slot `status`, and a `next` cursor when more rows exist. Use the selected row's exact `sequence` and `id` for a load-menu action. `SavePresentation.describe(entry, pattern, utc_offset_minutes)` supplies the label, time, context, and optional preview for display. The game supplies slot IDs, keybindings, labels, screenshots, and checkpoint triggers. Slot history covers a known slot. See [save result shapes and limits](../src/modules/save/docs/API.md) and [SavePresentation](../src/modules/save/docs/PRESENTATION.md).

The integrated checkpoint captures all registered match stores and the durable world ID. Register the same store set and validators before restoring. The game decides what goes into those stores. To restore a player position or inventory, persist the corresponding records with stable IDs and bind the mounted scene nodes to those records. Store plain DTOs and encode engine values such as transforms and vectors as data. Scene nodes, connection IDs, input queues, and live Godot objects have runtime lifetimes.

## Authority gameplay and visibility

Before the match starts, use `register_command(name, handler, requires_primary = false)` on the authority. Each client uses `declare_command(name, requires_primary = false)` with the same name and primary-character requirement. Only the authority runs the handler. Call `submit(name, payload)` from local input on any ready participant. Crimson supplies the trusted player identity at the handler boundary, and validates the match, world epoch, command sequence, and primary-character assignment where required. The handler validates game-specific permissions and performs any state changes explicitly.

The sample below gives an admitted player permission to light the beacon in the active world. Call `register_gameplay()` and `register_authority_state()` on the offline or network authority before the first world. Each client calls `game.declare_command("world.light_beacon")`. Re-register match stores when starting a new match after teardown. The command registration belongs to the `CrimsonGame` instance and is reused across matches.

```gdscript
func register_gameplay() -> Error:
    return game.register_command("world.light_beacon", _light_beacon)

func register_authority_state() -> Error:
    var stores := game.get_transactions()
    var error := stores.register_store(
        "beacons", {"harbor": false, "foundry": false},
        _prepare_beacons, _validate_beacon_restore,
    )
    if error != OK:
        return error
    return game.set_shared_stores(PackedStringArray(["beacons"]))

func light_current_beacon() -> Dictionary:
    return await game.submit("world.light_beacon").wait()

func _light_beacon(player_id: String, _intent: Dictionary) -> Dictionary:
    var stores := game.get_transactions()
    var edit := stores.begin({"player_id": player_id})
    var error := edit.add_change(
        "beacons",
        {"world_id": String(game.get_world_id())},
        stores.get_store_revision("beacons"),
    )
    if error != OK:
        return {"ok": false, "code": "InvalidBeaconChange"}
    return stores.commit(edit)

func _prepare_beacons(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
    for operation: Dictionary in operations:
        var world_id: String = operation.get("world_id", "")
        if not current.has(world_id):
            return {"ok": false, "message": "The world has no beacon."}
        current[world_id] = true
    return {"ok": true, "state": current}

func _validate_beacon_restore(candidate: Dictionary, _context: Dictionary) -> Dictionary:
    var valid := (
        candidate.size() == 2
        and candidate.get("harbor") is bool
        and candidate.get("foundry") is bool
    )
    return {"ok": valid, "message": "Invalid beacon checkpoint." if not valid else ""}
```

The public coordinator can stage multiple changes from game-owned and framework stores in one commit. For example, a pickup handler can call `CrimsonInventoryTransactionAdapter.queue_place()` on the same transaction that consumes a world pickup. Store preparation and restore validation are synchronous and work on detached data. See [the transaction contract](ARCHITECTURE.md#transaction-contract).

For multiplayer, the server can bind a remote peer after verifying its game-specific identity. Connect to `peer_joined(peer_id)` to begin that process and call `bind_player(peer_id, verified_player_id)` on the authority when the identity is known. A listen host using local commands also binds its own peer, ID `1`. Read the local binding and current primary with `get_local_player_id()` and `get_local_character_id()`. These return an empty string when the relevant identity is unavailable. Client code can listen to `projection_updated(revision, baseline)` and read the detached visible state with `get_projection()`. It can observe the authority's result of a command separately from presentation of the resulting projection.

- `set_shared_stores(store_ids)` publishes the complete contents of explicitly selected stores to every bound client. Use it only for state all participants may see. By default, the game contributes no shared store data.
- `set_projector(callback)` gives the authority finer visibility control. The synchronous callback receives `(peer_id, player_id, world_epoch, committed_revision)` and returns `{ok, view}`, with `view` a dictionary of visible records keyed by stable strings. This projector takes precedence over the shared-store list. Crimson reserves `crimson.context` for its internal command context and removes it from the public view.
- Replication accepts bounded plain DTOs and is intended for committed gameplay state. Its public [view and delivery contract](../src/modules/replication/README.md#view-and-delivery-bounds) gives size and depth limits. Games supply their own high-frequency movement, prediction, and transient presentation event transport.
- Integrated ENet admission happens in the lobby. Starting a world requires all connected remote peers to be bound. A world becomes ready after the scene and required peer baselines are confirmed. A failure before publication keeps the prior world available. A synchronization failure after the commit enters `RECOVERING` and closes commands. The current facade supports leaving and starting a new session from that state.

## Direct APIs and extension points

A project may use the modules directly or start with the game facade and use its public authority handles where it needs control.

| Need | API |
| --- | --- |
| Grid geometry, placement, stacks, equipment, containers, or trade | [Inventory API](../src/modules/inventory/README.md), including `SpatialInventory` and its [grid API](../src/modules/inventory/MODEL.md) |
| Custom checkpoint participants, migrations, backend, and save presentation | [SaveCoordinator API](../src/modules/save/docs/API.md) and [SavePresentation](../src/modules/save/docs/PRESENTATION.md) |
| Custom transport orchestration or a standalone managed scene | [Game Session](../src/modules/session/README.md) and [Scene Loader](../src/modules/scene_load/README.md) |
| Custom replicated views or lower-level synchronization | [CrimsonReplication](../src/modules/replication/README.md) |
| Authority-owned state, custom transactions, and primary possession | `game.get_transactions()` and `game.get_possession()` when configuring or running the authority. Reacquire the handles for each new match. See [architecture](ARCHITECTURE.md#module-map-and-apis). |

The public facade owns its composition. Its service children and RPC names are implementation details. The present integration points are the game definition, command handlers, game-owned stores and validators, trusted player binding, and projection policy. Direct module use exposes the lower-level capture and scene activation boundaries when a game needs to supply its own orchestration.

## Fluent API direction

Game definitions and save requests use fluent modifiers today. The examples below sketch future Godot bindings for inventory queries, inventory commands, and travel options.

```gdscript
var matches := inventory.filter(can_use).search("medkit").collect()
var selected := matches.first()
var move := inventory.move(selected).to(&"belt").first_fit().submit()

var travel := game.match.travel_to(&"foundry").at_spawn(&"north_gate").submit()
```

The `collect()` terminal would read one snapshot, and collection operations would refine that already-read result. A mutation would start at an explicit terminal such as `submit()`. Its options carry IDs and preconditions to the same authority and transaction path as direct calls. A GDScript predicate runs on data the caller can already read.

Future scope-specific handles and feature providers can make common workflows shorter while keeping wiring explicit. A feature should receive its declared dependencies when its game, session, match, or local-player scope starts. A custom provider could replace a documented contract at setup. The current facade has concrete services and the extension points above. Game-specific systems can already use native Godot nodes, scoped callbacks, and custom commands without a feature provider.
