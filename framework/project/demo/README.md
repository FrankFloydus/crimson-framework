<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson demo

Open [project.godot](../project.godot) in Godot 4.7 stable with the [Crimson addon built](../../README.md#build) and press F5. The project boots into the Crimson menu.

## Play

- **Single Player** starts an offline session and spawns you in Transfer Hall.
- **Multiplayer → Host Game** starts an ENet listen host on UDP port `24830` and spawns you immediately. Up to three guests can join while the host is in the hall.
- **Multiplayer → Join Game** takes the host's IP address or hostname. Use `127.0.0.1` for separate processes on the same PC, or the host's LAN address on the same network. Internet connections require the host's UDP port to be reachable. If the host disconnects, guests return to the menu.
- **Options** is an inactive menu entry in this slice.

Use the mouse or directional focus to navigate. Enter or the controller's confirm button selects the focused action. Escape or the controller's back button goes back in the menu or cancels a pending connection. The join form takes keyboard text.

Players have fixed first-person cameras and stationary avatars. Transfer Hall branches into winding north, east, and west passages. The cyan, amber, and violet portals are visual props reserved for future level travel. To inspect them now, open the [hub scene](hub/starting_hub.tscn) in the Godot editor and navigate in the 3D viewport.

## Start from the command line

From the repository root, use the demo's graphical launch options after Godot's `--` separator. Start the host in one process and the guest in another.

```powershell
& $GodotExe --path framework/project -- --crimson-role=listen --crimson-bind=127.0.0.1
& $GodotExe --path framework/project -- --crimson-role=join --crimson-address=127.0.0.1
```

Set `--crimson-port` on both processes to override the demo's UDP port for that run. A listen host defaults to bind address `*`. Use the host machine's LAN address in Join Game from another machine. The same Join form accepts a dedicated server's address.

Automatic launches use the same menu actions and feedback as interactive play. A failed join keeps the requested address in the form. On success, the game prints `[Crimson] READY listen <port>` or `[Crimson] READY join <port>` after the operation completes. Failed launches print `[Crimson] ERROR <role> <message>`.

## Menu architecture

[Main](main.tscn) assigns its native `GameRuntime` to the [menu view](ui/main_menu.tscn) in the Inspector. The [boot script](main.gd) forwards optional command-line requests to the menu. The [view model](ui/menu_view_model.gd) calls `play_solo()`, `host_game()`, or `join_game(address)` and presents each operation's pending, failure, and ready states.

[ScreenNavigator](ui/screen_navigator.gd) owns push, pop, and focus restoration for screen controls. It can be reused by other menus by passing the screen and its initial focus target. Session commands, connection feedback, and the shared [menu theme](ui/menu_theme.tres) are outside the navigator.

The [UI guide](ui/README.md) documents the navigator's calls and the menu input fixture.

## Session and scene ownership

The [Transfer Hall definition](session/transfer_hall.tres) supplies the [hub](hub/starting_hub.tscn), [player](player/stationary_player.tscn), and rules to [GameRuntime](../../docs/PUBLIC_API.md#start-a-game). The definition permits four total players, enables join in progress, and uses UDP port `24830`. The authority chooses a placement from the hub's `PlayerSpawnPoint` nodes. The player scene's `LocalPlayerView` sets the camera and local body visibility through same-scene Inspector references.

In the hub scene, each room or tunnel has a `Lighting` child that holds its fixtures. The reusable [portal scene](hub/portal/portal.tscn) has its own `Lighting` child for its terminal light. The [concrete wall shader](hub/concrete_wall.gdshader) follows each tunnel wall face and carries texture alignment through the bends.

The [PowerShell harness](../../scripts/Test-Multiplayer.ps1) runs the demo against the debug and release DLLs in isolated projects. The [demo fixture](tests/session_smoke.gd) covers solo play, independent guests joining an active host, distinct spawn slots, departure and rejoin, and returning to the menu on host loss.

The live Steam fixtures use Transfer Hall to exercise the built-in Steam provider. The custom online provider runs in the regular PowerShell harness. [Live Steam verification](../../vendor/README.md#live-provider-verification)

## Visual references

- The hub uses muted masonry and steel, localized pools of fixture light, fog, and saturated portal apertures. The references for those effects were id Software's [Quake III Arena Shader Manual](https://graphics.stanford.edu/courses/cs448-00-spring/q3ashader_manual.pdf) and the [Q3Radiant lighting guide](https://www.asc.ohio-state.edu/lewis.239/Gauge/q3rmanhtml.htm).
- Reinforced access frames, service conduits, and industrial fixtures draw on [Valve's Half-Life screenshots](https://store.steampowered.com/app/70/HalfLife/).
- The hub's original game textures were generated with built-in ImageGen using the supplied bunker image as a style reference. The prompt requested flat, unlit albedo with coarse late-1990s FPS texture detail for streaked [board-formed concrete](hub/textures/cast_concrete.png), mottled [floor slabs](hub/textures/deck_slabs.png), [oxidized iron](hub/textures/oxidized_steel.png), and [blackened steel](hub/textures/blackened_steel.png). The tiles were prepared for repetition and applied to editable geometry.
