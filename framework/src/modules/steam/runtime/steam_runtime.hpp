// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/runtime/steam_socket_runtime.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/string.hpp>

#include <steam_api.h>
#include <steam_api_flat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>

namespace crimson::steam {

struct SteamHtmlCallback;
struct SteamClientScope;

class SteamTimeline;
class SteamAuth;
class SteamVideo;
class SteamVideoRequests;

// Detached native lobby chat data valid after Steam frees the callback and chat index.
struct SteamLobbyChatRecord {
    // Lobby identified by the exact Steam callback.
    uint64_t lobby = 0;
    // Steam sender matched against the native chat entry before copying.
    uint64_t sender = 0;
    // Length of the UTF-8 bytes, or zero for an invalid entry.
    int size = 0;
    // Extra byte distinguishes oversize data from an allowed 1024-byte message.
    std::array<char, 1025> bytes{};
};

// Owns one lazily loaded Steam client runtime and its scene-thread manual dispatcher.
class SteamRuntime : public SteamSocketRuntime {
public:
    // An API result is null if Steam reports an I/O or callback validation failure.
    using CallHandler = std::function<void(const void *)>;
    // Owned HTML data remains valid during this scene-thread dispatch.
    using HtmlHandler = std::function<void(const SteamHtmlCallback &)>;

    // Start the signed-in Steam client API or reuse its compatible process instance.
    static std::shared_ptr<SteamRuntime> acquire(uint32_t expected_app_id, godot::String &error);
    // Check that the caller owns Godot's main thread before accessing the Steam client API.
    static bool on_main_thread();
    // Resolve only the packaged Steam library beside the loaded Crimson extension.
    static std::wstring packaged_library_path();
    // Ask Steam to relaunch an explicitly selected shipping client before any native initialization.
    static godot::Error restart_through_steam_if_needed(uint32_t app_id);
    // Prepare the explicitly requested Steam overlay before Godot opens its display.
    static void prepare_overlay_from_launch();
    // Release the pre-render runtime reference during the matching Godot shutdown phase.
    static void release_overlay_at_shutdown();
    // Release Steam resources when the last consumer and pump node leave.
    ~SteamRuntime() override;
    // Resolve a flat C API export from the already loaded SDK.
    template <typename Function>
    Function symbol(const char *name) const {
        return reinterpret_cast<Function>(find_symbol(name));
    }
    // Return the selected SDK networking interface.
    ISteamNetworkingSockets *get_sockets() const override;
    // Return the signed-in user's network identity on this client pipe.
    uint64_t get_local_socket_id() const override;
    // Return the SDK-authenticated local individual Steam ID.
    uint64_t get_user_id() const;
    // Return the AppID selected by the initialized Steam client.
    uint32_t get_app_id() const;
    // Report whether the process client pipe can accept work from this generation.
    bool is_healthy() const;
    // Query the live backend connection independently of local SDK readiness.
    bool is_online() const;
    // Copy a bounded command line supplied by Steam for URL and Rich Presence launches.
    godot::String read_launch_command_line() const;
    // Track one client start so fatal IPC invalidates all scopes before signaling callers.
    void track_client(uint64_t id, const std::shared_ptr<SteamClientScope> &scope, std::function<void()> on_loss);
    // Remove the observer for a normally stopped client.
    void forget_client(uint64_t id);
    // Return Steam's lobby interface for the active client AppID.
    ISteamMatchmaking *get_matchmaking() const;
    // Return Steam's friends and native invite interface.
    ISteamFriends *get_friends() const;
    // Return Steam's utilities, including overlay readiness.
    ISteamUtils *get_utils() const;
    // Return the last valid overlay activation state observed for this AppID.
    bool is_overlay_active() const;
    // Lazily initialize Steam HTML for the process; shutdown occurs before SteamAPI_Shutdown.
    ISteamHTMLSurface *start_html_surface();
    // Return the already initialized HTML interface without starting it.
    ISteamHTMLSurface *get_html_surface() const;
    // Reserve one slot before starting an async browser create.
    bool reserve_html_browser();
    // Release a create reservation with no live browser, discarding any unexpected native handle.
    void discard_html_browser(HHTMLBrowser handle);
    // Transfer a successful create reservation to a live handle and callback observer.
    bool register_html_browser(HHTMLBrowser handle, HtmlHandler handler);
    // Release one exact registered handle and its reservation.
    void close_html_browser(HHTMLBrowser handle);
    // Verify ownership before copying or dispatching an SDK browser record.
    bool owns_html_browser(HHTMLBrowser handle) const;
    // Spend at most two paint copies per owned browser in one Godot process frame.
    bool claim_html_paint(HHTMLBrowser handle);
    // Warm Steam Datagram Relay when a networking consumer uses this runtime.
    void warm_relay();
    // Report whether there is capacity to start another async SDK call.
    bool can_track_call() const;
    // Route one async SDK call result to its scene-thread consumer.
    bool watch_call(SteamAPICall_t call, int callback_id, size_t size, CallHandler handler);
    // Forget a call whose consumer has already retired its native SDK resources.
    void unwatch_call(SteamAPICall_t call);
    // Subscribe to a manual callback ID, with zero returned on failure.
    uint64_t subscribe(int callback_id, CallbackHandler handler) override;
    // Remove an existing manual callback subscription.
    void unsubscribe(uint64_t subscription) override;
    // Return the latest epoch that reached an empty native callback queue.
    uint64_t get_callback_drain_epoch() const override;
    // Drain a bounded number of typed SDK events once per Godot process frame.
    void pump();

private:
    friend class SteamTimeline;
    friend class SteamAuth;
    friend class SteamVideo;

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
    // One exact browser handle registered after Steam reports readiness.
    struct HtmlBrowser {
        // Scene-thread handler receiving copied records for this handle only.
        HtmlHandler handler;
        // Number of paint copies charged to this handle in the current pump.
        uint8_t paints = 0;
    };
    // An exact client generation and its scene-thread cleanup callback.
    struct ClientLease {
        // Scope used by native facades to guard access.
        std::weak_ptr<SteamClientScope> scope;
        // Settles this client's services and reports fatal runtime loss.
        std::function<void()> on_loss;
    };

    // Loaded steam_api64.dll module handle owned by this runtime.
    void *library = nullptr;
    // Whether SteamAPI_InitFlat succeeded and requires one Shutdown.
    bool initialized = false;
    // Fatal IPC permanently retires this generation even if an old handle still retains it.
    bool ipc_failed = false;
    // Whether the optional networking interface has started relay initialization.
    bool relay_warmup_started = false;
    // Prevent nested manual dispatch from scene callbacks.
    bool pumping = false;
    // One process overlay focus state shared by all scopes retaining this runtime.
    bool overlay_active = false;
    // Exact Timeline facade that started the current game phase, or zero while idle.
    uint64_t timeline_phase_owner = 0;
    // One set of handle-less Video requests shared across all client scopes.
    std::shared_ptr<SteamVideoRequests> video_requests;
    // Native encrypted ticket request kept reserved through matching result dispatch.
    uint64_t encrypted_ticket_call = 0;
    // Process frame already pumped by any consumer of this runtime.
    uint64_t last_pump_frame = (std::numeric_limits<uint64_t>::max)();
    // Changes only when manual dispatch drains through the end of the queue.
    uint64_t callback_drain_epoch = 0;
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
    // Process HTML interface after a successful opt-in Init.
    ISteamHTMLSurface *html_surface = nullptr;
    // Shared budget for currently live browsers and in-flight browser creation.
    size_t html_reservations = 0;
    // Exact HTML handle routing retained while the browser is live.
    std::map<HHTMLBrowser, HtmlBrowser> html_browsers;
    // Next opaque observer ID, zero reserved for failure.
    uint64_t next_subscription = 1;
    // Bounded in-flight SDK call handlers.
    std::map<SteamAPICall_t, PendingCall> calls;
    // Bounded SDK event observers keyed by monotonic subscription ID.
    std::map<uint64_t, Subscription> subscriptions;
    // Current client starts keyed by their owning Godot node IDs.
    std::map<uint64_t, ClientLease> clients;

    // Initialize a fresh runtime only through acquire.
    SteamRuntime() = default;
    // Load the packaged runtime, verify the client account and establish manual dispatch.
    bool initialize(uint32_t expected_app_id, godot::String &error);
    // Shut down the native client once after its owned handles have been retired.
    void shutdown_client();
    // Invalidate this generation, settle its observers, and release the failed native pipe.
    void fail_ipc(const IPCFailure_t &failure);
    // Read one exact symbol without searching another module.
    void *find_symbol(const char *name) const;
    // Dispatch a copied non-call event to live observers.
    void dispatch(int callback_id, const void *record, size_t size);
};

} // namespace crimson::steam

#endif
