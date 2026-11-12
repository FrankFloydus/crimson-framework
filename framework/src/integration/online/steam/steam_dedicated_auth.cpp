// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_dedicated_auth.hpp"

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_runtime.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include <array>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

namespace crimson::integration {
using namespace godot;
using steam::SteamRuntime;
using steam::SteamServerRuntime;

// Keeps one SteamID reserved until its exact native validation can no longer admit a peer.
struct SteamTicketReservation {};

namespace {

// Keep tickets inside the existing bounded Crimson auth proof frame.
constexpr int32_t maximum_ticket_bytes = 4000;
// Leave room for one unresolved generation per configured guest seat.
constexpr size_t maximum_ticket_records = 128;
// Exact account ownership, including callbacks queued before EndAuthSession.
struct AccountReservation {
    // A live or unresolved native validation still holds this reservation.
    std::weak_ptr<SteamTicketReservation> active;
    // True while already queued callbacks from an ended validation may still arrive.
    bool draining = false;
    // Require the next actual end-of-queue observation after ending the validation.
    uint64_t ended_at_epoch = 0;
};

// Independent validation slots for one exact native Steam client or server pipe.
struct PipeReservations {
    // Prevent a recycled native runtime address from inheriting another pipe's reservations.
    std::weak_ptr<steam::SteamSocketRuntime> runtime;
    // Bounded slots keyed by the Steam account carried in Valve validation callbacks.
    std::map<uint64_t, AccountReservation> accounts;
};

// A client or GameServer pipe may have retired observations from a preceding Game.
std::map<steam::SteamSocketRuntime *, PipeReservations> ticket_reservations;

// Reserve an account on its exact Steam pipe while any earlier callback can still arrive.
std::shared_ptr<SteamTicketReservation> reserve_ticket(const std::shared_ptr<steam::SteamSocketRuntime> &sdk,
        uint64_t account) {
    for (auto pipe = ticket_reservations.begin(); pipe != ticket_reservations.end();) {
        const auto live = pipe->second.runtime.lock();
        if (!live) {
            pipe = ticket_reservations.erase(pipe);
            continue;
        }
        auto &accounts = pipe->second.accounts;
        for (auto record = accounts.begin(); record != accounts.end();) {
            const auto &slot = record->second;
            const bool drained = !slot.draining || live->get_callback_drain_epoch() > slot.ended_at_epoch;
            record = slot.active.expired() && drained ? accounts.erase(record) : std::next(record);
        }
        pipe = accounts.empty() ? ticket_reservations.erase(pipe) : std::next(pipe);
    }
    auto &pipe = ticket_reservations[sdk.get()];
    pipe.runtime = sdk;
    auto &accounts = pipe.accounts;
    if (accounts.size() >= maximum_ticket_records || accounts.find(account) != accounts.end()) {
        return {};
    }
    auto reservation = std::make_shared<SteamTicketReservation>();
    accounts.emplace(account, AccountReservation{reservation});
    return reservation;
}

// Read the exact current authority node without extending its lifetime.
session::GameSession *get_session(ObjectID id) {
    return Object::cast_to<session::GameSession>(ObjectDB::get_instance(id));
}

// Resolve ISteamUser without linking an SDK import library.
ISteamUser *get_signed_in_user(const std::shared_ptr<SteamRuntime> &sdk) {
    const auto get = sdk ? sdk->symbol<decltype(&SteamAPI_SteamUser_v023)>("SteamAPI_SteamUser_v023") : nullptr;
    return get ? get() : nullptr;
}

// Build the authority proof from its transport-certified GameServer account.
PackedByteArray identity_proof(uint64_t id, const PackedByteArray &challenge) {
    PackedByteArray proof = challenge;
    if (id == 0 || proof.resize(challenge.size() + sizeof(id)) != OK) {
        return {};
    }
    proof.encode_u64(challenge.size(), id);
    return proof;
}

} // namespace

std::shared_ptr<SteamClientTicket> SteamClientTicket::issue(const std::shared_ptr<SteamRuntime> &sdk,
        uint64_t selected, Issued callback) {
    const CSteamID server(selected);
    if (!sdk || !callback || !server.IsValid() || (server.GetEAccountType() != k_EAccountTypeGameServer &&
                server.GetEAccountType() != k_EAccountTypeAnonGameServer &&
                server.GetEAccountType() != k_EAccountTypeIndividual)) {
        return {};
    }
    ISteamUser *user = get_signed_in_user(sdk);
    if (!user) {
        return {};
    }

    auto ticket = std::shared_ptr<SteamClientTicket>(new SteamClientTicket());
    ticket->runtime = sdk;
    ticket->user = user;
    ticket->server_id = selected;
    ticket->on_issued = std::move(callback);
    const std::weak_ptr<SteamClientTicket> weak = ticket;
    ticket->subscription = sdk->subscribe(GetAuthSessionTicketResponse_t::k_iCallback,
            [weak](const void *data, size_t size) {
                if (const auto live = weak.lock()) {
                    live->on_response(data, size);
                }
            });
    if (ticket->subscription == 0) {
        return {};
    }

    SteamNetworkingIdentity receiver;
    receiver.Clear();
    receiver.SetSteamID64(selected);
    std::array<uint8_t, maximum_ticket_bytes> bytes{};
    uint32_t length = 0;
    ticket->handle = user->GetAuthSessionTicket(bytes.data(), static_cast<int>(bytes.size()), &length, &receiver);
    if (ticket->handle == k_HAuthTicketInvalid || length == 0 || length > bytes.size() ||
            ticket->proof.resize(length) != OK) {
        return {};
    }
    std::memcpy(ticket->proof.ptrw(), bytes.data(), length);
    return ticket;
}

SteamClientTicket::~SteamClientTicket() {
    cancel();
}

void SteamClientTicket::cancel() {
    if (runtime && subscription != 0) {
        runtime->unsubscribe(std::exchange(subscription, 0));
    }
    if (user && handle != k_HAuthTicketInvalid) {
        user->CancelAuthTicket(std::exchange(handle, k_HAuthTicketInvalid));
    }
    on_issued = {};
    proof.clear();
}

void SteamClientTicket::on_response(const void *data, size_t size) {
    if (!data || size != sizeof(GetAuthSessionTicketResponse_t)) {
        return;
    }
    GetAuthSessionTicketResponse_t event{};
    std::memcpy(&event, data, sizeof(event));
    if (event.m_hAuthTicket != handle || !on_issued) {
        return;
    }
    runtime->unsubscribe(std::exchange(subscription, 0));
    const Issued completion = std::exchange(on_issued, {});
    if (event.m_eResult != k_EResultOK) {
        cancel();
        completion(ERR_UNAUTHORIZED);
        return;
    }
    completion(OK);
}

PackedByteArray SteamClientTicket::take_proof(uint64_t actual_server_id) {
    return matches(actual_server_id) && !on_issued ? std::exchange(proof, {}) : PackedByteArray();
}

bool SteamClientTicket::matches(uint64_t actual_server_id) const {
    return handle != k_HAuthTicketInvalid && actual_server_id == server_id;
}

std::shared_ptr<SteamTicketVerifier> SteamTicketVerifier::for_listen(const std::shared_ptr<SteamRuntime> &sdk,
        const Ref<SteamSessionTransport> &adapter, session::GameSession &session, Eligibility eligible) {
    auto *api = get_signed_in_user(sdk);
    if (!api) {
        return {};
    }
    const uint32_t app = sdk->get_app_id();
    return create(sdk, adapter, session,
            [api](const void *data, int32_t size, CSteamID account) { return api->BeginAuthSession(data, size, account); },
            [api](CSteamID account) { api->EndAuthSession(account); },
            [api, app](CSteamID account) { return api->UserHasLicenseForApp(account, app) == k_EUserHasLicenseResultHasLicense; },
            std::move(eligible));
}

std::shared_ptr<SteamTicketVerifier> SteamTicketVerifier::for_dedicated(const std::shared_ptr<SteamServerRuntime> &sdk,
        const Ref<SteamSessionTransport> &adapter, session::GameSession &session) {
    if (!sdk || !sdk->get_server()) {
        return {};
    }
    auto *api = sdk->get_server();
    const uint32_t app = sdk->get_app_id();
    const std::weak_ptr<SteamServerRuntime> weak = sdk;
    return create(sdk, adapter, session,
            [api](const void *data, int32_t size, CSteamID account) { return api->BeginAuthSession(data, size, account); },
            [api](CSteamID account) { api->EndAuthSession(account); },
            [api, app](CSteamID account) { return api->UserHasLicenseForApp(account, app) == k_EUserHasLicenseResultHasLicense; },
            [weak](uint64_t) {
                const auto live = weak.lock();
                return live && live->is_ready();
            });
}

std::shared_ptr<SteamTicketVerifier> SteamTicketVerifier::create(
        const std::shared_ptr<steam::SteamSocketRuntime> &sdk, const Ref<SteamSessionTransport> &adapter,
        session::GameSession &session, Begin begin, End end, Owns owns, Eligibility eligible) {
    if (!sdk || adapter.is_null() || !begin || !end || !owns || !eligible) {
        return {};
    }
    auto verifier = std::shared_ptr<SteamTicketVerifier>(new SteamTicketVerifier());
    verifier->runtime = sdk;
    verifier->transport = adapter;
    verifier->session_id = ObjectID(session.get_instance_id());
    verifier->begin = std::move(begin);
    verifier->end = std::move(end);
    verifier->owns = std::move(owns);
    verifier->eligible = std::move(eligible);
    verifier->subscription = sdk->subscribe(ValidateAuthTicketResponse_t::k_iCallback,
            [verifier](const void *data, size_t size) {
                verifier->on_validation(data, size);
            });
    return verifier->subscription != 0 ? verifier : std::shared_ptr<SteamTicketVerifier>();
}

void SteamTicketVerifier::detach_owner() {
    if (!std::exchange(detached, true)) {
        retire_connections();
        transport.unref();
        release_listener_if_drained();
    }
}

void SteamTicketVerifier::release_listener_if_drained() {
    if (detached && tickets.empty()) {
        if (const auto sdk = runtime.lock()) {
            sdk->unsubscribe(std::exchange(subscription, 0));
        }
    }
}

void SteamTicketVerifier::end_ticket(const std::shared_ptr<steam::SteamSocketRuntime> &sdk, uint64_t account,
        const std::shared_ptr<SteamTicketReservation> &reservation) {
    const auto pipe = ticket_reservations.find(sdk.get());
    if (pipe != ticket_reservations.end()) {
        const auto slot = pipe->second.accounts.find(account);
        if (slot != pipe->second.accounts.end() && slot->second.active.lock() == reservation) {
            slot->second.draining = true;
            slot->second.ended_at_epoch = sdk->get_callback_drain_epoch();
        }
    }
    end(CSteamID(account));
}

void SteamTicketVerifier::verify(const Ref<session::SessionAuthRequest> &request) {
    const auto sdk = runtime.lock();
    if (request.is_null() || !request->is_authority() || detached || !sdk || transport.is_null()) {
        if (request.is_valid()) {
            request->reject();
        }
        return;
    }
    const int32_t peer = request->get_peer_id();
    const uint64_t account = transport->get_remote_steam_id(peer);
    const PackedByteArray challenge = request->get_challenge();
    const PackedByteArray proof = request->get_proof();
    const CSteamID client(account);
    if (!client.IsValid() || client.GetEAccountType() != k_EAccountTypeIndividual ||
            proof.size() <= challenge.size() || proof.size() - challenge.size() > maximum_ticket_bytes ||
            proof.slice(0, challenge.size()) != challenge || !eligible(account)) {
        request->reject();
        return;
    }

    // Reserve the certified account before asking Steam to accept a new ticket for it.
    const auto reservation = reserve_ticket(sdk, account);
    if (!reservation) {
        request->reject();
        return;
    }
    auto record = tickets.emplace(account, Ticket{peer, request, {}, true, reservation}).first;
    const auto result = begin(proof.ptr() + challenge.size(),
            static_cast<int>(proof.size() - challenge.size()), client);
    if (result != k_EBeginAuthSessionResultOK) {
        tickets.erase(record);
        request->reject();
    }
}

void SteamTicketVerifier::on_validation(const void *data, size_t size) {
    const auto sdk = runtime.lock();
    if (!sdk || !data || size != sizeof(ValidateAuthTicketResponse_t)) {
        return;
    }
    ValidateAuthTicketResponse_t event{};
    std::memcpy(&event, data, sizeof(event));
    const uint64_t account = event.m_SteamID.ConvertToUint64();
    const auto found = tickets.find(account);
    if (found == tickets.end()) {
        return;
    }

    Ticket &ticket = found->second;
    const bool connected = !detached && transport.is_valid() && transport->get_remote_steam_id(ticket.peer_id) == account;
    const bool valid = connected && event.m_eAuthSessionResponse == k_EAuthSessionResponseOK &&
            eligible(account) && owns(event.m_SteamID);
    if (ticket.pending && valid && ticket.request.is_valid()) {
        ticket.pending = false;
        ticket.identity = session::SessionIdentity::verified("steam", String(std::to_string(account).c_str()));
        const auto request = std::exchange(ticket.request, {});
        if (request->accept(ticket.identity) == OK) {
            return;
        }
    } else if (!ticket.pending && valid) {
        return;
    }

    const auto retired = ticket;
    tickets.erase(found);
    end_ticket(sdk, account, retired.reservation);
    release_listener_if_drained();
    if (retired.request.is_valid()) {
        retired.request->reject();
    }
    if (retired.identity.is_valid()) {
        if (auto *session = get_session(session_id)) {
            session->revoke_verified_peer(retired.peer_id, retired.identity);
        }
    }
}

void SteamTicketVerifier::poll() {
    const auto sdk = runtime.lock();
    if (detached || !sdk || transport.is_null()) {
        return;
    }
    for (auto record = tickets.begin(); record != tickets.end();) {
        Ticket &ticket = record->second;
        if (ticket.peer_id != 0 && transport->get_remote_steam_id(ticket.peer_id) != record->first) {
            if (ticket.pending) {
                // Keep a tombstone until this account's uncorrelated initial callback is drained.
                ticket.peer_id = 0;
                ticket.request.unref();
            } else {
                end_ticket(sdk, record->first, ticket.reservation);
                record = tickets.erase(record);
                continue;
            }
        }
        ++record;
    }
}

void SteamTicketVerifier::retire_connections() {
    const auto sdk = runtime.lock();
    if (!sdk) {
        tickets.clear();
        return;
    }
    for (auto record = tickets.begin(); record != tickets.end();) {
        if (record->second.pending) {
            record->second.peer_id = 0;
            record->second.request.unref();
            ++record;
        } else {
            end_ticket(sdk, record->first, record->second.reservation);
            record = tickets.erase(record);
        }
    }
}

void SteamDedicatedAuthenticator::_bind_methods() {}

SteamDedicatedAuthenticator::~SteamDedicatedAuthenticator() {
    detach_owner();
}

bool SteamDedicatedAuthenticator::configure(const std::shared_ptr<SteamServerRuntime> &sdk,
        const Ref<SteamSessionTransport> &adapter, session::GameSession &session) {
    if (runtime) {
        return false;
    }
    verifier = SteamTicketVerifier::for_dedicated(sdk, adapter, session);
    if (!verifier) {
        return false;
    }
    runtime = sdk;
    return true;
}

PackedByteArray SteamDedicatedAuthenticator::make_proof(int32_t, bool authority, const PackedByteArray &challenge) {
    return authority && runtime && runtime->is_ready() ? identity_proof(runtime->get_local_socket_id(), challenge) :
                                                         PackedByteArray();
}

void SteamDedicatedAuthenticator::verify(const Ref<session::SessionAuthRequest> &request) {
    if (verifier) {
        verifier->verify(request);
    } else if (request.is_valid()) {
        request->reject();
    }
}

void SteamDedicatedAuthenticator::poll() {
    if (verifier) {
        verifier->poll();
    }
}

void SteamDedicatedAuthenticator::retire_connections() {
    if (verifier) {
        verifier->retire_connections();
    }
}

void SteamDedicatedAuthenticator::detach_owner() {
    if (const auto closing = std::exchange(verifier, {})) {
        closing->detach_owner();
    }
    runtime.reset();
}

} // namespace crimson::integration

#endif
