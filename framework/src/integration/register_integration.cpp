// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/register_integration.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/game/dedicated_boot.hpp"
#include "integration/game/game_services.hpp"
#include "integration/game/game_voice.hpp"
#include "integration/adapters/inventory_transaction_adapter.hpp"
#include "integration/world_session/local_view.hpp"
#include "integration/game/multiplayer_rules.hpp"
#include "integration/online/online_directory.hpp"
#include "integration/world_session/spawn_point.hpp"
#include "integration/online/steam/steam_dedicated.hpp"
#include "integration/online/steam/steam_online.hpp"
#include "integration/online/steam/steam_relay_coordinator.hpp"
#include "integration/online/steam/steam_transport.hpp"
#include "integration/world_session/world_session.hpp"
#include "integration/world_session/world_session_definition.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "integration/online/steam/steam_dedicated_auth.hpp"
#endif

namespace crimson::integration {

void register_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(MultiplayerRules);
        GDREGISTER_CLASS(PlayerSpawnPoint);
        GDREGISTER_CLASS(LocalPlayerView);
        GDREGISTER_CLASS(OnlineDirectoryRequest);
        GDREGISTER_CLASS(OnlineDirectory);
        GDREGISTER_CLASS(GameServiceProvider);
        GDREGISTER_CLASS(GameDefinition);
        GDREGISTER_CLASS(GameOperation);
        GDREGISTER_INTERNAL_CLASS(MatchCheckpointCandidate);
        GDREGISTER_INTERNAL_CLASS(MatchCheckpoint);
        GDREGISTER_INTERNAL_CLASS(MatchNetwork);
        GDREGISTER_CLASS(SaveRequest);
        GDREGISTER_CLASS(SaveSlot);
        GDREGISTER_CLASS(GameVoice);
        GDREGISTER_CLASS(GameRuntime);
        GDREGISTER_CLASS(SceneService);
        GDREGISTER_CLASS(GameServices);
        GDREGISTER_CLASS(DedicatedServerBoot);
        GDREGISTER_INTERNAL_CLASS(SteamPeer);
        GDREGISTER_CLASS(SteamSessionTransport);
#ifdef CRIMSON_WITH_STEAM
        GDREGISTER_INTERNAL_CLASS(SteamRuntimeNode);
        GDREGISTER_INTERNAL_CLASS(SteamAuthenticator);
        GDREGISTER_INTERNAL_CLASS(SteamDedicatedAuthenticator);
#endif
        GDREGISTER_INTERNAL_CLASS(SteamOnlineDirectory);
        GDREGISTER_INTERNAL_CLASS(SteamDedicatedRuntimeNode);
        GDREGISTER_INTERNAL_CLASS(SteamDedicatedOnlineDirectory);
        GDREGISTER_CLASS(SteamRelayRequest);
        GDREGISTER_CLASS(SteamRelayCoordinator);
        GDREGISTER_CLASS(SteamProvider);
        GDREGISTER_CLASS(SteamInvite);
        GDREGISTER_CLASS(GameInvites);
        GDREGISTER_CLASS(SteamScope);
        GDREGISTER_CLASS(SteamServerStatus);
        GDREGISTER_CLASS(SteamDedicatedServer);
        GDREGISTER_CLASS(SteamServices);
        GDREGISTER_CLASS(InventoryTransactionAdapter);
        GDREGISTER_CLASS(WorldSessionDefinition);
        GDREGISTER_CLASS(WorldSession);
    }
}

void unregister_types(godot::ModuleInitializationLevel) {
}

} // namespace crimson::integration
