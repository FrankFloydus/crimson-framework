// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_auth.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <cstring>
#include <utility>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Bound the service audience and encrypted challenge accepted from scripts.
constexpr int64_t maximum_audience_bytes = 255;
constexpr int64_t maximum_challenge_bytes = 1024;

// Require a named service and reject control characters before passing it to Steam.
bool valid_audience(const CharString &audience) {
    if (audience.length() == 0 || audience.length() > maximum_audience_bytes) {
        return false;
    }
    for (int64_t index = 0; index < audience.length(); ++index) {
        const auto byte = static_cast<unsigned char>(audience[index]);
        if (byte < 0x20 || byte == 0x7f) {
            return false;
        }
    }
    return true;
}

#ifdef CRIMSON_WITH_STEAM
// Bound unclosed native handles per client start.
constexpr size_t maximum_web_tickets = 16;
// Bound copied ticket data returned from the app ticket interface.
constexpr uint32_t maximum_ownership_ticket_bytes = 16 * 1024;
// Local deadline for either ticket observer.
constexpr uint64_t ticket_timeout_msec = 15000;

// Resolve Steam's signed-in User interface without exposing it to script.
ISteamUser *get_user(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamUser_v023)>("SteamAPI_SteamUser_v023") : nullptr;
    return get ? get() : nullptr;
}

// Hold the exact service alive while a copied callback or ticket close calls it.
Ref<SteamAuth> auth_service(ObjectID owner) {
    return Ref<SteamAuth>(Object::cast_to<SteamAuth>(ObjectDB::get_instance(owner)));
}

// Translate Steam's authentication outcomes without returning credentials or raw SDK text.
Error ticket_error(EResult result) {
    switch (result) {
        case k_EResultOK: return OK;
        case k_EResultInvalidParam: return ERR_INVALID_PARAMETER;
        case k_EResultAccessDenied: return ERR_UNAUTHORIZED;
        case k_EResultDuplicateRequest:
        case k_EResultLimitExceeded:
        case k_EResultRateLimitExceeded: return ERR_BUSY;
        case k_EResultNoConnection:
        case k_EResultNotLoggedOn: return ERR_CONNECTION_ERROR;
        case k_EResultTimeout: return ERR_TIMEOUT;
        case k_EResultServiceUnavailable: return ERR_UNAVAILABLE;
        default: return FAILED;
    }
}

// Cancel an exact native Web API ticket before settling its observer or releasing bytes.
void cancel_web_ticket(const std::shared_ptr<SteamRuntime> &runtime, uint32_t handle) {
    if (ISteamUser *user = get_user(runtime)) {
        const auto cancel = runtime->symbol<decltype(&SteamAPI_ISteamUser_CancelAuthTicket)>(
                "SteamAPI_ISteamUser_CancelAuthTicket");
        if (cancel) {
            cancel(user, handle);
        }
    }
}
#endif

} // namespace

void SteamWebTicket::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bytes"), &SteamWebTicket::bytes);
    ClassDB::bind_method(D_METHOD("close"), &SteamWebTicket::close);
}

PackedByteArray SteamWebTicket::bytes() const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return {};
    }
    const auto lifetime = scope.lock();
    return lifetime && lifetime->active ? payload : PackedByteArray();
#else
    return {};
#endif
}

void SteamWebTicket::close() {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && handle != 0) {
        if (const auto service = auth_service(owner); service.is_valid()) {
            service->close_web_ticket(this);
        }
    }
#endif
}

void SteamWebTicketResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWebTicketResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWebTicket",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamWebTicket> SteamWebTicketResult::get_value() const {
    return value;
}

Ref<SteamWebTicketResult> SteamWebTicketResult::create(Error error, const Ref<SteamWebTicket> &ticket) {
    Ref<SteamWebTicketResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = ticket;
    return result;
}

void SteamOwnershipTicket::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bytes"), &SteamOwnershipTicket::bytes);
}

PackedByteArray SteamOwnershipTicket::bytes() const {
    return payload;
}

void SteamOwnershipTicketResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamOwnershipTicketResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamOwnershipTicket",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamOwnershipTicket> SteamOwnershipTicketResult::get_value() const {
    return value;
}

Ref<SteamOwnershipTicketResult> SteamOwnershipTicketResult::create(
        Error error, const Ref<SteamOwnershipTicket> &ticket) {
    Ref<SteamOwnershipTicketResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = ticket;
    return result;
}

void SteamAuthOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("web_ticket_completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamWebTicketResult")));
    ADD_SIGNAL(MethodInfo("ownership_ticket_completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamOwnershipTicketResult")));
}

void SteamAuth::_bind_methods() {
    ClassDB::bind_method(D_METHOD("web_ticket", "audience"), &SteamAuth::web_ticket);
    ClassDB::bind_method(D_METHOD("ownership_ticket", "challenge"), &SteamAuth::ownership_ticket);
}

void SteamAuth::configure(
        const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime) {
    runtime = active;
    scope = lifetime;
}

std::shared_ptr<SteamRuntime> SteamAuth::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto lifetime = scope.lock();
        if (lifetime && lifetime->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

Variant SteamAuth::web_ticket(const String &audience) {
    const CharString text = audience.utf8();
    if (!valid_audience(text) || audience.strip_edges().is_empty()) {
        return SteamWebTicketResult::create(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamUser *user = get_user(active);
    const auto get = user ? active->symbol<decltype(&SteamAPI_ISteamUser_GetAuthTicketForWebApi)>(
                                    "SteamAPI_ISteamUser_GetAuthTicketForWebApi")
                          : nullptr;
    const auto cancel = user ? active->symbol<decltype(&SteamAPI_ISteamUser_CancelAuthTicket)>(
                                       "SteamAPI_ISteamUser_CancelAuthTicket")
                             : nullptr;
    if (!get || !cancel) {
        return SteamWebTicketResult::create(ERR_UNAVAILABLE);
    }
    if (pending_web.size() + issued_web.size() >= maximum_web_tickets) {
        return SteamWebTicketResult::create(ERR_BUSY);
    }
    if (web_subscription == 0) {
        const ObjectID self(get_instance_id());
        web_subscription = active->subscribe(GetTicketForWebApiResponse_t::k_iCallback,
                [self](const void *data, size_t size) {
                    if (const auto service = auth_service(self); service.is_valid()) {
                        service->on_web_ticket(data, size);
                    }
                });
        if (web_subscription == 0) {
            return SteamWebTicketResult::create(ERR_BUSY);
        }
    }

    const HAuthTicket handle = get(user, text.get_data());
    if (handle == k_HAuthTicketInvalid || pending_web.count(handle) || issued_web.count(handle)) {
        return SteamWebTicketResult::create(FAILED);
    }
    Ref<SteamAuthOperation> operation;
    operation.instantiate();
    pending_web.emplace(handle, Request{operation, Time::get_singleton()->get_ticks_msec() + ticket_timeout_msec});
    return Signal(operation.ptr(), "web_ticket_completed");
#else
    return SteamWebTicketResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamAuth::on_web_ticket(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || !data || size != sizeof(GetTicketForWebApiResponse_t)) {
        return;
    }
    GetTicketForWebApiResponse_t event{};
    std::memcpy(&event, data, sizeof(event));
    auto found = pending_web.extract(event.m_hAuthTicket);
    if (found.empty()) {
        return;
    }
    const auto &request = found.mapped();
    Error status = Time::get_singleton()->get_ticks_msec() >= request.deadline
            ? ERR_TIMEOUT
            : ticket_error(event.m_eResult);
    Ref<SteamWebTicket> ticket;
    if (status == OK) {
        if (event.m_cubTicket <= 0 || event.m_cubTicket > GetTicketForWebApiResponse_t::k_nCubTicketMaxLength) {
            status = FAILED;
        } else {
            ticket.instantiate();
            if (ticket->payload.resize(event.m_cubTicket) != OK) {
                status = ERR_OUT_OF_MEMORY;
                ticket.unref();
            } else {
                std::memcpy(ticket->payload.ptrw(), event.m_rgubTicket, static_cast<size_t>(event.m_cubTicket));
                ticket->owner = ObjectID(get_instance_id());
                ticket->handle = event.m_hAuthTicket;
                ticket->scope = scope;
                issued_web.emplace(ticket->handle, ticket);
            }
        }
    }
    if (status != OK) {
        cancel_web_ticket(active, event.m_hAuthTicket);
    }
    request.operation->emit_signal("web_ticket_completed", SteamWebTicketResult::create(status, ticket));
#endif
}

void SteamAuth::close_web_ticket(SteamWebTicket *ticket) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    const auto found = issued_web.find(ticket->handle);
    if (found == issued_web.end() || found->second.ptr() != ticket) {
        return;
    }
    const auto registered = issued_web.extract(found);
    ticket->payload.clear();
    ticket->scope.reset();
    ticket->owner = ObjectID();
    cancel_web_ticket(active, std::exchange(ticket->handle, 0));
#endif
}

Variant SteamAuth::ownership_ticket(const PackedByteArray &challenge) {
    if (challenge.is_empty() || challenge.size() > maximum_challenge_bytes) {
        return SteamOwnershipTicketResult::create(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    ISteamUser *user = get_user(active);
    const auto request = user ? active->symbol<decltype(&SteamAPI_ISteamUser_RequestEncryptedAppTicket)>(
                                        "SteamAPI_ISteamUser_RequestEncryptedAppTicket")
                              : nullptr;
    const auto get = user ? active->symbol<decltype(&SteamAPI_ISteamUser_GetEncryptedAppTicket)>(
                                    "SteamAPI_ISteamUser_GetEncryptedAppTicket")
                          : nullptr;
    if (!request || !get) {
        return SteamOwnershipTicketResult::create(ERR_UNAVAILABLE);
    }
    if (active->encrypted_ticket_call != 0 || !active->can_track_call()) {
        return SteamOwnershipTicketResult::create(ERR_BUSY);
    }

    PackedByteArray input = challenge;
    const auto call = request(user, input.ptrw(), static_cast<int>(input.size()));
    if (call == k_uAPICallInvalid) {
        return SteamOwnershipTicketResult::create(FAILED);
    }
    active->encrypted_ticket_call = call;
    const ObjectID self(get_instance_id());
    const std::weak_ptr<SteamRuntime> weak_runtime(active);
    if (!active->watch_call(call, EncryptedAppTicketResponse_t::k_iCallback, sizeof(EncryptedAppTicketResponse_t),
                [weak_runtime, call, self](const void *data) {
                    const auto live = weak_runtime.lock();
                    if (!live || live->encrypted_ticket_call != call) {
                        return;
                    }
                    live->encrypted_ticket_call = 0;
                    if (const auto service = auth_service(self); service.is_valid()) {
                        service->on_ownership_ticket(call, data);
                    }
                })) {
        return SteamOwnershipTicketResult::create(ERR_BUSY);
    }

    pending_ownership.operation.instantiate();
    pending_ownership.deadline = Time::get_singleton()->get_ticks_msec() + ticket_timeout_msec;
    ownership_call = call;
    return Signal(pending_ownership.operation.ptr(), "ownership_ticket_completed");
#else
    return SteamOwnershipTicketResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamAuth::on_ownership_ticket(uint64_t call, const void *data) {
#ifdef CRIMSON_WITH_STEAM
    if (ownership_call != call || pending_ownership.operation.is_null()) {
        return;
    }
    ownership_call = 0;
    const auto request = std::exchange(pending_ownership, {});
    const auto active = active_runtime();
    Error status = ERR_SKIP;
    Ref<SteamOwnershipTicket> ticket;
    if (active && Time::get_singleton()->get_ticks_msec() >= request.deadline) {
        status = ERR_TIMEOUT;
    } else if (active) {
        EncryptedAppTicketResponse_t response{};
        if (data) {
            std::memcpy(&response, data, sizeof(response));
        }
        status = data ? ticket_error(response.m_eResult) : FAILED;
        if (status == OK) {
            ISteamUser *user = get_user(active);
            const auto get = user ? active->symbol<decltype(&SteamAPI_ISteamUser_GetEncryptedAppTicket)>(
                                            "SteamAPI_ISteamUser_GetEncryptedAppTicket")
                                  : nullptr;
            uint32_t needed = 0;
            if (get) {
                get(user, nullptr, 0, &needed);
            }
            if (!get || needed == 0 || needed > maximum_ownership_ticket_bytes) {
                status = FAILED;
            } else {
                ticket.instantiate();
                uint32_t copied = 0;
                if (ticket->payload.resize(needed) != OK) {
                    status = ERR_OUT_OF_MEMORY;
                } else if (!get(user, ticket->payload.ptrw(), static_cast<int>(needed), &copied) ||
                        copied == 0 || copied > needed || ticket->payload.resize(copied) != OK) {
                    status = FAILED;
                }
                if (status != OK) {
                    ticket.unref();
                }
            }
        }
    }
    request.operation->emit_signal("ownership_ticket_completed", SteamOwnershipTicketResult::create(status, ticket));
#endif
}

void SteamAuth::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<Ref<SteamAuthOperation>> expired;
    for (auto found = pending_web.begin(); found != pending_web.end();) {
        if (now < found->second.deadline) {
            ++found;
            continue;
        }
        cancel_web_ticket(active, found->first);
        expired.push_back(found->second.operation);
        found = pending_web.erase(found);
    }
    Ref<SteamAuthOperation> expired_ownership;
    if (pending_ownership.operation.is_valid() && now >= pending_ownership.deadline) {
        expired_ownership = std::exchange(pending_ownership, {}).operation;
        ownership_call = 0;
    }
    for (const auto &operation : expired) {
        operation->emit_signal("web_ticket_completed", SteamWebTicketResult::create(ERR_TIMEOUT));
    }
    if (expired_ownership.is_valid()) {
        expired_ownership->emit_signal("ownership_ticket_completed", SteamOwnershipTicketResult::create(ERR_TIMEOUT));
    }
#endif
}

void SteamAuth::close() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    runtime.reset();
    scope.reset();
    const auto pending = std::exchange(pending_web, {});
    const auto issued = std::exchange(issued_web, {});
    const auto ownership = std::exchange(pending_ownership, {});
    ownership_call = 0;
    if (active) {
        active->unsubscribe(std::exchange(web_subscription, 0));
        for (const auto &[handle, request] : pending) {
            cancel_web_ticket(active, handle);
        }
        for (const auto &[handle, ticket] : issued) {
            cancel_web_ticket(active, handle);
            ticket->payload.clear();
            ticket->scope.reset();
            ticket->owner = ObjectID();
            ticket->handle = 0;
        }
    }
    for (const auto &[handle, request] : pending) {
        request.operation->emit_signal("web_ticket_completed", SteamWebTicketResult::create(ERR_SKIP));
    }
    if (ownership.operation.is_valid()) {
        ownership.operation->emit_signal("ownership_ticket_completed", SteamOwnershipTicketResult::create(ERR_SKIP));
    }
#endif
}

} // namespace crimson::steam
