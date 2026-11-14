<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Crimson public API

Use the [code-first quickstart](CODE_FIRST.md) for new projects. `Game.configure(level, player).boot(self)` attaches the game and its private services to an existing scene owner. `Game`, `Inventory`, and `Steamworks` expose the direct workflows. The detailed sections below also document legacy bindings and lower-level extension points.

The native Godot 4.7 GDExtension owns solo and multiplayer startup, coordinated travel, and checkpoints.

Existing projects can use the [Godot class-name migration guide](API_NAME_MIGRATION.md) to update their scripts, scenes, and resources to the neutral API names.

## Public API design guidelines

Crimson takes its API style from Laravel's fluent configuration, focused facades, and composable queries. These rules apply to customer-facing changes across the framework and demo.

- Start with the code a game developer should write for the common case. Resolve services at boot and pass them to their consumers. Use one domain action with sensible defaults for an ordinary read or effect. A facade handles wiring, authority, and coordination. Add fluent steps when they express choices, such as naming a save, choosing a destination, or staging several edits for one submission.
- Keep setup declarative and scoped to the game, match, or player that owns it. Inspector setup and code setup use the same configuration and validation. Keep reusable builders safe to branch.
- Make reads and effects predictable. Queries and previews read state. A fluent mutation starts at an explicit terminal such as `submit()`. Document whether success means an accepted request, a completed effect, or confirmed persistence. When a later step fails after an effect succeeds, return a typed receipt if callers need to recover from it.
- Preserve Godot autocomplete through the whole call site. Use named result and payload types for fixed shapes and bind collection elements as `Array[ItemType]`. Reserve `Dictionary` and `Variant` for caller-defined shapes. A direct `await` can require one type annotation at the call site. Retained operations can expose a typed `result()` and typed completion signal when callers need to observe or cancel work over time.
- Preserve direct module APIs and explicit extension points for fine-grained control. The facade and direct calls use the same underlying rules and validation. Make unavailable optional services, such as a platform runtime in offline play, report a documented result so callers can choose a fallback.
- Show the smallest common call site and how a game can customize it. Verify both through their normal entry points and update the affected public documentation. Mark planned signatures as proposals until the bindings exist.

For example, `steam.stats().achievements()` returns `Array[SteamAchievement]`, so `:=` preserves completion on each item in a loop. `var result: SteamLeaderboardPageResult = await steam.leaderboards().top("weekly_speedrun")` gives completion on the returned page and its entries. The [Steam progression guide](STEAMWORKS.md#progression-and-cloud) shows how direct calls and composable drafts fit together.

[Laravel application bootstrap](https://laravel.com/framework/docs/routing#routing-customization) · [Laravel service providers](https://laravel.com/docs/13.x/providers) · [Laravel query builder](https://laravel.com/docs/13.x/queries) · [Laravel fluent HTTP client](https://laravel.com/docs/13.x/http-client)

## Start a game

For local movement, look, and key rebinding, see [Default player controls](PLAYER_CONTROLS.md). Author movement on the player scene's `MoverComponent` and local input on its `ControlsComponent`. Use [Controller Style](PLAYER_CONTROLS.md#choose-how-the-body-turns) to choose how movement turns the character. `PlayerControl.lock_on(target)` sets the target for the Lock-On style, and `clear_lock_on()` clears it. Call `GameConfig.player_controls()` to use the scene's movement settings for [networked movement](MOVEMENT_NETWORKING.md). Supplying a `PlayerControlProfile` explicitly uses its movement settings. Enable **Predict Movement** on a compatible grounded capsule to opt into prediction and reconciliation.

For the current public setup and direct awaited actions, see [Start from code](CODE_FIRST.md).

### Legacy GameRuntime

Use `GameServices.game()` to get the active game when the process has one registered `GameRuntime`. A process with several games can select a primary with `GameServices.set_default_game(game)` or call `GameServices.for_node(node)` from within a game's subtree. These lookups do not extend a game's lifetime.

Use `game.codecs()` to register custom types for explicit binary messages on the live game. Use `NetworkWriter` and `NetworkReader` directly for a small fixed layout. [Typed binary message contract](../src/modules/network/README.md#typed-binary-messages)

`GameServices.scenes().get_world()` reads the primary game's current mount. In a coordinated game, `await GameServices.scenes().load(world_id).wait()` travels through the same authority checks and Scene Loader as `game.travel_to(world_id)`. A scene service handle expires when its owning game scope leaves the tree.

Game setup supplies native Session and Scene Loader services by default. To customize either service, subclass `GameServiceProvider` and override `_create_session()` or `_create_scene_loader()` to return a fresh detached node. Set **Service Provider** on the definition or call `definition.with_provider(provider)` before attaching the game. The game injects the returned nodes into its normal lifecycle and owns them after attachment.

Use the Session factory to select a custom gameplay transport. The adapter creates the peers. Crimson still owns admission, readiness, and teardown.

```gdscript
class_name MyGameProvider
extends GameServiceProvider

func _create_session() -> GameSession:
    var session := GameSession.new()
    if session.set_transport_adapter(MyTransport.new()) != OK:
        session.free()
        return null
    return session
```

`MyTransport` extends `SessionTransport`. Its server and client factories return a typed `SessionTransportResult` carrying either an initialized peer or the opening error. The default is `ENetSessionTransport`. See the [Session transport contract](../src/modules/session/README.md) and the [demo's scripted ENet adapter](../project/demo/tests/session_smoke.gd) for a provider that exercises real host and join through the public game actions.

An existing scene with a serialized `GameRuntime` can still load its **Definition**, starting world, player scene, and multiplayer rules. Legacy `PlayerSpawnPoint` placements also continue to resolve. The [current demo](../project/demo/main.gd) boots through `Game` and uses ordinary `Marker3D` placements.

The same configuration in code uses a branchable definition. `boot(owner)` creates the configured node, attaches it to a live owner, and returns it. `create()` is available for callers who need to attach the node themselves. Both use an independent definition and the shared `Game` node name.

```gdscript
var game: GameRuntime

func _ready() -> void:
    game = (
        GameRuntime.configure()
        .with_starting_world(&"transfer_hall", preload("res://demo/hub/starting_hub.tscn"))
        .with_player(preload("res://demo/player/stationary_player.tscn"))
        .with_multiplayer(
            MultiplayerRules.listen_coop()
            .with_max_players(4)
            .allow_join_in_progress()
            .on_port(24830)
        )
        .boot(self)
    )
```

| Game action | Contract |
| --- | --- |
| `await game.play_solo().wait()` | Mount the starting world and spawn the local player offline. |
| `await game.host_game(bind_address).wait()` | Mount the world and spawn the listen host, then open admission. The bind address defaults to `*`. |
| `await game.start_dedicated(bind_address).wait()` | Mount the authoritative world and open admission with every seat available to guests. The bind address defaults to `*`. |
| `await game.join_game(address).wait()` | Connect at the configured port, mount the authority's active world, and apply the acknowledged player roster and local spawn. |
| `game.leave()` | Cancel a pending start or retire the mounted session. |

Each start returns a `GameOperation`. Await `wait()` because Scene Load prepares and mounts the world asynchronously. Inspect `result.ok`, `result.code`, and `result.message`. A successful start reaches `RUNNING`. Read the world with `get_world()`, the local actor with `get_local_player()`, or any actor by connection with `get_player(peer_id)`. A dedicated server has no local actor. Observe `session_ended(reason)` after teardown to return to the menu.

### Multiplayer rules

Use `MultiplayerRules.listen_coop()` for drop-in play. It defaults to four total players and UDP port `7000`. `max_players` includes the listen host, so four players permit three guests. Dedicated hosting makes all four seats remote. Listen starts support 2 through 64 players. Dedicated starts support 1 through 64. An empty rules resource reference gives the definition solo play. Drop-in hosting and joining require join in progress to be enabled. Use the coordinated lobby flow for a game whose admission closes before the first match starts.

Peers use the same `GameRuntime` node path relative to the multiplayer root, starting world ID, packaged scene layout, and player cap. The authority owns admission, the world epoch, and spawn assignment. Each late guest mounts the current world through its own Scene Loader. The host and already joined guests retain their active world. These actions cover a single shared 3D world. Use [coordinated game setup](#start-a-coordinated-game) for registered destinations, travel, checkpoints, and game-owned replicated state.

### Spawn and player components

Place `PlayerSpawnPoint` nodes inside the world, anywhere beneath spatial parents. Each supplies an authored position and facing direction. Direct `Marker3D` children can supply additional placements from one spawn component. Author clear, distinct poses for the expected number of simultaneous players. Crimson automatically chooses an unassigned placement and keeps it assigned until that actor departs. If every placement is assigned, the entering player receives a placement failure and the current session continues.

Add an optional `LocalPlayerView` inside the player scene. Use the Inspector there to assign its **Camera** and **Hidden Visuals** references from the same scene. The framework activates the local camera and hides the selected visuals on the local actor. It can select a sole camera automatically when a local-view component is absent.

## Use Steam

The Steam provider is part of the Crimson extension. Build with Steamworks and stage its Windows runtime. Use the fixed-world game definition from [Start a game](#start-a-game) with the same scenes and rules on all peers. [Steamworks SDK setup](../vendor/README.md)

To use Steam app services independently of multiplayer, call `SteamClient.boot(self)` from a persistent node's `_ready()`. The client shares the process runtime with the Steam game provider. Its typed services provide direct actions such as `await steam.cloud().read("profile.dat")` and `await steam.http().fetch(version_url)`. [Standalone Steam API and CLI verification](STEAMWORKS.md#start-the-client)

Call `game.leave()` to release the online session and its listing or membership. The local dedicated launcher uses ENet. [Live Steam verification and account requirements](../vendor/README.md#live-provider-verification)

### Listen hosting

Choose the AppID, shared lobby namespace, and visibility before attaching the game. The signed-in Steam client supplies the launch AppID when no explicit AppID is set. `SteamProvider.configure()` defaults to private invitations and virtual port `0`.

```gdscript
var provider := SteamProvider.configure().for_app(480).with_lobby_namespace("my-studio-crimson-demo").friends_only()
var game := definition.with_provider(provider).boot(self)
var steam := SteamServices.for_game(game)
if not steam.is_ready():
    push_error(steam.get_initialization_error())
    return

var hosted: Dictionary = await game.host_online().wait()
if hosted.ok:
    game.invites().open_overlay()
```

`game.invites().open_overlay()` returns `ERR_BUSY` while Steam loads the overlay. `OK` means Steam received the request. For a direct windowed development launch, pass `--crimson-steam-overlay=<AppID>` after Godot's `--` separator. Crimson initializes Steam before Godot opens its display and the matching provider reuses that process runtime. [Steam overlay readiness](https://partner.steamgames.com/doc/features/overlay#activating-the-overlay-from-your-app)

Send an invitation with `game.invites().send(selected_steam_id)` after hosting succeeds. Pass the selected recipient's decimal SteamID64 as a `String`. Crimson checks that the current game owns the lobby. `OK` means Steam accepted the submission. The guest accepts through Steam. [Steam lobby invitations](https://partner.steamgames.com/doc/api/ISteamMatchmaking#InviteUserToLobby)

On the guest, connect `game.invites().received` from the persistent menu. Read `game.invites().pending()` to catch a Steam launch invitation. Enable **Use launch command line** in Steamworks to pass Steam URL and Rich Presence launch arguments through the SDK. Crimson supports SDK launch arguments at startup and while running, along with normal Steam invitation callbacks and direct development launch arguments. When the player confirms, call `await game.join_listing(invite.listing).wait()`. The provider resolves the current lobby and checks its AppID, namespace, membership and owner before the game completes its authenticated join. You can pass a known ID to `await game.join_online(listing_id).wait()`. [Steam launch arguments](https://partner.steamgames.com/doc/api/ISteamApps#GetLaunchCommandLine) · [Steam lobby API](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

The guest issues a Steam session ticket for the certified listen host. The host checks the authenticated connection, current lobby membership, and Steam's AppID ownership result before admitting the guest. Both players need a build using the same Steam game lobby protocol. Ready friends-only and public hosts publish an owned Steam Rich Presence join route while seats are open. Call `steam.presence().clear_joinable()` to suppress the current listing's route while keeping its display status. Use `steam.presence().joinable(lobby.listing())` to restore the route for the current owned listing. Accepted Steam joins pass through the same typed invite gateway and player confirmation. The route is available to friends-only and public games. `SteamProvider.prefer_relay()` returns a branch that biases listen route selection toward Steam relay while preserving a viable direct route. [Steam game authentication](https://partner.steamgames.com/doc/features/auth) · [Steam Rich Presence joins](https://partner.steamgames.com/doc/api/ISteamFriends#GameRichPresenceJoinRequested_t)

For discoverable play, use `public_lobbies()` or `invisible_lobby()` on the provider. Private lobbies use invitations, and friends-only lobbies use invitations or `steam.lobbies().friend_lobby(friend_id)`. Use the same AppID and namespace on each peer.

```gdscript
var discovered: SteamListingsResult = await steam.lobbies().query() \
    .on_map(&"transfer_hall").with_room_for(2).fetch()
if discovered.ok and not discovered.value.is_empty():
    var joined: Dictionary = await game.join_listing(discovered.value[0]).wait()
```

Query branches allow one to 50 results and one to 64 required open seats. Use `fetch_operation()` to retain a cancellable handle. Steam handles one search at a time. A cancelled observation releases its caller, and the native query lane drains the outstanding Steam result. `SteamServices.for_game(game)` resolves the exact attachment even when startup failed. Retained invite and lobby services expire when their game leaves the scene tree. The legacy provider invite methods require a unique attached game when called on the provider Resource. [Steam lobby search and visibility](https://partner.steamgames.com/doc/api/ISteamMatchmaking#RequestLobbyList)

The Steam scope also supplies `apps()`, `friends()`, `overlay()`, `utils()`, `diagnostics()`, `http()`, `stats()`, `leaderboards()`, `cloud()`, and `timeline()` from the game's own Steam client scope. Resolve Steam once and pass the typed service to its consumer, for example `profile_service.bind(steam.cloud())`. A game and a standalone `SteamClient` can share the process Steam runtime with independent service lifetimes. Reattaching the same game creates fresh handles and leaves the old ones inactive.

### Standalone social lobbies

```gdscript
var steam := SteamClient.boot(self)
if steam == null or not steam.is_ready():
    return
var created: SteamLobbyResult = await steam.lobbies().create().wait()
if created.ok:
    created.value.edit().named("Crew staging").tag(&"mode", "practice").submit()
```

The default is a private four-seat lobby, including its creator, under the `crimson` namespace. For a specific mode, pass `SteamLobbyOptions.configure().in_namespace(&"my_game").for_players(8).with_visibility(SteamLobbyOptions.PUBLIC)` to `create()`. Search with `await steam.lobbies().query(&"my_game").social().fetch()` and join with `await steam.lobbies().join(listing).wait()`. The provider verifies fresh Steam membership and social metadata before reporting success. The default query searches gameplay listings, and social listings cannot join a `GameRuntime`. [Steam lobby discovery and membership](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

One local account can acquire one membership across standalone clients and games. Other acquisitions return busy while that membership is held or a cancelled native request drains. `SteamLobbyOperation.cancel()` completes its caller with `ERR_SKIP`. The 30-second observation deadline returns `ERR_TIMEOUT`. An SDK call that later succeeds is retired before the account can enter another lobby.

Use `lobby.members()` for a roster of Steam identities, `members_changed` for Steam membership events, and `lobby.closed` for retirement. The live Steam owner can stage up to 16 display or application metadata changes with `edit().named().tag().submit()`. Each write is individually acknowledged by Steam, and the `crimson_` prefix is reserved. A standalone owner can change `set_joinable(open)`, and a standalone member can `leave()`. Game-owned membership follows `game.leave()` and game admission rules.

`lobby.send_chat(text)` accepts up to 1024 UTF-8 bytes without ASCII controls, at most once every 250 milliseconds. `chat_received(member, text)` carries plain text from the matching Steam lobby and current member. Crimson bounds each lobby's delivery to 20 messages per second. Render chat as text and treat it as social input. Game authority continues to use verified transport admission. `lobby.invite(steam_id)` submits one explicit owner invitation, `open_invite_dialog()` requests the overlay picker, and `steam.lobbies().invite_received` observes accepted invitations on standalone scopes without initiating a join. [Steam lobby chat and invitation callbacks](https://partner.steamgames.com/doc/api/ISteamMatchmaking#LobbyChatMsg_t)

### Dedicated Steam games

```gdscript
var settings := SteamServer.configure("Transfer Hall", 27015, 27016) \
    .with_product_metadata("1.2.0.0", "Transfer Hall", "TransferHall")
var provider := SteamProvider.configure().for_app(app_id).dedicated(settings)
var game := definition.with_provider(provider).boot(self)
var started: Dictionary = await game.start_dedicated_online().wait()
if started.ok:
    SteamServices.server_for(game).status_changed.connect(on_server_status)
```

The dedicated process uses a separate SteamGameServer pipe. It needs the Steam launch AppID and packaged SDK. An anonymous server can run without an interactive Steam account. To use an operator login, set `with_login(&"MY_STEAM_SERVER_TOKEN")` and supply that variable to the dedicated process. Keep gameplay and query UDP ports distinct. `bind_to(address)` selects a local IPv4 interface. Pass a literal reachable IPv4 address to `start_dedicated_online(public_address)` when the route should differ from the bound or Steam-reported address. [Steam GameServer lifecycle](https://partner.steamgames.com/doc/api/ISteamGameServer)

`with_product_metadata(version, description, install_dir)` returns an independent branch for the app's master-server version, game description, and Steam install folder name. Supply app-specific values for public deployment. `SteamServerStatus.advertisement_requested` is the local request and shares its value with `listed`. Confirm that a separate client observes the server in the Steam browser before treating it as discoverable. [Valve server version](https://partner.steamgames.com/doc/api/steam_gameserver#SteamGameServer_Init) · [Valve server metadata](https://partner.steamgames.com/doc/api/ISteamGameServer#SetModDir)

The default `SteamServer.DIRECT` mode publishes a locator through `game.get_online_listing()` and leaves Steam browser advertisement disabled. `advertise_on(SteamServer.INTERNET)` requests Steam discovery after the playerless world, listener, logged-in server, secure policy, and certificate are ready. The current SDK exposes one joint Internet and LAN advertisement switch, so a secure `SteamServer.LAN` host request returns unavailable. The LAN browser source can read compatible announcements. [Steam dedicated server integration](https://partner.steamgames.com/doc/features/multiplayer/game_servers)

Guests can join an exact direct locator with `await game.join_online(listing_id).wait()`. The route pins the server's certified Steam identity and the matching project's AppID and namespace. The guest issues a ticket for that server. The authority binds Steam's ticket validation to the actual certified connection before Session admission, and revokes the exact peer on an invalid follow-up. `SteamServices.server_for(game).status()` reports whether the world is ready, whether it requested advertisement, the native secure state, and the number of admitted remote players. [Steam ticket authentication](https://partner.steamgames.com/doc/features/auth) · [Steam Networking Sockets authentication](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets#InitAuthentication)

```gdscript
var servers: SteamListingsResult = await steam.server_browser().query() \
    .on_map(&"transfer_hall").with_room_for(1).fetch()
if servers.ok and not servers.value.is_empty():
    var refreshed: SteamServerDetailsResult = await steam.server_browser().inspect(servers.value[0]).wait()
    if refreshed.ok:
        await game.join_listing(refreshed.value.listing).wait()
```

Steam browser rows are discovery observations. The join performs its own certificate, ticket and world readiness checks. Use `fetch_operation()` to cancel an in-flight search. `query(SteamServer.LAN)` selects the LAN source. A standalone client can set a custom namespace as the second query argument. Game scopes use their provider's namespace. [Steam server browser requests](https://partner.steamgames.com/doc/api/ISteamMatchmakingServers)

For published Steam stats configured as **Game Server** or **Official GS**, call `SteamServices.server_for(game).stats(peer_id)` on the dedicated authority. This returns a `SteamServerPlayerStats` for the exact admitted connection, or `null` when that peer is ineligible. Load the cache with `await stats.load().wait()` for reads. A draft loads it when needed and waits for Steam's store callback.

```gdscript
var player_stats := SteamServices.server_for(game).stats(peer_id)
if player_stats != null:
    var stored: SteamServerStatsResult = await player_stats.edit() \
        .set_int(&"official_matches", authoritative_match_count) \
        .unlock(&"official_first_win").submit().wait()
```

Check `stored.ok`, `stored.applied`, `stored.failed_at`, and `stored.store_submitted` before choosing a retry. An accepted setter can remain in the cache after a later setter or store failure. `store()` retries persistence without replaying those setters. The handle expires when the player leaves or the dedicated runtime retires. [Steam GameServer stats](https://partner.steamgames.com/doc/api/ISteamGameServerStats)

#### Hosted Steam Datagram Relay

```gdscript
var settings := SteamServer.configure("Transfer Hall", provisioned_game_port, query_port).via_sdr(&"eu-west")
var provider := SteamProvider.configure().for_app(app_id).with_relay_coordinator(coordinator).dedicated(settings)
var game := definition.with_provider(provider).boot(self)
var started: Dictionary = await game.start_dedicated_online().wait()
```

The profile requires a Valve-provisioned hosted endpoint and a trusted `SteamRelayCoordinator`. Override `_register_server(request)` to copy the Valve-signed server login on the scene thread and send it to that backend. Verify its AppID, namespace, profile, virtual port and publication nonce against the request. Call `request.registered()` after the exact locator is registered. Override `_withdraw_server(listing_id)` to retire that same generation idempotently. The game opens admission after registration settles and stays on its provisioned hosted socket. Keep the hosted server's Valve certificate credentials in its provisioned runtime and the separate relay ticket signing key in the trusted coordinator. [Valve hosted dedicated server APIs](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets#CreateHostedDedicatedServerListenSocket)

The client uses its own coordinator adapter on `SteamProvider` and calls `await game.join_online(listing_id).wait()` with the opaque hosted locator. The backend independently authenticates the client in `_authorize_client(request)` and signs a fresh Steam relay ticket for that exact authorized client, server, AppID, virtual port, and registration. Sign the request's decimal registration nonce into the `crimson_registration` fixed64 extra field. Pass the signed ticket to `request.authorize(ticket)`. Valve's SDK caches the supplied ticket before returning its parsed fields. Crimson checks those fields, the expiry against Steam time, and the exact cache selection before granting a single native connection attempt. The game's separate Steam authentication ticket still gates world admission. Keep server routing blobs and signing keys in the trusted backend. [Valve relay ticket flow](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay#ticket_based_server_connection_flow)

## Use a custom online provider

Add a `GameServiceProvider` before attaching the game. Return a detached session with a `SessionAuthenticator` from `_create_session()`, and an independent `OnlineDirectory` from `_create_online_directory()`. The provider can use the default ENet transport or install its own adapter on that session. The [demo online fixture](../project/demo/tests/online_smoke.gd) shows a directory and verifier implemented in the game.

```gdscript
var hosted: Dictionary = await game.host_online("127.0.0.1", "127.0.0.1").wait()
if hosted.ok:
    print(hosted.listing_id)

# In the guest process with the same game provider.
var joined: Dictionary = await game.join_online(listing_id).wait()
```

The first host argument is the address advertised to guests. Pass an empty address for a provider that supplies its own route. The second argument is the local bind address and defaults to `*`. `start_dedicated_online(public_address, bind_address)` uses the same directory with all seats available to remote players.

- The directory's `_publish(endpoint, world_id, request)` reports a unique listing ID through `request.publish(id)`. The authority opens admission after the listing succeeds. `_unpublish(id)` retires the listing when the game leaves.
- The directory's `_resolve(id, request)` completes with `request.resolve(endpoint)`. Both hooks can complete later on the scene thread and fail through `request.fail()`. Requests expire after 15 seconds. A directory should release its resources when `request.is_cancelled()` becomes true.
- The session authenticator answers and verifies challenges for the pending connection. The host receives the verified provider and subject through `GameSession.get_peer_identity(peer_id)` by the time `peer_joined` fires. The guest reaches game readiness after the normal Scene Loader and roster acknowledgment.

Use a transport that protects credentials in transit whenever a verifier exchanges sensitive proofs. The demo fixture exercises the public contracts with test proofs on local loopback. [Session authentication and transport](../src/modules/session/README.md)

## Start a world session

Existing standalone `WorldSession` integrations can continue to assign their world and player scenes in the **Definition** resource and call `start_offline()`, `host()`, or `join(address)`. This entry point uses a `SpawnPoints` container with direct `Marker3D` children in slot order.

The same setup in code uses a branchable resource. `create()` returns a detached node named `WorldSession` with a copy of the definition. Add it to a persistent parent before starting play. Configuration takes effect through the same startup checks as an Inspector resource.

```gdscript
var session: WorldSession

func _ready() -> void:
    session = (
        WorldSession.configure()
        .with_world(preload("res://levels/hub.tscn"))
        .with_players(preload("res://actors/player.tscn"))
        .create()
    )
    add_child(session)
```

Before `create()`, add `.with_local_view(^"Camera3D", ^"Visuals")` to select the local camera and hide its body mesh, or `.on_port(24830)` to set a game-specific port.

| Definition property | Contract |
| --- | --- |
| `world_scene`, `player_scene` | Locally available scenes rooted at `Node3D`. Each participant uses matching assets and spawn order. |
| `spawn_points_path` | World-relative path to the spawn container. Defaults to `SpawnPoints`. The container and its ancestors inside the world are `Node3D` nodes. Its direct markers determine the capacity, including the authority's first slot. Use 1–64 markers for offline play and 2–64 for multiplayer. |
| `local_camera_path`, `local_hidden_path` | Optional player-relative paths to a `Camera3D` and a `Node3D`. The session activates the camera and hides the selected node on the local avatar. Set these with `with_local_view()`. |
| `port` | The UDP port used for host and join. Defaults to `7000`. Set it with `on_port()`. |

| World session method | Contract |
| --- | --- |
| `start_offline() -> Error` | Mount the world, place the local player, and enter `RUNNING` on success. |
| `host() -> Error` | Start the listen host and enter the world. The remaining spawn slots stay open for admitted guests. |
| `join(address) -> Error` | Begin direct-address connection. `OK` means the request started. Wait for `RUNNING` before using the local player. |
| `leave() -> Error` | Cancel a pending join or leave an active world session. |
| `get_state()`, `get_port()` | Read `IDLE`, `STARTING`, or `RUNNING` and the configured UDP port. |
| `get_world()`, `get_local_player()`, `get_player(peer_id)` | Access mounted scene nodes for the active session. The session owns their lifetime. |

Observe `state_changed(state)` for readiness. `session_ended(reason)` arrives after teardown and permits the next start. Startup methods return immediate errors directly. Asynchronous end reasons include the [native session reasons](../src/modules/session/README.md#signals) and `spawn_timeout`, `invalid_roster`, or `spawn_failed`.

Each participant keeps the same session node path relative to its MultiplayerAPI root. The authority assigns slots and publishes the roster to admitted peers. The session reserves a child named `World` under itself and a child named `Players` under the mounted world. Use `GameConfig.player_controls(profile)` on a fixed-world game to opt into the native movement and look stream. Use the coordinated game flow below for registered destinations, travel, and checkpoints. [Networked player movement](MOVEMENT_NETWORKING.md)

## Start a coordinated game

For new coordinated games use [`Game.campaign()`](CODE_FIRST.md#coordinated-worlds-and-inventory). Existing projects can use their serialized `GameRuntime` or create one from a legacy definition in code. Keep HUD and persistent UI outside the mounted world. Host and guest require matching RPC paths and all catalog scenes in their exported builds.

This example assumes the main script has an exported reference to its `GameRuntime` node. The setup chain returns a `GameDefinition` with the same fields available in the Inspector. Run this setup once for a new node, then start and restore matches through that node.

```gdscript
@export var game: GameRuntime

func start_campaign() -> void:
    game.definition = (
        GameRuntime.configure()
        .with_worlds({
            "harbor": {"scene_id": "harbor_map", "path": "res://levels/harbor.tscn"},
            "foundry": {"scene_id": "foundry_map", "path": "res://levels/foundry.tscn"},
        })
        .starting_in(&"harbor")
        .with_saves("my_game.campaign")
    )

    var error := register_gameplay()
    if error == OK:
        error = register_authority_state()
    if error != OK:
        push_error("Crimson setup failed: %s" % error_string(error))
        return

    var result: Dictionary = await game.start_offline().wait()
    if not result.ok:
        push_error(result.message)
```

Use a stable, game-owned world ID as the catalog key. Multiple worlds may refer to the same scene ID and resource path. The definition copies its catalog on assignment and retrieval. Assign the full dictionary after editing a detached copy. Each setup modifier returns a new definition, so a project can branch from a shared catalog. `with_saves(app_id, root = "user://crimson", schema_version = 1)` sets the checkpoint fields. The first valid configuration takes effect when opening a session or reading save history.

| `GameDefinition` property | Contract |
| --- | --- |
| `app_id: String` | Stable checkpoint application identifier. The default is `"crimson.game"`. Set a distinct ID for your project. |
| `save_root: String` | Checkpoint root under `user://`. The default is `"user://crimson"`. |
| `schema_version: int` | Positive integer version of the integrated match checkpoint. The default is `1`. |
| `worlds: Dictionary` | Map from world IDs to dictionaries with `scene_id` and `path`. IDs are case-sensitive, 1–64 ASCII characters, start with a letter or digit, and can also contain periods, hyphens, and underscores. A scene ID consistently resolves to one canonical `res://` path ending in `.tscn` or `.scn`. |
| `starting_world: StringName` | A registered world to start when the caller omits a destination. |

## Game operations

`GameOperation` exposes `is_finished()`, a read-only `result` property, and `completed(result)`. In GDScript, `await operation.wait()` returns the stored result for an operation that has already finished or awaits the result of pending work. Inspect `result.ok`, `result.code`, and any operation-specific fields. The coordinated `host()` and `join()` methods return a Godot `Error` immediately. Use `phase_changed` and `session_ended` to observe asynchronous join and disconnect.

Coordinated world transitions and checkpoint saves and restores return a readable `message` with the operation code. Use the result for gameplay decisions. Diagnostic lines for failures and noteworthy outcomes appear in Godot's Output panel and the process console with the `[Crimson]` prefix.

| `GameRuntime` method | Contract |
| --- | --- |
| `start_offline()` | Open the local session, bind the local player, and start the configured world. Returns an operation. |
| `open_offline() -> Error` | Open a local authority lobby. Use it before `restore()` to continue directly from a title screen. |
| `host(port, max_clients = 32, bind_address = "*") -> Error` | Open an ENet lobby. Start the match after the game has admitted and bound the intended participants. |
| `join(address, port, timeout_seconds = 10.0) -> Error` | Begin connection to an ENet lobby. `OK` means the connection attempt started. |
| `bind_player(peer_id, player_id) -> Error` | From the authority lobby, assign an admitted connection a stable game-owned player ID. The game supplies that identity from trusted code. |
| `start_match(world = &"")` | Start a world from the authority lobby. Uses `starting_world` when omitted. Returns an operation. |
| `travel_to(world)` | Activate a registered destination while keeping the live match records. Returns an operation. |
| `save_slot(profile, slot) -> SaveSlot` | Address one slot for fluent save options, history, and restore. |
| `save(profile, slot, metadata = {}, autosave = false)` | Capture the committed state of a running authority and write a generation. Use `autosave = true` for a checkpoint slot. Returns an operation. |
| `save_history(profile, slot, before_sequence = "", before_id = "", limit = 16)` | Inspect a page of generations in a known slot on this instance's storage. Can be used from a title screen. Returns an operation. |
| `restore(profile, slot, sequence = "", generation_id = "")` | Resolve the destination from the checkpoint and activate the saved scene and stores. Omit both generation arguments for the current head. Returns an operation. |
| `leave() -> Error` | Begin releasing the transport, match, and mounted world. The game emits `session_ended(reason)` and returns to `IDLE` after cleanup, when it can be reused. |

Read readiness with `get_phase()` and the `IDLE`, `LOBBY`, `TRANSITIONING`, `RUNNING`, and `RECOVERING` constants. Use `world_ready(world_id, world_epoch)` when scene code needs a playable world. `get_world_id()` and `get_world_epoch()` report the last published world. `get_world()` returns the mounted root, which is valid only until replacement or teardown.

### Save, travel, and restore

This example saves the current Harbor match, travels to Foundry, and restores the exact Harbor generation returned by the save. A menu can call the same methods independently.

```gdscript
func checkpoint_round_trip(screenshot: Image) -> Dictionary:
    var quick := game.save_slot("campaign", "quick")
    var request := (
        quick.save()
        .named("Harbor beacon")
        .with_context({"region": "Harbor"})
        .with_preview(screenshot)
    )
    var saved: Dictionary = await request.submit().wait()
    if not saved.ok:
        return saved

    var travel: Dictionary = await game.travel_to(&"foundry").wait()
    if not travel.ok:
        return travel

    var generation: Dictionary = saved.value
    return await quick.restore(generation.sequence, generation.id).wait()
```

`SaveSlot.save()` creates a `SaveRequest`. `named(label)`, `with_context(fields)`, `with_preview(image)`, and `as_autosave()` each return a new request. `submit()` timestamps and schedules the save. For defaults, call `quick.save().submit()`. Use `quick.restore_latest()` to load the slot head and `quick.history(before_sequence = "", before_id = "", limit = 16)` to inspect generations. These calls use the same implementation as the direct methods on `GameRuntime`.

A successful save has `value = {sequence, id}` once storage publishes the generation. Save history returns newest-first rows in `value`, a slot `status`, and a `next` cursor when more rows exist. Use the selected row's exact `sequence` and `id` for a load-menu action. `SavePresentation.describe(entry, pattern, utc_offset_minutes)` supplies the label, time, context, and optional preview for display. The game supplies slot IDs, keybindings, labels, screenshots, and checkpoint triggers. Slot history covers a known slot. See [save result shapes and limits](../src/modules/save/docs/API.md) and [SavePresentation](../src/modules/save/docs/PRESENTATION.md).

The integrated checkpoint captures all registered match stores and the durable world ID. Register the same store set and validators before restoring. The game decides what goes into those stores. To restore a player position or inventory, persist the corresponding records with stable IDs and bind the mounted scene nodes to those records. Store plain DTOs and encode engine values such as transforms and vectors as data. Scene nodes, connection IDs, input queues, and live Godot objects have runtime lifetimes.

## Authority gameplay and visibility

Before the match starts, use `register_command(name, handler, requires_primary = false)` on the authority. Each client uses `declare_command(name, requires_primary = false)` with the same name and primary-character requirement. Only the authority runs the handler. Call `submit(name, payload)` from local input on any ready participant. Crimson supplies the trusted player identity at the handler boundary, and validates the match, world epoch, command sequence, and primary-character assignment where required. The handler validates game-specific permissions and performs any state changes explicitly.

The sample below gives an admitted player permission to light the beacon in the active world. Call `register_gameplay()` and `register_authority_state()` on the offline or network authority before the first world. Each client calls `game.declare_command("world.light_beacon")`. Re-register match stores when starting a new match after teardown. The command registration belongs to the `GameRuntime` instance and is reused across matches.

```gdscript
func register_gameplay() -> Error:
    return game.register_command("world.light_beacon", _light_beacon)

func register_authority_state() -> Error:
    var stores := game.get_transactions()
    var error := stores.register_store(
        "beacons", {"harbor": false, "foundry": false},
        _prepare_beacons, _validate_beacon_restore,
    )
    if error != OK:
        return error
    return game.set_shared_stores(PackedStringArray(["beacons"]))

func light_current_beacon() -> Dictionary:
    return await game.submit("world.light_beacon").wait()

func _light_beacon(player_id: String, _intent: Dictionary) -> Dictionary:
    var stores := game.get_transactions()
    var edit := stores.begin({"player_id": player_id})
    var error := edit.add_change(
        "beacons",
        {"world_id": String(game.get_world_id())},
        stores.get_store_revision("beacons"),
    )
    if error != OK:
        return {"ok": false, "code": "InvalidBeaconChange"}
    return stores.commit(edit)

func _prepare_beacons(current: Dictionary, operations: Array, _context: Dictionary) -> Dictionary:
    for operation: Dictionary in operations:
        var world_id: String = operation.get("world_id", "")
        if not current.has(world_id):
            return {"ok": false, "message": "The world has no beacon."}
        current[world_id] = true
    return {"ok": true, "state": current}

func _validate_beacon_restore(candidate: Dictionary, _context: Dictionary) -> Dictionary:
    var valid := (
        candidate.size() == 2
        and candidate.get("harbor") is bool
        and candidate.get("foundry") is bool
    )
    return {"ok": valid, "message": "Invalid beacon checkpoint." if not valid else ""}
```

The public coordinator can stage multiple changes from game-owned and framework stores in one commit. For example, a pickup handler can call `InventoryTransactionAdapter.queue_place()` on the same transaction that consumes a world pickup. Store preparation and restore validation are synchronous and work on detached data. See [the transaction contract](ARCHITECTURE.md#transaction-contract).

For multiplayer, the server can bind a remote peer after verifying its game-specific identity. Connect to `peer_joined(peer_id)` to begin that process and call `bind_player(peer_id, verified_player_id)` on the authority when the identity is known. A listen host using local commands also binds its own peer, ID `1`. Read the local binding and current primary with `get_local_player_id()` and `get_local_character_id()`. These return an empty string when the relevant identity is unavailable. Client code can listen to `projection_updated(revision, baseline)` and read the detached visible state with `get_projection()`. It can observe the authority's result of a command separately from presentation of the resulting projection.

- `set_shared_stores(store_ids)` publishes the complete contents of explicitly selected stores to every bound client. Use it only for state all participants may see. By default, the game contributes no shared store data.
- `set_projector(callback)` gives the authority finer visibility control. The synchronous callback receives `(peer_id, player_id, world_epoch, committed_revision)` and returns `{ok, view}`, with `view` a dictionary of visible records keyed by stable strings. This projector takes precedence over the shared-store list. Crimson reserves `crimson.context` for its internal command context and removes it from the public view.
- Replication accepts bounded plain DTOs and is intended for committed gameplay state. Its public [view and delivery contract](../src/modules/network/README.md#view-and-delivery-bounds) gives size and depth limits. Coordinated games supply their own high-frequency movement and transient presentation streams. The fixed-world player profile uses the dedicated [movement stream](MOVEMENT_NETWORKING.md).
- Integrated ENet admission happens in the lobby. Starting a world requires all connected remote peers to be bound. A world becomes ready after the scene and required peer baselines are confirmed. A failure before publication keeps the prior world available. A synchronization failure after the commit enters `RECOVERING` and closes commands. The current facade supports leaving and starting a new session from that state.

## Direct APIs and extension points

A project may use the modules directly or start with the game facade and use its public authority handles where it needs control.

| Need | API |
| --- | --- |
| Grid geometry, placement, stacks, equipment, containers, or trade | [Inventory API](../src/modules/inventory/README.md), including `SpatialInventory` and its [grid API](../src/modules/inventory/MODEL.md) |
| Custom checkpoint participants, migrations, backend, and save presentation | [SaveCoordinator API](../src/modules/save/docs/API.md) and [SavePresentation](../src/modules/save/docs/PRESENTATION.md) |
| Custom transport orchestration or a standalone managed scene | [Game Session](../src/modules/session/README.md) and [Scene Manager](../src/modules/scene_manager/README.md) |
| Custom replicated views or lower-level synchronization | [StateReplication](../src/modules/network/README.md) |
| Authority-owned state, custom transactions, and primary possession | `game.get_transactions()` and `game.get_possession()` when configuring or running the authority. Reacquire the handles for each new match. See [architecture](ARCHITECTURE.md#module-map-and-apis). |

The public facade owns its composition. Its service children and RPC names are implementation details. The present integration points are the game definition, command handlers, game-owned stores and validators, trusted player binding, and projection policy. Direct module use exposes the lower-level capture and scene activation boundaries when a game needs to supply its own orchestration.

## Fluent API direction

Game definitions and save requests use fluent modifiers today. The examples below sketch future Godot bindings for inventory queries, inventory commands, and travel options.

```gdscript
var matches := inventory.filter(can_use).search("medkit").collect()
var selected := matches.first()
var move := inventory.move(selected).to(&"belt").first_fit().submit()

var travel := game.match.travel_to(&"foundry").at_spawn(&"north_gate").submit()
```

The `collect()` terminal would read one snapshot, and collection operations would refine that already-read result. A mutation would start at an explicit terminal such as `submit()`. Its options carry IDs and preconditions to the same authority and transaction path as direct calls. A GDScript predicate runs on data the caller can already read.

Future scope-specific handles and feature providers can make common workflows shorter while keeping wiring explicit. A feature should receive its declared dependencies when its game, session, match, or local-player scope starts. A custom provider could replace a documented contract at setup. The current facade has concrete services and the extension points above. Game-specific systems can already use native Godot nodes, scoped callbacks, and custom commands without a feature provider.
