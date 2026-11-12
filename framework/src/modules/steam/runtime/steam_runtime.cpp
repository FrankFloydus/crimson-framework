// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/runtime/steam_runtime.hpp"

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/lobbies/steam_lobby_lease.hpp"
#include "modules/steam/runtime/steam_html_callback.hpp"
#include "modules/steam/runtime/steam_keyboard_callback.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace godot;

namespace crimson::steam {
namespace {

// Maximum SDK event size copied before returning the manual dispatch buffer.
constexpr size_t maximum_callback_size = 4096;
// Limit work and retainers owned by a single client runtime.
constexpr size_t maximum_callbacks_per_pump = 128;
constexpr size_t maximum_calls = 32;
constexpr size_t maximum_subscriptions = 64;
// Bound total off-screen browser memory and asynchronous create requests per Steam client.
constexpr size_t maximum_html_browsers = 4;
// Yield the native queue before dropping a newer paint from an owned browser.
constexpr uint8_t maximum_html_paints_per_browser = 2;
// The client API is process-wide. The current scene's consumers own it jointly.
std::weak_ptr<SteamRuntime> client_runtime;
// A graphical opt-in retains the same Steam runtime across game scope changes.
std::shared_ptr<SteamRuntime> overlay_runtime;
// The shipping check must precede even an unsuccessful native initialization attempt.
bool client_init_attempted = false;
// Identifies the GDExtension module without consulting the process working directory.
const char framework_module_anchor = 0;

} // namespace

std::wstring SteamRuntime::packaged_library_path() {
    HMODULE framework = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&framework_module_anchor), &framework)) {
        return {};
    }
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(framework, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    std::wstring filename(path.data(), length);
    const size_t directory = filename.find_last_of(L"/\\");
    return directory == std::wstring::npos ? std::wstring() : filename.substr(0, directory + 1) + L"steam_api64.dll";
}

bool SteamRuntime::on_main_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

std::shared_ptr<SteamRuntime> SteamRuntime::acquire(uint32_t expected_app_id, String &error) {
    if (!on_main_thread()) {
        error = "Steam can only initialize on the scene thread.";
        return {};
    }
    if (auto active = client_runtime.lock(); active && active->initialized) {
        if (!active->is_healthy()) {
            error = "Steam is retiring its failed client connection. Retry after the runtime_lost callback returns.";
            return {};
        }
        if (expected_app_id != 0 && active->app_id != expected_app_id) {
            error = "The active Steam client API uses a different AppID.";
            return {};
        }
        error = String();
        return active;
    }
    const std::shared_ptr<SteamRuntime> fresh(new SteamRuntime());
    if (!fresh->initialize(expected_app_id, error)) {
        return {};
    }
    client_runtime = fresh;
    return fresh;
}

Error SteamRuntime::restart_through_steam_if_needed(uint32_t app_id) {
    if (!on_main_thread() || client_init_attempted) {
        return ERR_BUSY;
    }
    const auto *display = DisplayServer::get_singleton();
    if (!display || display->get_name() == "headless" || Engine::get_singleton()->is_editor_hint()) {
        return ERR_UNAVAILABLE;
    }
    const std::wstring filename = packaged_library_path();
    const HMODULE module = filename.empty() ? nullptr : LoadLibraryExW(filename.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        return ERR_UNAVAILABLE;
    }
    const auto restart = reinterpret_cast<decltype(&SteamAPI_RestartAppIfNecessary)>(
            GetProcAddress(module, "SteamAPI_RestartAppIfNecessary"));
    const Error result = !restart ? ERR_UNAVAILABLE : restart(app_id) ? ERR_SKIP : OK;
    FreeLibrary(module);
    return result;
}

void SteamRuntime::prepare_overlay_from_launch() {
    constexpr const char *option = "--crimson-steam-overlay=";
    OS *os = OS::get_singleton();
    if (!os || overlay_runtime) {
        return;
    }
    const PackedStringArray arguments = os->get_cmdline_user_args();
    uint32_t selected_app_id = 0;
    for (int64_t index = 0; index < arguments.size(); ++index) {
        const String argument = arguments[index];
        if (!argument.begins_with(option)) {
            continue;
        }
        const String value = argument.trim_prefix(option);
        const int64_t app_id = value.to_int();
        if (selected_app_id != 0 || value.length() > 10 || !value.is_valid_int() || value.begins_with("+") ||
                app_id < 1 || app_id > (std::numeric_limits<uint32_t>::max)()) {
            UtilityFunctions::printerr("Crimson Steam overlay startup requires one positive AppID.");
            return;
        }
        selected_app_id = static_cast<uint32_t>(app_id);
    }
    if (selected_app_id == 0) {
        return;
    }

    String error;
    overlay_runtime = acquire(selected_app_id, error);
    if (overlay_runtime) {
        UtilityFunctions::print("CRIMSON_STEAM_OVERLAY_BOOTSTRAP_READY");
    } else {
        UtilityFunctions::printerr("Crimson Steam overlay startup failed. ", error);
    }
}

void SteamRuntime::release_overlay_at_shutdown() {
    overlay_runtime.reset();
}

SteamRuntime::~SteamRuntime() {
    calls.clear();
    subscriptions.clear();
    html_browsers.clear();
    shutdown_client();
    if (library) {
        FreeLibrary(static_cast<HMODULE>(library));
    }
}

void SteamRuntime::shutdown_client() {
    if (html_surface) {
        html_surface->Shutdown();
        html_surface = nullptr;
    }
    if (initialized) {
        symbol<decltype(&SteamAPI_Shutdown)>("SteamAPI_Shutdown")();
        initialized = false;
    }
    pipe = 0;
    sockets = nullptr;
    matchmaking = nullptr;
    friends = nullptr;
    utils = nullptr;
    overlay_active = false;
}

void SteamRuntime::track_client(uint64_t id, const std::shared_ptr<SteamClientScope> &scope, std::function<void()> on_loss) {
    clients.insert_or_assign(id, ClientLease{scope, std::move(on_loss)});
}

void SteamRuntime::forget_client(uint64_t id) {
    clients.erase(id);
}

void SteamRuntime::fail_ipc(const IPCFailure_t &failure) {
    ipc_failed = true;
    auto lost_clients = std::exchange(clients, {});
    for (const auto &[id, client] : lost_clients) {
        if (const auto scope = client.scope.lock()) {
            scope->active = false;
        }
    }
    // Game observers retire their native routes and tickets while their old pipe still exists.
    dispatch(IPCFailure_t::k_iCallback, &failure, sizeof(failure));
    for (const auto &[id, client] : lost_clients) {
        client.on_loss();
    }
    auto abandoned = std::exchange(calls, {});
    for (const auto &[handle, pending] : abandoned) {
        pending.handler(nullptr);
    }
    SteamLobbyLease::forget_runtime(*this);
    subscriptions.clear();
    html_browsers.clear();
    shutdown_client();
    if (overlay_runtime.get() == this) {
        overlay_runtime.reset();
    }
}

ISteamHTMLSurface *SteamRuntime::start_html_surface() {
    if (!html_surface && on_main_thread()) {
        const auto get = symbol<decltype(&SteamAPI_SteamHTMLSurface_v005)>("SteamAPI_SteamHTMLSurface_v005");
        auto *interface = get ? get() : nullptr;
        if (interface && interface->Init()) {
            html_surface = interface;
        }
    }
    return html_surface;
}

ISteamHTMLSurface *SteamRuntime::get_html_surface() const { return html_surface; }

bool SteamRuntime::reserve_html_browser() {
    if (html_reservations >= maximum_html_browsers) {
        return false;
    }
    ++html_reservations;
    return true;
}

void SteamRuntime::discard_html_browser(HHTMLBrowser handle) {
    if (html_surface && handle != INVALID_HTMLBROWSER && !owns_html_browser(handle)) {
        html_surface->RemoveBrowser(handle);
    }
    if (html_reservations > 0) {
        --html_reservations;
    }
}

bool SteamRuntime::register_html_browser(HHTMLBrowser handle, HtmlHandler handler) {
    return handle != INVALID_HTMLBROWSER && html_browsers.emplace(handle, HtmlBrowser{std::move(handler)}).second;
}

void SteamRuntime::close_html_browser(HHTMLBrowser handle) {
    if (html_browsers.erase(handle) != 0) {
        discard_html_browser(handle);
    }
}

bool SteamRuntime::owns_html_browser(HHTMLBrowser handle) const {
    return html_browsers.find(handle) != html_browsers.end();
}

bool SteamRuntime::claim_html_paint(HHTMLBrowser handle) {
    const auto browser = html_browsers.find(handle);
    if (browser == html_browsers.end() || browser->second.paints >= maximum_html_paints_per_browser) {
        return false;
    }
    ++browser->second.paints;
    return true;
}

bool SteamRuntime::initialize(uint32_t expected_app_id, String &error) {
    const std::wstring filename = packaged_library_path();
    if (!filename.empty()) {
        library = LoadLibraryExW(filename.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    if (!library) {
        error = "Install the official Steam runtime beside the Crimson DLL.";
        return false;
    }
    const auto init = symbol<decltype(&SteamAPI_InitFlat)>("SteamAPI_InitFlat");
    const auto get_pipe = symbol<decltype(&SteamAPI_GetHSteamPipe)>("SteamAPI_GetHSteamPipe");
    const auto manual_init = symbol<decltype(&SteamAPI_ManualDispatch_Init)>("SteamAPI_ManualDispatch_Init");
    const auto user_api = symbol<decltype(&SteamAPI_SteamUser_v023)>("SteamAPI_SteamUser_v023");
    const auto utils_api = symbol<decltype(&SteamAPI_SteamUtils_v011)>("SteamAPI_SteamUtils_v011");
    const auto friends_api = symbol<decltype(&SteamAPI_SteamFriends_v018)>("SteamAPI_SteamFriends_v018");
    const auto matchmaking_api = symbol<decltype(&SteamAPI_SteamMatchmaking_v009)>("SteamAPI_SteamMatchmaking_v009");
    const auto sockets_api = symbol<decltype(&SteamAPI_SteamNetworkingSockets_SteamAPI_v013)>(
            "SteamAPI_SteamNetworkingSockets_SteamAPI_v013");
    const auto get_user = symbol<decltype(&SteamAPI_ISteamUser_GetSteamID)>("SteamAPI_ISteamUser_GetSteamID");
    const auto get_app = symbol<decltype(&SteamAPI_ISteamUtils_GetAppID)>("SteamAPI_ISteamUtils_GetAppID");
    if (!init || !get_pipe || !manual_init || !user_api || !utils_api || !get_user || !get_app ||
            !find_symbol("SteamAPI_Shutdown") ||
            !find_symbol("SteamAPI_ManualDispatch_RunFrame") || !find_symbol("SteamAPI_ManualDispatch_GetNextCallback") ||
            !find_symbol("SteamAPI_ManualDispatch_FreeLastCallback") || !find_symbol("SteamAPI_ManualDispatch_GetAPICallResult")) {
        error = "The installed Steam runtime does not expose the required Steam client and callback APIs.";
        return false;
    }
    SteamErrMsg initialization_error{};
    client_init_attempted = true;
    if (init(&initialization_error) != k_ESteamAPIInitResult_OK) {
        error = "Steam could not initialize. Open a signed-in Steam client and check the game's AppID.";
        return false;
    }
    initialized = true;
    ISteamUser *user = user_api();
    utils = utils_api();
    if (!user || !utils) {
        error = "Steam did not expose a local account or app interface.";
        return false;
    }
    user_id = get_user(user);
    app_id = get_app(utils);
    const CSteamID identity(user_id);
    if (!identity.IsValid() || identity.GetEAccountType() != k_EAccountTypeIndividual || app_id == 0 ||
            (expected_app_id != 0 && app_id != expected_app_id)) {
        error = "Steam returned an unexpected AppID or account type.";
        return false;
    }
    pipe = get_pipe();
    if (pipe == 0) {
        error = "The Steam client did not expose an active callback pipe.";
        return false;
    }
    manual_init();
    sockets = sockets_api ? sockets_api() : nullptr;
    matchmaking = matchmaking_api ? matchmaking_api() : nullptr;
    friends = friends_api ? friends_api() : nullptr;
    error = String();
    return true;
}

void *SteamRuntime::find_symbol(const char *name) const {
    return library && (!ipc_failed || initialized) ?
            reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), name)) : nullptr;
}

ISteamNetworkingSockets *SteamRuntime::get_sockets() const {
    return sockets;
}

uint64_t SteamRuntime::get_local_socket_id() const {
    return user_id;
}

uint64_t SteamRuntime::get_user_id() const {
    return user_id;
}

uint32_t SteamRuntime::get_app_id() const {
    return app_id;
}

bool SteamRuntime::is_healthy() const {
    return initialized && !ipc_failed;
}

bool SteamRuntime::is_online() const {
    if (!is_healthy() || !on_main_thread()) {
        return false;
    }
    const auto get = symbol<decltype(&SteamAPI_SteamUser_v023)>("SteamAPI_SteamUser_v023");
    const auto logged_on = symbol<decltype(&SteamAPI_ISteamUser_BLoggedOn)>("SteamAPI_ISteamUser_BLoggedOn");
    ISteamUser *user = get ? get() : nullptr;
    return user && logged_on && logged_on(user);
}

String SteamRuntime::read_launch_command_line() const {
    if (!is_healthy() || !on_main_thread()) {
        return {};
    }
    const auto get = symbol<decltype(&SteamAPI_SteamApps_v009)>("SteamAPI_SteamApps_v009");
    const auto read = symbol<decltype(&SteamAPI_ISteamApps_GetLaunchCommandLine)>("SteamAPI_ISteamApps_GetLaunchCommandLine");
    ISteamApps *apps = get ? get() : nullptr;
    if (!apps || !read) {
        return {};
    }
    std::array<char, 2048> buffer{};
    const int bytes = read(apps, buffer.data(), static_cast<int>(buffer.size()));
    if (bytes <= 0 || bytes >= static_cast<int>(buffer.size() - 1)) {
        return {};
    }
    const auto *end = static_cast<const char *>(std::memchr(buffer.data(), '\0', bytes + 1));
    if (!end || (end != buffer.data() + bytes && end != buffer.data() + bytes - 1)) {
        return {};
    }
    return String::utf8(buffer.data(), end - buffer.data());
}

ISteamMatchmaking *SteamRuntime::get_matchmaking() const {
    return matchmaking;
}

ISteamFriends *SteamRuntime::get_friends() const {
    return friends;
}

ISteamUtils *SteamRuntime::get_utils() const {
    return utils;
}

bool SteamRuntime::is_overlay_active() const {
    return overlay_active;
}

void SteamRuntime::warm_relay() {
    if (relay_warmup_started || !on_main_thread()) {
        return;
    }
    const auto networking_utils_api = symbol<decltype(&SteamAPI_SteamNetworkingUtils_SteamAPI_v004)>(
            "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    const auto init_relay = symbol<decltype(&SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess)>(
            "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    ISteamNetworkingUtils *networking_utils = networking_utils_api ? networking_utils_api() : nullptr;
    if (networking_utils && init_relay) {
        relay_warmup_started = true;
        init_relay(networking_utils);
    }
}

bool SteamRuntime::can_track_call() const {
    return is_healthy() && calls.size() < maximum_calls;
}

bool SteamRuntime::watch_call(SteamAPICall_t call, int callback_id, size_t size, CallHandler handler) {
    return call != k_uAPICallInvalid && callback_id > 0 && size != 0 && size <= maximum_callback_size &&
            handler && can_track_call() && calls.emplace(call, PendingCall{callback_id, size, std::move(handler)}).second;
}

void SteamRuntime::unwatch_call(SteamAPICall_t call) {
    calls.erase(call);
}

uint64_t SteamRuntime::subscribe(int callback_id, CallbackHandler handler) {
    // The sole Game attachment must observe IPC failure even when service observers fill the ordinary budget.
    const size_t limit = maximum_subscriptions + (callback_id == IPCFailure_t::k_iCallback ? 1 : 0);
    if (!is_healthy() || !handler || callback_id <= 0 || subscriptions.size() >= limit ||
            next_subscription == (std::numeric_limits<uint64_t>::max)()) {
        return 0;
    }
    const uint64_t id = next_subscription++;
    subscriptions.emplace(id, Subscription{callback_id, std::move(handler)});
    return id;
}

void SteamRuntime::unsubscribe(uint64_t subscription) {
    subscriptions.erase(subscription);
}

uint64_t SteamRuntime::get_callback_drain_epoch() const {
    return callback_drain_epoch;
}

void SteamRuntime::dispatch(int callback_id, const void *record, size_t size) {
    if (callback_id == GameOverlayActivated_t::k_iCallback) {
        if (!record || size != sizeof(GameOverlayActivated_t)) {
            return;
        }
        GameOverlayActivated_t event{};
        std::memcpy(&event, record, sizeof(event));
        const bool active = event.m_bActive == 1;
        if (event.m_nAppID != app_id || event.m_bActive > 1 || active == overlay_active) {
            return;
        }
        overlay_active = active;
    }

    std::vector<uint64_t> matched;
    for (const auto &observer : subscriptions) {
        if (observer.second.callback_id == callback_id) {
            matched.push_back(observer.first);
        }
    }
    for (const uint64_t id : matched) {
        const auto observer = subscriptions.find(id);
        if (observer != subscriptions.end()) {
            const CallbackHandler notify = observer->second.handler;
            notify(record, size);
        }
    }
}

void SteamRuntime::pump() {
    if (!is_healthy() || pumping || !on_main_thread()) {
        return;
    }
    const Engine *engine = Engine::get_singleton();
    if (!engine) {
        return;
    }
    const uint64_t frame = engine->get_process_frames();
    if (last_pump_frame == frame) {
        return;
    }
    last_pump_frame = frame;
    pumping = true;
    const auto run_frame = symbol<decltype(&SteamAPI_ManualDispatch_RunFrame)>("SteamAPI_ManualDispatch_RunFrame");
    const auto next = symbol<decltype(&SteamAPI_ManualDispatch_GetNextCallback)>("SteamAPI_ManualDispatch_GetNextCallback");
    const auto free_last = symbol<decltype(&SteamAPI_ManualDispatch_FreeLastCallback)>("SteamAPI_ManualDispatch_FreeLastCallback");
    const auto get_result = symbol<decltype(&SteamAPI_ManualDispatch_GetAPICallResult)>(
            "SteamAPI_ManualDispatch_GetAPICallResult");
    for (auto &browser : html_browsers) {
        browser.second.paints = 0;
    }
    run_frame(pipe);
    for (size_t index = 0; index < maximum_callbacks_per_pump; ++index) {
        CallbackMsg_t event{};
        if (!next(pipe, &event)) {
            if (callback_drain_epoch != (std::numeric_limits<uint64_t>::max)()) {
                ++callback_drain_epoch;
            }
            break;
        }
        if (event.m_iCallback == IPCFailure_t::k_iCallback && event.m_pubParam && event.m_cubParam == sizeof(IPCFailure_t)) {
            IPCFailure_t failure{};
            std::memcpy(&failure, event.m_pubParam, sizeof(failure));
            free_last(pipe);
            fail_ipc(failure);
            break;
        }
        if (event.m_iCallback == GamepadTextInputDismissed_t::k_iCallback) {
            const auto dismissal = copy_steam_modal_dismissal(*this, event.m_pubParam,
                    static_cast<size_t>(event.m_cubParam));
            free_last(pipe);
            if (dismissal.matches) {
                dispatch(GamepadTextInputDismissed_t::k_iCallback, &dismissal, sizeof(dismissal));
            }
            continue;
        }
        if (is_steam_html_callback(event.m_iCallback)) {
            const auto owned = copy_steam_html_callback(*this, event.m_iCallback, event.m_pubParam,
                    static_cast<size_t>(event.m_cubParam));
            free_last(pipe);
            const auto browser = html_browsers.find(owned.browser);
            if (browser != html_browsers.end()) {
                const bool yield_after_paint = owned.kind == HTML_NeedsPaint_t::k_iCallback &&
                        browser->second.paints >= maximum_html_paints_per_browser;
                const auto notify = browser->second.handler;
                notify(owned);
                if (yield_after_paint) {
                    break;
                }
            }
            continue;
        }
        alignas(std::max_align_t) std::array<std::byte, maximum_callback_size> storage;
        const bool is_call = event.m_iCallback == SteamAPICallCompleted_t::k_iCallback;
        const bool is_lobby_chat = event.m_iCallback == LobbyChatMsg_t::k_iCallback;
        const bool has_data = event.m_pubParam && event.m_cubParam > 0 &&
                static_cast<size_t>(event.m_cubParam) <= storage.size();
        SteamLobbyChatRecord lobby_chat;
        CallHandler handler;
        bool successful = false;
        if (is_call && has_data && event.m_cubParam == sizeof(SteamAPICallCompleted_t)) {
            SteamAPICallCompleted_t completed{};
            std::memcpy(&completed, event.m_pubParam, sizeof(completed));
            const auto found = calls.find(completed.m_hAsyncCall);
            if (found != calls.end()) {
                PendingCall pending = std::move(found->second);
                calls.erase(found);
                handler = std::move(pending.handler);
                bool io_failure = false;
                successful = completed.m_iCallback == pending.callback_id && completed.m_cubParam == pending.size &&
                        get_result(pipe, completed.m_hAsyncCall, storage.data(), static_cast<int>(pending.size),
                                pending.callback_id, &io_failure) && !io_failure;
            }
        } else if (is_lobby_chat && has_data && event.m_cubParam == sizeof(LobbyChatMsg_t) && matchmaking) {
            LobbyChatMsg_t message{};
            std::memcpy(&message, event.m_pubParam, sizeof(message));
            const CSteamID lobby(message.m_ulSteamIDLobby);
            if (lobby.IsValid() && lobby.IsLobby() && message.m_eChatEntryType == k_EChatEntryTypeChatMsg &&
                    message.m_iChatID <= (std::numeric_limits<int>::max)()) {
                CSteamID sender;
                EChatEntryType entry = k_EChatEntryTypeInvalid;
                const int count = matchmaking->GetLobbyChatEntry(lobby, static_cast<int>(message.m_iChatID),
                        &sender, lobby_chat.bytes.data(), static_cast<int>(lobby_chat.bytes.size()), &entry);
                if (entry == k_EChatEntryTypeChatMsg && sender.IsValid() &&
                        sender.GetEAccountType() == k_EAccountTypeIndividual &&
                        sender.ConvertToUint64() == message.m_ulSteamIDUser && count > 0 && count <= 1024) {
                    lobby_chat.lobby = message.m_ulSteamIDLobby;
                    lobby_chat.sender = message.m_ulSteamIDUser;
                    lobby_chat.size = count;
                }
            }
        } else if (!is_call && !is_lobby_chat && has_data) {
            std::memcpy(storage.data(), event.m_pubParam, event.m_cubParam);
        }
        free_last(pipe);
        if (handler) {
            handler(successful ? storage.data() : nullptr);
        } else if (is_lobby_chat) {
            if (lobby_chat.size > 0) {
                dispatch(event.m_iCallback, &lobby_chat, sizeof(lobby_chat));
            }
        } else if (!is_call && has_data) {
            dispatch(event.m_iCallback, storage.data(), static_cast<size_t>(event.m_cubParam));
        } else if (!is_call && event.m_iCallback > 0 && event.m_cubParam == 0) {
            dispatch(event.m_iCallback, nullptr, 0);
        }
    }
    pumping = false;
}

} // namespace crimson::steam

#endif
