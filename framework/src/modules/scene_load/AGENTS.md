<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Scene Loader

Read the [scene loader contract](README.md), especially [coordinated state publication](README.md#coordinated-state-publication). The [lifecycle contract](../../../docs/ARCHITECTURE.md#lifecycle-and-transition-boundaries) defines the Game Flow boundary.

- Keep one pending transition per persistent loader and reserve `World` under its configured container. Peers resolve an allowlisted scene ID through their local catalogs. Thread resource loading, then instantiate the candidate detached and mount it on the scene thread. Drain abandoned resource requests after cancellation or teardown.
- Preserve the captured host peer set, transition token, and sender checks through preparation, commit, and activation confirmation. Validate the staged node and mount before calling the optional authority publisher. Run the publisher once before detaching the old scene so the incoming scene can read committed state in `_ready()`.
- A failed preparation or rejected publisher leaves the live mount in place and reports `load_failed`. Failures after a network commit decision or successful authority state publication report `load_sync_failed`, including an offline mount failure after state publication. Callers use that signal to keep gameplay gated during recovery.
- Preserve standalone admission ownership by default. External admission management requires the orchestrator to hold the host gate closed through confirmation. Scene completion covers mount confirmation. Game Flow owns gameplay baseline readiness and command admission.
- Immediate play uses the native scoped local request for the authority and each entering guest. Keep candidate validation and mounting on the shared path. The orchestrator validates the authority's world offer and owns guest admission and readiness. Keep coordinated RPCs disabled while this flow owns the shared loader.

Use the [scene loader tests](../../../project/tests/scene_load/scene_load_tests.gd) and the [separate-process ENet fixture](../../../project/tests/multiplayer_peer.gd) for the standalone contract. The [offline Game Flow test](../../../project/tests/game_flow/offline.gd) and [ENet Game Flow test](../../../project/tests/game_flow/network_peer.gd) cover coordinated restore and readiness.
