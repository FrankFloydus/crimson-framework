// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/session/session_transport.hpp"

#include <cstdint>

namespace crimson::steam {

class SteamRuntime;
class SteamServerRuntime;

// Certified hosted server locator containing no ticket or physical routing data.
struct SteamHostedRoute {
    // Expected Steam GameServer certificate identity.
    uint64_t server_id = 0;
    // Hosted Steam virtual port requested by the game.
    uint16_t virtual_port = 0;
    // Exact publication generation signed by the game coordinator.
    uint64_t registration = 0;
    // Configured coordinator deployment profile, absent on a Session endpoint.
    godot::StringName profile;

    // Check the bound identity and nonzero publication generation.
    bool is_valid() const;
    // Encode a versioned endpoint without including backend or ticket data.
    godot::Ref<session::SessionEndpoint> endpoint() const;
    // Encode a hosted listing scoped to the selected app and game.
    godot::String listing_id(uint32_t app_id, const godot::StringName &name_space) const;
    // Decode a hosted endpoint before looking up its exact Steam cache entry.
    static SteamHostedRoute from_endpoint(const godot::Ref<session::SessionEndpoint> &endpoint);
    // Decode a hosted listing in the exact app and game namespace.
    static SteamHostedRoute from_listing(const godot::String &id, uint32_t app_id,
            const godot::StringName &name_space);
};

// Validate an operator relay profile before passing it to the game coordinator.
bool valid_steam_relay_profile(const godot::StringName &name);

// Obtain the Valve-signed server login blob for the trusted coordinator only.
godot::PackedByteArray steam_hosted_server_login(SteamServerRuntime &runtime, const godot::String &listing,
        const godot::StringName &world);
// Install the backend's signed relay ticket only for the exact local client and selected registration.
bool install_steam_relay_ticket(SteamRuntime &runtime, const SteamHostedRoute &route, const godot::PackedByteArray &ticket);
// Verify Steam's chosen cached ticket is still valid for the same local user and registration.
bool has_steam_relay_ticket(SteamRuntime &runtime, const SteamHostedRoute &route);

} // namespace crimson::steam
