<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Local platform SDKs

Steamworks and Epic Online Services are optional backends in one Crimson GDExtension. Use the [public provider API](../docs/PUBLIC_API.md#use-steam-or-epic-online-services) to attach either to a game definition.

## Epic Online Services

Extract the Epic Online Services Windows C SDK so that `vendor/eos/SDK/Include/eos_sdk.h` and `vendor/eos/SDK/Bin/EOSSDK-Win64-Shipping.dll` exist under the framework root. The validated archive is version 1.19.2.1 (CL58105819), SHA-256 `56a3bd805df426606946d74ba662f25223ffe24159e7884a625225ba80b582fb`. [Epic's SDK download page](https://onlineservices.epicgames.com/sdk?lang=en-US)

SCons and CMake discover the local SDK by default. Set `CRIMSON_EOS_SDK` in the environment to use a different extracted SDK directory. Set `eos_sdk=off` in SCons or `-DCRIMSON_EOS_SDK=OFF` in CMake for an SDK-free build. Builds with EOS stage its Windows x64 runtime beside the Crimson DLL in the Godot addon. The SDK tree itself stays local.

Pass `-TestEosRuntime` to [the multiplayer harness](../scripts/Test-Multiplayer.ps1) after building both variants. It checks the explicit missing-DLL result and initializes the real installed SDK in a fresh process. This check requires no EOS deployment configuration.

## Steamworks

Extract Valve's complete Steamworks SDK to `vendor/steamworks/sdk`. The validated archive is Steamworks 1.65, SHA-256 `8c42792e09100988e31e3dc069de2eb1bc60702a0445bb37298ba0c54067c202`. The build looks for `public/steam/steam_api_flat.h` and stages `redistributable_bin/win64/steam_api64.dll` beside the Crimson DLL when it is present. Set `CRIMSON_STEAMWORKS_SDK` to override the directory, `steam_sdk=off` to disable Steamworks with SCons, or `-DCRIMSON_STEAMWORKS_SDK=OFF` with CMake. [Valve's SDK download](https://partner.steamgames.com/doc/sdk)

For compile-only development, the build also recognizes Valve's published 1.65 headers under `vendor/steamworks/proton-1.65/public/steam`. This header snapshot supplies no Steam runtime. Use the full SDK and its matching runtime for Spacewar validation. [Valve's pinned source](https://github.com/ValveSoftware/Proton/tree/1f089f5610a882c8bfcb4b58a3c86ee88bb4b9f1/lsteamclient/steamworks_sdk_165)

## Windows exports

Add `crimson_steam` or `crimson_eos` to **Custom Features** in the Godot Windows export preset to copy the selected vendor DLL beside the exported executable and Crimson extension. Use both features when the export offers both providers. Build with the corresponding SDKs before exporting. Exports without either feature ship Crimson's direct play support. Keep the development `steam_appid.txt` outside shipping exports. [Godot GDExtension dependencies](https://docs.godotengine.org/en/4.7/engine_details/engine_api/gdextension/gdextension_file.html#dependencies)

Run [the Windows export harness](../scripts/Test-WindowsExports.ps1) with Godot 4.7 and its Windows x86-64 debug export template. It checks the exact DLL selection and boots direct, Steam, EOS, and combined exports. The EOS-tagged executables also initialize the SDK. The [multiplayer harness](../scripts/Test-Multiplayer.ps1) covers the scripted custom provider across separate processes.

```powershell
.\framework\scripts\Test-WindowsExports.ps1 -GodotExe 'C:\Path\To\Godot_v4.7-stable_win64_console.exe' -WindowsDebugTemplate 'C:\Path\To\windows_debug_x86_64.exe'
```

## Live provider verification

Build with the official SDKs. Use a copy of the demo and a Godot 4.7 console executable to run the opt-in fixtures. The demo menu uses direct ENet play.

### Spacewar 480

Start the Steam client and sign in. For a direct development launch, use a test copy of the Godot executable and its matching console executable. Put a local `steam_appid.txt` containing `480` beside them, use that directory as the working directory, and run Godot under the same Windows user and elevation as Steam. Keep the demo path absolute. [Valve initialization guide](https://partner.steamgames.com/doc/api/steam_api?language=english) · [Valve's Spacewar example](https://partner.steamgames.com/doc/sdk/api/example)

```powershell
& $godot --headless --path $project --script res://demo/tests/steam_smoke.gd -- runtime
& $godot --headless --path $project --script res://demo/tests/steam_smoke.gd -- listen
```

The runtime check verifies SDK startup and a signed-in Steam identity. The listen check opens a native listener, spawns the host, publishes a private lobby, and tears the session down. For a live guest, use the same build on a second machine signed into a distinct Steam account. Use the same lobby namespace and virtual port on both sides and send a Steam invite from the active host. The private lobby uses the invitation to grant membership. [Steam account use](https://help.steampowered.com/en/faqs/view/71EA-CDCE-FB5C-82B3) · [Steam matchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

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

### Private EOS deployment

Use an EOS test product, its sandbox and deployment, and a client linked to its Epic Account Services application. Enable Basic Profile for Epic Account Services. The client policy must permit signed-in players to use Lobbies and P2P. For an application awaiting brand review, use two distinct Epic accounts in the product's Developer Portal organization. Internal Dev Auth testing can use a product without an Epic Games Store page. [Epic Account Services setup](https://dev.epicgames.com/docs/epic-online-services/accounts-and-social/eos-epic-account-services/auth-interface/auth-guide/set-up-epic-account-services) · [Client policy guide](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide)

Extract the Developer Authentication Tool from the SDK's `Tools` directory. Start it locally, sign into both accounts through its Epic sign-in flow, and assign a different credential name to each account. Set the fixture variables in the PowerShell process that launches Godot. Keep the client secret and account sign-ins local. [Epic Dev Auth Tool](https://dev.epicgames.com/docs/epic-online-services/accounts-and-social/eos-epic-account-services/developer-authentication-tool)

| Environment variable | Value |
| --- | --- |
| `CRIMSON_EOS_PRODUCT_ID`, `CRIMSON_EOS_SANDBOX_ID`, `CRIMSON_EOS_DEPLOYMENT_ID` | The same test deployment on both peers. |
| `CRIMSON_EOS_CLIENT_ID`, `CRIMSON_EOS_CLIENT_SECRET` | The associated EOS client credential. Use a process-scoped variable for the secret. |
| `CRIMSON_EOS_HOST_CREDENTIAL`, `CRIMSON_EOS_GUEST_CREDENTIAL` | The two distinct names entered in the Dev Auth Tool. |
| `CRIMSON_EOS_DEV_AUTH_ADDRESS` | Optional Dev Auth host and port. Defaults to `127.0.0.1:6547`. |

Run [Test-EosLive.ps1](../scripts/Test-EosLive.ps1) from the workspace root in PowerShell 7. It reads settings from the current process or prompts for the missing values, starts an isolated host, and uses the published lobby ID to start the guest. Both instances can run on the same workstation with two Dev Auth identities. Use `-NoPrompt` if the calling shell supplies every required value.

```powershell
.\framework\scripts\Test-EosLive.ps1 -GodotExe $godot
.\framework\scripts\Test-EosLive.ps1 -GodotExe $godot -ForceRelay
```

To run the fixture manually, start the host, read its `CRIMSON_EOS_LOBBY_ID`, then start the guest with that value while the host is running. Configure both terminals with the same process-local deployment settings.

```powershell
& $godot --headless --path $project --script res://demo/tests/eos_live.gd -- --host
& $godot --headless --path $project --script res://demo/tests/eos_live.gd -- --join $lobbyId
```

The test requires both `CRIMSON_EOS_HOST_PASSED` and `CRIMSON_EOS_GUEST_PASSED`. It checks admission, the mounted world, player spawning, reliable traffic in both directions, and seat cleanup. To verify relay, repeat with `--force-relay` on both processes and check for `CRIMSON_EOS_PEER_PATH=relay` on each. [EOS relay control](https://dev.epicgames.com/docs/en-US/api-ref/enums/eos-e-relay-control)
