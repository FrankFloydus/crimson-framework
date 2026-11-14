<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Work log

- **Week starting:** 2026-11-12
- **Next archive date:** 2026-11-19 (Europe/Rome)

## Added

- Added an authored native dedicated server entry. The headless Godot server boots from its scene without a GDScript bootstrap and shares the demo player and world. [Dedicated server](../server/AGENTS.md) `Pending push`

- Added reusable Input Action and Input Map resources, an FPS preset, separate Mouse X, Mouse Y, and Mouse XY binding sources, and ordered Invert, Scale, Normalize, and Response Curve modifiers. [Input guide](src/modules/input/README.md) `Pending push`

- Added four player controller styles on the Mover component and profile, with independent camera aim and body facing in fixed-world multiplayer. Added a target selection call for Lock-On and wrapped Inspector tooltips for the style options and Mover values. [Player controls](docs/PLAYER_CONTROLS.md) `Pending push`

- Added a local third-person camera preview to show the demo character's facing during solo and multiplayer movement tests. The Lock-On preview uses the north doorway as a test target. [Demo guide](project/demo/README.md) `Pending push`

- Exposed grounded acceleration, deceleration, friction, gravity, air control, and jump speed on the player Mover component. Live physics and multiplayer prediction share the tuning calculation. [Mover guide](src/modules/mover/README.md#configure-movement-on-a-player-scene) `Pending push`

- Added a default grounded jump on Space in the demo and Input Layout preset. The server validates takeoff and owning clients predict and replay the same jump command. [Player controls](docs/PLAYER_CONTROLS.md) `Pending push`

- Added a bounded typed binary codec and game-scoped custom codec registry. Fixed-world movement uses the native codec for input and pose records. [Network](src/modules/network/README.md#typed-binary-messages) `Pending push`

- Added directional bandwidth throttling, named simulator profiles, temporary latency spikes, and timed network scenarios to the demo debugger. The same simulator runs headless JSON plans and reports machine-readable diagnostics. Added focused and concurrent network regression suites. [Network debugger](project/demo/README.md#network-debugger) `Pending push`

- Added fixed-tick prediction and authoritative reconciliation for grounded movement against static collision. The demo opts in through its shared player profile. [Movement networking](docs/MOVEMENT_NETWORKING.md) `Pending push`

- Added a demo network debugger with typed game diagnostics, loopback ENet delay and loss, and disconnect and reconnect controls. Playable windows display their assigned player slot and role. [Demo network debugger](project/demo/README.md#network-debugger) `Pending push`

- Added native Mover, scoped input and rebinding, and a local Player Control profile. Single Player uses default movement and look and can rebind Move Forward from GameUI. [Player controls](docs/PLAYER_CONTROLS.md) `Pending push`
- Added authority-driven movement and look for the immediate-world profile, including guest pose updates and late join. [Movement networking](docs/MOVEMENT_NETWORKING.md) `Pending push`

- Implemented code-first Game boot, scoped typed service overrides, direct game actions, standalone Inventory grids, coordinated pickups, and the Steamworks facade. Migrated the demo and dedicated entry point to code and hid legacy setup classes from Add Node. [Quickstart](docs/CODE_FIRST.md) `Pending push`

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

- Isolated framework, gameplay, and Steam behavior tests from editable demo assets. Tests supply their own scenes, input mappings, and settings. The module suite boots without the demo, and live demo startup is a separate check. [Test guidance](AGENTS.md#verification) `Pending push`

- Grouped player speed and direction under Ground Movement and prediction under Network. Forward Direction explains the view, character, and world choices. `Pending push`

- Made the player Mover component's inline fields its sole source of grounded tuning and removed its Rule Override. `MotionRule` remains available for standalone Mover and explicit player profiles. `Pending push`

- Checked Unreal Engine 5.8.4 Mover's default character setup, jump input and impulse, and optional modes against the installed source. `Pending push`

- Renamed the native modules to `network` and `scene_manager`, moved rebinding under Input and removed the empty logging directory. [Module map](README.md#modules) `Pending push`

- Moved demo movement and input tuning to Mover and Controls components on the player scene. LAN and dedicated sessions read the same authored movement settings. [Player controls](docs/PLAYER_CONTROLS.md) `Pending push`
- Replaced the demo's network overlay with a developer window with live monitoring, configurable packet simulation, timed outages, and a session event log. [Network debugger](project/demo/README.md#network-debugger) `Pending push`
- Removed blanket authoring requirements from the workspace and documentation agent guides. `Pending push`
- The Windows LAN launcher tiles its host and guest windows in the primary display's work area. [Demo launchers](project/demo/README.md#start-from-the-command-line) `Pending push`

- Reviewed the Input and Mover API drafts with the demo-menu chat and proposed scoped facades for pointer capture, action reads, modal suspension, rebinding, and adapter replacement. `Pending push`

- Reviewed the installed Unreal Engine 5.8.4 Mover source and reported how Crimson could extract a native kinematic movement component from a Transfer Hall controller, with a staged path to multiplayer prediction. `Pending push`

- Audited Laravel 13.x and Cashier Stripe and revised the [API simplification proposal](docs/API_SIMPLIFICATION_PROPOSAL.md) around code-first Game, Inventory, and Steamworks, with scoped services, complete actions, and a migration for authored framework nodes. `Pending push`

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

- Retired the unused online provider, its native SDK wiring, and its integration fixtures. The Steam and ENet launch paths remain available. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Retired the demo's Quake asset importers, playback backdrop, BSP shaders, and runtime asset dependency. `Superseded before publication`
- Retired the local FPS prototype and its dedicated tests before publishing the initial framework snapshot. `847cccea9473b7561001ea2c399eba1fd651a0ab`

## Fixed

- Restored visible remote player models and made the network spike check wait for the application probe separately from relay forwarding. `Pending push`

- Fixed broken unit labels in the Mover Inspector. Both Windows native builds check first-party C++ text to prevent the same encoding error in other strings. `Pending push`

- Serialized guest startup in the Windows LAN launcher and capped frame rate and worker threads in its staged multi-window project. The launcher stops opening windows after a process crashes and preserves per-process logs. [Demo guide](project/demo/README.md#start-from-the-command-line) `Pending push`

- Kept the multiplayer test harness waiting when Windows briefly locks the movement conditioner's readiness marker during publication. [Test harness](scripts/Test-Multiplayer.ps1) `Pending push`

- Added Mover Inspector value-control help generated from the same property documentation as Godot's built-in Help. `Pending push`

- Removed GDScript shadowed-name warnings and added an agent rule requiring distinct names across scopes, inherited members, and Godot globals. `Pending push`

- Audited public Godot properties and enum values across the addon, demo, and server. Filled missing Inspector and class Help descriptions and clarified the choices offered by editable enum fields. `Pending push`

- Fixed movement RPC registration so the reliable authority roster keeps its own configuration when the code configures replaceable snapshots and input uploads. Added a 32-guest ENet regression for admission and motion convergence. [Movement networking](docs/MOVEMENT_NETWORKING.md#prediction-and-reconciliation) `Pending push`

- Routed the stock demo's ENet guests through the authority to avoid Godot peer-relay notices to peers retiring in the same network poll. The concurrent harness verifies that every guest still sees the full player roster. [Network debugger](project/demo/README.md#network-debugger) `Pending push`

- Local Windows launchers select an available UDP port when the implicit default is unavailable, and explain an unavailable explicit port. [Demo launchers](project/demo/README.md#start-from-the-command-line) `Pending push`

- Preserved actionable failure details across game startup, world travel, checkpoints, inventory operations, multiplayer sessions, and Steam services. Framework diagnostics use Godot's built-in output with a `[Crimson]` prefix. `Pending push`

- Isolated Steam publisher fixtures from the demo's editor import so their trusted native types resolve in the publisher test project. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Fixed Steam audit findings in offline startup and fatal IPC recovery, Steam Join Game launch handling and presence controls, listen relay preference, dedicated advertisement metadata, moderation durations, and notification opt-in deduplication. The 28 Spacewar cases passed on both Windows targets after a targeted Workshop rerun. The 30-case SDK-off suite, isolated runtime recovery, signed dedicated reconnect, and trusted publisher mocks also passed. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Ended the game process tick after online startup or a baseline timeout can resume a caller. Added a fixture that frees a game from its completed host operation and verifies listing cleanup. `e5c51b338dafe38b5693dca206909e0aa2657699`

- Fixed Steam invite dialog startup for direct graphical launches by initializing the opt-in Steam runtime before Godot creates the display and waiting for SDK overlay readiness before submitting the dialog request. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Fixed the root launchers to work from Explorer with the Windows PowerShell included with Windows. Verified dedicated and four-instance LAN startup with a Windows-only PATH. `e83f32f806417f36379fb381e6f9cb5b6461e836`
