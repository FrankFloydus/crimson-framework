<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Fixed-world session

- Keep [World Session](world_session.hpp) over standalone Game Session and Scene Loader. Route immediate play through [GameRuntime](../game/crimson_game.hpp), preserve the standalone marker-slot API, and allow one active lifecycle per facade.
- Match session RPC paths across peers. Apply [multiplayer rules](../game/multiplayer_rules.hpp) to admission and roster capacity. Resolve spawn placements from the mounted world on the authority. Signal guest readiness after its assigned actor is present.
- Run the [multiplayer harness](../../../scripts/Test-Multiplayer.ps1) after changing startup, spawn placement, or world readiness.
