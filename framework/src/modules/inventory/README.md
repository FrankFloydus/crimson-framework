<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson inventory module

`register_inventory.cpp` registers the inventory module's Godot classes at scene initialization. The module builds into the shared Crimson Framework library. Its C++ code uses Godot's standard data and callback types. A host supplies the item records and stable instance IDs, and decides which actions to expose and which participants may change shared state.

The native `SpatialInventory` class and its `version = 1` snapshot schema come from the original `godot-spatial-inventory` add-on. Existing geometry and batch semantics are documented in [the grid API](MODEL.md). Only one extension registering `SpatialInventory` should be enabled in a Godot project. When bringing a project over from the standalone add-on, install Crimson and remove the standalone descriptor before opening Godot.

## Item commands and stacks

`SpatialInventoryActions` implements explicit moves and swaps, quick transfers, stack merges, and stack splits. Attach a `SpatialInventoryStackAdapter` for host-owned quantity records. The adapter's `describe_stack(id)` callback returns the stack key, quantity, and maximum quantity. Its `apply_changes(rows, commit)` callback verifies the expected record values, stages the record changes, invokes the placement commit on the same thread, and rolls back if that commit fails. Use `invalidate_stack_records()` when the host changes quantities outside the adapter.

## Trade

`SpatialInventoryTrade` registers two actors with backpacks, manages the invitation and offer grids, and settles the exchange when both actors lock their offers. The host can supply item and participant eligibility, stack record callbacks, and a wallet. Version 2 trade snapshots preserve offers, reservations, escrow, and pending returns. Persist the host item records and backpacks alongside that snapshot. Use the native default arguments for first fit. A script supplying the sentinel explicitly can pass `Vector2i(-1, -1)`.

## Sorting placements

Create one `SpatialInventorySorter` per independent sort history. `preview(grid, pinned_ids = [], allow_rotation = true, placement_policy = Callable())` returns `error`, and on success `grid_size`, detached `placements`, and `changed_count`. Pinned IDs keep their original positions and orientations. The optional policy receives `id`, `footprint`, `origin`, `rotated`, `source`, `destination`, `swap_id`, and `action`. Return an empty `String` to accept. Preview and apply both check the policy, and apply checks that the grid still matches the preview baseline. `apply_preview()`, `cancel_preview()`, `has_preview()`, `can_undo()`, and `undo(placement_policy = Callable())` manage the current plan and single-level undo. Conflicting edits keep the undo available and preserve the live grid.

## Integer balances

`SpatialInventoryWallet` provides `get_balance(actor, currency)`, `set_balance(actor, currency, amount)`, and `apply_balance_changes(changes, commit = Callable())`. Use positive integer actor IDs, nonempty `StringName` currencies, and nonnegative integer amounts. A change row contains `actor`, `currency`, `before`, and `after`. The wallet validates the whole batch and stages its changes before invoking the synchronous callback. A callback error restores all the expected balances. The host decides how to associate actor IDs with users and persist balances.

A custom wallet can override `get_balance` and `apply_balance_changes` in C++. For GDScript hosts, implement `_get_balance(actor, currency) -> int` and `_apply_balance_changes(changes: Array[Dictionary], commit: Callable) -> Error`. The override owns the same compare, stage, commit, and rollback contract. Rename public overrides from a standalone GDScript wallet to these virtual methods when migrating it to Crimson.

## Containers and temporary staging

Use `SpatialInventoryContainers.register_inventory(grid, container_id = &"")` to associate each grid with its owning item. An empty container ID marks an independent grid such as a backpack. The registry's `placement_policy(context)` checks that item IDs are unique across registered grids and that a container stays out of its own contents and descendants. An optional `host_placement_policy` can apply game rules. Pass `validate_changes(rows)` the native placement batch before committing several ownership changes together. It checks the projected ownership graph after all rows have been applied.

Use `SpatialInventoryStaging.configure(size)` to initialize a tray. `stage_from(source, id, origin, rotated = false)` transfers a complete placed item into the tray and remembers its original grid, cells, and rotation. `place_item(id, destination, origin, rotated = false)` transfers it out. `restore_all()` uses one checked batch to return the staged items. A failed batch leaves the tray intact and reports `last_reason`. Wire the registry's `placement_policy` to the tray's `drop_policy` and `validate_changes` to `batch_policy` when using a tray with nested storage.

## Equipment

`SpatialInventoryEquipment.configure(backpack, slots)` associates named slots with a backpack. The optional `slot_requirements(id, requested_slot)` callback returns all occupied slots or a denial string. Each equipped item has one record even when it occupies several slots. The optional `move_policy(context)` checks equips and returns, including gear displaced by a swap.

Use `preview_equip` or `preview_unequip` to get a detached preview. Pass the preview as `expected` to `equip` or `unequip` to require that the reviewed result still matches. A successful change publishes the backpack and equipment state together. Save `export_snapshot()` alongside the backpack and the host's item records. To restore, configure an empty equipment endpoint with the restored backpack, apply the same slot rules, and call `import_snapshot(snapshot)`.
