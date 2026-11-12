<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson Framework workspace

The native GDExtension, Godot demo, build instructions, tests, and release procedure are in [framework/](framework/README.md).

Open [framework/project/project.godot](framework/project/project.godot) in Godot 4.7 and press F5 to run the demo.

## Local multiplayer

Build the framework DLL first, then run either launcher from the workspace root.

- [Run-Dedicated.bat](Run-Dedicated.bat) starts a headless match on loopback UDP `24831` and opens one client that joins when the world is ready. Pass a port as the first argument to override the default.
- [Run-LAN.bat](Run-LAN.bat) starts one listen host and up to three clients on UDP `24830`. Pass a total instance count from `1` through `4` as the first argument and an optional port as the second.

The launchers use the Windows PowerShell that comes with Windows and print the location of each run's logs. Dedicated mode shows one game window because the authority runs headlessly. The launcher reports readiness for both the server and the client. Set `CRIMSON_GODOT_EXE` to the full path of your Godot 4.7 executable if it is installed outside the launcher's search locations. Remote LAN players can use the normal **Join Game** form with the host's LAN address and port.
