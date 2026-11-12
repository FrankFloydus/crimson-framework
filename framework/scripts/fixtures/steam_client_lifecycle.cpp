// Copyright Francesco Di Blasi 2026 - All Rights Reserved

// Compile only as a temporary steam_api64.dll for the isolated runtime-mock fixture.
#define STEAM_API_NODLL
#include <steam_api.h>
#include <steam_api_flat.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>

namespace {

// Identity used exclusively by the no-network fixture.
constexpr uint64_t test_account = 76561197960265729ULL;
// Storage for opaque flat-interface markers. The fixture never uses their vtables.
int interface_marker = 0;
// Track whether native client initialization has an outstanding shutdown.
bool initialized = false;
// Keep callback records valid through the matching manual FreeLastCallback call.
IPCFailure_t failure{IPCFailure_t::k_EFailurePipeFail};
NewUrlLaunchParameters_t query;

// Read a flag set by the isolated GDScript process.
bool enabled(const char *name) {
    char flag[2]{};
    return GetEnvironmentVariableA(name, flag, sizeof(flag)) == 1 && flag[0] == '1';
}

} // namespace

// Begin one local client generation without making any network requests.
ESteamAPIInitResult S_CALLTYPE SteamAPI_InitFlat(SteamErrMsg *) {
    if (initialized) {
        return k_ESteamAPIInitResult_FailedGeneric;
    }
    initialized = true;
    return k_ESteamAPIInitResult_OK;
}

// Record the old native generation's shutdown for the script assertion.
void S_CALLTYPE SteamAPI_Shutdown() {
    initialized = false;
    SetEnvironmentVariableA("CRIMSON_STEAM_TEST_SHUTDOWN", "1");
}

// Expose a local callback pipe for the current generation.
HSteamPipe S_CALLTYPE SteamAPI_GetHSteamPipe() { return initialized ? 1 : 0; }
// Accept initialization of manual dispatch.
void S_CALLTYPE SteamAPI_ManualDispatch_Init() {}
// Leave the script in control of which event arrives in a frame.
void S_CALLTYPE SteamAPI_ManualDispatch_RunFrame(HSteamPipe) {}

// Emit a single native event when the script arms the selected branch.
bool S_CALLTYPE SteamAPI_ManualDispatch_GetNextCallback(HSteamPipe, CallbackMsg_t *event) {
    if (enabled("CRIMSON_STEAM_TEST_FAIL_IPC")) {
        SetEnvironmentVariableA("CRIMSON_STEAM_TEST_FAIL_IPC", nullptr);
        event->m_iCallback = IPCFailure_t::k_iCallback;
        event->m_pubParam = reinterpret_cast<uint8 *>(&failure);
        event->m_cubParam = sizeof(failure);
        return true;
    }
    if (enabled("CRIMSON_STEAM_TEST_QUERY_EVENT")) {
        SetEnvironmentVariableA("CRIMSON_STEAM_TEST_QUERY_EVENT", nullptr);
        event->m_iCallback = NewUrlLaunchParameters_t::k_iCallback;
        event->m_pubParam = reinterpret_cast<uint8 *>(&query);
        event->m_cubParam = sizeof(query);
        return true;
    }
    return false;
}

// Record return of the borrowed SDK buffer before failure observers run.
void S_CALLTYPE SteamAPI_ManualDispatch_FreeLastCallback(HSteamPipe) {
    SetEnvironmentVariableA("CRIMSON_STEAM_TEST_BUFFER_FREED", "1");
}

// No asynchronous Steam calls can complete in this fixture.
bool S_CALLTYPE SteamAPI_ManualDispatch_GetAPICallResult(HSteamPipe, SteamAPICall_t, void *, int, int, bool *) {
    return false;
}

// Return the flat API marker for the local user interface.
ISteamUser *SteamAPI_SteamUser_v023() { return reinterpret_cast<ISteamUser *>(&interface_marker); }
// Return the flat API marker for the utilities interface.
ISteamUtils *SteamAPI_SteamUtils_v011() { return reinterpret_cast<ISteamUtils *>(&interface_marker); }
// Return the flat API marker for the Apps interface.
ISteamApps *SteamAPI_SteamApps_v009() { return reinterpret_cast<ISteamApps *>(&interface_marker); }
// Return the flat API marker for the social interface.
ISteamFriends *SteamAPI_SteamFriends_v018() { return reinterpret_cast<ISteamFriends *>(&interface_marker); }
// Return the flat API marker for the local stats interface.
ISteamUserStats *SteamAPI_SteamUserStats_v013() { return reinterpret_cast<ISteamUserStats *>(&interface_marker); }
// Supply the exact offline account through the native flat query.
uint64_steamid SteamAPI_ISteamUser_GetSteamID(ISteamUser *) { return test_account; }
// Simulate an independent live backend connection on demand.
bool SteamAPI_ISteamUser_BLoggedOn(ISteamUser *) { return enabled("CRIMSON_STEAM_TEST_ONLINE"); }
// Select the development AppID without contacting Steam.
uint32 SteamAPI_ISteamUtils_GetAppID(ISteamUtils *) { return 480; }

// Provide one cached counter even while the native online flag is false.
bool SteamAPI_ISteamUserStats_GetStatInt32(ISteamUserStats *, const char *name, int32 *value) {
    if (std::strcmp(name, "NumGames") != 0) {
        return false;
    }
    *value = 7;
    return true;
}

// Supply either an exact uint64 session ID or an intentionally invalid query value.
const char *SteamAPI_ISteamApps_GetLaunchQueryParam(ISteamApps *, const char *) {
    return enabled("CRIMSON_STEAM_TEST_INVALID_SESSION") ? "0002" : "18446744073709551615";
}
