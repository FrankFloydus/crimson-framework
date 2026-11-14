<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson demo

Use the demo to exercise [Crimson's public API](../../docs/PUBLIC_API.md). Keep game rules, art, UI, and level scenes here. Follow the [repository's specialist assignments](../../AGENTS.md#agent-delegation).

- Document the intended player flow and verification for each feature. Keep the implemented features below current.
- The persistent [demo script](main.gd) currently boots [Game](../../docs/PUBLIC_API.md#start-a-game) and passes its handle to the menu. Keep view state and focus handling in the UI.
- Test play through the real boot scene. For session changes, use the [separate-process harness](../../scripts/Test-Multiplayer.ps1).
- Use the [live Steam fixtures](../../vendor/README.md#live-provider-verification) for Steam account, lobby, and P2P checks.
- The [online fixture](tests/online_smoke.gd) installs a game-owned directory and authenticator on the real boot scene. Keep its local directory and ephemeral test proof isolated from the playable demo.

## Implemented features

- The project boots to Single Player, Multiplayer, and Options. Solo and multiplayer share grounded movement and Space to jump in Transfer Hall. Multiplayer hosts or joins the hub over ENet by direct address. Guests predict local grounded movement and reconcile with the server. Escape or controller Start opens the game UI's controls overlay, which can rebind Move Forward. Options is inert.
- Transfer Hall has a central room with winding north, east, and west tunnels. Each tunnel ends at a decorative portal with its own light color. The game assigns players to available spawn markers. The player scene owns the first-person view. Keep the light nodes under the owning area's `Lighting` child.
- The menu uses a view model for presentation state and a reusable screen stack for navigation and focus restoration.
- The player scene's Mover component exposes speed, acceleration, deceleration, ground friction, gravity, air control, and jump speed directly. The Controls component owns look and points to the layout that includes the authored On Foot input map. Edit the map in its resource Inspector. Playable and dedicated sessions read the same mover settings.
- The On Foot input map uses ordered Swap Axis and Invert modifiers to turn individual movement keys and stick directions into X and Y values. Mouse XY supplies both relative mouse axes for aim.
- The demo starts with a first-person view for FPS / Shoulder Aim and a view of the local body for the other controller styles. F6 or right-stick click toggles the body view. In Lock-On, L or left-stick click selects or releases the north doorway. Remote players' models are visible and follow their bodies' replicated heading.
- GameUI shows the local player's assigned slot and role. F8 or controller View opens the separate Network Debugger window. Loopback ENet guests can throttle upload and download, save profiles, schedule lag spikes and outages, run timed scenarios, and rejoin their last successful direct host. The headless runner uses the same relay and profiles, executes JSON test plans, and reports assertions and telemetry as JSONL.
- Command-line listen and join requests use the same game actions as interactive play. The boot script reports readiness after a successful launch. The menu reports blocked launches and keeps Cancel available when the game rejects cancellation.
- The Windows LAN launcher tiles its instances on the primary display and starts guests in sequence. Multi-window runs cap FPS and worker threads in the staged copy to limit resource pressure during shader compilation.

See the [demo guide](README.md) for controls, network setup, and scene ownership.
