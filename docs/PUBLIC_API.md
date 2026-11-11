<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson public API

Use a persistent `CrimsonGame` node to compose the framework into a game. Assign a `CrimsonGameDefinition` Resource in the Inspector or construct the same definition in code. The game node owns the session, scene mount, checkpoint operations, and current match. The game project defines records, commands, and visibility. All framework services and orchestration run in the native Godot 4.7 GDExtension.

## Start a game

Add `CrimsonGame` to a persistent scene such as your main menu or application root. Keep HUD and other persistent UI outside the world mounted by Crimson. For ENet, place the node at the same path relative to the multiplayer root in the host and client builds and include all catalog scenes in the exported game.

This example assumes the main script has an exported reference to its `CrimsonGame` node. The setup chain returns a `CrimsonGameDefinition` with the same fields available in the Inspector. Run this setup once for a new node, then start and restore matches through that node.

```gdscript
@export var game: CrimsonGame

func start_campaign() -> void:
    game.definition = (
        CrimsonGameDefinition.configure()
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

`CrimsonGameOperation` exposes `is_finished()`, a read-only `result` property, and `completed(result)`. In GDScript, `await operation.wait()` returns the stored result for an operation that has already finished or awaits the result of pending work. Inspect `result.ok`, `result.code`, and any operation-specific fields. Connection methods return a Godot `Error` immediately. Use `phase_changed` and `session_ended` to observe asynchronous join and disconnect.

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
