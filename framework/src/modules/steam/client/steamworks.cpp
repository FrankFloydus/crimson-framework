// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steamworks.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {
using namespace godot;
namespace {

// Restrict scene and SDK access to the main thread in SDK-on and SDK-off builds.
bool on_main_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

} // namespace

void SteamAchievements::_bind_methods() {
    ClassDB::bind_method(D_METHOD("unlock", "name"), &SteamAchievements::unlock);
}

Ref<SteamStatsSubmissionResult> SteamAchievements::unlock(const String &name) const {
    if (!on_main_thread()) {
        return SteamStatsSubmissionResult::create(ERR_BUSY, 0, -1, "Call Steam achievements from the main thread.");
    }
    if (stats_service.is_valid()) {
        return stats_service->edit()->unlock(name)->submit();
    }
    return startup.is_valid() && !startup->is_ok() ?
            SteamStatsSubmissionResult::create(startup->get_error(), 0, -1, startup->get_message()) :
            SteamStatsSubmissionResult::create(ERR_UNAVAILABLE);
}

void Steamworks::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("boot", "owner", "settings"), &Steamworks::boot,
            DEFVAL(Ref<SteamConfig>()));
    ClassDB::bind_static_method(get_class_static(), D_METHOD("restart_through_steam_if_needed", "app_id"),
            &Steamworks::restart_through_steam_if_needed);
    ClassDB::bind_method(D_METHOD("boot_result"), &Steamworks::boot_result);
    ClassDB::bind_method(D_METHOD("is_ready"), &Steamworks::is_ready);
    ClassDB::bind_method(D_METHOD("is_online"), &Steamworks::is_online);
    ClassDB::bind_method(D_METHOD("get_app_id"), &Steamworks::get_app_id);
    ClassDB::bind_method(D_METHOD("get_local_steam_id"), &Steamworks::get_local_steam_id);
    ClassDB::bind_method(D_METHOD("open_achievements"), &Steamworks::open_achievements);
    ClassDB::bind_method(D_METHOD("achievements"), &Steamworks::achievements);
    ClassDB::bind_method(D_METHOD("apps"), &Steamworks::apps);
    ClassDB::bind_method(D_METHOD("friends"), &Steamworks::friends);
    ClassDB::bind_method(D_METHOD("overlay"), &Steamworks::overlay);
    ClassDB::bind_method(D_METHOD("utils"), &Steamworks::utils);
    ClassDB::bind_method(D_METHOD("diagnostics"), &Steamworks::diagnostics);
    ClassDB::bind_method(D_METHOD("http"), &Steamworks::http);
    ClassDB::bind_method(D_METHOD("stats"), &Steamworks::stats);
    ClassDB::bind_method(D_METHOD("leaderboards"), &Steamworks::leaderboards);
    ClassDB::bind_method(D_METHOD("cloud"), &Steamworks::cloud);
    ClassDB::bind_method(D_METHOD("timeline"), &Steamworks::timeline);
    ClassDB::bind_method(D_METHOD("screenshots"), &Steamworks::screenshots);
    ClassDB::bind_method(D_METHOD("remote_play"), &Steamworks::remote_play);
    ClassDB::bind_method(D_METHOD("inventory"), &Steamworks::inventory);
    ClassDB::bind_method(D_METHOD("lobbies"), &Steamworks::lobbies);
    ClassDB::bind_method(D_METHOD("server_browser"), &Steamworks::server_browser);
    ClassDB::bind_method(D_METHOD("workshop"), &Steamworks::workshop);
    ClassDB::bind_method(D_METHOD("presence"), &Steamworks::presence);
    ClassDB::bind_method(D_METHOD("music"), &Steamworks::music);
    ClassDB::bind_method(D_METHOD("video"), &Steamworks::video);
    ClassDB::bind_method(D_METHOD("device"), &Steamworks::device);
    ClassDB::bind_method(D_METHOD("auth"), &Steamworks::auth);
    ClassDB::bind_method(D_METHOD("input"), &Steamworks::input);
    ClassDB::bind_method(D_METHOD("browser"), &Steamworks::browser);
    ClassDB::bind_method(D_METHOD("purchases"), &Steamworks::purchases);
    ClassDB::bind_method(D_METHOD("voice"), &Steamworks::voice);
    ClassDB::bind_method(D_METHOD("parties"), &Steamworks::parties);
    ClassDB::bind_method(D_METHOD("messages"), &Steamworks::messages);
    ClassDB::bind_method(D_METHOD("networking"), &Steamworks::networking);
    ClassDB::bind_method(D_METHOD("sockets"), &Steamworks::sockets);
    ADD_SIGNAL(MethodInfo("runtime_lost"));
}

Error Steamworks::restart_through_steam_if_needed(int64_t app_id) {
    return SteamClient::restart_through_steam_if_needed(app_id);
}

Ref<Steamworks> Steamworks::boot(Node *owner, const Ref<SteamConfig> &settings) {
    if (!on_main_thread()) {
        return unavailable(ERR_BUSY, "Boot Steamworks from the main thread.");
    }
    if (settings.is_valid() && !settings->is_valid()) {
        return unavailable(ERR_INVALID_PARAMETER,
                "Supply a valid Steam AppID and a test group of 1 to 64 characters when using Spacewar.");
    }
    Error started = ERR_UNCONFIGURED;
    SteamClient *client = SteamClient::attach_and_start(owner, settings.is_valid() ? settings->get_app_id() : 0,
            started, true);
    if (!client) {
        return unavailable(started, started == ERR_INVALID_PARAMETER ?
                "Attach Steamworks to an existing node in the scene tree." :
                "The scene tree could not attach the internal Steam client.");
    }
    if (started != OK) {
        const String message = client->get_initialization_error();
        client->queue_free();
        return unavailable(started, message);
    }
    return from_client(client);
}

Ref<Steamworks> Steamworks::from_client(SteamClient *client) {
    if (!on_main_thread()) {
        return unavailable(ERR_BUSY, "Resolve Steamworks from the main thread.");
    }
    if (!client || !client->is_ready() || !client->is_inside_tree() || client->is_queued_for_deletion() ||
            !client->get_scope_generation()) {
        return unavailable(ERR_UNAVAILABLE, "Steam client services are unavailable for this attachment.");
    }
    const Ref<Steamworks> existing(Object::cast_to<Steamworks>(ObjectDB::get_instance(client->code_first_facade_id)));
    if (existing.is_valid() && existing->generation.lock() == client->get_scope_generation()) {
        return existing;
    }
    Ref<Steamworks> result;
    result.instantiate();
    result->client_id = ObjectID(client->get_instance_id());
    result->generation = client->get_scope_generation();
    result->startup = SteamResult::from_error(OK);
    result->achievement_service.instantiate();
    result->achievement_service->stats_service = client->stats();
    client->code_first_facade_id = ObjectID(result->get_instance_id());
    return result;
}

void Steamworks::on_runtime_lost() {
    const Ref<Steamworks> keep_alive(this);
    emit_signal("runtime_lost");
}

Ref<Steamworks> Steamworks::unavailable(Error error, const String &message) {
    Ref<Steamworks> result;
    result.instantiate();
    result->startup = SteamResult::from_error(error, message);
    result->achievement_service.instantiate();
    result->achievement_service->startup = result->startup;
    return result;
}

SteamClient *Steamworks::resolve_client() const {
    if (!on_main_thread()) {
        return nullptr;
    }
    const auto scope = generation.lock();
    if (!scope || !scope->active) {
        return nullptr;
    }
    auto *client = Object::cast_to<SteamClient>(ObjectDB::get_instance(client_id));
    return client && client->get_scope_generation() == scope && client->is_inside_tree() &&
            !client->is_queued_for_deletion() && client->is_ready() ? client : nullptr;
}

Ref<SteamResult> Steamworks::boot_result() const {
    return startup.is_valid() ? startup : SteamResult::from_error(ERR_UNCONFIGURED, "Boot a Steamworks attachment first.");
}

bool Steamworks::is_ready() const { return resolve_client() != nullptr; }

bool Steamworks::is_online() const {
    const auto *client = resolve_client();
    return client && client->is_online();
}

int64_t Steamworks::get_app_id() const {
    const auto *client = resolve_client();
    return client ? client->get_app_id() : 0;
}

String Steamworks::get_local_steam_id() const {
    const auto *client = resolve_client();
    return client ? client->get_local_steam_id() : String();
}

Ref<SteamResult> Steamworks::open_achievements() const {
    if (!on_main_thread()) {
        return SteamResult::from_error(ERR_BUSY, "Open Steam achievements from the main thread.");
    }
    const auto *client = resolve_client();
    if (!client) {
        return startup.is_valid() && !startup->is_ok() ? startup :
                SteamResult::from_error(ERR_UNAVAILABLE, "The Steamworks client attachment is no longer available.");
    }
    const Ref<SteamOverlay> overlay = client->overlay();
    if (overlay.is_null()) {
        return SteamResult::from_error(ERR_UNAVAILABLE, "The running Steam client does not expose its overlay.");
    }
    const Error result = overlay->open_achievements();
    if (result == ERR_BUSY) {
        return SteamResult::from_error(result, "Steam's overlay is not ready. Enable the overlay and try again.");
    }
    return SteamResult::from_error(result);
}

Ref<SteamAchievements> Steamworks::achievements() const {
    return achievement_service;
}

Ref<SteamApps> Steamworks::apps() const {
    const auto *client = resolve_client();
    return client ? client->apps() : Ref<SteamApps>();
}

Ref<SteamFriends> Steamworks::friends() const {
    const auto *client = resolve_client();
    return client ? client->friends() : Ref<SteamFriends>();
}

Ref<SteamOverlay> Steamworks::overlay() const {
    const auto *client = resolve_client();
    return client ? client->overlay() : Ref<SteamOverlay>();
}

Ref<SteamUtils> Steamworks::utils() const {
    const auto *client = resolve_client();
    return client ? client->utils() : Ref<SteamUtils>();
}

Ref<SteamDiagnostics> Steamworks::diagnostics() const {
    const auto *client = resolve_client();
    return client ? client->diagnostics() : Ref<SteamDiagnostics>();
}

Ref<SteamHttp> Steamworks::http() const {
    const auto *client = resolve_client();
    return client ? client->http() : Ref<SteamHttp>();
}

Ref<SteamStats> Steamworks::stats() const {
    const auto *client = resolve_client();
    return client ? client->stats() : Ref<SteamStats>();
}

Ref<SteamLeaderboards> Steamworks::leaderboards() const {
    const auto *client = resolve_client();
    return client ? client->leaderboards() : Ref<SteamLeaderboards>();
}

Ref<SteamCloud> Steamworks::cloud() const {
    const auto *client = resolve_client();
    return client ? client->cloud() : Ref<SteamCloud>();
}

Ref<SteamTimeline> Steamworks::timeline() const {
    const auto *client = resolve_client();
    return client ? client->timeline() : Ref<SteamTimeline>();
}

Ref<SteamScreenshots> Steamworks::screenshots() const {
    const auto *client = resolve_client();
    return client ? client->screenshots() : Ref<SteamScreenshots>();
}

Ref<SteamRemotePlay> Steamworks::remote_play() const {
    const auto *client = resolve_client();
    return client ? client->remote_play() : Ref<SteamRemotePlay>();
}

Ref<SteamInventory> Steamworks::inventory() const {
    const auto *client = resolve_client();
    return client ? client->inventory() : Ref<SteamInventory>();
}

Ref<SteamLobbies> Steamworks::lobbies() const {
    const auto *client = resolve_client();
    return client ? client->lobbies() : Ref<SteamLobbies>();
}

Ref<SteamServerBrowser> Steamworks::server_browser() const {
    const auto *client = resolve_client();
    return client ? client->server_browser() : Ref<SteamServerBrowser>();
}

Ref<SteamWorkshop> Steamworks::workshop() const {
    const auto *client = resolve_client();
    return client ? client->workshop() : Ref<SteamWorkshop>();
}

Ref<SteamPresence> Steamworks::presence() const {
    const auto *client = resolve_client();
    return client ? client->presence() : Ref<SteamPresence>();
}

Ref<SteamMusic> Steamworks::music() const {
    const auto *client = resolve_client();
    return client ? client->music() : Ref<SteamMusic>();
}

Ref<SteamVideo> Steamworks::video() const {
    const auto *client = resolve_client();
    return client ? client->video() : Ref<SteamVideo>();
}

Ref<SteamDevice> Steamworks::device() const {
    const auto *client = resolve_client();
    return client ? client->device() : Ref<SteamDevice>();
}

Ref<SteamAuth> Steamworks::auth() const {
    const auto *client = resolve_client();
    return client ? client->auth() : Ref<SteamAuth>();
}

Ref<SteamInput> Steamworks::input() const {
    const auto *client = resolve_client();
    return client ? client->input() : Ref<SteamInput>();
}

Ref<SteamBrowser> Steamworks::browser() const {
    const auto *client = resolve_client();
    return client ? client->browser() : Ref<SteamBrowser>();
}

Ref<SteamPurchases> Steamworks::purchases() const {
    const auto *client = resolve_client();
    return client ? client->purchases() : Ref<SteamPurchases>();
}

Ref<SteamVoice> Steamworks::voice() const {
    const auto *client = resolve_client();
    return client ? client->voice() : Ref<SteamVoice>();
}

Ref<SteamParties> Steamworks::parties() const {
    const auto *client = resolve_client();
    return client ? client->parties() : Ref<SteamParties>();
}

Ref<SteamMessages> Steamworks::messages() const {
    const auto *client = resolve_client();
    return client ? client->messages() : Ref<SteamMessages>();
}

Ref<SteamNetworking> Steamworks::networking() const {
    const auto *client = resolve_client();
    return client ? client->networking() : Ref<SteamNetworking>();
}

Ref<SteamSockets> Steamworks::sockets() const {
    const auto *client = resolve_client();
    return client ? client->sockets() : Ref<SteamSockets>();
}

} // namespace crimson::steam
