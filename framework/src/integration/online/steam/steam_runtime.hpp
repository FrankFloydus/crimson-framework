// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include <godot_cpp/variant/string.hpp>

#include <steam_api.h>
#include <steam_api_flat.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace crimson::integration {

// Owns one lazily loaded Steam client runtime and its scene-thread manual dispatcher.
class SteamRuntime {
public:
    // An API result is null if Steam reports an I/O or callback validation failure.
    using CallHandler = std::function<void(const void *)>;
    // A copied callback record is valid only for the duration of the call.
    using CallbackHandler = std::function<void(const void *, size_t)>;

    // Start the signed-in Steam client API or reuse its compatible process instance.
    static std::shared_ptr<SteamRuntime> acquire(uint32_t expected_app_id, godot::String &error);
    // Prepare the explicitly requested Steam overlay before Godot opens its display.
    static void prepare_overlay_from_launch();
    // Release the pre-render runtime reference during the matching Godot shutdown phase.
    static void release_overlay_at_shutdown();
    // Release Steam resources when the last consumer and pump node leave.
    ~SteamRuntime();
    // Resolve a flat C API export from the already loaded SDK.
    template <typename Function>
    Function symbol(const char *name) const {
        return reinterpret_cast<Function>(find_symbol(name));
    }
    // Return the selected SDK networking interface.
    ISteamNetworkingSockets *get_sockets() const;
    // Return the SDK-authenticated local individual Steam ID.
    uint64_t get_user_id() const;
    // Return Steam's lobby interface for the active client AppID.
    ISteamMatchmaking *get_matchmaking() const;
    // Return Steam's friends and native invite interface.
    ISteamFriends *get_friends() const;
    // Return Steam's utilities, including overlay readiness.
    ISteamUtils *get_utils() const;
    // Reserve the shared local account for one game scope.
    bool claim_game();
    // Release the game reservation when its persistent services are destroyed.
    void release_game();
    // Record the current private lobby once its host publication succeeds.
    void set_host_lobby(uint64_t lobby);
    // Clear only the matching host publication so late cleanup cannot affect a newer one.
    void clear_host_lobby(uint64_t lobby);
    // Require a verified transport identity to be a member of the current hosted lobby.
    bool is_host_lobby_member(uint64_t account) const;
    // Report whether there is capacity to start another async SDK call.
    bool can_track_call() const;
    // Route one async SDK call result to its scene-thread consumer.
    bool watch_call(SteamAPICall_t call, int callback_id, size_t size, CallHandler handler);
    // Subscribe to a manual callback ID, with zero returned on failure.
    uint64_t subscribe(int callback_id, CallbackHandler handler);
    // Remove an existing manual callback subscription.
    void unsubscribe(uint64_t subscription);
    // Drain a bounded number of typed SDK events on the scene thread.
    void pump();

private:
    // A pending SDK result and its exact expected payload shape.
    struct PendingCall {
        // SDK callback ID expected for this call handle.
        int callback_id = 0;
        // Exact number of bytes in the SDK result struct.
        size_t size = 0;
        // One terminal observer for the call.
        CallHandler handler;
    };
    // One observed event type and its scene-thread callback.
    struct Subscription {
        // SDK callback ID accepted by this observer.
        int callback_id = 0;
        // Callback invoked after returning the SDK buffer.
        CallbackHandler handler;
    };

    // Loaded steam_api64.dll module handle owned by this runtime.
    void *library = nullptr;
    // Whether SteamAPI_InitFlat succeeded and requires one Shutdown.
    bool initialized = false;
    // Prevent nested manual dispatch from scene callbacks.
    bool pumping = false;
    // Steam lobby membership belongs to one selected game for this local account.
    bool game_claimed = false;
    // SDK lobby supplying the current authority's admission policy.
    uint64_t host_lobby = 0;
    // AppID supplied by the actual Steam runtime.
    uint32_t app_id = 0;
    // Local signed-in Steam user captured after initialization.
    uint64_t user_id = 0;
    // Pipe from which manual dispatch receives client events.
    HSteamPipe pipe = 0;
    // Resolved Steam client networking interface.
    ISteamNetworkingSockets *sockets = nullptr;
    // Resolved Steam client lobby interface.
    ISteamMatchmaking *matchmaking = nullptr;
    // Resolved Steam client social interface.
    ISteamFriends *friends = nullptr;
    // Resolved Steam client utility interface.
    ISteamUtils *utils = nullptr;
    // Next opaque observer ID, zero reserved for failure.
    uint64_t next_subscription = 1;
    // Bounded in-flight SDK call handlers.
    std::map<SteamAPICall_t, PendingCall> calls;
    // Bounded SDK event observers keyed by monotonic subscription ID.
    std::map<uint64_t, Subscription> subscriptions;

    // Initialize a fresh runtime only through acquire.
    SteamRuntime() = default;
    // Load the packaged runtime, verify the client account and establish manual dispatch.
    bool initialize(uint32_t expected_app_id, godot::String &error);
    // Read one exact symbol without searching another module.
    void *find_symbol(const char *name) const;
    // Dispatch a copied non-call event to live observers.
    void dispatch(int callback_id, const void *record, size_t size);
};

} // namespace crimson::integration

#endif
