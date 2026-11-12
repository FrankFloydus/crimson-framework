// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include "integration/online/steam/steam_transport.hpp"
#include "modules/session/game_session.hpp"
#include "modules/steam/runtime/steam_socket_runtime.hpp"

#include <steam_api.h>

#include <functional>
#include <map>
#include <memory>

namespace crimson::integration {

struct SteamTicketReservation;

// Client ticket held until the exact Steam game connection completes or leaves.
class SteamClientTicket {
public:
    // Observe issuance before the normal Session connects to the certified server.
    using Issued = std::function<void(godot::Error)>;

    // Start a Steam ticket scoped to the selected listen or dedicated authority identity.
    static std::shared_ptr<SteamClientTicket> issue(const std::shared_ptr<steam::SteamRuntime> &runtime,
            uint64_t server_id, Issued callback);
    // Cancel the exact native ticket and its response watcher.
    ~SteamClientTicket();
    // Retire the ticket without resolving a cancelled directory request.
    void cancel();
    // Build the one-time response for the physically certified selected server.
    godot::PackedByteArray take_proof(uint64_t actual_server_id);
    // Test the certificate-backed server identity selected by directory resolution.
    bool matches(uint64_t actual_server_id) const;

private:
    // Keep the signed-in client pipe alive through exact ticket teardown.
    std::shared_ptr<steam::SteamRuntime> runtime;
    // Exact Steam ticket API selected during issuance.
    ISteamUser *user = nullptr;
    // Ticket expected by the asynchronous Steam issuance callback.
    HAuthTicket handle = k_HAuthTicketInvalid;
    // Watch the exact handle on the owning client pipe.
    uint64_t subscription = 0;
    // Selected Steam authority identity required at the physical transport.
    uint64_t server_id = 0;
    // Bounded proof bytes available once to the provider.
    godot::PackedByteArray proof;
    // Directory resolution callback for the pending issuance.
    Issued on_issued;

    // Decode a copied Steam issuance response for the exact handle.
    void on_response(const void *data, size_t size);
};

// Native Steam ticket validation for the exact certified client or GameServer pipe.
class SteamTicketVerifier {
public:
    // Check whether the current Game still authorizes this certified Steam account.
    using Eligibility = std::function<bool(uint64_t)>;

    // Create a verifier using the signed-in listen authority's ISteamUser.
    static std::shared_ptr<SteamTicketVerifier> for_listen(const std::shared_ptr<steam::SteamRuntime> &runtime,
            const godot::Ref<SteamSessionTransport> &transport, session::GameSession &session, Eligibility eligible);
    // Create a verifier using the separate dedicated SteamGameServer identity.
    static std::shared_ptr<SteamTicketVerifier> for_dedicated(const std::shared_ptr<steam::SteamServerRuntime> &runtime,
            const godot::Ref<SteamSessionTransport> &transport, session::GameSession &session);
    // Drop the Game owner and retain native observation only while old tickets drain.
    void detach_owner();
    // Begin native validation only for a current physically certified peer.
    void verify(const godot::Ref<session::SessionAuthRequest> &request);
    // Prune dropped physical connections while draining pending ticket results.
    void poll();
    // Stop admitted credentials on leave and keep pending tombstones until validated.
    void retire_connections();

private:
    // Begin authentication on the pipe from which validation events will arrive.
    using Begin = std::function<EBeginAuthSessionResult(const void *, int32_t, CSteamID)>;
    // End the exact account's native authentication session.
    using End = std::function<void(CSteamID)>;
    // Check ownership through the validated ticket session and selected AppID.
    using Owns = std::function<bool(CSteamID)>;

    // Steam reports its validation callbacks by account ID rather than ticket handle.
    struct Ticket {
        // Physical Godot peer matched to the current transport's certified account.
        int32_t peer_id = 0;
        // Exact pending GameSession proof operation, dropped when the connection retires.
        godot::Ref<session::SessionAuthRequest> request;
        // Exact verified identity which may be revoked by a later Steam callback.
        godot::Ref<session::SessionIdentity> identity;
        // Awaiting the first validation callback, even after the requester disconnects.
        bool pending = true;
        // Exact account reservation retained across Game attachment lifetimes.
        std::shared_ptr<SteamTicketReservation> reservation;
    };

    // SDK client or GameServer callback pipe issuing the native results.
    std::weak_ptr<steam::SteamSocketRuntime> runtime;
    // Exact native methods selected from that same Steam interface.
    Begin begin;
    End end;
    Owns owns;
    // Optional membership or readiness policy supplied by the Game provider.
    Eligibility eligible;
    // Game's current certified physical peer resolver.
    godot::Ref<SteamSessionTransport> transport;
    // Weak native authority Session receiving only exact identity revocations.
    godot::ObjectID session_id;
    // Steam validation observer attached to this dedicated server.
    uint64_t subscription = 0;
    // Bounded live identities and retired unresolved attempts.
    std::map<uint64_t, Ticket> tickets;
    // Prevent new use after the persistent Game owner has released this verifier.
    bool detached = false;

    // Bind native methods and the current Game before publishing the validator.
    static std::shared_ptr<SteamTicketVerifier> create(const std::shared_ptr<steam::SteamSocketRuntime> &runtime,
            const godot::Ref<SteamSessionTransport> &transport, session::GameSession &session,
            Begin begin, End end, Owns owns, Eligibility eligible);
    // Decode and apply a copied native validation or revocation callback.
    void on_validation(const void *data, size_t size);
    // End native observation once a detached verifier has no pending callbacks.
    void release_listener_if_drained();
    // End the native validation and hold the exact account through a later complete callback drain.
    void end_ticket(const std::shared_ptr<steam::SteamSocketRuntime> &sdk, uint64_t account,
            const std::shared_ptr<SteamTicketReservation> &reservation);
};

// Bilateral proof adapter for one playerless Steam authority.
class SteamDedicatedAuthenticator : public session::SessionAuthenticator {
    GDCLASS(SteamDedicatedAuthenticator, session::SessionAuthenticator)

public:
    // Configure the exact dedicated Session and matching Steam ticket verifier.
    bool configure(const std::shared_ptr<steam::SteamServerRuntime> &runtime,
            const godot::Ref<SteamSessionTransport> &transport, session::GameSession &session);
    // Retire the owner while keeping unresolved native callbacks safe to drain.
    ~SteamDedicatedAuthenticator() override;
    // Prune dropped physical connections while draining pending ticket results.
    void poll();
    // Retire connection tickets when the dedicated Game leaves.
    void retire_connections();
    // Invalidate the persistent owner without discarding old pending observations.
    void detach_owner();

protected:
    // Register the internal dedicated authenticator with Godot.
    static void _bind_methods();
    // Answer the guest using the authority identity already certified by the transport.
    godot::PackedByteArray make_proof(int32_t peer_id, bool authority,
            const godot::PackedByteArray &challenge) override;
    // Validate the guest's challenge and Steam ticket.
    void verify(const godot::Ref<session::SessionAuthRequest> &request) override;

private:
    // Dedicated login and certificate identity used for the authority proof.
    std::shared_ptr<steam::SteamServerRuntime> runtime;
    // Exact native ticket owner for this authority.
    std::shared_ptr<SteamTicketVerifier> verifier;
};

} // namespace crimson::integration

#endif
