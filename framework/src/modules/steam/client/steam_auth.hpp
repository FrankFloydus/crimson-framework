// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <map>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Owns a Web API authentication ticket issued for one trusted service.
class SteamWebTicket : public godot::RefCounted {
    GDCLASS(SteamWebTicket, godot::RefCounted)

public:
    // Return the opaque bytes for transport to the trusted backend.
    godot::PackedByteArray bytes() const;
    // Cancel the native ticket and release this local copy.
    void close();

protected:
    // Register scoped ticket access and cleanup.
    static void _bind_methods();

private:
    friend class SteamAuth;

    // Exact service retaining the native ticket.
    godot::ObjectID owner;
    // Native HAuthTicket, cleared when retired.
    uint32_t handle = 0;
    // Shared liveness of the issuing client start.
    std::weak_ptr<SteamClientScope> scope;
    // Private SDK ticket bytes retained until close or scope stop.
    godot::PackedByteArray payload;
};

// Outcome of obtaining a Web API authentication ticket.
class SteamWebTicketResult : public SteamResult {
    GDCLASS(SteamWebTicketResult, SteamResult)

public:
    // Return the revocable ticket on success.
    godot::Ref<SteamWebTicket> get_value() const;

protected:
    // Register the typed ticket payload.
    static void _bind_methods();

private:
    friend class SteamAuth;

    // Create the terminal result for a Web API ticket request.
    static godot::Ref<SteamWebTicketResult> create(godot::Error error,
            const godot::Ref<SteamWebTicket> &ticket = {});
    // Ticket owned by the client until explicitly closed or stopped.
    godot::Ref<SteamWebTicket> value;
};

// Holds an encrypted Steam ownership ticket for one backend challenge.
class SteamOwnershipTicket : public godot::RefCounted {
    GDCLASS(SteamOwnershipTicket, godot::RefCounted)

public:
    // Return the opaque encrypted ticket for transport to a trusted backend.
    godot::PackedByteArray bytes() const;

protected:
    // Register the encrypted byte access method.
    static void _bind_methods();

private:
    friend class SteamAuth;

    // Encrypted ticket copied after the exact SDK call succeeds.
    godot::PackedByteArray payload;
};

// Outcome of requesting an encrypted Steam ownership ticket.
class SteamOwnershipTicketResult : public SteamResult {
    GDCLASS(SteamOwnershipTicketResult, SteamResult)

public:
    // Return the encrypted ticket on success.
    godot::Ref<SteamOwnershipTicket> get_value() const;

protected:
    // Register the typed encrypted ticket payload.
    static void _bind_methods();

private:
    friend class SteamAuth;

    // Create the terminal result for an encrypted ticket request.
    static godot::Ref<SteamOwnershipTicketResult> create(godot::Error error,
            const godot::Ref<SteamOwnershipTicket> &ticket = {});
    // Encrypted ticket ready for transport on success.
    godot::Ref<SteamOwnershipTicket> value;
};

// Carries one typed completion for a direct authentication request.
class SteamAuthOperation : public godot::RefCounted {
    GDCLASS(SteamAuthOperation, godot::RefCounted)

protected:
    // Register the separate typed completion signals.
    static void _bind_methods();
};

// Issues client-side authentication and ownership proof for trusted verification.
class SteamAuth : public godot::RefCounted {
    GDCLASS(SteamAuth, godot::RefCounted)

public:
    // Await a Web API authentication ticket for the specified service audience.
    godot::Variant web_ticket(const godot::String &audience);
    // Await an encrypted app ticket that embeds the backend's challenge.
    godot::Variant ownership_ticket(const godot::PackedByteArray &challenge);

protected:
    // Register the direct await API.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamWebTicket;

    // A typed signal observed until the SDK callback or local expiry.
    struct Request {
        // Script-facing signal carrier owned while pending.
        godot::Ref<SteamAuthOperation> operation;
        // Local observation deadline in monotonic milliseconds.
        uint64_t deadline = 0;
    };

    // Associate the facade with one exact client scope.
    void configure(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime);
    // Invalidate this scope, cancel web handles, and settle local observers.
    void close();
    // Check request deadlines without retiring a still-pending encrypted SDK call.
    void tick();
    // Resolve Steam only on the main thread while the issuing scope is active.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Handle the signed-in client's exact Web API ticket callback.
    void on_web_ticket(const void *data, size_t size);
    // Release the exact web ticket after verifying its registered object.
    void close_web_ticket(SteamWebTicket *ticket);
    // Process the exact SDK result after the native encrypted reservation is cleared.
    void on_ownership_ticket(uint64_t call, const void *data);

    // Current process runtime, retained by the client.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact issuing generation, invalidated before cancellation signals.
    std::weak_ptr<SteamClientScope> scope;
    // Subscription for Web API ticket responses.
    uint64_t web_subscription = 0;
    // Pending ticket requests keyed by the exact HAuthTicket.
    std::map<uint32_t, Request> pending_web;
    // Issued tickets with active handles until explicitly closed or stopped.
    std::map<uint32_t, godot::Ref<SteamWebTicket>> issued_web;
    // The outstanding encrypted ticket observation, if owned by this facade.
    Request pending_ownership;
    // Call handle identifying this scope's pending encrypted observation.
    uint64_t ownership_call = 0;
};

} // namespace crimson::steam
