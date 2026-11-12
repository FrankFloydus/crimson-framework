// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/commerce/steam_purchase_types.hpp"

#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <map>
#include <memory>

namespace crimson::integration {
class SteamProvider;
}

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Single-use typed completion supplied to a game-owned commerce adapter.
class SteamCommerceRequest : public godot::RefCounted {
    GDCLASS(SteamCommerceRequest, godot::RefCounted)

public:
    // Resolve a pending catalog request with the backend's confirmed response.
    godot::Error complete_catalog(const godot::Ref<SteamPurchaseCatalogResult> &result);
    // Resolve a pending begin, refresh, or authorization request.
    godot::Error complete_order(const godot::Ref<SteamPurchaseOutcomeResult> &result);

protected:
    // Register exact completion methods and typed signals.
    static void _bind_methods();

private:
    friend class SteamPurchases;

    // Exact issuing facade, cleared before notifying observers.
    godot::ObjectID owner;
    // Monotonic identifier in the issuing facade.
    uint64_t id = 0;
    // The catalog result when this was a catalog request.
    godot::Ref<SteamPurchaseCatalogResult> catalog_result;
    // The order receipt when this was an order request.
    godot::Ref<SteamPurchaseOutcomeResult> order_result;
};

// Scriptable connection to the game's authenticated purchasing backend.
class SteamCommerceBackend : public godot::RefCounted {
    GDCLASS(SteamCommerceBackend, godot::RefCounted)

protected:
    // Fetch the current authenticated account's purchasable catalog.
    GDVIRTUAL1(_catalog, godot::Ref<SteamCommerceRequest>)
    // Create or resume one server order using the cart and stable checkout ID.
    GDVIRTUAL3(_begin, godot::Ref<SteamPurchaseCart>, godot::String, godot::Ref<SteamCommerceRequest>)
    // Ask the backend to check a Steam authorization hint against the known order.
    GDVIRTUAL3(_observe_authorization, godot::String, bool, godot::Ref<SteamCommerceRequest>)
    // Fetch a known order for the currently authenticated account.
    GDVIRTUAL2(_order, godot::String, godot::Ref<SteamCommerceRequest>)

    // Register the typed backend override hooks.
    static void _bind_methods();

private:
    friend class SteamPurchases;
};

// Checkout and recoverable order state for one Steam client with a bound backend.
class SteamPurchases : public godot::RefCounted {
    GDCLASS(SteamPurchases, godot::RefCounted)

public:
    // Await the authenticated backend's catalog and localized prices.
    godot::Variant catalog();
    // Await a receipt or recoverable error for this cart's stable checkout ID.
    godot::Variant checkout(const godot::Ref<SteamPurchaseCart> &cart);
    // Await a refreshed backend receipt for the current account's order.
    godot::Variant order(const godot::String &order_id);

protected:
    // Register commerce actions and backend-confirmed state changes.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamCommerceRequest;
    friend class crimson::integration::SteamProvider;

    // Supported game backend operations.
    enum class Kind { Catalog, Begin, Order, Authorization };

    // Outstanding game backend request and its correlation parameters.
    struct Pending {
        // Typed observer held until the backend completes or the scope closes.
        godot::Ref<SteamCommerceRequest> request;
        // The exact backend action this request completes.
        Kind kind;
        // Expected order ID for an order refresh or an authorization hint.
        godot::String order_id;
        // Stable checkout ID for a begin request.
        godot::String checkout_id;
        // Monotonic local observation deadline.
        uint64_t deadline = 0;
    };

    // Store the latest backend-confirmed state and whether Steam's hint was forwarded.
    struct KnownOrder {
        // Latest backend receipt for this exact order.
        godot::Ref<SteamPurchaseOutcome> receipt;
        // Whether this scope already forwarded an authorization hint.
        bool forwarded = false;
    };

    // Attach to one client start. The standalone service begins without a backend.
    void configure(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &lifetime);
    // Bind the exact Game attachment's authenticated commerce adapter.
    void bind_backend(const godot::Ref<SteamCommerceBackend> &adapter);
    // Cancel local observations without changing durable backend orders.
    void close();
    // Expire the local observers whose backend did not return in time.
    void tick();
    // Resolve Steam only while the configured exact scope remains live on the main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Create and track one typed request before invoking script.
    godot::Ref<SteamCommerceRequest> begin_request(Kind kind, const godot::String &order_id = {},
            const godot::String &checkout_id = {});
    // Validate and settle a typed catalog response from the exact request.
    godot::Error finish_catalog(SteamCommerceRequest *request, const godot::Ref<SteamPurchaseCatalogResult> &result);
    // Validate and settle a typed order response from the exact request.
    godot::Error finish_order(SteamCommerceRequest *request, const godot::Ref<SteamPurchaseOutcomeResult> &result);
    // Record one confirmed order state and report whether the state changed.
    bool record_order(const godot::Ref<SteamPurchaseOutcome> &receipt);
    // Resolve a copied Steam hint against the current AppID and known orders.
    void on_authorization(const void *data, size_t size);
    // Forward a pending native hint when the order is known and no refresh is in progress.
    void forward_hint(const godot::String &order_id);

    // Steam runtime observed without retaining the process.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact generation that owns the backend binding.
    std::weak_ptr<SteamClientScope> scope;
    // Game-supplied authenticated adapter, absent in standalone startup.
    godot::Ref<SteamCommerceBackend> backend;
    // Native subscription for microtransaction authorization.
    uint64_t authorization_subscription = 0;
    // Next local correlation identifier.
    uint64_t next_request = 1;
    // In-flight game backend requests.
    std::map<uint64_t, Pending> pending;
    // Bounded locally observed order states.
    std::map<godot::String, KnownOrder> orders;
    // Bounded hints waiting for an in-flight begin or order refresh.
    std::map<godot::String, bool> hints;
};

} // namespace crimson::steam
