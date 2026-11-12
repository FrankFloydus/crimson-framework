// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/server/steam_sdr.hpp"

namespace crimson::integration {

class SteamRelayCoordinator;

// One exact hosted Steam registration or client ticket request for the trusted backend.
class SteamRelayRequest : public godot::RefCounted {
    GDCLASS(SteamRelayRequest, godot::RefCounted)

public:
    // Identify the native hosted relay step the coordinator must complete.
    enum Kind {
        UNCONFIGURED, // No native Game owns this request.
        REGISTER, // Authenticate the signed server login and register the exact hosted locator.
        AUTHORIZE, // Authenticate this client and sign a relay ticket for the selected locator.
    };

    // Return the exact purpose of this request.
    Kind get_kind() const;
    // Return the configured backend deployment profile.
    godot::StringName get_profile() const;
    // Return the exact hosted listing containing the publication generation.
    godot::String get_listing_id() const;
    // Return the Steam AppID scoped by the native Game.
    int64_t get_app_id() const;
    // Return the configured Game namespace.
    godot::StringName get_namespace() const;
    // Return the expected dedicated server's certified SteamID64.
    godot::String get_server_steam_id() const;
    // Return the expected server's virtual port.
    int32_t get_virtual_port() const;
    // Return the exact registration's fixed-width nonce as a decimal string.
    godot::String get_registration_id() const;
    // Return the authority's mounted world for registration.
    godot::StringName get_world_id() const;
    // Copy the Steam-signed server login on the scene thread for the trusted coordinator backend only.
    godot::PackedByteArray get_login_blob() const;
    // Return the candidate local client SteamID64, which the backend must authenticate independently.
    godot::String get_local_steam_id() const;

    // Confirm backend registration of this exact server and generation.
    godot::Error registered();
    // Complete client authorization with a bounded Steam-signed relay ticket.
    godot::Error authorize(const godot::PackedByteArray &ticket);
    // Settle this request without registration or a ticket.
    godot::Error fail();
    // Report that the owning Game abandoned the request.
    bool is_cancelled() const;

    // Build the native request for one already validated hosted route.
    static godot::Ref<SteamRelayRequest> create(Kind kind, const steam::SteamHostedRoute &route,
            uint32_t app, const godot::StringName &name_space, const SteamRelayCoordinator &coordinator);
    // Attach the native signed login and committed world to an authority request.
    void set_server_login(const godot::StringName &world, const godot::PackedByteArray &blob);
    // Attach the signed-in SDK's local identity to an authorization request.
    void set_local_account(uint64_t account);
    // Report terminal completion to the exact native Game caller.
    bool is_finished() const;
    // Transfer ownership of the exact acknowledged registration to its directory.
    bool take_registration();
    // Consume the bounded ticket before native SDK validation.
    godot::PackedByteArray take_ticket();
    // Retire this request and compensate a registration that completed after cancellation.
    void cancel();

protected:
    // Register typed request data and completion methods.
    static void _bind_methods();

private:
    // Lifecycle of the exact registration or authorization.
    enum class Phase { PENDING, REGISTERED, AUTHORIZED, FAILED, CONSUMED, CANCELLED, CANCELLED_SETTLED };

    // Purpose chosen by the native directory.
    Kind kind = UNCONFIGURED;
    // Exact native observation state.
    Phase phase = Phase::PENDING;
    // Weak trusted backend for compensating late registrations.
    godot::ObjectID coordinator_id;
    // Validated native authority identity, port, deployment and registration nonce.
    steam::SteamHostedRoute route;
    // AppID captured from the selected SDK pipe.
    uint32_t app_id = 0;
    // Project namespace pinned by the provider.
    godot::StringName name_space;
    // Unique route selected by the native Game, also used for exact withdrawal.
    godot::String listing_id;
    // Stable world mounted before registration.
    godot::StringName world_id;
    // Sensitive SDK registration blob, held only until the backend responds.
    godot::PackedByteArray login_blob;
    // Candidate local identity from Steam's signed-in client pipe.
    uint64_t local_steam_id = 0;
    // Sensitive coordinator authorization held until the native SDK consumes it.
    godot::PackedByteArray relay_ticket;

    // Withdraw only the registration this request represented.
    godot::Error compensate();
};

// Game-owned trusted backend adapter for Valve's hosted relay registration and tickets.
class SteamRelayCoordinator : public godot::Resource {
    GDCLASS(SteamRelayCoordinator, godot::Resource)

public:
    // Report whether both server registration and exact withdrawal are provided.
    bool supports_host();
    // Report whether the backend authorizes client requests.
    bool supports_client();
    // Ask the trusted backend to authenticate and register the ready server.
    void register_server(const godot::Ref<SteamRelayRequest> &request);
    // Ask the trusted backend for a fresh authorized signed ticket.
    void authorize_client(const godot::Ref<SteamRelayRequest> &request);
    // Withdraw a previously submitted exact registration.
    void withdraw(const godot::String &listing_id);

protected:
    // Authenticate the signed server login and acknowledge this exact route asynchronously.
    GDVIRTUAL1(_register_server, godot::Ref<SteamRelayRequest>)
    // Authenticate the caller independently and issue an exact per-client signed ticket.
    GDVIRTUAL1(_authorize_client, godot::Ref<SteamRelayRequest>)
    // Retire one exact registration, including a late success after cancellation.
    GDVIRTUAL1(_withdraw_server, godot::String)
    // Bind the trusted GDScript extension points.
    static void _bind_methods();
};

} // namespace crimson::integration

VARIANT_ENUM_CAST(crimson::integration::SteamRelayRequest::Kind);
