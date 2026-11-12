// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_runtime.hpp"

#ifdef CRIMSON_WITH_STEAM

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

namespace crimson::integration {
namespace {

// Maximum SDK event size copied before returning the manual dispatch buffer.
constexpr size_t maximum_callback_size = 4096;
// Limit work and retainers owned by a single client runtime.
constexpr size_t maximum_callbacks_per_pump = 128;
constexpr size_t maximum_calls = 32;
constexpr size_t maximum_subscriptions = 64;
// The client API is process-wide. The current scene's consumers own it jointly.
std::weak_ptr<SteamRuntime> client_runtime;
// A graphical opt-in retains the same Steam runtime across game scope changes.
std::shared_ptr<SteamRuntime> overlay_runtime;
// Identifies the GDExtension module without consulting the process working directory.
const char framework_module_anchor = 0;

// Return only the packaged DLL beside the currently loaded Crimson binary.
std::wstring steam_library_path() {
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

// Verify that the acquisition takes place on the Godot scene thread.
bool on_scene_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

} // namespace

std::shared_ptr<SteamRuntime> SteamRuntime::acquire(uint32_t expected_app_id, String &error) {
    if (!on_scene_thread()) {
        error = "Steam can only initialize on the scene thread.";
        return {};
    }
    if (auto active = client_runtime.lock()) {
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
    if (initialized) {
        symbol<decltype(&SteamAPI_Shutdown)>("SteamAPI_Shutdown")();
    }
    if (library) {
        FreeLibrary(static_cast<HMODULE>(library));
    }
}

bool SteamRuntime::initialize(uint32_t expected_app_id, String &error) {
    const std::wstring filename = steam_library_path();
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
    const auto networking_utils_api = symbol<decltype(&SteamAPI_SteamNetworkingUtils_SteamAPI_v004)>(
            "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    const auto init_relay = symbol<decltype(&SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess)>(
            "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    const auto get_user = symbol<decltype(&SteamAPI_ISteamUser_GetSteamID)>("SteamAPI_ISteamUser_GetSteamID");
    const auto logged_on = symbol<decltype(&SteamAPI_ISteamUser_BLoggedOn)>("SteamAPI_ISteamUser_BLoggedOn");
    const auto get_app = symbol<decltype(&SteamAPI_ISteamUtils_GetAppID)>("SteamAPI_ISteamUtils_GetAppID");
    if (!init || !get_pipe || !manual_init || !user_api || !utils_api || !friends_api || !matchmaking_api ||
            !sockets_api || !networking_utils_api || !init_relay || !get_user || !logged_on || !get_app ||
            !find_symbol("SteamAPI_Shutdown") ||
            !find_symbol("SteamAPI_ManualDispatch_RunFrame") || !find_symbol("SteamAPI_ManualDispatch_GetNextCallback") ||
            !find_symbol("SteamAPI_ManualDispatch_FreeLastCallback") || !find_symbol("SteamAPI_ManualDispatch_GetAPICallResult")) {
        error = "The installed Steam runtime does not expose the SDK interfaces required by Crimson.";
        return false;
    }
    SteamErrMsg initialization_error{};
    if (init(&initialization_error) != k_ESteamAPIInitResult_OK) {
        error = "Steam could not initialize. Open a signed-in Steam client and check the game's AppID.";
        return false;
    }
    initialized = true;
    ISteamUser *user = user_api();
    utils = utils_api();
    if (!user || !utils || !logged_on(user)) {
        error = "Steam needs a signed-in client account for online play.";
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
    sockets = sockets_api();
    matchmaking = matchmaking_api();
    friends = friends_api();
    ISteamNetworkingUtils *networking_utils = networking_utils_api();
    if (pipe == 0 || !sockets || !matchmaking || !friends || !networking_utils) {
        error = "The Steam client did not expose an active networking and lobby pipe.";
        return false;
    }
    manual_init();
    init_relay(networking_utils);
    error = String();
    return true;
}

void *SteamRuntime::find_symbol(const char *name) const {
    return library ? reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), name)) : nullptr;
}

ISteamNetworkingSockets *SteamRuntime::get_sockets() const {
    return sockets;
}

uint64_t SteamRuntime::get_user_id() const {
    return user_id;
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

bool SteamRuntime::claim_game() {
    if (game_claimed) {
        return false;
    }
    game_claimed = true;
    return true;
}

void SteamRuntime::release_game() {
    host_lobby = 0;
    game_claimed = false;
}

void SteamRuntime::set_host_lobby(uint64_t lobby) {
    host_lobby = lobby;
}

void SteamRuntime::clear_host_lobby(uint64_t lobby) {
    if (host_lobby == lobby) {
        host_lobby = 0;
    }
}

bool SteamRuntime::is_host_lobby_member(uint64_t account) const {
    const auto count = symbol<decltype(&SteamAPI_ISteamMatchmaking_GetNumLobbyMembers)>(
            "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    const auto member = symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    if (host_lobby == 0 || account == 0 || !count || !member) {
        return false;
    }
    const int members = count(matchmaking, host_lobby);
    if (members < 1 || members > 65) {
        return false;
    }
    for (int index = 0; index < members; ++index) {
        if (member(matchmaking, host_lobby, index) == account) {
            return true;
        }
    }
    return false;
}

bool SteamRuntime::can_track_call() const {
    return calls.size() < maximum_calls;
}

bool SteamRuntime::watch_call(SteamAPICall_t call, int callback_id, size_t size, CallHandler handler) {
    return call != k_uAPICallInvalid && callback_id > 0 && size != 0 && size <= maximum_callback_size &&
            handler && can_track_call() && calls.emplace(call, PendingCall{callback_id, size, std::move(handler)}).second;
}

uint64_t SteamRuntime::subscribe(int callback_id, CallbackHandler handler) {
    if (!handler || callback_id <= 0 || subscriptions.size() >= maximum_subscriptions ||
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

void SteamRuntime::dispatch(int callback_id, const void *record, size_t size) {
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
    if (pumping) {
        return;
    }
    pumping = true;
    const auto run_frame = symbol<decltype(&SteamAPI_ManualDispatch_RunFrame)>("SteamAPI_ManualDispatch_RunFrame");
    const auto next = symbol<decltype(&SteamAPI_ManualDispatch_GetNextCallback)>("SteamAPI_ManualDispatch_GetNextCallback");
    const auto free_last = symbol<decltype(&SteamAPI_ManualDispatch_FreeLastCallback)>("SteamAPI_ManualDispatch_FreeLastCallback");
    const auto get_result = symbol<decltype(&SteamAPI_ManualDispatch_GetAPICallResult)>(
            "SteamAPI_ManualDispatch_GetAPICallResult");
    run_frame(pipe);
    for (size_t index = 0; index < maximum_callbacks_per_pump; ++index) {
        CallbackMsg_t event{};
        if (!next(pipe, &event)) {
            break;
        }
        alignas(std::max_align_t) std::array<std::byte, maximum_callback_size> storage;
        const bool is_call = event.m_iCallback == SteamAPICallCompleted_t::k_iCallback;
        const bool has_data = event.m_pubParam && event.m_cubParam > 0 &&
                static_cast<size_t>(event.m_cubParam) <= storage.size();
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
        } else if (!is_call && has_data) {
            std::memcpy(storage.data(), event.m_pubParam, event.m_cubParam);
        }
        free_last(pipe);
        if (handler) {
            handler(successful ? storage.data() : nullptr);
        } else if (!is_call && has_data) {
            dispatch(event.m_iCallback, storage.data(), static_cast<size_t>(event.m_cubParam));
        }
    }
    pumping = false;
}

} // namespace crimson::integration

#endif
