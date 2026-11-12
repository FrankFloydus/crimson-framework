// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/session/session_transport.hpp"

#include <cstdint>

namespace crimson::steam {

// Protocol tag identifying a compatible certificate-pinned direct Game server.
inline constexpr const char *STEAM_DIRECT_SERVER_PROTOCOL = "crimson-steam-direct-v1";
// Build the stable Steam browser tag for a validated project namespace.
godot::String steam_server_namespace_tag(const godot::StringName &name_space);
// Build a bounded Steam browser tag for a world without truncating its identity.
godot::String steam_server_world_tag(const godot::StringName &world);

// A versioned locator for a certified Steam dedicated server over direct IPv4.
struct SteamServerRoute {
    // Expected certificate-backed GameServer or anonymous GameServer account.
    uint64_t server_id = 0;
    // Unicast IPv4 in host byte order.
    uint32_t ipv4 = 0;
    // Direct gameplay UDP port.
    uint16_t port = 0;

    // Check the Steam account type, IPv4 address and gameplay port.
    bool is_valid() const;
    // Serialize only the direct route as a typed Session endpoint.
    godot::Ref<session::SessionEndpoint> endpoint() const;
    // Build the provider ID scoped to the app and project namespace.
    godot::String listing_id(uint32_t app_id, const godot::StringName &name_space) const;
    // Decode an exact Session adapter route without accepting any other transport.
    static SteamServerRoute from_endpoint(const godot::Ref<session::SessionEndpoint> &endpoint);
    // Resolve a typed or explicitly supplied provider ID for this app and namespace.
    static SteamServerRoute from_listing(const godot::String &listing_id, uint32_t expected_app,
            const godot::StringName &expected_namespace);
};

} // namespace crimson::steam
