<!-- Copyright Francesco Di Blasi 2026 - All Rights Reserved -->

# Trusted Steam publisher tools

Enable the trusted publisher module for a server or operator tool with `steam_publisher=yes` in SCons or `CRIMSON_STEAM_PUBLISHER=ON` in CMake. The trusted artifact lives in `bin/publisher/windows` for SCons or the selected CMake build directory. The default addon keeps its client build. The publisher can run in headless Godot without Steamworks client initialization.

Attach a `SteamPublisherHost` under the backend's persistent scene root and override its secret resolver. Keep the actual key in the backend's secret store. `SteamPublisherSettings` carries its reference and takes a weak reference to the attached host.

```gdscript
class_name ProfilePublisherHost
extends SteamPublisherHost

func _resolve_secret(reference: StringName) -> String:
    if reference == &"TRANSFER_HALL_STEAM_PUBLISHER_KEY":
        return OS.get_environment(String(reference))
    return ""
```

```gdscript
var settings := SteamPublisherSettings.new()
settings.app_id = configured_app_id
settings.host = $ProfilePublisherHost
settings.publisher_key_ref = &"TRANSFER_HALL_STEAM_PUBLISHER_KEY"
var boot := SteamPublisher.boot(settings)
if boot.ok:
    profile_publisher = boot.value
```

The host resolves the publisher credential for each request and passes it in Valve's `X-WebAPI-Key` header. The native transport uses verified TLS and a fixed endpoint table. It rejects redirects and bounds the concurrent requests, deadline and body size. Typed errors omit credential, ticket and response text. `SANDBOX` selects a separate Valve sandbox only for a feature that provides one. Auth, ownership and leaderboard calls use the configured AppID on their standard partner endpoints. [Valve Web API host and request format](https://partner.steamgames.com/doc/webapi_overview) · [Valve publisher keys](https://partner.steamgames.com/doc/webapi_overview/auth)

## Verify a profile and submit an authoritative score

The game backend supplies its configured audience and receives the ticket bytes over its authenticated connection. It holds the verified identity in the backend session that owns the player. The issuer invalidates identities when closed or when the host leaves its scene-tree generation. Player and run authorization remain game rules.

```gdscript
var verified: SteamIdentityResult = await profile_publisher.auth().verify(
    ticket_bytes,
    "transfer-hall-profile"
)
if verified.ok:
    var player := verified.value
    var license: SteamAppOwnershipResult = await profile_publisher.auth().owns_app(
        player,
        configured_app_id
    )
    if license.ok and license.value.owns_app:
        profile_sessions.attach_authenticated_player(player)
```

After the game verifies a ranked run, submit the score with the identity from that authenticated backend session. The publisher call uses KeepBest and accepts up to 256 raw details bytes. A successful result carries the submitted score and acknowledges the Steam HTTP call. The endpoint documentation does not specify rank or change fields, so the publisher result leaves `value.upload` empty. Use an authorized leaderboard read to check the current entry. [Ticket verification](https://partner.steamgames.com/doc/webapi/ISteamUserAuth#AuthenticateUserTicket) · [App ownership](https://partner.steamgames.com/doc/webapi/ISteamUser#CheckAppOwnership) · [Trusted score submission](https://partner.steamgames.com/doc/webapi/ISteamLeaderboards#SetLeaderboardScore)

```gdscript
var score: SteamLeaderboardSubmissionResult = await profile_publisher.leaderboards().submit_score(
    authenticated_player,
    ranked_board_id,
    verified_run.time_ms
)
```

## Process Steam Wallet orders

Enable microtransactions for an authorized AppID and provide a trusted `SteamPurchaseStore` before calling `SteamPublisher.boot`. The game adapter obtains the buyer's authenticated Steam identity and authorizes access to their known order IDs. The store atomically prices and reserves each account-scoped checkout, assigns a canonical positive decimal order ID, and saves the app, environment, account, checkout ID, currency, language, and priced lines. Replaying a checkout returns its original quote.

Before quoting a new checkout, call `await publisher.purchases().buyer_info(player)` with the publisher-verified `SteamIdentity`. `SteamPurchaseBuyerInfoResult.value` supplies the buyer's Wallet `country`, `currency`, `region`, and `account_status`. A missing status is `UNKNOWN`. Use these fields in game-owned pricing and eligibility policy. The result does not grant entitlement or update an existing order. The lookup follows the publisher's production or sandbox configuration. [Valve Wallet buyer information](https://partner.steamgames.com/doc/webapi/ISteamMicroTxn#GetUserInfo)

```gdscript
settings.purchase_store = armory_purchase_store
settings.environment = SteamPublisherSettings.SANDBOX
var publisher := SteamPublisher.boot(settings).value
var purchases := publisher.purchases()
var submitted: SteamPurchaseOutcomeResult = await purchases.begin(player, cart, checkout_id)
# Run after the backend observes the authorization hint for this account and order.
var receipt: SteamPurchaseOutcomeResult = await purchases.finalize(order_id)
```

The store chooses a currency and quoted total for each entire line using Valve's required units and increments. Configure `SteamPublisherPurchaseOrder.NET` when the quote matches Steam's reported amount excluding VAT, or `GROSS` when it matches the reported amount plus VAT. Use currency-specific pricing that can be verified in the original quoted currency. An unmatched payer, order, transaction, currency, or item leaves the order unresolved for backend review.

`finalize` queries Steam before it claims the charge. A client approval callback is a hint to run that check. The store's `_compare_and_commit` must persist the exact next revision and its entitlement difference atomically, or enqueue the same difference in a durable outbox in that transaction. Compute the desired paid item set from the incoming phase and exclude `reversed_item_ids`. Compare against the durable granted set and apply changes idempotently by order and item. A partial refund received as the first proof of payment grants only the surviving paid lines. Keep an uncertain order and retry the read. The service queries an unresolved claim without issuing another charge or refund. A failed result can carry a recoverable order in the `RECONCILING` state.

Call `reconcile(since_unix, max_results = 1000)` from a trusted scheduled job and retain its returned cursor after each successful page. Set a page limit from 1 through 1,000 and reduce it if the response exceeds the 4 MiB report limit. The cursor is inclusive, so updates from the last second can recur. Steam may return fewer than the requested row count when more updates are available. A failed page retains the original cursor, and a full page that cannot move past its first second returns `ERR_BUSY` for operator review. A successful call with an unchanged cursor can be polled again. Full reversals revoke the order's entitlements. A partial refund revokes the exact Steam item IDs in the verified receipt and leaves remaining items active. `refund(order_id)` requires explicit authorization through the store's refund hook and checks Steam's updated transaction before recording the reversal. [Steam purchasing implementation](https://partner.steamgames.com/doc/features/microtransactions/implementation) · [Microtransaction API](https://partner.steamgames.com/doc/webapi/ISteamMicroTxn) · [Steam sandbox](https://partner.steamgames.com/doc/webapi/ISteamMicroTxnSandbox)

## Register Game Notifications once per launch

Call `publisher.game_notifications().request_opt_in(player, launch_id)` from the trusted backend with the verified player and a stable ID issued for that game launch. Reuse the launch ID when refreshing the same authenticated session. Issue a fresh ID for a new launch. A missing launch ID returns `ERR_INVALID_PARAMETER`.

Implement `SteamNotificationStore._claim_launch_opt_in(app_id, steam_id, launch_id)` as an atomic durable reservation. Return `OK` only when the backend owns a new claim and `ERR_ALREADY_IN_USE` for an existing one. `_release_unsent_launch_opt_in(app_id, steam_id, launch_id)` releases only the exact claim when the request could not be submitted. Keep the claim if the request was dispatched, including when the result is ambiguous. The backend keeps its launch ID local. Steam receives only the AppID and verified account. [Valve notification registration](https://partner.steamgames.com/doc/webapi/IGameNotificationsService#RequestNotifications)

When a player opens a notification session through Steam, the client reads `SteamApps.notification_session_id()` at boot or receives `notification_session_requested(session_id)` while running. Confirm that the authenticated player belongs to the session through the game's backend before resuming it. [Valve notification launch flow](https://partner.steamgames.com/doc/features/game_notifications#launching_specific_game_sessions)

## Validate moderation duration

`publisher.moderation().suspend(report_id, seconds, reason)` accepts 1 through 31,535,999 seconds. Longer requests fail before game policy review and before any publisher request. Use the separately authorized `ban(report_id, reason)` operation for a permanent ban. Valve classifies durations under one year as suspensions. [Valve sanction duration](https://partner.steamgames.com/doc/webapi/ICheatReportingService#RequestPlayerGameBan)

## Verify locally

Build the test-only transport with `steam_publisher=yes steam_publisher_tests=yes` in SCons or both `CRIMSON_STEAM_PUBLISHER` and `CRIMSON_STEAM_PUBLISHER_TESTS` enabled in CMake. [The fixture runner](../scripts/Test-Steam.ps1) stages the trusted binary and the selected [publisher fixture](../scripts/fixtures/steam_publisher/) into an isolated Godot project. Keeping these scripts outside the framework project lets its editor load with the standard addon. The fixture uses synthetic ticket bytes and a dummy key. The native test transport never opens a socket.

```powershell
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case publisher -Mode mock -Target template_debug -BinaryDirectory $trustedBin
.\framework\scripts\Test-Steam.ps1 -GodotExe $godot -Case publisher_purchases -Mode mock -Target template_debug -BinaryDirectory $trustedBin
```

The same runner accepts `publisher_inventory`, `publisher_notifications`, and `moderation` in mock mode. Use `-Case all -Mode mock` for the complete trusted test suite. The separate `publisher_ownership` fixture accepts `mock`, `sdk-off`, `missing`, `invalid`, and `app`. Its positive mock uses the installed Steam SDK headers and the test-only transport. The `invalid` lane uses the actual SDK decoder in a trusted production build with deliberately invalid ciphertext. The [live ownership command](STEAM_TESTING.md#verify-trusted-publisher-operations-and-drm) uses the matching app's key, a signed-in entitled account, and a fixture challenge store to check real issuance, decryption, and rejection of a replay. Use a durable one-use challenge store in production. [Valve encrypted AppTicket API](https://partner.steamgames.com/doc/api/SteamEncryptedAppTicket)

Look for `CRIMSON_STEAM_PUBLISHER_PASSED mock`. The fixture checks AppID and audience forwarding, exact large IDs, ownership results, cross-issuer rejection, form encoding, redacted errors, and teardown. Live evidence still requires an authorized test AppID, a matching Steamworks publisher credential, and a publisher-write leaderboard. Use a production transport build for that run.

Look for `CRIMSON_STEAM_PUBLISHER_PURCHASES_PASSED mock`. The purchases fixture checks sandbox routing, durable checkout replays, payer and pricing mismatches, ambiguous charge recovery, one-time fulfillment, partial reversals, and an explicitly approved refund. An authorized partner run requires a test AppID with microtransactions enabled, a publisher key with Microtransaction permission, a signed-in licensed test account, a real durable store, and a production transport build pointed at Valve's sandbox. Verify that approval appears in the overlay, the backend records one grant, and a sandbox refund reconciles that same order without charging the test wallet.
