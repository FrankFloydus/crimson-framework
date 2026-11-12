// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/commerce/steam_purchase_types.hpp"

#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <charconv>
#include <set>

namespace crimson::steam {
using namespace godot;

namespace {

// Check game SKU identifiers before they are sent to a commerce adapter.
bool valid_sku(const StringName &sku) {
    const CharString bytes = String(sku).utf8();
    if (bytes.length() == 0 || bytes.length() > 64) {
        return false;
    }
    for (int64_t index = 0; index < bytes.length(); ++index) {
        const char value = bytes[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.')) {
            return false;
        }
    }
    return true;
}

// Steam currency codes carry three uppercase ASCII letters.
bool valid_currency(const String &currency) {
    return currency.length() == 3 && currency[0] >= 'A' && currency[0] <= 'Z' &&
            currency[1] >= 'A' && currency[1] <= 'Z' && currency[2] >= 'A' && currency[2] <= 'Z';
}

} // namespace

bool valid_steam_checkout_id(const String &id) {
    if (id.length() != 32) {
        return false;
    }
    for (int64_t index = 0; index < id.length(); ++index) {
        const auto character = id[index];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool valid_steam_order_id(const String &value) {
    const CharString bytes = value.utf8();
    if (bytes.length() == 0 || bytes.length() > 20 || bytes[0] == '0') {
        return false;
    }
    uint64_t parsed = 0;
    const char *end = bytes.get_data() + bytes.length();
    const auto result = std::from_chars(bytes.get_data(), end, parsed);
    return result.ec == std::errc() && result.ptr == end && parsed != 0;
}

void SteamPurchaseLine::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_sku"), &SteamPurchaseLine::get_sku);
    ClassDB::bind_method(D_METHOD("get_quantity"), &SteamPurchaseLine::get_quantity);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sku", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_sku");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "quantity", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_quantity");
}

StringName SteamPurchaseLine::get_sku() const { return sku; }
int64_t SteamPurchaseLine::get_quantity() const { return quantity; }

void SteamPurchaseCart::_bind_methods() {
    ClassDB::bind_method(D_METHOD("with_item", "sku", "quantity"), &SteamPurchaseCart::with_item);
    ClassDB::bind_method(D_METHOD("with_checkout_id", "checkout_id"), &SteamPurchaseCart::with_checkout_id);
    ClassDB::bind_method(D_METHOD("lines"), &SteamPurchaseCart::lines);
    ClassDB::bind_method(D_METHOD("get_checkout_id"), &SteamPurchaseCart::get_checkout_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "checkout_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_checkout_id");
}

Ref<SteamPurchaseCart> SteamPurchaseCart::with_item(const StringName &sku, int64_t quantity) const {
    if (!valid_sku(sku) || quantity < 1 || quantity > 1000) {
        return {};
    }

    Ref<SteamPurchaseLine> selection;
    selection.instantiate();
    selection->sku = sku;
    selection->quantity = quantity;
    Ref<SteamPurchaseCart> branch;
    branch.instantiate();
    bool replaced = false;
    for (int64_t index = 0; index < selections.size(); ++index) {
        const Ref<SteamPurchaseLine> current = selections[index];
        if (current->get_sku() == sku) {
            branch->selections.append(selection);
            replaced = true;
        } else {
            branch->selections.append(current);
        }
    }
    if (!replaced) {
        if (branch->selections.size() >= 32) {
            return {};
        }
        branch->selections.append(selection);
    }
    return branch;
}

Ref<SteamPurchaseCart> SteamPurchaseCart::with_checkout_id(const String &id) const {
    if (!valid_steam_checkout_id(id)) {
        return {};
    }
    Ref<SteamPurchaseCart> branch;
    branch.instantiate();
    branch->selections = selections;
    branch->checkout_id = id;
    return branch;
}

TypedArray<SteamPurchaseLine> SteamPurchaseCart::lines() const {
    TypedArray<SteamPurchaseLine> detached(selections.duplicate());
    detached.make_read_only();
    return detached;
}

String SteamPurchaseCart::get_checkout_id() const {
    const auto *os = OS::get_singleton();
    return os && os->get_thread_caller_id() == os->get_main_thread_id() ? checkout_id : String();
}

bool SteamPurchaseCart::ensure_checkout_id() {
    if (checkout_id.is_empty()) {
        Ref<Crypto> crypto;
        crypto.instantiate();
        const auto bytes = crypto->generate_random_bytes(16);
        if (bytes.size() != 16) {
            return false;
        }
        checkout_id = bytes.hex_encode();
    }
    return true;
}

void SteamPurchaseItem::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "sku", "display_name", "amount", "currency"),
            &SteamPurchaseItem::create);
    ClassDB::bind_method(D_METHOD("get_sku"), &SteamPurchaseItem::get_sku);
    ClassDB::bind_method(D_METHOD("get_display_name"), &SteamPurchaseItem::get_display_name);
    ClassDB::bind_method(D_METHOD("get_amount"), &SteamPurchaseItem::get_amount);
    ClassDB::bind_method(D_METHOD("get_currency"), &SteamPurchaseItem::get_currency);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sku", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_sku");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "amount", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_amount");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "currency", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_currency");
}

Ref<SteamPurchaseItem> SteamPurchaseItem::create(
        const StringName &sku, const String &display_name, int64_t amount, const String &currency) {
    if (!valid_sku(sku) || display_name.strip_edges().is_empty() || display_name.utf8().length() > 512 ||
            amount <= 0 || !valid_currency(currency)) {
        return {};
    }
    Ref<SteamPurchaseItem> item;
    item.instantiate();
    item->sku = sku;
    item->display_name = display_name;
    item->amount = amount;
    item->currency = currency;
    return item;
}

StringName SteamPurchaseItem::get_sku() const { return sku; }
String SteamPurchaseItem::get_display_name() const { return display_name; }
int64_t SteamPurchaseItem::get_amount() const { return amount; }
String SteamPurchaseItem::get_currency() const { return currency; }

void SteamPurchaseCatalog::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "entries"), &SteamPurchaseCatalog::create);
    ClassDB::bind_method(D_METHOD("get_entries"), &SteamPurchaseCatalog::get_entries);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_ARRAY_TYPE, "SteamPurchaseItem",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_entries");
}

SteamPurchaseCatalog::SteamPurchaseCatalog() {
    entries.make_read_only();
}

Ref<SteamPurchaseCatalog> SteamPurchaseCatalog::create(const TypedArray<SteamPurchaseItem> &entries) {
    if (entries.size() > 256) {
        return {};
    }
    std::set<StringName> seen;
    TypedArray<SteamPurchaseItem> snapshot;
    for (int64_t index = 0; index < entries.size(); ++index) {
        const Ref<SteamPurchaseItem> item = entries[index];
        if (item.is_null() || !valid_sku(item->get_sku()) || !seen.emplace(item->get_sku()).second) {
            return {};
        }
        snapshot.append(item);
    }
    snapshot.make_read_only();
    Ref<SteamPurchaseCatalog> catalog;
    catalog.instantiate();
    catalog->entries = snapshot;
    return catalog;
}

TypedArray<SteamPurchaseItem> SteamPurchaseCatalog::get_entries() const { return entries; }

void SteamPurchaseOutcome::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "order_id", "currency", "total", "state"),
            &SteamPurchaseOutcome::create);
    ClassDB::bind_method(D_METHOD("get_order_id"), &SteamPurchaseOutcome::get_order_id);
    ClassDB::bind_method(D_METHOD("get_currency"), &SteamPurchaseOutcome::get_currency);
    ClassDB::bind_method(D_METHOD("get_total"), &SteamPurchaseOutcome::get_total);
    ClassDB::bind_method(D_METHOD("get_state"), &SteamPurchaseOutcome::get_state);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "order_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_order_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "currency", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_currency");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_total");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "state", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_state");
    BIND_ENUM_CONSTANT(AWAITING_APPROVAL);
    BIND_ENUM_CONSTANT(DECLINED);
    BIND_ENUM_CONSTANT(RECONCILING);
    BIND_ENUM_CONSTANT(FULFILLED);
    BIND_ENUM_CONSTANT(REVERSED);
    BIND_ENUM_CONSTANT(PARTIALLY_REVERSED);
}

Ref<SteamPurchaseOutcome> SteamPurchaseOutcome::create(
        const String &order_id, const String &currency, int64_t total, State state) {
    if (!valid_steam_order_id(order_id) || !valid_currency(currency) || total <= 0 ||
            state < AWAITING_APPROVAL || state > PARTIALLY_REVERSED) {
        return {};
    }
    Ref<SteamPurchaseOutcome> receipt;
    receipt.instantiate();
    receipt->order_id = order_id;
    receipt->currency = currency;
    receipt->total = total;
    receipt->state = state;
    return receipt;
}

String SteamPurchaseOutcome::get_order_id() const { return order_id; }
String SteamPurchaseOutcome::get_currency() const { return currency; }
int64_t SteamPurchaseOutcome::get_total() const { return total; }
SteamPurchaseOutcome::State SteamPurchaseOutcome::get_state() const { return state; }

void SteamPurchaseCatalogResult::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "error", "value"),
            &SteamPurchaseCatalogResult::create, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPurchaseCatalogResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamPurchaseCatalog",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamPurchaseCatalogResult> SteamPurchaseCatalogResult::create(Error error, const Ref<SteamPurchaseCatalog> &value) {
    Ref<SteamPurchaseCatalogResult> result;
    result.instantiate();
    result->set_outcome(error == OK && value.is_null() ? ERR_INVALID_DATA : error);
    if (result->is_ok()) {
        result->value = value;
    }
    return result;
}

Ref<SteamPurchaseCatalog> SteamPurchaseCatalogResult::get_value() const { return value; }

void SteamPurchaseOutcomeResult::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("create", "error", "value", "checkout_id"),
            &SteamPurchaseOutcomeResult::create, DEFVAL(Variant()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPurchaseOutcomeResult::get_value);
    ClassDB::bind_method(D_METHOD("get_checkout_id"), &SteamPurchaseOutcomeResult::get_checkout_id);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamPurchaseOutcome",
                         PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "checkout_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_checkout_id");
}

Ref<SteamPurchaseOutcomeResult> SteamPurchaseOutcomeResult::create(
        Error error, const Ref<SteamPurchaseOutcome> &value, const String &checkout_id) {
    Ref<SteamPurchaseOutcomeResult> result;
    result.instantiate();
    const bool valid_receipt = value.is_valid() && valid_steam_order_id(value->get_order_id());
    result->set_outcome(error == OK && !valid_receipt ? ERR_INVALID_DATA : error);
    if (valid_receipt && (result->is_ok() || value->get_state() == SteamPurchaseOutcome::RECONCILING)) {
        result->value = value;
    }
    if (valid_steam_checkout_id(checkout_id)) {
        result->checkout_id = checkout_id;
    }
    return result;
}

Ref<SteamPurchaseOutcome> SteamPurchaseOutcomeResult::get_value() const { return value; }
String SteamPurchaseOutcomeResult::get_checkout_id() const { return checkout_id; }

} // namespace crimson::steam
