// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/binder_common.hpp>

#include <cstdint>
#include <functional>
#include <memory>

#ifdef CRIMSON_WITH_STEAM
class ISteamNetworkingSockets;
#endif

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Detached metrics for one connected and authenticated Steam socket.
class SteamConnectionPath : public godot::RefCounted {
    GDCLASS(SteamConnectionPath, godot::RefCounted)

public:
    // Route currently observed by Steam Networking Sockets.
    enum RouteKind {
        UNKNOWN, // Steam does not identify the current route.
        DIRECT, // Steam reports a live direct socket address.
        RELAY, // Steam marks the connection as relayed.
    };

    // Return the route reported by the live SDK socket.
    RouteKind get_kind() const;
    // Return measured latency in milliseconds, or -1 when unknown.
    int32_t get_ping_ms() const;
    // Return one minus Steam's in-order delivery quality, or -1 when unknown.
    double get_packet_loss() const;
    // Build a detached snapshot from validated native values.
    static godot::Ref<SteamConnectionPath> snapshot(RouteKind route, int32_t ping, double loss);

protected:
    // Register read-only path properties and route kinds.
    static void _bind_methods();

private:
    // Current route as identified by Steam.
    RouteKind kind = UNKNOWN;
    // Last SDK latency measurement in milliseconds.
    int32_t ping_ms = -1;
    // Approximate local in-order delivery deficit, or -1 if unknown.
    double packet_loss = -1.0;
};

// Terminal result of reading an exact Game peer's transport path.
class SteamConnectionPathResult : public SteamResult {
    GDCLASS(SteamConnectionPathResult, SteamResult)

public:
    // Return the snapshot when the queried peer had a live path.
    godot::Ref<SteamConnectionPath> get_value() const;
    // Create a typed result for the validated path.
    static godot::Ref<SteamConnectionPathResult> create(godot::Error error,
            const godot::Ref<SteamConnectionPath> &path = {});

protected:
    // Register the typed snapshot payload.
    static void _bind_methods();

private:
    // Detached metrics published with a successful result.
    godot::Ref<SteamConnectionPath> value;
};

// Detached snapshot of this Steam client's relay availability.
class SteamRelayStatus : public godot::RefCounted {
    GDCLASS(SteamRelayStatus, godot::RefCounted)

public:
    // Steam relay readiness as reported by Steam Networking Utils.
    enum Availability {
        CANNOT_TRY = -102, // A dependency required for relay access is missing.
        FAILED = -101, // Relay access has never become available.
        PREVIOUSLY_AVAILABLE = -100, // A previously ready relay is now unavailable.
        RETRYING = -10, // Steam is retrying a previous failure.
        UNKNOWN = 0, // The scope or relay state is unknown.
        NEVER_TRIED = 1, // Steam has not attempted relay preparation.
        WAITING = 2, // Steam is waiting for a required dependency.
        ATTEMPTING = 3, // Steam is attempting to prepare relay access.
        CURRENT = 100, // Steam reports that relay access is ready.
    };

    // Return Steam's current relay availability.
    Availability get_availability() const;
    // Return whether Steam is measuring relay latency.
    bool is_measuring() const;
    // Build a typed snapshot from validated Steam status values.
    static godot::Ref<SteamRelayStatus> snapshot(int availability, bool measuring);

protected:
    // Register immutable relay properties and availability values.
    static void _bind_methods();

private:
    // Latest validated native availability.
    Availability availability = UNKNOWN;
    // Whether Steam is measuring or waiting to measure relay latency.
    bool measuring = false;
};

#ifdef CRIMSON_WITH_STEAM
// Sample only a currently connected socket matching the exact token and certified Steam identity.
godot::Ref<SteamConnectionPath> sample_steam_path(ISteamNetworkingSockets &sockets, uint32_t handle,
        int64_t token, uint64_t expected_account);
#endif

// Relay readiness and read-only path diagnostics for the owning Steam client.
class SteamNetworking : public godot::RefCounted {
    GDCLASS(SteamNetworking, godot::RefCounted)

public:
    // Revalidate one Game peer before reading the path of its native socket.
    using PeerSource = std::function<godot::Ref<SteamConnectionPath>(int32_t)>;

    // Return the current Steam relay readiness and measurement state.
    godot::Ref<SteamRelayStatus> relay_status() const;
    // Ask the process Steam runtime to start relay preparation.
    godot::Error warm_relay();
    // Sample a current admitted peer when attached to an integrated Game.
    godot::Ref<SteamConnectionPathResult> peer_path(int64_t peer_id) const;

    // Attach to the current SteamClient generation and observe native relay changes.
    void attach(const std::shared_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &scope);
    // Retire this generation's native observer and Game query hook.
    void detach();
    // Inject the Game's authority-checked query without making the module depend on Game.
    void set_game_peer_source(PeerSource source);

protected:
    // Register the diagnostics facade and relay state signal.
    static void _bind_methods();

private:
    // Resolve only a current client generation on the scene thread.
    std::shared_ptr<SteamRuntime> live() const;

    // Shared Steam process runtime, observed weakly.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact liveness of the originating SteamClient.
    std::weak_ptr<SteamClientScope> scope;
    // Native relay status subscription, or zero if unavailable.
    uint64_t subscription = 0;
    // Optional Game-owned sampler for a verified current Session peer.
    PeerSource peer_source;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamConnectionPath::RouteKind);
VARIANT_ENUM_CAST(crimson::steam::SteamRelayStatus::Availability);
