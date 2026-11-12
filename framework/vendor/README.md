<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Local Steamworks SDK

Steamworks is optional in the Crimson GDExtension. Use the [Steam provider API](../docs/PUBLIC_API.md#use-steam) to attach it to a game definition.

## SDK setup

The addon editor icon `images/bi_steam.png` is a 16×16 PNG rendered from the Bootstrap Icons Steam artwork. The addon includes the [Bootstrap Icons license](../project/addons/crimson-framework/licenses/bootstrap-icons-MIT.txt). [Bootstrap Icons Steam artwork](https://icons.getbootstrap.com/icons/steam/)

Extract Valve's complete Steamworks SDK to `vendor/steamworks/sdk`. The validated archive is Steamworks 1.65, SHA-256 `8c42792e09100988e31e3dc069de2eb1bc60702a0445bb37298ba0c54067c202`. The build looks for `public/steam/steam_api_flat.h` and stages `redistributable_bin/win64/steam_api64.dll` beside the Crimson DLL when it is present. Set `CRIMSON_STEAMWORKS_SDK` to override the directory, `steam_sdk=off` to disable Steamworks with SCons, or `-DCRIMSON_STEAMWORKS_SDK=OFF` with CMake. [Valve's SDK download](https://partner.steamgames.com/doc/sdk)

For compile-only development, the build also recognizes Valve's published 1.65 headers under `vendor/steamworks/proton-1.65/public/steam`. This header snapshot supplies no Steam runtime. Use the full SDK and its matching runtime for Spacewar validation. [Valve's pinned source](https://github.com/ValveSoftware/Proton/tree/1f089f5610a882c8bfcb4b58a3c86ee88bb4b9f1/lsteamclient/steamworks_sdk_165)

For hosted dedicated SDR, place Valve's separate Game Coordinator SDK at `vendor/steamworks/sdr` so `include/steam/steamdatagram_tickets.h` is present. Override with `steam_sdr_sdk=<path>` in SCons or `CRIMSON_STEAM_SDR_SDK` in CMake or the environment. Use `off` to omit hosted relay support. The addon uses the headers and the standard Steam runtime. The game's trusted coordinator owns ticket signing and Valve deployment credentials. [Valve Game Coordinator SDK and hosted relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay#gamecoordinator)

## Windows exports

Build with Steamworks, then add `crimson_steam` to **Custom Features** in the Godot Windows export preset to include `steam_api64.dll` with the Crimson extension. For direct ENet play, omit the feature. Keep the development `steam_appid.txt` outside shipping exports. [Godot GDExtension dependencies](https://docs.godotengine.org/en/4.7/engine_details/engine_api/gdextension/gdextension_file.html#dependencies)

Run [the Windows export harness](../scripts/Test-WindowsExports.ps1) with Godot 4.7 and its Windows x86-64 debug export template. It checks the exact DLL selection and boots both direct and Steam-tagged exports. The [multiplayer harness](../scripts/Test-Multiplayer.ps1) covers the scripted custom provider across separate processes.

```powershell
.\framework\scripts\Test-WindowsExports.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe' -WindowsDebugTemplate 'C:\Path\To\windows_debug_x86_64.exe'
```

## Live provider verification

Build with Valve's SDK. Use a copy of the demo and a Godot 4.7 console executable to run the opt-in fixtures. The demo menu uses direct ENet play.

### Spacewar 480

Use the [standalone Steam client harness](../docs/STEAMWORKS.md#start-the-client) to verify app metadata, local rich presence, and client scope lifecycle without starting a game.

Start the Steam client and sign in. For a direct development launch, use a test copy of the Godot executable and its matching console executable. Put a local `steam_appid.txt` containing `480` beside them, use that directory as the working directory, and run Godot under the same Windows user and elevation as Steam. Keep the demo path absolute. [Valve initialization guide](https://partner.steamgames.com/doc/api/steam_api?language=english) · [Valve's Spacewar example](https://partner.steamgames.com/doc/sdk/api/example)

```powershell
& $godot --headless --path $project --script res://demo/tests/steam_smoke.gd -- runtime
& $godot --headless --path $project --script res://demo/tests/steam_smoke.gd -- listen
& $godot --headless --path $project --script res://demo/tests/steam_smoke.gd -- coexist
```

The runtime check verifies SDK startup and a signed-in Steam identity. The listen check opens a native listener, spawns the host, publishes a private lobby, and tears the session down. The coexistence check verifies that a standalone client and the game provider share Steam while releasing their own scopes independently. For a live guest, use the same build on a second machine signed into a distinct Steam account. Use the same lobby namespace and virtual port on both sides and send a Steam invite from the active host. The private lobby uses the invitation to grant membership. [Steam account use](https://help.steampowered.com/en/faqs/view/71EA-CDCE-FB5C-82B3) · [Steam matchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

Run the guest command first on its machine. Run the host with a window to open the Steam invite overlay, then select the guest. Accept the invitation through Steam on the guest machine.

```powershell
# On the guest machine.
& $godot --headless --path $project --script res://demo/tests/steam_smoke.gd -- guest-invite crimson-qa
# On the host machine.
& $godot --path $project --script res://demo/tests/steam_smoke.gd -- host crimson-qa --invite --crimson-steam-overlay=480
```

If Steam has already granted lobby access, the guest can instead use `guest <lobby-id> crimson-qa` with the ID from the host's `CRIMSON_STEAM_LOBBY_ID` line. Look for `CRIMSON_STEAM_PASSED host` and the corresponding guest pass marker. This checks verified remote identity, late joining the running world, bidirectional reliable RPCs, and guest cleanup.

The host's `--crimson-steam-overlay=480` option prepares the Steam API before Godot creates the graphics device. The fixture waits up to ten seconds for Steam to report overlay readiness, then prints `CRIMSON_STEAM_OVERLAY_REQUESTED` when it submits the dialog request. Verify the dialog is visible in the game window. [Steam overlay requirements](https://partner.steamgames.com/doc/features/overlay#requirements)

For a development launch without an available overlay, explicitly select the intended guest. Set `CRIMSON_STEAM_INVITE_STEAM_ID` for the host process from a local prompt and use `host crimson-qa --invite-from-env`. The fixture consumes the value and submits one invitation after private lobby publication. The guest uses the same `guest-invite crimson-qa` command. [Steam overlay setup](https://partner.steamgames.com/doc/features/overlay#requirements) · [Steam private lobby invitations](https://partner.steamgames.com/doc/api/ISteamMatchmaking#InviteUserToLobby)
