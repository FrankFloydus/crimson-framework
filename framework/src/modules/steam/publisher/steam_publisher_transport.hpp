// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/publisher/steam_publisher.hpp"
#include "modules/steam/publisher/steam_publisher_moderation.hpp"
#include "modules/steam/publisher/steam_publisher_purchases.hpp"
#include "modules/steam/publisher/steam_ownership_challenge_store.hpp"

#include <atomic>
#include <utility>
#include <vector>

namespace crimson::steam {

// Immutable snapshot of the configuration accepted by one trusted publisher scope.
struct SteamPublisherState {
    // Invalidated before any retained action can resume a closed issuer.
    std::atomic_bool live{true};
    // AppID used for ticket verification and publisher actions.
    int64_t app_id = 0;
    // Vendor environment for capabilities that have a separate sandbox.
    SteamPublisherSettings::Environment environment = SteamPublisherSettings::LIVE;
    // Credential reference resolved on demand by the exact host.
    godot::StringName publisher_key_ref;
    // Optional reference to the distinct encrypted ticket credential.
    godot::StringName encrypted_ticket_key_ref;
    // Durable storage for the configured application's notification sessions.
    godot::Ref<SteamNotificationStore> notification_store;
    // Application policy for publisher reports and sanctions.
    godot::Ref<SteamModerationPolicy> moderation_policy;
    // Durable game-owned pricing, orders, and entitlement updates.
    godot::Ref<SteamPurchaseStore> purchase_store;
    // Durable challenge authority for encrypted ownership verification.
    godot::Ref<SteamOwnershipChallengeStore> ownership_challenge_store;
    // Scene-tree owner for transport and credential resolution.
    godot::ObjectID host;
    // Host attachment generation at publisher boot.
    uint64_t host_generation = 0;
};

// The only routes issued by the trusted publisher transport.
enum class SteamPublisherEndpoint {
    VERIFY_TICKET, // ISteamUserAuth AuthenticateUserTicket.
    CHECK_APP, // ISteamUser CheckAppOwnership.
    SUBMIT_SCORE, // ISteamLeaderboards SetLeaderboardScore.
    NOTIFY_REQUEST, // IGameNotificationsService RequestNotifications.
    NOTIFY_CREATE, // IGameNotificationsService CreateSession.
    NOTIFY_UPDATE, // IGameNotificationsService UpdateSession.
    NOTIFY_LIST, // IGameNotificationsService EnumerateSessionsForApp.
    NOTIFY_DETAILS, // IGameNotificationsService GetSessionDetailsForApp.
    NOTIFY_REMOVE, // IGameNotificationsService DeleteSession.
    REPORT_CHEATING, // ICheatReportingService ReportPlayerCheating.
    LIST_CHEATING_REPORTS, // ICheatReportingService GetCheatingReports.
    REQUEST_GAME_BAN, // ICheatReportingService RequestPlayerGameBan.
    REMOVE_GAME_BAN, // ICheatReportingService RemovePlayerGameBan.
    PURCHASE_BUYER_INFO, // ISteamMicroTxn GetUserInfo.
    INIT_PURCHASE, // ISteamMicroTxn InitTxn.
    QUERY_PURCHASE, // ISteamMicroTxn QueryTxn.
    FINALIZE_PURCHASE, // ISteamMicroTxn FinalizeTxn.
    REFUND_PURCHASE, // ISteamMicroTxn RefundTxn.
    PURCHASE_REPORT, // ISteamMicroTxn GetReport.
    GET_INVENTORY, // IInventoryService GetInventory.
    GRANT_INVENTORY_ITEM, // IInventoryService AddItem.
    CONSUME_INVENTORY_ITEM, // IInventoryService ConsumeItem.
    START_INVENTORY_TRADE, // ISteamEconomy StartTrade.
};

// Internal form fields for one fixed Valve route.
struct SteamPublisherRequest {
    // Selects the method and path from the native route table.
    SteamPublisherEndpoint endpoint;
    // Field values are bytes to preserve binary parameters through form encoding.
    std::vector<std::pair<godot::String, godot::PackedByteArray>> fields;

    // Add a textual parameter encoded as UTF-8.
    void add(const godot::String &name, const godot::String &value);
    // Add a raw binary parameter without converting it to text.
    void add_bytes(const godot::String &name, const godot::PackedByteArray &value);
};

// Bounded vendor reply delivered only to the requesting publisher action.
struct SteamPublisherReply {
    // Local transport status, including normalized HTTP rejection codes.
    godot::Error error = godot::ERR_UNAVAILABLE;
    // Bounded response bytes. No vendor body is exposed to scripts.
    godot::PackedByteArray body;
};

// Restrict trusted Godot object and request access to the scene thread.
bool publisher_on_main_thread();

} // namespace crimson::steam
