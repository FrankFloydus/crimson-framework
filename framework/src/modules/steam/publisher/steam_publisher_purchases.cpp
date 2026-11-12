// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_transport.hpp"
#include "modules/steam/publisher/steam_publisher_codec.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <string>

namespace crimson::steam {
using namespace godot;
using namespace publisher_codec;

namespace {

using Order = SteamPublisherPurchaseOrder;
using Endpoint = SteamPublisherEndpoint;

// The last second expressible as a four-digit RFC 3339 year.
constexpr int64_t maximum_cursor = 253402300799;

// Extract an exact uint64 from a Steam reply field.
bool read_unsigned(const Json *object, const char *key, uint64_t &value) {
    const auto *field = member(object, key);
    return field && unsigned_value(*field, value);
}

// Read a canonical positive Steam transaction or game order ID.
String read_id(const Json *object, const char *key) {
    uint64_t value = 0;
    return read_unsigned(object, key, value) && value > 0 ? String(std::to_string(value).c_str()) : String();
}

// Match Steam's documented response markers without leaking vendor text.
bool equals(const Json *object, const char *key, const char *expected) {
    const auto *value = member(object, key);
    return value && value->is_string() && *value == expected;
}

// Get response parameters only from Steam's explicit OK envelope.
const Json *success_params(const Json &body) {
    const auto *response = member(&body, "response");
    const auto *params = member(response, "params");
    if (!equals(response, "result", "OK") || !params || !params->is_object()) {
        return nullptr;
    }
    for (const auto *object : {&body, response, params}) {
        if (member(object, "error") || member(object, "errorcode")) {
            return nullptr;
        }
    }
    return params;
}

// Read one bounded Wallet metadata field without exposing the raw vendor payload.
bool buyer_field(const Json *params, const char *key, String &output, bool required = false) {
    const auto *field = member(params, key);
    if (!field) {
        return !required;
    }
    if (!field->is_string()) {
        return false;
    }
    const auto &text = field->get_ref<const std::string &>();
    if (text.size() > 64 || std::any_of(text.begin(), text.end(), [](unsigned char character) {
            return character < 32 || character == 127;
        })) {
        return false;
    }
    output = String::utf8(text.data(), static_cast<int64_t>(text.size()));
    return true;
}

// Interpret only the Wallet purchasing states Valve documents.
SteamPurchaseBuyerInfo::AccountStatus buyer_status(const String &value) {
    const String status = value.to_lower();
    if (status == "active") {
        return SteamPurchaseBuyerInfo::ACTIVE;
    }
    if (status == "trusted") {
        return SteamPurchaseBuyerInfo::TRUSTED;
    }
    if (status == "locked" || status == "locked from purchasing") {
        return SteamPurchaseBuyerInfo::LOCKED;
    }
    return SteamPurchaseBuyerInfo::UNKNOWN;
}

// Recognize only explicit, definitive checkout rejection codes from InitTxn.
bool init_rejected(const Json &body, const Ref<Order> &order) {
    const auto *response = member(&body, "response");
    const auto *params = member(response, "params");
    uint64_t code = 0;
    uint64_t transaction = 0;
    if (!equals(response, "result", "Failure") || !read_unsigned(member(response, "error"), "errorcode", code) ||
            (params && !params->is_object()) ||
            (member(params, "orderid") && read_id(params, "orderid") != order->get_order_id()) ||
            (member(response, "orderid") && read_id(response, "orderid") != order->get_order_id()) ||
            (member(&body, "orderid") && read_id(&body, "orderid") != order->get_order_id()) ||
            (member(params, "transid") && (!read_unsigned(params, "transid", transaction) || transaction != 0))) {
        return false;
    }
    switch (code) {
        case 7: // User is not logged in.
        case 8: // Currency does not match the Steam account.
        case 10: // Denied by the user.
        case 11: // Restricted country.
        case 100: // Insufficient funds.
        case 102: // Disabled account.
        case 103: // Account cannot purchase.
        case 104: // Fraud denial.
            return true;
        default:
            return false;
    }
}

// Decode Steam's documented RFC 3339 UTC second without accepting normalized invalid dates.
int64_t read_time(const Json *object, const char *key) {
    const auto *value = member(object, key);
    if (!value || !value->is_string()) {
        return -1;
    }
    const auto &source = value->get_ref<const std::string &>();
    if (source.size() != 20 || source[4] != '-' || source[7] != '-' || source[10] != 'T' ||
            source[13] != ':' || source[16] != ':' || source[19] != 'Z') {
        return -1;
    }
    for (const size_t index : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18}) {
        if (source[index] < '0' || source[index] > '9') {
            return -1;
        }
    }
    const auto number = [&source](size_t index) {
        return (source[index] - '0') * 10 + (source[index + 1] - '0');
    };
    const int year = number(0) * 100 + number(2);
    const int month = number(5);
    const int day = number(8);
    constexpr int month_lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < 1970 || month < 1 || month > 12 || day < 1 ||
            day > month_lengths[month - 1] + (month == 2 && leap) ||
            number(11) > 23 || number(14) > 59 || number(17) > 59) {
        return -1;
    }
    const String date = String::utf8(source.data(), 19);
    auto *time = Time::get_singleton();
    const int64_t unix = time->get_unix_time_from_datetime_string(date);
    return unix >= 0 && unix <= maximum_cursor && time->get_datetime_string_from_unix_time(unix) == date ? unix : -1;
}

// Match the server-priced order to the exact unpriced cart and stable checkout retry ID.
bool matches_cart(const Ref<Order> &order, const Ref<SteamPurchaseCart> &cart, const String &checkout_id,
        const String &payer) {
    if (order.is_null() || order->get_checkout_id() != checkout_id || order->get_steam_id() != payer) {
        return false;
    }
    const auto requested = cart->lines();
    const auto quoted = order->get_lines();
    if (requested.size() != quoted.size()) {
        return false;
    }
    for (int64_t index = 0; index < requested.size(); ++index) {
        const Ref<SteamPurchaseLine> line = requested[index];
        bool matched = false;
        for (int64_t quote_index = 0; quote_index < quoted.size(); ++quote_index) {
            const Ref<SteamPublisherPurchaseLine> quote = quoted[quote_index];
            if (quote->get_sku() == line->get_sku() && quote->get_quantity() == line->get_quantity()) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

// Convert only durable entitlement state into a caller-facing outcome.
Ref<SteamPurchaseOutcomeResult> outcome(Error error, const Ref<Order> &order = {}) {
    if (order.is_null()) {
        return SteamPurchaseOutcomeResult::create(error);
    }
    auto status = SteamPurchaseOutcome::RECONCILING;
    if (error == OK) {
        switch (order->get_phase()) {
            case Order::AWAITING:
            case Order::APPROVED:
                status = SteamPurchaseOutcome::AWAITING_APPROVAL;
                break;
            case Order::DECLINED:
                status = SteamPurchaseOutcome::DECLINED;
                break;
            case Order::FULFILLED:
                status = SteamPurchaseOutcome::FULFILLED;
                break;
            case Order::PARTIALLY_REVERSED:
                status = SteamPurchaseOutcome::PARTIALLY_REVERSED;
                break;
            case Order::REVERSED:
                status = SteamPurchaseOutcome::REVERSED;
                break;
            default:
                break;
        }
    }
    return SteamPurchaseOutcomeResult::create(error, SteamPurchaseOutcome::create(order->get_order_id(),
            order->get_currency(), order->get_total(), status), order->get_checkout_id());
}

// Match an endpoint acknowledgment to the same known order and exact transaction, when already known.
String acknowledged_transaction(const Json &body, const Ref<Order> &order) {
    const auto *params = success_params(body);
    const String transaction = read_id(params, "transid");
    return read_id(params, "orderid") == order->get_order_id() &&
            (order->get_transaction_id().is_empty() || order->get_transaction_id() == transaction) ?
            transaction : String();
}

// Return whether a Steam item or order has one of the documented reversal statuses.
bool reversed(const Json *object, const char *key) {
    return equals(object, key, "Refunded") || equals(object, key, "Chargedback") ||
            equals(object, key, "RefundedSuspectedFraud") || equals(object, key, "RefundedFriendlyFraud");
}

// Compare Steam's exact line set, quantities, and configured tax basis to the durable quote.
bool verify_items(const Json *items, const Ref<Order> &order, bool all_reversed, bool partial, bool paid,
        PackedInt64Array &reversed_items) {
    const auto quoted = order->get_lines();
    if (!items || !items->is_array() || items->size() != static_cast<size_t>(quoted.size())) {
        return false;
    }
    std::set<int64_t> seen;
    for (const auto &entry : *items) {
        uint64_t item_id = 0;
        uint64_t quantity = 0;
        uint64_t net = 0;
        uint64_t vat = 0;
        if (!read_unsigned(&entry, "itemid", item_id) || item_id == 0 || item_id > UINT32_MAX ||
                !read_unsigned(&entry, "qty", quantity) || !read_unsigned(&entry, "amount", net) ||
                !read_unsigned(&entry, "vat", vat) || vat > UINT64_MAX - net ||
                !seen.insert(static_cast<int64_t>(item_id)).second) {
            return false;
        }
        Ref<SteamPublisherPurchaseLine> quote;
        for (int64_t index = 0; index < quoted.size(); ++index) {
            const Ref<SteamPublisherPurchaseLine> candidate = quoted[index];
            if (candidate->get_item_id() == static_cast<int64_t>(item_id)) {
                quote = candidate;
                break;
            }
        }
        const uint64_t total = order->get_tax_basis() == Order::GROSS ? net + vat : net;
        if (quote.is_null() || quantity != static_cast<uint64_t>(quote->get_quantity()) ||
                total != static_cast<uint64_t>(quote->get_total())) {
            return false;
        }
        if (all_reversed || (partial && reversed(&entry, "itemstatus"))) {
            reversed_items.push_back(static_cast<int64_t>(item_id));
        } else if ((partial || paid) && !equals(&entry, "itemstatus", "Succeeded")) {
            return false;
        }
    }
    return !partial || (!reversed_items.is_empty() && reversed_items.size() < quoted.size());
}

// Preserve terminal and uncertain phases when a stale read would move them backwards.
Order::Phase observed_phase(Order::Phase before, Order::Phase observed) {
    if (before == Order::REVERSED || (before == Order::PARTIALLY_REVERSED && observed != Order::REVERSED)) {
        return before;
    }
    if (before == Order::FULFILLED || before == Order::REFUNDING) {
        return observed >= Order::PARTIALLY_REVERSED ? observed : before;
    }
    if (before == Order::DECLINED || (before == Order::FINALIZING && observed < Order::DECLINED) ||
            (before == Order::APPROVED && observed == Order::AWAITING)) {
        return before;
    }
    return observed;
}

} // namespace

// Own one bounded trusted purchase action until its exact callback settles.
struct SteamPublisherPurchaseFlow : std::enable_shared_from_this<SteamPublisherPurchaseFlow> {
    // Weak issuer so a pending action cannot extend the publisher's lifetime.
    std::weak_ptr<SteamPublisherState> scope;
    // One await carrier kept by the native pending request.
    Ref<SteamPublisherCompletion> completion;

    // Observe an exact durable order read and validate its namespace before use.
    static Error load(const std::shared_ptr<SteamPublisherState> &state, const String &id, Ref<Order> &order) {
        Ref<SteamPublisherPurchaseOrderResult> read;
        if (!state->purchase_store->GDVIRTUAL_CALL(_load_order, state->app_id, state->environment, id, read) ||
                read.is_null()) {
            return ERR_UNCONFIGURED;
        }
        if (!SteamPublisherHost::resolve(state)) {
            return ERR_SKIP;
        }
        if (!read->is_ok()) {
            return read->get_error();
        }
        order = read->get_value();
        if (order.is_null() || order->get_app_id() != state->app_id ||
                order->get_environment() != state->environment || order->get_order_id() != id) {
            order.unref();
            return ERR_INVALID_DATA;
        }
        return OK;
    }

    // Save the exact next revision and its associated entitlement difference in one durable operation.
    static Error advance(const std::shared_ptr<SteamPublisherState> &state, Ref<Order> &order, Order::Phase phase,
            const String &transaction, int64_t updated_at, const PackedInt64Array &reversed_items = {}) {
        if (!SteamPublisherHost::resolve(state)) {
            return ERR_SKIP;
        }
        if (phase == order->get_phase() && transaction == order->get_transaction_id() &&
                updated_at == order->get_vendor_updated_at() && reversed_items == order->get_reversed_item_ids()) {
            return OK;
        }
        const auto next = order->with_progress(order->get_revision() + 1, phase, transaction, updated_at, reversed_items);
        if (next.is_null()) {
            return ERR_INVALID_DATA;
        }
        bool committed = false;
        if (!state->purchase_store->GDVIRTUAL_CALL(_compare_and_commit, order->get_revision(), next, committed) ||
                !committed) {
            return ERR_BUSY;
        }
        order = next;
        return SteamPublisherHost::resolve(state) ? OK : ERR_SKIP;
    }

    // Validate and atomically apply a complete QueryTxn/GetReport row for this exact quote.
    static Error apply(const std::shared_ptr<SteamPublisherState> &state, Ref<Order> &order, const Json *row) {
        String payer;
        const String transaction = read_id(row, "transid");
        const int64_t updated_at = read_time(row, "time");
        uint64_t response_app = 0;
        if (read_id(row, "orderid") != order->get_order_id() || transaction.is_empty() || updated_at < 0 ||
                (!order->get_transaction_id().is_empty() && transaction != order->get_transaction_id()) ||
                !steam_id(member(row, "steamid"), true, payer) || payer != order->get_steam_id() ||
                !equals(row, "currency", order->get_currency().utf8().get_data()) ||
                (member(row, "appid") && (!read_unsigned(row, "appid", response_app) ||
                        response_app != static_cast<uint64_t>(state->app_id)))) {
            return ERR_INVALID_DATA;
        }
        const bool partial = equals(row, "status", "PartialRefund");
        const bool all_reversed = reversed(row, "status");
        const bool paid = equals(row, "status", "Succeeded");
        PackedInt64Array item_reversals;
        if (!verify_items(member(row, "items"), order, all_reversed, partial, paid, item_reversals)) {
            return ERR_INVALID_DATA;
        }
        Order::Phase observed;
        if (all_reversed) {
            observed = Order::REVERSED;
        } else if (partial) {
            observed = Order::PARTIALLY_REVERSED;
        } else if (paid) {
            observed = Order::FULFILLED;
        } else if (equals(row, "status", "Approved")) {
            observed = Order::APPROVED;
        } else if (equals(row, "status", "Init")) {
            observed = Order::AWAITING;
        } else if (equals(row, "status", "Failed")) {
            observed = Order::DECLINED;
        } else {
            return ERR_INVALID_DATA;
        }
        if (updated_at < order->get_vendor_updated_at()) {
            return OK;
        }
        const auto next = observed_phase(order->get_phase(), observed);
        if (next != Order::PARTIALLY_REVERSED && next != Order::REVERSED) {
            item_reversals.clear();
        } else if (next == Order::REVERSED && observed != Order::REVERSED) {
            item_reversals = order->get_reversed_item_ids();
        } else if (next == Order::PARTIALLY_REVERSED) {
            const auto prior = order->get_reversed_item_ids();
            for (int64_t index = 0; index < prior.size(); ++index) {
                if (!item_reversals.has(prior[index])) {
                    item_reversals.push_back(prior[index]);
                }
            }
            if (item_reversals.size() == order->get_lines().size()) {
                return advance(state, order, Order::REVERSED, transaction, updated_at, item_reversals);
            }
        }
        item_reversals.sort();
        return advance(state, order, next, transaction, updated_at, item_reversals);
    }

    // Issue a fixed route in the exact publisher scope and retain this action in its pending callback.
    Error send(const SteamPublisherRequest &request,
            std::function<void(const SteamPublisherReply &, const std::shared_ptr<SteamPublisherState> &)> handler) {
        const auto state = scope.lock();
        auto *host = SteamPublisherHost::resolve(state);
        if (!host) {
            return ERR_UNAVAILABLE;
        }
        return host->send(state, request, [self = shared_from_this(), handler = std::move(handler)](const auto &reply) {
            handler(reply, self->scope.lock());
        });
    }

    // Begin Steam authorization after durably claiming an immutable order.
    void initialize(Ref<Order> order) {
        const auto state = scope.lock();
        const Error claimed = advance(state, order, Order::INITIALIZING, {}, 0);
        if (claimed != OK) {
            completion->finish(outcome(claimed, order));
            return;
        }
        SteamPublisherRequest request{Endpoint::INIT_PURCHASE};
        request.add("appid", String::num_int64(state->app_id));
        request.add("orderid", order->get_order_id());
        request.add("steamid", order->get_steam_id());
        request.add("language", order->get_language());
        request.add("currency", order->get_currency());
        request.add("usersession", "client");
        const auto lines = order->get_lines();
        request.add("itemcount", String::num_int64(lines.size()));
        for (int64_t index = 0; index < lines.size(); ++index) {
            const Ref<SteamPublisherPurchaseLine> line = lines[index];
            const String suffix = "[" + String::num_int64(index) + "]";
            request.add("itemid" + suffix, String::num_int64(line->get_item_id()));
            request.add("qty" + suffix, String::num_int64(line->get_quantity()));
            request.add("amount" + suffix, String::num_int64(line->get_total()));
            request.add("description" + suffix, line->get_description());
        }
        const Error started = send(request, [this, order](const auto &reply, const auto &owner) mutable {
            if (reply.error != OK) {
                completion->finish(outcome(reply.error, order));
                return;
            }
            const Json body = parse_body(reply.body);
            const String transaction = acknowledged_transaction(body, order);
            Error error = ERR_INVALID_DATA;
            if (!transaction.is_empty()) {
                error = advance(owner, order, Order::AWAITING, transaction, 0);
            } else if (init_rejected(body, order)) {
                error = advance(owner, order, Order::DECLINED, {}, 0);
            }
            completion->finish(outcome(error, order));
        });
        if (started != OK) {
            // A synchronous submission error proves that the request was never queued.
            advance(state, order, Order::RESERVED, {}, 0);
            completion->finish(outcome(started, order));
        }
    }

    // Query before sending a charge or refund and observe previously uncertain mutations without repeating them.
    void query(Ref<Order> order, Endpoint follow_up = Endpoint::QUERY_PURCHASE) {
        if (order->get_phase() == Order::RESERVED || (order->get_phase() == Order::DECLINED &&
                order->get_transaction_id().is_empty())) {
            completion->finish(outcome(OK, order));
            return;
        }
        const auto state = scope.lock();
        SteamPublisherRequest request{Endpoint::QUERY_PURCHASE};
        request.add("appid", String::num_int64(state->app_id));
        request.add("orderid", order->get_order_id());
        const Error started = send(request, [this, order, follow_up](const auto &reply, const auto &owner) mutable {
            const Error error = reply.error == OK ? apply(owner, order, success_params(parse_body(reply.body))) : reply.error;
            if (error != OK) {
                completion->finish(outcome(error, order));
                return;
            }
            if (follow_up == Endpoint::FINALIZE_PURCHASE && order->get_phase() == Order::APPROVED) {
                mutate(order, follow_up, Order::FINALIZING);
            } else if (follow_up == Endpoint::REFUND_PURCHASE && order->get_phase() == Order::FULFILLED) {
                bool authorized = false;
                if (!owner->purchase_store->GDVIRTUAL_CALL(_authorize_refund, order, authorized) || !authorized) {
                    completion->finish(outcome(ERR_UNAUTHORIZED));
                } else {
                    mutate(order, follow_up, Order::REFUNDING);
                }
            } else {
                completion->finish(outcome(OK, order));
            }
        });
        if (started != OK) {
            completion->finish(outcome(started, order));
        }
    }

    // Claim a verified mutation, require an exact acknowledgment, and preserve uncertain effects for QueryTxn.
    void mutate(Ref<Order> order, Endpoint endpoint, Order::Phase claimed_phase) {
        const auto state = scope.lock();
        const Order::Phase previous = order->get_phase();
        const String transaction = order->get_transaction_id();
        const int64_t time = order->get_vendor_updated_at();
        const Error claimed = advance(state, order, claimed_phase, transaction, time);
        if (claimed != OK) {
            completion->finish(outcome(claimed, order));
            return;
        }
        SteamPublisherRequest request{endpoint};
        request.add("appid", String::num_int64(state->app_id));
        request.add("orderid", order->get_order_id());
        const Error started = send(request, [this, order, endpoint](const auto &reply, const auto &owner) mutable {
            if (reply.error != OK || acknowledged_transaction(parse_body(reply.body), order).is_empty()) {
                completion->finish(outcome(reply.error != OK ? reply.error : ERR_INVALID_DATA, order));
                return;
            }
            if (endpoint == Endpoint::REFUND_PURCHASE) {
                query(order);
                return;
            }
            const Error committed = advance(owner, order, Order::FULFILLED,
                    order->get_transaction_id(), order->get_vendor_updated_at());
            completion->finish(outcome(committed, order));
        });
        if (started != OK) {
            advance(state, order, previous, transaction, time);
            completion->finish(outcome(started, order));
        }
    }

    // Apply one inclusive, ordered settlement page and keep the input cursor on any error.
    void report(int64_t since, int64_t max_results) {
        const auto state = scope.lock();
        SteamPublisherRequest request{Endpoint::PURCHASE_REPORT};
        request.add("appid", String::num_int64(state->app_id));
        request.add("type", "SETTLEMENT");
        request.add("time", Time::get_singleton()->get_datetime_string_from_unix_time(since) + "Z");
        request.add("maxresults", String::num_int64(max_results));
        const Error started = send(request, [this, since, max_results](const auto &reply, const auto &owner) {
            Ref<SteamPurchaseReconciliation> result;
            result.instantiate();
            result->next_since_unix = since;
            const Error error = reply.error == OK ? apply_report(owner, reply.body, since, max_results, result) : reply.error;
            completion->finish(SteamPurchaseReconciliationResult::create(error, result));
        });
        if (started != OK) {
            completion->finish(SteamPurchaseReconciliationResult::create(started));
        }
    }

    // Validate settlement order, then persist known orders without skipping a failed update.
    static Error apply_report(const std::shared_ptr<SteamPublisherState> &state, const PackedByteArray &reply,
            int64_t since, int64_t max_results, const Ref<SteamPurchaseReconciliation> &result) {
        const Json body = parse_body(reply);
        const auto *params = success_params(body);
        const auto *rows = member(params, "orders");
        uint64_t count = 0;
        if (!read_unsigned(params, "count", count) || count > static_cast<uint64_t>(max_results) ||
                (rows && (!rows->is_array() || rows->size() != count)) || (!rows && count != 0)) {
            return ERR_INVALID_DATA;
        }
        if (count == 0) {
            return OK;
        }
        int64_t cursor = since;
        for (const auto &row : *rows) {
            const int64_t time = read_time(&row, "time");
            uint64_t raw_order_id = 0;
            if (time < cursor || !read_unsigned(&row, "orderid", raw_order_id)) {
                return ERR_INVALID_DATA;
            }
            cursor = time;
            if (raw_order_id == 0) {
                continue;
            }
            Ref<Order> order;
            const Error loaded = load(state, read_id(&row, "orderid"), order);
            if (loaded == ERR_DOES_NOT_EXIST) {
                continue;
            }
            if (loaded != OK) {
                return loaded;
            }
            const Error applied = apply(state, order, &row);
            if (applied != OK) {
                return applied;
            }
            ++result->processed;
        }
        if (count == static_cast<uint64_t>(max_results) && cursor == since) {
            return ERR_BUSY;
        }
        result->next_since_unix = cursor;
        return OK;
    }
};

void SteamPublisherPurchases::_bind_methods() {
    ClassDB::bind_method(D_METHOD("buyer_info", "player"), &SteamPublisherPurchases::buyer_info);
    ClassDB::bind_method(D_METHOD("begin", "player", "cart", "checkout_id"), &SteamPublisherPurchases::begin);
    ClassDB::bind_method(D_METHOD("query", "order_id"), &SteamPublisherPurchases::query);
    ClassDB::bind_method(D_METHOD("finalize", "order_id"), &SteamPublisherPurchases::finalize);
    ClassDB::bind_method(D_METHOD("reconcile", "since_unix", "max_results"), &SteamPublisherPurchases::reconcile,
            DEFVAL(maximum_report_rows));
    ClassDB::bind_method(D_METHOD("refund", "order_id"), &SteamPublisherPurchases::refund);
}

Variant SteamPublisherPurchases::buyer_info(const Ref<SteamIdentity> &player) {
    if (!publisher_on_main_thread()) {
        return SteamPurchaseBuyerInfoResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamPurchaseBuyerInfoResult::create(ERR_UNAVAILABLE);
    }
    if (player.is_null() || !player->belongs_to(owner)) {
        return SteamPurchaseBuyerInfoResult::create(ERR_UNAUTHORIZED);
    }
    const int64_t app_id = owner->app_id;
    const String payer = player->get_steam_id();
    SteamPublisherRequest request{Endpoint::PURCHASE_BUYER_INFO};
    request.add("appid", String::num_int64(app_id));
    request.add("steamid", payer);
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(owner, request, [operation, app_id, payer](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamPurchaseBuyerInfoResult::create(reply.error));
            return;
        }
        const Json root = parse_body(reply.body);
        const auto *params = success_params(root);
        String country;
        String currency;
        String region;
        String status;
        String response_player;
        uint64_t response_app = 0;
        if (!params || !buyer_field(params, "country", country, true) || !buyer_field(params, "currency", currency, true) ||
                !buyer_field(params, "state", region) || !buyer_field(params, "status", status) ||
                (member(params, "steamid") && (!steam_id(member(params, "steamid"), true, response_player) || response_player != payer)) ||
                (member(params, "appid") && (!read_unsigned(params, "appid", response_app) || response_app != static_cast<uint64_t>(app_id)))) {
            operation->finish(SteamPurchaseBuyerInfoResult::create(ERR_INVALID_DATA));
            return;
        }
        const auto value = SteamPurchaseBuyerInfo::create(country, currency, region, buyer_status(status));
        operation->finish(SteamPurchaseBuyerInfoResult::create(value.is_valid() ? OK : ERR_INVALID_DATA, value));
    });
    return started == OK ? operation->wait() : Variant(SteamPurchaseBuyerInfoResult::create(started));
}

Variant SteamPublisherPurchases::begin(const Ref<SteamIdentity> &player, const Ref<SteamPurchaseCart> &cart,
        const String &checkout_id) {
    if (!publisher_on_main_thread()) {
        return outcome(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return outcome(ERR_UNAVAILABLE);
    }
    if (owner->purchase_store.is_null()) {
        return outcome(ERR_UNCONFIGURED);
    }
    if (player.is_null() || !player->belongs_to(owner)) {
        return outcome(ERR_UNAUTHORIZED);
    }
    if (cart.is_null() || cart->lines().is_empty() || cart->lines().size() > 32 || !valid_steam_checkout_id(checkout_id) ||
            (!cart->get_checkout_id().is_empty() && cart->get_checkout_id() != checkout_id)) {
        return outcome(ERR_INVALID_PARAMETER);
    }
    Ref<Order> order;
    const String payer = player->get_steam_id();
    if (!owner->purchase_store->GDVIRTUAL_CALL(_reserve_checkout, owner->app_id, owner->environment, payer, cart,
                checkout_id, order)) {
        return outcome(ERR_UNCONFIGURED);
    }
    if (!SteamPublisherHost::resolve(owner)) {
        return outcome(ERR_SKIP);
    }
    if (order.is_null()) {
        return outcome(ERR_INVALID_PARAMETER);
    }
    if (order->get_app_id() != owner->app_id || order->get_environment() != owner->environment ||
            !matches_cart(order, cart, checkout_id, payer)) {
        return outcome(ERR_INVALID_DATA);
    }
    if (order->get_phase() != Order::RESERVED && order->get_phase() != Order::INITIALIZING) {
        return outcome(OK, order);
    }
    auto flow = std::make_shared<SteamPublisherPurchaseFlow>();
    flow->scope = owner;
    flow->completion.instantiate();
    if (order->get_phase() == Order::RESERVED) {
        flow->initialize(order);
    } else {
        flow->query(order);
    }
    return flow->completion->wait();
}

Variant SteamPublisherPurchases::observe(const String &order_id, Action action) {
    if (!publisher_on_main_thread()) {
        return outcome(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return outcome(ERR_UNAVAILABLE);
    }
    if (owner->purchase_store.is_null()) {
        return outcome(ERR_UNCONFIGURED);
    }
    if (!valid_steam_order_id(order_id)) {
        return outcome(ERR_INVALID_PARAMETER);
    }
    Ref<Order> order;
    const Error loaded = SteamPublisherPurchaseFlow::load(owner, order_id, order);
    if (loaded != OK) {
        return outcome(loaded);
    }
    auto flow = std::make_shared<SteamPublisherPurchaseFlow>();
    flow->scope = owner;
    flow->completion.instantiate();
    Endpoint next = Endpoint::QUERY_PURCHASE;
    if (action == Action::FINALIZE) {
        next = Endpoint::FINALIZE_PURCHASE;
    } else if (action == Action::REFUND) {
        next = Endpoint::REFUND_PURCHASE;
    }
    flow->query(order, next);
    return flow->completion->wait();
}

Variant SteamPublisherPurchases::query(const String &order_id) {
    return observe(order_id, Action::QUERY);
}

Variant SteamPublisherPurchases::finalize(const String &order_id) {
    return observe(order_id, Action::FINALIZE);
}

Variant SteamPublisherPurchases::refund(const String &order_id) {
    return observe(order_id, Action::REFUND);
}

Variant SteamPublisherPurchases::reconcile(int64_t since_unix, int64_t max_results) {
    if (!publisher_on_main_thread()) {
        return SteamPurchaseReconciliationResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamPurchaseReconciliationResult::create(ERR_UNAVAILABLE);
    }
    if (owner->purchase_store.is_null()) {
        return SteamPurchaseReconciliationResult::create(ERR_UNCONFIGURED);
    }
    if (since_unix < 0 || since_unix > maximum_cursor || max_results < 1 || max_results > maximum_report_rows) {
        return SteamPurchaseReconciliationResult::create(ERR_INVALID_PARAMETER);
    }
    auto flow = std::make_shared<SteamPublisherPurchaseFlow>();
    flow->scope = owner;
    flow->completion.instantiate();
    flow->report(since_unix, max_results);
    return flow->completion->wait();
}

} // namespace crimson::steam
