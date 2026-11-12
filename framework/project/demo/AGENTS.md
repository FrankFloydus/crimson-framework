<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson demo

Use the demo to exercise [Crimson's public API](../../docs/PUBLIC_API.md). Keep game rules, art, UI, and level scenes here. Follow the [repository's specialist assignments](../../AGENTS.md#agent-delegation).

- Document the intended player flow and verification for each feature. Keep the implemented features below current.
- Configure [CrimsonGame](../../docs/PUBLIC_API.md#start-a-game) in the persistent demo root. Use [the definition](session/transfer_hall.tres) to select the world, actor, and multiplayer rules. Author spawn points in the world and local-view references in the actor scene. Keep view state and focus handling in the UI.
- Test play through the real boot scene. For session changes, use the [separate-process harness](../../scripts/Test-Multiplayer.ps1).
- Use the [live provider fixtures](../../vendor/README.md#live-provider-verification) for platform account, lobby, and P2P checks.
- The [online fixture](tests/online_smoke.gd) installs a game-owned directory and authenticator on the real boot scene. Keep its local directory and ephemeral test proof isolated from the playable demo.

## Implemented features

- The project boots to Single Player, Multiplayer, and Options. Single Player spawns a stationary player in Transfer Hall. Multiplayer hosts or joins the same hub over ENet by direct address. Options is inert.
- Transfer Hall has a central room with winding north, east, and west tunnels. Each tunnel ends at a decorative portal with its own light color. CrimsonGame assigns players to available spawn components. The player scene owns the stationary first-person view. Keep the light nodes under the owning area's `Lighting` child.
- The menu uses a view model for presentation state and a reusable screen stack for navigation and focus restoration.
- Command-line listen and join requests use the same menu view model as interactive play. The boot script reports readiness after the launch operation settles successfully.

See the [demo guide](README.md) for controls, network setup, and scene ownership.
