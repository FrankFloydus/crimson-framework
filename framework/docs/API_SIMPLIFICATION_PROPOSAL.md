<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Code-first API restructure for Game, Inventory, and Steamworks

**Design record.** See the [code-first quickstart](CODE_FIRST.md) for the implemented API and compatibility guidance. The public setup uses an ordinary Godot script as the lifetime owner and returns typed services. Game, Inventory, and Steamworks setup require zero authored Crimson nodes.

[Source audit](#what-the-sources-actually-do) · [Game](#game) · [Inventory](#inventory) · [Steamworks](#steamworks) · [Services and overrides](#services-and-overrides) · [Migration and verification](#migration-and-verification)

## What the sources actually do

The Laravel source is the supplied `framework-13.x.zip`. Its `Foundation/Application.php:49` identifies 13.42.0. Archive SHA-256 is `54438be5c20b45306f597c05ad79ec213d3066d4bec76b1571d6a423034cc4a7`. The Cashier Stripe checkout is branch `16.x` at [5ca07a1](https://github.com/laravel/cashier-stripe/commit/5ca07a1dde00e58fec88e9da9dafe61c946daf4d). Six agents audited the sources and applicable Crimson boundaries. Paths below are relative to those source checkouts.

| Pattern | Source evidence | What Crimson should adopt |
| --- | --- | --- |
| A small application entry point | Laravel `src/Illuminate/Foundation/Application.php:225–250` installs baseline bindings and chooses defaults through `configure()`. `Configuration/ApplicationBuilder.php:498` returns the configured app. HTTP and console kernels run the bootstrap sequence afterward. | One code expression declares the game and attaches its scope. Starting play is a separate action. A config draft performs no SDK, world, or network work. |
| Defaults first, overrides before activation | `Foundation/Http/Kernel.php:44–49` orders configuration, facades, provider registration, then boot. `Foundation/Bootstrap/LoadConfiguration.php:28–51,79–143` combines defaults and application config, with special shallow merges for named maps. `Configuration/ApplicationBuilder.php:388–427` schedules binding overrides before provider boot. | Resolve one typed snapshot. Choose a connection or module once, apply explicit overrides, and validate the combination before publishing it. Catalog replacement and addition have separate semantics. |
| A facade and an injected consumer reach the same service | Laravel `Container/Container.php:359–395,502–535,907–969` resolves bindings and shared or scoped instances. `Support/Facades/Facade.php:209–244,356–364` forwards to the container and can cache separately. `RegisterFacades.php:18–22` clears that cache when installing an application. | Use one typed service registry and a single cache per lifetime. A facade resolves the same object as an injected consumer. Retire references with their scope. Keep PHP's reflective resolution and its separate facade cache out of the Godot API design. |
| A default action and custom options share one engine | `Filesystem/FilesystemManager.php:87–117,552–554` forwards to the selected disk. `Http/Client/Factory.php:605–622,652–658` creates a fresh request for both `Http::get()` and `Http::timeout(...)->get()`. | Give the ordinary action a direct call. Add a draft when optional choices make a real workflow clearer. Both paths use the same validation, lifetime, and completion. |
| Domain actions own the atomic boundary | `Database/Eloquent/Model.php:1912–1936` creates a scoped query. `Database/Concerns/ManagesTransactions.php:26–74` owns commit and rollback. `Database/Eloquent/Builder.php:753–777` handles a creation race with a uniqueness constraint and a follow-up read. | Bind an inventory to its actual player or container. A pickup should commit world consumption and placement together. Keep detached typed reads and explicit game rules. Retry only where the action has a defined safe retry policy. |

### What Cashier hides and what it preserves

Cashier is the stronger comparison for Steamworks. Its domain API is attached to a known billable customer. The caller chooses the product and the action. The package resolves the customer, builds the SDK request, and handles local state.

| Cashier call or boundary | Work inside it | Design consequence |
| --- | --- | --- |
| Install the package and add `Billable` to the model | Package discovery names the provider. Register merges config and binds logger and invoice renderer. Boot installs routes, views, publishing, and commands. The trait composes customer, subscriptions, invoicing, payment methods, and charging. | Install module defaults once. Resolve work from the subject that owns it. A game knows its connection, a backpack knows its inventory identity, and a Steam scope knows its app and account. Sources `composer.json:66–75`, `src/CashierServiceProvider.php:20–83`, `src/Billable.php:13–22`. |
| `$user->newSubscription('main', $price)->create($paymentMethod)` | A fresh owner-bound builder stores the meaningful choices. The terminal validates items, gets or creates the customer, submits to Stripe, records the subscription and items locally, then handles incomplete payment. | Keep one complete terminal. A result can describe action required even after remote and local records exist. The short call cannot imply rollback across two systems. Sources `src/Concerns/ManagesSubscriptions.php:21–24`, `src/SubscriptionBuilder.php:277–365,424–475`, `src/Concerns/HandlesPaymentFailures.php:38–75`. |
| `$user->newSubscription('main', $price)->checkout()` | Cashier prepares the Checkout Session, customer, and return URLs. Its response redirects the browser. Subscription webhooks reconcile local status separately. | A returned panel, invite, or checkout request has its own meaning. A game join should settle only at playable readiness. A purchase needs a verified outcome before an entitlement is granted. Sources `src/SubscriptionBuilder.php:375–413`, `src/Checkout.php:60–128`, `src/Http/Controllers/WebhookController.php:40–57,66–111,132–213`. |
| SDK access and tests | `Cashier::stripe()` is a static gateway that passes key, version, base URL, and optional overrides into the container's Stripe client resolution. A controller test replaces the client factory and exercises the same route. | Keep one SDK adapter seam below public actions. Configure a test fake at boot and exercise the normal API. Keep backend secrets with the trusted host. Sources `src/Cashier.php:120–128`, `tests/Unit/PaymentControllerTest.php:11–52`. |

Cashier's local lookup by Stripe ID and webhook upserts help reconcile a known remote record. They do not establish exactly-once execution for a repeated user request. Signature verification is conditional on the configured webhook secret in this snapshot. Crimson's trusted operations should require verified ingress and define their own idempotency and reconciliation. The supplied Laravel archive omits its test suite. The Cashier tests informed the audit through source inspection.

## Game

**The smallest game.** Put this script on the project's existing main scene. Choose the level and player scene in code. The game's player scene supplies input, movement, and presentation.

```gdscript
extends Node

const LEVEL := preload("res://levels/training_room.tscn")
const PLAYER := preload("res://actors/player.tscn")

@onready var game := Game.configure(LEVEL, PLAYER).boot(self)

func _on_play_pressed() -> void:
    var played: GameResult = await game.play()
    if not played.ok:
        $Status.text = played.message
```

`Game` is the typed handle. `boot(self)` binds the scope to the existing owner. If configuration fails, the handle reports that failure through actions and through `game.boot_result()` when startup UI needs it. A rejected boot publishes no active game scope. Retain a configured draft in code or adapt a saved `GameDefinition` with `GameConfig.from_definition()`.

| Bootstrap API | Contract |
| --- | --- |
| `Game.configure(level: PackedScene, player: PackedScene) -> GameConfig` | Create a fixed-world, solo-ready draft with the standard runtime services and no online connection selected. |
| `GameConfig.boot(owner: Node) -> Game` | Attach to the existing Godot owner and return its typed handle. Scene entry and script lifetimes use the same scope. |
| `GameConfig.provider(provider: GameProvider) -> GameConfig` | Add an explicit factory override at composition time. |
| `GameConfig.inventory(settings: InventoryConfig) -> GameConfig` | Enable game-bound inventory on a compatible coordinated profile. The standalone grid remains independently available. |

**Choose multiplayer once.** Use one of these alternatives in the same bootstrap:

```gdscript
# Two-player LAN
@onready var game := Game.configure(LEVEL, PLAYER).lan().boot(self)

# Steam, using the app that launched the game
@onready var game := Game.configure(LEVEL, PLAYER).steam().boot(self)

# A discoverable Spacewar development match
@onready var game := Game.configure(LEVEL, PLAYER) \
    .steam(SteamConfig.spacewar("our-team-training").public_games()).boot(self)
```

`lan(players: int = 2)` selects the default ENet setup. `steam(settings: SteamConfig = null, players: int = 2)` uses the launch App ID and invitation-only visibility by default. `SteamConfig.spacewar(group)` checks App ID 480 and isolates discovery to the matching test group. The Steam client and SDK launch context still need their [development setup](../vendor/README.md#spacewar-480). These modifiers return independent drafts so one config can supply several test profiles.

| Intent | Before · current path | After · proposed call |
| --- | --- | --- |
| Start solo | Build a definition, boot or assign `GameRuntime`, then `await game.play_solo().wait()`. | `Game.configure(LEVEL, PLAYER).boot(self)`, then `await game.play()`. |
| Host LAN or Steam | Call `host_game().wait()` or configure `SteamProvider`, resolve and check Steam, then call `host_online().wait()`. | Choose `.lan()` or `.steam()` at setup. Both call `await game.host()`. |
| Join LAN | `await game.join_game(address).wait()`. | `await game.join(address)`. |
| Invite and accept | Resolve `game.invites()`; open the overlay, mirror pending state in the menu, then join its listing. | `game.invite_friends()`. Show the offer from `game.invite_changed` or `game.pending_invite()`, then `await game.accept_invite(offer)` on confirmation. |
| Find and join | Resolve Steam, build and fetch a query, select a listing, then call `join_listing().wait()`. | `await game.find_and_join()`. A custom browser may still select a typed listing and join it explicitly. |
| Stop or leave | `game.leave()`, with separate observation of `session_ended` for teardown. | `await game.leave()`. |
| Travel to another level | Register a world and call `await game.travel_to(world_id).wait()`. | `await game.scenes().load(foundry)` on a coordinated game with an authored `WorldDefinition`. |
| Dedicated server | Use a dedicated boot scene or configure a Steam server and call `start_dedicated_online().wait()`. | A server script uses an explicit dedicated configuration and calls `await game.serve()`. Launch arguments and configuration choose role and bind settings. |

**Complete actions.** Host and join settle when the world and local role are ready. The host publishes and opens admission through the configured connection. A Steam startup failure reports the Steam cause. A guest sends the selected address, listing, or confirmed invitation, and Crimson owns resolution, authentication, scene loading, and cleanup. It uses the injected Scene Loader internally. Quick join attempts the first compatible discoverable match with an open seat, revalidates it, and reports empty search or admission failure distinctly. A replaced invitation leaves the newer offer pending. Leave settles after the exact start or session retires and succeeds immediately when idle.

**Worlds and ownership.** Use an existing ordinary Godot spawn convention, such as `Marker3D` children under `SpawnPoints`, or supply a spawn policy in code. The framework selects the slot on the authority. Preserve a stable world identity independently of its scene path. For games using travel, checkpoints, commands, or integrated inventory, explicitly choose the coordinated game profile and supply their state rules. The current fixed-world flow and coordinated flow have different readiness and spawn contracts. An additive facade can expose both while a later migration proves parity. A dedicated Steam server uses separate game-server settings and an explicit advertisement policy. [Current game actions](PUBLIC_API.md#start-a-game) · [Current authority and travel](PUBLIC_API.md#authority-gameplay-and-visibility)

```gdscript
# TRAINING and FOUNDRY are saved WorldDefinition resources.
var campaign := Game.campaign(TRAINING).with_world(FOUNDRY).boot(self)
await campaign.play()
await campaign.scenes().load(FOUNDRY)

# In an explicitly selected LAN server entry script.
var server := Game.configure(LEVEL, PLAYER).lan().boot(self)
await server.serve()
```

`Game.campaign(start: WorldDefinition) -> GameConfig` selects the coordinated path. `with_world(world: WorldDefinition) -> GameConfig` adds an authored destination. The game supplies its coordinated authority rules at boot. A Steam dedicated entry selects `GameConfig.steam_server(settings: SteamServer)` and uses the same `serve()` action. Its settings define the game port, query port, login reference, and advertisement policy.

## Inventory

**Standalone.** Use a grid directly, without booting a game or registering services.

```gdscript
var bag := Inventory.grid(Vector2i(8, 5))
var placed := bag.add(&"medkit_17", Vector2i(2, 1))
```

**Game-bound.** Enable it once on a coordinated game profile with `.inventory(InventoryConfig.backpack(Vector2i(8, 5)).pickups(GamePickups.new()))`. The game's pickup provider reads its own item definitions and authorizes use of its world record. At gameplay readiness, resolve the local player's backpack once and pass that typed handle to the consumer.

```gdscript
var backpack := game.inventory().backpack()
var picked_up: InventoryResult = await backpack.pick_up(pickup_id)
```

| Intent | Before · current path | After · proposed call |
| --- | --- | --- |
| Make a bag and add an item | Construct `SpatialInventory`, call `configure()`, search for a fit, test the sentinel, then call `try_place()`. | `Inventory.grid(size)`, then `bag.add(id, footprint)`. Add searches and commits the first legal unrotated fit. |
| Show items | Read placement dictionaries and assemble a view. | `bag.items() -> Array[InventoryItemView]` supplies detached IDs and placement geometry. A UI can read `bag.item_at(cell)` when it needs one cell. The game can attach its own item metadata. |
| Move or transfer to a chosen cell | `bag.try_move(id, cell, rotated)`, `bag.try_transfer(id, chest, cell, rotated)`. | Keep these precise calls. An optional placement draft is warranted only when a real flow needs several placement options. |
| Take a world item | Register the transaction adapter, route the command, stage source removal and placement, then commit. | `await backpack.pick_up(pickup_id)`. The named bag and pickup ID are enough for the caller. |

The coordinated service resolves the authenticated actor and inventory on the authority. It checks the game policy, chooses placement, and stages consumption and insertion in the same existing transaction. A full grid preserves the world item. A guest can submit an intent for a known ID and gains no permission by holding the handle. A result identifies the committed revision or rejection. Notify consumers after publication. Existing standalone stacks, equipment, wallet, and trade stay independent until a complete authority workflow is defined for them. [Grid API](../src/modules/inventory/MODEL.md) · [Current pickup transaction](../project/tests/match_runtime/test_match_runtime.gd#L267)

## Steamworks

A game configured with `.steam()` uses `game.steam()`. A project using Steam app features independently can boot a scoped client in one line:

```gdscript
@onready var steam := Steamworks.boot(self)

func _on_achievements_pressed() -> void:
    var shown := steam.open_achievements()
    if not shown.ok:
        $Status.text = shown.message
```

| Intent | Before · current path | After · proposed call |
| --- | --- | --- |
| Start standalone Steam | `SteamClient.boot(self)`, check the attachment, check initialization, then fetch the service. | `Steamworks.boot(self)` gives one typed scoped service. Startup diagnostics are available through the action or `steam.boot_result()`. |
| Use Steam attached to a game | `SteamServices.for_game(game)`, then readiness and capability lookups. | `game.steam()` resolves the exact game scope. Inject `steam.cloud()` or another narrower service into a reusable consumer. |
| Open a panel | Get the overlay, guard it, check readiness, then open Achievements. | `steam.open_achievements()` reports request submitted, overlay busy, or unavailable. Actual display remains a separate observation. |
| Unlock an achievement | Build a stat edit or update the cache and call store. | `steam.achievements().unlock("first_win")` validates, edits, and requests storage once. Keep `steam.stats().edit()…submit()` for a batch. Report submission separately from the app-wide storage callback. |
| Read data or send a request | Resolve each service and use existing typed operations. | Keep `await steam.cloud().read(name)`, `await steam.leaderboards().top(board)`, `await steam.http().fetch(url)`, and the existing typed Workshop, social, input, and media APIs. |
| Checkout and fulfillment | Use the client and trusted publisher workflows, with SDK callbacks and durable state. | Keep `await steam.purchases().checkout(cart)` as a complete client action. The trusted backend verifies, authorizes, finalizes, and records fulfillment. Keep the same checkout identity for reconciliation if an outcome is uncertain. |

Preserve one native client lease per matching process App ID and one pump per frame. Each game or standalone attachment gets its own service generation, so ending one scope retires its handles independently. Capture launch invites when needed. Overlay initialization before the display remains part of the launch path. Keep dedicated SteamGameServer and trusted publisher as explicit roles with separate credentials, policies, and lifetimes. An optional Steam failure must leave solo playable. [Current Steamworks API](STEAMWORKS.md) · [Trusted publisher](STEAM_PUBLISHER.md)

## Services and overrides

**One boot path.** Resolve typed defaults, apply the selected module settings, register built-in factories, and apply explicit project overrides. Freeze those choices. Resolve the factories needed to attach the game, wire them, then publish the scope. Defer other services until needed. Unwind acquired resources in reverse order when boot fails.

| Lifetime | Owner and examples |
| --- | --- |
| Process | The native Steam SDK lease and callback pump. It lives while an eligible attachment holds it. |
| Game | Config, Session, Scene Loader, selected online directory, and app-facing services. An existing Godot owner controls the lifetime. |
| Match | Player bindings, commands, inventory records, and current world state. Recreate or rebind when the match changes. |

Expose selected replacement contracts through typed provider bindings. A custom transport can keep the same gameplay call sites:

```gdscript
class_name ProjectProvider
extends GameProvider

func _register(services: GameBindings) -> void:
    services.session_transport(func() -> SessionTransport:
        return ProjectTransport.new()
    )
```

Attach with `Game.configure(LEVEL, PLAYER).lan().provider(ProjectProvider.new()).boot(self)`. Validate that the selected transport and authentication policy are compatible. A test provider binds a fake at the same seam before boot. The production and test callers both use `await game.host()`. Use one cache for a service at its declared lifetime, and inject the resolved typed handle into consumers. `Game.current()` can resolve the sole published game for simple scripts. When a project runs several games, use the returned handle or resolve from the actual owning scene. Choose a default explicitly if the project needs global access. A scope replacement retires the old handles and resolves new ones.

**Outcome contract.** Direct awaited actions return a typed result immediately or settle through a signal. `GameResult` supplies `ok`, a stable `code`, and a readable `message`. Add typed payloads only when useful, such as a lobby identifier or inventory placement. Keep existing retained operations for callers that need lifecycle observation. Publish one outcome after the owning boundary, and distinguish requested, accepted, committed, and externally observed states where they differ.

## Migration and verification

1. **Move composition behind code boot.** Build on the existing Game service container. Hide the runtime, RPC, mount, and callback owners under one lifecycle. Preserve deterministic relative RPC paths, attachment generations, and scope teardown. Public code uses the typed handle.
2. **Move the demo and dedicated entry point.** Replace authored framework boot and setup nodes with ordinary Godot scenes and code. Convert spawn anchors to ordinary markers and select the camera through the existing single-camera convention or an explicit binding. Verify equivalent spawn and view behavior before retiring the old authoring classes. Native Godot nodes still provide the RPC, scene mounting, and callback lifecycle internally.
3. **Deliver complete actions before expanding options.** First prove solo, LAN, Steam host/invite/quick join, and leave through one result per action. Then prove a standalone grid and one authoritative pickup. Then use the same scoped Steam service for standalone and game use.
4. **Keep compatibility explicit.** Existing serialized scenes and methods remain a migration path until the replacement flows and bindings are verified. Test direct and retained completion, invalid configuration, scope loss during pending work, two games without shared match state, and a provider fake exercised through the normal action.
5. **Verify real boundaries.** Run separate-process LAN and dedicated checks, coordinated travel and baseline checks, Steam invite and public discovery with two accounts, and Steam-unavailable solo. Verify one winner in a pickup race, a full bag preserving the source, and Steam results that distinguish submission from durable or externally observed completion.

The [code-first quickstart](CODE_FIRST.md) is the usage reference. This document records the source audit and design decisions behind it.
