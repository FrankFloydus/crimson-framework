<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson integration

Use the [public API guide](../../docs/PUBLIC_API.md) for supported Godot bindings and the [architecture](../../docs/ARCHITECTURE.md) for module ownership and lifecycle contracts.

- [Game](game/game.hpp) exposes session actions and [GameRuntime](game/crimson_game.hpp) composes native services. Keep game rules in registered stores, validators, commands, and projectors. Keep standalone modules independent of `src/integration`. [Cross-module adapters](adapters/) call owning modules through their APIs.
- Resolve Session and Scene Loader through [game-scoped factories](game/game_services.hpp). Attach the returned nodes at the established RPC-relative names and release the scope when the game leaves the tree.
- Put fixed-world startup and player placement in [World Session](world_session/AGENTS.md). Put vendor composition and the directory contract in [Online](online/AGENTS.md).
- Keep local player composition in [Player Control](player_control/player_control.hpp). It consumes the standalone Input and Mover modules. The game owns possession and supplies the existing body and view. Keep settings UI and simulation authority outside this profile.
- Keep the game and its service nodes outside the replaceable world. Keep their paths relative to the Godot multiplayer root consistent across peers. Use durable world IDs for checkpoint destinations and scene IDs for asset selection.
- Keep [definition](game/game_definition.hpp) and [save request](game/game_save_slot.hpp) modifiers safe to branch. Preserve detached configuration and options, explicit submission, and weak save-slot access to the originating game. Settle each [operation](game/game_operation.hpp) once and preserve the same result through `wait()` before or after completion.
- Attach the machine code and a meaningful message to the caller's result before invoking callbacks. State whether the old world is still available or the session needs recovery when relevant. Report failures and noteworthy outcomes once at settlement through Godot's built-in output with a `[Crimson]` prefix. Clients report accepted transitions from their local perspective.
- Keep [dedicated boot](game/dedicated_boot.hpp) native and private behind `GameConfig.boot_server()`. Start only with the explicit server role and emit launcher readiness after the operation publishes a playerless world.
- Register composition classes through [integration registration](register_integration.cpp). Document Inspector properties in the owning folder's `doc_classes/` directory.

## Scene and checkpoint publication

1. Game Flow closes command admission and captures pending saves from the current committed revision before staging a transition. [Save Coordinator contract](../modules/save/docs/API.md)
2. For restore, [Match Checkpoint](game/match_checkpoint.hpp) stages the exact registered store set and resolves the saved world ID. The [Scene Manager](../modules/scene_manager/README.md) prepares the destination scene while the current world stays active.
3. The authority publication hook activates the prepared checkpoint and installs the destination ID and epoch before mounting the scene. Keep deferred store notifications behind the mount boundary so the incoming scene and store observers see matching state. [Transaction contract](../../docs/ARCHITECTURE.md#transaction-contract)
4. Preserve the prior world and phase on preparation failure. Treat a failure after publication as recovery with commands closed. On teardown, retire the transport, scene, and old match handles before signaling that the game is reusable. [Lifecycle and transition boundaries](../../docs/ARCHITECTURE.md#lifecycle-and-transition-boundaries)

## Multiplayer readiness

- Bind admitted peers in the authority lobby with trusted player IDs. Close admission when the first world transition starts. Use the binding generation and current epoch when checking a remote command's readiness.
- Send recipient-specific baselines after scene activation. Reach `RUNNING` and emit `world_ready` after the required acknowledgments. Keep world and command control on the authority, and pass game-approved committed views through [Network](../modules/network/README.md). Preserve the explicit visibility policy when changing the facade or its adapters.

Use the [offline Game Flow fixture](../../project/tests/game_flow/offline.gd) for restore rejection, mount-time reads, deferred notification, and session reuse. Use the [separate-process ENet fixture](../../project/tests/game_flow/network_peer.gd) for trusted command dispatch and world readiness. Both are part of the [multiplayer verification script](../../scripts/Test-Multiplayer.ps1).
