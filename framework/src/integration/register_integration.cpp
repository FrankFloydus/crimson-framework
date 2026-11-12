// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/register_integration.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/game/dedicated_boot.hpp"
#include "integration/online/eos/eos_online.hpp"
#include "integration/online/eos/eos_runtime.hpp"
#include "integration/online/eos/eos_transport.hpp"
#include "integration/game/game_services.hpp"
#include "integration/adapters/inventory_transaction_adapter.hpp"
#include "integration/world_session/local_view.hpp"
#include "integration/game/multiplayer_rules.hpp"
#include "integration/online/online_directory.hpp"
#include "integration/world_session/spawn_point.hpp"
#include "integration/online/steam/steam_online.hpp"
#include "integration/online/steam/steam_runtime.hpp"
#include "integration/online/steam/steam_transport.hpp"
#include "integration/world_session/world_session.hpp"
#include "integration/world_session/world_session_definition.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::integration {

void register_types(godot::ModuleInitializationLevel level) {
#ifdef CRIMSON_WITH_STEAM
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SERVERS) {
        SteamRuntime::prepare_overlay_from_launch();
    }
#endif
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(CrimsonMultiplayerRules);
        GDREGISTER_CLASS(CrimsonSpawnPoint);
        GDREGISTER_CLASS(CrimsonLocalView);
        GDREGISTER_CLASS(CrimsonOnlineDirectoryRequest);
        GDREGISTER_CLASS(CrimsonOnlineDirectory);
        GDREGISTER_CLASS(CrimsonServiceProvider);
        GDREGISTER_CLASS(CrimsonGameDefinition);
        GDREGISTER_CLASS(CrimsonGameOperation);
        GDREGISTER_INTERNAL_CLASS(CrimsonMatchCheckpointCandidate);
        GDREGISTER_INTERNAL_CLASS(CrimsonMatchCheckpoint);
        GDREGISTER_INTERNAL_CLASS(CrimsonMatchNetwork);
        GDREGISTER_CLASS(CrimsonSaveRequest);
        GDREGISTER_CLASS(CrimsonSaveSlot);
        GDREGISTER_CLASS(CrimsonGame);
        GDREGISTER_CLASS(CrimsonSceneService);
        GDREGISTER_CLASS(Crimson);
        GDREGISTER_CLASS(CrimsonDedicatedBoot);
        GDREGISTER_CLASS(CrimsonEOS);
        GDREGISTER_INTERNAL_CLASS(CrimsonEOSPeer);
        GDREGISTER_CLASS(CrimsonEOSSessionTransport);
#ifdef CRIMSON_WITH_EOS
        GDREGISTER_INTERNAL_CLASS(CrimsonEOSPoller);
        GDREGISTER_INTERNAL_CLASS(CrimsonEOSAuthenticator);
        GDREGISTER_INTERNAL_CLASS(CrimsonEOSLobbyDirectory);
#endif
        GDREGISTER_CLASS(CrimsonEOSProvider);
        GDREGISTER_INTERNAL_CLASS(CrimsonSteamPeer);
        GDREGISTER_CLASS(CrimsonSteamSessionTransport);
#ifdef CRIMSON_WITH_STEAM
        GDREGISTER_INTERNAL_CLASS(CrimsonSteamRuntimeNode);
        GDREGISTER_INTERNAL_CLASS(CrimsonSteamAuthenticator);
        GDREGISTER_INTERNAL_CLASS(CrimsonSteamOnlineDirectory);
#endif
        GDREGISTER_CLASS(CrimsonSteamProvider);
        GDREGISTER_CLASS(CrimsonInventoryTransactionAdapter);
        GDREGISTER_CLASS(CrimsonWorldSessionDefinition);
        GDREGISTER_CLASS(CrimsonWorldSession);
    }
}

void unregister_types(godot::ModuleInitializationLevel level) {
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        CrimsonEOS::shutdown_runtime();
    }
#ifdef CRIMSON_WITH_STEAM
    if (level == godot::MODULE_INITIALIZATION_LEVEL_SERVERS) {
        SteamRuntime::release_overlay_at_shutdown();
    }
#endif
}

} // namespace crimson::integration
