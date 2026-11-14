<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Fixed-world session

- Keep [World Session](world_session.hpp) over standalone Game Session and Scene Loader. Route immediate play through [GameRuntime](../game/crimson_game.hpp), preserve the standalone marker-slot API, and allow one active lifecycle per facade.
- Match session RPC paths across peers. Apply [multiplayer rules](../game/multiplayer_rules.hpp) to admission and roster capacity. Resolve spawn placements from the mounted world on the authority. Signal guest readiness after its assigned actor is present.
- For [configured movement](../../../docs/MOVEMENT_NETWORKING.md), install the reliable authority pose baseline before acknowledging readiness. Bind intent to the actual admitted sender, world epoch, and join ticket. Pair committed state with its retired input tick, bound history and replay, and expire an unrecoverable input prefix without motion when a stale stream resumes. Consume jump only from its exact tick and validate floor contact in the authority solver. Keep camera smoothing out of collision state. Each authority actor has one movement driver. Remote proxies follow snapshots.
- Carry Scene Loader failure details into the owning game startup result. Keep the public `session_ended(reason)` signal stable.
- Run the [multiplayer harness](../../../scripts/Test-Multiplayer.ps1) after changing startup, spawn placement, or world readiness.
