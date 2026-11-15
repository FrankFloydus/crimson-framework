<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Visual Script adversarial checks

From the workspace root, use PowerShell 7 and stock Godot 4.7. The fixture copies only the addon and its own test assets. Logs and generated files stay in the temporary directories printed by the runner.

```powershell
.\framework\project\tests\visual_scripting\Test-VisualScript.ps1 `
    -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe' `
    -VerifyEditor -VerifyDebugger -VerifyPack -VerifyNativeFocus
```

`-BinaryDirectory` tests staging binaries without installing them. `-VerifyNativeFocus` shifts focus between the Godot editor and a separate test window in an isolated project. Omit it on a locked or headless desktop.

Use the dedicated panning stress test on an unlocked Windows desktop with stock Godot 4.7.

```powershell
.\framework\project\tests\visual_scripting\Test-CanvasStress.ps1 `
    -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

The test opens an isolated native editor at 125% scale and generates graphs with 48, 256, and 1,024 cards. It pans the same distance with one or eight viewport motion events per frame and checks that it can still release the pan without changing the saved graph or selection. Input dispatch and frame timings are saved in `results.json` beside the log. Use `-BinaryDirectory` to compare staged Debug DLLs and `-Renderer gl_compatibility` to test the Compatibility renderer. Compare runs on the same machine with the same renderer and viewport size.

Measure canvas panning in a stock editor with an isolated project and generated graphs. Use the same engine and renderer on each run. Pass `-BinaryDirectory` to compare a saved Debug DLL with the current build.

```powershell
.\framework\project\tests\visual_scripting\Test-CanvasStress.ps1 `
    -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

The runner reports median and 95th-percentile frame times and input dispatch times for 48, 256, and 1,024 nodes. It sends one motion per frame and a burst of eight. It checks that the canvas travels the expected distance and the saved graph stays intact.

| Surface | Sequences exercised |
| --- | --- |
| Array, Dictionary, and enum authoring | The 28-case interaction matrix mixes Add, Delete, Enter, pointer and keyboard reorder, dropped drags, invalid input, and Undo/Redo. It checks pending text and stored values separately. It also tests duplicate Array values, Dictionary keys and key type changes, enum identity, and optional function and macro defaults. |
| Variable defaults | Try exact integer limits, malformed exponents, out-of-range vector axes, and successive color changes in the same popup. Choose two generated scenes through Details, reorder and reopen them; clear a saved reference whose scene is missing. |
| Exported variables | Author groups, units, range, and number control style in Details. Reject malformed hints and retain numeric, enum, and resource types. Edit a property in Godot's Inspector, save and reload two nodes with the same script, change the script default, and revert the override. Exercise the same variable through graph Get and Set and round-trip typed node and resource references. |
| Narrow Details pane | Resize the sidebar while an Inspector field has an unsubmitted edit. Check the visible controls and pending text after resizing. Inspect variable, signature, collection, enum, and Custom Script forms at narrow and wide widths. |
| Scene dock attachment | Drop a graph file onto a compatible node, undo and redo the attachment, and save the scene. Try an incompatible base and verify that Undo restores the previous script. In the native editor, verify that an attached Visual Script uses the custom icon beside a node with a stock script button. |
| Main screen and scene references | Open a graph from a stock Scene dock script icon and return to it from another Godot main screen. Switch screens with a draft and an unfinished drag. Check that the draft survives and a fresh drag works. Drop a scene node from the stock Scene dock onto the canvas and select the new reference to add an event from **Signals** in **Details**. |
| Picker, graph cards, and asset drops | Type `cast to` with the canvas previously focused. Use the scrollbar on a long result list and check that the source wire stays visible until the picker closes. Drop generated assets from Godot's **FileSystem** dock onto blank canvas, onto an unrelated card, and as a multiple selection. Click both script choices and both scene choices. Check that dismissing the choice menu leaves the graph unchanged. Verify texture previews, the scene icon fallback, and the same hover state for socket and label. |
| Resource loading and script construction | Compare graph and GDScript preloads for generated scenes, scripts, materials, and imported audio. Try dynamic paths, missing files, type mismatch, saved UIDs, and both constructor defaults and overrides. Run the editor-authored load and constructor scenes. A selective pack includes scenes and scripts through the graph's dependencies. |
| Graph editor | Insert and shake nodes while holding the pointer, interrupt before release, then start a fresh drag. Use forks and multi-selection, pass over the same wire again, and splice inside a function with overlapping node IDs. Check actual displayed connections against persisted graph state and verify one Undo. Exercise search, variable drops, clipboard, renaming, signature edits, and wire commands. The native focus fixture transfers focus during a middle-button pan, then checks the first click on an inline field. |
| Runtime and export | Run control flow, nested loops, typed and mixed collections, disabled nodes, functions and macros, native reentry, and all nine tested lifecycle callbacks. Run a selected-resource export with a scene reachable only through a collection default. |
| Godot member catalogue | Insert calls from typed String, StringName, Vector, Dictionary, and scripted scene pins. Toggle Context Sensitive while keeping the query, place an incompatible action without a wire, and connect a compatible action. Exercise value methods, defaults, class and value static calls, global functions, and variadic calls in the running graph. |
| Constants and singletons | Search for a keycode from a typed input and auto-wire it. Save and reload symbolic constants, including a signed 64-bit limit. Resolve stock singletons, a registered service, and script and scene Autoloads. |
| Node action matrix | Exercise Cast branches, runtime Input Map changes, Make and Break values, collection edits, resource preloads, and suspended Event Graph paths. Use generated scenes and assets to verify persistence, invalid input, lifetimes, and Context Sensitive editing. |
| Inferred graph pin types | Wire the separate lanes of a legacy Make Dictionary after connecting For Each, a reroute, and a lookup. Check the dependent pin types after each wire, Undo, Redo, and reload. Open the contextual picker from the lookup's typed value. Verify that lookups with identical node IDs in the Event Graph and a function use their own variable types, matching Select branches resolve their result, and mixed branches or legacy array items remain open. |
| Collection constructors | Use **Add Pin** and right-click **Remove Pin** on a wired middle input. Verify that later defaults and wires follow their entries. Undo and redo Array item and Dictionary pair removal. Establish each Dictionary type independently, add a pair, and reload the graph. Try incompatible pins and a type change that conflicts with an existing output wire. Check that **Self** uses a compact card and retains its output. |
| Sequence | Add an execution output on the card. Remove a wired middle output and check the order and connections of surviving branches. Verify undo, redo, and the output limit. |
| Custom Script and debugger | Change editor diagnostics from an error through a warning to clean code, exercise autocomplete, and run the saved result. Step into and out of calls; restart the debugger with a breakpoint and an existing watch and check that values from the old run do not carry over. |

These checks validate behavior in stock Godot on Windows. They do not cover every Godot method and signal exposed through the picker, every combination of graph pins, or long interactive sessions on other platforms. Most editor actions use synthetic Godot input or native control signals. The native-focus phase covers focus loss and recovery with real windows.
