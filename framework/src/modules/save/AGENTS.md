<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Save module

Read the coordinator contract for request and adapter changes. Use the format guide, provider SDK, or presentation guide for changes to those surfaces. [Coordinator API](docs/API.md) · [Format and recovery](docs/FORMAT.md) · [Provider SDK](sdk/README.md) · [Presentation API](docs/PRESENTATION.md)

- Run host and adapter callbacks on the main thread. Capture detached DTOs at a committed host boundary and pass only data to persistence workers. Declare participant schemas and migration steps before the first admitted save or load. Resolve durable IDs and finish migration, staging, linking, and validation before publishing a restore candidate. A failed activation leaves the live world intact.
- Preserve GSAV readability and native provider ABI compatibility. Changes to the envelope or provider ABI require an explicit migration and compatibility plan. Keep expected-head checks and cancellation before publishing a fresh immutable generation. Prune backups after publication.
- Keep the selected generation's sequence and ID together. Reconcile `CommitUnknown` by inspecting that exact generation before retrying a write. Preserve incompatible and corrupt heads for explicit recovery.

## Verify changes

- Use the offline Game Flow fixture for cross-world save and restore, missing and corrupt generation rejection, and publication order. Run the ENet fixture when a change affects authority capture or peer readiness after restore. [Offline fixture](../../../project/tests/game_flow/offline.gd) · [ENet fixture](../../../project/tests/game_flow/network_peer.gd) · [Test runner](../../../scripts/Test-Multiplayer.ps1)
- For storage, codec, or provider changes, verify a pre-change generation can still be read and that cancellation and publication failures preserve the documented recovery outcomes.
