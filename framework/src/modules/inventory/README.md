<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson inventory module

`register_inventory.cpp` registers the inventory module's Godot classes at scene initialization. The module builds into the shared Crimson Framework library. Its C++ code uses Godot's standard data and callback types. A host supplies the item records and stable instance IDs, and decides which actions to expose and which participants may change shared state.

The native `SpatialInventory` class and its `version = 1` snapshot schema come from the original `godot-spatial-inventory` add-on. Existing geometry and batch semantics are documented in [the grid API](MODEL.md). Only one extension registering `SpatialInventory` should be enabled in a Godot project. When bringing a project over from the standalone add-on, install Crimson and remove the standalone descriptor before opening Godot.

## Start from code

```gdscript
var bag := Inventory.grid(Vector2i(8, 5))
var placed := bag.add(&"medkit_17", Vector2i(2, 1))
var rows: Array[InventoryItemView] = bag.items()
```

`Inventory.grid()` creates an empty configured grid, and returns null for invalid dimensions. `add()` commits the first legal unrotated placement in row-major order. A full grid returns `ERR_CANT_CREATE`. Use `item_at(cell)` for one occupied cell. Typed views contain the game-owned ID and captured geometry. A later move does not change a retained view.

### Pick up a world item

Configure a backpack when booting a coordinated game. Once play succeeds, pass the local player's backpack to the script that handles pickup input.

```gdscript
var settings := InventoryConfig.backpack(Vector2i(8, 5)).pickups(WorldPickups.new())
var game := Game.campaign(starting_world).inventory(settings).boot(self)
var started: GameResult = await game.play()
if started.ok:
    var backpack := game.inventory().backpack()
    var picked_up: InventoryResult = await backpack.pick_up(&"medkit_17")
    if picked_up.ok:
        show_pickup(picked_up.item_id)
```

`WorldPickups` extends `InventoryPickupSource`. Implement `_register_stores(stores)` if the provider owns a source store. The authority calls it once for each new match coordinator. Implement `_resolve_pickup(player_id, pickup_id, stores)` to check game policy and read committed state. Return `InventoryPickup.take(item_id, footprint, source_store, source_edit)` to describe the item and the source consumption operation, or null to deny it. The source store's existing prepare callback checks that the item is still available and stages its removal. Keep these callbacks synchronous and put effects after commit.

The runtime routes pickup through the admitted player's command scope, finds a free cell, and commits the source and backpack changes together. A full backpack preserves the source. A retained backpack expires when its match retires. Its typed result reports the committed item, origin, and revision. The backpack store is private to the authority. Games choose what inventory data to project. [Offline pickup fixture](../../../project/tests/match_runtime/test_match_runtime.gd) · [Separate-process pickup fixture](../../../project/tests/match_runtime/inventory_network.gd)

## Item commands and stacks

`SpatialInventoryActions` implements explicit moves and swaps, quick transfers, stack merges, and stack splits. Attach a `SpatialInventoryStackAdapter` for host-owned quantity records. The adapter's `describe_stack(id)` callback returns the stack key, quantity, and maximum quantity. Its `apply_changes(rows, commit)` callback verifies the expected record values, stages the record changes, invokes the placement commit on the same thread, and rolls back if that commit fails. Use `invalidate_stack_records()` when the host changes quantities outside the adapter.

Keep configured policies, destination providers, and placement callbacks alive through the operation. A configured callback that becomes unavailable rejects the action. Pass an empty `Callable()` when you intentionally want an optional policy omitted. `find_items()` returns an empty list and reports an unavailable or malformed predicate to the console. Stack and trade commit diagnostics identify a host callback that reports success without running its required commit or disagrees with the commit result. Inspect the host records before retrying such an operation.

## Trade

`SpatialInventoryTrade` registers two actors with backpacks, manages the invitation and offer grids, and settles the exchange when both actors lock their offers. The host can supply item and participant eligibility, stack record callbacks, and a wallet. Version 2 trade snapshots preserve offers, reservations, escrow, and pending returns. Persist the host item records and backpacks alongside that snapshot. Use the native default arguments for first fit. A script supplying the sentinel explicitly can pass `Vector2i(-1, -1)`.

## Sorting placements

Create one `SpatialInventorySorter` per independent sort history. `preview(grid, pinned_ids = [], allow_rotation = true, placement_policy = Callable())` returns `error`, and on success `grid_size`, detached `placements`, and `changed_count`. Pinned IDs keep their original positions and orientations. The optional policy receives `id`, `footprint`, `origin`, `rotated`, `source`, `destination`, `swap_id`, and `action`. Return an empty `String` to accept. Preview and apply both check the policy, and apply checks that the grid still matches the preview baseline. `apply_preview()`, `cancel_preview()`, `has_preview()`, `can_undo()`, and `undo(placement_policy = Callable())` manage the current plan and single-level undo. Conflicting edits keep the undo available and preserve the live grid. An unavailable configured sort policy returns `ERR_UNAUTHORIZED` and reports the cause to the console.

## Integer balances

`SpatialInventoryWallet` provides `get_balance(actor, currency)`, `set_balance(actor, currency, amount)`, and `apply_balance_changes(changes, commit = Callable())`. Use positive integer actor IDs, nonempty `StringName` currencies, and nonnegative integer amounts. A change row contains `actor`, `currency`, `before`, and `after`. The wallet validates the whole batch and stages its changes before invoking the synchronous callback. A callback error restores all the expected balances. The host decides how to associate actor IDs with users and persist balances.

A custom wallet can override `get_balance` and `apply_balance_changes` in C++. For GDScript hosts, implement `_get_balance(actor, currency) -> int` and `_apply_balance_changes(changes: Array[Dictionary], commit: Callable) -> Error`. The override owns the same compare, stage, commit, and rollback contract. Rename public overrides from a standalone GDScript wallet to these virtual methods when migrating it to Crimson.

When a scripted balance callback fails or returns a negative amount, `get_balance()` reports the error and returns `-1`. A balance update that cannot read the expected balance returns `ERR_INVALID_DATA`. A malformed commit result also returns `ERR_INVALID_DATA`, with the native wallet restoring all staged balances.

## Containers and temporary staging

Use `SpatialInventoryContainers.register_inventory(grid, container_id = &"")` to associate each grid with its owning item. An empty container ID marks an independent grid such as a backpack. The registry's `placement_policy(context)` checks that item IDs are unique across registered grids and that a container stays out of its own contents and descendants. An optional `host_placement_policy` can apply game rules. Pass `validate_changes(rows)` the native placement batch before committing several ownership changes together. It checks the projected ownership graph after all rows have been applied.

Use `SpatialInventoryStaging.configure(size)` to initialize a tray. `stage_from(source, id, origin, rotated = false)` transfers a complete placed item into the tray and remembers its original grid, cells, and rotation. `place_item(id, destination, origin, rotated = false)` transfers it out. `restore_all()` uses one checked batch to return the staged items. A failed batch leaves the tray intact and reports `last_reason`. Wire the registry's `placement_policy` to the tray's `drop_policy` and `validate_changes` to `batch_policy` when using a tray with nested storage.

## Equipment

`SpatialInventoryEquipment.configure(backpack, slots)` associates named slots with a backpack. The optional `slot_requirements(id, requested_slot)` callback returns all occupied slots or a denial string. Each equipped item has one record even when it occupies several slots. The optional `move_policy(context)` checks equips and returns, including gear displaced by a swap.

Use `preview_equip` or `preview_unequip` to get a detached preview. Pass the preview as `expected` to `equip` or `unequip` to require that the reviewed result still matches. A successful change publishes the backpack and equipment state together. Save `export_snapshot()` alongside the backpack and the host's item records. To restore, configure an empty equipment endpoint with the restored backpack, apply the same slot rules, and call `import_snapshot(snapshot)`.
