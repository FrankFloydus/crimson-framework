// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_server_runtime.hpp"

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/lobbies/steam_lobbies.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_route.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstring>
#include <utility>

namespace crimson::steam {
using namespace godot;

namespace {

// The Valve GameServer API exposes one initialized server pipe per process.
std::weak_ptr<SteamServerRuntime> dedicated_runtime;

// Allow complete current Steam socket status and ticket callbacks before SDK release.
constexpr size_t maximum_server_event_size = 4096;
// One result per admitted player, with capacity to drain calls left by departing players.
constexpr size_t maximum_server_calls = 128;
static_assert(sizeof(SteamNetConnectionStatusChangedCallback_t) <= maximum_server_event_size);
static_assert(sizeof(ValidateAuthTicketResponse_t) <= maximum_server_event_size);

// Steam game servers accept one operator-provided printable token, without logging it.
bool valid_token(const String &token) {
    if (token.is_empty() || token.length() > 128) {
        return false;
    }
    for (int64_t index = 0; index < token.length(); ++index) {
        const char32_t c = token[index];
        if (!((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9'))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::shared_ptr<SteamServerRuntime> SteamServerRuntime::acquire(const Ref<SteamServer> &options, uint32_t expected_app,
        const StringName &selected_namespace, String &error) {
    if (!SteamRuntime::on_main_thread() || options.is_null() || !options->is_valid() ||
            !valid_steam_lobby_namespace(String(selected_namespace))) {
        error = "The Steam dedicated server configuration is invalid.";
        return {};
    }
    if (options->get_advertisement() == SteamServer::LAN) {
        error = "Secure LAN-only Steam advertisement is unavailable.";
        return {};
    }
    if (!options->get_relay_profile().is_empty()) {
#ifndef CRIMSON_WITH_STEAM_SDR
        error = "This build does not include the Steam hosted relay SDK headers.";
        return {};
#else
        if (options->get_advertisement() != SteamServer::DIRECT || options->get_bind_ipv4() != 0) {
            error = "Steam hosted relay uses the provisioned interface and its trusted coordinator for discovery.";
            return {};
        }
#endif
    }
    if (!dedicated_runtime.expired()) {
        error = "A Steam dedicated server already owns this process's server API.";
        return {};
    }
    auto runtime = std::shared_ptr<SteamServerRuntime>(new SteamServerRuntime());
    runtime->settings = options;
    runtime->name_space = selected_namespace;
    if (!runtime->initialize(expected_app, error)) {
        return {};
    }
    dedicated_runtime = runtime;
    return runtime;
}

SteamServerRuntime::~SteamServerRuntime() {
    observers.clear();
    if (server) {
        unpublish();
        server->LogOff();
    }
    if (initialized) {
        symbol<decltype(&SteamGameServer_Shutdown)>("SteamGameServer_Shutdown")();
    }
    if (library) {
        FreeLibrary(static_cast<HMODULE>(library));
    }
}

void *SteamServerRuntime::find_symbol(const char *name) const {
    return library ? reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), name)) : nullptr;
}

bool SteamServerRuntime::initialize(uint32_t expected_app, String &error) {
    const std::wstring filename = SteamRuntime::packaged_library_path();
    if (filename.empty()) {
        error = "The Steam dedicated SDK path could not be resolved.";
        return false;
    }
    library = LoadLibraryExW(filename.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!library) {
        error = "The packaged Steam dedicated SDK could not be loaded.";
        return false;
    }
    const auto init = symbol<decltype(&SteamInternal_GameServer_Init_V2)>("SteamInternal_GameServer_Init_V2");
    const auto get_server = symbol<decltype(&SteamAPI_SteamGameServer_v015)>("SteamAPI_SteamGameServer_v015");
    const auto get_sockets = symbol<decltype(&SteamAPI_SteamGameServerNetworkingSockets_SteamAPI_v013)>(
            "SteamAPI_SteamGameServerNetworkingSockets_SteamAPI_v013");
    const auto get_utils = symbol<decltype(&SteamAPI_SteamGameServerUtils_v011)>("SteamAPI_SteamGameServerUtils_v011");
    const auto get_pipe = symbol<decltype(&SteamGameServer_GetHSteamPipe)>("SteamGameServer_GetHSteamPipe");
    const auto init_dispatch = symbol<decltype(&SteamAPI_ManualDispatch_Init)>("SteamAPI_ManualDispatch_Init");
    if (!init || !get_server || !get_sockets || !get_utils || !get_pipe || !init_dispatch ||
            !find_symbol("SteamGameServer_Shutdown") || !find_symbol("SteamAPI_ManualDispatch_RunFrame") ||
            !find_symbol("SteamAPI_ManualDispatch_GetNextCallback") || !find_symbol("SteamAPI_ManualDispatch_FreeLastCallback")) {
        error = "The packaged Steam SDK does not expose the required dedicated server interfaces.";
        return false;
    }

    const String secret_ref(settings->get_login_ref());
    const String token = secret_ref.is_empty() ? String() : OS::get_singleton()->get_environment(secret_ref);
    if (!secret_ref.is_empty() && !valid_token(token)) {
        error = "The configured Steam dedicated server login credential is missing or invalid.";
        return false;
    }
    constexpr const char interfaces[] = STEAMUTILS_INTERFACE_VERSION "\0"
            STEAMNETWORKINGUTILS_INTERFACE_VERSION "\0" STEAMGAMESERVER_INTERFACE_VERSION "\0"
            STEAMNETWORKINGSOCKETS_INTERFACE_VERSION "\0";
    SteamErrMsg ignored{};
    if (init(settings->get_bind_ipv4(), settings->get_game_port(), settings->get_query_port(),
                eServerModeAuthenticationAndSecure, settings->get_product_version().utf8().get_data(), interfaces,
                &ignored) != k_ESteamAPIInitResult_OK) {
        error = "Steam could not initialize its dedicated server API. Check the launch AppID and ports.";
        return false;
    }
    initialized = true;

    server = get_server();
    sockets = get_sockets();
    auto *utils = get_utils();
    pipe = get_pipe();
    app_id = utils ? utils->GetAppID() : 0;
    if (!server || !sockets || pipe == 0 || app_id == 0 || (expected_app != 0 && app_id != expected_app)) {
        error = "The dedicated Steam pipe is unavailable or uses a different AppID.";
        return false;
    }
#ifdef CRIMSON_WITH_STEAM_SDR
    if (!settings->get_relay_profile().is_empty() &&
            (sockets->GetHostedDedicatedServerPort() != settings->get_game_port() ||
                    sockets->GetHostedDedicatedServerPOPID() == 0)) {
        error = "The named Steam hosted relay profile is not provisioned on its configured gameplay UDP port.";
        return false;
    }
#endif
    init_dispatch();
    server->SetProduct(String::num_uint64(app_id).utf8().get_data());
    server->SetGameDescription(settings->get_game_description().utf8().get_data());
    server->SetModDir(settings->get_install_dir().utf8().get_data());
    server->SetDedicatedServer(true);
    server->SetServerName(settings->get_server_name().utf8().get_data());
    server->SetBotPlayerCount(0);
    if (token.is_empty()) {
        server->LogOnAnonymous();
    } else {
        server->LogOn(token.utf8().get_data());
    }
    sockets->InitAuthentication();
    error = String();
    return true;
}

ISteamNetworkingSockets *SteamServerRuntime::get_sockets() const { return sockets; }

uint64_t SteamServerRuntime::get_local_socket_id() const {
    if (server && server->BLoggedOn()) {
        const CSteamID id = server->GetSteamID();
        if (id.IsValid() && (id.GetEAccountType() == k_EAccountTypeGameServer ||
                                    id.GetEAccountType() == k_EAccountTypeAnonGameServer)) {
            return id.ConvertToUint64();
        }
    }
    return 0;
}

bool SteamServerRuntime::is_ready() const {
    return get_local_socket_id() != 0 && server->BSecure() && sockets &&
            sockets->GetAuthenticationStatus(nullptr) == k_ESteamNetworkingAvailability_Current;
}

uint32_t SteamServerRuntime::get_app_id() const { return app_id; }
ISteamGameServer *SteamServerRuntime::get_server() const { return server; }

ISteamGameServerStats *SteamServerRuntime::get_stats() const {
    const auto get_stats = symbol<decltype(&SteamAPI_SteamGameServerStats_v001)>("SteamAPI_SteamGameServerStats_v001");
    return get_stats && find_symbol("SteamAPI_ManualDispatch_GetAPICallResult") ? get_stats() : nullptr;
}

uint32_t SteamServerRuntime::get_public_ipv4() const {
    if (!server || !server->BLoggedOn()) {
        return 0;
    }
    const SteamIPAddress_t address = server->GetPublicIP();
    return address.m_eType == k_ESteamIPTypeIPv4 ? address.m_unIPv4 : 0;
}

bool SteamServerRuntime::is_listed() const { return listed; }

bool SteamServerRuntime::publish(const StringName &world, int32_t capacity) {
    const String world_id(world);
    if (!is_ready() || !valid_steam_lobby_world(world_id) || capacity < 1 || capacity > 64) {
        return false;
    }
    String display_map = world_id.left(k_cbMaxGameServerMapName - 1);
    while (display_map.utf8().length() >= k_cbMaxGameServerMapName) {
        display_map = display_map.left(display_map.length() - 1);
    }
    server->SetMapName(display_map.utf8().get_data());
    server->SetKeyValue("crimson_world", world_id.utf8().get_data());
    server->SetMaxPlayerCount(capacity);
    const String tag = String(STEAM_DIRECT_SERVER_PROTOCOL) + "," + steam_server_namespace_tag(name_space) +
            "," + steam_server_world_tag(world);
    server->SetGameTags(tag.utf8().get_data());
    if (settings->get_advertisement() == SteamServer::INTERNET) {
        server->SetAdvertiseServerActive(true);
        listed = true;
    }
    return true;
}

void SteamServerRuntime::unpublish() {
    if (std::exchange(listed, false)) {
        server->SetAdvertiseServerActive(false);
    }
}

uint64_t SteamServerRuntime::subscribe(int callback_id, CallbackHandler handler) {
    if (!handler || callback_id <= 0 || observers.size() >= 16 ||
            next_observer == (std::numeric_limits<uint64_t>::max)()) {
        return 0;
    }
    const uint64_t id = next_observer++;
    observers.emplace(id, Subscription{callback_id, std::move(handler)});
    return id;
}

void SteamServerRuntime::unsubscribe(uint64_t subscription) {
    observers.erase(subscription);
}

uint64_t SteamServerRuntime::get_callback_drain_epoch() const {
    return callback_drain_epoch;
}

bool SteamServerRuntime::can_track_call() const {
    return SteamRuntime::on_main_thread() && calls.size() < maximum_server_calls;
}

bool SteamServerRuntime::track_call(SteamAPICall_t call, int callback_id, size_t size,
        std::function<void(const void *)> handler) {
    return call != k_uAPICallInvalid && callback_id > 0 && size > 0 && size <= maximum_server_event_size &&
            handler && can_track_call() && calls.emplace(call, PendingCall{callback_id, size, std::move(handler)}).second;
}

void SteamServerRuntime::forget_call(SteamAPICall_t call) {
    calls.erase(call);
}

void SteamServerRuntime::dispatch(int callback_id, const void *data, size_t size) {
    std::array<uint64_t, 16> targets{};
    size_t count = 0;
    for (const auto &entry : observers) {
        if (entry.second.id == callback_id) {
            targets[count++] = entry.first;
        }
    }
    for (size_t index = 0; index < count; ++index) {
        const auto found = observers.find(targets[index]);
        if (found != observers.end()) {
            const CallbackHandler notify = found->second.callback;
            notify(data, size);
        }
    }
}

void SteamServerRuntime::pump() {
    const auto *engine = Engine::get_singleton();
    if (!engine || !SteamRuntime::on_main_thread() || pumping || last_frame == engine->get_process_frames()) {
        return;
    }
    last_frame = engine->get_process_frames();
    pumping = true;
    const auto run = symbol<decltype(&SteamAPI_ManualDispatch_RunFrame)>("SteamAPI_ManualDispatch_RunFrame");
    const auto next = symbol<decltype(&SteamAPI_ManualDispatch_GetNextCallback)>("SteamAPI_ManualDispatch_GetNextCallback");
    const auto free = symbol<decltype(&SteamAPI_ManualDispatch_FreeLastCallback)>("SteamAPI_ManualDispatch_FreeLastCallback");
    const auto get_result = symbol<decltype(&SteamAPI_ManualDispatch_GetAPICallResult)>(
            "SteamAPI_ManualDispatch_GetAPICallResult");
    run(pipe);
    for (size_t index = 0; index < 128; ++index) {
        CallbackMsg_t event{};
        if (!next(pipe, &event)) {
            if (callback_drain_epoch != (std::numeric_limits<uint64_t>::max)()) {
                ++callback_drain_epoch;
            }
            break;
        }
        alignas(std::max_align_t) std::array<std::byte, maximum_server_event_size> storage;
        const int count = event.m_cubParam;
        const bool copy = event.m_pubParam && count > 0 && static_cast<size_t>(count) <= storage.size();
        if (event.m_iCallback == SteamAPICallCompleted_t::k_iCallback) {
            std::function<void(const void *)> handler;
            bool successful = false;
            if (copy && count == sizeof(SteamAPICallCompleted_t)) {
                SteamAPICallCompleted_t completed{};
                std::memcpy(&completed, event.m_pubParam, sizeof(completed));
                const auto found = calls.find(completed.m_hAsyncCall);
                if (found != calls.end()) {
                    auto pending = std::move(found->second);
                    calls.erase(found);
                    handler = std::move(pending.handler);
                    bool io_failure = false;
                    successful = get_result && completed.m_iCallback == pending.callback_id &&
                            completed.m_cubParam == pending.size && get_result(pipe, completed.m_hAsyncCall,
                                    storage.data(), static_cast<int>(pending.size), pending.callback_id, &io_failure) &&
                            !io_failure;
                }
            }
            free(pipe);
            if (handler) {
                handler(successful ? storage.data() : nullptr);
            }
            continue;
        }
        if (copy) {
            std::memcpy(storage.data(), event.m_pubParam, count);
        }
        free(pipe);
        if (copy || count == 0) {
            dispatch(event.m_iCallback, copy ? storage.data() : nullptr, static_cast<size_t>(count));
        }
    }
    pumping = false;
}

} // namespace crimson::steam

#endif
