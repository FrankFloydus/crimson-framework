<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Work log

- **Week starting:** 2026-11-12
- **Next archive date:** 2026-11-19 (Europe/Rome)

## Added

- Added game-scoped Session and Scene Loader injection, global Crimson game and scene facades, and native dedicated startup. Added root Windows launchers for a headless server and for up to four LAN instances. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added typed gameplay transport factories with native ENet defaults and verified a scripted demo adapter through the same host, late-join, rejection, and teardown paths. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added asynchronous online directory hooks, typed join endpoints, and verified identity before session admission. The demo's custom online provider passed separate-process host, authorized guest, and denied guest checks on both DLL variants. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added optional native SDK build configuration and an EOS runtime probe. Verified the official Epic C SDK initialization and missing-runtime behavior on both DLL variants. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added native Steamworks and Epic Online Services listen providers in the Crimson extension, with vendor lobbies, P2P transport, and provider identity checks. Verified both sides of a two-account Spacewar match and all four Windows export dependency selections. The private EOS deployment still requires live verification. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Added an isolated EOS live runner that starts both demo peers, verifies the selected network path, and keeps test credentials in the child processes. Added two-account Spacewar fixture roles and verified a portable test bundle from a fresh extraction. `e83f32f806417f36379fb381e6f9cb5b6461e836`

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

- Retired the demo's Quake asset importers, playback backdrop, BSP shaders, and runtime asset dependency. `Superseded before publication`
- Retired the local FPS prototype and its dedicated tests before publishing the initial framework snapshot. `847cccea9473b7561001ea2c399eba1fd651a0ab`

## Fixed

- Fixed Steam invite dialog startup for direct graphical launches by initializing the opt-in Steam runtime before Godot creates the display and waiting for SDK overlay readiness before submitting the dialog request. `e83f32f806417f36379fb381e6f9cb5b6461e836`

- Fixed the root launchers to work from Explorer with the Windows PowerShell included with Windows. Verified dedicated and four-instance LAN startup with a Windows-only PATH. `e83f32f806417f36379fb381e6f9cb5b6461e836`
