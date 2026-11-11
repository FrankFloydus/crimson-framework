<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson Framework

Crimson Framework is a Godot 4.7 C++ GDExtension for offline and multiplayer games. Start with the [public API guide](docs/PUBLIC_API.md) to configure a persistent `CrimsonGame` node, register game-owned state, and run one lifecycle for play, travel, save, and restore. The underlying modules are also available directly. Open `project/project.godot` to inspect the registered classes in Godot.

## Modules

| Module | Source | Responsibility |
| --- | --- | --- |
| Inventory | [Inventory module](src/modules/inventory/README.md) | Grid placement and snapshots, stack operations, containers, sorting, equipment, staging, wallet balances, and trade. |
| Save | [Save module](src/modules/save/README.md) | SaveCoordinator, detached capture and restore, schema migration, generation storage, provider ABI, and save metadata and previews. |
| Game session | [Game session module](src/modules/session/README.md) | Offline sessions, ENet host and join, peer lifecycle, connection admission, and server authority. |
| Scene loading | [Scene load module](src/modules/scene_load/README.md) | Threaded scene preparation and server-coordinated world transitions over the current Godot multiplayer API. |
| Match runtime | [Runtime API](docs/ARCHITECTURE.md) | Public transactions, player bindings, possession, and authority-side command dispatch. |
| Replication | [Replication API](src/modules/replication/README.md) | Recipient-specific world baselines and committed state deltas. |
| Game Flow integration | [Public API](docs/PUBLIC_API.md) | `CrimsonGame` composes the modules and owns session, scene, and checkpoint coordination. |

A game owns the rules for how inventory state becomes a checkpoint participant. Keep gameplay IDs stable across saves. Use the host or server as the authority for shared state if you integrate the framework into a multiplayer game.

[Crimson Framework architecture](docs/ARCHITECTURE.md) maps module dependencies, the transaction contract, and the current multiplayer lifecycle.

## Build

Use Godot 4.7 stable for Windows x86-64 single precision, with MSVC, the Windows SDK, Python and SCons. The godot-cpp submodule is pinned to `507ed9d840c01a3c5b2a39af8bb4000bfac30bf5` and the build selects API 4.7.

```powershell
git submodule update --init --recursive
scons platform=windows arch=x86_64 target=template_debug
scons platform=windows arch=x86_64 target=template_release
```

Copy [the crimson-framework addon](project/addons/crimson-framework) into the destination game's `addons/` directory after building. Its descriptor uses relative paths to the two DLLs. Use one extension that registers the `SpatialInventory` and `SaveCoordinator` class names in a project. Remove the original standalone descriptors from the destination project when migrating to Crimson.

For CMake with Visual Studio, configure an x64 build directory. Choose `template_debug` or `template_release` with `GODOTCPP_TARGET`. Both build systems place the DLL under `project/addons/crimson-framework/bin`.

## Multiplayer integration

Place `CrimsonGame` at the same persistent path relative to the multiplayer root on the host and clients, and give each peer the same world catalog. The server hosts a lobby, binds connected peers to verified game identities, then starts the first world. The game becomes ready once the scene and required peer baselines are confirmed. Projects that supply their own orchestration can compose `CrimsonGameSession`, `CrimsonSceneLoadManager`, and `CrimsonReplication` directly.

Run the headless test harness from the repository root with PowerShell 7 after building both DLLs. It checks the native modules and runs ENet host/client checks in separate Godot processes. Test scripts and fixtures live under `project/tests`.

```powershell
.\scripts\Test-Multiplayer.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```

## Origin

The repository starts from the official [godot-cpp-template](https://github.com/godotengine/godot-cpp-template) at `d36f9d03e9e02dad22829f1faf4e03a347616df5`. Inventory and persistence derive from the existing Spatial Inventory and Godot Save System projects. Redistribution notices are included in [the addon](project/addons/crimson-framework/licenses).
