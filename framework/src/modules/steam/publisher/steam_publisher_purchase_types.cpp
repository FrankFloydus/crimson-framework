// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_purchases.hpp"
#include "modules/steam/publisher/steam_publisher_codec.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <limits>
#include <set>

namespace crimson::steam {
using namespace godot;

namespace {

// Publish immutable DTO fields to Godot Help and Inspector.
constexpr auto read_only = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY;

// Preserve the enum's documented Godot type on a read-only snapshot field.
template <typename Enum>
PropertyInfo enum_property(const char *name) {
    PropertyInfo property = GetTypeInfo<Enum>::get_class_info();
    property.name = name;
    property.usage |= PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_EDITOR;
    return property;
}

// Limit stable identifiers and ensure they survive the exact persisted round trip.
bool valid_text(const String &value, int64_t maximum) {
    if (value.is_empty() || value.length() > maximum || value.strip_edges() != value) {
        return false;
    }
    for (int64_t index = 0; index < value.length(); ++index) {
        if (value[index] < 32 || value[index] == 127) {
            return false;
        }
    }
    return true;
}

// Validate one canonical positive decimal ID and optionally a public individual Steam account.
bool valid_id(const String &value, bool individual = false) {
    if (value.is_empty() || value.length() > 20) {
        return false;
    }
    const auto text = value.utf8();
    uint64_t parsed = 0;
    return publisher_codec::unsigned_value(publisher_codec::Json(text.get_data()), parsed) && parsed > 0 &&
            (!individual || publisher_codec::individual_id(parsed));
}

// Restrict the persisted quote to exactly scoped ISO letter codes.
bool valid_code(const String &value, int64_t length, char32_t first, char32_t last) {
    if (value.length() != length) {
        return false;
    }
    for (int64_t index = 0; index < length; ++index) {
        if (value[index] < first || value[index] > last) {
            return false;
        }
    }
    return true;
}

} // namespace

void SteamPublisherPurchaseLine::_bind_methods() {
    ClassDB::bind_static_method("SteamPublisherPurchaseLine",
            D_METHOD("create", "sku", "item_id", "quantity", "total", "description"), &SteamPublisherPurchaseLine::create);
    ClassDB::bind_method(D_METHOD("get_sku"), &SteamPublisherPurchaseLine::get_sku);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sku", PROPERTY_HINT_NONE, "", read_only), "", "get_sku");
    ClassDB::bind_method(D_METHOD("get_item_id"), &SteamPublisherPurchaseLine::get_item_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "item_id", PROPERTY_HINT_NONE, "", read_only), "", "get_item_id");
    ClassDB::bind_method(D_METHOD("get_quantity"), &SteamPublisherPurchaseLine::get_quantity);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "quantity", PROPERTY_HINT_NONE, "", read_only), "", "get_quantity");
    ClassDB::bind_method(D_METHOD("get_total"), &SteamPublisherPurchaseLine::get_total);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total", PROPERTY_HINT_NONE, "", read_only), "", "get_total");
    ClassDB::bind_method(D_METHOD("get_description"), &SteamPublisherPurchaseLine::get_description);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "description", PROPERTY_HINT_NONE, "", read_only), "", "get_description");
}

Ref<SteamPublisherPurchaseLine> SteamPublisherPurchaseLine::create(const StringName &sku, int64_t item_id,
        int64_t quantity, int64_t total, const String &description) {
    if (!valid_text(String(sku), 96) || item_id <= 0 || item_id > UINT32_MAX || quantity <= 0 ||
            quantity > INT16_MAX || total <= 0 || !valid_text(description, 128)) {
        return {};
    }
    Ref<SteamPublisherPurchaseLine> line;
    line.instantiate();
    line->sku = sku;
    line->item_id = item_id;
    line->quantity = quantity;
    line->total = total;
    line->description = description;
    return line;
}

StringName SteamPublisherPurchaseLine::get_sku() const {
    return sku;
}

int64_t SteamPublisherPurchaseLine::get_item_id() const {
    return item_id;
}

int64_t SteamPublisherPurchaseLine::get_quantity() const {
    return quantity;
}

int64_t SteamPublisherPurchaseLine::get_total() const {
    return total;
}

String SteamPublisherPurchaseLine::get_description() const {
    return description;
}

void SteamPublisherPurchaseOrder::_bind_methods() {
    ClassDB::bind_static_method("SteamPublisherPurchaseOrder", D_METHOD("create", "app_id", "environment", "order_id",
            "steam_id", "checkout_id", "currency", "language", "lines", "tax_basis"), &SteamPublisherPurchaseOrder::create,
            DEFVAL(NET));
    ClassDB::bind_method(D_METHOD("with_progress", "revision", "phase", "transaction_id", "vendor_updated_at",
            "reversed_item_ids"), &SteamPublisherPurchaseOrder::with_progress, DEFVAL(String()), DEFVAL(0),
            DEFVAL(PackedInt64Array()));
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamPublisherPurchaseOrder::get_app_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
    ClassDB::bind_method(D_METHOD("get_environment"), &SteamPublisherPurchaseOrder::get_environment);
    ADD_PROPERTY(enum_property<SteamPublisherSettings::Environment>("environment"), "", "get_environment");
    ClassDB::bind_method(D_METHOD("get_order_id"), &SteamPublisherPurchaseOrder::get_order_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "order_id", PROPERTY_HINT_NONE, "", read_only), "", "get_order_id");
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamPublisherPurchaseOrder::get_steam_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_steam_id");
    ClassDB::bind_method(D_METHOD("get_checkout_id"), &SteamPublisherPurchaseOrder::get_checkout_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "checkout_id", PROPERTY_HINT_NONE, "", read_only), "", "get_checkout_id");
    ClassDB::bind_method(D_METHOD("get_currency"), &SteamPublisherPurchaseOrder::get_currency);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "currency", PROPERTY_HINT_NONE, "", read_only), "", "get_currency");
    ClassDB::bind_method(D_METHOD("get_language"), &SteamPublisherPurchaseOrder::get_language);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "language", PROPERTY_HINT_NONE, "", read_only), "", "get_language");
    ClassDB::bind_method(D_METHOD("get_lines"), &SteamPublisherPurchaseOrder::get_lines);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "lines", PROPERTY_HINT_ARRAY_TYPE, SteamPublisherPurchaseLine::get_class_static(), read_only), "", "get_lines");
    ClassDB::bind_method(D_METHOD("get_total"), &SteamPublisherPurchaseOrder::get_total);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total", PROPERTY_HINT_NONE, "", read_only), "", "get_total");
    ClassDB::bind_method(D_METHOD("get_tax_basis"), &SteamPublisherPurchaseOrder::get_tax_basis);
    ADD_PROPERTY(enum_property<SteamPublisherPurchaseOrder::TaxBasis>("tax_basis"), "", "get_tax_basis");
    ClassDB::bind_method(D_METHOD("get_revision"), &SteamPublisherPurchaseOrder::get_revision);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "revision", PROPERTY_HINT_NONE, "", read_only), "", "get_revision");
    ClassDB::bind_method(D_METHOD("get_phase"), &SteamPublisherPurchaseOrder::get_phase);
    ADD_PROPERTY(enum_property<SteamPublisherPurchaseOrder::Phase>("phase"), "", "get_phase");
    ClassDB::bind_method(D_METHOD("get_transaction_id"), &SteamPublisherPurchaseOrder::get_transaction_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "transaction_id", PROPERTY_HINT_NONE, "", read_only), "", "get_transaction_id");
    ClassDB::bind_method(D_METHOD("get_vendor_updated_at"), &SteamPublisherPurchaseOrder::get_vendor_updated_at);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "vendor_updated_at", PROPERTY_HINT_NONE, "", read_only), "", "get_vendor_updated_at");
    ClassDB::bind_method(D_METHOD("get_reversed_item_ids"), &SteamPublisherPurchaseOrder::get_reversed_item_ids);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT64_ARRAY, "reversed_item_ids", PROPERTY_HINT_NONE, "", read_only), "", "get_reversed_item_ids");
    BIND_ENUM_CONSTANT(NET);
    BIND_ENUM_CONSTANT(GROSS);
    BIND_ENUM_CONSTANT(RESERVED);
    BIND_ENUM_CONSTANT(INITIALIZING);
    BIND_ENUM_CONSTANT(AWAITING);
    BIND_ENUM_CONSTANT(APPROVED);
    BIND_ENUM_CONSTANT(FINALIZING);
    BIND_ENUM_CONSTANT(DECLINED);
    BIND_ENUM_CONSTANT(FULFILLED);
    BIND_ENUM_CONSTANT(REFUNDING);
    BIND_ENUM_CONSTANT(PARTIALLY_REVERSED);
    BIND_ENUM_CONSTANT(REVERSED);
}

Ref<SteamPublisherPurchaseOrder> SteamPublisherPurchaseOrder::create(int64_t app_id,
        SteamPublisherSettings::Environment environment, const String &order_id, const String &steam_id,
        const String &checkout_id, const String &currency, const String &language,
        const TypedArray<SteamPublisherPurchaseLine> &lines, TaxBasis tax_basis) {
    if (app_id <= 0 || app_id > UINT32_MAX || (environment != SteamPublisherSettings::LIVE &&
            environment != SteamPublisherSettings::SANDBOX) || !valid_id(order_id) || !valid_id(steam_id, true) ||
            !valid_steam_checkout_id(checkout_id) || !valid_code(currency, 3, 'A', 'Z') || !valid_code(language, 2, 'a', 'z') ||
            lines.is_empty() || lines.size() > 32 || (tax_basis != NET && tax_basis != GROSS)) {
        return {};
    }
    int64_t total = 0;
    std::set<int64_t> ids;
    std::set<StringName> skus;
    for (int64_t index = 0; index < lines.size(); ++index) {
        const Ref<SteamPublisherPurchaseLine> line = lines[index];
        if (line.is_null() || line->get_item_id() <= 0 || line->get_total() <= 0 ||
                !ids.insert(line->get_item_id()).second || !skus.insert(line->get_sku()).second ||
                line->get_total() > (std::numeric_limits<int64_t>::max)() - total) {
            return {};
        }
        total += line->get_total();
    }
    Ref<SteamPublisherPurchaseOrder> quote;
    quote.instantiate();
    quote->app_id = app_id;
    quote->environment = environment;
    quote->order_id = order_id;
    quote->steam_id = steam_id;
    quote->checkout_id = checkout_id;
    quote->currency = currency;
    quote->language = language;
    quote->lines = lines.duplicate();
    quote->lines.make_read_only();
    quote->total = total;
    quote->tax_basis = tax_basis;
    return quote;
}

Ref<SteamPublisherPurchaseOrder> SteamPublisherPurchaseOrder::with_progress(int64_t revision, Phase phase,
        const String &transaction_id, int64_t vendor_updated_at, const PackedInt64Array &reversed_item_ids) const {
    if (app_id <= 0 || revision < 0 || revision == (std::numeric_limits<int64_t>::max)() ||
            phase < RESERVED || phase > REVERSED || vendor_updated_at < 0 ||
            (!transaction_id.is_empty() && !valid_id(transaction_id)) ||
            (phase <= INITIALIZING && (!transaction_id.is_empty() || vendor_updated_at != 0)) ||
            (phase == DECLINED && transaction_id.is_empty() && vendor_updated_at != 0) ||
            (phase >= AWAITING && phase != DECLINED && transaction_id.is_empty()) ||
            (phase == PARTIALLY_REVERSED && (reversed_item_ids.is_empty() || reversed_item_ids.size() >= lines.size())) ||
            (phase == REVERSED && reversed_item_ids.size() != lines.size()) ||
            (phase != PARTIALLY_REVERSED && phase != REVERSED && !reversed_item_ids.is_empty())) {
        return {};
    }
    std::set<int64_t> expected;
    for (int64_t index = 0; index < lines.size(); ++index) {
        const Ref<SteamPublisherPurchaseLine> line = lines[index];
        expected.insert(line->get_item_id());
    }
    for (int64_t index = 0; index < reversed_item_ids.size(); ++index) {
        if (expected.erase(reversed_item_ids[index]) == 0) {
            return {};
        }
    }
    Ref<SteamPublisherPurchaseOrder> snapshot;
    snapshot.instantiate();
    snapshot->app_id = app_id;
    snapshot->environment = environment;
    snapshot->order_id = order_id;
    snapshot->steam_id = steam_id;
    snapshot->checkout_id = checkout_id;
    snapshot->currency = currency;
    snapshot->language = language;
    snapshot->lines = lines;
    snapshot->total = total;
    snapshot->tax_basis = tax_basis;
    snapshot->revision = revision;
    snapshot->phase = phase;
    snapshot->transaction_id = transaction_id;
    snapshot->vendor_updated_at = vendor_updated_at;
    snapshot->reversed_item_ids = reversed_item_ids;
    return snapshot;
}

int64_t SteamPublisherPurchaseOrder::get_app_id() const {
    return app_id;
}

SteamPublisherSettings::Environment SteamPublisherPurchaseOrder::get_environment() const {
    return environment;
}

String SteamPublisherPurchaseOrder::get_order_id() const {
    return order_id;
}

String SteamPublisherPurchaseOrder::get_steam_id() const {
    return steam_id;
}

String SteamPublisherPurchaseOrder::get_checkout_id() const {
    return checkout_id;
}

String SteamPublisherPurchaseOrder::get_currency() const {
    return currency;
}

String SteamPublisherPurchaseOrder::get_language() const {
    return language;
}

TypedArray<SteamPublisherPurchaseLine> SteamPublisherPurchaseOrder::get_lines() const {
    return lines;
}

int64_t SteamPublisherPurchaseOrder::get_total() const {
    return total;
}

SteamPublisherPurchaseOrder::TaxBasis SteamPublisherPurchaseOrder::get_tax_basis() const {
    return tax_basis;
}

int64_t SteamPublisherPurchaseOrder::get_revision() const {
    return revision;
}

SteamPublisherPurchaseOrder::Phase SteamPublisherPurchaseOrder::get_phase() const {
    return phase;
}

String SteamPublisherPurchaseOrder::get_transaction_id() const {
    return transaction_id;
}

int64_t SteamPublisherPurchaseOrder::get_vendor_updated_at() const {
    return vendor_updated_at;
}

PackedInt64Array SteamPublisherPurchaseOrder::get_reversed_item_ids() const {
    return reversed_item_ids;
}

void SteamPublisherPurchaseOrderResult::_bind_methods() {
    ClassDB::bind_static_method("SteamPublisherPurchaseOrderResult", D_METHOD("create", "error", "value"),
            &SteamPublisherPurchaseOrderResult::create, DEFVAL(Ref<SteamPublisherPurchaseOrder>()));
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPublisherPurchaseOrderResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
            SteamPublisherPurchaseOrder::get_class_static(), read_only), "", "get_value");
}

Ref<SteamPublisherPurchaseOrder> SteamPublisherPurchaseOrderResult::get_value() const {
    return value;
}

Ref<SteamPublisherPurchaseOrderResult> SteamPublisherPurchaseOrderResult::create(Error error,
        const Ref<SteamPublisherPurchaseOrder> &value) {
    Ref<SteamPublisherPurchaseOrderResult> result;
    result.instantiate();
    if (error == OK && value.is_null()) {
        error = ERR_INVALID_DATA;
    }
    result->set_outcome(error);
    if (error == OK) {
        result->value = value;
    }
    return result;
}

void SteamPurchaseStore::_bind_methods() {
    GDVIRTUAL_BIND(_reserve_checkout, "app_id", "environment", "steam_id", "cart", "checkout_id");
    GDVIRTUAL_BIND(_load_order, "app_id", "environment", "order_id");
    GDVIRTUAL_BIND(_compare_and_commit, "expected_revision", "next");
    GDVIRTUAL_BIND(_authorize_refund, "order");
}

void SteamPurchaseBuyerInfo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_country"), &SteamPurchaseBuyerInfo::get_country);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "country", PROPERTY_HINT_NONE, "", read_only), "", "get_country");
    ClassDB::bind_method(D_METHOD("get_currency"), &SteamPurchaseBuyerInfo::get_currency);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "currency", PROPERTY_HINT_NONE, "", read_only), "", "get_currency");
    ClassDB::bind_method(D_METHOD("get_region"), &SteamPurchaseBuyerInfo::get_region);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "region", PROPERTY_HINT_NONE, "", read_only), "", "get_region");
    ClassDB::bind_method(D_METHOD("get_account_status"), &SteamPurchaseBuyerInfo::get_account_status);
    ADD_PROPERTY(enum_property<SteamPurchaseBuyerInfo::AccountStatus>("account_status"), "", "get_account_status");
    BIND_ENUM_CONSTANT(UNKNOWN);
    BIND_ENUM_CONSTANT(ACTIVE);
    BIND_ENUM_CONSTANT(TRUSTED);
    BIND_ENUM_CONSTANT(LOCKED);
}

Ref<SteamPurchaseBuyerInfo> SteamPurchaseBuyerInfo::create(const String &country, const String &currency,
        const String &region, AccountStatus status) {
    if (!valid_code(country, 2, 'A', 'Z') || !valid_code(currency, 3, 'A', 'Z') ||
            (!region.is_empty() && !valid_text(region, 64))) {
        return {};
    }
    Ref<SteamPurchaseBuyerInfo> result;
    result.instantiate();
    result->country = country;
    result->currency = currency;
    result->region = region;
    result->account_status = status;
    return result;
}

String SteamPurchaseBuyerInfo::get_country() const {
    return country;
}

String SteamPurchaseBuyerInfo::get_currency() const {
    return currency;
}

String SteamPurchaseBuyerInfo::get_region() const {
    return region;
}

SteamPurchaseBuyerInfo::AccountStatus SteamPurchaseBuyerInfo::get_account_status() const {
    return account_status;
}

void SteamPurchaseBuyerInfoResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPurchaseBuyerInfoResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
            SteamPurchaseBuyerInfo::get_class_static(), read_only), "", "get_value");
}

Ref<SteamPurchaseBuyerInfo> SteamPurchaseBuyerInfoResult::get_value() const {
    return value;
}

Ref<SteamPurchaseBuyerInfoResult> SteamPurchaseBuyerInfoResult::create(Error error,
        const Ref<SteamPurchaseBuyerInfo> &value) {
    Ref<SteamPurchaseBuyerInfoResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamPurchaseReconciliation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_processed"), &SteamPurchaseReconciliation::get_processed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "processed", PROPERTY_HINT_NONE, "", read_only), "", "get_processed");
    ClassDB::bind_method(D_METHOD("get_next_since_unix"), &SteamPurchaseReconciliation::get_next_since_unix);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "next_since_unix", PROPERTY_HINT_NONE, "", read_only), "", "get_next_since_unix");
}

int64_t SteamPurchaseReconciliation::get_processed() const {
    return processed;
}

int64_t SteamPurchaseReconciliation::get_next_since_unix() const {
    return next_since_unix;
}

void SteamPurchaseReconciliationResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPurchaseReconciliationResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
            SteamPurchaseReconciliation::get_class_static(), read_only), "", "get_value");
}

Ref<SteamPurchaseReconciliation> SteamPurchaseReconciliationResult::get_value() const {
    return value;
}

Ref<SteamPurchaseReconciliationResult> SteamPurchaseReconciliationResult::create(Error error,
        const Ref<SteamPurchaseReconciliation> &value) {
    Ref<SteamPurchaseReconciliationResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

} // namespace crimson::steam
