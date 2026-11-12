<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson dedicated demo server

- Keep the dedicated boot scene here. Stage it into the temporary Godot project as `res://server/` for launch and verification.
- The native [DedicatedServerBoot](../framework/src/integration/game/dedicated_boot.hpp) node owns command-line startup and readiness.
- Use the demo's shared game definition and the public `GameRuntime` dedicated operation. Keep the server and client multiplayer node paths consistent.
- Emit the ready marker only after the dedicated operation succeeds. Report startup errors to the launcher and exit nonzero.
- Follow the [framework agent assignments](../framework/AGENTS.md#agent-delegation) for changes to multiplayer behavior.
