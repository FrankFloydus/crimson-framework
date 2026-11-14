// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_client.hpp"
#include "modules/steam/client/steamworks.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <limits>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <string>
#include <utility>
#endif

namespace crimson::steam {
using namespace godot;

void SteamClient::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("restart_through_steam_if_needed", "app_id"),
            &SteamClient::restart_through_steam_if_needed);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("boot", "owner", "expected_app_id"), &SteamClient::boot, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("start", "expected_app_id"), &SteamClient::start, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("stop"), &SteamClient::stop);
    ClassDB::bind_method(D_METHOD("is_ready"), &SteamClient::is_ready);
    ClassDB::bind_method(D_METHOD("is_online"), &SteamClient::is_online);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamClient::get_app_id);
    ClassDB::bind_method(D_METHOD("get_local_steam_id"), &SteamClient::get_local_steam_id);
    ClassDB::bind_method(D_METHOD("get_initialization_error"), &SteamClient::get_initialization_error);
    ClassDB::bind_method(D_METHOD("apps"), &SteamClient::apps);
    ClassDB::bind_method(D_METHOD("friends"), &SteamClient::friends);
    ClassDB::bind_method(D_METHOD("overlay"), &SteamClient::overlay);
    ClassDB::bind_method(D_METHOD("utils"), &SteamClient::utils);
    ClassDB::bind_method(D_METHOD("diagnostics"), &SteamClient::diagnostics);
    ClassDB::bind_method(D_METHOD("http"), &SteamClient::http);
    ClassDB::bind_method(D_METHOD("stats"), &SteamClient::stats);
    ClassDB::bind_method(D_METHOD("leaderboards"), &SteamClient::leaderboards);
    ClassDB::bind_method(D_METHOD("cloud"), &SteamClient::cloud);
    ClassDB::bind_method(D_METHOD("timeline"), &SteamClient::timeline);
    ClassDB::bind_method(D_METHOD("screenshots"), &SteamClient::screenshots);
    ClassDB::bind_method(D_METHOD("remote_play"), &SteamClient::remote_play);
    ClassDB::bind_method(D_METHOD("inventory"), &SteamClient::inventory);
    ClassDB::bind_method(D_METHOD("lobbies"), &SteamClient::lobbies);
    ClassDB::bind_method(D_METHOD("server_browser"), &SteamClient::server_browser);
    ClassDB::bind_method(D_METHOD("workshop"), &SteamClient::workshop);
    ClassDB::bind_method(D_METHOD("presence"), &SteamClient::presence);
    ClassDB::bind_method(D_METHOD("music"), &SteamClient::music);
    ClassDB::bind_method(D_METHOD("video"), &SteamClient::video);
    ClassDB::bind_method(D_METHOD("device"), &SteamClient::device);
    ClassDB::bind_method(D_METHOD("auth"), &SteamClient::auth);
    ClassDB::bind_method(D_METHOD("input"), &SteamClient::input);
    ClassDB::bind_method(D_METHOD("browser"), &SteamClient::browser);
    ClassDB::bind_method(D_METHOD("purchases"), &SteamClient::purchases);
    ClassDB::bind_method(D_METHOD("voice"), &SteamClient::voice);
    ClassDB::bind_method(D_METHOD("parties"), &SteamClient::parties);
    ClassDB::bind_method(D_METHOD("messages"), &SteamClient::messages);
    ClassDB::bind_method(D_METHOD("networking"), &SteamClient::networking);
    ClassDB::bind_method(D_METHOD("sockets"), &SteamClient::sockets);
    ADD_SIGNAL(MethodInfo("runtime_lost"));
}

Error SteamClient::restart_through_steam_if_needed(int64_t app_id) {
    if (app_id <= 0 || app_id > (std::numeric_limits<uint32_t>::max)() || app_id == 480) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    return SteamRuntime::restart_through_steam_if_needed(static_cast<uint32_t>(app_id));
#else
    return ERR_UNAVAILABLE;
#endif
}

SteamClient *SteamClient::boot(Node *owner, int64_t expected_app_id) {
    Error started = ERR_UNCONFIGURED;
    return attach_and_start(owner, expected_app_id, started, false);
}

SteamClient *SteamClient::attach_and_start(Node *owner, int64_t expected_app_id, Error &started, bool internal) {
    const OS *os = OS::get_singleton();
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id()) {
        started = ERR_BUSY;
        return nullptr;
    }
    if (!owner || !owner->is_inside_tree() || owner->is_queued_for_deletion()) {
        started = ERR_INVALID_PARAMETER;
        return nullptr;
    }

    started = ERR_CANT_CREATE;
    auto *client = memnew(SteamClient);
    const uint64_t client_id = client->get_instance_id();
    const uint64_t owner_id = owner->get_instance_id();
    owner->add_child(client, false, internal ? Node::INTERNAL_MODE_BACK : Node::INTERNAL_MODE_DISABLED);
    // Scene-tree notifications may remove, reparent, or queue the client for deletion.
    client = Object::cast_to<SteamClient>(ObjectDB::get_instance(client_id));
    if (!client) {
        return nullptr;
    }
    Node *parent = client->get_parent();
    if (!parent) {
        memdelete(client);
        return nullptr;
    }
    if (parent->get_instance_id() != owner_id || parent->is_queued_for_deletion() ||
            !client->is_inside_tree() || client->is_queued_for_deletion()) {
        if (internal && !client->is_queued_for_deletion()) {
            client->queue_free();
        }
        return nullptr;
    }
    started = client->start(expected_app_id);
    return client;
}

Error SteamClient::start(int64_t expected_app_id) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (!is_inside_tree() || exiting_tree || expected_app_id < 0 || expected_app_id > (std::numeric_limits<uint32_t>::max)()) {
        initialization_error = "Attach the Steam client to the scene tree and supply a valid AppID.";
        return ERR_INVALID_PARAMETER;
    }
    if (runtime) {
        if (!runtime->is_healthy()) {
            initialization_error = "Steam is retiring its failed client connection. Retry after the runtime_lost callback returns.";
            return ERR_BUSY;
        }
        if (expected_app_id == 0 || expected_app_id == runtime->get_app_id()) {
            initialization_error = String();
            return OK;
        }
        initialization_error = "This Steam client already uses a different AppID.";
        return ERR_ALREADY_IN_USE;
    }

    runtime = SteamRuntime::acquire(static_cast<uint32_t>(expected_app_id), initialization_error);
    if (!runtime) {
        return ERR_CANT_CONNECT;
    }
    scope = std::make_shared<SteamClientScope>();
    apps_service.instantiate();
    apps_service->attach(runtime, scope);
    friends_service.instantiate();
    friends_service->runtime = runtime;
    overlay_service.instantiate();
    if (!overlay_service->attach(runtime, scope)) {
        overlay_service.unref();
    }
    utils_service.instantiate();
    utils_service->runtime = runtime;
    diagnostics_service.instantiate();
    diagnostics_service->attach(runtime, scope);
    http_service.instantiate();
    http_service->attach(runtime, scope);
    stats_service.instantiate();
    stats_service->start_scope(runtime, scope);
    leaderboards_service.instantiate();
    leaderboards_service->start_scope(runtime, scope);
    cloud_service.instantiate();
    cloud_service->configure(runtime, scope);
    timeline_service.instantiate();
    timeline_service->configure(runtime, scope);
    screenshots_service.instantiate();
    screenshots_service->attach(runtime, scope);
    remote_play_service.instantiate();
    remote_play_service->attach(runtime, scope);
    inventory_service.instantiate();
    inventory_service->attach(runtime, scope);
    lobby_service.instantiate();
    if (!game_lobby_namespace.is_empty()) {
        lobby_service->configure_game(game_lobby_namespace);
    }
    lobby_service->attach(runtime, scope);
    server_browser_service.instantiate();
    server_browser_service->attach(runtime, scope, game_lobby_namespace);
    workshop_service.instantiate();
    workshop_service->configure(runtime, scope);
    presence_service.instantiate();
    presence_service->configure(runtime, scope);
    music_service.instantiate();
    music_service->configure(runtime, scope);
    video_service.instantiate();
    video_service->configure(runtime, scope);
    device_service.instantiate();
    device_service->attach(runtime, scope);
    auth_service.instantiate();
    auth_service->configure(runtime, scope);
    input_service.instantiate();
    input_service->attach(runtime, scope);
    browser_service.instantiate();
    browser_service->attach(runtime, scope);
    purchases_service.instantiate();
    purchases_service->configure(runtime, scope);
    voice_service.instantiate();
    voice_service->attach(runtime, scope);
    parties_service.instantiate();
    parties_service->attach(runtime, scope);
    messages_service.instantiate();
    messages_service->attach(runtime, scope);
    networking_service.instantiate();
    networking_service->attach(runtime, scope);
    sockets_service.instantiate();
    sockets_service->attach(runtime, scope);
    const ObjectID node_id(get_instance_id());
    const std::weak_ptr<SteamClientScope> generation = scope;
    runtime->track_client(get_instance_id(), scope, [node_id, generation]() {
        auto *client = Object::cast_to<SteamClient>(ObjectDB::get_instance(node_id));
        const auto expected = generation.lock();
        if (!client || !expected || client->scope != expected) {
            return;
        }
        const Ref<Steamworks> facade(Object::cast_to<Steamworks>(
                ObjectDB::get_instance(client->code_first_facade_id)));
        client->initialization_error = "Steam lost its local connection. Start a fresh client scope to reconnect.";
        client->stop();
        client = Object::cast_to<SteamClient>(ObjectDB::get_instance(node_id));
        if (client) {
            client->emit_signal("runtime_lost");
        }
        if (facade.is_valid() && facade->generation.lock() == expected) {
            facade->on_runtime_lost();
        }
    });
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process(true);
    return OK;
#else
    initialization_error = "This Crimson build has Steamworks disabled.";
    return ERR_UNAVAILABLE;
#endif
}

void SteamClient::stop() {
#ifdef CRIMSON_WITH_STEAM
    if (!runtime || !SteamRuntime::on_main_thread()) {
        return;
    }
    set_process(false);
    scope->active = false;
    scope.reset();
    code_first_facade_id = ObjectID();
    const auto active = std::exchange(runtime, {});
    active->forget_client(get_instance_id());
    const auto old_http = std::exchange(http_service, {});
    const auto old_stats = std::exchange(stats_service, {});
    const auto old_leaderboards = std::exchange(leaderboards_service, {});
    const auto old_cloud = std::exchange(cloud_service, {});
    const auto old_timeline = std::exchange(timeline_service, {});
    const auto old_screenshots = std::exchange(screenshots_service, {});
    const auto old_remote_play = std::exchange(remote_play_service, {});
    const auto old_inventory = std::exchange(inventory_service, {});
    const auto old_lobbies = std::exchange(lobby_service, {});
    const auto old_server_browser = std::exchange(server_browser_service, {});
    const auto old_workshop = std::exchange(workshop_service, {});
    const auto old_presence = std::exchange(presence_service, {});
    const auto old_music = std::exchange(music_service, {});
    const auto old_video = std::exchange(video_service, {});
    const auto old_device = std::exchange(device_service, {});
    const auto old_auth = std::exchange(auth_service, {});
    const auto old_input = std::exchange(input_service, {});
    const auto old_browser = std::exchange(browser_service, {});
    const auto old_purchases = std::exchange(purchases_service, {});
    const auto old_voice = std::exchange(voice_service, {});
    const auto old_parties = std::exchange(parties_service, {});
    const auto old_messages = std::exchange(messages_service, {});
    const auto old_networking = std::exchange(networking_service, {});
    const auto old_sockets = std::exchange(sockets_service, {});
    apps_service->detach();
    friends_service->runtime.reset();
    if (overlay_service.is_valid()) {
        overlay_service->detach();
        overlay_service.unref();
    }
    utils_service->detach();
    diagnostics_service->detach();
    apps_service.unref();
    friends_service.unref();
    utils_service.unref();
    diagnostics_service.unref();
    old_remote_play->detach();
    old_timeline->retire_phase(active);
    old_cloud->retire_write_batch();
    old_networking->detach();
    old_voice->detach();
    old_input->detach();
    old_presence->close();
    old_music->close();

    // Cancellation may resume a caller that restarts or frees this node.
    old_sockets->detach();
    old_messages->detach();
    old_parties->detach();
    old_purchases->close();
    old_browser->detach();
    old_auth->close();
    old_lobbies->detach();
    old_server_browser->detach();
    old_video->close();
    old_device->detach();
    old_inventory->close();
    old_workshop->close();
    old_screenshots->detach();
    old_stats->close();
    old_http->detach();
    old_leaderboards->close();
    old_cloud->close();
    old_timeline->close();
#endif
}

bool SteamClient::is_ready() const {
#ifdef CRIMSON_WITH_STEAM
    return runtime && runtime->is_healthy();
#else
    return false;
#endif
}

bool SteamClient::is_online() const {
#ifdef CRIMSON_WITH_STEAM
    return runtime && runtime->is_online();
#else
    return false;
#endif
}

int64_t SteamClient::get_app_id() const {
#ifdef CRIMSON_WITH_STEAM
    return is_ready() ? runtime->get_app_id() : 0;
#else
    return 0;
#endif
}

String SteamClient::get_local_steam_id() const {
#ifdef CRIMSON_WITH_STEAM
    if (is_ready()) {
        return String::utf8(std::to_string(runtime->get_user_id()).c_str());
    }
#endif
    return {};
}

String SteamClient::get_initialization_error() const {
    return initialization_error;
}

Ref<SteamApps> SteamClient::apps() const {
    return apps_service;
}

Ref<SteamFriends> SteamClient::friends() const {
    return friends_service;
}

Ref<SteamOverlay> SteamClient::overlay() const {
    return overlay_service;
}

Ref<SteamUtils> SteamClient::utils() const {
    return utils_service;
}

Ref<SteamDiagnostics> SteamClient::diagnostics() const {
    return diagnostics_service;
}

Ref<SteamHttp> SteamClient::http() const {
    return http_service;
}

Ref<SteamStats> SteamClient::stats() const {
    return stats_service;
}

Ref<SteamLeaderboards> SteamClient::leaderboards() const {
    return leaderboards_service;
}

Ref<SteamCloud> SteamClient::cloud() const {
    return cloud_service;
}

Ref<SteamTimeline> SteamClient::timeline() const {
    return timeline_service;
}

Ref<SteamScreenshots> SteamClient::screenshots() const {
    return screenshots_service;
}

Ref<SteamRemotePlay> SteamClient::remote_play() const {
    return remote_play_service;
}

Ref<SteamInventory> SteamClient::inventory() const {
    return inventory_service;
}

Ref<SteamLobbies> SteamClient::lobbies() const {
    return lobby_service;
}

Ref<SteamServerBrowser> SteamClient::server_browser() const {
    return server_browser_service;
}

void SteamClient::configure_game_lobbies(const StringName &name_space) {
    if (!is_inside_tree()) {
        game_lobby_namespace = name_space;
    }
}

Ref<SteamWorkshop> SteamClient::workshop() const {
    return workshop_service;
}

Ref<SteamPresence> SteamClient::presence() const {
    return presence_service;
}

Ref<SteamMusic> SteamClient::music() const {
    return music_service;
}

Ref<SteamVideo> SteamClient::video() const {
    return video_service;
}

Ref<SteamDevice> SteamClient::device() const {
    return device_service;
}

Ref<SteamAuth> SteamClient::auth() const {
    return auth_service;
}

Ref<SteamInput> SteamClient::input() const {
    return input_service;
}

Ref<SteamBrowser> SteamClient::browser() const {
    return browser_service;
}

Ref<SteamPurchases> SteamClient::purchases() const {
    return purchases_service;
}

Ref<SteamVoice> SteamClient::voice() const {
    return voice_service;
}

Ref<SteamParties> SteamClient::parties() const {
    return parties_service;
}

Ref<SteamMessages> SteamClient::messages() const {
    return messages_service;
}

Ref<SteamNetworking> SteamClient::networking() const {
    return networking_service;
}

Ref<SteamSockets> SteamClient::sockets() const {
    return sockets_service;
}

void SteamClient::_enter_tree() {
    exiting_tree = false;
}

const std::shared_ptr<SteamClientScope> &SteamClient::get_scope_generation() const {
    return scope;
}

void SteamClient::retire_scope_generation(const std::shared_ptr<SteamClientScope> &expected) {
    if (expected && scope == expected) {
        exiting_tree = true;
        stop();
    }
}

void SteamClient::_process(double) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime;
    if (active) {
        const auto http = http_service;
        const auto leaderboards = leaderboards_service;
        const auto cloud = cloud_service;
        const auto timeline = timeline_service;
        const auto screenshots = screenshots_service;
        const auto inventory = inventory_service;
        const auto lobbies = lobby_service;
        const auto server_browser = server_browser_service;
        const auto workshop = workshop_service;
        const auto video = video_service;
        const auto device = device_service;
        const auto auth = auth_service;
        const auto input = input_service;
        const auto browser = browser_service;
        const auto purchases = purchases_service;
        const auto voice = voice_service;
        const auto parties = parties_service;
        const auto messages = messages_service;
        const auto sockets = sockets_service;
        active->pump();
        if (!active->is_healthy()) {
            return;
        }
        SteamLobbies::poll();
        lobbies->tick();
        server_browser->tick();
        http->poll();
        leaderboards->tick();
        cloud->tick();
        timeline->tick();
        screenshots->tick();
        inventory->tick();
        workshop->tick();
        video->tick();
        device->tick();
        auth->tick();
        input->tick();
        browser->tick();
        purchases->tick();
        voice->tick();
        parties->tick();
        messages->tick();
        sockets->tick();
    }
#endif
}

void SteamClient::_exit_tree() {
    exiting_tree = true;
    stop();
}

} // namespace crimson::steam
