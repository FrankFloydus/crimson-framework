<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Steamworks architecture and verification

Crimson ships Steamworks support in the same native GDExtension as its standalone modules and game integration. A game can use Steam for app metadata, saves, input, or another capability without starting multiplayer. An online game composes Steam with Session and Scene Loader through `SteamProvider`. [SDK setup](../vendor/README.md) · [Run the Steam tests](STEAM_TESTING.md) · [Public game API](PUBLIC_API.md) · [Approved Steam API contract](STEAM_PUBLIC_API_PROPOSAL.md)

## Ownership and startup

| Area | Responsibility |
| --- | --- |
| [Steam module](../src/modules/steam/) | Own the process Steam client runtime, typed client services, callback dispatch, and native SDK handle cleanup. Client scopes share the signed-in AppID and pump callbacks at most once per frame. |
| [Steam lobby services](../src/modules/steam/lobbies/) | Coordinate one SDK account membership and one search across client and Game scopes. Social metadata and chat stay separate from game route validation. |
| [Steam game integration](../src/integration/online/steam/) | Translate Steam lobby and transport events into Crimson admission, identity, and game lifecycle actions. The game keeps control of lobby policy, player placement, world loading, and presentation. |
| Session backends | Move the reusable Steam transport into Session once a standalone configuration can create, pump, and verify it through the public Session contract. The Steam provider will compose the same transport. |
| Steam dedicated server | Own a separate `SteamGameServer` runtime, login, callback pipe, authentication, and publication lifecycle. Adapt its transport to the native headless Crimson server and keep direct ENet available. |
| Publisher backend and tools | Perform operations that require trusted publisher credentials, including Web API authentication, payments, notifications, and bans. Run these in explicitly configured server-side tools. SteamPipe wrapping and depot verification belong in the export and release process. |

The client runtime owns SDK initialization and shutdown. The internal client owner or a game provider holds a lease. An ended attachment invalidates its service handles before any cancellation callback can use them, even if another consumer still holds the SDK. Call the facade and its services on Godot's main thread. A direct graphical launch can opt into early Steam initialization with the documented overlay argument. SDK-off builds register the same public classes and report unavailable on startup.

Keep each Steam capability in its own service with typed Godot methods. Map Steam identifiers and opaque handles to values that preserve their full range. SDK handles stay native, and operations that own them release them on completion, failure, cancellation, and scope teardown. For future callbacks with borrowed pointers, copy the payload into bounded, owned storage before releasing the SDK event. Keep process-owned Steam services outside replaceable worlds.

## Start the client

Boot Steamworks once from an existing persistent scene-tree owner. Pass the returned facade or a typed service to the code that needs it. Crimson takes the AppID from the Steam launch context. Pass `SteamConfig.spacewar("my-test-group")` when you want to verify a Spacewar launch.

```gdscript
var steam := Steamworks.boot(self)
var startup := steam.boot_result()
if not startup.ok:
    push_error(startup.message)
    return

print(steam.apps().current_language())
steam.friends().set_rich_presence("mode", "co-op")
```

`boot(owner)` returns a typed facade with one startup result, including the SDK's reason on failure. The existing owner controls the internal callback client's lifetime. Removing the owner from the tree retires the scope and its services. Direct actions such as `open_achievements()` report startup or availability failures themselves. Inspect `boot_result()` when you need to bind advanced typed services during setup. The original `SteamClient` API remains available for existing scripts.

`is_ready()` reports whether the facade still owns its original active Steam client generation. `is_online()` reports whether that client currently has a live connection to Valve. Offline Mode can still provide cached progression. A temporary loss of network connectivity preserves the client scope. Fatal Steam IPC failure invalidates all services sharing the failed pipe and retires active Steam game connections through the Game lifecycle. A standalone owner can respond to `runtime_lost` by deferring a fresh `Steamworks.boot(self)` until the failure callback returns. Resolve new service handles after recovery. [Valve connectivity](https://partner.steamgames.com/doc/api/ISteamUser#BLoggedOn) · [Valve IPC recovery](https://partner.steamgames.com/doc/api/ISteamUser#IPCFailure_t)

An unwrapped shipping client can opt into Valve's relaunch check from its common entry point for Steam launches and direct executable launches. Call it before any Steam initialization, including attaching a Steam provider or using the early overlay bootstrap.

```gdscript
var launch := Steamworks.restart_through_steam_if_needed(app_id)
if launch == ERR_SKIP:
    get_tree().quit()
    return
if launch != OK:
    push_error("Could not check Steam launch context.")
    return
var steam := Steamworks.boot(self)
```

Use a shipping AppID and the packaged Steam runtime in a rendered export. Steam relaunches the installed Library copy when needed. The DRM wrapper already performs this check for the wrapped executable. Leave development `steam_appid.txt` files out of shipped depots. [Valve launch check](https://partner.steamgames.com/doc/sdk/api#SteamAPI_RestartAppIfNecessary)

For a game configured with `.steam()`, use `game.steam()`. The game creates and starts its Steam client under the persistent Session. The same attachment supplies lobbies and typed app services, so the game needs no separate client boot. A detached scope expires even if the owner later enters the tree again. Legacy integrations can use `SteamServices.for_game(game_runtime)`.

The Apps service reads current language, available languages, active beta branch, local subscription through `is_subscribed_to_app(app_id)`, and installed DLC. An empty beta branch denotes the default branch or unavailable data. Use verified tickets for ownership checks that grant access on a server. The Friends service updates a specific presence key and can clear it without touching other keys. Steam's displayed rich presence requires an app-configured localization token.

For Steam Game Notifications, read `steam.apps().notification_session_id()` during startup and connect `notification_session_requested(session_id)` to handle a new session link while the game runs. The value is a decimal string that preserves the full unsigned Steam session ID. Ask the backend to verify the authenticated player's membership before resuming. [Valve notification launches](https://partner.steamgames.com/doc/features/game_notifications#launching_specific_game_sessions)

Use `steam.device()` for hardware, suggested graphics presets, Proton and VR status, and opt-in text entry. Treat the suggested preset as an input to initial game settings. Keep saved player settings authoritative. In a graphical window, pass a focused `LineEdit` or `TextEdit` to `show_keyboard_for()`. Await `enter_text()` for modal text entry. Use Godot's OpenXR API for the actual XR session. `steam.utils()` provides the Steam UI language, overlay readiness, and underlying hardware queries.

Run the standalone client fixture in PowerShell 7. It stages an isolated Godot project and enables Spacewar only for the test process. The signed-in test uses one temporary presence key, checks Steam's local readback, and restores its prior value. The other modes verify explicit missing-runtime and SDK-off behavior.

```powershell
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Mode spacewar
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case utils -Mode spacewar
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Mode missing
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Mode sdk-off -Target template_debug -BinaryDirectory $sdkOffBin
```

## Lobbies and game invitations

Use [the standalone lobby API](PUBLIC_API.md#standalone-social-lobbies) for social membership, owner-managed metadata and chat. A hosted game exposes its current lobby through `SteamServices.for_game(game).lobbies().current()` and owns admission, verified transport, and membership teardown. Standalone and Game scopes share one exact account reservation.

Enable **Use launch command line** under **Installation → General** in Steamworks to send Rich Presence launch arguments through Steam. Read `game.invites().pending()` at cold boot and subscribe to `game.invites().received` from the persistent menu for accepted invitations and Steam URL launches while running. Both provide a candidate that the player can confirm through normal Game admission. A ready public or friends-only host advertises Join Game while it has room. Call `steam.presence().clear_joinable()` to suppress this route for the current listing without clearing the display status, and `joinable(lobby.listing())` to restore it while admission remains open. Select `SteamProvider.prefer_relay()` to bias listen connections toward Steam relay routing. Verify the route with connection diagnostics because Steam can still select a viable direct route. [Valve launch arguments](https://partner.steamgames.com/doc/api/ISteamApps#GetLaunchCommandLine) · [Valve Rich Presence](https://partner.steamgames.com/doc/api/ISteamFriends#SetRichPresence)

```powershell
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case lobbies -Mode missing
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case lobbies -Mode spacewar
```

The signed fixture creates temporary private lobbies and sends one bounded chat to its own account. It checks typed member snapshots, owner metadata, local rate and size bounds, cancellation, and independent scope teardown. The demo's `steam_smoke.gd -- coexist` also checks that neither standalone nor Game code can replace an already owned membership. Use two consenting test accounts to verify a separate guest's metadata, invite and chat. [Valve lobby callbacks](https://partner.steamgames.com/doc/api/ISteamMatchmaking#LobbyChatMsg_t)

## Standalone Sockets and route measurements

An application that owns its own protocol can use Steam Sockets directly. An outgoing P2P connection pins the Steam identity in the endpoint. A direct endpoint can also pin an expected Steam identity with `expect_peer(host_id)`. A listener receives a short-lived offer through `connection_requested`. Accept the exact offer and wait for `connected` before using the authenticated remote identity or sending data.

```gdscript
var endpoint := SteamSocketEndpoint.p2p(host_id, 7)
var connected: SteamSocketConnectionResult = await steam.sockets().connect_to(endpoint).wait()
if connected.ok:
    connected.value.send(payload)
```

For gameplay, sample the current admitted peer through `SteamServices.for_game(game).networking().peer_path(peer_id)`. Inspect `kind`, `ping_ms`, and the local delivery estimate on the result. `DIRECT` and `RELAY` reflect Steam's observed connection, while `UNKNOWN` indicates an unavailable route measurement. The signed Sockets fixture exercises pinned direct traffic, expired offers, cancellation, and teardown. Use the [two-account P2P check](STEAM_TESTING.md#run-the-multiplayer-fixtures) for real remote traffic.

## Steam Messages side channels

Supply a bounded app channel allowlist when building the Game provider. A guest binds its authenticated authority. An authority binds a Steam-certified guest once it reaches the mounted world.

```gdscript
var channels: Array[StringName] = [&"latency_probe"]
var provider := SteamProvider.configure().for_app(app_id).friends_only().with_message_channels(channels)
game.definition = game.definition.with_provider(provider)
```

After the peer reaches the game, resolve its scoped permission and send the timing sample.

```gdscript
var result := SteamServices.for_game(game).messages().for_peer(peer_id)
if result.ok:
    result.value.send(&"latency_probe", timing_bytes, SteamDelivery.UNRELIABLE)
```

A standalone client can explicitly authorize an individual Steam account through `steam.messages().authorize(account, channels)`. Each account has one local owner for Crimson's reserved native Messages channel. The SDK supplies the sender's authenticated identity. Game Messages additionally match the exact admission instance, provider namespace and world epoch. Both sides prove fresh binding nonces before delivering app data. When negotiation is pending, the service queues up to eight reliable frames and 16 KiB for up to 15 seconds. `OK` confirms local queue acceptance. A change in the remote binding drops payloads queued under the previous binding. An unreliable send reports `ERR_BUSY` until the binding is ready.

Each direction permits up to 60 frames and 64 KiB per peer per second. Frame payloads are limited to 4,096 bytes. Reliable and unreliable delivery track independent sequences. The permitted sequence advance grows with elapsed time so a long loss burst can recover. A departed Game peer or epoch change retires the handle, and close releases only Crimson's native channel. [Valve Networking Messages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages)

`Test-Steam.ps1 -Case messages` checks registration, invalid and self-authorization, and stale scope behavior without sending. With two signed-in test accounts and `CRIMSON_STEAM_MESSAGES_ALLOW_LIVE=1`, run `steam_messages.gd -- standalone <other-test-account>` on each machine. To verify Game admission and teardown, use `-- game host <namespace>` and `-- game guest <namespace> <lobby-id>` against the current demo. These opt-in modes send only fixed fixture data over the selected test session.

## Party beacons and voice

Read eligible group-chat locations with `steam.parties().locations()`. The signed-in client may return an empty list. Publish after the owning Game mounts its host world and opens a non-private Steam lobby. This is an explicit Steam group publication. The exact lobby, available seats, and selected location are checked again at submission.

```gdscript
var parties := SteamServices.for_game(game).parties()
var published: SteamPartyBeaconResult = await parties.publish(hosted_listing, selected_location, 2)
var followed: SteamListingResult = await parties.reserve(selected_beacon)
if followed.ok:
    await game.join_listing(followed.value).wait()
```

The authority keeps one live beacon for the local Steam process, subtracts admitted players and pending reservations from available seats, and automatically acknowledges a reserved account after it reaches the mounted world. An abandoned reservation expires after 30 seconds. Leaving the hosted Game withdraws the beacon. A guest reservation returns a locator pinned to the Steam-observed beacon owner. Normal lobby admission verifies the actual owner before connecting. [Valve Parties](https://partner.steamgames.com/doc/api/ISteamParties)

`game.voice()` returns the persistent voice service for a Steam game. Bind capture only after consent and a deliberate push-to-talk press. Feed the decoded PCM and sample rate into the game's audio sink. For team or proximity voice, install an authority-owned recipient predicate that reads trusted actor state.

```gdscript
var voice := game.voice()
if voice != null:
    push_to_talk.button_down.connect(voice.start_talking)
    push_to_talk.button_up.connect(voice.stop_talking)
    voice.frame_received.connect(play_voice)
```

Game voice runs over logical gameplay channel 3 with unreliable ordered delivery. The authority checks the actual Steam-authenticated sender, the committed world epoch, world readiness and recipient policy before relaying. Limits are 1,200 bytes per frame, 60 frames and 32 KiB per sender per second, and 256 KiB of authority relay data per second. Clients drop old or muted frames before decoding. The server forwards encoded bytes and keeps microphone and playback access with the signed-in game clients. Direct codec users can use `steam.voice().capture()` and `steam.voice().decode(bytes)` to supply their own application route. [Valve Voice](https://partner.steamgames.com/doc/features/voice)

Use `Test-Steam.ps1 -Case parties` and `-Case voice` for Spacewar, unavailable runtime, and SDK-disabled checks. Those fixtures leave publication and microphone access inactive. For a selected two-account test, launch the current project with `steam_parties.gd -- host <location-id> <namespace>` and `-- guest <beacon-id> <namespace>` after setting `CRIMSON_STEAM_PARTIES_ALLOW_LIVE=1` in those processes. Use friends with access to the selected location. `steam_voice_session.gd -- voice-policy host <namespace>` and `-- voice-policy guest <namespace> <lobby-id>` exercise sender, epoch, size and replay rejection without microphone access. Replace `voice-policy` with `voice` and set `CRIMSON_STEAM_VOICE_ALLOW_MICROPHONE=1` for a deliberate capture, decode and mute check on both machines. The game still supplies its audio playback sink.

## Dedicated server and discovery

Use [the dedicated API](PUBLIC_API.md#dedicated-steam-games) to configure a playerless Steam authority and connect using its explicit direct locator or a browser result. The GameServer API uses a native pipe independent of the signed-in client's SDK. A secure host requires Steam logon, VAC policy, and a current Steam Networking certificate. Each guest's ticket is issued for the selected server and verified against the actual transport-certified Steam account. Host leave, authority shutdown, and invalid subsequent Steam validation retire the exact ticket session. [Valve SteamGameServer](https://partner.steamgames.com/doc/api/ISteamGameServer) · [Valve auth tickets](https://partner.steamgames.com/doc/features/auth)

Set `SteamServer.with_product_metadata(version, description, install_dir)` for an advertised production game. Use the version expected by Steam's master server, the game's description, and the Steam install folder name. `SteamServerStatus.advertisement_requested` reports the local advertisement request and is also available through the existing `listed` property. Confirm discovery from a separate client's Internet query. [Valve server version](https://partner.steamgames.com/doc/api/steam_gameserver#SteamGameServer_Init) · [Valve server metadata](https://partner.steamgames.com/doc/api/ISteamGameServer#SetModDir)

Stage Godot and the SDK runtime with Spacewar AppID 480 in isolated processes for development. Run the host as `res://demo/tests/steam_dedicated.gd -- host <namespace> <game-port> <query-port> 127.0.0.1` and pass its `CRIMSON_STEAM_DEDICATED_READY` locator to `-- guest <namespace> <locator>` in the signed-in client. The fixture checks the playerless unlisted authority, the guest's exact server identity and local actor, the host's observation of the verified guest actor, and both retirements. The `host-only` role checks anonymous secure startup and leave. The `missing`, `sdk-off`, and `lan-unavailable` roles check explicit diagnostics.

Run `host-reconnect` and `guest-reconnect` on loopback to reject a deliberately invalid fixture proof and then connect the same guest twice with fresh identities and the same host world. Run `internet-host` and `browse-guest` with an eligible AppID and reachable gameplay and query UDP ports to verify Steam discovery, selected-server inspection, and the typed join from another client network.

For hosted Steam Datagram Relay, build with Valve's [Game Coordinator SDK headers](https://media.steampowered.com/apps/sdr/sdk/steamdatagram_gamecoordinator_sdk.zip) and install the authorized deployment configuration outside the game. Run `relay-host <namespace> <provisioned-game-udp-port> <query-port> <profile> res://your_coordinator.gd [virtual-port]`. Run `relay-guest <namespace> <h1-locator> res://your_coordinator.gd` on the signed-in client. The fixture requires Steam to report an actual relayed path. `relay-unavailable` checks a missing coordinator. `relay-unprovisioned` checks that an adapter with no hosted UDP deployment does not submit a registration or mount a world. [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay)

```powershell
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case server_browser -Mode spacewar
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case server_browser -Mode missing
```

The read-only browser fixture checks a bounded empty search, cancellation, and scope release. Use an explicitly advertised server and a separate client network to verify public discovery. An Internet advertisement request alone supplies no read-back evidence. [Valve matchmaking server lists](https://partner.steamgames.com/doc/api/ISteamMatchmakingServers)

## HTTP and diagnostics

Use `fetch()` for a GET with the default limits. The result carries transport status. A complete response remains available as `value` when the server returns 4xx or 5xx. Check `value.is_successful()` when the call needs a 2xx status. Read `result.message` for the rejected setting or the reason transport, response validation, or cancellation ended the request.

```gdscript
var response: SteamHttpResponseResult = await steam.http().fetch(version_url)
if response.ok and response.value.is_successful():
    var payload := response.value.get_body()
```

Options return independent branches. The same terminal works with or without options. Use `head()` for metadata, `post()`, `put()`, and `patch()` for JSON objects, or `delete()` and `options()` for their corresponding methods.

```gdscript
var rotation: SteamHttpResponseResult = await steam.http().accept_json().with_timeout(3000).fetch(rotation_url)
if rotation.ok and rotation.value.is_successful():
    var decoded := rotation.value.json()
    if decoded.ok:
        browser_view.show_rotation(decoded.value)
```

Use `request()` when you need a cancellable handle or raw request bytes. Call `send()` to submit the draft. The operation supports `is_finished()`, `result()`, `wait()`, and `cancel()`.

```gdscript
var request := steam.http().accept_json().with_timeout(3000).request(version_url)
var operation := request.send()
var response: SteamHttpResponseResult = await operation.wait()
```

The response always captures `Content-Type` when available. `with_response_headers(names)` selects up to 16 additional fields. Field names are case-insensitive. A captured field above 2,048 bytes or a body beyond the configured cap settles with a limit error and an empty response value. The request requires verified certificates for HTTPS. Steam can buffer data before Crimson checks the configured limits. Cancellation and client stop release the SDK request and settle with `ERR_SKIP`.

Run `Test-Steam.ps1 -Case http -Mode spacewar` against the controlled loopback fixture to exercise both GET paths, JSON verbs, selected metadata, HEAD, HTTP status errors, payload bounds, and teardown. The fixture generates a temporary self-signed loopback certificate to exercise rejection without changing the system trust store. Pass `-TrustedHttpsUrl` with an HTTPS endpoint you can use to check a valid certificate and 2xx response. The SDK-off and missing-runtime lanes exercise the unavailable result.

Use `steam.diagnostics()` after startup to opt into the SDK warning hook. One client scope can own capture at a time. `start()` returns `OK` for the owner, `ERR_BUSY` during contention, or `ERR_UNAVAILABLE` if the client scope or hook is unavailable. Call it on the main thread.

```gdscript
var diagnostics := steam.diagnostics()
if diagnostics != null and diagnostics.start() == OK:
    var batch := diagnostics.drain()
    for message in batch.messages:
        print("%d %s" % [message.severity, message.text])
    if batch.dropped_count > 0:
        print("Steam diagnostics dropped %d messages." % batch.dropped_count)
    diagnostics.stop()
```

Drain from the main thread on a developer console refresh or after the operations you are investigating. Each batch holds a read-only typed array of up to 32 records in capture order. Each record gives the Steam severity and a fixed description that keeps the original SDK payload private. The drain resets the overflow count. An inactive or non-owner service returns an empty batch. `stop()` discards pending messages and releases the hook. Stopping the client also releases it. [Valve warning hook](https://partner.steamgames.com/doc/api/ISteamUtils#SetWarningMessageHook)

## Progression and Cloud

Use `steam.achievements().unlock("first_win")` for a single published achievement. Use `edit()` to submit the stat and achievement changes from one game event together. Both calls use the same cache validation and storage submission.

```gdscript
var submitted := steam.stats().edit() \
    .set_int("kills", total_kills) \
    .unlock("first_win") \
    .submit()
```

Each modifier returns a new draft. `submit()` validates the draft before editing Steam's cache and requests persistence once after all edits succeed. Its typed result exposes `ok`, `error`, `message`, and a receipt under `value`. `value.applied` counts edits accepted into the cache, and `value.failed_at` gives the zero-based index of a rejected edit or `-1` when there is no single rejected edit. Accepted cache edits can remain after a later edit or persistence request fails. `OK` means Steam accepted the persistence request. Steam's `stats_stored` callback reports an app-wide result and can also follow achievement progress.

Use `steam.stats()` for typed cached reads, achievement metadata, and individual edits. Read `stat_int(name).value` or `stat_float(name).value` after checking the result's `ok` property. `achievements()` returns `Array[SteamAchievement]`, and `achievement(api_name)` returns the cached snapshot or `null`. `show_progress()` requests a progress popup. `store()` submits the modified cache.

For server-owned progression, configure each definition as **Game Server** or **Official GS** in Steamworks. On a Steam dedicated authority, use `SteamServices.server_for(game).stats(peer_id)` for an admitted player. Call `await player_stats.load().wait()` before cached reads or submit an edit draft and let it load if needed. The handle expires when that admission ends, including after a reconnect by the same account. A submit result confirms Steam's store response and reports the number of accepted cache edits. If Steam accepted edits but did not confirm persistence, inspect the result before retrying `store()`. [Valve server progression](https://partner.steamgames.com/doc/api/ISteamGameServerStats)

```gdscript
var leaders: SteamLeaderboardPageResult = await steam.leaderboards().top("weekly_speedrun", 10)
if leaders.ok:
    for entry in leaders.value.entries:
        scoreboard.add_time(entry.rank, entry.steam_id, entry.score)
```

`top()` looks up a published leaderboard and downloads the first page. It accepts a count from 1 to 100 and defaults to 10. Use `friends(board_name)` or build a branch with `query(board_name)` for a different page. The typed page exposes `entries` and `total_count`. Use `submit_score(board_name, score)` to submit a keep-best score. Compose `with_details()` and `with_replay()` for a replay-backed run. Steam can accept the score and reject the replay. That result retains the accepted score receipt under `value.upload`.

Use `steam.cloud()` for app-scoped files and quota. Check each operation's terminal error before consuming returned data. A cancelled observation can leave a submitted Steam write in flight. After a write succeeds locally, verify cross-device synchronization with a separate Steam session on the other device.

```gdscript
var write: SteamResult = await steam.cloud().write("player-profile.json", json_bytes)
if write.ok:
    var read: SteamBytesResult = await steam.cloud().read("player-profile.json")
    if read.ok:
        var profile_bytes := read.value
```

Use `read_file()` or `write_file()` when you need to retain and cancel a pending operation. `file_info()`, `files()`, and `quota()` return typed metadata and quota snapshots. Use `share()` only for a file the game deliberately shares. A successful `download_shared()` returns the handle, filename, owner SteamID, and bytes in a typed snapshot. Test write and share behavior with an authorized non-Spacewar AppID.

For several related writes or deletes, call `cloud.begin_write_batch()` and check the returned `SteamCloudWriteBatchResult`. Keep `result.value` open while the group is written and call `close()` on the main thread when finished, including after a write error. The hint applies to explicit Remote Storage and local files included in Steam Auto-Cloud. One process batch can be active at a time. Steam still handles synchronization, and each file retains its own write and failure semantics. [Valve Cloud write batches](https://partner.steamgames.com/doc/api/ISteamRemoteStorage#BeginFileWriteBatch)

### Game Recording

```gdscript
var timeline := steam.timeline()
timeline.start_phase(match_id)
timeline.add_phase_tag("Transfer Hall", "steam_marker", "Map")
var marker := timeline.mark("Final portal sealed")
timeline.end_phase()
if marker.ok:
    var footage: SteamBoolResult = await timeline.recording_exists(marker.value)
```

Use `mark_range()` for a known duration or `start_range()` and `end_range()` for an open range. A phase belongs to its starting scope and prevents another scope from starting a phase until it ends. The `markers` fixture exercises marker submission, both recording queries, and phase ownership. A headless Spacewar run returned no footage. Enable Steam Game Recording in a rendered session to verify playback. [Valve Timeline API](https://partner.steamgames.com/doc/api/ISteamTimeline)

### Social, hardware, and media checks

Use `steam.workshop().subscriptions()` for a bounded snapshot of the signed-in user's Workshop IDs for this app. Inspect `value.published_ids` and `value.truncated`. A publishing result can preserve the item ID and indicate that the user needs to accept Steam's legal agreement. On explicit player action, `show_legal_agreement()` requests Steam's overlay. Query `await legal_agreement_status()` before continuing the update with the preserved item ID. [Valve Workshop subscriptions](https://partner.steamgames.com/doc/api/ISteamUGC#GetSubscribedItems) · [Valve Workshop agreement](https://partner.steamgames.com/doc/api/ISteamUGC#ShowWorkshopEULA)

`steam.friends().list()` returns detached typed friend rows. `steam.presence().show()` manages a Steam localization token and optional party group for one scope. A status update writes keys individually. If Steam rejects a later key, previously accepted writes remain tracked for cleanup. Explicit Friends writes to custom keys relinquish managed ownership even when they write the same value.

Use `-Case screenshots`, `remote_play`, `device`, `keyboard`, `input`, `music`, `video`, or `browser` with `Test-Steam.ps1`. The signed Spacewar runs exercise available status queries, input startup, scope teardown, and validation. They leave music playback and volume alone, and use Spacewar as a negative video lookup. Use the opt-in rendered fixtures and relevant controller, headset, Steam Deck, or remote device for player-visible behavior. [Steam Input](https://partner.steamgames.com/doc/api/ISteamInput) · [Steam Remote Play](https://partner.steamgames.com/doc/api/ISteamRemotePlay)

The HTML Surface URL policy applies to main-document navigation and rendered content. Steam SDK 1.65 can fetch an HTTP redirect destination before notifying the game. The browser wrapper checks the resulting document URL, closes unexpected documents, and withholds their paint frames. Use an owned content origin. [Steam HTML Surface callbacks](https://partner.steamgames.com/doc/api/ISteamHTMLSurface)

#### Opt-in hardware and media fixtures

Run these scripts in a Godot project with the current addon and the indicated fixture copied into `tests/online/`. Supply the Steam launch context for the chosen AppID. For a loose development executable, put `steam_appid.txt` beside the executable in an isolated staging directory. Include `--crimson-steam-overlay=<AppID>` in the user arguments for the Input and Timeline graphical lanes so Steam can initialize the overlay before Godot opens its window. For example, from `framework/` with a staged Godot path in `$GodotExe`:

```powershell
$env:CRIMSON_STEAM_TIMELINE_ALLOW_MARKERS = '1'
& $GodotExe --path .\project --script res://tests/online/steam_timeline.gd -- graphical $AppId "--crimson-steam-overlay=$AppId"
```

| Script | User arguments and positive result |
| --- | --- |
| `steam_input.gd` | `graphical <AppID> <absolute loose manifest> <set> <digital> <analog> [layer]` with `CRIMSON_STEAM_INPUT_ALLOW_REBIND=1`. Use a test controller and its own binding profile. Press the digital action, move the analog action, then save a deliberate test binding change in Steam. The fixture checks both input samples, glyph textures, and the binding change event. |
| `steam_timeline.gd` | `graphical <AppID>` with `CRIMSON_STEAM_TIMELINE_ALLOW_MARKERS=1` and Steam Game Recording enabled for the test app. The fixture records a synthetic color change, checks actual range and phase footage, and observes Steam overlay activation when opening the event and phase. Close each pane when prompted. `markers <AppID>` uses the same opt-in to test marker lifecycle without requiring recorded footage. |
| `steam_device.gd` | `graphical <AppID> <OpenXR boolean action>` with a headset, the caller's OpenXR action map, OpenXR enabled at project launch, and SteamVR as the active OpenXR runtime. Look for the cube and press the configured right-hand action. Confirm the dashboard appears, then stop the test session to exercise the native stopping signal. |
| `steam_music.gd` | `controls <AppID>` with `CRIMSON_STEAM_MUSIC_ALLOW_CONTROLS=1` and an enabled, active test soundtrack queue. The fixture checks pause, play, next, previous, and a volume change. It restores the original playback state and volume. Steam does not expose the current track through this API, so use a dedicated test queue. |
| `steam_video.gd` | `entitled <running AppID> <entitled video AppID> [projection]` checks the lookup without logging the stream URL. Add `projection` when the test video has projection data. Use `graphical` in place of `entitled` with `CRIMSON_STEAM_VIDEO_ALLOW_BROADCAST=1` and a private test broadcast. Start and stop the broadcast through Steam to check its callbacks and upload status. |

### Authentication and checkout

```gdscript
var issued: SteamWebTicketResult = await steam.auth().web_ticket("game-profile")
if issued.ok:
    var ticket := issued.value
    await profile_backend.sign_in(ticket.bytes())
    ticket.close()
```

Use the same audience and AppID when verifying the Web API ticket on the trusted backend. Keep ticket bytes out of logs, saves, and replays. Spacewar can verify issuance and revocation scope. The backend verifies identity, license ownership, and any encrypted challenge separately. [Valve Steam authentication](https://partner.steamgames.com/doc/features/auth)

Bind the game's authenticated adapter with `SteamProvider.configure().with_commerce_backend(adapter)`. `await steam.purchases().checkout(cart)` returns a backend receipt or a recoverable checkout ID. Retry the same cart and ID after an ambiguous response. Implement the adapter's typed completion hooks and use `order(order_id)` to refresh the order. Steam's authorization callback tells the backend when to check an order. The backend confirms the fulfillment state. [Trusted publisher setup](STEAM_PUBLISHER.md) · [Valve microtransactions](https://partner.steamgames.com/doc/features/microtransactions/implementation)

### SteamPipe DRM

Run `Wrap-SteamRelease.ps1` with the exported Windows executable and an unused output path. Use your app's authorized SteamCMD account. Valve receives the input executable to perform the wrap. The script preserves the input and only moves a successful wrapped output into the destination.

```powershell
.\framework\scripts\Wrap-SteamRelease.ps1 -SteamCmd $steamCmd -AppId $appId -InputExe $exportedExe -OutputExe $depotExe -Account $steamAccount
```

The controlled process test covered both wrap modes, paths with spaces, failure cleanup, and collision handling. Real wrapping and installed-depot launch verification require an authorized app. [Valve's Steam DRM procedure](https://partner.steamgames.com/doc/features/drm#automated)

## Steam Overlay

A pause menu can issue the default request from its Achievements button. The facade handles initialization and overlay readiness for this action.

```gdscript
var opened := steam.open_achievements()
if not opened.ok:
    status_label.text = opened.message
```

Get `steam.overlay()` for advanced pane targets or activation events. Use `open_store()` for the running game's Steam Store page or `open_store(other_app_id)` to choose another product. `open_profile(steam_id)` accepts an individual decimal SteamID64. `open_web(url)` accepts an absolute HTTP or HTTPS URL without embedded credentials, up to 2048 UTF-8 bytes. `open_trade(partner_steam_id)` opens the trading pane after a trusted backend starts the authorized trade. [Steam Overlay](https://partner.steamgames.com/doc/features/overlay) · [Valve's pane actions](https://partner.steamgames.com/doc/api/ISteamFriends#ActivateGameOverlay)

Check `is_ready()` before enabling Overlay controls. `ERR_BUSY` means a live Steam overlay is not accessible yet or the call came from another thread. `ERR_UNAVAILABLE` covers detached handles and unavailable SDK interfaces. Invalid targets on a live handle return `ERR_INVALID_PARAMETER`. `OK` means Steam received a pane request. Observe the rendered pane to confirm what opened. [Valve's readiness query](https://partner.steamgames.com/doc/api/ISteamUtils#IsOverlayEnabled)

Read `is_active()` when binding a pause menu and subscribe to `activation_changed(active)`. Suspend local gameplay input and release mouse capture while Steam has focus. When it closes, restore the owning screen's last valid focus or recapture the mouse if gameplay owns input. An online authority and its world continue to run. Each client start receives its own facade. Stopping it detaches that facade while remaining runtime consumers retain the last observed activation. [Valve's activation callback](https://partner.steamgames.com/doc/api/ISteamFriends#GameOverlayActivated_t)

### Overlay verification

Use the standalone CLI fixture for detached, SDK-off, and signed-in Spacewar modes. The signed-in case checks invalid targets and independent scope lifetimes, and reports Steam's actual readiness without opening a pane.

```powershell
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case overlay -Mode missing
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case overlay -Mode sdk-off -Target template_debug -BinaryDirectory $sdkOffBin
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case overlay -Mode spacewar
```

For a graphical check, enable the overlay in Steam and launch a rendered window from Steam or from the [direct development setup](../vendor/README.md#spacewar-480). The Spacewar CLI run leaves an imported fixture project and matching engine in its reported temporary directory. Use that engine directory as the working directory and its isolated `template_debug/project` as `$project`.

Spacewar uses Valve's achievement definitions for AppID `480`. The graphical Achievements test opens that existing list. Use the [interactive achievement preview](STEAM_TESTING.md#see-a-real-achievement-unlock) to opt into a real unlock of Valve's Winner achievement or a published definition in an app you administer. [Valve's Spacewar sample](https://partner.steamgames.com/doc/sdk/api/example) · [Steamworks achievements](https://partner.steamgames.com/doc/features/achievements)

```powershell
& $godot --path $project --script res://steam_overlay.gd -- graphical 480 achievements --crimson-steam-overlay=480
```

Choose `achievements`, `store [app_id]`, `profile [steam_id]`, `web <url>`, or `trade <partner_steam_id>` as the explicit fixture action. An omitted profile target selects the signed-in test account. Use a configured test AppID where the requested pane is unavailable under Spacewar. The trade test requires an existing backend-started trade between authorized test accounts. Check the visible pane, then close it with the Steam overlay shortcut. The fixture prints request issuance and both activation transitions. Its pass marker confirms callback and scope behavior. Record the visible pane separately. [Valve's overlay requirements](https://partner.steamgames.com/doc/features/overlay#requirements)

## Capability verification plan

Each capability gets a runnable CLI case or an explicit action in an opt-in Godot fixture. An asynchronous case reports success only after the relevant result and observable effect. Record the AppID, build, account topology, fixture mode, and result without credentials or private account data. Separate local lifecycle tests, signed-in Spacewar tests, and live app or hardware tests in the results.

| Capability | Design and verification |
| --- | --- |
| Stats and achievements | Use `ISteamUserStats` and Steam's automatically loaded local stats. Verify Spacewar reads first. Use an app with configured stat and achievement definitions to verify `StoreStats`, progress, unlock persistence, and the popup. [Valve](https://partner.steamgames.com/doc/api/ISteamUserStats) |
| Leaderboards | Use typed find/create, range and friends queries, score updates, and UGC attachments. Read a known Spacewar board. Verify app-owned writes and replay retrieval on a test board. Route trusted writes through the publisher backend. [Valve](https://partner.steamgames.com/doc/features/leaderboards) |
| Cloud | Provide a standalone Remote Storage file API for bounded bytes, metadata, quota, and file-change events. Verify a throwaway write/read/delete locally and then a same-account download on a second device. Auto-Cloud paths are App Admin configuration. [Valve](https://partner.steamgames.com/doc/features/cloud) |
| Timeline | Use the SDK's instantaneous and range event API. Verify a known marker and its recording query, then open the event in Steam Game Recording. The test needs recording enabled. [Valve](https://partner.steamgames.com/doc/api/ISteamTimeline) |
| Lobbies and matchmaking | Extend the existing directory with private, friends-only, and public policies, bounded metadata, discovery, and chat. Verify separate accounts, expected visibility, metadata exchange, and admission for each mode. [Valve](https://partner.steamgames.com/doc/api/ISteamMatchmaking) |
| Dedicated servers and browser | Start a native headless Steam server, publish the advertised query endpoint, find it from a separate Steam client, and verify a ticket-authenticated join. Test Steam's server list with the required AppID and login configuration. [Valve](https://partner.steamgames.com/doc/features/multiplayer/game_servers) |
| Networking Sockets, Messages, and relay | Keep reliable ordered game traffic on Sockets. Give Messages its own authenticated peer/channel policy and deterministic cleanup. Verify real bidirectional traffic and the observed relay path. Dedicated SDR needs the applicable Valve configuration. [Valve](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay) |
| Parties | Expose party beacons and reservations as explicit Steam operations. The game owns persistent group membership and match changes. Verify discovery, reservation, slot counts, and cancellation with two accounts. [Valve](https://partner.steamgames.com/doc/api/ISteamParties) |
| Game Notifications | Use the publisher-only `IGameNotificationsService` for asynchronous game sessions. A configured app and test recipients are needed to verify delivery and update or removal. [Valve](https://partner.steamgames.com/doc/features/game_notifications) |
| Voice | Use `ISteamUser` for capture and decompression and the selected game transport for bounded voice packets. Verify capture, playback, mute, and departure with microphones and two peers. [Valve](https://partner.steamgames.com/doc/features/voice) |
| Overlay | Route deliberate UI actions to Steam for invites, store, achievements, profiles, and other supported panes. Verify readiness and visibly observe the selected pane in a graphical fixture. [Valve](https://partner.steamgames.com/doc/features/overlay) |
| Rich Presence | Keep the key API in Friends and let the game choose when to write or clear status. Verify the local key round trip, then visible status and join behavior on a second account with configured display tokens. [Valve](https://partner.steamgames.com/doc/api/ISteamFriends#SetRichPresence) |
| Workshop and UGC | Provide owned async query and item-update operations. Verify a query with a known item. Use an app-owned hidden test item for publishing, subscription, install, and voting checks. [Valve](https://partner.steamgames.com/doc/api/ISteamUGC) |
| Screenshots | Verify a known rendered frame and `ScreenshotReady_t`, then use a consenting test account to exercise supported tags and location. [Valve](https://partner.steamgames.com/doc/api/ISteamScreenshots) |
| Remote Play | Use Steam's session API and Remote Play Together overlay. Verify an actual remote device or account connection, input, and disconnect. [Valve](https://partner.steamgames.com/doc/api/ISteamRemotePlay) |
| Steam Input | Load an app action manifest and provide action sets, state, and glyphs. Verify a real controller action and matching glyph from the test fixture. [Valve](https://partner.steamgames.com/doc/api/ISteamInput) |
| Deck, Proton, and keyboard | Use current Utils hardware, Proton, and text input APIs. Verify submitted or dismissed text visually and check hardware-specific results on the relevant device. Running a Windows build under Proton is a separate validation target. [Valve](https://partner.steamgames.com/doc/api/ISteamUtils) |
| SteamVR | Keep Steam platform queries in Utils and coordinate XR through Godot's OpenXR interface with SteamVR as the selected runtime. Verify runtime and session startup in a separate headset fixture. [Godot](https://docs.godotengine.org/en/4.7/tutorials/xr/setting_up_xr.html) |
| HTML Surface | Expose owned browser sessions and bounded frame data. Verify a deterministic local page through readiness, paint, input, and browser shutdown. [Valve](https://partner.steamgames.com/doc/api/ISteamHTMLSurface) |
| Music and Video | Query interface and client availability separately. Verify music controls when the Steam client supports playback and verify video with an entitled test AppID. Keep playback URLs outside logs. [Music](https://partner.steamgames.com/doc/api/ISteamMusic) · [Video](https://partner.steamgames.com/doc/api/ISteamVideo) |
| Inventory | Use Steam item definitions and result handles, preserving instance identifiers and account ownership. Verify definitions and inventory reads, then app-owned promotions, crafting, and consumption. Steam Trading handles supported user-to-user trades. [Valve](https://partner.steamgames.com/doc/api/ISteamInventory) |
| Microtransactions | Keep order creation, finalization, and idempotent fulfillment in a trusted purchasing backend. Verify with Valve's sandbox and an authorized app and test account. [Valve](https://partner.steamgames.com/doc/features/microtransactions/implementation) |
| DLC and app metadata | Use the Apps facade. Read Spacewar metadata, then test known owned and unowned DLC, install state, and a named beta branch on an authorized AppID. [Valve](https://partner.steamgames.com/doc/api/ISteamApps) |
| Identity and ownership tickets | Use public `ISteamUser` Session Tickets and the encrypted ticket API or trusted Web API verification for ownership. The installed `ISteamAppTicket` is a private DRM interface. Verify correct and rejected tickets in the appropriate server context. [Valve](https://partner.steamgames.com/doc/features/auth) |
| VAC and game bans | Surface authentication outcomes and secure server configuration. Put publisher reports and ban actions in authorized backend tooling. Verify validation and test-app outcomes without applying a live ban as part of routine verification. [Valve](https://partner.steamgames.com/doc/features/anticheat/vac_integration) · [Publisher API](https://partner.steamgames.com/doc/webapi/ICheatReportingService) |
| DRM wrapper | Run the official `drm_wrap` step for an authorized AppID and verify the wrapped executable in its test depot. [Valve](https://partner.steamgames.com/doc/features/drm) |
| Error reporting | Provide the supported warning hook and bounded diagnostics in Crimson x64. Valve documents the legacy Steam minidump upload service as Windows 32-bit only. [Valve](https://partner.steamgames.com/doc/features/error_reporting#requirements) |
| HTTP | Wrap async request and response handles with bounded bodies, timeouts, and cleanup. Verify against a controlled endpoint, including an error and a cancelled request. [Valve](https://partner.steamgames.com/doc/api/ISteamHTTP) |

## Save and economy boundaries

Crimson Save retains ownership of checkpoint validation, generations, and atomic local publication. Auto-Cloud can sync committed generation files from an app-configured per-account root. Keep writer locks and temporary files outside the rule. An explicit Cloud checkpoint integration needs a detached export and import path through Save so conflicts are surfaced before activation. Keep the standalone Cloud service useful for games that own their save format. [Save format](../src/modules/save/docs/FORMAT.md) · [Steam Cloud](https://partner.steamgames.com/doc/features/cloud)

Steam Inventory owns platform item definitions and instances. Crimson's inventory owns placement and the game owns its item records. Project verified results into a game transaction through the existing inventory adapter. Complete the vendor mutation before committing a local projection, then refresh provider state if the result is ambiguous. [Inventory model](../src/modules/inventory/MODEL.md) · [Steam Inventory](https://partner.steamgames.com/doc/features/inventory)

## Verification performed

- After the audit fixes, the 28 Spacewar fixtures passed on Windows Debug and Release, including the corrected Workshop rerun. Steam accepted and closed an empty Cloud write-batch hint on both targets. Spacewar returned an invalid-parameter response for the Workshop agreement query. The fixture checked that no agreement snapshot was returned and still verified the subscription snapshot and Workshop reads.
- The isolated native client fixture passed on Debug and Release. It simulated a Steam Offline Mode boot with a readable cache, duplicate and invalid notification launch parameters, and fatal IPC followed by fresh client initialization. The Debug SDK-off suite passed 30 cases. The trusted publisher mock suite passed on Release, and the revised publisher fixtures passed on Debug.
- The final Release dedicated authority read the admitted Spacewar guest's published `NumGames` stat and `ACH_WIN_ONE_GAME` achievement over the Steam GameServer pipe. It rejected an unknown stat and overlapping account requests, and the retained handle became unavailable after the guest left. The guest observed a direct Steam route. The Spacewar progression fixture performed reads only.

- All 28 signed Spacewar cases passed on Windows Debug and Release, including client lifetime, app and device metadata, progression reads, Cloud reads, private lobby self-chat, presence cleanup, Web API and encrypted ticket issuance, and certificate-pinned reliable and unreliable socket traffic on loopback. All 29 cases passed in each of the Debug missing-runtime and SDK-off suites.
- The Spacewar direct dedicated fixture ran an anonymous server and a signed client in separate processes on both DLL variants. It rejected an invalid proof, admitted the exact account on two fresh joins, checked the mounted world and actor on both sides, and observed Steam's `DIRECT` route. Hosted mode rejected both a missing coordinator and an unprovisioned deployment without opening admission.
- A signed Spacewar Debug run received a successful native dedicated stats load for the authenticated guest. It also exercised an unknown stat, a competing request, and the expired scope after the admission closed. On the signed client, the three direct launch argument cases passed. A valid candidate queued without joining, and duplicate or incomplete candidates were discarded. The Spacewar listen smoke exercised local Join Game publication, suppression, and restoration with relay preference enabled.
- The rendered Achievements and HTML fixtures confirmed actual overlay activation, browser painting, input, and teardown. The HTML fixture confirmed an off-origin redirect could not replace the displayed frame. In the interactive Spacewar preview, the operator unlocked Winner and saw the real Steam notification. The fixture received Steam's persistence and full-unlock callbacks, and a fresh Godot process read the achievement as already unlocked. A custom definition still needs an app-owned test.
- The HTTP fixture verified HTTPS with a trusted certificate and rejected the self-signed loopback certificate on both DLL variants. The existing offline, LAN, custom-provider, and ENet dedicated suite passed both Windows targets.
- The six trusted publisher fixtures passed against the controlled transport. Encrypted ownership also exercised unavailable paths and the actual Valve decoder with invalid ciphertext. The controlled DRM fixture verified the two wrapper modes, paths with spaces, and output collision and failure handling. Use the [testing guide](STEAM_TESTING.md) for all commands and prerequisites.

The table above identifies the additional app, account, deployment, or hardware prerequisites for live tests beyond these runs.
