<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Save module

The Crimson Framework entry point registers `SaveCoordinator` and `SavePresentation` at scene initialization. The save module accepts detached DTOs from host-owned adapters. Background I/O publishes immutable generations, while the host stages, validates, and publishes restored gameplay state on the main thread.

The original `SaveCoordinator` methods and signals, GSAV framing, and native provider ABI remain available. Existing saves require the same `app_id`, root, profile and slot IDs, scope ID, and compatible schema. The default store uses local Windows filesystem semantics. A multiplayer host can supply a snapshot of server-owned state, and the game remains responsible for session authority and replication.

- [Coordinator API](docs/API.md)
- [Save format and recovery](docs/FORMAT.md)
- [Native presentation methods](docs/PRESENTATION.md)
- [Provider SDK](sdk/README.md)

The inventory module is independent. A game can register an inventory adapter using its own stable DTO and reconstruction rules.
