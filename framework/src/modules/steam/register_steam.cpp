// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/register_steam.hpp"

#include "modules/steam/client/steam_client.hpp"
#include "modules/steam/client/steam_diagnostics_service.hpp"
#include "modules/steam/client/steam_result.hpp"
#include "modules/steam/client/steamworks.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"
#include "modules/steam/server/steam_server.hpp"
#include "modules/steam/server/steam_server_browser.hpp"
#include "modules/steam/server/steam_inventory_verifier.hpp"
#include "modules/steam/server/steam_server_progression.hpp"

#ifdef CRIMSON_WITH_STEAM_PUBLISHER
#include "modules/steam/publisher/steam_game_notifications.hpp"
#include "modules/steam/publisher/steam_ownership.hpp"
#include "modules/steam/publisher/steam_ownership_challenge_store.hpp"
#include "modules/steam/publisher/steam_publisher.hpp"
#include "modules/steam/publisher/steam_publisher_inventory.hpp"
#include "modules/steam/publisher/steam_publisher_moderation.hpp"
#include "modules/steam/publisher/steam_publisher_purchases.hpp"
#endif

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#endif

#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {

void register_types(godot::ModuleInitializationLevel level) {
#ifdef CRIMSON_WITH_STEAM
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SERVERS) {
        SteamRuntime::prepare_overlay_from_launch();
    }
#endif
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(SteamResult);
        GDREGISTER_CLASS(SteamBoolResult);
        GDREGISTER_CLASS(SteamStringResult);
        GDREGISTER_CLASS(SteamDelivery);
        GDREGISTER_CLASS(SteamOperation);
        GDREGISTER_CLASS(SteamIntResult);
        GDREGISTER_CLASS(SteamFloatResult);
        GDREGISTER_CLASS(SteamServerStatsResult);
        GDREGISTER_CLASS(SteamServerStatsOperation);
        GDREGISTER_CLASS(SteamServerStatsEdit);
        GDREGISTER_CLASS(SteamServerPlayerStats);
        GDREGISTER_CLASS(SteamAchievement);
        GDREGISTER_CLASS(SteamStatsSubmission);
        GDREGISTER_CLASS(SteamStatsSubmissionResult);
        GDREGISTER_CLASS(SteamDlcInfo);
        GDREGISTER_CLASS(SteamDlcProgress);
        GDREGISTER_CLASS(SteamDlcListResult);
        GDREGISTER_CLASS(SteamDlcProgressResult);
        GDREGISTER_CLASS(SteamApps);
        GDREGISTER_CLASS(SteamFriend);
        GDREGISTER_CLASS(SteamFriendsResult);
        GDREGISTER_CLASS(SteamFriends);
        GDREGISTER_CLASS(SteamPresence);
        GDREGISTER_CLASS(SteamUtils);
        GDREGISTER_CLASS(SteamJsonResult);
        GDREGISTER_CLASS(SteamHttpResponse);
        GDREGISTER_CLASS(SteamHttpResponseResult);
        GDREGISTER_CLASS(SteamHttpResponseOperation);
        GDREGISTER_CLASS(SteamHttpRequest);
        GDREGISTER_CLASS(SteamHttpOptions);
        GDREGISTER_CLASS(SteamHttp);
        GDREGISTER_CLASS(SteamLeaderboardEntry);
        GDREGISTER_CLASS(SteamLeaderboardPage);
        GDREGISTER_CLASS(SteamLeaderboardPageResult);
        GDREGISTER_CLASS(SteamLeaderboardPageOperation);
        GDREGISTER_CLASS(SteamLeaderboardQuery);
        GDREGISTER_CLASS(SteamLeaderboardInfo);
        GDREGISTER_CLASS(SteamLeaderboardInfoResult);
        GDREGISTER_CLASS(SteamScoreUpload);
        GDREGISTER_CLASS(SteamScoreUploadResult);
        GDREGISTER_CLASS(SteamScoreUploadOperation);
        GDREGISTER_CLASS(SteamLeaderboardSubmission);
        GDREGISTER_CLASS(SteamLeaderboardSubmissionResult);
        GDREGISTER_CLASS(SteamLeaderboardSubmissionOperation);
        GDREGISTER_CLASS(SteamScoreOptions);
        GDREGISTER_CLASS(SteamStatsEdit);
        GDREGISTER_CLASS(SteamStats);
        GDREGISTER_CLASS(SteamLeaderboard);
        GDREGISTER_CLASS(SteamLeaderboardResult);
        GDREGISTER_CLASS(SteamLeaderboardOperation);
        GDREGISTER_CLASS(SteamLeaderboards);
        GDREGISTER_CLASS(SteamBytesResult);
        GDREGISTER_CLASS(SteamBytesOperation);
        GDREGISTER_CLASS(SteamItemDefinition);
        GDREGISTER_CLASS(SteamItemInstance);
        GDREGISTER_CLASS(SteamItemQuantity);
        GDREGISTER_CLASS(SteamInventorySnapshot);
        GDREGISTER_CLASS(SteamInventoryChange);
        GDREGISTER_CLASS(SteamItemDefinitionsResult);
        GDREGISTER_CLASS(SteamItemDefinitionResult);
        GDREGISTER_CLASS(SteamInventorySnapshotResult);
        GDREGISTER_CLASS(SteamInventoryChangeResult);
        GDREGISTER_CLASS(SteamItemDefinitionsOperation);
        GDREGISTER_CLASS(SteamItemDefinitionOperation);
        GDREGISTER_CLASS(SteamInventorySnapshotOperation);
        GDREGISTER_CLASS(SteamInventoryChangeOperation);
        GDREGISTER_CLASS(SteamInventory);
        GDREGISTER_CLASS(SteamInventoryVerifier);
        GDREGISTER_CLASS(SteamWorkshopSubscription);
        GDREGISTER_CLASS(SteamWorkshopSubscriptionResult);
        GDREGISTER_CLASS(SteamWorkshopInstall);
        GDREGISTER_CLASS(SteamWorkshopInstallResult);
        GDREGISTER_CLASS(SteamWorkshopVote);
        GDREGISTER_CLASS(SteamWorkshopVoteResult);
        GDREGISTER_CLASS(SteamWorkshopPublication);
        GDREGISTER_CLASS(SteamWorkshopPublicationResult);
        GDREGISTER_CLASS(SteamWorkshopItem);
        GDREGISTER_CLASS(SteamWorkshopPage);
        GDREGISTER_CLASS(SteamWorkshopItemResult);
        GDREGISTER_CLASS(SteamWorkshopPageResult);
        GDREGISTER_INTERNAL_CLASS(SteamWorkshopAction);
        GDREGISTER_CLASS(SteamWorkshopQuery);
        GDREGISTER_CLASS(SteamWorkshopPublishRequest);
        GDREGISTER_CLASS(SteamWorkshopItemRef);
        GDREGISTER_CLASS(SteamWorkshopSubscriptions);
        GDREGISTER_CLASS(SteamWorkshopSubscriptionsResult);
        GDREGISTER_CLASS(SteamWorkshopLegalAgreementStatus);
        GDREGISTER_CLASS(SteamWorkshopLegalAgreementStatusResult);
        GDREGISTER_CLASS(SteamWorkshop);
        GDREGISTER_CLASS(SteamCloudFile);
        GDREGISTER_CLASS(SteamCloudFileResult);
        GDREGISTER_CLASS(SteamCloudFilesResult);
        GDREGISTER_CLASS(SteamCloudQuota);
        GDREGISTER_CLASS(SteamCloudQuotaResult);
        GDREGISTER_CLASS(SteamCloudChange);
        GDREGISTER_CLASS(SteamCloudShareOperation);
        GDREGISTER_CLASS(SteamSharedCloudFile);
        GDREGISTER_CLASS(SteamSharedCloudFileResult);
        GDREGISTER_CLASS(SteamSharedCloudFileOperation);
        GDREGISTER_CLASS(SteamCloudWriteBatch);
        GDREGISTER_CLASS(SteamCloudWriteBatchResult);
        GDREGISTER_CLASS(SteamCloud);
        GDREGISTER_CLASS(TimelineMarkerPresentation);
        GDREGISTER_CLASS(TimelineMarkerOptions);
        GDREGISTER_CLASS(SteamTimelineEvent);
        GDREGISTER_CLASS(SteamTimelineEventResult);
        GDREGISTER_CLASS(SteamTimelineOperation);
        GDREGISTER_CLASS(TimelinePhaseRecording);
        GDREGISTER_CLASS(SteamTimelinePhaseResult);
        GDREGISTER_INTERNAL_CLASS(SteamTimelinePhaseOperation);
        GDREGISTER_CLASS(SteamTimeline);
        GDREGISTER_CLASS(SteamScreenshot);
        GDREGISTER_CLASS(SteamScreenshotResult);
        GDREGISTER_CLASS(SteamScreenshotOperation);
        GDREGISTER_CLASS(SteamScreenshotRequest);
        GDREGISTER_CLASS(SteamScreenshots);
        GDREGISTER_CLASS(SteamRemotePlaySession);
        GDREGISTER_CLASS(SteamRemotePlayInputEvent);
        GDREGISTER_CLASS(SteamRemotePlay);
        GDREGISTER_CLASS(SteamOverlay);
        GDREGISTER_CLASS(SteamDiagnosticMessage);
        GDREGISTER_CLASS(SteamDiagnosticBatch);
        GDREGISTER_CLASS(SteamDiagnostics);
        GDREGISTER_CLASS(SteamListing);
        GDREGISTER_CLASS(SteamListingResult);
        GDREGISTER_CLASS(SteamListingsResult);
        GDREGISTER_CLASS(SteamListingsOperation);
        GDREGISTER_CLASS(SteamLobbyMember);
        GDREGISTER_CLASS(SteamLobbyOptions);
        GDREGISTER_CLASS(SteamLobbyEdit);
        GDREGISTER_CLASS(SteamLobby);
        GDREGISTER_CLASS(SteamLobbyResult);
        GDREGISTER_CLASS(SteamLobbyOperation);
        GDREGISTER_CLASS(SteamLobbyQuery);
        GDREGISTER_CLASS(SteamLobbies);
        GDREGISTER_CLASS(SteamPartyLocation);
        GDREGISTER_CLASS(SteamPartyListing);
        GDREGISTER_CLASS(SteamPartyReservation);
        GDREGISTER_CLASS(SteamPartyLocationsResult);
        GDREGISTER_CLASS(SteamPartyListingsResult);
        GDREGISTER_CLASS(SteamPartySlotsOperation);
        GDREGISTER_CLASS(SteamPartyBeacon);
        GDREGISTER_CLASS(SteamPartyBeaconResult);
        GDREGISTER_INTERNAL_CLASS(SteamPartyObservation);
        GDREGISTER_CLASS(SteamParties);
        GDREGISTER_CLASS(SteamMessagePeer);
        GDREGISTER_CLASS(SteamMessagePeerResult);
        GDREGISTER_CLASS(SteamMessages);
        GDREGISTER_CLASS(SteamConnectionPath);
        GDREGISTER_CLASS(SteamConnectionPathResult);
        GDREGISTER_CLASS(SteamRelayStatus);
        GDREGISTER_CLASS(SteamNetworking);
        GDREGISTER_CLASS(SteamSocketEndpoint);
        GDREGISTER_CLASS(SteamSocketOffer);
        GDREGISTER_CLASS(SteamSocketConnection);
        GDREGISTER_CLASS(SteamSocketConnectionResult);
        GDREGISTER_CLASS(SteamSocketConnectOperation);
        GDREGISTER_CLASS(SteamSocketListener);
        GDREGISTER_CLASS(SteamSocketListenerResult);
        GDREGISTER_CLASS(SteamSockets);
        GDREGISTER_CLASS(SteamMusicStatus);
        GDREGISTER_CLASS(SteamMusicStatusResult);
        GDREGISTER_CLASS(SteamMusic);
        GDREGISTER_CLASS(SteamBroadcastStatus);
        GDREGISTER_CLASS(SteamBroadcastStatusResult);
        GDREGISTER_INTERNAL_CLASS(SteamVideoOperation);
        GDREGISTER_CLASS(SteamVideo);
        GDREGISTER_CLASS(SteamTextEntry);
        GDREGISTER_CLASS(SteamTextEntryResult);
        GDREGISTER_INTERNAL_CLASS(SteamTextEntryOperation);
        GDREGISTER_CLASS(SteamDevice);
        GDREGISTER_CLASS(SteamWebTicket);
        GDREGISTER_CLASS(SteamWebTicketResult);
        GDREGISTER_CLASS(SteamOwnershipTicket);
        GDREGISTER_CLASS(SteamOwnershipTicketResult);
        GDREGISTER_INTERNAL_CLASS(SteamAuthOperation);
        GDREGISTER_CLASS(SteamAuth);
        GDREGISTER_CLASS(SteamDigitalInput);
        GDREGISTER_CLASS(SteamAnalogInput);
        GDREGISTER_CLASS(SteamDigitalInputResult);
        GDREGISTER_CLASS(SteamAnalogInputResult);
        GDREGISTER_CLASS(SteamTextureResult);
        GDREGISTER_CLASS(SteamTexturesResult);
        GDREGISTER_CLASS(SteamController);
        GDREGISTER_CLASS(SteamInput);
        GDREGISTER_CLASS(SteamBrowserPage);
        GDREGISTER_CLASS(SteamBrowserPageResult);
        GDREGISTER_INTERNAL_CLASS(SteamBrowserOpenOperation);
        GDREGISTER_CLASS(SteamBrowserRequest);
        GDREGISTER_CLASS(SteamBrowser);
        GDREGISTER_CLASS(SteamPurchaseLine);
        GDREGISTER_CLASS(SteamPurchaseCart);
        GDREGISTER_CLASS(SteamPurchaseItem);
        GDREGISTER_CLASS(SteamPurchaseCatalog);
        GDREGISTER_CLASS(SteamPurchaseOutcome);
        GDREGISTER_CLASS(SteamPurchaseCatalogResult);
        GDREGISTER_CLASS(SteamPurchaseOutcomeResult);
        GDREGISTER_CLASS(SteamCommerceRequest);
        GDREGISTER_CLASS(SteamCommerceBackend);
        GDREGISTER_CLASS(SteamPurchases);
        GDREGISTER_CLASS(SteamVoiceFrame);
        GDREGISTER_CLASS(SteamVoicePcm);
        GDREGISTER_CLASS(SteamVoiceFrameResult);
        GDREGISTER_CLASS(SteamVoicePcmResult);
        GDREGISTER_CLASS(SteamVoiceCapture);
        GDREGISTER_CLASS(SteamVoice);
        GDREGISTER_CLASS(SteamServer);
        GDREGISTER_CLASS(SteamServerDetails);
        GDREGISTER_CLASS(SteamServerDetailsResult);
        GDREGISTER_CLASS(SteamServerDetailsOperation);
        GDREGISTER_CLASS(SteamServerQuery);
        GDREGISTER_CLASS(SteamServerBrowser);
        GDREGISTER_VIRTUAL_CLASS(SteamClient);
        GDREGISTER_CLASS(SteamConfig);
        GDREGISTER_CLASS(SteamAchievements);
        GDREGISTER_CLASS(Steamworks);
#ifdef CRIMSON_WITH_STEAM_PUBLISHER
        GDREGISTER_VIRTUAL_CLASS(SteamPublisherHost);
        GDREGISTER_CLASS(SteamPublisherSettings);
        GDREGISTER_CLASS(SteamIdentity);
        GDREGISTER_CLASS(SteamIdentityResult);
        GDREGISTER_CLASS(SteamOwnershipChallengeStore);
        GDREGISTER_CLASS(SteamOwnership);
        GDREGISTER_CLASS(SteamOwnershipResult);
        GDREGISTER_CLASS(SteamAppOwnership);
        GDREGISTER_CLASS(SteamAppOwnershipResult);
        GDREGISTER_CLASS(SteamPublisherAuth);
        GDREGISTER_CLASS(SteamPublisherLeaderboards);
        GDREGISTER_CLASS(SteamNotificationClaim);
        GDREGISTER_CLASS(SteamNotificationStore);
        GDREGISTER_CLASS(SteamLocalizedText);
        GDREGISTER_CLASS(SteamNotificationUserState);
        GDREGISTER_CLASS(SteamNotificationSessionSpec);
        GDREGISTER_CLASS(SteamNotificationParticipant);
        GDREGISTER_CLASS(SteamNotificationSession);
        GDREGISTER_CLASS(SteamNotificationSessionResult);
        GDREGISTER_CLASS(SteamNotificationSessionsResult);
        GDREGISTER_CLASS(SteamNotificationBoolOperation);
        GDREGISTER_CLASS(SteamNotificationSessionOperation);
        GDREGISTER_CLASS(SteamNotificationSessionsOperation);
        GDREGISTER_CLASS(SteamGameNotifications);
        GDREGISTER_CLASS(SteamCheatEvidence);
        GDREGISTER_CLASS(SteamModerationRequest);
        GDREGISTER_CLASS(SteamModerationAuthorization);
        GDREGISTER_CLASS(SteamModerationReceipt);
        GDREGISTER_CLASS(SteamModerationPolicy);
        GDREGISTER_CLASS(SteamCheatReport);
        GDREGISTER_CLASS(SteamCheatReportPage);
        GDREGISTER_CLASS(SteamCheatReportResult);
        GDREGISTER_CLASS(SteamCheatReportPageResult);
        GDREGISTER_CLASS(SteamModerationReceiptResult);
        GDREGISTER_CLASS(SteamPublisherModeration);
        GDREGISTER_CLASS(SteamPublisherPurchaseLine);
        GDREGISTER_CLASS(SteamPublisherPurchaseOrder);
        GDREGISTER_CLASS(SteamPublisherPurchaseOrderResult);
        GDREGISTER_CLASS(SteamPurchaseStore);
        GDREGISTER_CLASS(SteamPurchaseReconciliation);
        GDREGISTER_CLASS(SteamPurchaseReconciliationResult);
        GDREGISTER_CLASS(SteamPurchaseBuyerInfo);
        GDREGISTER_CLASS(SteamPurchaseBuyerInfoResult);
        GDREGISTER_CLASS(SteamPublisherPurchases);
        GDREGISTER_CLASS(SteamTradeSession);
        GDREGISTER_CLASS(SteamTradeSessionResult);
        GDREGISTER_CLASS(SteamPublisherInventory);
        GDREGISTER_CLASS(SteamPublisher);
        GDREGISTER_CLASS(SteamPublisherResult);
        GDREGISTER_INTERNAL_CLASS(SteamPublisherCompletion);
#endif
    }
}

void unregister_types(godot::ModuleInitializationLevel level) {
#ifdef CRIMSON_WITH_STEAM
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SERVERS) {
        SteamRuntime::release_overlay_at_shutdown();
    }
#endif
}

} // namespace crimson::steam
