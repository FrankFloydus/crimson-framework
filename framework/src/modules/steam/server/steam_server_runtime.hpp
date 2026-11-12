// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/runtime/steam_socket_runtime.hpp"
#include "modules/steam/server/steam_server.hpp"

#include <steam_api.h>
#include <steam_gameserver.h>

#include <limits>
#include <map>
#include <memory>

namespace crimson::steam {

// Owns the dedicated SteamGameServer API, independent of any signed-in client scope.
class SteamServerRuntime final : public SteamSocketRuntime {
public:
    // Open the process's sole dedicated Steam API pipe and start authenticated logon.
    static std::shared_ptr<SteamServerRuntime> acquire(const godot::Ref<SteamServer> &settings, uint32_t expected_app,
            const godot::StringName &name_space, godot::String &error);
    // Stop listing and log off the native server before releasing the packaged SDK.
    ~SteamServerRuntime() override;
    // Return the network interface for the dedicated server pipe.
    ISteamNetworkingSockets *get_sockets() const override;
    // Return the current logged-in dedicated network identity.
    uint64_t get_local_socket_id() const override;
    // Observe a copied callback from the dedicated server pipe.
    uint64_t subscribe(int callback_id, CallbackHandler handler) override;
    // Remove an exact dedicated server observer.
    void unsubscribe(uint64_t subscription) override;
    // Return the latest dedicated epoch that reached an empty native callback queue.
    uint64_t get_callback_drain_epoch() const override;
    // Drain bounded native callbacks once per Godot process frame.
    void pump();
    // Report room to observe one exact dedicated Steam call result.
    bool can_track_call() const;
    // Observe a result copied before the SDK releases the callback buffer. Null reports an invalid result.
    bool track_call(SteamAPICall_t call, int callback_id, size_t size, std::function<void(const void *)> handler);
    // Stop observing this call without cancelling the submitted native operation.
    void forget_call(SteamAPICall_t call);

    // Return the dedicated SDK identity and certificate readiness.
    bool is_ready() const;
    // Return the AppID validated against the dedicated Steam pipe.
    uint32_t get_app_id() const;
    // Return the Steam API used for exact native ticket sessions.
    ISteamGameServer *get_server() const;
    // Resolve the optional progression interface on the dedicated pipe.
    ISteamGameServerStats *get_stats() const;
    // Return the Steam-reported public IPv4 in host order, or zero.
    uint32_t get_public_ipv4() const;
    // Return whether explicit Steam discovery has been requested for the active world.
    bool is_listed() const;
    // Update compatible browser metadata and submit the explicit discovery setting.
    bool publish(const godot::StringName &world, int32_t capacity);
    // Withdraw advertisement for the current game before stopping its listener.
    void unpublish();

private:
    friend class SteamInventoryVerifier;

    // One registered observer of a native dedicated callback.
    struct Subscription {
        // Exact Steam callback ID expected by this observer.
        int id = 0;
        // Called only after returning the SDK memory.
        CallbackHandler callback;
    };

    // Expected shape and observer for one native dedicated API result.
    struct PendingCall {
        // Exact Steam result callback ID.
        int callback_id = 0;
        // Expected size in bytes before reading the SDK result.
        size_t size = 0;
        // Consumed after Steam releases the callback buffer.
        std::function<void(const void *)> handler;
    };

    // Packaged Steam library reference separate from client initialization.
    void *library = nullptr;
    // Steam shutdown is required only after a successful GameServer initialization.
    bool initialized = false;
    // Only explicit Internet policy may activate the SDK's joint advertisement switch.
    bool listed = false;
    // Prevent nested or duplicate manual dispatch.
    bool pumping = false;
    // Last scene process frame delivered by this pipe.
    uint64_t last_frame = (std::numeric_limits<uint64_t>::max)();
    // Counts complete native callback drains, including while no Game is running.
    uint64_t callback_drain_epoch = 0;
    // Dedicated native pipe. Client API callbacks use a different pipe.
    HSteamPipe pipe = 0;
    // AppID returned by the initialized dedicated Steam pipe.
    uint32_t app_id = 0;
    // Dedicated game server configuration detached from user scripts.
    godot::Ref<SteamServer> settings;
    // Namespace advertised with the dedicated gameplay protocol.
    godot::StringName name_space;
    // Dedicated SteamGameServer interface.
    ISteamGameServer *server = nullptr;
    // Dedicated networking interface for certified IP transport.
    ISteamNetworkingSockets *sockets = nullptr;
    // Bounded native subscriptions keyed by unique lifetime ID.
    std::map<uint64_t, Subscription> observers;
    // Next available observer identifier, zero is reserved for failure.
    uint64_t next_observer = 1;
    // Bounded native results keyed by this pipe's exact call handle.
    std::map<SteamAPICall_t, PendingCall> calls;

    // Initialize one fresh native runtime only through acquire.
    SteamServerRuntime() = default;
    // Resolve only exports from the packaged Steam library this runtime owns.
    template <typename Function>
    Function symbol(const char *name) const {
        return reinterpret_cast<Function>(find_symbol(name));
    }
    // Return one symbol from the exact loaded SDK module.
    void *find_symbol(const char *name) const;
    // Validate and initialize the dedicated SDK, then start logon without an interactive client.
    bool initialize(uint32_t expected_app, godot::String &error);
    // Dispatch one copied native event without retaining SDK callback storage.
    void dispatch(int callback_id, const void *data, size_t size);
};

} // namespace crimson::steam

#endif
