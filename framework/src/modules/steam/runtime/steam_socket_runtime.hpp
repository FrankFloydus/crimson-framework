// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include <cstddef>
#include <cstdint>
#include <functional>

class ISteamNetworkingSockets;

namespace crimson::steam {

// Native client or GameServer networking interface and its matching callback pipe.
class SteamSocketRuntime {
public:
    // A detached native event valid for the duration of this scene-thread observer.
    using CallbackHandler = std::function<void(const void *, size_t)>;

    // Release the implementation only after its sockets and observers have closed.
    virtual ~SteamSocketRuntime() = default;
    // Return the Steam Networking Sockets interface for this exact pipe.
    virtual ISteamNetworkingSockets *get_sockets() const = 0;
    // Return the local Steam network identity once available, or zero while logging in.
    virtual uint64_t get_local_socket_id() const = 0;
    // Observe one native event type on this runtime, returning zero when unavailable.
    virtual uint64_t subscribe(int callback_id, CallbackHandler handler) = 0;
    // Remove one exact native event subscription.
    virtual void unsubscribe(uint64_t subscription) = 0;
    // Count complete native callback drains on this exact SDK pipe.
    virtual uint64_t get_callback_drain_epoch() const = 0;
};

} // namespace crimson::steam

#endif
