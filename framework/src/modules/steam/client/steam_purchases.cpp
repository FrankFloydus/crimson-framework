// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_purchases.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>
#include <vector>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <cstring>
#include <string>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Limit pending script-backed observers and the associated local order cache.
constexpr size_t maximum_requests = 16;
constexpr size_t maximum_orders = 128;
// Await a receipt for at most 20 seconds before exposing the stable retry ID.
constexpr uint64_t request_timeout_msec = 20000;

// Resolve the exact owner on Godot's main thread without holding an idle facade alive.
Ref<SteamPurchases> purchases_service(ObjectID owner) {
    const auto *os = OS::get_singleton();
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id()) {
        return {};
    }
    return Ref<SteamPurchases>(Object::cast_to<SteamPurchases>(ObjectDB::get_instance(owner)));
}

} // namespace

void SteamCommerceRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("complete_catalog", "result"), &SteamCommerceRequest::complete_catalog);
    ClassDB::bind_method(D_METHOD("complete_order", "result"), &SteamCommerceRequest::complete_order);
    ADD_SIGNAL(MethodInfo("catalog_completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamPurchaseCatalogResult")));
    ADD_SIGNAL(MethodInfo("order_completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamPurchaseOutcomeResult")));
}

Error SteamCommerceRequest::complete_catalog(const Ref<SteamPurchaseCatalogResult> &result) {
    const auto *os = OS::get_singleton();
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id()) {
        return ERR_BUSY;
    }
    const auto service = purchases_service(owner);
    return service.is_valid() ? service->finish_catalog(this, result) : ERR_UNAVAILABLE;
}

Error SteamCommerceRequest::complete_order(const Ref<SteamPurchaseOutcomeResult> &result) {
    const auto *os = OS::get_singleton();
    if (!os || os->get_thread_caller_id() != os->get_main_thread_id()) {
        return ERR_BUSY;
    }
    const auto service = purchases_service(owner);
    return service.is_valid() ? service->finish_order(this, result) : ERR_UNAVAILABLE;
}

void SteamCommerceBackend::_bind_methods() {
    GDVIRTUAL_BIND(_catalog, "request");
    GDVIRTUAL_BIND(_begin, "cart", "checkout_id", "request");
    GDVIRTUAL_BIND(_observe_authorization, "order_id", "authorized", "request");
    GDVIRTUAL_BIND(_order, "order_id", "request");
}

void SteamPurchases::_bind_methods() {
    ClassDB::bind_method(D_METHOD("catalog"), &SteamPurchases::catalog);
    ClassDB::bind_method(D_METHOD("checkout", "cart"), &SteamPurchases::checkout);
    ClassDB::bind_method(D_METHOD("order", "order_id"), &SteamPurchases::order);
    ADD_SIGNAL(MethodInfo("order_changed", PropertyInfo(Variant::OBJECT, "outcome",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamPurchaseOutcome")));
}

void SteamPurchases::configure(
        const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime) {
    runtime = active;
    scope = lifetime;
}

void SteamPurchases::bind_backend(const Ref<SteamCommerceBackend> &adapter) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || adapter.is_null() || backend.is_valid()) {
        return;
    }
    const ObjectID self(get_instance_id());
    authorization_subscription = active->subscribe(MicroTxnAuthorizationResponse_t::k_iCallback,
            [self](const void *data, size_t size) {
                if (const auto service = purchases_service(self); service.is_valid()) {
                    service->on_authorization(data, size);
                }
            });
    if (authorization_subscription != 0) {
        backend = adapter;
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamPurchases::active_runtime() const {
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

Ref<SteamCommerceRequest> SteamPurchases::begin_request(
        Kind kind, const String &order_id, const String &checkout_id) {
    if (pending.size() >= maximum_requests) {
        return {};
    }
    for (const auto &[id, request] : pending) {
        if ((!order_id.is_empty() && request.order_id == order_id) ||
                (!checkout_id.is_empty() && request.checkout_id == checkout_id)) {
            return {};
        }
    }
    Ref<SteamCommerceRequest> request;
    request.instantiate();
    request->owner = ObjectID(get_instance_id());
    request->id = next_request++;
    pending.emplace(request->id, Pending{request, kind, order_id, checkout_id,
            Time::get_singleton()->get_ticks_msec() + request_timeout_msec});
    return request;
}

Variant SteamPurchases::catalog() {
    if (!active_runtime() || backend.is_null()) {
        return SteamPurchaseCatalogResult::create(ERR_UNAVAILABLE);
    }
    const auto request = begin_request(Kind::Catalog);
    if (request.is_null()) {
        return SteamPurchaseCatalogResult::create(ERR_BUSY);
    }
    const Ref<SteamPurchases> self(this);
    const auto adapter = backend;
    if (!adapter->GDVIRTUAL_CALL(_catalog, request)) {
        finish_catalog(request.ptr(), SteamPurchaseCatalogResult::create(ERR_UNAVAILABLE));
    }
    return request->catalog_result.is_valid() ? Variant(request->catalog_result)
                                             : Variant(Signal(request.ptr(), "catalog_completed"));
}

Variant SteamPurchases::checkout(const Ref<SteamPurchaseCart> &cart) {
    if (!active_runtime() || backend.is_null()) {
        return SteamPurchaseOutcomeResult::create(ERR_UNAVAILABLE);
    }
    if (cart.is_null() || cart->selections.is_empty()) {
        return SteamPurchaseOutcomeResult::create(ERR_INVALID_PARAMETER);
    }
    if (!cart->ensure_checkout_id()) {
        return SteamPurchaseOutcomeResult::create(ERR_CANT_CREATE);
    }
    const String checkout_id = cart->get_checkout_id();
    const auto request = begin_request(Kind::Begin, {}, checkout_id);
    if (request.is_null()) {
        return SteamPurchaseOutcomeResult::create(ERR_BUSY, {}, checkout_id);
    }
    const Ref<SteamPurchases> self(this);
    const auto adapter = backend;
    if (!adapter->GDVIRTUAL_CALL(_begin, cart, checkout_id, request)) {
        finish_order(request.ptr(), SteamPurchaseOutcomeResult::create(ERR_UNAVAILABLE));
    }
    return request->order_result.is_valid() ? Variant(request->order_result)
                                           : Variant(Signal(request.ptr(), "order_completed"));
}

Variant SteamPurchases::order(const String &order_id) {
    if (!valid_steam_order_id(order_id)) {
        return SteamPurchaseOutcomeResult::create(ERR_INVALID_PARAMETER);
    }
    if (!active_runtime() || backend.is_null()) {
        return SteamPurchaseOutcomeResult::create(ERR_UNAVAILABLE);
    }
    const auto request = begin_request(Kind::Order, order_id);
    if (request.is_null()) {
        return SteamPurchaseOutcomeResult::create(ERR_BUSY);
    }
    const Ref<SteamPurchases> self(this);
    const auto adapter = backend;
    if (!adapter->GDVIRTUAL_CALL(_order, order_id, request)) {
        finish_order(request.ptr(), SteamPurchaseOutcomeResult::create(ERR_UNAVAILABLE));
    }
    return request->order_result.is_valid() ? Variant(request->order_result)
                                           : Variant(Signal(request.ptr(), "order_completed"));
}

Error SteamPurchases::finish_catalog(SteamCommerceRequest *request, const Ref<SteamPurchaseCatalogResult> &result) {
    if (!active_runtime()) {
        return ERR_UNAVAILABLE;
    }
    const auto found = pending.find(request->id);
    if (found == pending.end() || found->second.request.ptr() != request) {
        return ERR_UNAVAILABLE;
    }
    if (found->second.kind != Kind::Catalog || result.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    const auto finished = pending.extract(found);
    const auto &operation = finished.mapped();
    const Error status = Time::get_singleton()->get_ticks_msec() >= operation.deadline
            ? ERR_TIMEOUT
            : result->get_error();
    request->owner = ObjectID();
    request->catalog_result = SteamPurchaseCatalogResult::create(status, result->get_value());
    request->emit_signal("catalog_completed", request->catalog_result);
    return OK;
}

Error SteamPurchases::finish_order(SteamCommerceRequest *request, const Ref<SteamPurchaseOutcomeResult> &result) {
    if (!active_runtime()) {
        return ERR_UNAVAILABLE;
    }
    const auto found = pending.find(request->id);
    if (found == pending.end() || found->second.request.ptr() != request) {
        return ERR_UNAVAILABLE;
    }
    if (found->second.kind == Kind::Catalog || result.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    const auto finished = pending.extract(found);
    const auto &operation = finished.mapped();
    const bool expired = Time::get_singleton()->get_ticks_msec() >= operation.deadline;
    Error status = expired ? ERR_TIMEOUT : result->get_error();
    auto receipt = expired ? Ref<SteamPurchaseOutcome>() : result->get_value();
    if ((status == OK && receipt.is_null()) || (receipt.is_valid() &&
                ((!operation.order_id.is_empty() && receipt->get_order_id() != operation.order_id) ||
                        (operation.kind == Kind::Begin && result->get_checkout_id() != operation.checkout_id)))) {
        status = ERR_INVALID_DATA;
        receipt.unref();
    }
    if (receipt.is_valid()) {
        const auto previous = orders.find(receipt->get_order_id());
        if (previous != orders.end() && (previous->second.receipt->get_currency() != receipt->get_currency() ||
                    previous->second.receipt->get_total() != receipt->get_total())) {
            status = ERR_INVALID_DATA;
            receipt.unref();
        }
    }
    request->owner = ObjectID();
    request->order_result = SteamPurchaseOutcomeResult::create(status, receipt, operation.checkout_id);
    if (request->order_result->get_value().is_valid() && record_order(receipt)) {
        emit_signal("order_changed", receipt);
    }
    request->emit_signal("order_completed", request->order_result);
    if (active_runtime()) {
        const String order_id = request->order_result->get_value().is_valid()
                ? receipt->get_order_id()
                : operation.order_id;
        if (!order_id.is_empty()) {
            forward_hint(order_id);
        }
    }
    return OK;
}

bool SteamPurchases::record_order(const Ref<SteamPurchaseOutcome> &receipt) {
    auto found = orders.find(receipt->get_order_id());
    if (found != orders.end()) {
        return std::exchange(found->second.receipt, receipt)->get_state() != receipt->get_state();
    }
    if (orders.size() >= maximum_orders) {
        const auto discarded = orders.begin();
        hints.erase(discarded->first);
        orders.erase(discarded);
    }
    orders.emplace(receipt->get_order_id(), KnownOrder{receipt});
    return true;
}

void SteamPurchases::on_authorization(const void *data, size_t size) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || !data || size != sizeof(MicroTxnAuthorizationResponse_t)) {
        return;
    }
    MicroTxnAuthorizationResponse_t event{};
    std::memcpy(&event, data, sizeof(event));
    if (event.m_unAppID != active->get_app_id() || event.m_ulOrderID == 0 || event.m_bAuthorized > 1) {
        return;
    }
    const String order_id = String::utf8(std::to_string(event.m_ulOrderID).c_str());
    if (orders.count(order_id)) {
        hints.emplace(order_id, event.m_bAuthorized == 1);
        forward_hint(order_id);
    } else if (hints.size() < maximum_requests) {
        // The overlay can answer before the backend delivers the initial order ID.
        for (const auto &[id, request] : pending) {
            if (request.kind == Kind::Begin) {
                hints.emplace(order_id, event.m_bAuthorized == 1);
                break;
            }
        }
    }
#endif
}

void SteamPurchases::forward_hint(const String &order_id) {
    if (!active_runtime() || backend.is_null()) {
        return;
    }
    const auto known = orders.find(order_id);
    const auto hint = hints.find(order_id);
    if (known == orders.end() || hint == hints.end()) {
        return;
    }
    const auto state = known->second.receipt->get_state();
    if (known->second.forwarded || (state != SteamPurchaseOutcome::AWAITING_APPROVAL &&
                    state != SteamPurchaseOutcome::RECONCILING)) {
        hints.erase(hint);
        return;
    }
    const auto request = begin_request(Kind::Authorization, order_id);
    if (request.is_null()) {
        return;
    }
    const bool authorized = hint->second;
    hints.erase(hint);
    known->second.forwarded = true;
    const auto adapter = backend;
    if (!adapter->GDVIRTUAL_CALL(_observe_authorization, order_id, authorized, request)) {
        finish_order(request.ptr(), SteamPurchaseOutcomeResult::create(ERR_UNAVAILABLE));
    }
}

void SteamPurchases::tick() {
    if (!active_runtime()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::vector<Ref<SteamCommerceRequest>> expired;
    bool waiting_for_order = false;
    for (const auto &[id, operation] : pending) {
        if (now >= operation.deadline) {
            expired.push_back(operation.request);
        } else if (operation.kind == Kind::Begin) {
            waiting_for_order = true;
        }
    }
    for (const auto &request : expired) {
        const auto found = pending.find(request->id);
        if (found != pending.end()) {
            if (found->second.kind == Kind::Catalog) {
                finish_catalog(request.ptr(), SteamPurchaseCatalogResult::create(ERR_TIMEOUT));
            } else {
                finish_order(request.ptr(), SteamPurchaseOutcomeResult::create(ERR_TIMEOUT));
            }
        }
    }
    if (!waiting_for_order) {
        for (auto found = hints.begin(); found != hints.end();) {
            if (!orders.count(found->first)) {
                found = hints.erase(found);
            } else {
                ++found;
            }
        }
    }
    if (!hints.empty() && pending.size() < maximum_requests) {
        std::vector<String> known_hints;
        for (const auto &[order_id, authorized] : hints) {
            if (orders.count(order_id)) {
                known_hints.push_back(order_id);
            }
        }
        for (const auto &order_id : known_hints) {
            forward_hint(order_id);
        }
    }
}

void SteamPurchases::close() {
    const auto active = runtime.lock();
    runtime.reset();
    scope.reset();
#ifdef CRIMSON_WITH_STEAM
    if (active) {
        active->unsubscribe(std::exchange(authorization_subscription, 0));
    }
#endif
    const auto operations = std::exchange(pending, {});
    const auto old_backend = std::exchange(backend, {});
    orders.clear();
    hints.clear();
    for (const auto &[id, operation] : operations) {
        operation.request->owner = ObjectID();
        if (operation.kind == Kind::Catalog) {
            operation.request->catalog_result = SteamPurchaseCatalogResult::create(ERR_SKIP);
            operation.request->emit_signal("catalog_completed", operation.request->catalog_result);
        } else {
            operation.request->order_result = SteamPurchaseOutcomeResult::create(ERR_SKIP, {}, operation.checkout_id);
            operation.request->emit_signal("order_completed", operation.request->order_result);
        }
    }
}

} // namespace crimson::steam
