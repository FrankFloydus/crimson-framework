<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Native model

`SpatialInventory` is a C++ `RefCounted` model for rectangular, cell-based inventories. It owns placement and occupancy. The host game owns item records, identity across inventories and saved games, and rules such as stack limits, equipment restrictions, and whether to accept a world drop. A placement can represent a single item or a stack if the game assigns it a stable instance ID.

The native grid is implemented in `spatial_inventory.h` and `spatial_inventory.cpp`. `register_inventory.cpp` registers the class during Crimson Framework scene initialization. `doc_classes/SpatialInventory.xml` supplies the in-editor reference.

## Data and geometry

| Field | Type and meaning |
| --- | --- |
| `id` | Nonempty `StringName` identifying an item instance. IDs are unique within a grid. The game should allocate stable IDs across inventories that can exchange items. |
| `footprint` | Positive `Vector2i(width, height)` in cells, before rotation. |
| `origin` | Zero-based `Vector2i(x, y)` of the top-left occupied cell. Columns increase rightward and rows increase downward. |
| `rotated` | `bool` selecting either the original footprint or a 90-degree orientation that swaps width and height. |

The oriented extent is `Vector2i(footprint.y, footprint.x)` when rotated. The occupied rectangle starts at `origin` and ends just before `origin + extent` on each axis. A `2 × 3` item rotated at `(1, 2)` occupies columns 1 through 3 and rows 2 through 3. Preserve the unrotated footprint when moving, swapping, resizing, or serializing an item. Rotation remains part of the placement even for square footprints.

A fresh model has size `Vector2i.ZERO`. Configure positive dimensions with at most 1,048,576 cells or load a snapshot. Coordinates and extents use signed Godot integers. Check bounds with subtraction before adding coordinates and extents so that malformed input cannot overflow an intermediate rectangle.

## Bound API

All origin and cell arguments are `Vector2i`, all ordinary item ID arguments are `StringName`, and destinations are `SpatialInventory` instances. Call the model on the main thread. Preview methods are read-only and report the state at the time of the call. A subsequent mutator validates against the live model again.

| Method | Contract |
| --- | --- |
| `configure(size) -> Error` | Set positive grid dimensions when empty. The current positive size returns `OK` without a change event. Changing the dimensions of a populated grid returns `ERR_BUSY`. |
| `get_grid_size() -> Vector2i` | Return dimensions or `Vector2i.ZERO` while unconfigured. |
| `get_placement_revision() -> int` | Return the live grid's revision. Actual placement or dimension changes advance it before notifications. Compare revisions on the same grid. |
| `check_placement(footprint, origin, rotated = false) -> Error` | Check whether a rectangle is in bounds and all its cells are free. This check takes geometry only. |
| `try_place(id, footprint, origin, rotated = false) -> Error` | Insert an unused ID with valid geometry. A failure leaves all existing placements intact. |
| `check_move(id, origin, rotated) -> Error` | Check a new origin and orientation for an existing item. The item's current cells count as free. |
| `try_move(id, origin, rotated) -> Error` | Commit a valid move or rotation. An identical placement returns `OK` without a change event. |
| `check_transfer(id, destination, origin, rotated) -> Error` | Check destination geometry and ID availability for a source item. A transfer to the source grid delegates to `check_move`. |
| `try_transfer(id, destination, origin, rotated) -> Error` | Move an item between models, preserving its unrotated footprint. Both grids commit before callbacks run. A failed transfer preserves the source placement. A transfer to the source grid delegates to `try_move`. |
| `try_resize_item(id, footprint) -> Error` | Apply a new unrotated footprint at the existing origin and orientation. Occupancy may overlap the item's own cells. An identical footprint returns `OK` without a change event. |
| `remove(id) -> Error` | Free all cells of the placement. An absent item returns `ERR_DOES_NOT_EXIST`. |
| `get_item_at(cell) -> StringName` | Return the instance ID at a cell or `&""` when free or out of bounds. |
| `get_placement(id) -> Dictionary` | Return a detached dictionary with the four placement fields, or `{}` if absent. |
| `get_placements() -> Array` | Return detached placement dictionaries for the whole grid. Treat their order as unspecified. |
| `find_first_fit(footprint, rotated = false, start = Vector2i.ZERO) -> Vector2i` | Scan origins left-to-right and top-to-bottom from the inclusive start. A start past the last possible origin in a row continues on the next row. Return `Vector2i(-1, -1)` for an invalid footprint, a negative start, or no fit. Call again with the other orientation to search it explicitly. |
| `get_swap_target(footprint, origin, rotated = false) -> StringName` | Return the ID of the single distinct item intersecting the in-bounds candidate. Multiple covered cells from the same item count once. Free, invalid, and multiple-item candidates return `&""`. |
| `try_swap_in(id, footprint, origin, rotated, expected_id) -> Dictionary` | Insert the held item in place of exactly the blocker named by `expected_id`. Return the displaced placement or `{}` on failure. Both removal and insertion finish before notification. |
| `export_snapshot() -> Dictionary` | Capture the current grid size and placement records. See [Snapshots](#snapshots). |
| `import_snapshot(snapshot) -> Error` | Validate and replace a grid atomically. Identical state returns `OK` without a change event. |
| `SpatialInventory.apply_placement_changes(changes) -> Error` | Compare expected placements and apply a batch across grids. See [Atomic placement changes](#atomic-placement-changes). |
| `set_placement_deltas_enabled(enabled) -> void` | Set per-grid emission of `placements_changed`. The flag starts disabled and toggling it emits no signal. |
| `is_placement_deltas_enabled() -> bool` | Read the per-grid flag. |

Geometry checks can return `ERR_UNCONFIGURED` on an unconfigured grid, `ERR_INVALID_PARAMETER` for invalid dimensions, `ERR_PARAMETER_RANGE_ERROR` for an out-of-bounds footprint, and `ERR_ALREADY_IN_USE` for a collision. Insertion also checks for an empty ID and returns `ERR_ALREADY_EXISTS` for an ID already present. Movement, resizing, transfer, and removal report `ERR_DOES_NOT_EXIST` for a missing source item. Transfer with a null destination returns `ERR_INVALID_PARAMETER`. Methods that return placement dictionaries or sentinel IDs have the results in the table above.

For a cursor swap, the controller detaches the held placement from its source before calling `try_swap_in`. The destination rechecks the geometry and blocker ID even if the UI has already called `get_swap_target`. `try_swap_in` requires an unused incoming ID in the destination. The returned dictionary retains the displaced item's original footprint, origin, and rotation.

## Snapshots

A version 1 snapshot has this shape in GDScript.

```gdscript
{
    "version": 1,
    "grid_size": Vector2i(6, 4),
    "placements": [
        {
            "id": &"scroll-42",
            "footprint": Vector2i(1, 2),
            "origin": Vector2i(2, 1),
            "rotated": true,
        },
    ],
}
```

- `version` is an integer and must equal `1` for the current importer. `grid_size` is `Vector2i` and `placements` is an `Array` of dictionaries. `Vector2i.ZERO` is allowed with an empty array to round-trip an unconfigured model.
- Placement dictionaries accept a nonempty `String` or `StringName` ID, `Vector2i` geometry, and a `bool` rotation. The importer rejects duplicate IDs, invalid sizes, collisions, and out-of-bounds geometry. It builds a temporary grid and replaces the live grid only after validation. IDs, geometry, and grid size determine equality independently of array order.
- The model serializes placement state. Resolve IDs through the game's saved item store. The host should settle any detached cursor item before saving or persist that ownership separately.
- `var_to_bytes` and `bytes_to_var` preserve the Variant types used by the snapshot. The native test suite exercises this round trip. For JSON or an external save format, encode vectors and IDs deliberately and reconstruct the declared Godot types before importing. Convert a decoded version to an integer when needed.
- If the format changes, keep version 1 saves readable or provide an explicit migration before import. Update the format example, editor class help, and round-trip and rejection tests in the same change.

## Atomic placement changes

`apply_placement_changes` applies a batch only if every row's expected state and every proposed placement still fit the current grids. Each row has four keys.

| Key | Meaning |
| --- | --- |
| `inventory` | A live `SpatialInventory` reference. |
| `id` | A nonempty `StringName` or `String`. A given inventory and ID pair appears at most once. |
| `before` | The expected placement dictionary or `{}` to require absence. |
| `after` | The desired placement dictionary or `{}` to remove the item. |

A populated `before` or `after` dictionary uses the placement schema and must have the row's ID. The same ID may appear in separate rows for separate grids. A row with identical `before` and `after` still checks its expectation and emits no event. An empty batch returns `OK`.

The model checks expectations, then validates final cell occupancy. Validation treats the cells vacated by the batch as free and detects overlap among the proposed placements. It rejects malformed rows with `ERR_INVALID_PARAMETER`, duplicate grid and ID pairs with `ERR_ALREADY_EXISTS`, stale expected placements with `ERR_BUSY`, and occupied desired cells with `ERR_ALREADY_IN_USE`. Invalid destination geometry uses the same bounds errors as normal placement. If any row fails, the batch preserves every grid and emits no events.

A successful batch clears the old footprints, installs their replacements, then notifies the affected grids. For cancellation, send the session's last written state as `before` and its original state as `after`. An unrelated placement survives cancellation, while a conflicting edit prevents the whole rollback.

## Change notifications and reentrancy

`changed()` fires for each grid whose state changed. When deltas are enabled on that grid, `placements_changed(ids: Array, full_refresh: bool)` fires before `changed()`. The ID array contains `StringName` values and is read-only.

- A place, move, resize, or remove reports the affected ID. A swap reports the incoming and displaced IDs. A cross-grid transfer reports the moved ID on each affected grid. A batch reports one list per changed grid.
- Configuration and changed snapshot imports report `full_refresh = true` with an empty ID array. For other events, resolve each reported ID in the live model. An absent placement means the view should remove its tile.
- Preview calls, failed writes, and successful no-ops emit neither signal. View binding enables deltas explicitly. Treat that flag as shared by the views and controller attached to the same model.
- Both signals are synchronous. Transfer and batch finish all writes before emitting their first signal, so a listener can inspect the committed state of every affected grid. Listeners can themselves mutate a grid during notification. Consumers should resolve IDs against the current state and avoid assuming that all later listeners see the same snapshot.
- Keep models alive through callbacks. Native transfer holds the destination reference across source notification, and native batch holds references to all its grids. Capture event data before invoking callbacks. Placement vector references and slot IDs are internal and can be invalidated by insertion or slot reuse.

## Storage and performance

`GridState` stores contiguous 32-bit occupancy slots in row-major order. Slot zero marks a free cell. A nonzero slot indexes the placement vector using `slot - 1`. A hash map resolves `StringName` IDs to slots, and removed slots enter a free list for reuse. The occupancy array, placement vector, and ID map must agree after each commit.

- Cell lookup is constant time and ID lookup is expected constant time. Placement checks visit at most the candidate's cells and stop at the first collision. Move and resize can reuse the existing record and scan or rewrite only its old and new footprints.
- `find_first_fit` preserves row-major order. The rectangle scan skips origins that still overlap its blocker. Large searches with repeated cell reads switch to sliding column counts for the remaining rows. A zero column count means that every cell in the footprint's vertical span is free.
- `get_placements` and snapshot exports allocate dictionaries for the grid. Use placement deltas and individual ID lookups for ordinary UI updates. Snapshot imports compare entries to the live grid first when dimensions and counts match. They start building a replacement grid when they reach the first difference. Batch validation tracks touched records and uses packed bits for small grids and dense batches, with sparse tracking for small edits in large grids. Within-grid batch moves reuse placement slots.
- Placement deltas default to off for model-only use. Keep game callbacks, presentation resolution, and allocations for UI payloads out of native cell loops. Compare native benchmark runs with the same DLL flavor and notification settings. Profile rendered updates separately because the model benchmark excludes view work.

## Verify a native change

Build the framework using the [project README](../../../README.md). When changing native bindings, synchronize method names, argument order, defaults, errors, and signals in `doc_classes/SpatialInventory.xml`. Check that rejected edits preserve existing placements and that transfer and batch listeners observe the complete committed state.
