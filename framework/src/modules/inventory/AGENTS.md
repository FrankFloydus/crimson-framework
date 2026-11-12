<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Inventory module

Start with the grid and snapshot contract for placement changes, and the module guide for stack, container, equipment, staging, sorter, wallet, and trade behavior. [Grid model](MODEL.md) · [Module API](README.md)

- Keep the placement index, occupancy cells, and ID map consistent. Preserve the unrotated footprint and use overflow-safe bounds checks. The host owns item records keyed by stable instance ID.
- Compare expected placements and any expected host records before writing. Validate final occupancy and, for registered containers, the projected ownership graph. A failed change preserves the affected grids and host records. Publish all successful writes before notifying observers, and keep affected grids alive through synchronous callbacks.
- Validate snapshot geometry and IDs before replacing a live grid. Preserve version 1 readability or provide an explicit migration before import. Sync changed Godot bindings with the [editor class reference](doc_classes/SpatialInventory.xml).

## Verify changes

- For placement changes, check rejected and stale edits, transfers across grids, and observer reads of the completed batch. For snapshot changes, check a version 1 round trip and malformed imports.
- Run the Match Runtime fixture when touching placement or its transaction adapter. It exercises a successful pickup and a full-inventory rejection across inventory and world state. [Pickup fixture](../../../project/tests/match_runtime/test_match_runtime.gd) · [Test runner](../../../scripts/Test-Multiplayer.ps1)
