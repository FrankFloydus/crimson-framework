<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Working on Crimson Framework

Read [README.md](README.md) and the module documentation before editing.

- `src/register_types.cpp` owns the GDExtension entry point and invokes the modules' independent registration functions.
- `src/modules/inventory/` owns placement and inventory domain operations. Keep player input, inventory UI, networking, and game rules outside the module.
- `src/modules/save/` owns checkpoint capture, validation, staged restore, providers, generation I/O, and optional save presentation helpers. It does not know any particular game or inventory model.
- `src/modules/session/` owns the ENet or offline peer it installs in the node's MultiplayerAPI and exposes session and roster events.
- `src/modules/scene_load/` owns scene IDs, threaded resource loading, staging, and transition coordination. It uses the node's MultiplayerAPI and has no dependency on the session module. Keep scene assets and network authority decisions at the host boundary.
- `src/modules/match_runtime/` owns transactions, player bindings, possession, and transport-neutral command routing. It also validates detached checkpoint candidates and publishes restored stores. See [architecture](docs/ARCHITECTURE.md) for the lifecycle and replication contracts.
- `src/modules/replication/` publishes recipient-specific baselines and revisioned state deltas over the current MultiplayerAPI. Keep game-specific visibility rules at the host boundary.
- `src/integration/` owns the native CrimsonGame facade, Game Flow orchestration, checkpoint bridge, and adapters between modules. See [public API](docs/PUBLIC_API.md). Both the Inventory and Match Runtime can be used independently of this integration layer.
- `project/addons/crimson-framework/` holds the descriptor, generated debug and release libraries, and redistribution notices. Use `project/project.godot` to import the extension.
- `project/tests/` contains test harnesses and fixture scenes. Run `scripts/Test-Multiplayer.ps1` with the Godot 4.7 console executable after building both DLLs.
- `build_profile.json` limits the generated godot-cpp API surface. Update it if a module adds a binding dependency. Keep SCons, CMake and the descriptor on Godot 4.7 and the same output filenames.

Keep system code native C++. Use Godot DTOs or native values at the host boundary. Save callbacks and world publication run on the main thread. Keep engine Objects out of persistence worker jobs. Never change the save envelope or provider ABI without an explicit migration and compatibility plan. In inventory, preserve atomic placement, revision checks, and snapshot validation.

Document every first-party framework function and every class property or member field. Describe its purpose, contract, or invariant next to its declaration. Document file-local functions at their definitions. Keep these comments current when behavior changes.

Put the exact notice `Copyright Francesco Di Blasi 2026 - All Rights Reserved` at the top of each first-party framework file. Use the file format's comment syntax or supported metadata. Preserve original license and copyright notices for upstream code and third-party dependencies. For generated files, apply the rule to the source inputs.

Verify both Windows x86-64 DLLs with Godot 4.7 stable. Test from a project that imports the addon alone. Keep test saves isolated from existing user data.

## Initial verification

Both SCons DLLs passed clean headless imports and the save coordinator, presentation, grid, actions, model commands, equipment, containers, staging, sorter, wallet, and trade suites on Godot 4.7 stable at `5b4e0cb0f`. The combined checkpoint test restored an open trade, stack escrow, and two inventories. A save created by the previous standalone DLL also restored. CMake debug built with MSVC and installed the same addon layout.

With the session and scene modules added, both SCons DLLs passed 81 session checks, 104 scene load checks, and a separate-process ENet test with two consecutive world transitions on Godot 4.7 stable. The session suite includes a refused join against a closed lobby. The scene suite includes pre-commit rollback, a post-commit sync failure, and recovery.

The Match Runtime suite adds 82 checks and a separate-process ENet server and client fixture. It covers atomic inventory and world state publication, possession integrity, stale bindings, and authority-side command dispatch. Run it with the same `Test-Multiplayer.ps1` command.
