<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Demo menu

Instance [MainMenu](main_menu.tscn) in the persistent demo scene and assign its **Game** field to the configured `CrimsonGame` node. When composing the scene from code, assign `menu.game = game` before adding the menu to the tree. The view owns input and renders the presentation state. [MenuViewModel](menu_view_model.gd) sends `play_solo()`, `host_game()`, and `join_game(address)` to the game and observes the returned operations and session lifecycle for menu feedback.

The [demo boot script](../main.gd) opens the normal multiplayer screen or Join form before invoking its action. The menu relays the settled launch result so a launcher can wait for real readiness.

Mount each page as a direct `Control` child of [ScreenNavigator](screen_navigator.gd). Call `reset(page, first_focus)` for the root and `push(page, first_focus)` to open another page. `pop()` restores the most recent usable focus on the revealed page. Configure directional and Tab neighbors on the page controls. Use `restore_focus()` after a temporary interaction lock, as the menu does after cancelling a connection.

Arrow keys, Tab, or the D-pad move focus. Confirm activates the focused action. Escape or the controller's Back action ends address editing or returns to the previous screen. The address field accepts keyboard input. Options remains visible and disabled.

From the repository root, exercise the real boot scene after importing the project in Godot 4.7.

```powershell
& $GodotExe --headless --path framework/project --script res://demo/ui/tests/menu_smoke.gd
```

The test prints `CRIMSON_MENU_PASS` after exercising menu input, focus restoration, and the solo launch.

The title uses Barlow Condensed Black under the [SIL Open Font License](fonts/OFL-Barlow.txt). The [menu backdrop](menu_backdrop.png) is original concept art generated for this demo with ImageGen.
