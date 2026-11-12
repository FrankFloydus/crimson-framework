<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Match Runtime

Read the [transaction and restore contract](../../../docs/ARCHITECTURE.md#transaction-contract) and the [possession and command routing contract](../../../docs/ARCHITECTURE.md#possession-and-command-routing).

- Keep the runtime transport-neutral. The caller resolves admitted player identities at ingress. Keep peer IDs, player IDs, and binding generations distinct so a delayed disconnect cannot revoke a newer binding.
- Keep store preparation and validation free of external side effects. Validate detached DTOs and expected revisions before publication. A failed commit leaves every store and revision intact. Publish all changed stores before notifying observers and keep reentrant mutation gated through notification.
- For restore, validate the exact registered store set, its schemas, and aggregate invariants before scene activation. A prepared candidate belongs to its coordinator and becomes stale if the revision or registry changes. Release deferred `committed` notifications after the destination scene is mounted.
- Preserve one primary character per player and one primary controller per character. Release an active assignment in the transaction that removes its player or character record. Validate match, world epoch, and applicable possession before dispatch. Consume the command sequence once a handler runs, including on handler rejection.

Use the [headless runner](../../../scripts/Test-Multiplayer.ps1) for the [transaction, restore, possession, and command assertions](../../../project/tests/match_runtime/test_match_runtime.gd) and the [authority and client fixture](../../../project/tests/match_runtime/network_peer.gd).
