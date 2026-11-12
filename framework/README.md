<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson Framework

Crimson Framework is a Godot 4.7 C++ GDExtension for offline and multiplayer games. Configure [`CrimsonGame`](docs/PUBLIC_API.md#start-a-game) with a starting world, player scene, and optional multiplayer rules to play solo or host or join a drop-in game. The same entry point supports [coordinated matches](docs/PUBLIC_API.md#start-a-coordinated-game) with travel, save, and restore. The underlying modules are also available directly. Open `project/project.godot` to try the configured demo.

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
| Game Flow integration | [Public API](docs/PUBLIC_API.md) | `CrimsonGame` composes the modules and owns session, scene, and checkpoint coordination. |

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

See [local platform SDKs](vendor/README.md) for building with Steamworks and Epic Online Services. Use `steam_sdk=off eos_sdk=off` with SCons or `-DCRIMSON_STEAMWORKS_SDK=OFF -DCRIMSON_EOS_SDK=OFF` with CMake to compile without either SDK.

## Releases

Each versioned work commit includes an addon ZIP under [releases/](releases/) and an annotated Git tag. Find the version, commit SHA, change description, and GitHub download in [DEVLOG.md](DEVLOG.md). See the [release procedure](releases/README.md) for packaging and verification.

## Multiplayer integration

Place `CrimsonGame` at the same persistent path relative to the multiplayer root on the host and clients. For a fixed shared world, [configure listen rules and spawn components](docs/PUBLIC_API.md#start-a-game) and use `host_game()` and `join_game(address)`. In a [coordinated match](docs/PUBLIC_API.md#start-a-coordinated-game), the server hosts a lobby, binds connected peers to verified game identities, and starts a registered world. The coordinated game becomes ready after scene activation and baseline confirmation. Projects that supply their own orchestration can compose the standalone modules directly.

Run the headless test harness from the workspace root with PowerShell 7 after building both DLLs. It checks the native modules and runs ENet host/client checks in separate Godot processes. It also runs the demo session fixture with a listen host and two clients. Framework fixtures live under `framework/project/tests` and the demo fixture lives under `framework/project/demo/tests`.

```powershell
.\framework\scripts\Test-Multiplayer.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

The [built-in provider guide](docs/PUBLIC_API.md#use-steam-or-epic-online-services) shows how to configure Steam or EOS with the same game API. The [platform SDK guide](vendor/README.md) describes the optional Windows export, runtime, and authenticated multiplayer checks.

## Demo project

Open [framework/project/project.godot](project/project.godot) and press F5 to boot the original retro co-op demo. Choose Single Player or host or join Multiplayer to spawn in Transfer Hall. The [demo guide](project/demo/README.md) covers controls, direct-address play, and the menu and hub architecture. The [demo agent instructions](project/demo/AGENTS.md) track implemented features and the development workflow.

## Contributing

See [agent instructions](AGENTS.md) for repository rules, [Crimson Codex agents](docs/AGENT_ROLES.md) for specialist ownership, and [the work log](LOG.md) for recent changes and the next archive date.

## Origin

The repository starts from the official [godot-cpp-template](https://github.com/godotengine/godot-cpp-template) at `d36f9d03e9e02dad22829f1faf4e03a347616df5`. Inventory and persistence derive from the existing Spatial Inventory and Godot Save System projects. Redistribution notices are included in [the addon](project/addons/crimson-framework/licenses).
