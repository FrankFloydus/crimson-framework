<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson dedicated demo server

- Keep the dedicated boot scene here. Stage it into the temporary Godot project as `res://server/` for launch and verification.
- The `DedicatedServerEntry` scene root uses the same level, player, and world ID as the demo client. It calls native `GameConfig::boot_server` for the explicit server role.
- The demo uses authority-routed gameplay. Keep **Relay Between Guests** disabled on the server scene and preserve the same policy in the demo client.
- The native [dedicated launcher](../framework/src/integration/game/dedicated_boot.cpp) owns command-line parsing, the headless startup operation, and readiness. Keep the server and client multiplayer node paths consistent.
- Emit the ready marker only after the dedicated operation succeeds. Report startup errors to the launcher and exit nonzero.
- Follow the [framework agent assignments](../framework/AGENTS.md#agent-delegation) for changes to multiplayer behavior.
