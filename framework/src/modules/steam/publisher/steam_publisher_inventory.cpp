// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_inventory.hpp"

#include "modules/steam/publisher/steam_publisher_codec.hpp"
#include "modules/steam/publisher/steam_publisher_transport.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <limits>
#include <set>
#include <string>
#include <vector>

namespace crimson::steam {
using namespace godot;

namespace {

using publisher_codec::Json;
using publisher_codec::member;
using publisher_codec::parse_body;
using publisher_codec::unsigned_value;

// Inventory reads and mutations share exact unsigned identifiers and response bounds.
constexpr uint64_t invalid_instance = (std::numeric_limits<uint64_t>::max)();
constexpr uint64_t max_quantity = (std::numeric_limits<uint32_t>::max)();
constexpr uint64_t max_definition = 999999999;
constexpr size_t max_items = 16384;
constexpr int64_t max_grant_definitions = 256;
// Keep immutable trade fields out of instance storage.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

// Decode a full-range unsigned decimal identifier without an intermediate float.
bool decimal_id(const String &text, uint64_t &id) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString bytes = text.utf8();
    return unsigned_value(Json(std::string(bytes.get_data(), bytes.length())), id) && id != 0;
}

// Add the Service API's JSON form field without placing credentials in the data.
void add_input_json(SteamPublisherRequest &request, const Json &fields) {
    const auto bytes = fields.dump();
    request.add("input_json", String::utf8(bytes.data(), static_cast<int64_t>(bytes.size())));
}

// Treat an echoed account or app mismatch as an invalid response.
bool matches_echo(const Json *object, const char *key, uint64_t expected) {
    const auto *value = member(object, key);
    uint64_t decoded = 0;
    return !value || (unsigned_value(*value, decoded) && decoded == expected);
}

// Recognize the Steam response envelope and reject explicit refusal.
Error envelope(const SteamPublisherReply &reply, Json &body, const Json *&response, bool &acknowledged) {
    if (reply.error != OK) {
        return reply.error;
    }
    body = parse_body(reply.body);
    response = member(&body, "response");
    if (member(&body, "error") || member(response, "error") ||
            member(&body, "errorcode") || member(response, "errorcode")) {
        return ERR_QUERY_FAILED;
    }
    if (!response || !response->is_object()) {
        return ERR_INVALID_DATA;
    }
    if (const auto *success = member(response, "success")) {
        if (!success->is_boolean()) {
            return ERR_INVALID_DATA;
        }
        if (!success->get<bool>()) {
            return ERR_QUERY_FAILED;
        }
        acknowledged = true;
    }
    if (const auto *result = member(response, "result")) {
        uint64_t code = 0;
        if (*result != "OK" && (!unsigned_value(*result, code) || code != 1)) {
            return ERR_QUERY_FAILED;
        }
        acknowledged = true;
    }
    return OK;
}

// A validated item or removal from a bounded Web API response.
struct InventoryRow {
    // Opaque instance ID.
    uint64_t id = 0;
    // Published item definition when the instance remains present.
    uint64_t definition = 0;
    // Current stack quantity when present.
    uint64_t quantity = 0;
    // Optional provider item flags.
    uint64_t flags = 0;
    // Whether the returned row identifies a removed stack.
    bool removed = false;
};

// Decode either a direct item object or one encoded as a JSON string by the Economy service.
bool decode_row(const Json &encoded, uint64_t app, uint64_t player, InventoryRow &row) {
    Json parsed;
    const Json *item = &encoded;
    if (encoded.is_string()) {
        parsed = Json::parse(encoded.get_ref<const std::string &>(), nullptr, false);
        item = &parsed;
    }
    const auto *id = member(item, "itemid");
    if (!id || !unsigned_value(*id, row.id) || row.id == 0 || row.id == invalid_instance ||
            !matches_echo(item, "appid", app) || !matches_echo(item, "accountid", player) ||
            !matches_echo(item, "steamid", player)) {
        return false;
    }
    if (const auto *state = member(item, "state")) {
        if (!state->is_string() || (*state != "" && *state != "removed")) {
            return false;
        }
        row.removed = *state == "removed";
    }
    const auto *quantity = member(item, "quantity");
    const auto *definition = member(item, "itemdefid");
    const auto *flags = member(item, "flags");
    if ((quantity && (!unsigned_value(*quantity, row.quantity) || row.quantity > max_quantity)) ||
            (definition && (!unsigned_value(*definition, row.definition) || row.definition > max_definition)) ||
            (flags && (!unsigned_value(*flags, row.flags) || row.flags > UINT16_MAX))) {
        return false;
    }
    return row.removed || (row.quantity > 0 && row.definition > 0);
}

// Copy only the recognized response array and reject duplicates or an unrelated consumed item.
Error read_rows(const SteamPublisherReply &reply, uint64_t app, uint64_t player,
        std::vector<InventoryRow> &rows, uint64_t consumed_instance = 0) {
    Json body;
    const Json *response = nullptr;
    bool acknowledged = false;
    const Error received = envelope(reply, body, response, acknowledged);
    if (received != OK) {
        return received;
    }
    if (!matches_echo(response, "appid", app) || !matches_echo(response, "steamid", player)) {
        return ERR_INVALID_DATA;
    }
    const auto *items = member(response, "item_json");
    if (!items) {
        return ERR_INVALID_DATA;
    }
    Json parsed;
    if (items->is_string()) {
        parsed = Json::parse(items->get_ref<const std::string &>(), nullptr, false);
        items = &parsed;
    }
    if (!items->is_array() || items->size() > max_items || (consumed_instance != 0 && items->size() != 1)) {
        return ERR_INVALID_DATA;
    }
    std::set<uint64_t> seen;
    rows.reserve(items->size());
    for (const auto &value : *items) {
        InventoryRow row;
        if (!decode_row(value, app, player, row) || !seen.insert(row.id).second ||
                (consumed_instance != 0 && row.id != consumed_instance)) {
            return ERR_INVALID_DATA;
        }
        rows.push_back(row);
    }
    return OK;
}

// Give the trusted caller a safe reconciliation instruction for an uncertain write.
String mutation_message(Error error) {
    switch (error) {
        case ERR_INVALID_DATA:
        case ERR_TIMEOUT:
        case ERR_SKIP:
        case ERR_CONNECTION_ERROR:
            return "The inventory write outcome is unresolved. Reconcile before retrying the same request ID.";
        default:
            return {};
    }
}

} // namespace

void SteamTradeSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_player_id"), &SteamTradeSession::get_player_id);
    ClassDB::bind_method(D_METHOD("get_partner_id"), &SteamTradeSession::get_partner_id);
    ClassDB::bind_method(D_METHOD("is_acknowledged"), &SteamTradeSession::is_acknowledged);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_id", PROPERTY_HINT_NONE, "", read_only), "", "get_player_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "partner_id", PROPERTY_HINT_NONE, "", read_only), "", "get_partner_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "acknowledged", PROPERTY_HINT_NONE, "", read_only), "", "is_acknowledged");
}

String SteamTradeSession::get_player_id() const { return player_id; }
String SteamTradeSession::get_partner_id() const { return partner_id; }
bool SteamTradeSession::is_acknowledged() const { return acknowledged; }

void SteamTradeSessionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamTradeSessionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamTradeSession::get_class_static(), read_only), "", "get_value");
}

Ref<SteamTradeSession> SteamTradeSessionResult::get_value() const { return value; }

Ref<SteamTradeSessionResult> SteamTradeSessionResult::create(Error error, const Ref<SteamTradeSession> &session) {
    Ref<SteamTradeSessionResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = session;
    return result;
}

void SteamPublisherInventory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("items", "player"), &SteamPublisherInventory::items);
    ClassDB::bind_method(D_METHOD("grant", "player", "definition_ids", "request_id"), &SteamPublisherInventory::grant);
    ClassDB::bind_method(D_METHOD("consume", "player", "instance_id", "quantity", "request_id"),
            &SteamPublisherInventory::consume);
    ClassDB::bind_method(D_METHOD("start_trade", "player", "partner"), &SteamPublisherInventory::start_trade);
}

Ref<SteamItemInstance> SteamPublisherInventory::make_item(uint64_t id, uint64_t definition, uint64_t quantity,
        uint64_t flags) {
    Ref<SteamItemInstance> item;
    item.instantiate();
    item->instance_id = String::num_uint64(id);
    item->definition_id = static_cast<int64_t>(definition);
    item->quantity = static_cast<int64_t>(quantity);
    item->flags = static_cast<int64_t>(flags);
    return item;
}

Variant SteamPublisherInventory::items(const Ref<SteamIdentity> &player) {
    if (!publisher_on_main_thread()) {
        return SteamInventorySnapshotResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamInventorySnapshotResult::create(ERR_UNAVAILABLE);
    }
    uint64_t account = 0;
    if (player.is_null() || !player->belongs_to(owner) || !decimal_id(player->get_steam_id(), account)) {
        return SteamInventorySnapshotResult::create(ERR_UNAUTHORIZED);
    }
    const uint64_t app = static_cast<uint64_t>(owner->app_id);
    SteamPublisherRequest request{SteamPublisherEndpoint::GET_INVENTORY};
    add_input_json(request, Json{{"appid", app}, {"steamid", account}});
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(owner, request, [operation, app, account](const SteamPublisherReply &reply) {
        std::vector<InventoryRow> rows;
        const Error status = read_rows(reply, app, account, rows);
        if (status != OK) {
            operation->finish(SteamInventorySnapshotResult::create(status));
            return;
        }
        TypedArray<SteamItemInstance> items;
        for (const auto &row : rows) {
            if (!row.removed) {
                items.push_back(make_item(row.id, row.definition, row.quantity, row.flags));
            }
        }
        items.make_read_only();
        Ref<SteamInventorySnapshot> snapshot;
        snapshot.instantiate();
        snapshot->items = items;
        operation->finish(SteamInventorySnapshotResult::create(OK, {}, snapshot));
    });
    return started == OK ? operation->wait() : Variant(SteamInventorySnapshotResult::create(started));
}

Variant SteamPublisherInventory::mutate(const std::shared_ptr<SteamPublisherState> &issuer,
        const SteamPublisherRequest &request, uint64_t player, uint64_t consumed_instance) {
    auto *host = SteamPublisherHost::resolve(issuer);
    if (!host) {
        return SteamInventoryChangeResult::create(ERR_UNAVAILABLE);
    }
    const uint64_t app = static_cast<uint64_t>(issuer->app_id);
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(issuer, request, [operation, app, player, consumed_instance](
            const SteamPublisherReply &reply) {
        std::vector<InventoryRow> rows;
        const Error status = read_rows(reply, app, player, rows, consumed_instance);
        if (status != OK) {
            operation->finish(SteamInventoryChangeResult::create(status, mutation_message(status)));
            return;
        }
        TypedArray<SteamItemInstance> present;
        PackedStringArray removed;
        for (const auto &row : rows) {
            if (row.removed) {
                removed.push_back(String::num_uint64(row.id));
            } else {
                present.push_back(make_item(row.id, row.definition, row.quantity, row.flags));
            }
        }
        present.make_read_only();
        Ref<SteamInventoryChange> change;
        change.instantiate();
        if (consumed_instance == 0) {
            change->granted = present;
        } else {
            change->changed = present;
        }
        change->removed_instance_ids = removed;
        operation->finish(SteamInventoryChangeResult::create(OK, {}, change));
    });
    return started == OK ? operation->wait() : Variant(SteamInventoryChangeResult::create(started));
}

Variant SteamPublisherInventory::grant(const Ref<SteamIdentity> &player, const PackedInt32Array &definition_ids,
        const String &request_id) {
    if (!publisher_on_main_thread()) {
        return SteamInventoryChangeResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamInventoryChangeResult::create(ERR_UNAVAILABLE);
    }
    uint64_t account = 0;
    if (player.is_null() || !player->belongs_to(owner) || !decimal_id(player->get_steam_id(), account)) {
        return SteamInventoryChangeResult::create(ERR_UNAUTHORIZED);
    }
    uint64_t key = 0;
    if (!decimal_id(request_id, key) || definition_ids.is_empty() || definition_ids.size() > max_grant_definitions) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER);
    }
    Json definitions = Json::array();
    for (int64_t index = 0; index < definition_ids.size(); ++index) {
        const int32_t definition = definition_ids[index];
        if (definition <= 0 || static_cast<uint64_t>(definition) > max_definition) {
            return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER);
        }
        definitions.push_back(definition);
    }
    SteamPublisherRequest request{SteamPublisherEndpoint::GRANT_INVENTORY_ITEM};
    add_input_json(request, Json{{"appid", owner->app_id}, {"steamid", account}, {"itemdefid", definitions},
            {"itempropsjson", "{}"}, {"notify", false}, {"requestid", key}, {"trade_restriction", false}});
    return mutate(owner, request, account);
}

Variant SteamPublisherInventory::consume(const Ref<SteamIdentity> &player, const String &instance_id,
        int64_t quantity, const String &request_id) {
    if (!publisher_on_main_thread()) {
        return SteamInventoryChangeResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamInventoryChangeResult::create(ERR_UNAVAILABLE);
    }
    uint64_t account = 0;
    if (player.is_null() || !player->belongs_to(owner) || !decimal_id(player->get_steam_id(), account)) {
        return SteamInventoryChangeResult::create(ERR_UNAUTHORIZED);
    }
    uint64_t id = 0;
    uint64_t key = 0;
    if (!decimal_id(instance_id, id) || id == invalid_instance || !decimal_id(request_id, key) ||
            quantity <= 0 || static_cast<uint64_t>(quantity) > max_quantity) {
        return SteamInventoryChangeResult::create(ERR_INVALID_PARAMETER);
    }
    SteamPublisherRequest request{SteamPublisherEndpoint::CONSUME_INVENTORY_ITEM};
    add_input_json(request, Json{{"appid", owner->app_id}, {"steamid", account}, {"itemid", id},
            {"quantity", std::to_string(quantity)}, {"requestid", key}});
    return mutate(owner, request, account, id);
}

Variant SteamPublisherInventory::start_trade(const Ref<SteamIdentity> &player, const Ref<SteamIdentity> &partner) {
    if (!publisher_on_main_thread()) {
        return SteamTradeSessionResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamTradeSessionResult::create(ERR_UNAVAILABLE);
    }
    uint64_t first = 0;
    uint64_t second = 0;
    if (player.is_null() || partner.is_null() || !player->belongs_to(owner) || !partner->belongs_to(owner) ||
            !decimal_id(player->get_steam_id(), first) || !decimal_id(partner->get_steam_id(), second)) {
        return SteamTradeSessionResult::create(ERR_UNAUTHORIZED);
    }
    if (first == second) {
        return SteamTradeSessionResult::create(ERR_INVALID_PARAMETER);
    }
    const uint64_t app = static_cast<uint64_t>(owner->app_id);
    SteamPublisherRequest request{SteamPublisherEndpoint::START_INVENTORY_TRADE};
    request.add("appid", String::num_uint64(app));
    request.add("partya", player->get_steam_id());
    request.add("partyb", partner->get_steam_id());
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(owner, request, [operation, app, first, second](const SteamPublisherReply &reply) {
        Json body;
        const Json *response = nullptr;
        bool acknowledged = false;
        const Error received = envelope(reply, body, response, acknowledged);
        if (received != OK || !acknowledged || !matches_echo(response, "appid", app) ||
                !matches_echo(response, "partya", first) || !matches_echo(response, "partyb", second)) {
            operation->finish(SteamTradeSessionResult::create(received == OK ? ERR_INVALID_DATA : received));
            return;
        }
        Ref<SteamTradeSession> session;
        session.instantiate();
        session->player_id = String::num_uint64(first);
        session->partner_id = String::num_uint64(second);
        session->acknowledged = true;
        operation->finish(SteamTradeSessionResult::create(OK, session));
    });
    return started == OK ? operation->wait() : Variant(SteamTradeSessionResult::create(started));
}

} // namespace crimson::steam
