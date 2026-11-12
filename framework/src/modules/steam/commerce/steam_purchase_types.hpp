// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>

namespace crimson::steam {

// Validate the canonical decimal form of a nonzero 64-bit Steam order ID.
bool valid_steam_order_id(const godot::String &value);
// Validate a checkout token containing 16 bytes encoded as exactly 32 lowercase hexadecimal characters.
bool valid_steam_checkout_id(const godot::String &value);

// Immutable selection of a game SKU and a positive quantity.
class SteamPurchaseLine : public godot::RefCounted {
    GDCLASS(SteamPurchaseLine, godot::RefCounted)

public:
    // Return the game-owned catalog identifier.
    godot::StringName get_sku() const;
    // Return the number of units requested.
    int64_t get_quantity() const;

protected:
    // Register the immutable cart selection.
    static void _bind_methods();

private:
    friend class SteamPurchaseCart;

    // Catalog identifier provided by the game.
    godot::StringName sku;
    // Number of units, bounded by the cart.
    int64_t quantity = 0;
};

// Holds a branchable selection of SKUs without prices or identities.
class SteamPurchaseCart : public godot::RefCounted {
    GDCLASS(SteamPurchaseCart, godot::RefCounted)

public:
    // Return a new cart with the selected quantity for this SKU.
    godot::Ref<SteamPurchaseCart> with_item(const godot::StringName &sku, int64_t quantity) const;
    // Return an exact cart branch with an earlier checkout's stable retry ID.
    godot::Ref<SteamPurchaseCart> with_checkout_id(const godot::String &checkout_id) const;
    // Return a read-only list of item selections.
    godot::TypedArray<SteamPurchaseLine> lines() const;
    // Return the assigned retry ID, or empty before the first checkout.
    godot::String get_checkout_id() const;

protected:
    // Register the branchable cart and checkout ID.
    static void _bind_methods();

private:
    friend class SteamPurchases;

    // Allocate a secure retry ID once for the exact submitted cart.
    bool ensure_checkout_id();

    // Copy-on-branch cart selections without backend price data.
    godot::TypedArray<SteamPurchaseLine> selections;
    // Stable checkout ID used to recover an ambiguous submission.
    godot::String checkout_id;
};

// One Steam purchase SKU and its current backend-supplied display price.
class SteamPurchaseItem : public godot::RefCounted {
    GDCLASS(SteamPurchaseItem, godot::RefCounted)

public:
    // Build a validated catalog row from backend price data.
    static godot::Ref<SteamPurchaseItem> create(const godot::StringName &sku, const godot::String &display_name,
            int64_t amount, const godot::String &currency);
    // Return the server's SKU.
    godot::StringName get_sku() const;
    // Return the localized display name supplied by the backend.
    godot::String get_display_name() const;
    // Return the amount in Steam's units for this currency.
    int64_t get_amount() const;
    // Return the three-letter uppercase currency code.
    godot::String get_currency() const;

protected:
    // Register the immutable price and presentation fields.
    static void _bind_methods();

private:
    // Game-owned catalog identifier.
    godot::StringName sku;
    // Game-owned localized item name.
    godot::String display_name;
    // Display price in the backend's Steam currency units.
    int64_t amount = 0;
    // Currency code matching the displayed amount.
    godot::String currency;
};

// Immutable list of the authenticated player's purchasable SKUs.
class SteamPurchaseCatalog : public godot::RefCounted {
    GDCLASS(SteamPurchaseCatalog, godot::RefCounted)

public:
    // Start with an empty, read-only catalog.
    SteamPurchaseCatalog();
    // Snapshot up to 256 unique, valid catalog rows.
    static godot::Ref<SteamPurchaseCatalog> create(const godot::TypedArray<SteamPurchaseItem> &entries);
    // Return read-only backend catalog rows.
    godot::TypedArray<SteamPurchaseItem> get_entries() const;

protected:
    // Register the immutable catalog.
    static void _bind_methods();

private:
    // The backend's detached list of display offers.
    godot::TypedArray<SteamPurchaseItem> entries;
};

// Backend-confirmed order state. Only the backend can confirm fulfillment.
class SteamPurchaseOutcome : public godot::RefCounted {
    GDCLASS(SteamPurchaseOutcome, godot::RefCounted)

public:
    // Order states reported by the authenticated commerce backend.
    enum State {
        AWAITING_APPROVAL, // Steam is waiting for authorization.
        DECLINED, // The order was declined.
        RECONCILING, // The backend is confirming an uncertain outcome.
        FULFILLED, // The backend confirms the durable grant.
        REVERSED, // The backend confirms the grant was reversed.
        PARTIALLY_REVERSED, // The backend confirms that only some purchased grants were reversed.
    };

    // Create a validated receipt from the backend's current order state.
    static godot::Ref<SteamPurchaseOutcome> create(const godot::String &order_id,
            const godot::String &currency, int64_t total, State state);
    // Return the canonical decimal order ID.
    godot::String get_order_id() const;
    // Return the backend's currency code.
    godot::String get_currency() const;
    // Return the order total in Steam units for the currency.
    int64_t get_total() const;
    // Return the observed order state.
    State get_state() const;

protected:
    // Register the receipt properties and order states.
    static void _bind_methods();

private:
    // Backend-issued unsigned decimal order ID.
    godot::String order_id;
    // Currency of the order.
    godot::String currency;
    // Server-selected price total.
    int64_t total = 0;
    // Backend's observed transaction and fulfillment state.
    State state = RECONCILING;
};

// Outcome of reading the authenticated player's catalog.
class SteamPurchaseCatalogResult : public SteamResult {
    GDCLASS(SteamPurchaseCatalogResult, SteamResult)

public:
    // Settle a catalog read with a typed payload on success.
    static godot::Ref<SteamPurchaseCatalogResult> create(godot::Error error,
            const godot::Ref<SteamPurchaseCatalog> &value = {});
    // Return the accepted catalog on success.
    godot::Ref<SteamPurchaseCatalog> get_value() const;

protected:
    // Register catalog result construction and read-only data.
    static void _bind_methods();

private:
    // Current offers copied from the game backend.
    godot::Ref<SteamPurchaseCatalog> value;
};

// Order outcome or a recoverable checkout attempt with a stable retry ID.
class SteamPurchaseOutcomeResult : public SteamResult {
    GDCLASS(SteamPurchaseOutcomeResult, SteamResult)

public:
    // Settle an order action with a receipt and optional stable checkout ID.
    static godot::Ref<SteamPurchaseOutcomeResult> create(godot::Error error,
            const godot::Ref<SteamPurchaseOutcome> &value = {}, const godot::String &checkout_id = {});
    // Return the confirmed receipt or a recoverable reconciliation snapshot.
    godot::Ref<SteamPurchaseOutcome> get_value() const;
    // Return the checkout retry ID when this result belongs to a submitted cart.
    godot::String get_checkout_id() const;

protected:
    // Register outcome construction, receipt, and retry ID.
    static void _bind_methods();

private:
    // Confirmed backend order state when available.
    godot::Ref<SteamPurchaseOutcome> value;
    // Retry ID retained even if the receipt never arrived.
    godot::String checkout_id;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamPurchaseOutcome::State);
