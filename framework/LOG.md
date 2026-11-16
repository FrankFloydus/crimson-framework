<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Work log

- **Week starting:** 2026-11-12
- **Next archive date:** 2026-11-19 (Europe/Rome)

## Added

- Added a shared Ability Defaults editor for GDScript and Visual Script, with UID-based storage and typed tag snapshots per grant. [Gameplay Abilities](src/modules/abilities/README.md) `Pending push`

- Added script-defined Gameplay Abilities with actor-owned grants, per-execution instances, Visual Script authoring, and coordinated authority. [Gameplay Abilities](src/modules/abilities/README.md) `Pending push`

- Added the Gameplay Attributes Project Settings tab, stable definitions, generated C++ accessors and subscriptions, and coordinated attribute replication. [Gameplay Attributes](src/modules/attributes/README.md) `Pending push`

- Planned a Project Settings registry for Gameplay Attributes with stable IDs, generated C++ accessors and change subscriptions, and a coordinated replication milestone. [Gameplay Attributes proposal](docs/GAMEPLAY_ATTRIBUTES_PROPOSAL.md) `Pending push`

- Added `state.control.stun` to the project tag catalog and saved a Find Tag node in the stationary player demo graph for editor inspection. `2f62248b0a290950eaa7b3acac407cff311b9708`

- Added a Gameplay Tags category and picker-backed unwired tag inputs in Visual Script. [Gameplay Tags](src/modules/gameplay_tags/README.md) `2f62248b0a290950eaa7b3acac407cff311b9708`

- Added Gameplay Tag and Gameplay Tag Container Inspector chips, a shared catalog picker, Godot bindings for tags and nested queries, and typed Gameplay Tags calls in Visual Script. [Gameplay Tags](src/modules/gameplay_tags/README.md) `2f62248b0a290950eaa7b3acac407cff311b9708`

- Added native Gameplay Tags, including scoped catalogs, hierarchical matching, and explicit tag containers. Recorded the gameplay system roadmap. [Gameplay Tags](src/modules/gameplay_tags/README.md) `2f62248b0a290950eaa7b3acac407cff311b9708`

- Added a read-only React Flow viewer to the docs and a stock Godot exporter for saved Visual Script graphs. The first graph tutorial embeds an interactive graph that readers can copy into a new Visual Script. `d70776d8be9eaee51a5fbfa0ce2318f2bf966e7b`

- Added an illustrated Visual Scripting node catalog with individual pin references, example wiring, and automated stock Godot captures. Kept one short tutorial, separated conceptual guides from setup steps, and reduced emphasis on routine labels. `d70776d8be9eaee51a5fbfa0ce2318f2bf966e7b`

- Expanded the customer Visual Scripting guides with pin-level node references, illustrated editor and gameplay checkpoints for all three tutorials, and reproducible Godot screenshot tooling. `d70776d8be9eaee51a5fbfa0ce2318f2bf966e7b`

- Added the customer Visual Scripting guide with a Godot editor walkthrough, language and node documentation, troubleshooting, and runnable tutorials. Captured the editor images from an isolated Godot project. `d70776d8be9eaee51a5fbfa0ce2318f2bf966e7b`

- Added runtime Load Resource with typed results and success/failure paths. Expanded Preload Resource authoring to saved scenes, scripts, and other Godot resources with native thumbnails when available. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`
- Added Create from Script to construct a saved GDScript from typed initializer inputs and use its instance and members in the graph. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added typed asset drops onto the Visual Script canvas and optional Inspector groups, units, and range controls for exported variables. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added typed singleton and Autoload references, casts, Input Map polling, Make and Break value nodes, collection construction and edits, resource preloads, and Event Graph waits. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added Godot global constants such as `KEY_ESCAPE` to the Visual Script picker. Search for a symbol or browse its enum group, then wire the constant's value into the graph. [Visual scripting](docs/VISUAL_SCRIPTING.md#read-properties-and-call-methods) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added a distinct Visual Script resource icon for `.crimsonscript` assets in Godot. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added Inspector export for Visual Script instance variables. Each attached node can save its own override while the graph reads and writes the same variable. [Visual scripting](docs/VISUAL_SCRIPTING.md#give-each-scene-node-its-own-setting) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added typed Godot value methods, global utility functions, native static and variadic calls, and members from typed scene scripts to the Visual Script picker. [Visual scripting](docs/VISUAL_SCRIPTING.md#read-properties-and-call-methods) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added typed resource defaults and collection-aware For Each pins. A loop over scene assets offers PackedScene methods from Item. Selective export keeps scenes referenced only by a collection default. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added variable container conversion with typed Array elements and Dictionary keys and values. Type changes show their effect on defaults and wires before applying as one undoable edit. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added drag reordering for Visual Script declarations, enum members, and collection defaults, and for Input Map actions, bindings, and modifiers. Moving an entry keeps its graph references or binding ID. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) [Input Map](src/modules/input/editor/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added direct variable-to-pin drops in Visual Script. Dropping a getter or Ctrl-dropping a setter onto a compatible pin inserts the node and its connection in one undoable action. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added reusable Input Action and Input Map resources, an FPS preset, separate Mouse X, Mouse Y, and Mouse XY binding sources, and ordered Invert, Scale, Normalize, and Response Curve modifiers. [Input guide](src/modules/input/README.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added an authored native dedicated server entry. The headless Godot server boots from its scene without a GDScript bootstrap and shares the demo player and world. [Dedicated server](../server/AGENTS.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added editable Visual Script Custom Script card names in **Details** and a code badge on the upper right of each card. [Custom Script](docs/VISUAL_SCRIPTING.md#write-a-custom-script-node) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added `#@input` and `#@output` declarations to Custom Script. Pins declared in code work alongside pins from **Details** and appear in stock GDScript autocomplete. [Declare pins in code](docs/VISUAL_SCRIPTING.md#declare-pins-in-code) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added the Visual Script Custom Script node with typed pins, inline GDScript editing, stock Godot autocomplete and diagnostics, and access to the live graph's declarations. [Custom Script](docs/VISUAL_SCRIPTING.md#write-a-custom-script-node) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added rename, cut, copy, duplicate, delete, and descriptions for Visual Script declarations. Native methods and signals show descriptions in the picker and on graph cards. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added 24 comparable FPS handler benchmarks across combat, items, movement, and objectives. The shared runner checks all state changes and records Crimson, Orchestrator, and paired GDScript timings. [FPS benchmark](docs/VISUAL_SCRIPTING.md#benchmark-24-fps-gameplay-handlers) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added a gameplay graph benchmark that measures a health pickup's Godot signal, authored function, and native reads and writes. The harness checks each player's final health and can compare Crimson and an external Orchestrator release with the same GDScript reference. [Visual scripting checks](docs/VISUAL_SCRIPTING.md#benchmark-a-health-pickup) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added a repeatable million-iteration Visual Script and GDScript benchmark with matched batches, checksum verification, and timings for the installed release DLL. [Visual scripting checks](docs/VISUAL_SCRIPTING.md#run-the-visual-scripting-checks) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added hover inspection and persistent pin watches in the Visual Script debugger. The paused view shows the active card, readable values, and where each input came from. [Watch a pin](docs/VISUAL_SCRIPTING.md#watch-a-pin) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added native Godot breakpoints, live pin and variable inspection, and stepping for Crimson Visual Scripts. Added row editors for array and dictionary defaults, and authored enums in **My Script**. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added visual graph clipboard, search and reference navigation, Quick Jumps, comment frames, dependency selection, and wire editing tools with batch connect, bypass, insertion, exchange, highlighting and sweep cutting. [Graph tools](docs/VISUAL_SCRIPTING.md#edit-and-navigate-a-larger-graph) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added **Switch on Enum** for native Godot class enums and **Add Custom Event** in the Event Graph picker. [Visual scripting guide](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added native signals to the visual script's typed object pin picker. The resulting event connects to the object and exposes its sender and typed signal arguments. [Visual scripting guide](docs/VISUAL_SCRIPTING.md#react-to-an-event) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added a Visual Script workspace with scene references, typed instance and local variables, function and macro graphs, and native and custom event authoring. Added flow nodes, graph extraction, and an example that runs a typed function from a branch. [Visual scripting guide](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added typed Godot property and method nodes to the visual script editor. Drag an object output into empty space to browse its members, or drop an audio asset into the resource field on **Set Stream**. [Visual scripting guide](docs/VISUAL_SCRIPTING.md#play-a-sound-when-a-scene-starts) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added Crimson Visual Script as a Godot scripting language. Create a script on a scene node, connect On Ready to Print, and browse native Godot node classes in the graph editor. [Visual scripting guide](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`


- Added a native editor component catalog with reusable controls and compositions, and an Input Map editor for standalone map and action assets. The demo layout now references the On Foot map and retains existing rebind slot IDs. [Input guide](src/modules/input/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added a floating Input Actions editor with action maps, typed actions, and physical binding authoring. Local Controls can enable maps independently while keeping the same action and rebinding IDs. [Input editor guide](src/modules/input/editor/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added four player controller styles on the Mover component and profile, with independent camera aim and body facing in fixed-world multiplayer. Added a target selection call for Lock-On and wrapped Inspector tooltips for the style options and Mover values. [Player controls](docs/PLAYER_CONTROLS.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added a local third-person camera preview to show the demo character's facing during solo and multiplayer movement tests. The Lock-On preview uses the north doorway as a test target. [Demo guide](project/demo/README.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Exposed grounded acceleration, deceleration, friction, gravity, air control, and jump speed on the player Mover component. Live physics and multiplayer prediction share the tuning calculation. [Mover guide](src/modules/mover/README.md#configure-movement-on-a-player-scene) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added a default grounded jump on Space in the demo and Input Layout preset. The server validates takeoff and owning clients predict and replay the same jump command. [Player controls](docs/PLAYER_CONTROLS.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added a bounded typed binary codec and game-scoped custom codec registry. Fixed-world movement uses the native codec for input and pose records. [Network](src/modules/network/README.md#typed-binary-messages) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added directional bandwidth throttling, named simulator profiles, temporary latency spikes, and timed network scenarios to the demo debugger. The same simulator runs headless JSON plans and reports machine-readable diagnostics. Added focused and concurrent network regression suites. [Network debugger](project/demo/README.md#network-debugger) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added fixed-tick prediction and authoritative reconciliation for grounded movement against static collision. The demo opts in through its shared player profile. [Movement networking](docs/MOVEMENT_NETWORKING.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added a demo network debugger with typed game diagnostics, loopback ENet delay and loss, and disconnect and reconnect controls. Playable windows display their assigned player slot and role. [Demo network debugger](project/demo/README.md#network-debugger) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added native Mover, scoped input and rebinding, and a local Player Control profile. Single Player uses default movement and look and can rebind Move Forward from GameUI. [Player controls](docs/PLAYER_CONTROLS.md) `f83da634214894e38ca02b52fad5f450f035bf6c`
- Added authority-driven movement and look for the immediate-world profile, including guest pose updates and late join. [Movement networking](docs/MOVEMENT_NETWORKING.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Implemented code-first Game boot, scoped typed service overrides, direct game actions, standalone Inventory grids, coordinated pickups, and the Steamworks facade. Migrated the demo and dedicated entry point to code and hid legacy setup classes from Add Node. [Quickstart](docs/CODE_FIRST.md) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added typed dedicated Steam stats and achievements for the exact admitted peer, Wallet buyer information, Cloud write-batch hints, Workshop subscriptions and legal agreement actions, and Steam notification session launch hints. Verified that an anonymous Spacewar dedicated server loaded the signed-in guest's Steam stats and retired the cache handle when the guest left. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Verified a live Spacewar Winner unlock through the rendered achievement preview. Steam acknowledged the store and unlock, the operator saw the Steam notification, and a fresh process read the achievement as already unlocked. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added a self-service Steam testing guide, bulk PowerShell suites, a rendered one-button achievement unlock, an opt-in live encrypted ownership fixture, and a repeatable offline DRM smoke test. Passed 28 signed Spacewar cases on both Windows builds, 29 Debug cases each for missing-runtime and SDK-off, and six trusted publisher mocks. Confirmed that the rendered preview read Spacewar's locked Winner and became ready without unlocking it. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added standalone authenticated Steam Sockets, observed connection path diagnostics, and hosted Steam Datagram Relay through a game-owned trusted coordinator. Verified direct loopback packet delivery and both dedicated connection variants' missing-coordinator and unprovisioned deployment handling. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added native Steam dedicated hosting, ticket-bound admission, and a typed server browser. Verified an anonymous secure Spacewar server and signed guest in separate processes, including invalid-proof denial, identity pinning, two fresh joins into the same world, and clean teardown. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added typed client checkout and trusted publisher authentication, Wallet, Inventory, Notifications, and moderation. Verified the client against a mock backend in Spacewar and exercised trusted operations with no-socket publisher fixtures. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added Steam Networking Messages with typed peer permissions, independent delivery modes, bilateral binding freshness, and exact Game admission checks. Verified signed Spacewar capability and authorization boundaries, plus missing-runtime behavior. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added typed Steam Voice capture and decoding, Game-owned push-to-talk routing, and Steam Parties discovery, owner-bound reservations, and scoped beacon publication. Verified signed read-only Spacewar and missing-runtime fixtures. Added explicit two-account voice and beacon fixtures. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added typed Steam Cloud sharing, leaderboard submissions, Timeline phases, friend and lobby snapshots, Workshop, screenshots, Remote Play, Device, Input, browser, media, Inventory, and client ticket services. Verified signed Spacewar read and lifecycle paths, including private lobby self-chat and Web API and encrypted ticket issuance. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added the trusted publisher build and a SteamPipe DRM wrapper. Verified the wrapper's arguments, failure cleanup, and destination protection with a controlled SteamCMD stub. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added `GameDefinition.boot()`, typed Steam achievement and stats results, the configured HTTP facade, and standalone Overlay actions. Verified the boot path, signed Spacewar reads and HTTP, and rendered Achievements overlay callbacks. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Registered the Steam editor icon for native Steam nodes and configuration types. Included the asset and upstream license in the addon layout. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Drafted a standalone Crimson Collections design with typed Godot views, fluent operations, snapshot semantics, and performance verification for approval before Steam API work. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Drafted the full Steam public API proposal for approval, with initialization, function contracts, FPS integrations, and verification plans for every feature. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added the standalone Steam client with typed Apps, Friends, and Utils services, shared runtime ownership, and isolated CLI fixtures. Documented the full Steamworks architecture and feature-by-feature live test requirements. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added Steam HTTP, Cloud, stats, achievements, leaderboards, Game Recording timelines, and SDK diagnostics. Added one-call client boot, direct HTTP, Cloud, and top-score actions, and branchable stat edits that submit together. Added CLI fixtures for the signed-in and unavailable paths. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Added game-scoped Session and Scene Loader injection, global Crimson game and scene facades, and native dedicated startup. Added root Windows launchers for a headless server and for up to four LAN instances. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added typed gameplay transport factories with native ENet defaults and verified a scripted demo adapter through the same host, late-join, rejection, and teardown paths. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added asynchronous online directory hooks, typed join endpoints, and verified identity before session admission. The demo's custom online provider passed separate-process host, authorized guest, and denied guest checks on both DLL variants. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added a native Steamworks listen provider with lobbies, P2P transport, and provider identity checks. Verified both sides of a two-account Spacewar match and the Windows export dependency selections. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added two-account Spacewar fixture roles and verified a portable test bundle from a fresh extraction. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added explicit Steam lobby invites for a selected SteamID64. Verified the invitation, guest admission, shared world, bidirectional gameplay RPCs, and teardown across two accounts after the graphical host's Steam invite dialog failed to open. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added native World Session startup for a configured world and player scene, with fluent and Inspector setup, solo play, and join-in-progress spawning. Migrated the demo to the native facade. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Built the demo boot menu and Transfer Hall from scratch. Added reusable MVVM menu navigation, solo and direct-address multiplayer spawns, three winding portal tunnels, reference-matched industrial textures, and per-area light groups. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added the first demo slice with the original Quake menu and START hub imported locally, solo and join-in-progress ENet sessions, stationary Ranger spawns, and a free-fly inspection camera. `Superseded before publication`
- Added the per-commit addon ZIP and Git tag workflow, with a permanent release ledger in DEVLOG.md. `e83f32f806417f36379fb381e6f9cb5b6461e836`
- Created the Quake-style co-op demo folder and documented its feature workflow and implementation inventory. Clarified in the root guide and role definitions that the specialists cover the framework and demo. `e83f32f806417f36379fb381e6f9cb5b6461e836`
- Added Crimson Codex specialists for architecture, networking and anti-cheat, and game UI, with ownership rules and primary references. `e83f32f806417f36379fb381e6f9cb5b6461e836`
- Published the Godot 4.7 C++ framework with integrated Game Flow, save/scene activation, replication, API docs, and headless tests. `847cccea9473b7561001ea2c399eba1fd651a0ab`
- Added seven scoped agent guides and verified discovery in Codex. `e83f32f806417f36379fb381e6f9cb5b6461e836`
- Started the weekly work log and archive. `e83f32f806417f36379fb381e6f9cb5b6461e836`

## Changed

- Added actor-owned Ability System Components for local and coordinated Attributes. Assign sets on the actor, then initialize values from gameplay code. [Gameplay Attributes](src/modules/attributes/README.md) `Pending push`

- Allowed handbook graphs to paste into existing Visual Scripts. Resolve declaration clashes when pasting and convert duplicate lifecycle entries to wired Custom Events. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-and-navigate-a-larger-graph) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added a gray hover highlight to execution pin rows and tightened typography in Visual Script graph context menus. [Visual scripting](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made Visual Script Details compact and responsive, added live toolbar search with results below the field, framed functions when opening them, and moved Sequence output editing onto the card. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made Make Dictionary infer key and value types independently. Add Pin now gives each new input its established type. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Updated Make Array and Make Dictionary with collection icons, inferred pin colors, an Add Pin command, and undoable removal from the input pin's context menu. Displayed Self as a compact card. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Simplified the Visual Script toolbar with a graph breadcrumb and centered search. Search dims nonmatching canvas cards while open. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`
- Enabled automatic saving for committed graph edits and moved Reload from Disk into Graph Tools. The editor keeps pending edits if the script changed on disk. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`
- Made declaration group hover span the full row and put variable export controls before the names, with types aligned on the right. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Removed the Event Graph row from the Visual Script sidebar. Open it from Graph Tools while editing a function or macro. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Moved Visual Script into Godot's main editor toolbar after Asset Store. The graph uses Godot's Scene dock for node drops and keeps its draft when switching screens. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`
- Placed script declarations above Details in the right sidebar and removed the My Script heading, leaving the canvas on the left. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made Visual Script Details sections collapsible and compacted the node picker. Wires remain visible while choosing a node, and socket and label hover share a type-colored highlight. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Show the Visual Script's native base class and Godot icon in the graph toolbar so authors can see which nodes can use the graph. [Visual scripting](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Moved Visual Script node defaults beside their input labels and sized the fields to their values. Paired outputs stay on the same row. [Visual scripting](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Put a dragged pin's own actions directly at the top of the Visual Script picker and group inherited actions under their declaring classes. [Visual scripting](docs/VISUAL_SCRIPTING.md#read-properties-and-call-methods) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Flattened Visual Script picker actions for the dragged pin, added a Context Sensitive toggle, and made scene reference cards compact with typed pin tooltips. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Updated typed collection markers in My Script. Arrays show their element type, and Dictionaries show their key and value colors in one icon. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Ranked the remaining Visual Script gaps by player and author impact and implementation effort, with Inspector-exported variables first. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Compared Orchestrator 2.5 stable and the local Crimson Visual Script implementation in both directions across the runtime, node catalog, editor, and debugger. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made Array defaults use one row per value and Dictionary defaults use labelled key and value fields. Removed the duplicate variable-node commands from Details and reused the shared typed Select for collection entries. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Strengthened Visual Script node role colors, grouped the toolbar commands, and distinguished current graphs, sections, and declarations in My Script. Interactive data pin labels highlight their sockets on hover. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Compared stock gameplay behaviors, starter projects, and editable visual scripting examples in Unreal, GameMaker, and Construct with Crimson's current components and addon packaging. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Installed the approved editor redesign in the local Godot project. Matched compact Select density to the Linear reference, kept matching command density in Input Map's action menus, and improved filtered results and the Design Style layout at higher editor scales. [Editor components](src/editor/ui/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Compared public visual scripting feedback and studies with Crimson's graph editor to identify opportunities in gameplay examples, node discovery, reuse across scripts, debugging, and review. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Isolated framework, gameplay, and Steam behavior tests from editable demo assets. Tests supply their own scenes, input mappings, and settings. The module suite boots without the demo, and live demo startup is a separate check. [Test guidance](AGENTS.md#verification) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Redesigned the Visual Script and Input Map editors and their shared component catalog. Added a reusable spacing scale and Design Style guide, unified command and Select popup styling, and reused shared dialogs, field rows, and graph card states. The offline review gallery now opens screenshots in a full-window viewer. [Editor components](src/editor/ui/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Refined Visual Script cards and debugging with typed headers, a two-level grid, compact property reads on Self, full-card hatching for disabled nodes, a compact pause badge, and aligned debugger values. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added shortcut keycaps to editor tooltips and linked the staged Godot and Linear evidence from the offline review gallery. [Editor components](src/editor/ui/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed Input Map group and selection contrast, the active capture icon, rounded empty states, and the native asset save dialog's corner artifacts and button styles. [Input Map editor](src/modules/input/editor/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Replaced Input Map's Choose Action Asset window with a styled project asset search. Refined Visual Script Details to distinguish text fields and type choices, and moved Custom Script diagnostics into a popup opened from the code editor header. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Aligned the Visual Script and Input Map editors with the shared editor library. Added a reusable graph node to the component catalog and made scene signals easier to scan and add from Details. [Editor components](src/editor/ui/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Reduced Visual Script argument and slot allocations, linked signal callbacks to compiled entries, reused native checks for compatible objects, and compiled eligible integer and boolean operands for native calls. On the 24-case FPS suite, the release took 52.1% less time than its starting build and 25.9% less time than Orchestrator 2.5 stable by geometric mean. [FPS benchmark](docs/VISUAL_SCRIPTING.md#benchmark-24-fps-gameplay-handlers) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Compiled native signal signatures and linked authored function input slots ahead of dispatch. Common native calls use a small stack buffer for argument pointers. Live debugging retains named pin snapshots, and each reentrant call keeps its own frame. [Gameplay benchmark](docs/VISUAL_SCRIPTING.md#benchmark-a-health-pickup) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Removed the C++ wrapper lookup from Visual Script owner checks and wrote loop indices directly into their compiled slots. The same million-iteration benchmark fell from 107.948 ms to 76.525 ms in fresh paired runs. Added coverage for a native signal handler detaching the active script. [Visual scripting checks](docs/VISUAL_SCRIPTING.md#run-the-visual-scripting-checks) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Compiled typed integer and boolean expressions feeding Visual Script branches and variable setters, and advanced through cached execution edges without copying them. The million-iteration release benchmark dropped from 1,815.482 ms to 110.597 ms; live debugging still uses the authored graph. [Visual scripting checks](docs/VISUAL_SCRIPTING.md#run-the-visual-scripting-checks) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Visual Script resolves node operations, pin connections, and variable slots when a graph is prepared. Running actions reuse value storage while the debugger still shows the original graph and pin identities. [Visual scripting checks](docs/VISUAL_SCRIPTING.md#run-the-visual-scripting-checks) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added cursor previews for scene nodes and declarations dragged from the sidebar. Drop a variable for a getter or hold Ctrl for a setter; one click places a node from the picker. [Visual scripting](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made **Shake Node Off Wire** work during ordinary drags, including groups on separate routes, with one Undo for movement and wire reconnection. Branch Auto Insert now keeps the original flow on True and preserves the Condition wire. [Visual scripting](docs/VISUAL_SCRIPTING.md#work-with-several-wires) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Moved Visual Script Continue and Step controls into the graph and added a resizable live values and call stack panel. Connected data pins now fill with their type color. [Visual scripting](docs/VISUAL_SCRIPTING.md#pause-and-inspect-a-running-graph) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Split graph editing, pin, and wire actions from the creation picker. Added compile disable for linear actions and function and macro settings on call cards. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Applied the approved framed Visual Script workspace using a dark palette, compact controls, and type-colored borders for variable nodes. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Restyled the visual script workspace with a compact toolbar, distinct node silhouettes, clearer execution sockets, a stronger selection outline, and compact asset fields. Graph-node deletion takes effect immediately and supports Undo. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Gave Visual Script nodes distinct role headers and outlines that stay visible when zoomed out. Grouped the Details fields and added a searchable type picker that uses pin colors. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Moved Promote to Variable to the top of the pin drag picker and let variable nodes open the variable's full Details from the canvas. [Variables in the graph](docs/VISUAL_SCRIPTING.md#store-a-setting-and-call-a-function) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Updated the Visual Script node picker and canvas with member groups by declaring class, typed pins, reroutes, wire replacement, and alignment commands. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Removed the Input Map header and moved Save and map commands to the footer. The demo and FPS map use explicit modifiers for directional bindings and retain their rebinding slot IDs. [Input Map editor](src/modules/input/editor/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Grouped player speed and direction under Ground Movement and prediction under Network. Forward Direction explains the view, character, and world choices. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Made the player Mover component's inline fields its sole source of grounded tuning and removed its Rule Override. `MotionRule` remains available for standalone Mover and explicit player profiles. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Checked Unreal Engine 5.8.4 Mover's default character setup, jump input and impulse, and optional modes against the installed source. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Renamed the native modules to `network` and `scene_manager`, moved rebinding under Input and removed the empty logging directory. [Module map](README.md#modules) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Moved demo movement and input tuning to Mover and Controls components on the player scene. LAN and dedicated sessions read the same authored movement settings. [Player controls](docs/PLAYER_CONTROLS.md) `f83da634214894e38ca02b52fad5f450f035bf6c`
- Replaced the demo's network overlay with a developer window with live monitoring, configurable packet simulation, timed outages, and a session event log. [Network debugger](project/demo/README.md#network-debugger) `f83da634214894e38ca02b52fad5f450f035bf6c`
- Removed blanket authoring requirements from the workspace and documentation agent guides. `f83da634214894e38ca02b52fad5f450f035bf6c`
- The Windows LAN launcher tiles its host and guest windows in the primary display's work area. [Demo launchers](project/demo/README.md#start-from-the-command-line) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Reviewed the Input and Mover API drafts with the demo-menu chat and proposed scoped facades for pointer capture, action reads, modal suspension, rebinding, and adapter replacement. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Reviewed the installed Unreal Engine 5.8.4 Mover source and reported how Crimson could extract a native kinematic movement component from a Transfer Hall controller, with a staged path to multiplayer prediction. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Audited Laravel 13.x and Cashier Stripe and revised the [API simplification proposal](docs/API_SIMPLIFICATION_PROPOSAL.md) around code-first Game, Inventory, and Steamworks, with scoped services, complete actions, and a migration for authored framework nodes. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Captured the Steam API design lessons in the framework-wide guidelines, including Godot autocomplete, typed results, asynchronous success and partial effects, and optional service fallbacks. `36d0a7892d7979e825504fdbbfa4bcec0a129d7d`

- Audited the current Steam implementation against Valve's documentation and the approved API. Identified gaps in offline startup, Steam launch and join controls, dedicated server metadata, and publisher notification and moderation behavior. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Required native Godot class XML for public methods, properties, enum values, and signals, and added Steam class documentation to the editor build. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Migrated Godot-facing classes to brand-neutral API names, including `GameRuntime`, `GameServices`, `SteamClient`, and `SteamProvider`. Updated the current demo, server, Inspector documentation, and fixture references. Deferred Collections. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Clarified Collections as a globally accessible framework module and documented shared adoption across game, inventory, save, scene, session, and tools APIs. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Refined the Steam public API proposal with typed achievement snapshots, concrete result and collection bindings, and Godot completion checks. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Kept explanatory Inspector property documentation mandatory and removed the manual tooltip test requirement from the agent guides. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Organized native game composition, fixed-world startup, adapters, and vendor providers by ownership. Moved each Inspector class reference beside its owner and centralized integration class registration. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Routed solo, listen, dedicated, and late-join demo mounts through the shared Scene Loader. Guests stage the active world and acknowledge the initial roster while existing players retain their mount. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Moved the framework, Godot project, build files, fixtures, and releases under `framework/` and updated workspace entry points and build workflows. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Routed demo solo, hosting, and joining through CrimsonGame with explicit listen rules and scene-local spawn and player-view components. Added Inspector tooltips and made them mandatory for exposed properties. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Reworked Foundry Gate's forecourt, lateral tunnel entrances, and lighting from measurements of Quake Enhanced START, and applied the six approved indexed atlas tiles. `Superseded before publication`
- Replaced the demo's imported menu and starting map with an original bitmap Crimson menu, a convex-block hub, original repeating tiles, and a stationary inspection suit. Added lateral difficulty choices and one forward travel portal to the Episode Hall. Guests use the host's selected difficulty. Applied matte 3D materials and corrected overlapping block faces. `Superseded before publication`
- Refactored the demo menu into reusable screen navigation with focus history and a typed session presentation state. Assessed the native UI module boundary against the demo's current use. `e83f32f806417f36379fb381e6f9cb5b6461e836`
- Made Crimson's Laravel-inspired public API guidelines mandatory for all implementers, including independent Network and UI work. `e83f32f806417f36379fb381e6f9cb5b6461e836`
- Documented the mandatory workflow in the root agent guide and linked it from the README. `e83f32f806417f36379fb381e6f9cb5b6461e836`

## Removed

- Removed the unpublished Attributes assets and documentation, and rebuilt and verified the Crimson 0.0.6 debug and release addon DLLs. `Superseded before publication`

- Removed the experimental Inventory V2 API, prototype scenes, artwork, and tests. The original spatial inventory is the supported implementation. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Retired the unused online provider, its native SDK wiring, and its integration fixtures. The Steam and ENet launch paths remain available. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Retired the demo's Quake asset importers, playback backdrop, BSP shaders, and runtime asset dependency. `Superseded before publication`
- Retired the local FPS prototype and its dedicated tests before publishing the initial framework snapshot. `847cccea9473b7561001ea2c399eba1fd651a0ab`

## Fixed

- Hid the Gameplay Attributes backing dictionary from General settings so sets are authored in the dedicated Gameplay Attributes tab. [Gameplay Attributes](src/modules/attributes/README.md) `Pending push`

- Matched Gameplay Tags picker checkboxes to the selected hierarchy, including checked ancestors, subtree clearing, and parent fallback. Changed the Inspector container to a clickable field with square, wrapping chips. [Gameplay Tags](src/modules/gameplay_tags/README.md) `2f62248b0a290950eaa7b3acac407cff311b9708`

- Fixed the standalone demo launch smoke test to stage addon editor assets before Godot imports the demo. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Corrected the ControlsComponent class reference to use the current Edit Input Map button label. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Carried inferred Visual Script pin types through adaptable nodes, including constructor collections, reroutes, lookups, and For Each. Collection sockets show the resolved element, key, and value colors. [Visual scripting](docs/VISUAL_SCRIPTING.md#choose-which-nodes-run) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Reduced pointer hit-testing work during middle-button Visual Script canvas pans. Added a stock Godot stress test for dense graphs. [Visual scripting checks](project/tests/visual_scripting/QA.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed dark corner artifacts on shared Crimson editor popups. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed FileSystem drops dismissing the Add Reference choices on the first click. The graph shows those choices in its own editor surface and uses the same input routing as the canvas. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed the Visual Script picker so typing and spaces stay in the search field while canvas shortcuts are inactive. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`
- Fixed the Scene dock's attached Visual Script button to show the Crimson icon while retaining Godot's script-opening action. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Report the required node type when Godot cannot attach an existing `.crimsonscript` to a node in the editor. Verified compatible drops through the native Scene dock. [Visual scripting](docs/VISUAL_SCRIPTING.md#print-a-message-when-a-scene-starts) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed loss of pending collection and enum text during structural edits and graph Undo/Redo. Added a 28-case interaction matrix, including equal Array entries, Dictionary key type changes, and optional function and macro defaults. [Adversarial checks](project/tests/visual_scripting/QA.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed Visual Script collection editing so applying one field or dragging a row preserves neighboring drafts. Added stock editor checks for rejected values, repeated reorders, undo/redo, and reopening saved collections. [Collection defaults and enums](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed Visual Script previews so Auto Insert and Shake Off show the replacement route during a drag and restore the original route when the drag is interrupted. Added tests for drag cancellation, debugger restart, and node lifecycle callbacks. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed Visual Script default editing for missing asset references, overflowing enum numbers, and successive color changes. [Adversarial checks](project/tests/visual_scripting/QA.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Preserved in-progress Array, Dictionary, and enum field text while applying another field. Reordering keeps that text with its row and accepts the insertion gap between rows. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made Shake Off show the bypass as soon as the gesture is detected. Auto Insert shows only the proposed replacement wires while the card is over its target, and restores the original when the card moves away or focus changes. The move and wire edit remain one undo step. [Visual scripting](docs/VISUAL_SCRIPTING.md#work-with-several-wires) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed Array, Dictionary, and enum member drag reordering in the floating Visual Script editor. Details and My Script show an inset insertion line and share the pointer badge with Input Map. [Visual scripting](docs/VISUAL_SCRIPTING.md#edit-collection-defaults-and-enums) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made Graph Tools Auto Insert use the topmost available compatible pins. Shake Off recognizes short back-and-forth motion and can bypass a node with one linked execution route even when it has other unused pins. [Visual scripting](docs/VISUAL_SCRIPTING.md#work-with-several-wires) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added compatible variable drop feedback using the graph's existing green connection check. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Kept Space repeats out of Godot's canvas pan handler so left-drag remains available for box selection after switching windows. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Composited rounded popups and tooltips inside Crimson editor windows so their corners reveal the underlying content on Windows Forward+. Kept file dialogs on the editor's normal native-window path. [Editor components](src/editor/ui/README.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed broken unit labels in the Mover Inspector. Both Windows native builds check first-party C++ text to prevent the same encoding error in other strings. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Restored visible remote player models and made the network spike check wait for the application probe separately from relay forwarding. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Fixed the Custom Script badge stretching across the card. The badge is drawn at the upper-right corner at a fixed size relative to the graph zoom. [Custom Script](docs/VISUAL_SCRIPTING.md#write-a-custom-script-node) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Restored Visual Script wire rendering after saving and reopening the graph editor. The canvas rebuilds its connections from saved pin IDs once the ports are available, avoiding Godot's out-of-bounds port errors. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Cancelled unfinished Visual Script wire drags when the graph loses focus or is rebuilt. Compatible inputs show a check while dragging. Shift adds to the graph selection and Ctrl removes. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Gave each Visual Script signal subscription its own connection identity. Two event nodes can receive the same signal, and a handler captured for delivery cannot execute after its graph is detached. [Visual scripting](docs/VISUAL_SCRIPTING.md#react-to-an-event) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Defined Visual Script integer division and remainder for the minimum 64-bit integer divided by -1. The quotient wraps to the minimum value and the remainder is zero, avoiding a native arithmetic trap. [Visual scripting checks](docs/VISUAL_SCRIPTING.md#run-the-visual-scripting-checks) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Restored the pre-loop pin value when the same producer runs both before and inside a Visual Script loop. Variable writes and state changes such as Flip Flop still carry forward. [Visual scripting flow control](docs/VISUAL_SCRIPTING.md#choose-which-nodes-run) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed broken punctuation in graph debugger tooltips and units in the Mover Inspector. Both Windows native builds check first-party C++ text to prevent the same encoding error in other strings. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Kept private parameter producers inside collapsed graphs and preserved the size of comment frames when moving them. Filled connected execution sockets for immediate visual feedback. [Visual scripting](docs/VISUAL_SCRIPTING.md) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed graph positions reverting when renaming an event after moving it, and deferred canvas rebuilds until Godot releases the node drag. Fixed double duplication from Ctrl+D. Enabled direct wire insertion by default and made it detect wire overlap with the dragged card. [Graph tools](docs/VISUAL_SCRIPTING.md#work-with-several-wires) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Kept Visual Script editor labels in one language when Godot uses a translated editor locale. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made **Q** straighten the connected socket heights and rendered each reroute as a single typed dot. Verified that a data reroute can feed multiple inputs. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Made the center of a Visual Script reroute a drag target separate from the wire sockets. [Graph editor](docs/VISUAL_SCRIPTING.md#find-your-way-around-the-graph-editor) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Fixed visual script window startup and limited graph resource pickers to saved project assets, including imported audio files. [Visual scripting guide](docs/VISUAL_SCRIPTING.md#play-a-sound-when-a-scene-starts) `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Added confirmation before removing an action, binding, or modifier from an Input Map. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Corrected punctuation in the Input Map editor and component catalog by decoding UTF-8 labels explicitly. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Restored the Input Map search icon and initial pane proportions. Kept rich Select previews within their popup rows and showed the selected icon and description in the closed field. `b9c0727fb254f15e3b09b63e2249543e7eb12735`

- Serialized guest startup in the Windows LAN launcher and capped frame rate and worker threads in its staged multi-window project. The launcher stops opening windows after a process crashes and preserves per-process logs. [Demo guide](project/demo/README.md#start-from-the-command-line) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Kept the multiplayer test harness waiting when Windows briefly locks the movement conditioner's readiness marker during publication. [Test harness](scripts/Test-Multiplayer.ps1) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Added Mover Inspector value-control help generated from the same property documentation as Godot's built-in Help. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Removed GDScript shadowed-name warnings and added an agent rule requiring distinct names across scopes, inherited members, and Godot globals. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Audited public Godot properties and enum values across the addon, demo, and server. Filled missing Inspector and class Help descriptions and clarified the choices offered by editable enum fields. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Fixed movement RPC registration so the reliable authority roster keeps its own configuration when the code configures replaceable snapshots and input uploads. Added a 32-guest ENet regression for admission and motion convergence. [Movement networking](docs/MOVEMENT_NETWORKING.md#prediction-and-reconciliation) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Routed the stock demo's ENet guests through the authority to avoid Godot peer-relay notices to peers retiring in the same network poll. The concurrent harness verifies that every guest still sees the full player roster. [Network debugger](project/demo/README.md#network-debugger) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Local Windows launchers select an available UDP port when the implicit default is unavailable, and explain an unavailable explicit port. [Demo launchers](project/demo/README.md#start-from-the-command-line) `f83da634214894e38ca02b52fad5f450f035bf6c`

- Preserved actionable failure details across game startup, world travel, checkpoints, inventory operations, multiplayer sessions, and Steam services. Framework diagnostics use Godot's built-in output with a `[Crimson]` prefix. `f83da634214894e38ca02b52fad5f450f035bf6c`

- Isolated Steam publisher fixtures from the demo's editor import so their trusted native types resolve in the publisher test project. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Fixed Steam audit findings in offline startup and fatal IPC recovery, Steam Join Game launch handling and presence controls, listen relay preference, dedicated advertisement metadata, moderation durations, and notification opt-in deduplication. The 28 Spacewar cases passed on both Windows targets after a targeted Workshop rerun. The 30-case SDK-off suite, isolated runtime recovery, signed dedicated reconnect, and trusted publisher mocks also passed. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Ended the game process tick after online startup or a baseline timeout can resume a caller. Added a fixture that frees a game from its completed host operation and verifies listing cleanup. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Fixed Steam invite dialog startup for direct graphical launches by initializing the opt-in Steam runtime before Godot creates the display and waiting for SDK overlay readiness before submitting the dialog request. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Fixed the root launchers to work from Explorer with the Windows PowerShell included with Windows. Verified dedicated and four-instance LAN startup with a Windows-only PATH. `e83f32f806417f36379fb381e6f9cb5b6461e836`
