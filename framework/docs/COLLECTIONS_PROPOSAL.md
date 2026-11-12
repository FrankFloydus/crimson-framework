<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson Collections proposal

**Status** · Deferred · 12 November 2026

This draft records the earlier collection design discussion. Collections is outside the approved Steam implementation scope. Its examples use the naming under discussion when the draft was written.

Crimson Collections is a shared, standalone C++ module for the whole framework. Framework modules, games, and tools can use it to select, order, and transform an in-memory list. Inventory, Save, Scene Loader, Match Runtime, UI, and platform integrations all use the same implementation where a collection makes the query easier to express.

## Global entry point

`CrimsonCollection.collect(items)` is the public constructor. The extension registers the static method with Godot at load time. Any script can call it, including an editor tool, a standalone game, and a headless fixture. Every call creates a collection for the supplied array.

Collections owns its code and class registration under `modules/collections`. Crimson registers it before modules that expose typed collection views. Native callers can include Collections directly and use `CrimsonCollection::collect(items)` for Godot arrays or the shared native algorithms for typed views. The module depends on Godot’s bindings and the C++ standard library. Each feature owns its record types, any typed view it exposes, and the policy that decides which records can be queried.

## What using it looks like

Wrap an existing list once. Build independent branches for different parts of the UI. Finish with one item, a count, or an array when a Godot API needs one.

```gdscript
var map_names: Array[String] = ["Transfer Hall", "North Tunnel", "East Tunnel"]
var tunnels := CrimsonCollection.collect(map_names) \
    .filter(func(name: String) -> bool: return name.ends_with("Tunnel"))
var tunnel_names: Array[String] = tunnels.to_array()
map_picker.show_names(tunnel_names)
```

For a domain with a known record type, a thin typed view keeps completion through the chain. This proposed inventory example starts from the authorized item rows the module already selected. The inventory module supplies the record type and `usable()` selector. Collections supplies the list operations.

```gdscript
var items := InventoryItemCollection.collect(item_rows)
var visible := items.usable().sort_by_name().take(8)

var first := visible.first()
if first != null:
    inventory_menu.highlight(first.display_name)

for item in visible.to_array():
    inventory_menu.add_entry(item.display_name, item.quantity)
```

The same typed view can take an explicit predicate for a game-specific choice.

```gdscript
var stocked := items.filter(
    func(item: InventoryItemView) -> bool: return item.quantity >= required_quantity
)
```

The proposal borrows Laravel’s ordered chains and independent results. These methods work on a list already in memory. A source query still chooses and validates the data it reads. For example, a future `inventory.filter(can_use).search("medkit").collect()` would read one authorized snapshot, and the returned collection would refine those rows locally. [Laravel Collections](https://laravel.com/docs/13.x/collections) · [Crimson’s fluent inventory examples](PUBLIC_API.md#fluent-api-direction)

## Typed collection design

| Surface | Contract |
| --- | --- |
| `CrimsonCollection.collect(items: Array) -> CrimsonCollection` | Wrap any Godot array from any module or script. Type-preserving transforms keep its runtime element metadata when present. A caller can give `to_array()` a single typed assignment, as in `var tunnel_names: Array[String] = tunnels.to_array()`. |
| `InventoryItemCollection.collect(items: Array[InventoryItemView]) -> InventoryItemCollection` | Illustrates a feature-owned typed binding over the shared native algorithms. A feature can return its typed view directly when callers usually chain queries. |
| Typed `filter()`, `sort_by()`, and `take()` | Return the same concrete collection type. For the inventory example, `first()` returns `InventoryItemView?` and `to_array()` returns `Array[InventoryItemView]`. The editor can complete record properties after either call. |
| Typed domain methods | Add selectors such as `usable()` or `sort_by_name()` where the feature uses them regularly. These selectors use declared fields and can run natively. |
| `map(transform: Callable) -> CrimsonCollection` | Allows the element type to change. The generic projection has a dynamic result because GDScript `Callable` carries no generic return type in the bound method signature. A commonly used typed projection can expose a concrete output such as `item_ids() -> Array[StringName]`. |
| Direct iteration | Validate a concrete native `_iter_get()` return before promising `for item in visible:` with completion. Godot 4.7 uses that signature for loop inference. The typed `to_array()` path is part of the initial contract. |

Use one native implementation for storage and transforms. A concrete view binds its own return types and forwards to those algorithms. It does not need a separate collection engine. GDScript infers from the bound signature. A generic `CrimsonCollection` method cannot specialize its declared return type from the source array at a call site. Explicitly type lambda arguments when the lambda body needs property completion. [Godot static typing](https://docs.godotengine.org/en/4.7/tutorials/scripting/gdscript/static_typing.html) · [Godot iterator contract](https://docs.godotengine.org/en/4.7/classes/class_object.html#class-object-private-method-iter-get) · [Godot 4.7 loop inference](https://github.com/godotengine/godot/blob/4.7-stable/modules/gdscript/gdscript_analyzer.cpp#L2129-L2164)

## Snapshot and execution rules

- **Ordered membership.** `collect()` creates a shallow container snapshot. Later inserts, removals, or reordering on the caller’s array leave the collection’s membership alone. `to_array()` returns an independent shallow container for interop. Referenced objects keep their identity. Use read-only record objects when the fields also need snapshot semantics.
- **Independent branches.** Every transform returns a new logical collection. Filtering preserves relative order. Sort ties retain input order. A branch can share immutable storage internally when the result allows it.
- **Eager execution.** A filter, projection, or sort runs when its method is called. `first_where()` and `any()` stop as soon as they have an answer. Query callbacks are synchronous and should read the entry. `each()` is the explicit method for visiting elements to perform effects.
- **Predictable empty results.** `first()`, `last()`, `at()`, and `first_where()` return `null` when there is no match. A generic collection can contain null entries, so use `count()` or `any(predicate)` when that distinction matters. A published domain collection contains valid typed records. `any()` is false on an empty collection and `every()` is true for a valid predicate on an empty collection.
- **Validation.** Predicates must return `bool`. Selectors for ordered keys use one comparable type per call. Invalid arguments and failed callable invocations are reported through Godot’s script error conventions. A failed transform returns `null` without publishing a partial branch. Failed Boolean and count queries report the error and return `false` or `0`. Absence of a match is an ordinary result. `each()` returns an `Error` and may have visited earlier elements before a callback failure.

## Proposed function plan

The signatures below describe the generic surface. A concrete typed view binds the same type-preserving methods with its own return type and typed element access. `?` marks a nullable result in this document. A transform can also return `null` on invalid use as described above.

### Initial functions

| Function | Behavior |
| --- | --- |
| `CrimsonCollection.collect(items: Array) -> CrimsonCollection` | Take a shallow ordered snapshot. An empty array is valid. |
| `count() -> int` · `is_empty() -> bool` | Read the current number of entries. |
| `first() -> Variant` · `last() -> Variant` · `at(index: int) -> Variant` | Get an entry or `null` on absence. `at()` uses a zero-based index. |
| `first_where(predicate: Callable) -> Variant` | Return the first matching entry in source order. Stop on the first match. |
| `contains(value: Variant) -> bool` | Test membership using Godot array equality. |
| `any(predicate: Callable) -> bool` · `every(predicate: Callable) -> bool` | Test whether at least one or every entry matches. Stop as soon as the answer is known. |
| `count_where(predicate: Callable) -> int` | Count matching entries without building a filtered list. |
| `filter(predicate: Callable) -> CrimsonCollection` · `reject(predicate: Callable) -> CrimsonCollection` | Keep matching or nonmatching entries in input order. |
| `take(count: int) -> CrimsonCollection` · `skip(count: int) -> CrimsonCollection` | Take or skip a nonnegative number of entries. Counts beyond the end clamp to the collection size. |
| `slice(start: int, length: int = -1) -> CrimsonCollection` | Read a range from a nonnegative zero-based position. A length of `-1` means through the end. The range clamps to the available entries. |
| `sort_by(key: Callable, direction: SortDirection = ASCENDING) -> CrimsonCollection` | Sort stably on one key per element. Support integer, finite float, string, and string-name keys. Require one key type within a call. String ordering is deterministic. |
| `unique_by(key: Callable) -> CrimsonCollection` | Keep the first item for each selected scalar key, in source order. |
| `map(transform: Callable) -> CrimsonCollection` | Transform each entry in order. Output can be dynamic when the projection changes type. |
| `to_array() -> Array` | Return a detached shallow array with the current order. Concrete views declare their element types here. |

For example, `InventoryItemCollection.filter(predicate)` returns `InventoryItemCollection`, `first_where(predicate)` returns `InventoryItemView?`, and `to_array()` returns `Array[InventoryItemView]`. A `Callable` takes one entry unless a method specifies a different signature. `to_array()` reads as a conversion in Godot, where `Array.all(predicate)` is a Boolean test. Godot’s existing `Array` operations remain useful for one-off transformations. [Godot Array](https://docs.godotengine.org/en/4.7/classes/class_array.html)

### Proposed additions after the core

| Function | Behavior |
| --- | --- |
| `reverse() -> CrimsonCollection` | Reverse the entry order. |
| `concat(items: Array) -> CrimsonCollection` | Append entries from another list to a new branch. A concrete view accepts its matching typed array. |
| `flat_map(transform: Callable) -> CrimsonCollection` | Call the transform for each entry and append the returned array’s entries in order. Flatten one level. |
| `min_by(key: Callable) -> Variant` · `max_by(key: Callable) -> Variant` | Return the original entry with the smallest or largest key. Prefer the first entry on a tie and return `null` when empty. |
| `reduce(step: Callable, initial: Variant) -> Variant` | Call `step(accumulator, entry)` in input order and return the final accumulator. An empty collection returns `initial`. |
| `sum_by(key: Callable) -> float` · `average_by(key: Callable, empty_value: float = 0.0) -> float` | Aggregate numeric keys in floating point for display and analytics. Use a typed integer loop or reducer for exact large totals. |
| `chunk(size: int) -> Array[CrimsonCollection]` | Split into ordered chunks of a positive size. The last chunk can be shorter. |
| `partition(predicate: Callable) -> CrimsonCollectionPartition` | Visit once and expose `matching()` and `remaining()` branches, both in source order. |
| `group_by(key: Callable) -> Array[CrimsonCollectionGroup]` | Emit groups in order of first key occurrence. Each group has a scalar `key` and `items: CrimsonCollection`. The group members keep input order. |
| `each(visit: Callable) -> Error` | Visit each entry in order for an explicit game-side effect. |

Keep the same ownership and ordering rules for the extended functions. A domain can bind a typed grouping or partition when its consumers need to browse the groups with completion. A lazy collection can be designed when a concrete streaming source or measured memory need warrants it. [Laravel Lazy Collections](https://laravel.com/docs/13.x/collections#lazy-collections)

## Cost and adoption

Yes, wrapping an array has a cost. The wrapper adds an allocation and a reference-counted object. Godot 4.7’s shallow `duplicate(false)` creates a separate array identity and can initially share copy-on-write element storage. A later write can pay for an element copy. The collection contract is an independent shallow snapshot, regardless of when storage detaches. [Godot 4.7 array duplication](https://github.com/godotengine/godot/blob/4.7-stable/core/variant/array.cpp#L497-L531) · [Godot copy-on-write storage](https://github.com/godotengine/godot/blob/4.7-stable/core/templates/cowdata.h)

| Operation | Work to expect |
| --- | --- |
| Wrap an array or return `to_array()` | An additional container identity. A shallow element copy can be deferred by Godot’s copy-on-write storage. Referenced objects are shared. |
| `filter()` and `map()` | A linear pass and a materialized output. A script callable crosses the binding once per examined entry. |
| `sort_by()` | One key selection per entry, key storage, and roughly `O(n log n)` comparisons. A later `take(5)` still sorts the full input. |
| `take()` and `slice()` | A branch with the selected range. Work and memory depend on the selected count. |
| `first()`, `count()`, and short-circuit queries | Constant-time indexed access or count. `first_where()` and `any()` examine only as far as necessary. |

Use collections throughout the framework where the caller would otherwise write several loops or intermediary arrays over a stable snapshot. Build or refresh the collection when the snapshot changes. Use a typed `Array[T]` where the caller’s main job is plain iteration or Godot interop. Keep packed arrays for bytes and numeric buffers. Use the existing native data path for movement, physics, replication, rendering, and other frame-sensitive or allocation-sensitive work.

For new APIs, choose the return shape from the normal call site. Use a typed collection directly when the natural workflow is to query or branch the snapshot. Use a typed array when plain iteration is the normal workflow. Existing array APIs can be wrapped on demand. That preserves their call sites and makes conversion cost visible to the caller.

| Framework area | Example use |
| --- | --- |
| Inventory | Filter usable items, group equipment candidates by slot, and build separate armory and trade views from an authorized snapshot. |
| Save and restore | Sort save summaries by time, select recent checkpoints, and filter the slot menu by campaign. |
| Scene Loader and world catalogs | Filter authored destinations by tag and sort the map picker by title. |
| Match Runtime and game UI | Rank committed scoreboard rows, split end-of-match results by team, and build post-match summaries. |
| Session and online providers | Filter detached browser listings by available seats or region and sort by latency. Steam is one provider using the same list operations. |
| Tools and diagnostics | Filter captured messages by severity and take the latest entries for a debug panel. |

## Approval and verification

A script-only Godot 4.7 probe verified shallow source and output isolation, typed element access, and independent nested iteration with a typed `_iter_get()` return. The native binding and its editor completion are part of the implementation acceptance checks below.

1. Review the names, the typed binding approach, and the eager snapshot contract before implementation.
2. Prove `CrimsonCollection.collect()` in an isolated Godot 4.7 script with the extension loaded and no game scene or vendor SDK. Verify array isolation, separate branches, stable sort ties, empty results, and invalid callbacks. Use a small typed record to check that `:=` and completion work after `filter().take().first()` and in a loop over `to_array()`. Test direct iteration separately, including two nested loops, before documenting it as a supported call site.
3. Measure conversion, one-off filters, chained filters, and sorting with small UI-sized and larger arrays. Compare a plain loop, Godot `Array.filter()`, and the collection with equivalent inputs. Record timing and allocations separately for native selectors and GDScript callables. Measure the operation that detaches shared storage as well as the initial wrap.
4. After the core passes, validate a framework consumer and a standalone game or tool consumer. Adopt Collections across framework APIs as their call sites warrant it. Apply the same criteria to the Steam proposal in its own review. The Steam integration goal remains paused.
