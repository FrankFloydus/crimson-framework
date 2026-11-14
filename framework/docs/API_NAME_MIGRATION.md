<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Migrate Godot class names

The public API uses neutral type names such as `GameRuntime`, `GameServices`, `SteamClient`, and `SteamProvider`. The [name table](#public-class-name-map) covers the previous published and preview class names. The renamed build registers the names in the right column. Godot resolves those names in scripts and serialized resources, so update both before opening the project with the renamed addon.

## Scripts and text resources

1. Back up the project and close the Godot editor. Update exact class references in `.gd` files, including `extends`, typed variables and signals, `Array[Type]`, `is` and `as` checks, constructors, static calls, and class names passed to `ClassDB`.
2. Update `type` values for the listed classes in `.tscn` and `.tres` files. For example, a node declaration changes from `type="CrimsonGame"` to `type="GameRuntime"`. Preserve scene and resource UIDs, local resource IDs, node names, node paths, exported property keys, gameplay IDs, and RPC methods.
3. Install the renamed addon and reopen the project. Check that custom resources and exported properties appear in the Inspector, that GDScript parses, and that the application boots. For multiplayer, rebuild and test both sides of the connection.

The package still uses `addons/crimson-framework`, its existing DLL filenames, and the `crimson_framework_init` entry symbol. The naming migration leaves native namespaces, export feature tags, and wire identifiers in place. Preserve the same multiplayer node paths across peers.

## Binary resources

In a project copy, open `.scn` and `.res` files with the earlier addon build that registers their original classes. Save them as `.tscn` or `.tres` and update project references to the converted files as needed. Keep one imported copy per resource UID. Apply the text migration above, then open the converted assets with the renamed addon to verify their types and properties.

## Public class name map

| Previous Godot class | Current Godot class |
| --- | --- |
| `Crimson` | `GameServices` |
| `CrimsonCommandRouter` | `CommandRouter` |
| `CrimsonDedicatedBoot` | Replace the scene's boot node with an ordinary root script calling `GameConfig.boot_server(self)`. |
| `CrimsonENetSessionTransport` | `ENetSessionTransport` |
| `CrimsonGame` | `GameRuntime` |
| `CrimsonGameDefinition` | `GameDefinition` |
| `CrimsonGameOperation` | `GameOperation` |
| `CrimsonGameSession` | `GameSession` |
| `CrimsonInventoryTransactionAdapter` | `InventoryTransactionAdapter` |
| `CrimsonLocalView` | `LocalPlayerView` |
| `CrimsonMultiplayerRules` | `MultiplayerRules` |
| `CrimsonOnlineDirectory` | `OnlineDirectory` |
| `CrimsonOnlineDirectoryRequest` | `OnlineDirectoryRequest` |
| `CrimsonPlayerBindings` | `PlayerBindings` |
| `CrimsonPossession` | `PlayerPossession` |
| `CrimsonReplication` | `StateReplication` |
| `CrimsonSaveRequest` | `SaveRequest` |
| `CrimsonSaveSlot` | `SaveSlot` |
| `CrimsonSceneLoadManager` | `SceneLoadManager` |
| `CrimsonSceneService` | `SceneService` |
| `CrimsonServiceProvider` | `GameServiceProvider` |
| `CrimsonSessionAuthRequest` | `SessionAuthRequest` |
| `CrimsonSessionAuthenticator` | `SessionAuthenticator` |
| `CrimsonSessionEndpoint` | `SessionEndpoint` |
| `CrimsonSessionIdentity` | `SessionIdentity` |
| `CrimsonSessionTransport` | `SessionTransport` |
| `CrimsonSessionTransportResult` | `SessionTransportResult` |
| `CrimsonSpawnPoint` | `PlayerSpawnPoint` |
| `CrimsonSteamApps` | `SteamApps` |
| `CrimsonSteamClient` | `SteamClient` |
| `CrimsonSteamCloud` | `SteamCloud` |
| `CrimsonSteamCloudOperation` | `SteamBytesOperation` for reads, `SteamOperation` for writes |
| `CrimsonSteamFriends` | `SteamFriends` |
| `CrimsonSteamHttp` | `SteamHttp` |
| `CrimsonSteamHttpOperation` | `SteamHttpResponseOperation` |
| `CrimsonSteamHttpRequest` | `SteamHttpRequest` |
| `CrimsonSteamHttpResponse` | `SteamHttpResponse` |
| `CrimsonSteamLeaderboard` | `SteamLeaderboard` |
| `CrimsonSteamLeaderboards` | `SteamLeaderboards` |
| `CrimsonSteamProvider` | `SteamProvider` |
| `CrimsonSteamSessionTransport` | `SteamSessionTransport` |
| `CrimsonSteamStats` | `SteamStats` |
| `CrimsonSteamStatsEdit` | `SteamStatsEdit` |
| `CrimsonSteamStatsOperation` | Typed leaderboard operations such as `SteamLeaderboardOperation` and `SteamScoreUploadOperation` |
| `CrimsonSteamTimeline` | `SteamTimeline` |
| `CrimsonSteamTimelineEvent` | `SteamTimelineEvent` |
| `CrimsonSteamTimelineOperation` | `SteamTimelineOperation` |
| `CrimsonSteamUtils` | `SteamUtils` |
| `CrimsonTransaction` | `GameTransaction` |
| `CrimsonTransactionCoordinator` | `TransactionCoordinator` |
| `CrimsonWorldSession` | `WorldSession` |
| `CrimsonWorldSessionDefinition` | `WorldSessionDefinition` |
