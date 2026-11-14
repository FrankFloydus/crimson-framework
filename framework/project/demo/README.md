<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson demo

Open [project.godot](../project.godot) in Godot 4.7 stable with the [Crimson addon built](../../README.md#build) and press F5. The project boots into the Crimson menu.

## Play

- **Single Player** starts an offline session in Transfer Hall. Move with WASD or the left stick and look with the mouse or right stick. Press **Space** to jump in solo or multiplayer. Press Escape or controller Start to open Controls and change the Move Forward key.
- **Multiplayer → Host Game** starts an ENet listen host on UDP port `24830` and spawns you immediately. Up to three guests can join while the host is in the hall.
- **Multiplayer → Join Game** takes the host's IP address or hostname. Use `127.0.0.1` for separate processes on the same PC, or the host's LAN address on the same network. Internet connections require the host's UDP port to be reachable. If the host disconnects, guests return to the menu.
- **Options** is an inactive menu entry in this slice.

Use the mouse or directional focus to navigate. Enter or the controller's confirm button selects the focused action. Escape or the controller's back button goes back in the menu or cancels a pending connection. The join form takes keyboard text.

Players move through Transfer Hall from a first-person view. The hub branches into winding north, east, and west passages. The cyan, amber, and violet portals are visual props reserved for future level travel. To inspect them now, open the [hub scene](hub/starting_hub.tscn) in the Godot editor and navigate in the 3D viewport.

## Network debugger

Each playable window shows its assigned player number and role in the upper-left corner. The listen host reads **Player 1 (Server)**, and guests show **Client** with their assigned slot number.

Press **F8** or controller **View** in a running demo window to open its **Network Debugger**. **Monitor** shows measured round-trip time (RTT), input acknowledgments, outstanding inputs, correction history, and server and local positions when available. Its relay table shows upload and download rates, their configured caps, and datagrams waiting or dropped in each direction. **Simulator** applies profiles and one-off disruptions to the current guest's link. **Scenarios** runs a timed sequence of conditions. **Events** records connection and simulation changes. Select **Focus game** to resume movement while the tool stays open, then press **F8** again to bring the debugger forward.

In **Simulator**, choose a **Profile** and click **Apply**. Upload and download limits are in kilobits per second. Enter `0` for unlimited in either direction. Delay and jitter are per direction. To keep your settings, enter a name and click **Save profile**. Both Windows batch launchers share saved profiles across game windows and later runs. The Godot editor uses its project data directory by default.

In **Scenarios**, choose **Profile cycle** or **Uplink interruption** and click **Load template**. To make a sequence, pick a profile and duration for each stage. A stage can also block uplink, downlink, or both. Click **Run scenario** for one pass or check **Loop** to keep cycling. Set a start delay if you need time to focus the game. When the scenario finishes or you click **Stop scenario**, the relay restores the profile applied before the run. The scenario draft lasts for the current game process.

### Test prediction and recovery

From the workspace root in PowerShell, run `./Run-LAN.bat 2` for a listen host and one guest. The number is an argument to the existing batch file. Use `./Run-LAN.bat 3` for a second guest as an observer, or double-click `Run-LAN.bat` for four windows.

| Check | What to do | What to look for |
| --- | --- | --- |
| Baseline | In the guest's **Simulator**, select **Clear** and click **Apply**. Return focus to the game and walk toward a wall. | The guest starts moving promptly. The host sees the guest move. After you stop, the local and server positions settle together and the input acknowledgment keeps advancing. |
| Delay and loss | Apply **150 ms RTT + 10% loss** on the guest. Focus the game, walk, turn, and push into a wall. | Measured RTT rises and the relay records dropped packets. The owner responds promptly, while the host or observer sees the delayed movement. The player stays on the playable side of the wall and the positions settle together on release. |
| Bandwidth limit | Choose **Clear**, set **Download (kbps)** to **32**, and apply. Watch **Monitor** while the host and guest move. | Under enough traffic the downlink queues datagrams and forwards them at the capped rate. Queue limits can cause drops if demand stays above the cap. |
| Lag spike | In **Lag spike**, set **Extra one-way delay (ms)** to **300**, **Duration (ms)** to **1000**, and **Start delay (ms)** to **2000**. Click **Start lag spike** and focus the game. | Packets in the affected directions arrive later. The regular profile resumes for new packets after the spike. **Events** shows when the spike started and ended. |
| Repeatable scenario | In **Scenarios**, load **Profile cycle**, click **Run scenario**, and focus the game. | **Events** records the stage changes. When the run ends, the relay restores the prior profile. |
| Temporary input outage | Face open floor. Set **Direction** to **Uplink**, **Duration (ms)** to **1400**, and **Start delay (ms)** to **2000**. Click **Start outage**, focus the game, and hold a movement key as the countdown ends. | Guest-to-server packets drop during the outage. When it ends, forwarding resumes with the applied profile. Acknowledgments advance again, the input backlog shrinks, and movement continues in the same session. |
| Timeout and rejoin | Click **Drop link** and wait for the connection to time out. Click **Reconnect** when available. | The guest returns to the menu, the host removes the guest avatar, and the guest joins the same host again. Use **Disconnect** to compare a normal leave without waiting for a timeout. |

For a dedicated test, run [Run-Dedicated.bat](../../../Run-Dedicated.bat) and repeat the guest steps in the visible client. The server runs headless. The simulator acts on the selected guest's same-machine ENet connection. RTT is measured by ENet. The preset expresses configured delay, and the drop counters record observed relay activity.

Use the guest's **Monitor** for its movement data. The server position is the latest received authoritative pose, so compare it to the local position after stopping. The input backlog can stay above zero during a healthy delayed connection. Look for it to settle near its level before the outage. **Pause charts** freezes the plots while live values continue to update. Use **Copy events** on **Events** to include the connection timeline in a bug report.

### Run the simulator without windows

Use the headless runner to apply network conditions, make assertions, and collect a trace that an agent or CI job can read. Each runner joins one ENet guest to a server on the same computer and shapes that guest's connection.

From the workspace root in PowerShell, start a host. Use your Godot 4.7 Windows console executable for `$GodotExe` and choose a free UDP port if `24830` is occupied. If this is the first run in a fresh checkout, import the demo once with `& $GodotExe --headless --path framework/project --editor --import`.

```powershell
$GodotExe = 'C:\Godot\Godot_v4.7-stable_win64_console.exe'
$Port = 24830
& $GodotExe --headless --path framework/project -- --crimson-role=listen --crimson-bind=127.0.0.1 "--crimson-port=$Port"
```

When the host prints `[Crimson] READY listen 24830`, open a second PowerShell in the workspace root. Use the same executable and port. The example applies a bandwidth profile, moves the guest during a lag spike, runs a short scenario, checks that the original cap returns, and reconnects. [Example plan](session/simulator_example.json)

```powershell
$GodotExe = 'C:\Godot\Godot_v4.7-stable_win64_console.exe'
$Port = 24830
$Runner = 'res://demo/session/network_simulator_cli.gd'
$Plan = 'res://demo/session/simulator_example.json'
$Trace = Join-Path $env:TEMP ("crimson-simulator-" + [guid]::NewGuid().ToString("N") + ".jsonl")
& $GodotExe --headless --path framework/project --script $Runner -- "--port=$Port" "--plan=$Plan" "--output=$Trace"
$Result = $LASTEXITCODE
Get-Content -LiteralPath $Trace -Tail 5
$Result
```

Pass `--plan` an absolute file path or a Godot `res://` path. The runner exits with `0` when the plan passes, `1` on a runtime or assertion failure, and `2` for invalid arguments or plans. Pass `--output` a new absolute file path to record one JSON object per line. On the console, the same records start with `CRIMSON_SIMULATOR ` so scripts can distinguish them from Godot's output. Stop the host when your tests are done.

Use `--help` for arguments or `--list` for the action, profile, and template catalog. Both work without a running host. The default address is `127.0.0.1` and the default port is `24830`. Use `--address` to select `localhost`, `127.0.0.1`, or `::1`.

```powershell
& $GodotExe --headless --path framework/project --script $Runner -- --list
```

#### Write a plan

A plan contains a `steps` array and can set a top-level `timeout_ms` to bound the run. The default is 120,000 milliseconds. Steps run in order. To use a built-in profile, supply a `preset` such as `slow-4g` or `constrained`. Use `saved` for a previously saved profile by name, or `settings` for custom `delay_ms`, `jitter_ms`, `loss_percent`, `uplink_kbps`, and `downlink_kbps`. Fields omitted from `settings` default to zero. A zero rate means unlimited.

| Action | Fields and effect |
| --- | --- |
| `status`, `list_profiles` | `status` records the current game and relay snapshot with an optional `label`. `list_profiles` lists the available presets and saved profiles. |
| `apply_profile`, `save_profile` | Apply one `preset`, `saved`, or `settings` selector. Save a custom profile with `name` and `settings`. |
| `spike`, `outage` | Set `duration_ms` from 100 to 30,000. `direction` defaults to `both` and also accepts `uplink` or `downlink`. `start_delay_ms` defaults to zero. A spike also takes `extra_delay_ms`. |
| `cancel_spike`, `drop_link`, `resume_link` | Cancel a spike, block both directions until the connection times out or you resume it, or end an outage. |
| `start_scenario`, `stop_scenario` | Start with `template` (`profile-cycle` or `uplink-interruption`) or with custom `stages`. Each stage takes a profile selector, `duration_ms`, and optional `label` and `outage` (`none`, `both`, `uplink`, or `downlink`). Use `loop` to repeat and `start_delay_ms` to schedule. Stopping or completing the scenario restores the previously applied profile. |
| `disconnect`, `reconnect`, `reset_counters` | Leave normally, join the same endpoint, or zero the relay counters. |
| `move` | Hold a movement direction (`forward`, `backward`, `left`, or `right`) for `duration_ms` from 20 to 10,000. The guest uses its demo movement controls and generates live input traffic. |
| `wait`, `sample` | Wait for `duration_ms` while the game runs. `sample` also records snapshots at `interval_ms`, which defaults to 250. |
| `assert`, `wait_until` | Check a dotted `field` against `value` using `op` (`eq`, `ne`, `gt`, `gte`, `lt`, or `lte`). Use a number for comparisons other than equality and inequality. `wait_until` takes a `timeout_ms` and polls until the check passes or times out. |

For a longer run, start a looping scenario with `"loop": true`, sample its state for the duration you want to observe, and stop it explicitly. Compare fields such as `relay.uplink.forwarded_bytes`, `relay.downlink.dropped`, `game.round_trip_ms`, and `game.pending_inputs`. The demo routes guest traffic through the authority. `game.connected_peers` counts direct connections, while `game.player_count` counts actors mounted in the world. Use one runner process and a distinct trace path for each guest.

Set `CRIMSON_NETWORK_PROFILE_STORE` to an absolute directory before starting the runner to isolate named profiles. To share the batch launchers' Windows profile directory, use `Join-Path $env:APPDATA 'Crimson Demo\Network Profiles'` as the value. Saved profiles appear in both tools when they use the same directory.

### Run the automated checks

From the workspace root in PowerShell 7, run the simulator harness with your Godot 4.7 Windows console executable. The default suite checks UDP pacing and a headless listen match with the Debug addon.

```powershell
$GodotExe = 'C:\Godot\Godot_v4.7-stable_win64_console.exe'
& .\framework\scripts\Test-NetworkSimulator.ps1 -GodotExe $GodotExe
```

Use `-Suite Full` to include dedicated play, invalid plans and failed assertions, and the ENet debugger fixture. Use `-Suite Stress -Repeat 3` to run three simultaneous dedicated guests through repeated scenarios and reconnects. Every guest must see the whole cohort and receive a distinct player slot. With both addon DLLs installed, add `-Target template_debug,template_release` to check both builds. The harness prints its isolated run directory, keeps the traces for inspection, and reports `CRIMSON_NETWORK_SIMULATOR_SUITE_PASS` for each passing build.

For the broader multiplayer regression suite, run the multiplayer harness with both addon DLLs installed.

```powershell
& .\framework\scripts\Test-Multiplayer.ps1 -GodotExe $GodotExe
```

Add `-Target template_debug` to run just Debug. The harness prints its log directory and checks listen and dedicated movement, delayed and dropped packets, bandwidth caps, lag spikes, timed scenarios, the 1.4-second uplink outage, timeout, and rejoin. Look for `CRIMSON_DEMO_MOVEMENT_PASSED`, `CRIMSON_NETWORK_PACING_PASS`, and `CRIMSON_NETWORK_DEBUGGER_PASS` in the output.

## Start from the command line

On Windows, [Run-LAN.bat](../../../Run-LAN.bat) starts one listen host and three guests by default. It tiles their windows on the primary display. Pass an instance count from 1 to 4 as the first argument and an optional UDP port as the second. [Run-Dedicated.bat](../../../Run-Dedicated.bat) starts the headless server and one visible client and accepts an optional port as its first argument. Without a port argument, each launcher tries its default and selects a free UDP port if the default is unavailable. All instances in that run use the port printed by the launcher.

The LAN launcher opens guests one at a time so their first Vulkan shader compilations do not all start together. With multiple windows, it caps each instance at 60 FPS and gives its staged project a four-thread Godot worker pool. The source project's graphics and environment settings are unchanged. The console prints the temporary run directory with a separate output and error log for each instance. If a process exits during startup, the launcher stops opening windows and closes the processes it started. If a guest stays open and reports a join error, use its **Join Game** menu to retry. A guest that times out also stays open, and the launcher pauses further automatic launches. Run `./Run-LAN.bat 2` for a host and one guest when fewer windows are enough.

From the repository root, use the demo's graphical launch options after Godot's `--` separator. Start the host in one process and the guest in another.

```powershell
& $GodotExe --path framework/project -- --crimson-role=listen --crimson-bind=127.0.0.1
& $GodotExe --path framework/project -- --crimson-role=join --crimson-address=127.0.0.1
```

Set `--crimson-port` on both processes to override the demo's UDP port for that run. A listen host defaults to bind address `*`. Use the host machine's LAN address in Join Game from another machine. The same Join form accepts a dedicated server's address.

Automatic launches use the same menu actions and feedback as interactive play. A failed join keeps the requested address in the form. On success, the game prints `[Crimson] READY listen <port>` or `[Crimson] READY join <port>` after the operation completes. Failed launches print `[Crimson] ERROR <role> <message>`.

## Menu architecture

[Main](main.tscn) contains the [menu view](ui/main_menu.tscn). The [boot script](main.gd) uses the [shared demo session setup](session/network_simulator_session.gd) and gives the booted game handle to the menu. It passes command-line port overrides to that setup and forwards automatic launches to the normal menu actions. The [view model](ui/menu_view_model.gd) awaits `game.play()`, `game.host()`, or `game.join(address)` and presents pending, failure, and ready states.

The boot script starts the player scene's Controls component when a session publishes its local body. The [Controls overlay](ui/control_settings.tscn) lives under GameUI and receives the same input scope. Listen and dedicated servers run [authoritative movement](../../docs/MOVEMENT_NETWORKING.md).

[ScreenNavigator](ui/screen_navigator.gd) owns push, pop, and focus restoration for screen controls. It can be reused by other menus by passing the screen and its initial focus target. Session commands, connection feedback, and the shared [menu theme](ui/menu_theme.tres) are outside the navigator.

The [UI guide](ui/README.md) documents the navigator's calls and the menu input fixture.

## Session and scene ownership

The [shared demo session setup](session/network_simulator_session.gd) selects the [hub](hub/starting_hub.tscn) and [player](player/stationary_player.tscn), reads the player scene's movement settings with `player_controls()`, and enables drop-in LAN play for four total players on UDP port `24830`. The authority chooses a placement from the hub's spawn markers. The player scene supplies the camera and body visuals.

### Configure the multiplayer player

Open [the player scene](player/stationary_player.tscn) and select **Mover → Controller → Controller Style**. Choose **FPS / Shoulder Aim**, **Free-Orbit Third Person**, **Forward-Only Third Person**, or **Lock-On**. Hover a choice to see what the body does when you press Left. Restart each running session after changing the scene so the host and guests use the same style.

The demo opens in first person for FPS / Shoulder Aim. The other styles start with the camera behind the local body so you can see it turn. Press **F6** or click the **right stick** to switch views in any style. In **Lock-On**, press **L** or click the **left stick** to lock onto the north doorway and press it again to release. Move left and right to see the body keep facing the doorway. In a LAN session, use a second game window to watch the other player's body turn.

Under **Ground Movement**, **Move Speed** sets the full-input speed and defaults to **6 m/s**. **Network → Predict Movement** enables prediction for the demo's static level geometry. Select **CollisionShape3D** to edit the capsule or **Camera3D** to change the aim view position.

Open the **On Foot** map in the Inspector to view its **Mappings** and the keys, mouse inputs, and gamepad bindings assigned to each action. Expand **Player Setup** on **Controls** to change action assignments, mouse sensitivity, stick look speed, or pointer capture. [Demo map](player/on_foot.tres) · [Demo layout](player/player_input.tres)

The [demo session setup](session/network_simulator_session.gd) and [native dedicated server scene](../../../server/main.tscn) use movement settings from the same player scene. Restart the host and guests after editing the player scene so they pick up the same settings.

In the hub scene, each room or tunnel has a `Lighting` child that holds its fixtures. The reusable [portal scene](hub/portal/portal.tscn) has its own `Lighting` child for its terminal light. The [concrete wall shader](hub/concrete_wall.gdshader) follows each tunnel wall face and carries texture alignment through the bends.

The [PowerShell harness](../../scripts/Test-Multiplayer.ps1) runs the framework and gameplay fixtures against the debug and release DLLs in isolated projects. Use `-CoreOnly` to run module tests without the demo. Use `-DemoLaunchSmoke` when you want to verify the current demo's dedicated entry point separately. The [demo fixture](tests/session_smoke.gd) covers solo play, independent guests joining an active host, distinct spawn slots, departure and rejoin, and returning to the menu on host loss.

The live Steam fixtures use Transfer Hall to exercise the built-in Steam provider. The custom online provider runs in the regular PowerShell harness. [Live Steam verification](../../vendor/README.md#live-provider-verification)

## Visual references

- The hub uses muted masonry and steel, localized pools of fixture light, fog, and saturated portal apertures. The references for those effects were id Software's [Quake III Arena Shader Manual](https://graphics.stanford.edu/courses/cs448-00-spring/q3ashader_manual.pdf) and the [Q3Radiant lighting guide](https://www.asc.ohio-state.edu/lewis.239/Gauge/q3rmanhtml.htm).
- Reinforced access frames, service conduits, and industrial fixtures draw on [Valve's Half-Life screenshots](https://store.steampowered.com/app/70/HalfLife/).
- The hub's original game textures were generated with built-in ImageGen using the supplied bunker image as a style reference. The prompt requested flat, unlit albedo with coarse late-1990s FPS texture detail for streaked [board-formed concrete](hub/textures/cast_concrete.png), mottled [floor slabs](hub/textures/deck_slabs.png), [oxidized iron](hub/textures/oxidized_steel.png), and [blackened steel](hub/textures/blackened_steel.png). The tiles were prepared for repetition and applied to editable geometry.
