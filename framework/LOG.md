<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Work log

- **Week starting:** 2026-11-12
- **Next archive date:** 2026-11-19 (Europe/Rome)

## Added

- Added typed dedicated Steam stats and achievements for the exact admitted peer, Wallet buyer information, Cloud write-batch hints, Workshop subscriptions and legal agreement actions, and Steam notification session launch hints. Verified that an anonymous Spacewar dedicated server loaded the signed-in guest's Steam stats and retired the cache handle when the guest left. `Pending push`

- Verified a live Spacewar Winner unlock through the rendered achievement preview. Steam acknowledged the store and unlock, the operator saw the Steam notification, and a fresh process read the achievement as already unlocked. `Pending push`

- Added a self-service Steam testing guide, bulk PowerShell suites, a rendered one-button achievement unlock, an opt-in live encrypted ownership fixture, and a repeatable offline DRM smoke test. Passed 28 signed Spacewar cases on both Windows builds, 29 Debug cases each for missing-runtime and SDK-off, and six trusted publisher mocks. Confirmed that the rendered preview read Spacewar's locked Winner and became ready without unlocking it. `Pending push`

- Added standalone authenticated Steam Sockets, observed connection path diagnostics, and hosted Steam Datagram Relay through a game-owned trusted coordinator. Verified direct loopback packet delivery and both dedicated connection variants' missing-coordinator and unprovisioned deployment handling. `Pending push`

- Added native Steam dedicated hosting, ticket-bound admission, and a typed server browser. Verified an anonymous secure Spacewar server and signed guest in separate processes, including invalid-proof denial, identity pinning, two fresh joins into the same world, and clean teardown. `Pending push`

- Added typed client checkout and trusted publisher authentication, Wallet, Inventory, Notifications, and moderation. Verified the client against a mock backend in Spacewar and exercised trusted operations with no-socket publisher fixtures. `Pending push`

- Added Steam Networking Messages with typed peer permissions, independent delivery modes, bilateral binding freshness, and exact Game admission checks. Verified signed Spacewar capability and authorization boundaries, plus missing-runtime behavior. `Pending push`

- Added typed Steam Voice capture and decoding, Game-owned push-to-talk routing, and Steam Parties discovery, owner-bound reservations, and scoped beacon publication. Verified signed read-only Spacewar and missing-runtime fixtures. Added explicit two-account voice and beacon fixtures. `Pending push`

- Added typed Steam Cloud sharing, leaderboard submissions, Timeline phases, friend and lobby snapshots, Workshop, screenshots, Remote Play, Device, Input, browser, media, Inventory, and client ticket services. Verified signed Spacewar read and lifecycle paths, including private lobby self-chat and Web API and encrypted ticket issuance. `Pending push`

- Added the trusted publisher build and a SteamPipe DRM wrapper. Verified the wrapper's arguments, failure cleanup, and destination protection with a controlled SteamCMD stub. `Pending push`

- Added `GameDefinition.boot()`, typed Steam achievement and stats results, the configured HTTP facade, and standalone Overlay actions. Verified the boot path, signed Spacewar reads and HTTP, and rendered Achievements overlay callbacks. `Pending push`

- Registered the Steam editor icon for native Steam nodes and configuration types. Included the asset and upstream license in the addon layout. `Pending push`

- Drafted a standalone Crimson Collections design with typed Godot views, fluent operations, snapshot semantics, and performance verification for approval before Steam API work. `Pending push`

- Drafted the full Steam public API proposal for approval, with initialization, function contracts, FPS integrations, and verification plans for every feature. `Pending push`

- Added the standalone Steam client with typed Apps, Friends, and Utils services, shared runtime ownership, and isolated CLI fixtures. Documented the full Steamworks architecture and feature-by-feature live test requirements. `Pending push`

- Added Steam HTTP, Cloud, stats, achievements, leaderboards, Game Recording timelines, and SDK diagnostics. Added one-call client boot, direct HTTP, Cloud, and top-score actions, and branchable stat edits that submit together. Added CLI fixtures for the signed-in and unavailable paths. `Pending push`

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

- Audited the current Steam implementation against Valve's documentation and the approved API. Identified gaps in offline startup, Steam launch and join controls, dedicated server metadata, and publisher notification and moderation behavior. `Pending push`

- Required native Godot class XML for public methods, properties, enum values, and signals, and added Steam class documentation to the editor build. `Pending push`

- Migrated Godot-facing classes to brand-neutral API names, including `GameRuntime`, `GameServices`, `SteamClient`, and `SteamProvider`. Updated the current demo, server, Inspector documentation, and fixture references. Deferred Collections. `Pending push`

- Clarified Collections as a globally accessible framework module and documented shared adoption across game, inventory, save, scene, session, and tools APIs. `Pending push`

- Refined the Steam public API proposal with typed achievement snapshots, concrete result and collection bindings, and Godot completion checks. `Pending push`

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

- Retired the unused online provider, its native SDK wiring, and its integration fixtures. The Steam and ENet launch paths remain available. `Pending push`

- Retired the demo's Quake asset importers, playback backdrop, BSP shaders, and runtime asset dependency. `Superseded before publication`
- Retired the local FPS prototype and its dedicated tests before publishing the initial framework snapshot. `847cccea9473b7561001ea2c399eba1fd651a0ab`

## Fixed

- Isolated Steam publisher fixtures from the demo's editor import so their trusted native types resolve in the publisher test project. `Pending push`

- Fixed Steam audit findings in offline startup and fatal IPC recovery, Steam Join Game launch handling and presence controls, listen relay preference, dedicated advertisement metadata, moderation durations, and notification opt-in deduplication. The 28 Spacewar cases passed on both Windows targets after a targeted Workshop rerun. The 30-case SDK-off suite, isolated runtime recovery, signed dedicated reconnect, and trusted publisher mocks also passed. `Pending push`

- Ended the game process tick after online startup or a baseline timeout can resume a caller. Added a fixture that frees a game from its completed host operation and verifies listing cleanup. `Pending push`

- Fixed Steam invite dialog startup for direct graphical launches by initializing the opt-in Steam runtime before Godot creates the display and waiting for SDK overlay readiness before submitting the dialog request. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Fixed the root launchers to work from Explorer with the Windows PowerShell included with Windows. Verified dedicated and four-instance LAN startup with a Windows-only PATH. `e83f32f806417f36379fb381e6f9cb5b6461e836`
