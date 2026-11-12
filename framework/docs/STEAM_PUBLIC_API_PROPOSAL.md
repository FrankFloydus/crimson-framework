<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Steam public API implementation contract

**Status** · Approved · 12 November 2026

**Implementation** · In progress

This contract defines the target Steam API and its verification requirements. The [Steamworks guide](STEAMWORKS.md) describes the available bindings. Existing game actions are identified where the contract builds on them. FPS examples use hypothetical additions to Transfer Hall, the demo hub.

- [Boot, scope, and dependency injection](#boot-scope-and-dependency-injection)
- [Public API conventions](#public-api-conventions)
- [Godot typing and code completion](#godot-typing-and-code-completion)
- [1. Player progression and Cloud](#1-player-progression-and-cloud)
- [2. Multiplayer](#2-multiplayer)
- [3. Social, Overlay, and Community](#3-social-overlay-and-community)
- [4. Hardware, Input, and Media](#4-hardware-input-and-media)
- [5. Economy, Inventory, and Monetization](#5-economy-inventory-and-monetization)
- [6. Security, Authentication, and Diagnostics](#6-security-authentication-and-diagnostics)
- [Verification](#verification)

## Boot, scope, and dependency injection

Steam is an optional module built into the same native C++ Crimson GDExtension. Online integration composes its services through `GameRuntime`. Games can use Steam app services with another multiplayer provider or with no multiplayer at all. Inspector settings and code configuration use the same resources and validation.

### Start an FPS with Steam multiplayer

The FPS’s `GameDefinition` contains the starting world, player scene, and multiplayer rules. Author `PlayerSpawnPoint` components in the level. The game selects placements and uses its shared Scene Loader for solo, hosting, and joining. The menu receives the booted game once.

```gdscript
var game: GameRuntime

func _ready() -> void:
    var steam_provider := SteamProvider.configure().friends_only()
    game = demo_definition.with_provider(steam_provider).boot(self)
    if game == null:
        show_startup_error("The game could not attach to the application scene.")
        return

    multiplayer_menu.bind(game)
    var steam := SteamServices.for_game(game)
    if steam != null and steam.is_ready():
        profile_service.bind(steam.cloud())
    elif steam != null:
        multiplayer_menu.show_unavailable(steam.get_initialization_error())
```

`profile_service`, `multiplayer_menu`, and `show_startup_error()` are game code. Menu actions use the existing `await game.play_solo().wait()`, `await game.host_online().wait()`, and `await game.join_online(listing_id).wait()` operations. A Steam startup failure leaves solo play available and appears as an explicit online failure in the selected provider.

| Setup function | Contract |
| --- | --- |
| `GameDefinition.boot(owner: Node) -> GameRuntime?` | Create the configured game and attach it to a live owner. Return the attached game or `null` if it cannot be attached. The existing `create()` path supports manual attachment. |
| `SteamProvider.configure() -> SteamProvider` | Create a provider resource with Steam’s launch AppID, private listen lobbies, namespace `crimson`, and virtual port `0`. Private preserves the existing Steam host behavior. |
| `provider.for_app(expected_app_id: int) -> SteamProvider` | Assert the expected unsigned 32-bit AppID when booting. `0` accepts the Steam launch context. The process can use one client AppID. |
| `provider.with_lobby_namespace(namespace: StringName) -> SteamProvider` | Set a shared discovery namespace. Use a distinct project namespace for Spacewar development. Host and guest use the same namespace. |
| `provider.on_virtual_port(port: int) -> SteamProvider` | Select a Steam Networking Sockets virtual port in `0..65535`. A dedicated server’s physical listener and query port are separate settings. |
| `provider.private() / friends_only() / public_lobbies() / invisible_lobby() -> SteamProvider` | Choose the listen lobby’s visibility and discovery policy. The game’s admission rules apply in every mode. |
| `provider.prefer_relay() -> SteamProvider` | Request the supported relay path for listen connections. Inspect the connected route when proving relay use. Dedicated relay configuration is separate. |
| `provider.with_commerce_backend(backend: SteamCommerceBackend) -> SteamProvider` | Inject the game’s checkout adapter. This adapter talks to the game’s backend using player credentials. |
| `SteamServices.for_game(game: GameRuntime) -> SteamScope?` | Resolve the exact game-owned scope. A configured game with a Steam initialization failure can expose its startup diagnostic here. Return `null` for a closed game or a game without this binding. |
| `SteamServices.current() -> SteamScope?` | Resolve Steam for the same default game that `GameServices.game()` selects. Return `null` if a default game is ambiguous or has no Steam scope. Resolution uses the binding created at boot. |
| `SteamServices.server_for(game: GameRuntime) -> SteamDedicatedServer?` | Resolve the dedicated Steam server binding for the exact game. It uses the separate Steam Game Server runtime. Return `null` for a game without the binding. |
| `scope.is_ready() -> bool` | Report whether this scope has an initialized Steam client. Individual capabilities may have their own availability requirements. |
| `scope.get_initialization_error() -> String` | Return the safe startup diagnostic or an empty string. |
| `scope.get_app_id() -> int` | Return the active AppID or `0` for an inactive scope. |
| `scope.get_local_steam_id() -> String` | Return the signed-in player’s decimal SteamID64 or an empty string when unavailable. |

Each provider modifier returns a new resource carrying the previous choices plus the requested change. An Inspector-authored resource supplies the equivalent configuration. Keep gameplay capacity and join policy in `MultiplayerRules`. Keep Steam visibility, discovery, and transport settings in the provider.

### Use app features independently

```gdscript
var steam := SteamClient.boot(self)
if steam != null and steam.is_ready():
    profile_service.bind(steam.cloud())
```

| Standalone function | Contract |
| --- | --- |
| `SteamClient.boot(owner: Node, expected_app_id: int = 0) -> SteamClient?` | Attach and initialize a standalone client under a persistent owner. Return `null` if attachment fails. An attached client retains its initialization diagnostic on Steam failure. It exposes the same app service accessors as a game scope. |
| `client.start(expected_app_id: int = 0) -> Error` | Start a scene-authored or manually attached client. Use the same initialization path as `boot()`. Calling it again for the active AppID leaves the scope intact. |
| `client.stop() -> void` | End this standalone scope and settle its operations. Game-owned services end through game teardown. |
| `client.is_ready() -> bool`, `client.is_online() -> bool` | Report local Steam API readiness and Valve connectivity separately. Offline Mode preserves cached services. |
| `client.runtime_lost()` signal | Fires after fatal IPC invalidates this standalone scope. Defer a new `start()` until the callback returns. Resolve new service handles after restarting. |
| `SteamClient.restart_through_steam_if_needed(app_id: int) -> Error` | Optional pre-boot check for unwrapped rendered shipping clients. `ERR_SKIP` requests that the current game quit for Steam to relaunch it. Use the check before any native Steam initialization. |

Standalone scopes can coexist with an ENet game. The FPS can use Steam achievements and Cloud in solo or LAN play while its menu starts multiplayer with the ENet provider. Standalone scopes are accessed through the owning node or injection. `SteamServices.current()` follows the game selected by `GameServices.game()`.

### Provider and service ownership

- **Composition** captures provider settings at boot, constructs the Session and Scene Loader through the existing typed factories, and attaches them to the game. Steam online integration adds a typed Steam scope binding. The shared Session and Scene Loader stay provider-independent.
- **Injection** passes the actual game-scoped object to a consumer, such as `profile_service.bind(steam.cloud())`. `SteamServices.for_game(game)` and `SteamServices.current()` resolve that same scope. A game can substitute its own provider through `with_provider()` and its own test doubles through the consumer’s dependency boundary.
- **Lifetime** begins on the main thread. Client scopes share one Steam client runtime and callback pump for the process AppID. Leaving a match retires session resources while the persistent game remains available to host again. Freeing the game or standalone owner retires its Steam scope. Pending observations settle with a closed or cancelled result even when a native operation has to finish before its resources can be drained.
- **Dedicated hosting** uses a separate Steam Game Server runtime and callback pipe in the native C++ headless Godot project. The server owns authentication, admission, and world state. The existing [dedicated launcher](../../Run-Dedicated.bat) and [LAN launcher](../../Run-LAN.bat) provide local ENet checks. The Steam server section defines its opt-in verification lane.
- **Trusted publisher functions** are part of Crimson’s server-side API plan. A game backend or operator tool boots `SteamPublisher.boot(settings: SteamPublisherSettings) -> Result<SteamPublisher>` once. Settings contain the AppID, environment selection, and a secret reference resolved by the trusted host. For actions on behalf of a player, use the backend-verified `SteamIdentity` from ticket verification. Other actions take the target identifiers and authority context described by the feature.

`SteamPublisherSettings` contains `app_id: int`, `environment: SteamPublisherEnvironment` (`LIVE` or `SANDBOX`), `publisher_key_ref: StringName`, and an optional `encrypted_ticket_key_ref: StringName`. The secret references resolve only in the trusted host. `SANDBOX` selects a vendor sandbox for features that have one. Other publisher calls use the configured test app and the feature’s documented authorization requirements.

Custom online providers retain the existing typed hooks on `GameServiceProvider`. `_create_session() -> GameSession` supplies a fresh session and its transport or authenticator. `_create_scene_loader() -> SceneLoadManager` can supply a fresh loader. `_create_online_directory() -> OnlineDirectory?` supplies the provider’s directory and route resolution. `GameRuntime` owns the resulting scene-tree services and keeps gameplay rules and admission outside vendor code.

The facade resolves the same scoped service the provider supplies to injected consumers. [Laravel service providers](https://laravel.com/docs/13.x/providers) · [Laravel scoped bindings](https://laravel.com/docs/13.x/container#binding-scoped-singletons) · [Laravel facades](https://laravel.com/docs/13.x/facades)

## Public API conventions

| Convention | Contract |
| --- | --- |
| Direct action | `await steam.cloud().read("profile.dat")` performs one operation with defaults. Synchronous calls return a value or a Godot `Error` with the outcome described at the method. |
| Configured action | `await steam.http().accept_json().with_timeout(3000).fetch(url)` keeps the same terminal verb as `await steam.http().fetch(url)`. The settings chain is side-effect free. |
| Composed query | `query()` or a named starting point creates a draft. Filters return independent drafts. `fetch()` submits the query and returns results. |
| Composed mutation | A draft such as `stats().edit()` holds input. `submit()` starts the effect. The method contract says whether partial effects can survive failure. |
| `Result<T>` | Shorthand for a named result class with read-only `ok: bool`, `error: Error`, `message: String`, and `value: T`. The concrete class binds `value` with its actual type. Immediate validation and asynchronous completion return the same class. An API may return a receipt on failure when it identifies a partial effect. `Result<void>` binds as `SteamResult`, with no payload. |
| `Operation<T>` | Shorthand for a named retained operation. `wait()` yields the result, `result()` returns the concrete typed result after settlement or `null` while pending, `is_finished()` reports settlement, and `cancel()` requests cancellation or stops local observation as documented. The `completed(result)` signal binds its argument to the same concrete result class. The existing `GameOperation` keeps its result contract. |
| IDs and units | SteamID64, UGC, lobby, order, and other full-range unsigned 64-bit identifiers use decimal strings at the Godot boundary. Provider listing IDs are opaque. Parameter names show units, such as `timeout_ms`, `sample_rate_hz`, and `price_minor`. |
| Acknowledgment and evidence | An issued overlay request, accepted cache update, accepted Cloud write, and externally observed result each have their own meaning. Read the method contract and use the relevant signal or readback when the distinction matters. |
| Compatibility | Preserve game and provider method contracts through the [class-name migration](API_NAME_MIGRATION.md), including string-based `join_online(listing_id)`. Preserve existing Inspector property keys and include hover documentation for new properties. Convenience calls use the same validation and ownership as their detailed paths. |

In signature tables, `?` marks a nullable object. An `await` prefix marks a directly awaited terminal, including one that can settle immediately. Commands that queue an action state what `OK` acknowledges. [Godot GDScript await](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html#awaiting-signals-or-coroutines) · [Laravel filesystem](https://laravel.com/docs/13.x/filesystem) · [Laravel query builder](https://laravel.com/docs/13.x/queries)

Use `ERR_INVALID_PARAMETER` for input that fails validation, `ERR_UNAVAILABLE` when the capability or scope is unavailable, `ERR_BUSY` for a conflicting in-flight action, `ERR_TIMEOUT` for deadlines, and `ERR_SKIP` when a pending observation is cancelled. For `Operation<T>`, `cancel() -> Error` acknowledges a cancellation request and `wait()` returns the terminal result. Late vendor callbacks can finish native cleanup after the terminal result is delivered.

## Godot typing and code completion

- Bind a domain snapshot as a named `RefCounted` class with typed read-only properties. `SteamInvite { listing: SteamListing, from_steam_id: String }` describes bound properties. Use a typed object for options whose fields Crimson defines. A nullable options argument uses `null` to select defaults.
- Bind record collections as `Array[ConcreteType]` using native `TypedArray<ConcreteType>` return and property metadata. Keep primitive arrays for values that are already the data the caller needs, such as a list of Steam IDs or supported language codes. Use the same concrete types for signal arguments and nested properties.
- Use direct typed reads for cached objects and collections. A caller can use `:=` and access completion on the returned object or each element of its collection. Where a primitive read can fail, use a typed result so the caller can distinguish a legitimate zero from a failed read.
- Reserve dictionaries and `Variant` for data with caller-defined keys or shapes, such as JSON, localization substitutions, and custom Steam metadata. The existing `GameOperation.wait()` dictionary is a compatibility boundary. New Steam result classes expose typed properties.

For a named payload, the result class uses the `Steam` prefix once and the `Result` suffix. For example, `Result<LeaderboardPage>` binds as `SteamLeaderboardPageResult` and `Result<SteamHttpResponse>` binds as `SteamHttpResponseResult`. The shared `SteamResult` base exposes status fields. Each concrete result declares its own typed `value`. An exposed retained operation uses the same name stem with `Operation`. Reuse a binding when methods have the same payload type.

| Payload | Concrete result class |
| --- | --- |
| `void` | `SteamResult` |
| `int`, `float`, `bool`, `String` | `SteamIntResult`, `SteamFloatResult`, `SteamBoolResult`, `SteamStringResult` |
| `PackedByteArray`, `Texture2D`, `Variant` | `SteamBytesResult`, `SteamTextureResult`, `SteamJsonResult` for arbitrary JSON |
| `Array[SteamListing]`, `Array[SteamFriend]` | `SteamListingsResult`, `SteamFriendsResult` |
| `Array[CloudFile]`, `Array[SteamItemDefinition]` | `SteamCloudFilesResult`, `SteamItemDefinitionsResult` |
| `Array[SteamNotificationSession]`, `Array[SteamPartyListing]`, `Array[SteamPartyLocation]` | `SteamNotificationSessionsResult`, `SteamPartyListingsResult`, `SteamPartyLocationsResult` |
| `Array[Texture2D]`, `Array[SteamDlcInfo]` | `SteamTexturesResult`, `SteamDlcListResult` |

Godot 4.7 treats an awaited native signal’s payload as `Variant` during static analysis. Annotate the result once at the direct `await` boundary. The concrete result then supplies completion for `value`, its properties, and its collection elements. A retained operation also exposes typed completion callbacks and a typed `result()` getter for `:=` after settlement. [Godot static typing](https://docs.godotengine.org/en/4.7/tutorials/scripting/gdscript/static_typing.html) · [Godot 4.7 await type analysis](https://github.com/godotengine/godot/blob/4.7-stable/modules/gdscript/gdscript_analyzer.cpp#L2857-L2885)

```gdscript
var leaders: SteamLeaderboardPageResult = await steam.leaderboards().top("HALL_SPEEDRUN_MS")
if leaders.ok:
    for entry in leaders.value.entries:
        scoreboard.add_time(entry.rank, entry.steam_id, entry.score)
```

## 1. Player progression and Cloud

### Stats and achievements

**Initialization**

Use `steam.stats()` after the shared client is ready. The current Steam SDK makes the signed-in player's cache available through client initialization. Publish the app's stat API names, types, bounds, write permissions, and achievement definitions in Steamworks. A co-op match records authoritative outcomes first, then submits the signed-in player's eligible personal progression. Competitive progression uses the app's trusted reporting path where the Steam definition requires it.

**Target API**

```text
steam.stats() -> SteamStats
stats.is_available() -> bool
stats.stat_int(name: String) -> SteamIntResult
stats.stat_float(name: String) -> SteamFloatResult
stats.achievements() -> Array[SteamAchievement]
stats.achievement(api_name: StringName) -> SteamAchievement?
stats.edit() -> SteamStatsEdit
edit.set_int(name: String, value: int) -> SteamStatsEdit
edit.set_float(name: String, value: float) -> SteamStatsEdit
edit.update_average_rate(name: String, count: float, seconds: float) -> SteamStatsEdit
edit.unlock(name: String) -> SteamStatsEdit
edit.submit() -> Result<StatsSubmission>
stats.show_progress(name: String, current: int, maximum: int) -> Error
stats.clear_achievement(name: String) -> Error
stats.store() -> Error
signal stats.stats_stored(steam_result: int)
signal stats.achievement_stored(name: String, current: int, maximum: int)
```

`SteamAchievement` is a read-only snapshot with `api_name: StringName`, `display_name: String`, `description: String`, `hidden: bool`, `unlocked: bool`, and `unlocked_at_unix: int`. `display_name` and `description` use Steam’s localized metadata. `unlocked_at_unix` is `0` if the achievement is locked or Steam has no timestamp. Read `unlocked` to determine its state. `is_available()` reports access to the SDK interface. Inspect each stat or achievement read to determine whether the cached data is available. `achievements()` returns typed cached entries and can be empty. `achievement()` returns one cached entry or `null` if the name or cache is unavailable. [Steam achievement display attributes](https://partner.steamgames.com/doc/api/ISteamUserStats#GetAchievementDisplayAttribute) · [Steam unlock timestamps](https://partner.steamgames.com/doc/api/ISteamUserStats#GetAchievementAndUnlockTime)

Numeric results distinguish unknown names, type mismatches, and unavailable data from a valid `0` or `0.0`. On success `SteamIntResult.value` is an `int` and `SteamFloatResult.value` is a `float`. Integer values fit a signed 32-bit stat, floats must be finite within Steam's single-precision range, and average-rate edits require a finite count and positive duration in seconds.

Each edit returns an independent branch. `submit()` checks names, value ranges, and readable definitions before modifying the cache and asks Steam to store once after applying the batch. `StatsSubmission` reports `applied: int` and `failed_at: int` with `-1` when no individual edit failed. The result preserves those fields on failure. If Steam rejects a setter after preflight, prior accepted edits remain cached. If Steam rejects the store request, the applied edits remain cached and a later `store()` can retry persistence. A successful submit acknowledges the local store request. The app-wide `stats_stored` signal reports backend outcomes from store and progress calls. Treat it as an app event and read the latest cache if Steam rejects a submitted value. Progress uses `0 < current < maximum`, requests a popup, and leaves the unlock state alone. `clear_achievement()` changes the cache for a controlled test or reset and is followed by an explicit `store()` when persistence is intended. [Valve Stats and Achievements](https://partner.steamgames.com/doc/features/achievements) · [Valve StoreStats](https://partner.steamgames.com/doc/api/ISteamUserStats#StoreStats)

**FPS integration**

The Transfer Hall pause menu can build its achievement list directly from the cached entries. The editor can complete `achievement.` from the array’s element type.

```gdscript
var achievements := steam.stats().achievements()
for achievement in achievements:
    if achievement.hidden and not achievement.unlocked:
        achievement_panel.add_hidden_entry()
    else:
        achievement_panel.add_entry(
            achievement.display_name,
            achievement.description,
            achievement.unlocked
        )
```

At the end of a fictional Transfer Hall defense, the match result supplies the player's lifetime eliminations, distance, and whether they completed their first successful defense. The game writes absolute totals and conditionally adds the achievement to the same draft. During a defense, the HUD can show local progress and request a Steam popup at a deliberate milestone. Submit at match completion or another meaningful checkpoint.

```gdscript
var stats := steam.stats()
var draft := stats.edit().set_int("TOTAL_ELIMINATIONS", lifetime_eliminations)
draft = draft.set_float("DISTANCE_METERS", lifetime_meters)
if first_defense_win:
    draft = draft.unlock("FIRST_HALL_DEFENSE")
var submission := draft.submit()
```

**Verification**

The planned CLI stats case reads Spacewar 480's `NumGames` and `ACH_WIN_ONE_GAME`, rejects an invalid second edit before applying any edits, and proves that a retained draft cannot submit after stop. Check completion on `achievement.` in the Godot editor using the loop above. The fixture should parse with strict typing and catch a deliberately misspelled property, with no cast or loop-variable annotation in the valid version. A separately opted-in app run requires a non-480 AppID, a signed-in test account, published writable numeric definitions and an initially locked achievement. The CLI observes the app-wide store and achievement signals, checks immediate cache readback, then relaunches with the same account to check persistence. The GUI shows a progress popup, an unlock popup and updated achievement state. Verify rejection with an app-owned stat bound and report the returned error and applied count. [Valve testing and reset guidance](https://partner.steamgames.com/doc/features/achievements#testing)

### Global leaderboards

**Initialization**

Use `steam.leaderboards()`. Configure stable board names, sort direction, display units, and read/write permissions in Steamworks for production. Steam supports up to 10,000 boards per app, with one entry per player on each board. Use a trusted-write board and the game's backend for ranked runs that require authority. Client uploads suit boards explicitly configured for them. [Valve leaderboard configuration](https://partner.steamgames.com/doc/features/leaderboards)

**Target API**

```text
steam.leaderboards() -> SteamLeaderboards
leaderboards.is_available() -> bool
await leaderboards.top(board_name: String, count: int = 10) -> Result<LeaderboardPage>
await leaderboards.friends(board_name: String, count: int = 10) -> Result<LeaderboardPage>
await leaderboards.submit_score(board_name: String, score: int) -> Result<LeaderboardSubmission>
leaderboards.with_details(details: PackedInt32Array) -> SteamScoreOptions
leaderboards.with_replay(shared_file_handle: String) -> SteamScoreOptions
options.with_details(details: PackedInt32Array) -> SteamScoreOptions
options.with_replay(shared_file_handle: String) -> SteamScoreOptions
await options.submit_score(board_name: String, score: int) -> Result<LeaderboardSubmission>
leaderboards.query(board_name: String) -> SteamLeaderboardQuery
query.global_ranks(first_rank: int = 1, count: int = 10) -> SteamLeaderboardQuery
query.friends(count: int = 10) -> SteamLeaderboardQuery
query.around_me(before: int = 4, after: int = 5) -> SteamLeaderboardQuery
query.users(steam_ids: PackedStringArray) -> SteamLeaderboardQuery
await query.fetch() -> Result<LeaderboardPage>
query.fetch_operation() -> Operation<LeaderboardPage>
leaderboards.find(board_name: String) -> Operation<SteamLeaderboard>
leaderboards.find_or_create(board_name: String, sort: Sort = DESCENDING,
                           display: Display = NUMERIC) -> Operation<SteamLeaderboard>
board.is_available() -> bool
board.metadata() -> Result<LeaderboardInfo>
board.entries(first_rank: int, count: int) -> Operation<LeaderboardPage>
board.around_user(before: int, after: int) -> Operation<LeaderboardPage>
board.friends(count: int = 100) -> Operation<LeaderboardPage>
board.upload(score: int, details: PackedInt32Array = [],
             force_update: bool = false) -> Operation<ScoreUpload>
board.attach_ugc(shared_file_handle: String) -> Operation<void>
publisher.leaderboards() -> SteamPublisherLeaderboards
await publisher.leaderboards().submit_score(player: SteamIdentity, board_id: int, score: int,
                                           details: PackedByteArray = []) -> Result<LeaderboardSubmission>
```

`Sort` is `ASCENDING` or `DESCENDING` and `Display` is `NUMERIC`, `SECONDS`, or `MILLISECONDS`. `LeaderboardInfo` has `name: String`, `entry_count: int`, `sort: Sort`, and `display: Display`. A `LeaderboardPage` has `entries: Array[LeaderboardEntry]` and `total_count: int` for the SDK result before Crimson's display cap. Each entry has `steam_id: String`, `rank: int`, `score: int`, `details: PackedInt32Array`, and `ugc_handle: String`, empty when absent. Queries are branchable, choose one selection per branch, and default to the first ten global ranks. Ranks begin at one. Pages contain at most 100 rows, including the sum of the before and after counts plus the local player for an around-me query. A selected-user query accepts 1 to 100 unique decimal Steam IDs, returns only users with entries, and allows one outstanding selected-user request. Empty results are successful pages. The around-me view checks whether the returned page contains the local account before highlighting a rank. A missing board, denied query, invalid range, timeout, or stopped scope produces an error. `top()` and `friends()` compose lookup and download. `find_or_create()` can create an app-owned board, and an existing board keeps its configured metadata.

`ScoreUpload` carries `score: int`, `changed: bool`, `rank: int`, and `previous_rank: int`. `score` is the submitted value. Steam accepts signed 32-bit scores and up to 64 signed 32-bit details. It allows one outstanding upload and applies its rate limit of 10 uploads per 10 minutes. Keep-best uses the board's sort direction, and an accepted score can leave the entry unchanged. Force update explicitly replaces the entry. Attach after the accepted score you intend to associate with the replay. The handle comes from `cloud.share()` for a same-app file. Sharing, score upload, and attachment are separate requests with independent outcomes. A failed attachment leaves the score available without the new replay. Steam keeps an attached replay available even if the source Cloud file later changes or is deleted. [Valve score uploads](https://partner.steamgames.com/doc/api/ISteamUserStats#UploadLeaderboardScore) · [Valve replay attachment](https://partner.steamgames.com/doc/api/ISteamUserStats#AttachLeaderboardUGC)

Both client `submit_score()` paths look up an existing board and keep the best entry. `LeaderboardSubmission` records the accepted score outcome and a replay state of `NONE`, `SKIPPED`, `ATTACHED`, or `FAILED`. Replay options attach only when the submitted score becomes the entry. A failed attachment returns the accepted score outcome so the game can offer an attachment retry. The publisher path takes the configured Web API board ID, keeps the best score, and accepts up to 256 bytes of details. The game backend verifies the player and the run before submission. The result contains Steam’s acceptance and any score information Steam returned. [Valve publisher leaderboard API](https://partner.steamgames.com/doc/webapi/ISteamLeaderboards#SetLeaderboardScore)

**FPS integration**

```gdscript
var fastest: SteamLeaderboardPageResult = await steam.leaderboards().top("HALL_SPEEDRUN_MS")
var friends: SteamLeaderboardPageResult = await steam.leaderboards().friends("HALL_SPEEDRUN_MS")
var around_me: SteamLeaderboardPageResult = await steam.leaderboards().query("HALL_SPEEDRUN_MS").around_me().fetch()
var submitted: SteamLeaderboardSubmissionResult = await steam.leaderboards().with_details(PackedInt32Array([course_version])) \
    .with_replay(shared_ghost).submit_score("HALL_SPEEDRUN_MS", finish_time_ms)
```

A Transfer Hall time trial uses an ascending milliseconds board. After a verified run, the app writes the time and a small course-version detail. If the player elects to publish a ghost, the game writes a replay file to Cloud, shares it, and attaches the returned handle once the score upload confirms that this run replaced the entry. Other players download the handle through `cloud.download_shared()`, then the game checks the replay format and course version before playback.

**Verification**

A read-only Spacewar CLI or GUI run looks up `Quickest Win`, shows a top and a friends page, checks rank ordering and the row cap, and verifies cancellation and missing-board results without creating a board. An app-owned test run creates a board or uses a published one, then verifies score submission, keep-best, forced replacement, and readback including details. Use two eligible Steam accounts to show friend filtering and replay retrieval by a second account. A competitive board test verifies that the client write is rejected and that the trusted backend submission appears in the same query. [Valve upload and filtering behavior](https://partner.steamgames.com/doc/features/leaderboards) · [Valve trusted score submission](https://partner.steamgames.com/doc/webapi/ISteamLeaderboards#SetLeaderboardScore)

### Steam Cloud

**Initialization**

Use `steam.cloud()` for game-managed files. Set the app's file and byte quotas in Steamworks and check the account and app sync settings. For existing local saves or configs managed by Crimson Save, select Steam Auto-Cloud in the partner portal and point its roots at the game's stable per-user save folders. Steam manages Auto-Cloud sync with the same shared game launch. Choose one path for each save or config file. If Dynamic Cloud Sync is enabled, handle its local change notification before resuming a loaded save. [Valve Steam Cloud setup](https://partner.steamgames.com/doc/features/cloud)

**Target API**

```text
steam.cloud() -> SteamCloud
cloud.is_available() -> bool
cloud.is_enabled_for_account() -> bool
cloud.is_enabled_for_app() -> bool
cloud.quota() -> Result<CloudQuota>
cloud.file_info(name: String) -> Result<CloudFile>
cloud.files() -> Result<Array[CloudFile]>
cloud.begin_write_batch() -> SteamCloudWriteBatchResult
batch.is_open() -> bool
batch.close() -> Error
await cloud.read(name: String) -> Result<PackedByteArray>
await cloud.write(name: String, bytes: PackedByteArray) -> Result<void>
cloud.read_file(name: String) -> Operation<PackedByteArray>
cloud.write_file(name: String, bytes: PackedByteArray) -> Operation<void>
cloud.delete_file(name: String) -> Error
await cloud.share(name: String) -> Result<String>
await cloud.download_shared(handle: String, max_bytes: int = 16777216) -> Result<SharedCloudFile>
signal cloud.local_files_changed(changes: Array[CloudChange], truncated: bool)
```

`begin_write_batch()` returns a scoped advisory hint in `value`. The hint covers a group of Remote Storage or Auto-Cloud writes and deletes for the active app. Await the individual writes and close the handle on the main thread. An already closed handle can be closed again. Another concurrent batch returns `ERR_BUSY`. Scope teardown also ends the hint. Each file keeps its own outcome. [Valve Cloud batches](https://partner.steamgames.com/doc/api/ISteamRemoteStorage#BeginFileWriteBatch)

`CloudQuota` has `total_bytes: int` and `available_bytes: int`. `CloudFile` has `name: String`, `size_bytes: int`, `modified_at_unix: int`, and `persisted: bool` as reported by Steam. `CloudChange` contains `name_or_path: String`, `change_type: UPDATED | DELETED`, and `path_type: API_FILENAME | ABSOLUTE` to distinguish Remote Storage names from Auto-Cloud paths. If Steam supplies more changes than the snapshot cap, `truncated` tells the game to rescan the affected save roots. `SharedCloudFile` carries `handle: String`, `name: String`, `owner_steam_id: String`, and `bytes: PackedByteArray`. File operations accept validated portable relative logical names. The whole-file cap is 16 MiB and writes require nonempty data. Read and write settle when Steam completes its local operation. Delete acknowledges the local removal and marks it for Steam's normal sync. Persistence on another machine depends on Steam completing sync. Missing files, disabled access, exceeded quotas, invalid names, and concurrent transfers for the same file get observable errors. Cancellation or timeout ends local observation, and an already-submitted write can still take effect. Sharing explicitly exposes the file to recipients of its handle. Download returns bounded bytes for the same app after checking the native result and size. [Valve Remote Storage](https://partner.steamgames.com/doc/api/ISteamRemoteStorage) · [Valve Dynamic Cloud Sync](https://partner.steamgames.com/doc/features/cloud#dynamic_cloud_sync)

**FPS integration**

Store the Transfer Hall checkpoint and versioned campaign data through the game's Save module with Auto-Cloud. Use the explicit file API for a separately selected portable preset. Apply an incoming preset after validating its schema. Keep hardware-specific bindings and video settings in a local file. Publish an opt-in ghost replay through `share()`, with a unique logical name per run.

```gdscript
var saved: SteamResult = await steam.cloud().write("presets/hall.json", preset_bytes)
var restored: SteamBytesResult = await steam.cloud().read("presets/hall.json")
if restored.ok:
    preset_service.apply_validated(restored.value)
```

**Verification**

The SDK-off and missing-runtime CLI cases confirm unavailable results. A signed-in Spacewar CLI run inspects availability, quota, and safe read failures. In app mode use a Cloud-enabled app, an entitled account, enough quota, and a unique test file. Write, read and compare bytes, inspect file metadata, then delete and check absence. For a cross-device run, use the GUI fixture to exit on the first device, wait for Steam to report sync, launch on the second device with the same account, and verify exact bytes and checkpoint restoration. For Dynamic Cloud Sync, use Valve's suspend/resume test to confirm the application sees changed files and reloads safely. A separate replay check uses another eligible account and compares the downloaded bytes to the shared file. [Valve Cloud pre-release testing](https://partner.steamgames.com/doc/features/cloud#pre-release_testing)

### Steam Timelines

**Initialization**

Use `steam.timeline()` on the player's client. Game Recording and the overlay are controlled by Steam and the player's settings. Built-in `steam_*` icons work without custom assets. Publish app-specific icons in Steamworks and supply localized display strings in Steam's UI language. Assign one active game to emit process-wide modes, tooltips and phases.

**Target API**

```text
steam.timeline() -> SteamTimeline
timeline.is_available() -> bool
timeline.set_game_mode(mode: GameMode) -> Error
timeline.set_tooltip(description: String, offset_seconds: float = 0.0) -> Error
timeline.clear_tooltip(offset_seconds: float = 0.0) -> Error
timeline.mark(title: String, options: TimelineMarkerOptions? = null) -> Result<SteamTimelineEvent>
timeline.mark_range(title: String, duration_seconds: float,
                    options: TimelineMarkerOptions? = null) -> Result<SteamTimelineEvent>
timeline.start_range(title: String, options: TimelineMarkerOptions? = null) -> Result<SteamTimelineEvent>
timeline.update_range(event: SteamTimelineEvent, title: String,
                      presentation: TimelineMarkerPresentation? = null) -> Error
timeline.end_range(event: SteamTimelineEvent, offset_seconds: float = 0.0) -> Error
timeline.remove_event(event: SteamTimelineEvent) -> Error
event.is_valid() -> bool
event.is_open() -> bool
await timeline.recording_exists(event: SteamTimelineEvent) -> Result<bool>
timeline.recording_operation(event: SteamTimelineEvent) -> Operation<bool>
timeline.open_in_overlay(event: SteamTimelineEvent) -> Error
timeline.start_phase(phase_id: String) -> Error
timeline.set_phase_attribute(group: String, value: String, priority: int = 0) -> Error
timeline.add_phase_tag(name: String, icon: String, group: String, priority: int = 0) -> Error
timeline.end_phase() -> Error
await timeline.phase_recording(phase_id: String) -> Result<TimelinePhaseRecording>
timeline.open_phase_in_overlay(phase_id: String) -> Error
```

`GameMode` is `PLAYING`, `STAGING`, `MENUS`, or `LOADING_SCREEN`. `TimelineMarkerPresentation` has typed option properties `description: String = ""`, `icon: String = "steam_marker"`, `priority: int = 0`, and `clip: TimelineClipPriority = NONE`, with `STANDARD` and `FEATURED` as other clip choices. `TimelineMarkerOptions` adds `offset_seconds: float = 0.0` to those fields. A null option selects those defaults. Marker, tag, and attribute priorities range from 0 to 1,000. Negative offsets place markers in recent past footage and fixed ranges require a positive duration. An open range accepts updates before ending or removal. Handles expire when their scope stops. Only one game phase can be open at a time. Stable game-owned phase IDs can identify recordings across runs. `TimelinePhaseRecording` contains `recording_ms: int`, `longest_clip_ms: int`, `clip_count: int`, and `screenshot_count: int`. Validation failures, unavailable interfaces, foreign or stale handles, and a disabled overlay are observable errors. A successful marker or overlay request means it was submitted. Query for available footage before offering playback. [Valve Steam Timeline API](https://partner.steamgames.com/doc/api/ISteamTimeline) · [Valve Timeline icons](https://partner.steamgames.com/doc/features/timeline)

**FPS integration**

Set `MENUS` in the main menu, `STAGING` while co-op players gather, `LOADING_SCREEN` during map travel, and `PLAYING` on entering the Hall. Start one phase for the match, tag its map and difficulty, and start a range when the defense begins. Mark the last portal seal with `steam_checkmark` and a `FEATURED` clip hint, then close the defense range and match phase. The post-match screen offers **Watch replay moment** only when Steam confirms footage for the selected marker.

```gdscript
var timeline := steam.timeline()
timeline.set_game_mode(SteamTimeline.PLAYING)
timeline.start_phase(match_id)
timeline.add_phase_tag("Transfer Hall", "steam_marker", "Map")
var marker_options := TimelineMarkerOptions.new()
marker_options.icon = "steam_checkmark"
marker_options.clip = SteamTimeline.FEATURED
var portal_seal := timeline.mark("Final portal sealed", marker_options)
```

**Verification**

The CLI Timeline case can check invalid input, event handle ownership, range end/removal, timeout or cancellation of a retained query, and the result that recording is absent. To verify what players see, use a graphical Steam launch with the overlay and background recording enabled, complete the Transfer Hall fixture, and open Game Recording. Confirm the mode colors, portal icon, defense range, phase tags, clip suggestion, and that opening the event seeks to the recorded moment. Spacewar can exercise built-in icons where the overlay and recording work. Verify custom icons and deployment settings with the app's own AppID. [Valve Steam Timelines](https://partner.steamgames.com/doc/features/timeline) · [Steam Game Recording](https://store.steampowered.com/gamerecording)

## 2. Multiplayer

The current game operation uses `await game.host_online().wait()`. Types such as `SteamListing` denote read-only Godot objects, and Steam IDs stay decimal strings. `?` denotes a nullable object.

### Lobbies and matchmaking

**Initialization**

```gdscript
var game := demo_definition.with_provider(SteamProvider.configure().friends_only()).boot(self)
var steam := SteamServices.for_game(game)
var hosted: Dictionary = await game.host_online().wait()
```

Choose `private()` for invite-only play, `friends_only()` for friends and invitees, `public_lobbies()` for search and friends visibility, or `invisible_lobby()` for search without friends visibility. The provider defaults to private. Standalone social scenes can use `steam.lobbies()` after booting a standalone Steam client. The local Steam account owns one ordinary lobby and one active search across its Crimson scopes. [Valve lobby types and search](https://partner.steamgames.com/doc/api/ISteamMatchmaking#ELobbyType)

**Target API**

| Call or signal | Contract |
| --- | --- |
| `SteamProvider.configure() -> SteamProvider`, `provider.for_app(expected_app_id: int) -> SteamProvider` | Begin with defaults or branch for an expected AppID. Zero takes the active launch AppID. |
| `provider.private() -> SteamProvider`, `friends_only() -> SteamProvider`, `public_lobbies() -> SteamProvider`, `invisible_lobby() -> SteamProvider` | Return a branch with the selected visibility. |
| `provider.with_lobby_namespace(namespace: StringName) -> SteamProvider`, `provider.on_virtual_port(port: int) -> SteamProvider`, `provider.prefer_relay() -> SteamProvider` | Set the game namespace, choose a virtual port, or prefer relayed listen-host routing. Default virtual port is zero. Shared-AppID search fixtures require an explicit namespace. |
| `steam.lobbies() -> SteamLobbies` and `lobbies.current() -> SteamLobby?` | Resolve the service and read the current lobby if present. |
| `lobbies.query() -> SteamLobbyQuery` | Create a detached query scoped to the current AppID and configured game namespace. |
| `query.on_map(world_id: StringName) -> SteamLobbyQuery`, `with_room_for(players: int) -> SteamLobbyQuery`, `within(distance: SteamLobbyDistance) -> SteamLobbyQuery`, `limit(count: int) -> SteamLobbyQuery` | Return branches for a map, minimum open seats, Steam search distance, and result cap. The defaults are any compatible map, one seat, nearby regions, and 20 results. |
| `await query.fetch() -> Result<Array[SteamListing]>`, `query.fetch_operation() -> Operation<Array[SteamListing]>` | Run the same bounded search. Use the operation to cancel observation. An overlapping local search returns busy. |
| `lobbies.friend_lobby(friend_id: String) -> Result<SteamListing>` | Read the lobby shared by a friend in the matching app. The snapshot may expire before join. |
| `lobbies.create(options: SteamLobbyOptions) -> Operation<SteamLobby>`, `lobbies.join(listing: SteamListing) -> Operation<SteamLobby>` | Create or join a standalone social lobby. Options are `visibility: SteamLobbyVisibility`, `namespace: StringName` and `capacity: int`. A game-owned membership already holding the account reservation returns busy. |
| `game.join_listing(listing: OnlineListing) -> GameOperation` | Revalidate a typed listing and join through the current game's provider. Existing `game.join_online(listing_id: String) -> GameOperation` remains the direct-ID entry point. |
| `lobby.listing() -> SteamListing`, `lobby.members() -> Array[SteamLobbyMember]` | Read a detached listing and the current lobby member snapshots. Members expose `steam_id: String`, `display_name: String` and `is_owner: bool`. |
| `lobby.edit() -> SteamLobbyEdit`, `edit.named(name: String) -> SteamLobbyEdit`, `edit.tag(key: StringName, value: String) -> SteamLobbyEdit`, `edit.submit() -> Error` | Stage bounded owner-managed display metadata and submit it to Steam. Crimson reserves protocol, route, world, and authority fields. |
| `lobby.set_joinable(open: bool) -> Error`, `lobby.leave() -> Error` | Change standalone lobby admission or leave standalone membership. For a game-owned lobby, Crimson owns joinability and `game.leave()` retires the game and lobby together. |
| `lobby.invite(steam_id: String) -> Error`, `lobby.open_invite_dialog() -> Error`, `lobby.send_chat(text: String) -> Error` | Submit an explicit invite, request the ready Steam overlay dialog, or send bounded social chat. Steam acknowledgment and recipient arrival are distinct events. |
| `lobbies.invite_received(listing: SteamListing)`, `lobby.members_changed(members: Array[SteamLobbyMember])`, `lobby.chat_received(member: SteamLobbyMember, text: String)`, `lobby.closed(reason: Error)` | Observe typed invites, membership, bounded chat, and retirement. |

`SteamListing` implements `OnlineListing`, a shared join snapshot with `provider: StringName` and `id: String`. Steam adds `kind: SteamListingKind` (`LOBBY` or `SERVER`), `app_id: int`, `namespace: StringName`, `world_id: StringName`, `name: String`, `players: int`, `capacity: int`, `server_steam_id: String` and `ping_ms: int`. The last two apply to server listings and default to empty and −1. Route data is owned by the provider. At join, the provider rechecks the current route, app, compatibility, and actual Steam owner or server identity. Steam lobby member status and admission to the Crimson world are distinct states. [Valve lobby lifecycle](https://partner.steamgames.com/doc/features/multiplayer/matchmaking) · [Valve lobby metadata and chat](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

**FPS integration**

Use friends or invites to join a friends-only Transfer Hall. The public browser shows games that publish to search.

```gdscript
var friend_lobby := steam.lobbies().friend_lobby(friend_id)
if friend_lobby.ok:
    await game.join_listing(friend_lobby.value).wait()
```

```gdscript
var listings: SteamListingsResult = await steam.lobbies().query().on_map(&"transfer_hall").with_room_for(2).fetch()
if listings.ok and not listings.value.is_empty():
    browser_view.show_lobbies(listings.value)
```

Crimson opens admission after the authority mounts and publishes, assigns each verified peer its actor, and declares a joining client ready after world and baseline activation. Closing the host ends the Crimson session even if Steam transfers social lobby ownership. Lobby chat stays outside simulation and command dispatch.

**Verification**

- On two signed-in accounts, use the planned `steam_smoke.gd -- lobbies <role> <namespace> <visibility>`. Use unique Spacewar namespaces and consenting accounts. Check private through direct invite, friends-only via a friend lookup or invite, and public or invisible through a matching bounded search. Check each visibility with a mismatched namespace and use the overlay fixture for an actual visible dialog.
- Require a real guest identity and actor on both peers, a preserved host world on late join, full-lobby rejection, no admission after close, and retirement of both memberships. Verify cancelling a search discards its late result and a later query works. Verify standalone chat and metadata exchange between the controlled accounts. [Valve testing with Spacewar](https://partner.steamgames.com/doc/sdk/api/example) · [Valve lobby search](https://partner.steamgames.com/doc/api/ISteamMatchmaking#RequestLobbyList)

### Steam dedicated server and server browser

**Initialization**

Give the headless Godot game a Steam provider configured with `SteamServer`, a gameplay port and a separate query port. The dedicated process opens Steam GameServer on its own callback pipe and uses its game server identity. Start with secure direct transport. Enable a Valve-approved hosted SDR profile explicitly when the target app and deployment support it. The ENet dedicated launcher remains available for local development.

```gdscript
var server_settings := SteamServer.configure("Transfer Hall").advertise_on(SteamServer.INTERNET)
var provider := SteamProvider.configure().for_app(app_id).dedicated(server_settings)
var game := demo_definition.with_provider(provider).boot(self)
var started: Dictionary = await game.start_dedicated_online().wait()
if started.ok:
    var server := SteamServices.server_for(game)
    server.status_changed.connect(server_log.report_status)
```

**Target API**

| Call or signal | Contract |
| --- | --- |
| `SteamServer.configure(name: String = "Crimson Server", game_port: int = 27015, query_port: int = 27016) -> SteamServer` | Build settings with secure authentication, wildcard IPv4 bind, and unlisted direct transport by default. Steamworks 1.65 controls LAN and master advertisement together, so Internet publication requires an explicit opt-in. |
| `options.secure() -> SteamServer`, `bind_to(address: String) -> SteamServer`, `advertise_on(scope: SteamServerScope) -> SteamServer`, `with_login(secret_ref: StringName) -> SteamServer`, `via_sdr(profile: StringName) -> SteamServer` | Select secure mode, an interface, LAN or Internet advertisement, an operator-managed Steam server login, or a configured SDR deployment. Anonymous Steam logon is the default. |
| `options.with_product_metadata(version: String, description: String, install_dir: String) -> SteamServer` | Select the app's master-server version, game description, and install folder name for a branch. Configure these before production advertisement. |
| `provider.dedicated(options: SteamServer) -> SteamProvider`, `game.start_dedicated_online() -> GameOperation` | Select Steam's dedicated mode and use the existing Game action to start the playerless authority. Readiness requires the secure login, world, and listener. The Internet scope also requests advertisement. |
| `SteamServices.server_for(game: GameRuntime) -> SteamDedicatedServer?`, `server.status() -> SteamServerStatus` | Observe the server scope without a signed-in client. Status exposes `advertisement_requested: bool`, its existing alias `listed`, `secure: bool`, `authenticated_players: int` and `world_id: StringName`. A separate client browser query confirms discovery. |
| `server.stats(peer_id: int) -> SteamServerPlayerStats?` | Resolve the exact admitted connection's dedicated server stats. Use `load().wait()` for cached reads or `edit().submit().wait()` to load as needed, apply staged edits and observe Steam persistence. The handle retires when that connection leaves. |
| `steam.server_browser() -> SteamServerBrowser`, `browser.query(source: SteamServerScope = INTERNET) -> SteamServerQuery` | Resolve browser and select Steam's Internet or LAN source. |
| `query.on_map(world_id: StringName) -> SteamServerQuery`, `with_room_for(players: int) -> SteamServerQuery`, `under_latency(milliseconds: int) -> SteamServerQuery`, `limit(count: int) -> SteamServerQuery` | Filter compatible results. Defaults accept any compatible map, require one seat, apply no additional ping cutoff, and return up to 50 entries. |
| `await query.fetch(timeout_ms: int = 5000) -> Result<Array[SteamListing]>`, `query.fetch_operation(timeout_ms: int = 5000) -> Operation<Array[SteamListing]>` | Browse or retain a cancellable query. Results carry server identity and measured ping in addition to the shared Steam listing fields. |
| `browser.inspect(listing: SteamListing, timeout_ms: int = 3000) -> Operation<SteamServerDetails>` | Refresh the selected server's public details. Details expose `listing: SteamListing`, `ping_ms: int` and `tags: PackedStringArray`. |
| `server.status_changed(status: SteamServerStatus)` | Observe server readiness and listing changes. Admission and revocation use the game's Session lifecycle. |

Direct transport keeps gameplay and query endpoints distinct. The authority validates Steam ticket ownership for the AppID, binds success to the actual connection, revokes a connection on a later invalid status, and ends tickets on teardown. The client verifies the selected server identity before treating it as its game authority. Steam browser data is discovery input. Trust and gameplay admission follow server identity and ticket checks. Release the browser query handle on every terminal path and withdraw server advertisement on shutdown. [Valve game servers](https://partner.steamgames.com/doc/features/multiplayer/game_servers) · [Valve GameServer interface](https://partner.steamgames.com/doc/api/ISteamGameServer) · [Valve browser handles](https://partner.steamgames.com/doc/api/ISteamMatchmakingServers#ReleaseRequest) · [Valve authentication](https://partner.steamgames.com/doc/features/auth)

**FPS integration**

Populate the server list and join when the player selects a row. The headless authority has no local pawn or camera. Guests receive the existing world and baseline path, and the server count reflects admitted clients. `game.leave() -> Error` initiates the existing game teardown.

```gdscript
var servers: SteamListingsResult = await steam.server_browser().query().on_map(&"transfer_hall").fetch()
if servers.ok:
    server_browser.show_listings(servers.value)

func join_selected_server(listing: OnlineListing) -> void:
    multiplayer_menu.show_join_result(await game.join_listing(listing).wait())
```

**Verification**

- Planned `steam_smoke.gd -- dedicated-host` and `-- dedicated-browser <lan|internet>` run separately. Require the headless ready marker with zero local actors, browser discovery from a separate Steam client, successful ticket validation and guest spawn, denial of a stale or wrong-app ticket, and a clean rediscovery after restart. Use an authorized AppID and the server login, listing settings and firewall appropriate to the target. Spacewar is a shared development context.
- Verify LAN and Internet listing separately, including cancellation while results are arriving. For hosted SDR, use the approved app and actual deployment configuration and assert that the joined connection reports a relay path. Existing ENet launchers cover credential-free dedicated and local fallback. [Valve dedicated SDR](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay)

### Networking Sockets, Datagram Relay, and Networking Messages

**Initialization**

The game provider chooses Steam Networking Sockets for gameplay and uses Steam's normal P2P routing on listen hosts. Standalone SDK users can resolve `steam.sockets()` for an explicit connection or `steam.messages()` for a separate bounded message channel. Enable game-side messages only for project-defined channels and actual admitted Steam peers.

**Target API**

| Call or signal | Contract |
| --- | --- |
| `steam.networking() -> SteamNetworking`, `networking.relay_status() -> SteamRelayStatus`, `networking.peer_path(peer_id: int) -> Result<SteamConnectionPath>` | Observe relay readiness and a live game peer's measured path, latency and connection quality. Path kind is `DIRECT`, `RELAY` or `UNKNOWN`. |
| `networking.warm_relay() -> Error`, `networking.relay_status_changed(status: SteamRelayStatus)` | Request SDK relay preparation and observe the reported state. |
| `SteamSocketEndpoint.direct(address: String, port: int) -> SteamSocketEndpoint`, `SteamSocketEndpoint.p2p(steam_id: String, virtual_port: int = 0) -> SteamSocketEndpoint` | Select typed IP or Steam identity routing for standalone Sockets. |
| `endpoint.expect_peer(steam_id: String) -> SteamSocketEndpoint` | Derive a direct endpoint that requires a matching Steam-certified remote account. |
| `steam.sockets() -> SteamSockets`, `sockets.listen(endpoint: SteamSocketEndpoint, remote_limit: int = 8) -> Result<SteamSocketListener>`, `sockets.connect_to(endpoint: SteamSocketEndpoint, timeout_ms: int = 10000) -> Operation<SteamSocketConnection>` | Start a bounded listener or asynchronous standalone connection. The method name preserves Godot's inherited signal connection API. |
| `listener.connection_requested(offer: SteamSocketOffer)`, `listener.accept(offer: SteamSocketOffer) -> Result<SteamSocketConnection>`, `listener.reject(offer: SteamSocketOffer) -> Error`, `listener.close() -> Error` | Observe, accept, or reject an expiring offer owned by this listener, and release the listener on close. |
| `connection.is_connected() -> bool`, `connection.steam_id: String`, `connection.connected()` | Observe the certified connection state and read the remote identity once the connection has authenticated. |
| `connection.send(bytes: PackedByteArray, delivery: SteamDelivery = RELIABLE) -> Error`, `connection.path() -> SteamConnectionPath`, `connection.close() -> Error` | Queue bounded bytes on an owned socket and observe or close its path. |
| `connection.received(bytes: PackedByteArray, delivery: SteamDelivery)`, `connection.closed(reason: Error)` | Receive copied bytes from the exact established connection or observe closure. |
| `steam.messages() -> SteamMessages`, `messages.for_peer(peer_id: int) -> Result<SteamMessagePeer>`, `messages.authorize(steam_id: String, channels: Array[StringName]) -> Result<SteamMessagePeer>` | Bind a message peer to an admitted game identity or an explicit standalone allowlist. Standalone authorization is for local application routing. |
| `message_peer.send(channel: StringName, bytes: PackedByteArray, delivery: SteamDelivery = RELIABLE) -> Error`, `message_peer.close() -> Error` | Queue data on an allowed channel or retire the binding. |
| `message_peer.received(channel: StringName, bytes: PackedByteArray, delivery: SteamDelivery)`, `message_peer.closed(reason: Error)` | Observe bounded packets attributed to the authenticated Steam endpoint and binding lifetime. |

`SteamDelivery` has `RELIABLE` and `UNRELIABLE` values. A socket offer exposes `endpoint: SteamSocketEndpoint`, the initial SDK account claim in `steam_id`, and `expires_at_msec: int`. Use the connection's account after `connected()` for authorization. `SteamConnectionPath` exposes `kind`, `ping_ms`, and `packet_loss`, an approximate local in-order delivery deficit with `-1` for an unavailable measurement. `SteamRelayStatus` exposes `availability` and `measuring`.

Use reliable ordered gameplay delivery for admission and baselines, and unreliable delivery for replaceable data. Steam Messages can manage SDK sessions implicitly. Crimson matches any incoming session request against the authorized identity and channel policy before acceptance, bounds packet size and rate, and closes the mapping on departure or epoch change. Standalone Sockets keep their own listeners. Crimson Session retains ownership of its game player roles. [Valve Sockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets) · [Valve Messages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages) · [Valve relay utilities](https://partner.steamgames.com/doc/api/ISteamNetworkingUtils#GetRelayNetworkStatus)

**FPS integration**

The game uses its existing host and join calls for normal play. Show measured connection quality for a verified peer through `steam.networking().peer_path(peer_id)`. Use `steam.messages()` for an explicitly configured side channel, such as an in-match ping carrying timing metadata only.

```gdscript
var path := steam.networking().peer_path(peer_id)
if path.ok:
    scoreboard.show_connection(peer_id, path.value)

var recipient := steam.messages().for_peer(peer_id)
if recipient.ok:
    recipient.value.send(&"latency_probe", timing_bytes, SteamDelivery.UNRELIABLE)
```

**Verification**

- Planned `steam_smoke.gd -- sockets <host|guest>` and `-- messages <host|guest>` run with two signed-in accounts. Check real bidirectional traffic, independent reliable and unreliable cases, rejection of a denied binding, and cleanup after reconnect. Apply local loss and delay for queue and teardown checks. Require actual route diagnostics before recording direct or relayed transport evidence.
- Exercise Steam P2P over a real network with a bounded timeout. Hosted dedicated relay additionally requires the permitted app and Valve deployment profile. An SDK relay-ready result only confirms availability. [Valve Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay)

### Parties and reservations

**Initialization**

Resolve `steam.parties()` under the signed-in client. Choose a location returned by Steam, such as an eligible group chat. Tie a game-managed beacon to the current joinable listing after the host publishes.

**Target API**

| Call or signal | Contract |
| --- | --- |
| `steam.parties() -> SteamParties`, `parties.locations() -> Result<Array[SteamPartyLocation]>`, `parties.beacons() -> Result<Array[SteamPartyListing]>` | Read eligible locations and a bounded snapshot of active visible beacons. |
| `await parties.publish(listing: SteamListing, location: SteamPartyLocation, open_slots: int) -> Result<SteamPartyBeacon>` | Publish one beacon for the owned listing using a bounded connect route. |
| `await parties.reserve(beacon: SteamPartyListing) -> Result<SteamListing>` | Follow the beacon and resolve its join route. Joining still follows the Steam lobby visibility policy and the game's admission rules. |
| `beacon.change_open_slots(slots: int) -> Operation<bool>`, `beacon.complete(reservation: SteamPartyReservation) -> Error`, `beacon.cancel(reservation: SteamPartyReservation) -> Error`, `beacon.close() -> Error` | Update the advertised slots, acknowledge a verified arrival, return a seat, or destroy the owned beacon. |
| `parties.beacons_changed()`, `beacon.reservation_received(reservation: SteamPartyReservation)`, `beacon.closed(reason: Error)` | Observe discovery and the bounded reservation lifecycle. |

A location exposes `id: String` and `kind: SteamPartyLocationKind`. A beacon snapshot exposes `id: String`, `owner_steam_id: String` and `location: SteamPartyLocation`. A reservation exposes `steam_id: String` and `beacon_id: String`. For a game-managed beacon, Crimson matches a reservation to the admitted Steam identity and acknowledges it after the guest reaches the game. The SDK or game scope expires abandoned reservations, and game teardown closes the beacon. [Valve Parties](https://partner.steamgames.com/doc/api/ISteamParties#OnReservationCompleted)

**FPS integration**

A host with open Transfer Hall slots can publish a beacon for a selected group that can join under the current lobby policy. A friend follows it and passes the resulting `SteamListing` to `game.join_listing()`. The game still owns seats, spawn positions, and all authority decisions.

```gdscript
# Host uses its current lobby's listing and an eligible Steam location.
var beacon: SteamPartyBeaconResult = await steam.parties().publish(hosted_listing, selected_location, 2)

# Guest selects the advertised beacon in the party browser.
var reservation: SteamListingResult = await steam.parties().reserve(selected_beacon)
if reservation.ok:
    await game.join_listing(reservation.value).wait()
```

**Verification**

- Planned `steam_smoke.gd -- parties <host|guest>` uses two signed-in accounts with access to the selected Steam location. Observe the beacon in Steam or through the guest query, reserve, mount the guest, confirm the consumed slot, and check cancellation and timeout restore availability. Close the game and confirm the beacon disappears. [Valve beacon lifecycle](https://partner.steamgames.com/doc/api/ISteamParties)

### Publisher Game Notifications

**Initialization**

Boot `SteamPublisher.boot(settings: SteamPublisherSettings) -> Result<SteamPublisher>` on a trusted backend and resolve `publisher.game_notifications()`. Configure the authorized AppID, publisher key from server secret configuration, and Steam-authenticated recipient mapping. Configure the app's localization tokens. The game client reports authenticated game events to its backend.

**Target API**

| Call or signal | Contract |
| --- | --- |
| `publisher.game_notifications() -> SteamGameNotifications` | Resolve the backend service for the configured app. |
| `notifications.request_opt_in(user: SteamIdentity, launch_id: String) -> Operation<bool>` | Request Steam notification registration for the verified user. The backend supplies a stable launch ID and atomically reserves the AppID, account and launch in `SteamNotificationStore`. Steam presents any opt-in UI. |
| `notifications.create(spec: SteamNotificationSessionSpec, operation_key: String) -> Operation<SteamNotificationSession>` | Create an asynchronous game session from a unique backend operation. Specs carry `context: String`, `title: SteamLocalizedText` and `users: Array[SteamNotificationUserState]`. |
| `notifications.update(session_id: String, spec: SteamNotificationSessionSpec, operation_key: String) -> Operation<SteamNotificationSession>` | Apply a bounded state change for a session the backend owns. |
| `notifications.sessions_for(user: SteamIdentity) -> Operation<Array[SteamNotificationSession]>`, `notifications.details(session_id: String) -> Operation<SteamNotificationSession>` | Read the verified user's sessions or a session owned by the app. |
| `notifications.remove(session_id: String, operation_key: String) -> Operation<bool>` | Remove the session and its notifications through an idempotent backend job. |

`SteamNotificationUserState` has `user: SteamIdentity`, `state: SteamNotificationState` and `message: SteamLocalizedText`. States are `WAITING`, `READY` and `DONE`. Localized text uses `token: StringName` and bounded `variables: Dictionary[StringName, String]`. Session results expose the provider session ID and confirmed state. Backend operation completion reports acceptance by Steam. Delivery and opt-in are observed through Steam. [Valve Game Notifications](https://partner.steamgames.com/doc/features/game_notifications) · [Valve publisher endpoints](https://partner.steamgames.com/doc/webapi/IGameNotificationsService)

Use the same backend launch ID through authentication refreshes. Read `SteamApps.notification_session_id()` on startup and connect `notification_session_requested(session_id)` for notification links opened while running. Check the verified player's session membership before resuming. [Trusted notification registration](STEAM_PUBLISHER.md#register-game-notifications-once-per-launch) · [Valve notification launch flow](https://partner.steamgames.com/doc/features/game_notifications#launching_specific_game_sessions)

**FPS integration**

A game-owned asynchronous co-op challenge may set a verified participant to `READY` when the next attempt becomes available, and update that participant to `DONE` when the authoritative attempt ends. Keep active in-match events in the existing game session.

```gdscript
# Trusted backend after the prior Transfer Hall challenge closes.
var notifications := publisher.game_notifications()
var next_attempt: SteamNotificationSessionResult = await notifications.update(challenge_session_id, ready_spec, event_id).wait()
if next_attempt.ok:
    challenge_store.record_notification_state(next_attempt.value)
```

**Verification**

- Planned `steam-publisher-smoke notifications <test-app>` uses an authorized publisher key, configured localization tokens, and opted-in test recipients. Verify server-side identity binding, create/read/update/delete, repeated operation keys, and a visible Steam notification for the deliberate `READY` transition. Check removal in the recipient's client and keep notification text, credentials and account identifiers out of logs. [Valve publisher requirements](https://partner.steamgames.com/doc/features/game_notifications#requirements)

### Steam Voice

**Initialization**

Resolve `steam.voice()` after a signed-in client starts. Bind capture to explicit push-to-talk after player opt-in. The game configures its voice recipients and routes compressed frames through the active session. Steam supplies capture and decoding. Godot owns playback and the game owns its input and accessibility settings. [Valve Voice](https://partner.steamgames.com/doc/features/voice)

**Target API**

| Call or signal | Contract |
| --- | --- |
| `steam.voice() -> SteamVoice`, `voice.capture() -> SteamVoiceCapture`, `voice.decode(bytes: PackedByteArray, sample_rate_hz: int = 0) -> Result<SteamVoicePcm>` | Resolve standalone codec access. Zero requests the codec's recommended sample rate. |
| `capture.start() -> Error`, `capture.stop() -> Error`, `capture.read_frame() -> Result<SteamVoiceFrame>` | Start exclusive microphone capture, stop and allow final frames to drain, or read one bounded compressed frame. An empty poll reports unavailable. |
| `game.voice() -> GameVoice?`, `game_voice.start_talking() -> Error`, `game_voice.stop_talking() -> Error`, `game_voice.set_muted(peer_id: int, muted: bool = true) -> Error` | Resolve the active session's voice binding. Start or stop its authorized capture path or locally mute one player. |
| `game_voice.set_channel(channel: SteamVoiceChannel) -> Error`, `game_voice.talking_changed(peer_id: int, talking: bool)`, `game_voice.frame_received(peer_id: int, pcm: SteamVoicePcm)` | Select a permitted channel and observe attributed talk state or decoded data for Godot playback. |

Frames expose `bytes: PackedByteArray`. PCM exposes `samples: PackedByteArray` in signed 16-bit mono format and `sample_rate_hz: int`. The configured `SteamVoiceChannel` values are `GROUP`, `TEAM`, and `PROXIMITY`. The authority binds voice to the admitted sender and current world epoch. It filters recipients with the game's channel, team or proximity rules, bounds frame sizes and rate, and retires all buffers when peers leave. Voice uses a dedicated unreliable lane and a small playback queue. Muting affects local presentation.

**FPS integration**

In Transfer Hall, push-to-talk defaults to the co-op group. An FPS can supply team or proximity policy based on server-owned actor state. The HUD subscribes to talk-state changes and uses the game's existing audio bus to play PCM for authorized peers.

```gdscript
var voice := game.voice()
if voice != null:
    voice.talking_changed.connect(squad_hud.set_talking)
    push_to_talk.button_down.connect(voice.start_talking)
    push_to_talk.button_up.connect(voice.stop_talking)
```

**Verification**

- Planned `steam_smoke.gd -- voice-policy` covers bounded frames, wrong sender and epoch, forbidden recipients, mute, and cleanup with controlled input. For live proof, use graphical `-- voice <host|guest>` on two signed-in devices with functioning microphones. Verify push-to-talk audio, mute/unmute, no transmission before activation, and silence after release and departure. Confirm that another talker's audio and gameplay remain responsive under controlled loss. [Valve Voice sample guidance](https://partner.steamgames.com/doc/features/voice)

## 3. Social, Overlay, and Community

Obtain the Steam facade once with `var steam := SteamServices.for_game(game)` or the standalone bootstrap. Resolve it from persistent application or menu code. Steam user, lobby, and published Workshop item identifiers are decimal `String` values.

### Steam Overlay

**Initialization**

Use a signed-in Steam launch with the overlay enabled and a rendered game window. For a direct development launch, pass `--crimson-steam-overlay=<AppID>` after Godot's `--` separator so Crimson can initialize Steam before graphics startup.

**Target API**

```text
steam.overlay() -> SteamOverlay
overlay.is_ready() -> bool
overlay.is_active() -> bool
overlay.open_achievements() -> Error
overlay.open_store(app_id: int = 0) -> Error
overlay.open_profile(steam_id: String) -> Error
overlay.open_web(url: String) -> Error
overlay.open_trade(partner_steam_id: String) -> Error
signal overlay.activation_changed(active: bool)
```

`open_store()` defaults to the running App ID and opens its page in browse mode. `open_profile()` uses the supplied user ID. `open_web()` accepts an absolute HTTP or HTTPS link. `open_trade()` requests the trading pane after the trusted backend starts a trade with the selected partner. Each `open_*` call returns `OK` when it issues the request. `ERR_BUSY` covers an overlay still loading, `ERR_UNAVAILABLE` covers missing Steam support, and invalid targets return `ERR_INVALID_PARAMETER`. `activation_changed` reports Steam's overlay state. Check the visible pane to verify a specific request.

**FPS integration**

The pause menu’s Achievements button opens Steam’s achievement view. Suspend gameplay input while Steam has focus and restore it when the overlay closes.

```gdscript
var overlay := steam.overlay()
overlay.activation_changed.connect(pause_menu.set_overlay_active)
achievements_button.pressed.connect(overlay.open_achievements)
```

**Verification**

CLI checks missing Steam, invalid targets, and readiness. In a Steam-launched graphical fixture, request each pane and observe the achievement list, store App ID, profile, or page and the activation and close callbacks. Use a configured test app for pages unavailable under Spacewar. [Steam Overlay](https://partner.steamgames.com/doc/features/overlay) · [Steam Friends interface](https://partner.steamgames.com/doc/api/ISteamFriends)

### Game-scoped Steam invites

**Initialization**

Select Steam as the game's online provider and host with `await game.host_online().wait()`. Once hosting succeeds, the authority has a published Steam lobby. Subscribe from the persistent guest menu so the UI can receive join requests before gameplay mounts.

**Target API**

```text
game.invites() -> GameInvites
invites.is_available() -> bool
invites.open_overlay() -> Error
invites.send(steam_id: String) -> Error
invites.pending() -> SteamInvite?
signal invites.received(invite: SteamInvite)

SteamInvite { listing: SteamListing, from_steam_id: String }
```

`is_available()` reports whether Steam invites are bound to the game. `open_overlay()` requests the published lobby's invite picker. `send()` submits one invitation to the selected account. `OK` means Steam accepted the request. `received` carries an actionable Steam join request. `pending()` lets a newly bound UI read a request received during startup. With **Use launch command line** enabled in Steamworks, Steam can supply a lobby launch argument at startup or through its URL callback while the game runs. The sender ID is empty when unavailable. The game chooses when to call `await game.join_listing(invite.listing).wait()` and can ask for confirmation when joining would replace an active session. The join settles after the local world and player are ready. [Valve launch arguments](https://partner.steamgames.com/doc/api/ISteamApps#GetLaunchCommandLine)

**FPS integration**

An active host opens the invite picker. The guest sees the incoming invitation in the persistent menu and confirms before starting the join.

```gdscript
game.invites().received.connect(multiplayer_menu.offer_invite)
invite_friends_button.pressed.connect(game.invites().open_overlay)

func accept_invite(invite: SteamInvite) -> void:
    multiplayer_menu.show_join_result(await game.join_listing(invite.listing).wait())
```

**Verification**

CLI covers an absent or retired lobby and invalid Steam IDs. Use two authorized Steam test accounts on separate clients. Observe the host's picker, have the guest accept through Steam, and verify the intended lobby ID and readiness in the same world. Check warm and cold guest startup separately. [Lobby invitations](https://partner.steamgames.com/doc/api/ISteamMatchmaking#InviteUserToLobby) · [Steam Friends callbacks](https://partner.steamgames.com/doc/api/ISteamFriends#GameLobbyJoinRequested_t)

### Rich Presence

**Initialization**

Configure localized Rich Presence tokens in Steamworks. Use a signed-in client and one persistent presence owner for the game's state.

**Target API**

```text
steam.presence() -> SteamPresence
presence.show(display_token: StringName, values: Dictionary[String, String] = {},
              party_id: String = "", party_size: int = 0) -> Error
presence.joinable(listing: SteamListing) -> Error
presence.clear_joinable() -> Error
presence.clear() -> Error
steam.friends() -> SteamFriends
friends.list() -> Result<Array[SteamFriend]>
friends.set_rich_presence(key: String, value: String) -> Error
friends.rich_presence(key: String) -> String
friends.clear_rich_presence(key: String) -> Error
```

`show()` issues a localized status token, its replacement values, and optional party grouping. Supply a nonempty party ID and positive party size to group players. Crimson validates Steam's byte and key limits before submitting writes. Steam applies keys individually. `clear()` releases this scope's managed values and preserves unrelated keys. The direct Friends methods set, read, or clear a custom local key outside the managed status. `list()` returns friend snapshots with `steam_id: String`, `display_name: String`, and `online: bool` for a game-owned friend picker.

`joinable()` publishes an opaque provider route for the current owned public or friends-only game listing through Steam’s Join Game action. Ready hosts with room publish by default. The recipient’s persistent menu receives the decoded request through `game.invites().received` and joins through the normal Game operation. The provider checks the listing and current visibility before publishing and again at join. `clear_joinable()` suppresses the current listing's automatic route while keeping the status label. Calling `joinable()` can restore the route. Crimson clears the route when that listing retires. [Steam Friends join requests](https://partner.steamgames.com/doc/api/ISteamFriends#GameRichPresenceJoinRequested_t)

**FPS integration**

Update the status when the world becomes active and when the player returns to the main menu. Scope teardown clears the managed status.

```gdscript
steam.presence().show(&"#InMap", {"map": "Transfer Hall"})
# Use this while the friends-only lobby has room.
steam.presence().joinable(hosted_listing)
# Call this when returning to the menu.
steam.presence().clear_joinable()
steam.presence().show(&"#InMenu")
```

**Verification**

Use an authorized test account to set a temporary status and verify local key readback and scoped cleanup in CLI. From a second account, verify the localized label, party grouping, and Join Game for a joinable lobby. Confirm that the join reaches the intended world and that a retired route is cleared and rejected if reused. The local read proves cache acceptance. The friend-side observation proves Steam displayed the intended text. [Enhanced Rich Presence](https://partner.steamgames.com/doc/features/enhancedrichpresence) · [SetRichPresence](https://partner.steamgames.com/doc/api/ISteamFriends#SetRichPresence)

### Workshop and user-generated content

**Initialization**

Enable Workshop for the App ID and configure an app-owned test item. Use a signed-in account with access for each read, subscription, install, and publishing check.

**Target API**

```text
steam.workshop() -> SteamWorkshop
workshop.query() -> SteamWorkshopQuery
query.search(text: String) -> SteamWorkshopQuery
query.tagged(tag: String) -> SteamWorkshopQuery
query.sort_by(sort: WorkshopSort) -> SteamWorkshopQuery
query.after(cursor: String) -> SteamWorkshopQuery
await query.fetch() -> Result<SteamWorkshopPage>
workshop.subscriptions(limit: int = 128, include_locally_disabled: bool = false) -> SteamWorkshopSubscriptionsResult
await workshop.legal_agreement_status() -> SteamWorkshopLegalAgreementStatusResult
workshop.show_legal_agreement() -> Error

workshop.item(published_id: String) -> SteamWorkshopItemRef
await item.fetch() -> Result<SteamWorkshopItem>
await item.subscribe() -> Result<SteamWorkshopSubscription>
await item.unsubscribe() -> Result<SteamWorkshopSubscription>
await item.install(high_priority: bool = false) -> Result<SteamWorkshopInstall>
item.installed() -> SteamWorkshopInstall?
await item.vote(up: bool) -> Result<SteamWorkshopVote>

workshop.new_item() -> SteamWorkshopPublishRequest
item.edit() -> SteamWorkshopPublishRequest
request.named(title: String) -> SteamWorkshopPublishRequest
request.described_as(description: String) -> SteamWorkshopPublishRequest
request.with_content(directory: String) -> SteamWorkshopPublishRequest
request.with_preview(image_file: String) -> SteamWorkshopPublishRequest
request.tagged(tag: String) -> SteamWorkshopPublishRequest
request.visibility(visibility: WorkshopVisibility) -> SteamWorkshopPublishRequest
await request.submit(change_notes: String = "") -> Result<SteamWorkshopPublication>

signal workshop.install_changed(published_id: String, install: SteamWorkshopInstall?)

WorkshopSort = RELEVANCE | RATING | NEWEST
WorkshopVisibility = PRIVATE | FRIENDS_ONLY | PUBLIC | UNLISTED
SteamWorkshopPage { items: Array[SteamWorkshopItem], next_cursor: String }
SteamWorkshopItem { published_id: String, title: String, description: String, author_steam_id: String }
SteamWorkshopSubscription { published_id: String, subscribed: bool }
SteamWorkshopInstall { published_id: String, directory: String, size_bytes: int }
SteamWorkshopVote { published_id: String, up: bool }
SteamWorkshopPublication { published_id: String, needs_legal_agreement: bool }
```

Queries start on the first page. The default sort is rating for browsing and relevance for text search. Modifiers return branchable queries or publish requests. An empty next cursor marks the end. Subscription success confirms the subscription change. Installation resolves an already-current local copy immediately. Otherwise it settles after Steam reports the matching app and item download and returns local install information.

New items default to private visibility. Updates preserve fields left unset. If creation succeeds and upload or agreement handling interrupts publication, the result preserves the created ID so the author can retry through `item(id).edit()`. On player confirmation, open the agreement through `show_legal_agreement()`. Await the result of `legal_agreement_status()` and check `ok` before reading `value.needs_action` to decide whether to resume. `subscriptions()` copies up to 512 decimal IDs in Steam order and reports whether more were observed. Release query handles and drop update state when the operation or owner ends. [Valve Workshop agreement](https://partner.steamgames.com/doc/api/ISteamUGC#ShowWorkshopEULA)

**FPS integration**

Query community maps and install the selected item. The game validates its content before offering it in the level catalog. The editor can also expose an explicit Publish action.

```gdscript
var maps: SteamWorkshopPageResult = await steam.workshop().query().tagged("Map").fetch()
var installed: SteamWorkshopInstallResult = await steam.workshop().item(selected_map_id).install()
if installed.ok:
    map_catalog.validate_and_add(installed.value.directory)

var published: SteamWorkshopPublicationResult = await steam.workshop().new_item().named("Transfer Hall Redux") \
    .with_content(map_export_directory).tagged("Map").submit()
```

**Verification**

CLI fetches a known ID and a filtered page, then verifies subscription and installation separately on an app-owned hidden item. For authoring, create a private item, update it, and query the same ID to verify the edit. Test voting with a controlled item and account. A graphical fixture shows the returned title and install status and can open the item in Steam. [Workshop implementation](https://partner.steamgames.com/doc/features/workshop/implementation) · [Steam UGC API](https://partner.steamgames.com/doc/api/ISteamUGC)

### Screenshots

**Initialization**

Use a rendered Steam session with screenshots enabled. The built-in capture uses Steam's ordinary screenshot path. An annotated request starts from a detached snapshot of a game-owned `Image`.

**Target API**

```text
steam.screenshots() -> SteamScreenshots
screenshots.capture() -> Error
screenshots.from_image(image: Image) -> SteamScreenshotRequest
request.at(location: String) -> SteamScreenshotRequest
request.tag(steam_id: String) -> SteamScreenshotRequest
request.tag_item(published_id: String) -> SteamScreenshotRequest
await request.submit() -> Result<SteamScreenshot>

signal screenshots.screenshot_ready(screenshot: SteamScreenshot)
SteamScreenshot { annotations_accepted: bool, annotation_errors: Array[Error] }
```

`capture()` issues a Steam screenshot request. Observe `screenshot_ready` after Steam reports a saved image. Steam's hotkey can produce the same event. Use an annotated submission for an exact result. `submit()` writes the owned image and settles when Steam reports the exact returned native handle ready. Crimson then applies the requested location and tags to that same image. Annotation fields report partial failures separately from saving the image. The native handle stays inside the screenshot object and expires with its Steam scope.

**FPS integration**

A Capture Screenshot button requests the ordinary Steam capture. Photo mode can submit a frame with the map location and a selected player tag.

```gdscript
steam.screenshots().capture()

var photo: SteamScreenshotResult = await steam.screenshots().from_image(frame) \
    .at("Transfer Hall").tag(selected_steam_id).submit()
```

**Verification**

Render a known frame, invoke `capture()`, and observe the saved callback and local library image. Separately submit an annotated frame and verify the correlated result, map location, and any tags using controlled accounts and items. Check the local library before any upload or sharing. [Steam Screenshots API](https://partner.steamgames.com/doc/api/ISteamScreenshots)

### Remote Play Together

**Initialization**

Enable Remote Play Together for the Steam app and start a rendered host through Steam. The game defines local seats and input routing. A second authorized device or account is needed to exercise streaming and guest input.

**Target API**

```text
steam.remote_play() -> SteamRemotePlay
remote_play.sessions() -> Array[SteamRemotePlaySession]
remote_play.open_together_overlay() -> Error
remote_play.send_invite(steam_id: String) -> Error
remote_play.set_separate_input(enabled: bool) -> Error
remote_play.poll_input(limit: int = 64) -> Array[SteamRemotePlayInputEvent]

signal remote_play.session_connected(session: SteamRemotePlaySession)
signal remote_play.session_disconnected(session_id: int)
SteamRemotePlaySession { session_id: int, steam_id: String, client_size: Vector2i, together: bool }
SteamRemotePlayInputEvent { session_id: int, event: InputEvent }
```

Opening the overlay and sending an invite report submission through `Error`. The session signals and query report actual stream connections. A guest without an available Steam ID uses an empty string. `client_size` is zero when the device has supplied no resolution. Steam merges supported remote input into the host by default. Opt in to separate keyboard and mouse input for per-seat routing and poll a bounded batch. Restore merged input when the owning scope stops.

**FPS integration**

The local co-op menu opens Remote Play Together. Assign a local seat and input source when a stream connects and release it on disconnect. Remote players use the host's local player and camera layout.

```gdscript
var remote_play := steam.remote_play()
remote_play.session_connected.connect(local_coop.assign_remote_seat)
remote_play.session_disconnected.connect(local_coop.release_remote_seat)
remote_play.open_together_overlay()
```

**Verification**

CLI verifies availability and enumeration, including a valid empty list. Open the Steam panel in a rendered app configured for Remote Play Together. Connect a second device and observe the connection, resolution when provided, and input reaching the assigned local seat. Verify that disconnect releases the seat. [Steam Remote Play API](https://partner.steamgames.com/doc/api/ISteamRemotePlay)

## 4. Hardware, Input, and Media

### Steam Input

**Initialization**

Call `steam.input().start()` once from the persistent graphical client. Publish the app’s action sets and controller layouts in Steamworks, or bundle a development action manifest and pass its absolute on-disk path. Crimson leases the process Steam Input interface and updates it from the shared frame pump. The game assigns discovered controllers to local players. A Steam controller handle and a Godot joypad index represent separate identities.

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.input().start(manifest_path: String = "") -> Error` | Acquires Steam Input for the client scope. The empty path uses app configuration. An explicit path names an unpacked action manifest. A conflicting active manifest returns `ERR_BUSY`. |
| `steam.input().stop() -> void` | Releases this scope’s input lease and invalidates its controller views. The runtime shuts down input after the last lease ends. |
| `steam.input().controllers() -> Array[SteamController]` | Lists Steam Input enabled controllers currently connected. The list is empty before input starts. |
| `SteamController.is_connected() -> bool` | Reports whether this opaque controller still represents the same live device. Reconnection can produce a fresh view. |
| `SteamController.type() -> SteamControllerType` | Returns Steam’s device family, with `UNKNOWN` for an unsupported type. |
| `SteamController.use_action_set(name: StringName) -> Error` | Selects a manifest action set such as `Gameplay` or `Menu` for this controller. |
| `SteamController.activate_layer(name: StringName) -> Error` / `deactivate_layer(name: StringName) -> Error` | Adds or removes a temporary binding layer such as scoped aim. |
| `SteamController.digital(action: StringName) -> Result<SteamDigitalInput>` | Reads `{active, pressed}` for the current frame. An inactive binding reports `active = false`. An unknown action or disconnected controller gives an error. |
| `SteamController.analog(action: StringName) -> Result<SteamAnalogInput>` | Reads `{active, value, mode}`. `value` is a `Vector2`. `mode` distinguishes the action’s input source mode so the game can interpret axes or relative motion. |
| `SteamController.glyph(action: StringName, size: SteamGlyphSize = MEDIUM) -> Result<Texture2D>` | Returns the glyph for the first effective binding origin in the current action set and layers. An unbound action returns an unavailable result so the HUD can use its own fallback. |
| `SteamController.glyphs(action: StringName, size: SteamGlyphSize = MEDIUM) -> Result<Array[Texture2D]>` | Returns glyphs for all reported origins. Crimson loads and caches the Steam glyph textures within the scope. |
| `SteamController.open_binding_panel() -> Error` | Requests Steam’s configuration panel for this controller. Success means the request was issued. |
| `SteamController.vibrate(low: float, high: float) -> Error` | Submits normalized low- and high-motor values from `0.0` to `1.0` where supported. Zero clears the game’s request. |

`input.controller_connected(controller)` and `input.controller_disconnected(controller)` update local player assignment. `input.bindings_changed(controller)` tells the HUD to refresh prompts after rebinding. Steam Input supplies the controller path for that local player. The game can use Godot input for keyboard and mouse and for controllers outside that assignment.

**FPS integration**

```gdscript
if steam.input().start() == OK:
    var controllers := steam.input().controllers()
    if not controllers.is_empty():
        var controller := controllers[0]
        controller.use_action_set(&"Gameplay")
        hud.bind_reload_prompt(controller.glyph(&"reload"))
        local_player_input.use_steam_controller(controller)
```

The game’s input mapper reads `move`, `look`, and `reload` from that controller. It converts the readings into the same local movement and command requests as keyboard input. Game code switches to `Menu` when the pause screen takes focus.

**Verification**

A GUI fixture uses a signed-in Steam client, a configured action manifest, and a physical controller. Observe move and look input, the gameplay-to-menu action-set change, the reload glyph before and after rebinding, and prompt removal on disconnect. Verify an unbound action and an unavailable input interface without hardware. [Steam Input API](https://partner.steamgames.com/doc/api/ISteamInput) · [In-game actions](https://partner.steamgames.com/doc/features/steam_controller/iga_file)

### Steam Deck, Proton, and text input

**Initialization**

Use `steam.device()` after client boot. In a graphical client, invoke a keyboard from a focused control in the active window. Crimson allows one Steam keyboard request per client process because the SDK reports dismissal globally.

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.device().is_available() -> bool` | Reports whether the client scope can read Steam Utils. |
| `steam.device().hardware() -> SteamHardware` | Returns Steam’s reported hardware family or `UNKNOWN` for an unknown or unavailable value. |
| `steam.device().is_steam_deck() -> bool` | Tests for the reported Steam Deck hardware family. |
| `steam.device().is_proton() -> bool` | Reads Steam’s Proton flag when supported. Use `is_available()` to check the device service before drawing a diagnostic conclusion. |
| `steam.device().recommended_preset() -> SteamDevicePreset` | Returns Steam’s device configuration hint or `UNKNOWN`. The game maps the hint to its own initial settings and honors saved player settings. |
| `steam.device().show_keyboard_for(field: Control, mode: SteamFloatingKeyboardMode = SINGLE_LINE) -> Error` | Opens the floating keyboard for an active `LineEdit` or `TextEdit`. Crimson derives the field bounds in game-window pixels. Steam sends OS key events to the focused Godot control. Choose `SINGLE_LINE`, `MULTILINE`, `EMAIL`, or `NUMERIC`. |
| `await steam.device().enter_text(prompt: String, initial: String = "", max_chars: int = 64, multiline: bool = false, hidden: bool = false) -> Result<SteamTextEntry>` | Opens Steam’s modal gamepad dialog where available. The result carries `{submitted, text}`. Dismissing without submission yields `submitted = false` and empty text. `hidden` masks the entered text. |
| `steam.device().dismiss_keyboard() -> Error` | Requests dismissal of the keyboard owned by this scope. The owning modal request settles when Steam reports dismissal or the scope closes. |

`device.keyboard_dismissed()` reports the floating keyboard closing. The field owns the text and normal Godot submit event. The modal path resolves the text from its own result. A second keyboard request receives `ERR_BUSY` while a dialog is active.

**FPS integration**

```gdscript
func _on_server_search_focused() -> void:
    steam.device().show_keyboard_for(server_search)
```

The server browser filters results from the search field’s normal text-change signal. The first-launch settings screen maps the device hint to a Transfer Hall graphics preset.

**Verification**

Run a CLI fixture for hardware, preset, Proton, and SDK-unavailable results. Run a GUI fixture on a Steam Deck and a Windows build launched through Proton to verify keyboard placement, focus, entered text, dismissal, and a second request while the first is active. Record the actual device and runtime. [Steam Utils](https://partner.steamgames.com/doc/api/ISteamUtils)

### SteamVR and OpenXR

**Initialization**

Enable OpenXR in the Godot project settings at launch, author an XR rig and OpenXR action map, and select SteamVR as the installed OpenXR runtime for the SteamVR test. Steam client boot adds optional platform state. Godot owns session creation, rendering, and XR input.

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.device().is_running_in_vr() -> bool` | Reads Steam’s VR launch state when Steam Utils is available. |
| `steam.device().is_headset_streaming_enabled() -> bool` | Reads Steam’s headset-streaming flag where supported. |
| `steam.device().open_vr_dashboard() -> Error` | Requests Steam’s VR dashboard in a supported SteamVR session. `OK` acknowledges the request. |
| `XRServer.find_interface("OpenXR") -> XRInterface` | Uses Godot’s existing API to resolve the OpenXR interface. Call `is_initialized()` and enable `Viewport.use_xr` on a compatible viewport. |

Use Godot’s `session_begun`, `session_stopping`, and `session_loss_pending` signals for the game’s XR lifecycle.

**FPS integration**

```gdscript
var xr := XRServer.find_interface("OpenXR") as OpenXRInterface
if xr and xr.is_initialized():
    get_viewport().use_xr = true
    xr.session_stopping.connect(pause_vr_training)
```

The optional Transfer Hall training scene chooses its XR pawn when Godot initializes OpenXR. Its desktop path keeps the regular FPS rig.

**Verification**

A GUI fixture with a headset and SteamVR configured observes the training scene in the headset, tracked input, and a session-stop transition. A desktop check exercises the regular camera when OpenXR is unavailable. [Godot OpenXR setup](https://docs.godotengine.org/en/4.7/tutorials/xr/setting_up_xr.html) · [OpenXR interface](https://docs.godotengine.org/en/4.7/classes/class_openxrinterface.html) · [Steam Utils](https://partner.steamgames.com/doc/api/ISteamUtils)

### HTML Surface

**Initialization**

Opt in from the graphical UI by opening a game-owned URL. Crimson initializes Steam HTML on first use, owns the browser handle, and pumps readiness and frame callbacks. HTTPS is required by default and main-frame navigation stays on the initial origin unless the game extends the allowlist. The CLI fixture can explicitly enable loopback HTTP for its local page.

**Target API**

| Signature | Behavior |
| --- | --- |
| `await steam.browser().open(url: String, size: Vector2i = Vector2i(960, 540)) -> Result<SteamBrowserPage>` | Creates the browser and issues initial navigation. The result is ready for input and display wiring. A page event and first frame confirm that content has arrived. |
| `steam.browser().allow_origins(origins: PackedStringArray) -> SteamBrowserRequest` | Returns a branchable navigation policy for an upcoming page. Use the same `await request.open(url, size)` terminal. Each origin is an explicit scheme, host, and optional port. |
| `await SteamBrowserRequest.open(url: String, size: Vector2i = Vector2i(960, 540)) -> Result<SteamBrowserPage>` | Opens the page with the selected policy. The lifecycle and result are the same as the default `open()`. |
| `SteamBrowserPage.texture() -> Texture2D` | Returns the owned Godot texture. Crimson copies bounded callback pixels and updates this texture on the main thread. |
| `SteamBrowserPage.resize(size: Vector2i) -> Error` | Changes the render size within the configured pixel budget. |
| `SteamBrowserPage.navigate(url: String) -> Error` | Issues a navigation request under the page’s original policy. |
| `SteamBrowserPage.set_focused(focused: bool) -> Error` | Gives or releases keyboard focus for the page. |
| `SteamBrowserPage.send_input(event: InputEvent) -> Error` | Forwards supported keyboard, mouse, and wheel input while the UI owns focus. Mouse positions are browser-local pixels. |
| `SteamBrowserPage.close() -> void` | Releases the browser. This is idempotent and scope teardown also closes the page. |

The page emits `page_finished(url, title)`, `frame_updated()`, and `navigation_blocked(url)`. Crimson answers Valve’s navigation and dialog callbacks on every path. Popups, file chooser, downloads, and native script bridges stay disabled. The navigation policy governs main-frame requests, so the game supplies pages and assets from a trusted source.

**FPS integration**

```gdscript
var opened: SteamBrowserPageResult = await steam.browser().open(field_manual_url)
if opened.ok:
    var page := opened.value as SteamBrowserPage
    briefing_panel.show_page(page, page.texture())
```

The briefing panel forwards pointer input only while the player is interacting with the terminal. It closes the page when the briefing ends.

**Verification**

Use an isolated signed-in CLI-driven GUI fixture with a deterministic local page. Observe a known texture frame, click and scroll behavior, an outside-origin navigation rejection, and handle release on panel exit. Verify an SDK-unavailable result in the headless lane. [Steam HTML Surface](https://partner.steamgames.com/doc/api/ISteamHTMLSurface)

### Steam Music

**Initialization**

Use `steam.music()` after graphical client boot. The installed Steamworks 1.65 SDK includes the interface. The client still needs to expose it and enable playback. Gate the optional controls on `status()` and treat live playback as a separate capability check.

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.music().status() -> Result<SteamMusicStatus>` | Reads `{enabled, playback, volume}`. `playback` reports `PLAYING`, `PAUSED`, `IDLE`, or `UNKNOWN`. `volume` is normalized to `0.0` through `1.0`. |
| `steam.music().play() -> Error` / `pause() -> Error` | Issues resume or pause for the Steam player’s current queue. |
| `steam.music().next() -> Error` / `previous() -> Error` | Requests a track change within that queue. |
| `steam.music().set_volume(level: float) -> Error` | Submits a normalized volume. A missing or disabled runtime returns `ERR_UNAVAILABLE` for these controls. |

`music.playback_changed()` and `music.volume_changed(level)` reflect Steam callbacks. A control acknowledgment means the command was issued.

**FPS integration**

```gdscript
func _on_pause_steam_music_pressed() -> void:
    steam.music().pause()
```

The pause menu displays this opt-in button only when the Steam player is enabled. The FPS soundtrack and sound effects remain in the game’s Godot audio buses.

**Verification**

Verify the SDK-off and disabled-client result from the CLI. Use a Steam client with active, controllable playback to observe pause, resume, track, and volume changes in a GUI fixture. Header availability alone supports only the interface check. [Steam Music API](https://partner.steamgames.com/doc/api/ISteamMusic)

### Steam Video and broadcast status

**Initialization**

Use `steam.video()` on a signed-in graphical client. Stream lookup requires a processed Steam video AppID the account can access. The installed SDK exposes the interface. Crimson returns the media descriptor for the game’s chosen player.

**Target API**

| Signature | Behavior |
| --- | --- |
| `await steam.video().stream_url(video_app_id: int) -> Result<String>` | Returns the Steam-issued stream URL when available and entitled. Treat it as temporary and keep it out of logs. |
| `await steam.video().projection_settings(video_app_id: int) -> Result<String>` | Retrieves the available 360° video projection settings for the selected video. |
| `steam.video().broadcast_status() -> Result<SteamBroadcastStatus>` | Reads `{uploading, viewers}` for the local client. `viewers` is meaningful during an active upload. |

`video.broadcast_started()` and `video.broadcast_stopped(reason)` report Steam’s upload state changes. Stream lookups correlate callbacks by video AppID and share the result of a pending identical query.

**FPS integration**

```gdscript
var clip: SteamStringResult = await steam.video().stream_url(training_video_app_id)
if clip.ok:
    training_player.open(clip.value)
else:
    briefing_panel.show_local_training_clip()
```

`training_player` is the game’s DASH-capable player adapter. The streaming HUD can also read broadcast state to toggle a local indicator.

**Verification**

Check denied and invalid video IDs through the CLI and exercise an entitled video AppID in a GUI fixture to observe playback. Run a separate real Steam broadcast to observe start, stop, and viewer state. Record unsupported or unconfigured runtime results separately from playback success. [Steam Video API](https://partner.steamgames.com/doc/api/ISteamVideo)

## 5. Economy, Inventory, and Monetization

### Steam Inventory

**Initialization**

Enable Steam Inventory Service for the game's AppID. Publish ItemDefs for the FPS cosmetics, consumables, promotions, drop generators, and exchange recipes it actually uses. Acquire `steam.inventory()` from the running client scope. Loading definitions and fetching inventory are lazy and have separate completion states. For match-earned grants, configure an Economy-authorized publisher key on the trusted backend and acquire `publisher.inventory()` there. Steam owns the persistent instances. Crimson can project verified instances into its equipment and inventory models. [Steam Inventory setup](https://partner.steamgames.com/doc/features/inventory) · [Item definition schema](https://partner.steamgames.com/doc/features/inventory/schema)

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.inventory() -> SteamInventory` | Resolves the current account's app-scoped inventory service. |
| `await SteamInventory.definitions() -> Result<Array[SteamItemDefinition]>` | Loads and returns known definitions, including localized names, types, and configured properties. |
| `await SteamInventory.definition(definition_id: int) -> Result<SteamItemDefinition>` | Loads definitions as needed and returns one definition or a missing-definition error. |
| `await SteamInventory.items() -> Result<SteamInventorySnapshot>` | Returns a detached snapshot of the signed-in account. The result identifies whether it reflects a fresh response or cached data. |
| `await SteamInventory.items_by_id(instance_ids: PackedStringArray) -> Result<SteamInventorySnapshot>` | Reads selected instances owned by the signed-in account. |
| `await SteamInventory.claim_promotions() -> Result<SteamInventoryChange>` | Requests all configured promotions Steam judges the account eligible to receive. A successful empty result is valid. |
| `await SteamInventory.claim_promotion(definition_id: int) -> Result<SteamInventoryChange>` | Requests one configured promotion and reports what Steam granted. |
| `await SteamInventory.claim_playtime_drop(generator_id: int) -> Result<SteamInventoryChange>` | Asks Steam to evaluate the configured playtime drop generator. A successful evaluation may grant zero items. |
| `await SteamInventory.craft(output_definition_id: int, ingredients: Array[SteamItemQuantity]) -> Result<SteamInventoryChange>` | Atomically exchanges owned inputs for one unit of the output if Steam recognizes the recipe. An invalid recipe changes neither side. |
| `await SteamInventory.consume(instance_id: String, quantity: int = 1) -> Result<SteamInventoryChange>` | Permanently consumes a positive quantity after the player confirms the action in game UI. |
| `await SteamInventory.split(instance_id: String, quantity: int) -> Result<SteamInventoryChange>` | Moves a positive quantity from an owned stack to a new instance and returns both changes. |
| `await SteamInventory.merge(from_instance_id: String, into_instance_id: String, quantity: int) -> Result<SteamInventoryChange>` | Moves a positive quantity between owned stacks with the same definition. Steam applies trade and market restrictions. |
| `await SteamInventory.proof(instance_ids: PackedStringArray) -> Result<PackedByteArray>` | Retrieves and serializes a signed result for only the selected owned instances. |
| `server.inventory_verifier() -> SteamInventoryVerifier` | Resolves the verifier from the authenticated Steam game server scope. |
| `await SteamInventoryVerifier.verify(proof: PackedByteArray, expected_steam_id: String) -> Result<SteamInventorySnapshot>` | Available to the authority with an appropriate Steam runtime. Verifies the signature, binds it to the authenticated owner, and rejects an expired or stale proof for equipment decisions. |
| `SteamInventory.definitions_changed()` signal | Fires after Steam announces a new definition snapshot. Refresh open catalog views. |
| `SteamInventory.items_changed(snapshot: SteamInventorySnapshot)` signal | Fires when the service accepts a newer full inventory view. Individual mutation results carry their own changes. |
| `SteamPublisher.inventory() -> SteamPublisherInventory` | Resolves the trusted, app-scoped inventory backend. |
| `await SteamPublisherInventory.items(player: SteamIdentity) -> Result<SteamInventorySnapshot>` | Fetches inventory for an account the backend is authorized to inspect. |
| `await SteamPublisherInventory.grant(player: SteamIdentity, definition_ids: PackedInt32Array, request_id: String) -> Result<SteamInventoryChange>` | Grants definitions after server-side rules authorize the award. Repeat the same decimal request ID for retries of this award to avoid duplicate grants. |
| `await SteamPublisherInventory.consume(player: SteamIdentity, instance_id: String, quantity: int, request_id: String) -> Result<SteamInventoryChange>` | Consumes an owned quantity for a trusted action using the same stable request ID on retry. |
| `await SteamPublisherInventory.start_trade(player: SteamIdentity, partner: SteamIdentity) -> Result<SteamTradeSession>` | Requests a Steam trade for two verified participants in an eligible Economy app. The result identifies the two accounts and acknowledges the request to start the session. The participants confirm the transfer in Steam. |

`SteamItemDefinition` exposes `definition_id: int`, `display_name: String`, `type: StringName`, and `properties: Dictionary[String, String]` for app-defined keys. `SteamItemInstance` exposes `instance_id: String`, `definition_id: int`, `quantity: int`, and `flags: int`. `SteamItemQuantity` pairs `instance_id: String` with a positive `quantity: int`. `SteamInventorySnapshot` exposes `items: Array[SteamItemInstance]`, `freshness: SteamInventoryFreshness`, and `observed_at_unix: int`. `SteamInventoryChange` exposes `granted: Array[SteamItemInstance]`, `changed: Array[SteamItemInstance]`, and `removed_instance_ids: PackedStringArray`. A timeout after dispatch can leave the mutation outcome unknown. Refresh the snapshot before retrying a client mutation.

Player-to-player offers and eligible market listings use Steam's Community trading interface. Item eligibility follows the app's schema and Steam's restrictions. An eligible Economy app can start a trade from its trusted backend and open the associated pane with `steam.overlay().open_trade(friend_id)`. Refresh both inventories after Steam completes the transfer. The API exposes signed proof of equipment for multiplayer and trusted grants for match rewards. [Inventory SDK](https://partner.steamgames.com/doc/api/ISteamInventory) · [Publisher Inventory API](https://partner.steamgames.com/doc/webapi/IInventoryService) · [Steam Economy trade sessions](https://partner.steamgames.com/doc/webapi/ISteamEconomy#StartTrade)

**FPS integration**

At the Transfer Hall armory, the player chooses scrap and crafts a cosmetic receiver finish. The armory updates its view from the accepted change. In multiplayer, the server verifies a signed proof for the selected finish before replicating the cosmetic.

```gdscript
var inventory := steam.inventory()
var crafted: SteamInventoryChangeResult = await inventory.craft(ARMORY_RECEIVER_FINISH, selected_scrap)
if crafted.ok:
    armory.apply_inventory_change(crafted.value)

var equipped: SteamBytesResult = await inventory.proof(selected_cosmetic_instance_ids)
if equipped.ok:
    loadout.send_cosmetic_proof(equipped.value)

# The game backend starts the Steam session after both players accept.
if trade_session_ready:
    steam.overlay().open_trade(friend_id)
```

**Verification**

Run the CLI fixture with a controlled AppID, an entitled test account, and configured test definitions. Verify definitions and snapshot reads, an eligible promo, an exchange that consumes the exact input and yields one finish, and an invalid exchange that leaves the same contents. Verify split, merge, and consume quantities. Reject stale proofs and proofs from another account. Retry a backend grant with the same request ID and verify one award. For a trade, use two test accounts and Steam's trade UI, then compare both inventories after the authorized transfer.

### Microtransactions and Steam Wallet

**Initialization**

Enable Steam microtransactions on the game's AppID and configure the game's purchasing backend with that AppID, a publisher key, a priced catalog, and persistent order and fulfillment storage. Bind an authenticated `SteamCommerceBackend` adapter with `SteamProvider.with_commerce_backend(backend)` before boot. The client resolves `steam.purchases()` through that binding. Steam supplies the authorization UI and supported currency for the user. The backend selects prices and grants. Select Valve's sandbox interface in backend test configuration. [Microtransactions implementation guide](https://partner.steamgames.com/doc/features/microtransactions/implementation) · [Sandbox API](https://partner.steamgames.com/doc/webapi/ISteamMicroTxnSandbox)

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.purchases() -> SteamPurchases` | Resolves the configured client purchasing service. Calls requiring a commerce adapter return unavailable when the app has no binding. |
| `await SteamPurchases.catalog() -> Result<SteamPurchaseCatalog>` | Reads the backend's purchasable SKUs and localized currency totals for the authenticated player. |
| `await SteamPurchases.checkout(cart: SteamPurchaseCart) -> Result<SteamPurchaseOutcome>` | Begins a backend order and invokes Steam's approval flow. Returns a confirmed outcome, a declined outcome, or a recoverable pending order when confirmation remains outstanding. |
| `await SteamPurchases.order(order_id: String) -> Result<SteamPurchaseOutcome>` | Refreshes a previously started order for the current authenticated account, including after reconnecting. |
| `SteamProvider.with_commerce_backend(backend: SteamCommerceBackend) -> SteamProvider` | Returns a new provider configuration with the client commerce adapter selected. |
| `SteamPurchases.order_changed(outcome: SteamPurchaseOutcome)` signal | Delivers observed order transitions to the menu. A Steam overlay approval alone means the player authorized the transaction. |
| `SteamCommerceBackend._catalog(request: SteamCommerceRequest)` | Virtual adapter hook for fetching the authenticated account's catalog and current prices. Complete the request with `complete_catalog(result)`. |
| `SteamCommerceBackend._begin(cart: SteamPurchaseCart, checkout_id: String, request: SteamCommerceRequest)` | Virtual hook to create or resume one order for the same checkout ID in the authenticated session. The server assigns the order ID. Echo the checkout ID in the result supplied to `complete_order(result)`. |
| `SteamCommerceBackend._observe_authorization(order_id: String, authorized: bool, request: SteamCommerceRequest)` | Virtual hook that tells the backend to check the order with Steam. Complete with the typed backend receipt. The backend verifies order state before finalizing. |
| `SteamCommerceBackend._order(order_id: String, request: SteamCommerceRequest)` | Virtual hook to fetch the current order and entitlement state from the trusted backend. Complete with `complete_order(result)`. |
| `SteamPublisher.purchases() -> SteamPublisherPurchases` | Resolves the publisher-scoped purchasing service on the trusted backend. |
| `await SteamPublisherPurchases.buyer_info(player: SteamIdentity) -> SteamPurchaseBuyerInfoResult` | Reads Wallet country, currency, region and account purchasing status for the publisher-verified buyer before pricing. |
| `await SteamPublisherPurchases.begin(player: SteamIdentity, cart: SteamPurchaseCart, checkout_id: String) -> Result<SteamPurchaseOutcome>` | Validates the verified player's account and cart against the server catalog, persists an order, then requests Steam authorization for that order. |
| `await SteamPublisherPurchases.query(order_id: String) -> Result<SteamPurchaseOutcome>` | Reads Steam's current transaction state for a stored order and checks it against the game's order record. |
| `await SteamPublisherPurchases.finalize(order_id: String) -> Result<SteamPurchaseOutcome>` | Confirms an approved order with Steam and applies fulfillment once against the persisted order. Reconciles an ambiguous reply before any retry. |
| `await SteamPublisherPurchases.reconcile(since_unix: int, max_results: int = 1000) -> Result<SteamPurchaseReconciliation>` | Processes up to 1,000 settlement updates, including refunds and reversals, through idempotent game fulfillment and entitlement updates. |
| `await SteamPublisherPurchases.refund(order_id: String) -> Result<SteamPurchaseOutcome>` | Requests a publisher-authorized refund and records the outcome for settlement reconciliation. |

`SteamPurchaseCart` carries SKU IDs and positive quantities. Catalog entries carry SKU, display name, integer amount and currency code. The backend maps amounts to Steam's currency-specific units and increments. `SteamPurchaseOutcome` carries the decimal order ID, currency and total, and a state such as `AWAITING_APPROVAL`, `DECLINED`, `RECONCILING`, `FULFILLED`, or `REVERSED`. `SteamPurchaseReconciliation` reports processed settlement events and the next reconciliation cursor. The backend checks the app, account, order and state, then fulfills idempotently by order ID. It associates callbacks with the AppID and known order ID and derives the account from an authenticated session. The UI shows the purchased entitlement only once fulfillment is confirmed. A client timeout or scope close can end local waiting while the server continues reconciling the same order. [Steam MicroTxn Web API](https://partner.steamgames.com/doc/webapi/ISteamMicroTxn)

**FPS integration**

The armory uses the same checkout action for a weapon finish or a bundle. It displays the server price and requests Steam approval when the player confirms the cart. A pending receipt stays in the purchases screen for a later refresh.

```gdscript
var provider := SteamProvider.configure().with_commerce_backend(shop_backend)
var game := demo_definition.with_provider(provider).boot(self)
var steam := SteamServices.for_game(game)
var purchases := steam.purchases()
var receipt: SteamPurchaseOutcomeResult = await purchases.checkout(confirmed_cart)
if receipt.ok:
    armory.render_receipt(receipt.value)
```

**Verification**

Use the sandbox with a permitted AppID, test account and configured purchasing server. In a graphical Steam build, approve one purchase and decline another. Observe one completed entitlement, no entitlement for the decline, and one fulfillment across replayed authorization callbacks and reconnects. In the CLI backend fixture, inject a lost response after finalization, then reconcile the stored order and verify the same entitlement stays single. Exercise reversal handling from a test settlement event. The sandbox runs simulated transactions without charging the player. [Steam MicroTxn Sandbox](https://partner.steamgames.com/doc/webapi/ISteamMicroTxnSandbox)

### DLC and app information

**Initialization**

Acquire `steam.apps()` from the running client scope. Configure known DLC AppIDs, their depots and optional installs in Steamworks. Configure game languages and any private beta in Steamworks. The game's map catalog maps known DLC AppIDs to content it can mount. Online admission or paid equipment decisions use the server's verified Steam identity and entitlement. [Steam DLC](https://partner.steamgames.com/doc/features/dlc) · [Ownership verification](https://partner.steamgames.com/doc/features/auth#ownership-verification)

**Target API**

| Signature | Behavior |
| --- | --- |
| `steam.apps() -> SteamApps` | Resolves the current app's client metadata service. |
| `SteamApps.is_available() -> bool` | Reports whether the current client scope has the Steam Apps interface. |
| `SteamApps.is_subscribed_to_app(app_id: int) -> bool` | Reads the local Steam subscription for a related AppID. |
| `SteamApps.owns_dlc(dlc_app_id: int) -> bool` | Reads local entitlement for a DLC AppID configured by the game. Use as a UI hint. |
| `SteamApps.is_dlc_installed(dlc_app_id: int) -> bool` | Reports whether the local player owns the DLC and its content is installed. |
| `SteamApps.dlc() -> Result<Array[SteamDlcInfo]>` | Lists Steam-visible DLC for this app. Steam can cap the enumeration, so the game keeps its own catalog of required content. |
| `SteamApps.install_dlc(dlc_app_id: int) -> Error` | Requests installation for an owned optional DLC. `OK` means the request was issued. |
| `SteamApps.uninstall_dlc(dlc_app_id: int) -> Error` | Requests removal of optional DLC after the player confirms. |
| `SteamApps.download_progress(dlc_app_id: int) -> Result<SteamDlcProgress>` | Returns an active flag and byte progress for an active download. An inactive result carries no active byte counts. |
| `SteamApps.beta_branch() -> String` | Returns the selected beta name or an empty string for the default branch. |
| `SteamApps.build_id() -> int` | Returns the Steam build ID or `0` for a build launched outside a Steam download. |
| `SteamApps.current_language() -> String` | Returns the app language selected by Steam for this user. |
| `SteamApps.available_languages() -> PackedStringArray` | Lists the languages configured by the app in Steamworks. |
| `SteamApps.notification_session_id() -> String`, `SteamApps.notification_session_requested(session_id: String)` signal | Read the Steam Game Notifications session selected at launch or observe a new one while running. Validate membership with the trusted backend before resuming. |
| `SteamApps.dlc_installed(dlc_app_id: int)` signal | Fires when Steam confirms DLC installation. Refresh the map catalog before offering the content. |

`SteamDlcInfo` carries the DLC AppID, display name, and Steam's availability flag. `SteamDlcProgress` carries active status, bytes downloaded, and total bytes when known. Scalar client queries return their documented empty or false value if the client is unavailable. Check `is_available()` when the UI needs to distinguish that state. Server-side ownership verification belongs to the authentication API. [Steam Apps SDK](https://partner.steamgames.com/doc/api/ISteamApps)

**FPS integration**

The campaign screen offers an owned optional map pack for download. When Steam reports the map pack installed, the game checks that the local map data is available before offering the mission.

```gdscript
var apps := steam.apps()
if apps.is_dlc_installed(TRANSFER_HALL_NIGHT_DLC) and mission_catalog.has_night_map():
    mission_menu.enable_mission("transfer_hall_night")
elif apps.owns_dlc(TRANSFER_HALL_NIGHT_DLC):
    mission_menu.offer_install(TRANSFER_HALL_NIGHT_DLC)
```

**Verification**

Read language and build metadata from a signed-in Spacewar run. Use an authorized test app and two entitlement states to distinguish unowned, owned but absent, and installed DLC. In Steam, start an optional depot download and verify progress and the installation callback. Boot once on the default branch and once on a named private branch. Verify the named branch and build ID, then reject unauthorized access to an entitlement-gated match through the server.

## 6. Security, Authentication, and Diagnostics

### Identity and ownership tickets

**Initialization**

The current Steam listen provider checks the connection’s Steam identity and lobby membership. The target API adds native session-ticket validation to Steam listen and dedicated hosting. Admission waits for the matching Steam validation callback and the game applies its rules before spawning the peer. A separate profile service can use Web API or encrypted app tickets. Configure the same AppID and service audience on the trusted backend and the client. Load the Web API credential or encrypted ticket key through the backend’s `SteamPublisherSettings`. [Steam authentication and ownership](https://partner.steamgames.com/doc/features/auth)

**Target API**

| Function or signal | Behavior |
| --- | --- |
| `session.get_peer_identity(peer_id: int) -> SessionIdentity?` | Reads the provider-verified identity for an admitted connection through the existing Session API. The authority can read each admitted guest. A guest can read the authority. |
| `await steam.auth().web_ticket(audience: String) -> Result<SteamWebTicket>` | Waits for Steam to issue a ticket for the named backend service. Use it with Web API validation. |
| `ticket.bytes() -> PackedByteArray` | Returns the issued Web API ticket bytes for transport to that backend over HTTPS. Keep the ticket out of save data, replay data, and logs. |
| `ticket.close() -> void` | Idempotently releases the native Web API ticket. Call after the backend has verified it and the ticket is no longer needed. Steam client shutdown releases outstanding tickets. |
| `await steam.auth().ownership_ticket(challenge: PackedByteArray) -> Result<SteamOwnershipTicket>` | Requests an encrypted app ticket with the backend’s bounded challenge embedded. Steam permits one pending encrypted ticket request and enforces a rate limit. |
| `ownership_ticket.bytes() -> PackedByteArray` | Returns the encrypted ticket for transport to the backend. It carries no revocable native handle. |
| `await publisher.auth().verify(ticket_bytes: PackedByteArray, audience: String) -> Result<SteamIdentity>` | Verifies a Web API ticket against the configured AppID and expected audience. Returns the Steam-authenticated player ID and the license owner ID when Valve supplies it. |
| `await publisher.auth().verify_ownership(ticket_bytes: PackedByteArray, challenge: PackedByteArray, dlc_ids: PackedInt64Array = []) -> Result<SteamOwnership>` | Decrypts using the app’s backend key, checks AppID and issue time, and checks the expected unexpired, unused challenge. Returns the verified player and ownership for the requested app and DLC IDs. The backend consumes the challenge once on success. |
| `await publisher.auth().owns_app(identity: SteamIdentity, app_id: int) -> Result<SteamAppOwnership>` | Queries the configured publisher’s ownership endpoint for a previously verified player. Returns the current app ownership and license details available to that publisher. |

The integrated provider binds each native auth session to the authenticated Steam identity and connection, rejects duplicate or invalid sessions, and ends the Steam session on failure, leave, or shutdown. If Steam later invalidates an admitted session, the authority removes the player through the normal peer lifecycle. Games receive the existing admission and disconnect outcomes. The trusted backend issues `SteamIdentity` after verification with `steam_id: String`, `license_owner_steam_id: String`, and `app_id: int`. Publisher functions on behalf of a player consume this verified identity. The installed SDK’s `ISteamAppTicket` header describes a private DRM interface. The API uses the public session, Web API, and encrypted app ticket APIs. [Steam User API](https://partner.steamgames.com/doc/api/ISteamUser) · [Steam Game Server API](https://partner.steamgames.com/doc/api/ISteamGameServer) · [Steam ticket verification](https://partner.steamgames.com/doc/webapi/ISteamUserAuth)

**FPS integration**

Steam lobby admission identifies the player who should spawn in Transfer Hall. A profile service can separately authenticate the player before loading their ranked profile. In this example `profile_backend` and `ProfileSignInResult` belong to the game. Its `sign_in` path calls `publisher.auth().verify()` on the server.

```gdscript
var issued: SteamWebTicketResult = await steam.auth().web_ticket("transfer-hall-profile")
if issued.ok:
    var ticket := issued.value
    var profile_session: ProfileSignInResult = await profile_backend.sign_in(ticket.bytes())
    ticket.close()
    if profile_session.ok:
        profile_view.show_profile(profile_session.value)
```

**Verification**

Run a native host and guest with distinct signed-in accounts and verify admission, teardown, and rejection using controlled fixtures. A second backend fixture uses an authorized AppID and the matching credential to exercise valid, expired, wrong-audience, and rejected tickets. Verify that failed tickets never produce a profile session. Use an app with an encrypted-ticket key and known DLC entitlements to verify encrypted ownership and repeated challenge rejection. Spacewar verifies native session behavior where available.

### VAC and publisher game bans

**Initialization**

Select `.dedicated(SteamServer.configure().secure())` on the Steam provider for a VAC-secure dedicated match. Crimson waits for the configured server login and security state before publication. Moderation runs in the trusted backend with the publisher credential, the AppID enabled for Valve’s anti-cheat Web APIs, and game-owned moderator authorization and audit records. A listen host or client can send a player report to the game’s backend through its authenticated game API.

**Target API**

| Function or signal | Behavior |
| --- | --- |
| `SteamServer.secure() -> SteamServer` | Selects Valve’s secure server mode on a configuration branch. Server startup reports failure if the requested secure mode cannot be established. |
| `await publisher.moderation().report(player_id: String, evidence: SteamCheatEvidence) -> Result<SteamCheatReport>` | Sends a player or authoritative server report for an eligible target. Evidence carries a match reference, source, reason code, and the authenticated reporter Steam ID for a player report. The result includes the Valve report ID used by subsequent review. |
| `await publisher.moderation().reports(from_unix: int, to_unix: int, after_report_id: String = "0") -> Result<SteamCheatReportPage>` | Lists publisher reports for a bounded UTC window. The result includes reports and a cursor when a subsequent page is available. |
| `await publisher.moderation().suspend(report_id: String, seconds: int, reason: String) -> Result<SteamModerationReceipt>` | Requests a suspension for the reviewed report. Accepts 1 through 31,535,999 seconds. The backend binds the report to its reviewed player and app before submission. |
| `await publisher.moderation().ban(report_id: String, reason: String) -> Result<SteamModerationReceipt>` | Requests a permanent game ban after explicit moderator review. The receipt records Steam’s acknowledgment and the audited action. |
| `await publisher.moderation().remove_ban(player_id: String, reason: String) -> Result<SteamModerationReceipt>` | Requests removal for the configured app and records the authorized reason. |

The secure match uses Steam’s authentication responses for admission and revocation. Publisher sanctions are a separate authority action. Report submission alone establishes a report for review. [Steam anti-cheat integration](https://partner.steamgames.com/doc/features/anticheat/vac_integration) · [Steam publisher moderation API](https://partner.steamgames.com/doc/webapi/ICheatReportingService)

**FPS integration**

The scoreboard Report button sends a selected player and reason to the game backend. The backend resolves the player from the recorded match and creates evidence with the authenticated reporter.

```gdscript
# Trusted backend handler after checking match membership.
var report: SteamCheatReportResult = await publisher.moderation().report(target_steam_id, evidence)
if report.ok:
    review_queue.add_report(report.value.report_id, evidence.match_id)
```

**Verification**

Use an authorized test app to verify a secure server advertises the requested mode and handles authentication failures. The backend fixture uses a controlled endpoint to prove report validation, moderator authorization, report-to-player binding, and removal handling. Verify a live report with designated test participants only when authorized. Verify sanction submission in the controlled fixture so routine runs apply no live bans.

### Steam DRM release tooling

**Initialization**

Install Valve’s SteamCMD content tools in the release environment and grant the release account the relevant app permissions. Export the Windows executable and game data to a test release staging directory. Configure an authorized AppID, explicit input and output paths, and a separate upload or test-depot step.

**Target API**

| Command | Behavior |
| --- | --- |
| `Wrap-SteamRelease.ps1 -SteamCmd <path> -AppId <int> -InputExe <path> -OutputExe <path> [-Mode Normal\|Compatibility]` | Applies Valve’s `drm_wrap` command with `drmtoolp` through authenticated SteamCMD. The default mode is `Normal`. Requires separate input and output files. Exit 0 means SteamCMD produced the wrapped output. A nonzero exit leaves packaging failed. Depot upload and publication are independent release commands. |

The normal and compatibility modes map to Valve’s supported wrapper settings. Compatibility mode disables the wrapper’s DRM capabilities for use with a separate DRM solution. The wrapper is a build-time operation for the shipped game executable. Game admission and backend entitlement checks use their own authentication paths. [Steam DRM](https://partner.steamgames.com/doc/features/drm)

**FPS integration**

A Transfer Hall Steam release exports the executable, wraps the staged Windows build, and uses the wrapped output in an authorized private test depot. The launched build reaches the same `GameRuntime` boot path.

```powershell
& $wrapSteamRelease -SteamCmd $steamCmd -AppId $testAppId -InputExe $exportedExe -OutputExe $depotExe
```

**Verification**

In release CI, verify that the source executable survives and the wrapped executable is the one staged for the depot. Then install the private test build through Steam and check launch as an entitled test user, with the configured launch and entitlement behavior confirmed in the test environment.

### SDK diagnostics and error reporting

**Initialization**

Opt into warning capture in a debug fixture, QA menu, or headless diagnostics command after starting the Steam scope. The warning hook is process-wide. Chromium, Vulkan driver, and Godot logs follow their own logging paths. Valve’s legacy Steam minidump service is near end of life and supports Windows 32-bit only. Crimson’s Windows x86-64 plan covers Steam SDK warnings and the game’s own diagnostic sink. [Steam error reporting](https://partner.steamgames.com/doc/features/error_reporting) · [Steam warning hook](https://partner.steamgames.com/doc/api/ISteamUtils#SetWarningMessageHook)

**Target API**

| Function | Behavior |
| --- | --- |
| `steam.diagnostics().start() -> Error` | Acquires the process warning hook for this scope. A repeated call by the owner succeeds. Another owner yields `ERR_BUSY`. |
| `steam.diagnostics().drain() -> SteamDiagnosticBatch` | Returns and clears a bounded snapshot on the Godot thread. A batch exposes `messages: Array[SteamDiagnosticMessage]` and `dropped_count: int`. Each message has `severity: int` and redacted `text: String`. |
| `steam.diagnostics().stop() -> void` | Releases the owner’s hook and clears its queued capture. Steam scope teardown invokes the same cleanup. |

**FPS integration**

A QA build lets testers inspect Steam warnings alongside separate Godot log output when a co-op invite fails.

```gdscript
if steam.diagnostics().start() == OK:
    qa_panel.show_steam_warnings(steam.diagnostics().drain())
```

**Verification**

A CLI fixture checks acquisition, contention between scopes, bounded queue drain, and cleanup after owner teardown. Controlled warning input verifies the dropped-message count and redaction. A graphical QA fixture displays the drained messages in the game’s diagnostics panel.

### HTTP client

**Initialization**

The HTTP service uses the initialized Steam client scope and requires no separate transport setup. A game can inject `steam.http()` into a public game service such as the map rotation reader. Configure backend credentials and publisher endpoints on the trusted backend.

**Target API**

The HTTP service and its options builder share the verbs below. Option calls return a new branch and send nothing. The HTTP result separates the existence of a complete response from the HTTP status.

| Function | Behavior |
| --- | --- |
| `accept_json() -> SteamHttpOptions` | Sets the `Accept` header to request JSON. |
| `with_header(name: String, value: String) -> SteamHttpOptions` | Sets one validated request header. Names are case-insensitive. The later value on a branch replaces an earlier value for the same name. |
| `with_timeout(milliseconds: int) -> SteamHttpOptions` | Sets the absolute deadline in milliseconds. Default 15,000, accepted range 100–120,000. |
| `with_max_response_bytes(bytes: int) -> SteamHttpOptions` | Sets the owned response body cap. Default 1 MiB, accepted range 1 byte–4 MiB. An oversized response settles as a limit error. |
| `with_response_headers(names: PackedStringArray) -> SteamHttpOptions` | Selects up to 16 extra response header names to copy before the SDK releases the request. `Content-Type` is available by default when supplied. |
| `await fetch(url: String) -> Result<SteamHttpResponse>` | Performs GET using the selected options. |
| `await head(url: String) -> Result<SteamHttpResponse>` | Performs HEAD and returns status and captured headers with an empty body. |
| `await post(url: String, data: Dictionary) -> Result<SteamHttpResponse>` | Sends a JSON object using POST. |
| `await put(url: String, data: Dictionary) -> Result<SteamHttpResponse>` | Sends a JSON object using PUT. |
| `await patch(url: String, data: Dictionary) -> Result<SteamHttpResponse>` | Sends a JSON object using PATCH. |
| `await delete(url: String) -> Result<SteamHttpResponse>` | Performs DELETE. |
| `await options(url: String) -> Result<SteamHttpResponse>` | Performs OPTIONS. |
| `request(url: String, method: SteamHttpMethod = GET) -> SteamHttpRequest` | Creates a draft for the same transport and validation. Choose this path when the caller needs a retained operation or raw request bytes. |
| `request.with_body(bytes: PackedByteArray, content_type: String) -> SteamHttpRequest` | Supplies an owned raw body for POST, PUT, or PATCH. The body cap is 1 MiB. The draft also supports the same configuration options as the main facade. |
| `request.send() -> Operation<SteamHttpResponse>` | Submits the configured request and returns an operation for cancellation or later observation. |

`SteamHttpResponse` has `get_status() -> int`, `get_body() -> PackedByteArray`, `header(name: String) -> String`, `json() -> Result<Variant>`, and `is_successful() -> bool`. A complete 404 or 503 produces a response with that status. `is_successful()` checks for 2xx. Invalid configuration, unavailable Steam, transport failure, deadline, oversized response, and local cancellation settle as operation errors. Requests use absolute HTTP or HTTPS URLs, with certificate validation for HTTPS. JSON parsing is explicit and can fail independently of transport. [Steam HTTP API](https://partner.steamgames.com/doc/api/ISteamHTTP) · [Laravel HTTP client](https://laravel.com/docs/13.x/http-client#headers)

**FPS integration**

The server browser menu reads a public map rotation feed with a shorter timeout. The default GET and configured GET use the same terminal verb.

```gdscript
var response: SteamHttpResponseResult = await steam.http().accept_json().with_timeout(3000).fetch(rotation_url)
if response.ok and response.value.is_successful():
    var rotation := response.value.json()
    if rotation.ok:
        browser_view.show_rotation(rotation.value)
```

**Verification**

Run a CLI fixture against controlled loopback endpoints in a signed-in client scope. Exercise default and configured GETs, JSON writes, captured headers, 4xx and 5xx responses, invalid URLs, deadlines, size limits, explicit cancellation, and shutdown during a request. A separate HTTPS fixture checks certificate acceptance and rejection. SDK-disabled and runtime-missing fixtures verify the unavailable result.

## Verification

Exercise each feature through its public methods in CLI or GUI fixtures. Each feature’s verification block specifies the expected observations. Extend the existing [Steam CLI harness](../scripts/Test-Steam.ps1) for additional cases. An opt-in Godot Steam test panel can drive graphical actions that need a visible result. Use both Windows x86-64 native builds with isolated Godot 4.7 test projects.

| Verification lane | What it establishes |
| --- | --- |
| SDK disabled or runtime missing | Startup diagnostics, typed unavailable results, argument validation, isolated scope teardown, and no fallback to a different online provider. |
| Signed-in Spacewar 480 | Shared client startup, supported read-only app queries, bounded HTTP against a test endpoint, and the Steam networking or overlay behaviors the shared AppID supports. Use unique lobby namespaces and distinct accounts for peer tests. |
| Authorized test AppID | Configured stats and leaderboards, private Workshop items, Cloud writes and second-device sync, Inventory definitions, localized presence, DLC entitlement, and app-specific publishing or discovery. Use scoped test data and explicit opt-in for durable writes. |
| GUI and hardware | Overlay pane, screenshots, timeline playback, Steam Input glyphs, keyboard dismissal, microphone audio, Remote Play input, and XR runtime behavior. Use the account, client settings, or hardware each feature names. |
| Trusted backend and release | Controlled endpoints for authorization and error paths, an authorized app for live publisher checks, the Steam payment sandbox for checkout, and a private test depot for DRM. Live secure server and hosted relay checks need their actual deployment configuration. |

Spacewar is a shared development app. Its passing cases establish what that AppID and the observed Steam client support. A game-owned test AppID, controlled participants, and the matching vendor configuration establish the app-specific behavior. [Valve Spacewar example](https://partner.steamgames.com/doc/sdk/api/example) · [Valve testing on Steam](https://partner.steamgames.com/doc/store/testing)

Use these criteria when implementing and verifying each feature.

1. **Composition** · One game boot selects a provider. Global Steam lookup, explicit game lookup, and injected consumers use the same live client scope. Standalone and dedicated roles have explicit owners.
2. **Call sites and completion** · The first example for each feature shows an FPS action. Builders carry choices and submit at a named terminal. Default and configured HTTP calls end with the same verb. Verify native method return metadata, properties, typed collection elements, and completion signal arguments from a GDScript consumer.
3. **Results** · Read each feature’s acknowledgment, partial-effect, and completion contracts. Direct awaits and retained operations use the documented terminal outcome. Existing Game operations retain `.wait()`.
4. **Authority** · The game scope owns its rules, scene loading, spawning, and admission. Verified player identity, purchasing, moderation, and release credentials have explicit owners.
5. **Verification** · Every feature has an observable pass condition and names any test app, account, device, or backend it needs. Check that an unavailable capability can be exercised through the public API as well.
