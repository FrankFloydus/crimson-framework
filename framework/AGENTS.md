<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Working on Crimson Framework

Crimson is a native C++ GDExtension for Godot 4.7. It provides reusable systems for offline and multiplayer games. Games can use modules independently or compose them through `GameRuntime` for session, travel, save, and restore. Game projects own their rules, state schemas, controllers, and presentation. Keep cross-module wiring in `src/integration/` and preserve standalone module entry points.

Use [Architecture](docs/ARCHITECTURE.md) for ownership, authority, and lifecycle contracts, [Public API](docs/PUBLIC_API.md) for Godot-facing behavior, and [README](README.md) for setup and packaging.

## Where to work

Paths in this guide are relative to `framework/`. Open the guide for each area you edit. For cross-module changes, check both sides of the integration boundary.

| Area | Scoped guide |
| --- | --- |
| Grid placement, item movement, and inventory models | [Inventory](src/modules/inventory/AGENTS.md) |
| Checkpoint capture, storage, migrations, and staged restore | [Save](src/modules/save/AGENTS.md) |
| Offline and ENet transport, admission, and peer lifecycle | [Session](src/modules/session/AGENTS.md) |
| Scene catalog, threaded preparation, and world mounting | [Scene Loader](src/modules/scene_load/AGENTS.md) |
| Transactions, player bindings, possession, and command routing | [Match Runtime](src/modules/match_runtime/AGENTS.md) |
| Recipient-specific baselines and state deltas | [Replication](src/modules/replication/AGENTS.md) |
| `GameRuntime`, Game Flow, and module adapters | [Integration](src/integration/AGENTS.md) |
| Fixed-world sessions, spawn placement, and local views | [World Session](src/integration/world_session/AGENTS.md) |
| Online directories and Steam provider composition | [Online](src/integration/online/AGENTS.md) |
| Standalone Steamworks services and SDK lifecycle | [Steamworks](src/modules/steam/AGENTS.md) |
| Original co-op FPS demo | [Demo project](project/demo/AGENTS.md) |

- `src/register_types.cpp` is the GDExtension entry point. Keep each module's class registration in its own registration code.
- `project/project.godot` hosts the addon and the headless fixtures in `project/tests/`. The descriptor and redistribution notices live in `project/addons/crimson-framework/`.
- `build_profile.json` limits the generated Godot bindings. Keep the SCons and CMake configurations and addon descriptor aligned on Godot 4.7, Windows x86-64, and the DLL filenames.

## Demo project

Build the [demo](project/demo/AGENTS.md) as an original retro FPS with co-op multiplayer and modern features such as inventory. Develop one player-facing feature at a time. Use the demo to exercise Crimson's public API and extract reusable behavior into the appropriate native C++ module when it fits other games. Keep the demo's rules, tuning, scenes, and presentation in the demo. Apply the [agent delegation rules](#agent-delegation) to demo work and any framework changes it produces.

Update the [demo feature inventory](project/demo/AGENTS.md#implemented-features) in the same task whenever the demo gains, changes, or removes behavior. Keep its entries tied to the current game and use [LOG.md](LOG.md) for the history.

## Repository rules

- Keep framework systems native C++. Exchange Godot-compatible DTOs at integration boundaries. Preserve documented Godot APIs and update the relevant public guide when behavior changes.
- Document every first-party framework function and every class property or member field at its declaration. Document file-local functions at their definitions. Explain the purpose, contract, or invariant and update comments with behavior changes.
- When adding or changing a native public Godot binding, update the owning class XML for its methods, properties, enum values, and signals. Describe arguments, return and failure behavior, defaults, units, and when signals fire. Include the XML in the editor documentation build so Godot's built-in Help shows it. [Godot GDExtension class documentation](https://docs.godotengine.org/en/4.7/tutorials/scripting/cpp/gdextension_docs_system.html)
- Every first-party property exposed in the Godot Inspector must have an explanatory tooltip. Use class XML for native properties, a documentation comment for GDScript exports, or equivalent help for a custom editor control.
- Put the exact notice `Copyright Francesco Di Blasi 2026 - All Rights Reserved` at the top of each first-party framework file using its supported comment syntax or metadata. Preserve upstream and dependency notices. For generated files, apply the rule to the source inputs.
- Keep agent instructions short and actionable. Put rules that apply across the repository here, put local invariants in the nearest scoped guide, and link to maintained contracts for detail.

## Agent delegation

Apply these roles to both the framework and the demo. Use the exact names in [Crimson Codex agents](docs/AGENT_ROLES.md).

- Invoke **Crimson Software Architect** whenever either project area gains, changes, or removes a feature, including when reusable demo behavior moves into the framework. Get its design decision before general implementation. It consults and gives the final architecture verdict on all framework and demo code changes.
- Assign all networking and multiplayer implementation and verification to **Crimson Network Engineer** in both areas, including demo co-op lobbies, replication, and synchronization.
- Assign all UI implementation and verification to **Crimson UI Engineer** in both areas, including demo HUDs, menus, inventory screens, and controller navigation.
- The Network and UI engineers can implement independently in their domains and consult the architect for a second judgment. The architect resolves shared architecture decisions and returns required changes to the code owner. Split mixed work by ownership. Other agents can investigate and review these changes and route edits to the relevant specialist.
- Every implementer adding or changing a customer-facing API must follow the [Laravel-inspired public API design guidelines](docs/PUBLIC_API.md#public-api-design-guidelines). This applies to the Network and UI engineers when they implement independently.

## Documentation and work log

- Documentation is part of every task. For additions, changes, and removals, update every affected document in the same task, including examples and scoped agent guides. Add a dedicated guide for a new module or substantial domain. Remove obsolete instructions and repair references when moving or removing a documented feature.
- Record every task briefly in [LOG.md](LOG.md) under **Added**, **Changed**, **Removed**, or **Fixed**. Record the full SHA of each pushed work commit the entry describes. Mark work awaiting a push as `Pending push`.
- Check the **Next archive date** in LOG.md at the start of each task and before a push, using the Europe/Rome calendar. On or after that date, archive the active log to [log_archive/](log_archive/) as `LOG-YYYY-MM-DD.md`, using the scheduled cutoff date. Start a fresh LOG.md with the same four headings. Set its week start to that cutoff and its next archive date seven calendar days later. Process overdue weekly cutoffs in order. Preserve existing archives and resolve any pending SHA in the file where its entry lives.

## Git conventions

- Name new branches `feature/<short-feature-name>`, `fix/<short-fix-title>`, or `experimental/<short-title>`. Use lowercase words separated by hyphens after the slash.
- Every commit requires a descriptive subject and a separate body explaining what changed and why. Start the subject with `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, or `chore:`.
- Before each push, fetch the destination and review the exact outgoing commits and their combined diff. For a new branch, review the unpublished commits against its intended base. Check that all affected docs are current and every outgoing task is represented in the active or archived log. Add any missing entries before pushing.
- After a successful push, verify the remote contains the intended work commits. Replace `Pending push` in LOG.md with their verified full SHAs and publish the log updates in a separate `docs:` commit with a body. Audit that follow-up push too.

## Addon releases

- Package the current addon before every work commit, including documentation and test commits. Add the ZIP under `releases/` in that same commit. Follow the [release procedure](releases/README.md).
- Start at `0.0.1` and increment the patch number for each work commit through `0.0.9999`. Create the matching annotated Git tag `v0.0.x` on the commit. Push the branch and tag, then publish the same ZIP as a GitHub Release asset for that tag.
- Record each work commit in [DEVLOG.md](DEVLOG.md) with its version, full commit SHA, description, and ZIP links. Record the SHA after committing and verify the public ZIP link after publishing. Update both logs together in the post-push `docs:` commit. A follow-up limited to recording SHAs and verified release links is covered by the work commits it records.

## Verification

Use the [build commands](README.md#build) for `template_debug` and `template_release`. For native behavior or binding changes, verify both Windows x86-64 DLLs in an isolated project with Godot 4.7 stable. Run the harness from the workspace root in PowerShell 7. It copies the addon and fixtures into separate test projects and isolates save data. Use `-Target template_debug` for a faster iteration.

```powershell
.\framework\scripts\Test-Multiplayer.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe'
```
