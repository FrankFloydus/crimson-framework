// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/commerce/steam_purchase_types.hpp"
#include "modules/steam/publisher/steam_publisher.hpp"

#include <godot_cpp/variant/packed_int64_array.hpp>

namespace crimson::steam {

// An immutable, server-priced line persisted before contacting Steam.
class SteamPublisherPurchaseLine : public godot::RefCounted {
    GDCLASS(SteamPublisherPurchaseLine, godot::RefCounted)

public:
    // Create one validated SKU mapping, with total amount for the entire line in Steam currency units.
    static godot::Ref<SteamPublisherPurchaseLine> create(const godot::StringName &sku, int64_t item_id,
            int64_t quantity, int64_t total, const godot::String &description);
    // Return the catalog SKU matched against the unpriced client cart.
    godot::StringName get_sku() const;
    // Return the publisher's uint32 Steam item ID.
    int64_t get_item_id() const;
    // Return the quantity quoted for this line.
    int64_t get_quantity() const;
    // Return the quote total for this line.
    int64_t get_total() const;
    // Return the trusted localized description shown in Steam.
    godot::String get_description() const;

protected:
    // Register immutable priced-line data.
    static void _bind_methods();

private:
    // Game catalog identifier.
    godot::StringName sku;
    // Game-configured positive uint32 item ID.
    int64_t item_id = 0;
    // Positive int16 quantity.
    int64_t quantity = 0;
    // Positive total amount for this quantity.
    int64_t total = 0;
    // Localized trusted line description.
    godot::String description;
};

// Immutable persistence record combining the original quote with the confirmed order progress.
class SteamPublisherPurchaseOrder : public godot::RefCounted {
    GDCLASS(SteamPublisherPurchaseOrder, godot::RefCounted)

public:
    // Defines the Steam receipt amount component the backend's original quote represents.
    enum TaxBasis {
        NET, // Compare each line with Steam's amount excluding VAT.
        GROSS, // Compare each line with Steam's checked amount plus VAT.
    };
    // Durable state used to claim mutations and apply grants/reversals exactly once.
    enum Phase {
        RESERVED, // The quote is persisted and no InitTxn has been submitted.
        INITIALIZING, // The InitTxn claim exists and its outcome may be uncertain.
        AWAITING, // Steam created the transaction and is waiting for authorization.
        APPROVED, // Steam verified authorization for the exact stored order.
        FINALIZING, // A FinalizeTxn claim exists and its outcome may be uncertain.
        DECLINED, // Steam confirmed the order was denied or failed.
        FULFILLED, // Steam confirmed payment and the store committed entitlement.
        REFUNDING, // An authorized refund claim exists and its outcome may be uncertain.
        PARTIALLY_REVERSED, // Confirmed item reversals coexist with remaining entitlements.
        REVERSED, // Steam confirmed the full order reversal.
    };

    // Construct an immutable reservation using prices, language and currency selected by the trusted backend.
    static godot::Ref<SteamPublisherPurchaseOrder> create(int64_t app_id,
            SteamPublisherSettings::Environment environment, const godot::String &order_id,
            const godot::String &steam_id, const godot::String &checkout_id, const godot::String &currency,
            const godot::String &language, const godot::TypedArray<SteamPublisherPurchaseLine> &lines,
            TaxBasis tax_basis = NET);
    // Rehydrate a durable snapshot. The publisher still checks its scope and receipt before granting anything.
    godot::Ref<SteamPublisherPurchaseOrder> with_progress(int64_t revision, Phase phase,
            const godot::String &transaction_id = {}, int64_t vendor_updated_at = 0,
            const godot::PackedInt64Array &reversed_item_ids = {}) const;
    // Return the AppID that owns this order.
    int64_t get_app_id() const;
    // Return the Steam environment that owns this order.
    SteamPublisherSettings::Environment get_environment() const;
    // Return the game-generated order ID as canonical decimal.
    godot::String get_order_id() const;
    // Return the verified payer's SteamID64.
    godot::String get_steam_id() const;
    // Return the account-scoped checkout retry ID.
    godot::String get_checkout_id() const;
    // Return the original quoted currency.
    godot::String get_currency() const;
    // Return the ISO 639-1 language for line descriptions.
    godot::String get_language() const;
    // Return the immutable quoted lines.
    godot::TypedArray<SteamPublisherPurchaseLine> get_lines() const;
    // Return the original combined order amount.
    int64_t get_total() const;
    // Return the pricing basis used for exact Steam receipt comparisons.
    TaxBasis get_tax_basis() const;
    // Return the optimistic concurrency revision, starting at zero.
    int64_t get_revision() const;
    // Return the latest durable phase.
    Phase get_phase() const;
    // Return Steam's transaction ID once acknowledged.
    godot::String get_transaction_id() const;
    // Return the Unix UTC timestamp of the last accepted Steam observation.
    int64_t get_vendor_updated_at() const;
    // Return the Steam item IDs whose entitlements have been reversed.
    godot::PackedInt64Array get_reversed_item_ids() const;

protected:
    // Register order creation, durable rehydration, and read-only data.
    static void _bind_methods();

private:
    // App-scoped environment and ownership fields.
    int64_t app_id = 0;
    // Selects the Valve service where the order was created.
    SteamPublisherSettings::Environment environment = SteamPublisherSettings::LIVE;
    // Publisher's globally unique positive uint64 order ID.
    godot::String order_id;
    // Verified Steam payer.
    godot::String steam_id;
    // Idempotency ID assigned to the originating checkout.
    godot::String checkout_id;
    // Backend's original ISO 4217 currency.
    godot::String currency;
    // Backend's original ISO 639-1 language.
    godot::String language;
    // Immutable server-priced line list.
    godot::TypedArray<SteamPublisherPurchaseLine> lines;
    // Checked sum of all quoted lines.
    int64_t total = 0;
    // Tax component used to compare Steam's reported amounts.
    TaxBasis tax_basis = NET;
    // Monotonic revision for compare-and-commit.
    int64_t revision = 0;
    // Latest durable progress of the order.
    Phase phase = RESERVED;
    // Exact Steam transaction ID when known.
    godot::String transaction_id;
    // Ordering timestamp of the latest accepted vendor snapshot.
    int64_t vendor_updated_at = 0;
    // Exact per-line reversals, accumulated across vendor updates.
    godot::PackedInt64Array reversed_item_ids;
};

// Distinguishes a missing order from a failed durable store read.
class SteamPublisherPurchaseOrderResult : public SteamResult {
    GDCLASS(SteamPublisherPurchaseOrderResult, SteamResult)

public:
    // Report a loaded order, ERR_DOES_NOT_EXIST, or a separate storage error.
    static godot::Ref<SteamPublisherPurchaseOrderResult> create(godot::Error error,
            const godot::Ref<SteamPublisherPurchaseOrder> &value = {});
    // Return the durable order when loaded.
    godot::Ref<SteamPublisherPurchaseOrder> get_value() const;

protected:
    // Register the trusted store result and its typed payload.
    static void _bind_methods();

private:
    // Exact stored order, present on a successful read.
    godot::Ref<SteamPublisherPurchaseOrder> value;
};

// Trusted game adapter for durable prices, order state, refund policy, and entitlement application.
class SteamPurchaseStore : public godot::RefCounted {
    GDCLASS(SteamPurchaseStore, godot::RefCounted)

protected:
    // Register the synchronous trusted persistence hooks.
    static void _bind_methods();
    // Atomically price and reserve a checkout or return its existing exact order. Null rejects the cart.
    GDVIRTUAL5R(godot::Ref<SteamPublisherPurchaseOrder>, _reserve_checkout, int64_t,
            SteamPublisherSettings::Environment, godot::String, godot::Ref<SteamPurchaseCart>, godot::String)
    // Read an order for the exact AppID, environment, and publisher order ID.
    GDVIRTUAL3R(godot::Ref<SteamPublisherPurchaseOrderResult>, _load_order, int64_t,
            SteamPublisherSettings::Environment, godot::String)
    // Atomically commit the next revision and idempotently apply entitlement differences, or reject unchanged.
    GDVIRTUAL2R(bool, _compare_and_commit, int64_t, godot::Ref<SteamPublisherPurchaseOrder>)
    // Approve this exact fulfilled order for an operator-requested refund. Default denies.
    GDVIRTUAL1R(bool, _authorize_refund, godot::Ref<SteamPublisherPurchaseOrder>)

private:
    friend class SteamPublisherPurchases;
    friend struct SteamPublisherPurchaseFlow;
};

// Steam Wallet location and purchasing status for one verified buyer.
class SteamPurchaseBuyerInfo : public godot::RefCounted {
    GDCLASS(SteamPurchaseBuyerInfo, godot::RefCounted)

public:
    // Steam's current purchasing status, with an explicit unknown fallback.
    enum AccountStatus {
        UNKNOWN, // Steam omitted the field or returned an unrecognized value.
        ACTIVE, // Steam reports the default purchasing state.
        TRUSTED, // Steam reports an established purchase history.
        LOCKED, // Steam reports that purchasing is blocked.
    };

    // Return the buyer's two-letter ISO 3166-1 country code.
    godot::String get_country() const;
    // Return the buyer's three-letter ISO 4217 Wallet currency code.
    godot::String get_currency() const;
    // Return the state or region Steam supplied, or an empty string.
    godot::String get_region() const;
    // Return the purchasing status reported by Steam.
    AccountStatus get_account_status() const;

protected:
    // Register the read-only buyer metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherPurchases;

    // Construct a detached snapshot only from bounded country, currency and region fields.
    static godot::Ref<SteamPurchaseBuyerInfo> create(const godot::String &country,
            const godot::String &currency, const godot::String &region, AccountStatus status);
    // Two uppercase letters from the verified Steam response.
    godot::String country;
    // Three uppercase letters from the verified Steam response.
    godot::String currency;
    // Optional state or region supplied by Steam.
    godot::String region;
    // Advisory status reported by Steam, independent of the game's eligibility policy.
    AccountStatus account_status = UNKNOWN;
};

// Status and optional verified buyer metadata from Steam Wallet.
class SteamPurchaseBuyerInfoResult : public SteamResult {
    GDCLASS(SteamPurchaseBuyerInfoResult, SteamResult)

public:
    // Return the detached buyer metadata on success.
    godot::Ref<SteamPurchaseBuyerInfo> get_value() const;

protected:
    // Register the typed buyer metadata payload.
    static void _bind_methods();

private:
    friend class SteamPublisherPurchases;

    // Construct a typed buyer metadata response without exposing vendor diagnostics.
    static godot::Ref<SteamPurchaseBuyerInfoResult> create(godot::Error error,
            const godot::Ref<SteamPurchaseBuyerInfo> &value = {});
    // Exact Steam Wallet metadata when the response succeeded.
    godot::Ref<SteamPurchaseBuyerInfo> value;
};

// Detached result of applying a bounded Steam settlement page.
class SteamPurchaseReconciliation : public godot::RefCounted {
    GDCLASS(SteamPurchaseReconciliation, godot::RefCounted)

public:
    // Return the count of known order observations safely applied or already recorded.
    int64_t get_processed() const;
    // Return the UTC Unix cursor for the next call, or the original cursor on error or empty response.
    int64_t get_next_since_unix() const;

protected:
    // Register the read-only settlement count and cursor.
    static void _bind_methods();

private:
    friend struct SteamPublisherPurchaseFlow;
    // Number of recognized known-order observations processed.
    int64_t processed = 0;
    // Timestamp in seconds, intentionally inclusive so equal-time events are replayed safely.
    int64_t next_since_unix = 0;
};

// Status and optional typed settlement progress.
class SteamPurchaseReconciliationResult : public SteamResult {
    GDCLASS(SteamPurchaseReconciliationResult, SteamResult)

public:
    // Return the bounded report processing result when supplied.
    godot::Ref<SteamPurchaseReconciliation> get_value() const;

protected:
    // Register the typed settlement payload.
    static void _bind_methods();

private:
    friend class SteamPublisherPurchases;
    friend struct SteamPublisherPurchaseFlow;
    // Create the local status and safely committed processing progress.
    static godot::Ref<SteamPurchaseReconciliationResult> create(godot::Error error,
            const godot::Ref<SteamPurchaseReconciliation> &value = {});
    // Detached settlement progress.
    godot::Ref<SteamPurchaseReconciliation> value;
};

// Trusted Steam Wallet service using the game-owned durable purchase store.
class SteamPublisherPurchases : public godot::RefCounted {
    GDCLASS(SteamPublisherPurchases, godot::RefCounted)

public:
    // Maximum and default page size for the bounded settlement endpoint.
    static constexpr int64_t maximum_report_rows = 1000;
    // Fetch Steam Wallet quote metadata for an identity verified by this publisher scope.
    godot::Variant buyer_info(const godot::Ref<SteamIdentity> &player);
    // Reserve the exact verified player's unpriced cart and initiate or resume Steam authorization.
    godot::Variant begin(const godot::Ref<SteamIdentity> &player, const godot::Ref<SteamPurchaseCart> &cart,
            const godot::String &checkout_id);
    // Observe and persist Steam's current transaction state for the known order.
    godot::Variant query(const godot::String &order_id);
    // Verify authorization, claim and finalize one known order, or reconcile an outstanding claim.
    godot::Variant finalize(const godot::String &order_id);
    // Apply a bounded settlement page for known orders from the supplied UTC Unix cursor.
    godot::Variant reconcile(int64_t since_unix, int64_t max_results = maximum_report_rows);
    // Submit an explicitly policy-approved full refund for a known fulfilled order.
    godot::Variant refund(const godot::String &order_id);

protected:
    // Register directly awaited Steam purchase actions.
    static void _bind_methods();

private:
    friend class SteamPublisher;
    // Select what may follow a verified transaction read.
    enum class Action { QUERY, FINALIZE, REFUND };
    // Read a stored order and begin its verified query, finalization or refund.
    godot::Variant observe(const godot::String &order_id, Action action);
    // Weak exact publisher issuer prevents retained purchases from extending its lifetime.
    std::weak_ptr<SteamPublisherState> scope;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamPublisherPurchaseOrder::TaxBasis);
VARIANT_ENUM_CAST(crimson::steam::SteamPublisherPurchaseOrder::Phase);
VARIANT_ENUM_CAST(crimson::steam::SteamPurchaseBuyerInfo::AccountStatus);
