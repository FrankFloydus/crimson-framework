<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Demo menu

Instance [MainMenu](main_menu.tscn) in the persistent demo scene. After booting a configured `Game`, pass the handle to `menu.bind_game(game)` once both scenes are in the tree. The view owns input and renders presentation state. [MenuViewModel](menu_view_model.gd) awaits `game.play()`, `game.host()`, and `game.join(address)` and uses the typed outcomes and session lifecycle for feedback.

The [demo boot script](../main.gd) opens the normal multiplayer screen or Join form before invoking its action. The menu relays the settled launch result so a launcher can wait for gameplay readiness. A missing game handle or an active session blocks startup with a message and a failed launch result. During a pending launch, a second request shows feedback while the original launch retains its completion signal. If the game rejects cancellation, the pending screen shows a retry message and keeps Cancel available.

Mount each page as a direct `Control` child of [ScreenNavigator](screen_navigator.gd). Call `reset(page, first_focus)` for the root and `push(page, first_focus)` to open another page. `pop()` restores the most recent usable focus on the revealed page. Configure directional and Tab neighbors on the page controls. Use `restore_focus()` after a temporary interaction lock, as the menu does after cancelling a connection.

Arrow keys, Tab, or the D-pad move focus. Confirm activates the focused action. Escape or the controller's Back action ends address editing or returns to the previous screen. The address field accepts keyboard input. Options remains visible and disabled.

[ControlSettings](control_settings.tscn) is an in-game overlay for the local player. The demo passes its local `Controls` to `setup()`. The overlay listens for the scoped `settings` action and suspends gameplay input while visible. It shows the current Move Forward binding, updates it through `Rebinding`, and returns focus to that row after capture. Resume closes the overlay so `Controls` can restore the prior pointer state.

Solo, LAN hosts, and LAN guests attach the overlay after their local player is ready. Rebinding changes that client's controls. Opening the overlay stops sending new steering and look intent from that player while the multiplayer session continues.

Open the demo's **On Foot** map in the Inspector to change the `settings` and `move` bindings. The default layout binds `settings` to **Escape** and gamepad **Start**. A custom layout without a `settings` button skips this overlay. A layout with `settings` but with no `move/key_up` slot shows Resume without the Move Forward row. [Input guide](../../../src/modules/input/README.md)

[NetworkDebugger](network_debugger.tscn) opens a separate developer window with its own compact theme. Press **F8** or controller **View** to open it. The focused tool owns input and releases the gameplay pointer. Choose **Focus game** to restore gameplay while charts keep updating. Press **F8** or **View** in the game to return to the open tool. **Escape**, controller **B**, or the title-bar close button dismisses the window. If Controls is open, finish or cancel rebinding before opening the debugger.

The [debugger view model](network_debug_model.gd) reads `game.network_diagnostics()` and applies test conditions through the demo relay on a loopback ENet guest. **Monitor** shows connection and replication measurements alongside each direction's payload rate, bandwidth cap, and packet counters. In **Simulator**, choose a preset or edit delay, jitter, loss, and upload and download caps. **Apply** changes the live link. **Save profile** stores the draft under a reusable name. Schedule a **Lag spike** to add temporary delay, or a **Connection outage** to block traffic in the selected direction while you play.

In **Scenarios**, load a template, edit or reorder the stages, and choose **Run scenario** to apply them in sequence. **Loop** repeats the sequence. The previously applied profile returns when the sequence completes or you choose **Stop scenario**. Scenario drafts live in the open game process. Named simulator profiles are stored in the local debugger directory so they can be selected again after relaunching.

**Events** keeps a bounded, copyable log across reconnects. The menu remembers the last successful direct join and reuses its ordinary launch action for **Reconnect**. The separate [identity label](local_player_identity.gd) reads the assigned spawn slot and role from the same typed diagnostics.

From the repository root, run the UI fixtures after importing the project in Godot 4.7. The fixtures build their own controls and worlds, so changes to the game's art, scene, and layout do not affect the checks.

```powershell
& $GodotExe --headless --path framework/project --script res://demo/ui/tests/menu_smoke.gd
```

The test prints `CRIMSON_MENU_PASS` after exercising menu input, focus restoration, launch feedback, join cancellation, and the solo launch.

The debugger fixture opens its own loopback host and guest. It checks focus, profile controls, a lag spike, a scenario, and reconnect. Use a temporary profile store for the run and its follow-up process, which checks that the saved profile can be loaded again.

```powershell
$env:CRIMSON_NETWORK_PROFILE_STORE = Join-Path $env:TEMP ("crimson-ui-" + [guid]::NewGuid().ToString("N"))
& $GodotExe --headless --path framework/project --script res://demo/ui/tests/network_simulator_ui.gd
& $GodotExe --headless --path framework/project --script res://demo/ui/tests/network_simulator_ui.gd -- --verify-profile
```

The expected markers are `CRIMSON_NETWORK_SIMULATOR_UI_PASS guest` and `CRIMSON_NETWORK_SIMULATOR_UI_PASS profile`.

The title uses Barlow Condensed Black under the [SIL Open Font License](fonts/OFL-Barlow.txt). The [menu backdrop](menu_backdrop.png) is original concept art generated for this demo with ImageGen.
