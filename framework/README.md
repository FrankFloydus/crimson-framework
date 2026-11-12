<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson Framework

Crimson Framework is a Godot 4.7 C++ GDExtension for offline and multiplayer games. Configure [`GameRuntime`](docs/PUBLIC_API.md#start-a-game) with a starting world, player scene, and optional multiplayer rules to play solo or host or join a drop-in game. The same entry point supports [coordinated matches](docs/PUBLIC_API.md#start-a-coordinated-game) with travel, save, and restore. The underlying modules are also available directly. Open `project/project.godot` to try the configured demo.

For existing projects, follow the [Godot class-name migration guide](docs/API_NAME_MIGRATION.md) before installing the renamed addon.

## Modules

| Module | Source | Responsibility |
| --- | --- | --- |
| Inventory | [Inventory module](src/modules/inventory/README.md) | Grid placement and snapshots, stack operations, containers, sorting, equipment, staging, wallet balances, and trade. |
| Save | [Save module](src/modules/save/README.md) | SaveCoordinator, detached capture and restore, schema migration, generation storage, provider ABI, and save metadata and previews. |
| Game session | [Game session module](src/modules/session/README.md) | Offline sessions, ENet host and join, peer lifecycle, connection admission, and server authority. |
| Scene loading | [Scene load module](src/modules/scene_load/README.md) | Threaded scene preparation and server-coordinated world transitions over the current Godot multiplayer API. |
| Match runtime | [Runtime API](docs/ARCHITECTURE.md) | Public transactions, player bindings, possession, and authority-side command dispatch. |
| Replication | [Replication API](src/modules/replication/README.md) | Recipient-specific world baselines and committed state deltas. |
| Immediate play | [Public API](docs/PUBLIC_API.md#start-a-game) | Inspector and fluent game setup, listen-session rules, and component-based player spawning. |
| Game Flow integration | [Public API](docs/PUBLIC_API.md) | `GameRuntime` composes the modules and owns session, scene, and checkpoint coordination. |
| Steamworks | [Steamworks guide](docs/STEAMWORKS.md) · [Run the Steam tests](docs/STEAM_TESTING.md) | Standalone typed Steam services, listen and dedicated multiplayer, and a separate trusted publisher build. |

A game owns the rules for how inventory state becomes a checkpoint participant. Keep gameplay IDs stable across saves. Use the host or server as the authority for shared state if you integrate the framework into a multiplayer game.

[Crimson Framework architecture](docs/ARCHITECTURE.md) maps module dependencies, the transaction contract, and the current multiplayer lifecycle.

## Build

Use Godot 4.7 stable for Windows x86-64 single precision, with MSVC, the Windows SDK, Python and SCons. The godot-cpp submodule is pinned to `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5` and the build selects API 4.7. From the workspace root, initialize the submodule and build in `framework/`.

```powershell
git submodule update --init --recursive
Set-Location framework
scons platform=windows arch=x86_64 target=template_debug
scons platform=windows arch=x86_64 target=template_release
```

Copy [the crimson-framework addon](project/addons/crimson-framework) into the destination game's `addons/` directory after building. Its descriptor uses relative paths to the two DLLs. Use one extension that registers the `SpatialInventory` and `SaveCoordinator` class names in a project. Remove the original standalone descriptors from the destination project when migrating to Crimson.

For CMake with Visual Studio, use `framework/` as the source directory and configure an x64 build directory. Choose `template_debug` or `template_release` with `GODOTCPP_TARGET`. Both build systems place the DLL under `framework/project/addons/crimson-framework/bin` relative to the workspace root.

Use `steam_sdk=off` with SCons or `-DCRIMSON_STEAMWORKS_SDK=OFF` with CMake to compile without Steamworks. [Steamworks SDK setup](vendor/README.md)

Build a separate trusted host with `steam_publisher=yes` or `-DCRIMSON_STEAM_PUBLISHER=ON` for publisher operations. SCons writes the trusted DLL under `bin/publisher/windows`. CMake writes it in its own build directory. Use the publisher test build only with the controlled fixture transport. The default addon excludes those trusted bindings. [Steam publisher setup](docs/STEAM_PUBLISHER.md)

## Releases

Each versioned work commit includes an addon ZIP under [releases/](releases/) and an annotated Git tag. Find the version, commit SHA, change description, and GitHub download in [DEVLOG.md](DEVLOG.md). See the [release procedure](releases/README.md) for packaging and verification.

## Multiplayer integration

Place `GameRuntime` at the same persistent path relative to the multiplayer root on the host and clients. For a fixed shared world, [configure listen rules and spawn components](docs/PUBLIC_API.md#start-a-game) and use `host_game()` and `join_game(address)`. In a [coordinated match](docs/PUBLIC_API.md#start-a-coordinated-game), the server hosts a lobby, binds connected peers to verified game identities, and starts a registered world. The coordinated game becomes ready after scene activation and baseline confirmation. Projects that supply their own orchestration can compose the standalone modules directly.

Run the headless test harness from the workspace root with PowerShell 7 after building both DLLs. It checks the native modules and runs ENet host/client checks in separate Godot processes. It also runs the demo session fixture with a listen host and two clients. Framework fixtures live under `framework/project/tests` and the demo fixture lives under `framework/project/demo/tests`.

```powershell
.\framework\scripts\Test-Multiplayer.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

Configure Steam listen or dedicated hosting with the [Steam provider API](docs/PUBLIC_API.md#use-steam). The [Steamworks SDK guide](vendor/README.md) describes Windows exports, runtime setup, and authenticated multiplayer checks.

## Demo project

Open [framework/project/project.godot](project/project.godot) and press F5 to boot the original retro co-op demo. Choose Single Player or host or join Multiplayer to spawn in Transfer Hall. The [demo guide](project/demo/README.md) covers controls, direct-address play, and the menu and hub architecture. The [demo agent instructions](project/demo/AGENTS.md) track implemented features and the development workflow.

## Contributing

See [agent instructions](AGENTS.md) for repository rules, [Crimson Codex agents](docs/AGENT_ROLES.md) for specialist ownership, and [the work log](LOG.md) for recent changes and the next archive date.

## Origin

The repository starts from the official [godot-cpp-template](https://github.com/godotengine/godot-cpp-template) at `d36f9d03e9e02dad22829f1faf4e03a347616df5`. Inventory and persistence derive from the existing Spatial Inventory and Godot Save System projects. Redistribution notices are included in [the addon](project/addons/crimson-framework/licenses).
