// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_client.hpp"
#include "modules/steam/client/steam_config.hpp"

namespace crimson::steam {

// Single achievement actions for the exact Steam client scope that supplied them.
class SteamAchievements : public godot::RefCounted {
    GDCLASS(SteamAchievements, godot::RefCounted)

    friend class Steamworks;

    // Stats service from this attachment. Its native scope retires with the client.
    godot::Ref<SteamStats> stats_service;
    // Startup error to return when no stats service could be acquired.
    godot::Ref<SteamResult> startup;

protected:
    // Bind the code-first achievement action.
    static void _bind_methods();

public:
    // Apply one achievement edit and submit storage, preserving edit and submission status.
    godot::Ref<SteamStatsSubmissionResult> unlock(const godot::String &name) const;
};

// Code-first access to one attachment of the process Steam client runtime.
class Steamworks : public godot::RefCounted {
    GDCLASS(Steamworks, godot::RefCounted)

    friend class SteamClient;

    // Weak identity of the owning internal client.
    godot::ObjectID client_id;
    // Generation captured at boot. A restarted client cannot revive this facade.
    std::weak_ptr<SteamClientScope> generation;
    // Outcome of attaching or resolving the requested client.
    godot::Ref<SteamResult> startup;
    // Single achievement surface for this facade.
    godot::Ref<SteamAchievements> achievement_service;

    // Resolve only the active main-thread client from this exact generation.
    SteamClient *resolve_client() const;
    // Forward native runtime retirement after its services have been invalidated.
    void on_runtime_lost();

protected:
    // Bind code-first boot and typed service access.
    static void _bind_methods();

public:
    // Boot a Steam client under an existing tree owner and return a scoped facade.
    static godot::Ref<Steamworks> boot(godot::Node *owner, const godot::Ref<SteamConfig> &settings = {});
    // Opt into Steam's shipping relaunch check before boot or early overlay initialization.
    static godot::Error restart_through_steam_if_needed(int64_t app_id);
    // Capture the generation of an already booted native client.
    static godot::Ref<Steamworks> from_client(SteamClient *client);
    // Supply an inert facade carrying the unavailable reason.
    static godot::Ref<Steamworks> unavailable(godot::Error error, const godot::String &message);
    // Report the result of the original attachment attempt.
    godot::Ref<SteamResult> boot_result() const;
    // Report whether this facade's exact client generation is usable.
    bool is_ready() const;
    // Report whether the exact client currently has a live connection to Steam.
    bool is_online() const;
    // Return this attachment's confirmed AppID or zero.
    int64_t get_app_id() const;
    // Return this attachment's signed-in SteamID64 as a decimal string.
    godot::String get_local_steam_id() const;
    // Request Steam's achievements pane, without requiring separate readiness checks.
    godot::Ref<SteamResult> open_achievements() const;
    // Return the achievement action surface for this attachment.
    godot::Ref<SteamAchievements> achievements() const;
    // Return this attachment's app and DLC information when available.
    godot::Ref<SteamApps> apps() const;
    // Return this attachment's friends and social data when available.
    godot::Ref<SteamFriends> friends() const;
    // Return this attachment's overlay controls and activation signals when available.
    godot::Ref<SteamOverlay> overlay() const;
    // Return this attachment's Steam utilities when available.
    godot::Ref<SteamUtils> utils() const;
    // Return this attachment's diagnostic controls when available.
    godot::Ref<SteamDiagnostics> diagnostics() const;
    // Return this attachment's HTTP client when available.
    godot::Ref<SteamHttp> http() const;
    // Return this attachment's stats and achievement editing when available.
    godot::Ref<SteamStats> stats() const;
    // Return this attachment's leaderboard service when available.
    godot::Ref<SteamLeaderboards> leaderboards() const;
    // Return this attachment's cloud file service when available.
    godot::Ref<SteamCloud> cloud() const;
    // Return this attachment's timeline event service when available.
    godot::Ref<SteamTimeline> timeline() const;
    // Return this attachment's screenshot service when available.
    godot::Ref<SteamScreenshots> screenshots() const;
    // Return this attachment's Remote Play service when available.
    godot::Ref<SteamRemotePlay> remote_play() const;
    // Return this attachment's Steam Inventory service when available.
    godot::Ref<SteamInventory> inventory() const;
    // Return this attachment's lobby service when available.
    godot::Ref<SteamLobbies> lobbies() const;
    // Return this attachment's dedicated server browser when available.
    godot::Ref<SteamServerBrowser> server_browser() const;
    // Return this attachment's Workshop service when available.
    godot::Ref<SteamWorkshop> workshop() const;
    // Return this attachment's rich presence service when available.
    godot::Ref<SteamPresence> presence() const;
    // Return this attachment's music service when available.
    godot::Ref<SteamMusic> music() const;
    // Return this attachment's video service when available.
    godot::Ref<SteamVideo> video() const;
    // Return this attachment's device service when available.
    godot::Ref<SteamDevice> device() const;
    // Return this attachment's authentication and ownership ticket service when available.
    godot::Ref<SteamAuth> auth() const;
    // Return this attachment's Steam Input service when available.
    godot::Ref<SteamInput> input() const;
    // Return this attachment's Steam browser service when available.
    godot::Ref<SteamBrowser> browser() const;
    // Return this attachment's purchase service when available.
    godot::Ref<SteamPurchases> purchases() const;
    // Return this attachment's voice service when available.
    godot::Ref<SteamVoice> voice() const;
    // Return this attachment's Steam Parties service when available.
    godot::Ref<SteamParties> parties() const;
    // Return this attachment's Steam Messages service when available.
    godot::Ref<SteamMessages> messages() const;
    // Return this attachment's route diagnostics when available.
    godot::Ref<SteamNetworking> networking() const;
    // Return this attachment's standalone Steam Sockets service when available.
    godot::Ref<SteamSockets> sockets() const;
};

} // namespace crimson::steam
