<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Replication

Read the [module API and delivery bounds](README.md) and the [admission and readiness contract](../../../docs/ARCHITECTURE.md#replication-and-admission).

- Project only the bounded, detached records the recipient may see. The authority supplies the admitted player ID and committed revision to the game-owned projector. Keep projection policy and admission in the caller. Keep Object decoding disabled in the multiplayer scope.
- Preserve server authority and the matching persistent RPC path in the multiplayer scope. Accept acknowledgments against the actual sender, current binding generation, world epoch, and baseline ID. Readiness follows acknowledgment and catch-up. A world or binding change invalidates prior readiness.
- Keep a delivery sequence per recipient. Hidden-only commits may advance the authority revision without consuming that recipient's sequence. Send removals when records leave the view. A client sequence gap invalidates the view and requests a fresh baseline.

Use the [headless runner](../../../scripts/Test-Multiplayer.ps1) for the [two-process replication fixture](../../../project/tests/replication/network_peer.gd). It exercises recipient filtering, catch-up, removals, epoch changes, and disconnect cleanup.
