<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Test Steam features

Run the commands below in PowerShell 7 from the repository root. Use the Windows Godot 4.7 console executable, including its companion executable in the same folder, and the built Steam-enabled addon. Sign into the Steam account you want to test. Set `$godot` to the path on your machine.

```powershell
$godot = 'C:\path\to\Godot_v4.7-stable_win64_console.exe'
$test = '.\framework\scripts\Test-Steam.ps1'
& $test -GodotExe $godot -Case all -Mode spacewar -Target template_debug
& $test -GodotExe $godot -Case all -Mode missing -Target template_debug
```

The runner stages isolated projects and prints the directory containing each Godot log. A pass means the named fixture observed its expected result. Omit `-Target` to exercise both Windows DLLs. The Spacewar suite starts temporary private lobbies, sends one lobby chat message to the signed-in account, temporarily sets and restores presence keys, runs local sockets and HTTP, and reads the other Steam capabilities. Python 3 on `PATH` is used for the HTTP test endpoints. Use `-TrustedHttpsUrl https://store.steampowered.com/robots.txt` to also check a trusted HTTPS response. The suite excludes explicit unlocks, published content, microphone capture, purchases, moderation, and remote invites.

| Use one `-Case` to run a focused check | Fixtures |
| --- | --- |
| Client, platform metadata, diagnostics, HTTP | `client`, `apps`, `utils`, `diagnostics`, `device`, `http` |
| Progression, Cloud, Recording | `stats`, `cloud`, `timeline` |
| Lobbies, presence, server discovery, transport, voice | `lobbies`, `presence`, `server_browser`, `sockets`, `messages`, `parties`, `voice` |
| Overlay, community, media, hardware | `overlay`, `workshop`, `screenshots`, `remote_play`, `input`, `keyboard`, `browser`, `music`, `video` |
| Inventory, checkout, tickets | `inventory`, `purchases`, `auth` |

Use `-Mode sdk-off -BinaryDirectory $sdkOffBin` against a build with Steam disabled to exercise typed unavailable results. `inventory_verifier` is available in `missing` and `sdk-off` modes. The app-owned and trusted checks below cover the additional requirements for live positive results.

## Verify Steam client startup and recovery

First run the signed Spacewar `client` and `stats` fixtures to cache app and progression data. Put Steam into Offline Mode yourself, then run the offline fixture. It expects a local Steam scope, no live Valve connection, and cached `ACH_WIN_ONE_GAME` data. Restore Steam to online operation before running signed fixtures again. [Valve offline progression](https://partner.steamgames.com/doc/features/achievements#offline_mode)

```powershell
& $test -GodotExe $godot -Case client -Mode offline -Target template_debug
```

Use the separate runtime fixture to exercise a native IPC failure without interfering with the running Steam client. From an x64 Visual Studio Developer PowerShell, build the fixture DLL into a new temporary directory. The harness copies that DLL only to isolated projects. Keep it separate from the shipped `steam_api64.dll`.

```powershell
$mockBin = Join-Path ([IO.Path]::GetTempPath()) ('crimson-steam-runtime-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $mockBin
Copy-Item .\framework\project\addons\crimson-framework\bin\crimson_framework.windows.template_*.x86_64.dll $mockBin
$steamPublic = (Resolve-Path .\framework\vendor\steamworks\sdk\public).Path
$fixtureSource = (Resolve-Path .\framework\scripts\fixtures\steam_client_lifecycle.cpp).Path
$fixtureExports = (Resolve-Path .\framework\scripts\fixtures\steam_client_lifecycle.def).Path
Push-Location $mockBin
try {
    & cl.exe /nologo /LD /std:c++17 "/I$steamPublic" "/I$steamPublic/steam" $fixtureSource /link "/DEF:$fixtureExports" /OUT:steam_api64.dll
    if ($LASTEXITCODE -ne 0) { throw 'Steam lifecycle fixture compilation failed.' }
} finally {
    Pop-Location
}
& $test -GodotExe $godot -Case client -Mode runtime-mock -BinaryDirectory $mockBin
```

This fixture checks cached reads while offline, cold and warm typed Game Notifications IDs, rejection of malformed IDs, and retirement of two shared client scopes after the borrowed IPC buffer is freed. It verifies that old service handles stay inactive after a fresh local pipe starts. [Valve IPC recovery](https://partner.steamgames.com/doc/api/ISteamUser#IPCFailure_t)

For the shipping relaunch check, use an unwrapped rendered build for an authorized Steam AppID. Install it through Steam and omit the development `steam_appid.txt`. Verify that Steam starts the installed copy when the executable is opened directly, and that a launch from the Steam Library proceeds through the same entry point. Spacewar is rejected before the relaunch call. [Shipping startup example](STEAMWORKS.md#start-the-client) · [Valve launch check](https://partner.steamgames.com/doc/sdk/api#SteamAPI_RestartAppIfNecessary)

## See a real achievement unlock

With Steam Overlay enabled, launch the rendered achievement fixture. Spacewar uses Valve’s existing **Winner** achievement and is free to test. Press **Unlock achievement** only on the signed-in test account you intend to change. The fixture waits for Steam's persistence and achievement callbacks, verifies the updated achievement state, and leaves the window open for you to see the Steam notification. An achievement already earned on that account cannot produce a fresh unlock in this fixture.

```powershell
& $test -GodotExe $godot -Case achievement_preview -Mode spacewar
```

To test a definition you created, open **Edit Steamworks Settings → Stats & Achievements → Achievements** for an AppID you administer. Create an available achievement in a developer-only set, set **Set By** to **Client**, and publish the app settings. Sign in with a developer account entitled to that AppID. Replace `123456` and `CRIMSON_TEST_UNLOCK` with your AppID and exact API name. This checks a newly published definition without releasing the game or store page. Valve charges its Steam Direct fee when you need a new app credit. [Valve achievement setup](https://partner.steamgames.com/doc/features/achievements/ach_guide) · [Steam Direct fee](https://partner.steamgames.com/doc/gettingstarted/appfee)

```powershell
& $test -GodotExe $godot -Case achievement_preview -Mode app -AppId 123456 -Achievement 'CRIMSON_TEST_UNLOCK'
```

Look for `CRIMSON_STEAM_ACHIEVEMENT_PREVIEW_PASSED` with the selected mode after the unlock. Check the Steam Overlay's Achievements pane or relaunch the fixture to confirm Steam now reads it as unlocked. The regular `stats -Mode spacewar` fixture only reads and validates existing achievement data.

## Open the rendered fixtures

The `overlay -Mode spacewar` case stages a matching Godot executable and a `steam_appid.txt`. Copy the printed evidence path into `$stage`. Run the staged Godot executable from its engine directory. Use the current framework project for the shared fixture paths and demo scenes below. For your own AppID, use a separate staging directory with its authorized launch context and change `$appId`. Steam-injected panes use `--crimson-steam-overlay=<AppID>` after `--` when launched directly.

```powershell
$project = (Resolve-Path '.\framework\project').Path
& $test -GodotExe $godot -Case overlay -Mode spacewar -Target template_debug
$stage = '<verification logs directory printed above>'
$engine = Join-Path "$stage\engine" (Split-Path $godot -Leaf)
$appId = 480
Push-Location "$stage\engine"
& $engine --path $project --script res://tests/online/steam_overlay.gd -- graphical $appId achievements "--crimson-steam-overlay=$appId"
& $engine --path $project --script res://tests/online/steam_browser.gd -- graphical $appId
Pop-Location
```

Run the other graphical checks with the same command shape. Replace the script and everything after `--` with the values below. [Hardware and media setup](STEAMWORKS.md#opt-in-hardware-and-media-fixtures)

| Script under `res://tests/online/` | Arguments and what to observe |
| --- | --- |
| `steam_overlay.gd` | `graphical <AppID> achievements --crimson-steam-overlay=<AppID>` opens the real overlay. Use `store [AppID]`, `profile <SteamID64>`, `web <URL>`, or `trade <SteamID64>` in place of `achievements` to check the selected pane. |
| `steam_browser.gd` | `graphical <AppID>` paints a fixture-owned loopback page and exercises input and teardown. |
| `steam_keyboard.gd` | `graphical <AppID> --crimson-steam-overlay=<AppID>` opens keyboard input in a focused field. Enter text or dismiss the Steam keyboard on supported hardware. |
| `steam_screenshots.gd` | `graphical <your AppID> --crimson-steam-overlay=<your AppID>` with `CRIMSON_STEAM_SCREENSHOTS_ALLOW_SAVE=1` saves a test frame in your Steam screenshot library. |
| `steam_remote_play.gd` | `graphical <your AppID> --crimson-steam-overlay=<your AppID>` uses an app with Remote Play Together and a second consenting player or test device. Confirm the Steam session, input, and disconnect. |
| `steam_timeline.gd` | `graphical <AppID> --crimson-steam-overlay=<AppID>` with Game Recording enabled and `CRIMSON_STEAM_TIMELINE_ALLOW_MARKERS=1`. Observe recorded footage for the test event and phase. Use `markers <AppID>` for marker lifecycle only. |
| `steam_input.gd` | `graphical <AppID> <manifest path> <set> <digital> <analog> [layer] --crimson-steam-overlay=<AppID>` with a configured controller and `CRIMSON_STEAM_INPUT_ALLOW_REBIND=1`. Exercise digital and analog actions, glyphs, and one deliberate rebinding. |
| `steam_device.gd` | `graphical <AppID> <OpenXR action>` checks a headset with SteamVR as its active OpenXR runtime. Run `device -Mode spacewar` on a Deck or under Proton for its platform detection. |
| `steam_music.gd` | `controls <AppID>` with a dedicated playback queue and `CRIMSON_STEAM_MUSIC_ALLOW_CONTROLS=1`. Observe playback controls and restored volume. |
| `steam_video.gd` | `entitled <AppID> <video AppID> [projection]` queries an entitled video. Use `graphical` with `CRIMSON_STEAM_VIDEO_ALLOW_BROADCAST=1` for a private start/stop broadcast check. |

## Run the multiplayer fixtures

Use the same staged engine and imported `$project`. Run hosts and guests in separate PowerShell windows from the staging engine directory. Use separate Steam accounts for a listen lobby, P2P traffic, a friend invite, or a party. The direct dedicated authority uses an anonymous GameServer login and can share the signed-in guest's PC. For non-480 dedicated tests, use the matching app launch context and set `$env:SteamAppId` and `$env:SteamGameId` to that AppID in each test window so the fixture selects it. On the host set `CRIMSON_STEAM_SERVER_VERSION`, `CRIMSON_STEAM_SERVER_DESCRIPTION`, and `CRIMSON_STEAM_SERVER_INSTALL_DIR` to the app's metadata.

```powershell
# Start this host first and copy its CRIMSON_STEAM_DEDICATED_READY locator.
& $engine --headless --path $project --script res://demo/tests/steam_dedicated.gd -- host-reconnect crimson-qa 27015 27016 127.0.0.1
# Then paste that locator in a second PowerShell window.
& $engine --headless --path $project --script res://demo/tests/steam_dedicated.gd -- guest-reconnect crimson-qa '<s1 locator>'
```

| Capability | Fixture script and user arguments after `--` |
| --- | --- |
| Listen lobby, discovery and invites | `res://demo/tests/steam_smoke.gd -- host <namespace>`, then `guest <lobby ID> <namespace>`. For discoverable lobbies add `--visibility=friends` or `--visibility=public` to the host and use `guest-discover <namespace>` on the guest. For the graphical invite flow add `--invite --crimson-steam-overlay=480` to the host, start the second account with `guest-invite <namespace>`, and accept the invitation in Steam. |
| Join Game controls and listen relay preference | `res://demo/tests/steam_smoke.gd -- listen <namespace> --visibility=friends --prefer-relay` exercises local route publication, suppression, and restoration. For a path observation, add `--prefer-relay` to the two-account host role and check both logs for `CRIMSON_STEAM_LISTEN_ROUTE` and its `direct`, `relay`, or `unknown` value after joining. Steam may still select direct routing. |
| Steam server browser | `res://demo/tests/steam_dedicated.gd -- internet-host <namespace> <game UDP port> <query UDP port> <reachable IPv4>` and `browse-guest <namespace> <s1 locator>` using an eligible app and a separate client network. Watch for the exact server row, inspection, and verified join. |
| Dedicated server progression | `res://demo/tests/steam_dedicated.gd -- progression-host <namespace> <game UDP port> <query UDP port> 127.0.0.1` and `progression-guest <namespace> <s1 locator>` exercise the exact admitted stats scope. Spacewar requests a native cache load without writing. `server_progression` in `missing` or `sdk-off` mode checks detached behavior. |
| Hosted dedicated relay | `res://demo/tests/steam_dedicated.gd -- relay-unavailable <namespace> <game port> <query port>` checks the configuration boundary. A Valve-enabled AppID and coordinator adapter are needed for `relay-host` and `relay-guest`. The guest requires an observed relay route. [Exact hosted commands](STEAMWORKS.md#dedicated-server-and-discovery) |
| Networking Sockets | `res://tests/online/steam_sockets.gd -- p2p-host 7 <guest SteamID64>` and `p2p-guest 7 <host SteamID64>`, with `CRIMSON_STEAM_SOCKETS_ALLOW_LIVE=1` on both sides. The regular Spacewar Sockets case already exchanges authenticated direct packets on loopback. |
| Networking Messages | `res://tests/online/steam_messages.gd -- standalone <other SteamID64>` on both sides with `CRIMSON_STEAM_MESSAGES_ALLOW_LIVE=1`. Use `game host <namespace>` and `game guest <namespace> <lobby ID>` for the Game binding and rebind path. |
| Parties | `res://tests/online/steam_parties.gd -- host <location ID> <namespace>` then `guest <beacon ID> <namespace>` with `CRIMSON_STEAM_PARTIES_ALLOW_LIVE=1` on both sides and access to the selected group location. |
| Voice | `res://tests/online/steam_voice.gd -- capture` with `CRIMSON_STEAM_VOICE_ALLOW_MICROPHONE=1` checks one microphone. Use `steam_voice_session.gd -- voice host <namespace>` and `voice guest <namespace> <lobby ID>` under `res://tests/online/` for two-account relay. Use `voice-policy` in place of `voice` for recipient checks without microphone access. |

To verify Steam's native Join Game launch path, enable **Use launch command line** in the app's Steamworks settings. From a second account, accept a join from a fully stopped client and verify that the pending invite reaches the game menu. Repeat with the game already running to exercise the Steam URL callback. Check that accepting the candidate uses the normal authenticated Game join. The direct-development fixture checks that a valid argument queues an invite with the Game still idle, and that duplicate or missing arguments queue nothing. [Valve launch arguments](https://partner.steamgames.com/doc/api/ISteamApps#GetLaunchCommandLine)

```powershell
& $engine --headless --path $project --script res://demo/tests/steam_smoke.gd -- launch-check 109775241006713287 +connect_lobby 109775241006713287
& $engine --headless --path $project --script res://demo/tests/steam_smoke.gd -- launch-check none +connect_lobby 109775241006713287 +connect_lobby 109775241006713288
& $engine --headless --path $project --script res://demo/tests/steam_smoke.gd -- launch-check none +connect_lobby
```

For a real server-awarded stat and achievement, use a private app with a test integer stat and achievement configured as **Game Server** or **Official GS**. For **Official GS**, configure the trusted server's IP range in Steamworks. In both progression test processes set `CRIMSON_STEAM_SERVER_STATS_TEST_WRITE=1`, `CRIMSON_STEAM_SERVER_STATS_TEST_STAT` to the stat API name, `CRIMSON_STEAM_SERVER_STATS_TEST_VALUE` to the requested integer, and `CRIMSON_STEAM_SERVER_STATS_TEST_ACHIEVEMENT` to the achievement API name. Use the app's launch context and metadata. Observe the host's confirmed Steam store and the guest's cached stat and unlock readback. [Valve server progression](https://partner.steamgames.com/doc/features/achievements#game_server_stats)

## Use app-owned data

Set the indicated environment variables in the same PowerShell window, then run `& $test -GodotExe $godot -Case <case> -Mode app -Target template_debug`. Each case uses the authorized non-480 AppID in its environment. Use disposable content and test accounts for state-changing checks.

| Case | Required settings and result |
| --- | --- |
| `apps` | `CRIMSON_STEAM_APPS_APP_ID`, `CRIMSON_STEAM_APPS_DLC_ID`, and `CRIMSON_STEAM_APPS_EXPECT_OWNED` (`0` or `1`). Optionally set `CRIMSON_STEAM_APPS_EXPECT_INSTALLED`, `CRIMSON_STEAM_APPS_EXPECT_BETA`, or `CRIMSON_STEAM_APPS_EXPECT_BUILD_ID`. `CRIMSON_STEAM_APPS_ALLOW_INSTALL=1` exercises installation for a licensed uninstalled test DLC. |
| `stats` | Set `CRIMSON_STEAM_STATS_APP_ID`, `CRIMSON_STEAM_STATS_ALLOW_WRITES=1`, and the names in `CRIMSON_STEAM_STATS_INT`, `CRIMSON_STEAM_STATS_FLOAT`, `CRIMSON_STEAM_STATS_ACHIEVEMENT`, and `CRIMSON_STEAM_STATS_BOARD`. Set `CRIMSON_STEAM_STATS_UGC_HANDLE` to an authorized test replay handle. The case writes both stats, unlocks the configured locked achievement, and verifies leaderboard and replay attachment. |
| `cloud` | Set `CRIMSON_STEAM_CLOUD_APP_ID` and `CRIMSON_STEAM_CLOUD_ALLOW_SHARE=1` for a Cloud-enabled app with quota. Exercise batch ownership and closure and write, share, download, and delete on fixture files. Check sync from the same account on a second device for cross-device proof. |
| `presence` | Set `CRIMSON_STEAM_PRESENCE_APP_ID`, `CRIMSON_STEAM_PRESENCE_ALLOW_WRITES=1`, and `CRIMSON_STEAM_PRESENCE_DISPLAY_TOKEN` to the published localization token. Inspect the rendered status from a Steam friend account for the social readback. |
| `workshop` | Set `CRIMSON_STEAM_WORKSHOP_APP_ID`. Use `CRIMSON_STEAM_WORKSHOP_ITEM_ID` for an app-owned item and `CRIMSON_STEAM_WORKSHOP_ALLOW_MUTATIONS=1` for subscription and item changes. `CRIMSON_STEAM_WORKSHOP_VOTE_ITEM_ID` selects another authorized author's test item. `CRIMSON_STEAM_WORKSHOP_ALLOW_PUBLISH=1` creates a private item and prints its ID for cleanup. `CRIMSON_STEAM_WORKSHOP_SHOW_EULA=1` runs a rendered check that opens the agreement in Steam. Enable the Steam Overlay and close the agreement window after observing it. |
| `inventory` | Set `CRIMSON_STEAM_INVENTORY_APP_ID` for app data reads. Select a fixture action with `CRIMSON_STEAM_INVENTORY_TEST_ACTION` and `CRIMSON_STEAM_INVENTORY_ALLOW_WRITES=1`. Supply the action's exact app-owned definitions and stacks using the `CRIMSON_STEAM_INVENTORY_TEST_` variables documented in the fixture. |

## Verify trusted publisher operations and DRM

Use an isolated, Steam SDK-enabled trusted test build. The mock suite runs publisher authentication and leaderboard writes, Wallet checkout and refunds, notifications, moderation, Inventory, and encrypted ownership against controlled inputs without contacting Valve's publisher endpoints. Use the production trusted build and the app's own configuration for live operations. [Trusted publisher build and live checks](STEAM_PUBLISHER.md#verify-locally) · [Inventory actions](../project/tests/online/steam_inventory.gd)

The runner copies the selected trusted fixture into its temporary project alongside the trusted DLL. The [publisher fixture scripts](../scripts/fixtures/steam_publisher/) live outside the framework Godot project because their base classes are only available in trusted builds.

The Wallet fixture checks buyer metadata and bounded settlement pages. The notification fixture checks that a fresh verified identity for the same launch cannot submit another opt-in request. To verify the full live notification path, open an app-owned session through Steam from a stopped client and repeat while the client is running. Confirm the typed session ID and let the authenticated backend verify membership before loading it. The moderation fixture checks the suspension boundary without issuing a real sanction. [Valve Game Notifications](https://partner.steamgames.com/doc/features/game_notifications#launching_specific_game_sessions)

```powershell
cmake -S framework -B "$env:TEMP/crimson-steam-mock" -A x64 -DCRIMSON_STEAM_PUBLISHER=ON -DCRIMSON_STEAM_PUBLISHER_TESTS=ON
cmake --build "$env:TEMP/crimson-steam-mock" --config Debug
$trustedMockBin = "$env:TEMP/crimson-steam-mock/bin"
& $test -GodotExe $godot -Case all -Mode mock -Target template_debug -BinaryDirectory $trustedMockBin
pwsh -NoProfile -File framework/scripts/Test-SteamDrm.ps1
```

Use a separate build directory with `-DCRIMSON_STEAM_PUBLISHER=ON -DCRIMSON_STEAM_PUBLISHER_TESTS=OFF` for the production trusted DLL. Point `$trustedBin` to that directory's `bin` folder. Run `& $test -GodotExe $godot -Case publisher_ownership -Mode invalid -Target template_debug -BinaryDirectory $trustedBin` to verify the production decoder rejects invalid ciphertext.

For an actual encrypted ownership round trip, the production trusted build needs `steam_api64.dll` and `sdkencryptedappticket64.dll`. The app must match the licensed test account and the 32-byte SDK Auth key. Save the raw key to a restricted file outside the repository. Pass its absolute path through the environment. The fixture requests one signed ticket, verifies the account and app, and rejects a wrong challenge and replay. Add `CRIMSON_STEAM_OWNERSHIP_DLC_EXPECTATIONS` with entries such as `123:owned,456:unowned` to check your app's actual DLC. Allow 60 seconds between encrypted ticket requests. [Valve encrypted ticket requirements](https://partner.steamgames.com/doc/features/auth#encryptedapptickets)

```powershell
$env:CRIMSON_STEAM_OWNERSHIP_ALLOW_LIVE = '1'
$env:CRIMSON_STEAM_OWNERSHIP_APP_ID = '<your authorized AppID>'
$env:CRIMSON_STEAM_OWNERSHIP_KEY_FILE = 'C:\restricted\test-app-sdk-auth-key.bin'
& $test -GodotExe $godot -Case publisher_ownership -Mode app -Target template_debug -BinaryDirectory $trustedBin
```

The mock DRM test uses a local SteamCMD substitute. For Valve's actual DRM wrapper, use an authorized SteamCMD account and app, point `Wrap-SteamRelease.ps1` at your exported executable, and upload the wrapped output and its companion files through SteamPipe. Assign the resulting build to a private test branch. Install and launch it in Steam with an entitled test account. [Framework DRM command](STEAMWORKS.md#steampipe-drm) · [Valve DRM](https://partner.steamgames.com/doc/features/drm) · [SteamPipe upload](https://partner.steamgames.com/doc/sdk/uploading)
