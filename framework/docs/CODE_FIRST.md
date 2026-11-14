<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Start from code

Put your script on an existing node in the main scene. Choose the level and player scene once. Crimson attaches its private runtime and scoped services to that owner. Starting a game is a separate action.

```gdscript
extends Node

const LEVEL := preload("res://levels/training_room.tscn")
const PLAYER := preload("res://actors/player.tscn")

@onready var game := Game.configure(LEVEL, PLAYER).boot(self)

func _on_play_pressed() -> void:
    var result: GameResult = await game.play()
    if not result.ok:
        $Status.text = result.message
```

Put regular `Marker3D` children under `SpawnPoints` in the level. Their order supplies spawn slots. Put one `Camera3D` in the player scene for automatic local camera selection. To hide a direct child of the local actor, assign that child the `crimson_local_hidden` group. The player scene supplies movement and controls.

Use [Default player controls](PLAYER_CONTROLS.md) for an Inspector-authored or code-first movement and look profile on an existing character. Share the local `Controls` handle with your game UI to offer rebinding.

## Play together

| Setup choice | Code | Player action |
| --- | --- | --- |
| LAN | `Game.configure(LEVEL, PLAYER).lan().boot(self)` | Host calls `await game.host()`. Guest calls `await game.join(address)`. For two copies on the same PC use `127.0.0.1`. |
| Steam | `Game.configure(LEVEL, PLAYER).steam().boot(self)` | Host calls `await game.host()` and then `game.invite_friends()`. |
| Discoverable Steam test | `Game.configure(LEVEL, PLAYER).steam(SteamConfig.spacewar("training").public_games()).boot(self)` | Host calls `await game.host()`. Guest calls `await game.find_and_join()` using the same test group and build. |
| Dedicated LAN | `Game.configure(LEVEL, PLAYER).lan(4).boot(self)` | A server script calls `await game.serve()`. The repository launcher opens a `DedicatedServerEntry` scene that selects the same assets and starts the native server. |
| Dedicated Steam | `Game.configure(LEVEL, PLAYER).steam_server(SteamServer.configure()).boot(self)` | The headless server calls `await game.serve()`. Configure the Steam server ports, login reference, and advertisement policy for deployment. |

The simple profile defaults to two total seats. For LAN, both peers use the same port, which defaults to UDP 7000. Use `.on_port(port)` before boot to change it. Steam uses the launch App ID and private visibility by default. Spacewar requires an actual App ID 480 launch context and two signed-in accounts for a host and guest test. [Steam development launch](../vendor/README.md#spacewar-480)

Observe `game.invite_changed` and read `game.pending_invite()` when the menu attaches. Show the exact offer to the player, then call `await game.accept_invite(offer)` after confirmation. A replaced offer fails without consuming its replacement. Call `game.dismiss_invite(offer)` to dismiss it. A custom browser can pass its selected listing to `await game.join_listing(listing)`.

`await game.leave()` settles after the targeted session or pending start retires. `game.boot_result()` reports configuration failures, and direct game actions carry that same cause when boot failed. `GameResult.ok`, `code`, and `message` support presentation. A successful host includes its `listing_id` when one is published.

## Resolve or replace a service

Pass the typed game handle into menus and gameplay consumers. `Game.for_node(self)` finds the nearest owning scope. `Game.current()` resolves the selected or sole live game. Use `Game.set_default(game)` when a project with several scopes wants an explicit default.

Call `game.network_diagnostics()` from a development panel or test to sample the game's local role, connection, and movement. The typed snapshot stays fixed until you sample again. ENet reports measured round-trip time. The demo can separately add lag, jitter, or packet loss to a local guest route through the transport provider below. [Demo network debugger](../project/demo/README.md)

```gdscript
class_name ProjectProvider
extends GameProvider

func _register(services: GameBindings) -> void:
    services.session_transport(func() -> SessionTransport:
        return ENetSessionTransport.new()
    )
```

Pass the provider once with `Game.configure(LEVEL, PLAYER).lan().provider(ProjectProvider.new()).boot(self)`. The scope resolves a fresh adapter before attachment and seals the registration draft. A configured online provider owns its connection and cannot be combined with this direct transport override. The demo continues to call the same Game methods for either implementation.

## Coordinated worlds and inventory

```gdscript
var training := WorldDefinition.at(&"training", "res://levels/training_room.tscn")
var foundry := WorldDefinition.at(&"foundry", "res://levels/foundry.tscn")
var game := Game.campaign(training).with_world(foundry).boot(self)
var started: GameResult = await game.play()
if started.ok:
    var traveled: GameResult = await game.scenes().load(foundry)
```

A coordinated game can register commands and state before starting. For coordinated LAN, select `.lan()`, open with `game.open_lobby()`, and have guests submit `game.join_lobby(address)`. Admission publishes the lobby phase. Bind the verified identity of every admitted player using `game.bind_player(peer_id, player_id)`, then start with `await game.start_match()`. This explicit lobby flow lets the game supply trusted identity and match rules. Fixed-world `host()` and `join()` own the entire drop-in flow.

Standalone inventory starts with `Inventory.grid(Vector2i(8, 5))` and `bag.add(id, footprint)`. Enable coordinated backpacks with `.inventory(InventoryConfig.backpack(size).pickups(source))` before boot. A player with a ready coordinated world can call `await game.inventory().backpack().pick_up(pickup_id)`. The authority validates the actor and game-owned pickup policy, then publishes the placement and world consumption together. [Inventory guide](../src/modules/inventory/README.md)

## Steam app features

Use `var steam := game.steam()` for the Steam game attachment. Use `var steam := Steamworks.boot(self)` for a project that only needs app services. Call `steam.open_achievements()` to request the panel or `steam.achievements().unlock(api_name)` to submit a stat-store request. Steam acknowledges requests separately from panel display and persistence. [Steamworks guide](STEAMWORKS.md)

## Existing projects

The [legacy reference](PUBLIC_API.md#legacy-gameruntime) describes the old serialized definitions, stand-alone engine services, and retained operation APIs. Those compatibility classes are hidden from the node picker. The demo and headless launcher use ordinary Godot scenes with code-first bootstrap. Engine owners remain private children with stable paths for Godot RPC, scene loading, and callbacks.
