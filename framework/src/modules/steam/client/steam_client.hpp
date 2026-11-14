// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_apps.hpp"
#include "modules/steam/client/steam_auth.hpp"
#include "modules/steam/client/steam_networking.hpp"
#include "modules/steam/client/steam_sockets.hpp"
#include "modules/steam/client/steam_messages.hpp"
#include "modules/steam/client/steam_parties.hpp"
#include "modules/steam/client/steam_voice.hpp"
#include "modules/steam/client/steam_purchases.hpp"
#include "modules/steam/client/steam_input.hpp"
#include "modules/steam/client/steam_browser.hpp"
#include "modules/steam/client/steam_diagnostics_service.hpp"
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/client/steam_cloud.hpp"
#include "modules/steam/client/steam_friends.hpp"
#include "modules/steam/client/steam_http.hpp"
#include "modules/steam/client/steam_inventory.hpp"
#include "modules/steam/client/steam_leaderboards.hpp"
#include "modules/steam/client/steam_overlay.hpp"
#include "modules/steam/client/steam_remote_play.hpp"
#include "modules/steam/client/steam_screenshots.hpp"
#include "modules/steam/client/steam_stats.hpp"
#include "modules/steam/client/steam_timeline.hpp"
#include "modules/steam/client/steam_utils.hpp"
#include "modules/steam/client/steam_presence.hpp"
#include "modules/steam/client/steam_music.hpp"
#include "modules/steam/client/steam_video.hpp"
#include "modules/steam/client/steam_device.hpp"
#include "modules/steam/client/workshop/steam_workshop.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"
#include "modules/steam/server/steam_server_browser.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <memory>

namespace crimson::steam {

class Steamworks;

// Owns a standalone Steam client scope and pumps SDK callbacks on the main thread.
class SteamClient : public godot::Node {
    GDCLASS(SteamClient, godot::Node)

public:
    // Opt into Valve's shipping launch check before boot. ERR_SKIP means Steam will relaunch the installed game.
    static godot::Error restart_through_steam_if_needed(int64_t app_id);
    // Attach and start a client on a live scene-tree owner. An attached client retains its startup diagnostic.
    static SteamClient *boot(godot::Node *owner, int64_t expected_app_id = 0);
    // Acquire the signed-in client. A zero AppID uses the Steam launch context.
    godot::Error start(int64_t expected_app_id = 0);
    // Detach this scope's services and release its runtime reference.
    void stop();
    // Report whether this node has an active client scope.
    bool is_ready() const;
    // Return whether the active Steam client has a live connection to Valve.
    bool is_online() const;
    // Return the AppID confirmed by Steam or zero without an active scope.
    int64_t get_app_id() const;
    // Return the signed-in individual SteamID64 as a decimal string.
    godot::String get_local_steam_id() const;
    // Return the most recent startup failure without exposing account data.
    godot::String get_initialization_error() const;
    // Return this scope's Apps facade after successful startup.
    godot::Ref<SteamApps> apps() const;
    // Return this scope's Friends facade after successful startup.
    godot::Ref<SteamFriends> friends() const;
    // Return this scope's overlay actions and activation signal after successful startup.
    godot::Ref<SteamOverlay> overlay() const;
    // Return this scope's platform utilities after successful startup.
    godot::Ref<SteamUtils> utils() const;
    // Return this scope's Steam warning capture service after successful startup.
    godot::Ref<SteamDiagnostics> diagnostics() const;
    // Return this scope's Steam HTTP facade after successful startup.
    godot::Ref<SteamHttp> http() const;
    // Return this scope's local stats and achievements facade.
    godot::Ref<SteamStats> stats() const;
    // Return this scope's leaderboard service.
    godot::Ref<SteamLeaderboards> leaderboards() const;
    // Return this scope's Steam Cloud file service.
    godot::Ref<SteamCloud> cloud() const;
    // Return this scope's Steam Game Recording timeline service.
    godot::Ref<SteamTimeline> timeline() const;
    // Return this scope's Steam screenshot service.
    godot::Ref<SteamScreenshots> screenshots() const;
    // Return this scope's Remote Play sessions and input service.
    godot::Ref<SteamRemotePlay> remote_play() const;
    // Return this scope's Steam Inventory service.
    godot::Ref<SteamInventory> inventory() const;
    // Return this scope's Steam lobby discovery and membership service.
    godot::Ref<SteamLobbies> lobbies() const;
    // Return the Steam server browser scoped to this exact client start.
    godot::Ref<SteamServerBrowser> server_browser() const;
    // Set the Game lobby policy before the internal client enters the tree.
    void configure_game_lobbies(const godot::StringName &name_space);
    // Return this scope's Steam Workshop service.
    godot::Ref<SteamWorkshop> workshop() const;
    // Return this scope's managed Rich Presence service.
    godot::Ref<SteamPresence> presence() const;
    // Return this scope's Steam music player service.
    godot::Ref<SteamMusic> music() const;
    // Return this scope's Steam video and broadcast service.
    godot::Ref<SteamVideo> video() const;
    // Return this scope's device, virtual keyboard, and VR status service.
    godot::Ref<SteamDevice> device() const;
    // Return this scope's authentication and ownership ticket service.
    godot::Ref<SteamAuth> auth() const;
    // Return this scope's Steam Input controller service.
    godot::Ref<SteamInput> input() const;
    // Return this scope's Steam HTML Surface service.
    godot::Ref<SteamBrowser> browser() const;
    // Return the client purchase facade, with an adapter when a Game bound one.
    godot::Ref<SteamPurchases> purchases() const;
    // Return the voice capture and decode service for this client start.
    godot::Ref<SteamVoice> voice() const;
    // Return this scope's Steam Parties locations and reservations.
    godot::Ref<SteamParties> parties() const;
    // Return this scope's authorized Steam message channels.
    godot::Ref<SteamMessages> messages() const;
    // Return this scope's Steam relay and connection-path diagnostics.
    godot::Ref<SteamNetworking> networking() const;
    // Return this scope's standalone Steam Sockets service.
    godot::Ref<SteamSockets> sockets() const;
    // Allow startup when this node is attached or reattached.
    void _enter_tree() override;
    // Dispatch pending SDK callbacks while the scope is active.
    void _process(double delta) override;
    // Release services before the owning node leaves its persistent tree.
    void _exit_tree() override;
    // Observe the exact start generation when an owning Game exposes scoped services.
    const std::shared_ptr<SteamClientScope> &get_scope_generation() const;
    // End the matching owner generation and reject restart until the node enters again.
    void retire_scope_generation(const std::shared_ptr<SteamClientScope> &expected);

protected:
    // Register the standalone client API with Godot.
    static void _bind_methods();

private:
    friend class Steamworks;

    // Attach the callback owner and capture its exact startup outcome for the code-first facade.
    static SteamClient *attach_and_start(godot::Node *owner, int64_t expected_app_id,
            godot::Error &started, bool internal);
    // Shared lease on the signed-in process runtime.
    std::shared_ptr<SteamRuntime> runtime;
    // Shared liveness of this start's asynchronous services.
    std::shared_ptr<SteamClientScope> scope;
    // Weak identity of the code-first facade associated with the current generation.
    godot::ObjectID code_first_facade_id;
    // Apps facade for the current start, invalidated when the scope stops.
    godot::Ref<SteamApps> apps_service;
    // Friends facade for the current start, invalidated when the scope stops.
    godot::Ref<SteamFriends> friends_service;
    // Overlay facade for the current start, subscribed until this scope stops.
    godot::Ref<SteamOverlay> overlay_service;
    // Utilities facade for the current start, invalidated when the scope stops.
    godot::Ref<SteamUtils> utils_service;
    // Diagnostics facade for the current start and its optional process hook.
    godot::Ref<SteamDiagnostics> diagnostics_service;
    // HTTP facade and its pending requests for the current start.
    godot::Ref<SteamHttp> http_service;
    // Stats facade and callback subscriptions for the current start.
    godot::Ref<SteamStats> stats_service;
    // Leaderboard facade and pending operations for the current start.
    godot::Ref<SteamLeaderboards> leaderboards_service;
    // Cloud facade and pending file operations for the current start.
    godot::Ref<SteamCloud> cloud_service;
    // Timeline facade and its unfinished events for the current start.
    godot::Ref<SteamTimeline> timeline_service;
    // Screenshot capture and observations for the current start.
    godot::Ref<SteamScreenshots> screenshots_service;
    // Remote Play session observation and optional input ownership.
    godot::Ref<SteamRemotePlay> remote_play_service;
    // Steam Inventory snapshots and pending native results for the current start.
    godot::Ref<SteamInventory> inventory_service;
    // Lobby operations and membership owned by this exact start generation.
    godot::Ref<SteamLobbies> lobby_service;
    // Dedicated discovery and direct inspection for the current start.
    godot::Ref<SteamServerBrowser> server_browser_service;
    // Namespace of a Game-owned client, empty for a standalone client.
    godot::StringName game_lobby_namespace;
    // Steam Workshop requests for the current start.
    godot::Ref<SteamWorkshop> workshop_service;
    // Active managed Rich Presence service for this client start.
    godot::Ref<SteamPresence> presence_service;
    // Active Steam music player service for this client start.
    godot::Ref<SteamMusic> music_service;
    // Active Steam video and broadcast service for this client start.
    godot::Ref<SteamVideo> video_service;
    // Active device, virtual keyboard, and VR status service for this client start.
    godot::Ref<SteamDevice> device_service;
    // Active authentication and ownership ticket service for this client start.
    godot::Ref<SteamAuth> auth_service;
    // Active Steam Input controller service for this client start.
    godot::Ref<SteamInput> input_service;
    // Active Steam HTML Surface service for this client start.
    godot::Ref<SteamBrowser> browser_service;
    // Authenticated commerce adapter actions scoped to the current client start.
    godot::Ref<SteamPurchases> purchases_service;
    // Opt-in voice capture and decoding for the current client start.
    godot::Ref<SteamVoice> voice_service;
    // Party beacons and reservations for the active client start.
    godot::Ref<SteamParties> parties_service;
    // Owned Steam message peers and their channel policy for the active client start.
    godot::Ref<SteamMessages> messages_service;
    // Current Steam relay and connection-path diagnostics for this client start.
    godot::Ref<SteamNetworking> networking_service;
    // Current standalone Steam Sockets service for this client start.
    godot::Ref<SteamSockets> sockets_service;
    // Block reentrant startup when an exit cancels pending operations.
    bool exiting_tree = false;
    // Most recent startup failure, cleared on a successful start.
    godot::String initialization_error;
};

} // namespace crimson::steam
