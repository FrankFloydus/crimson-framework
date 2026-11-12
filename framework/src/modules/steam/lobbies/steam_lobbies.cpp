// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/lobbies/steam_lobbies.hpp"
#include "modules/steam/server/steam_server_route.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <string>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>
#include <utility>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Return a decimal representation without floating-point conversion.
String lobby_id_text(uint64_t id) {
    return String(std::to_string(id).c_str());
}

#ifdef CRIMSON_WITH_STEAM

// Valve allows one pending search on the Steam client pipe.
struct LobbySearch {
    // Runtime which owns this exact native call.
    std::weak_ptr<SteamRuntime> runtime;
    // Caller observation, which may cancel before the SDK operation settles.
    Ref<SteamListingsOperation> operation;
    // Service which submitted the immutable query.
    ObjectID owner;
    // Native async call handle retained until its result or deadline.
    SteamAPICall_t call = k_uAPICallInvalid;
    // Monotonic time after Valve's documented search timeout.
    uint64_t deadline = 0;
};

// Exact outstanding search shared across all Steam lobby discovery scopes.
std::shared_ptr<LobbySearch> pending_search;

// Read a bounded SDK string before crossing a callback or gameplay boundary.
String read_metadata(ISteamMatchmaking &api, CSteamID id, const char *key, size_t limit = 127) {
    const char *text = api.GetLobbyData(id, key);
    if (!text) {
        return {};
    }
    size_t size = 0;
    while (size <= limit && text[size] != '\0') {
        ++size;
    }
    return size <= limit ? String::utf8(text, static_cast<int64_t>(size)) : String();
}

// Parse a Steam ID from bounded decimal text before SDK type checks.
bool parse_decimal_id(const String &text, uint64_t &id) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString ascii = text.utf8();
    const char *first = ascii.get_data();
    const char *end = first + ascii.length();
    const auto parsed = std::from_chars(first, end, id);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

// Copy an eligible published lobby while Steam's current result indices are valid.
Ref<SteamListing> search_snapshot(SteamRuntime &runtime, CSteamID id, const StringName &name_space,
        const StringName &map, int32_t seats, bool social) {
    if (!id.IsValid() || !id.IsLobby()) {
        return {};
    }
    ISteamMatchmaking &api = *runtime.get_matchmaking();
    const int32_t count = api.GetNumLobbyMembers(id);
    const int32_t capacity = api.GetLobbyMemberLimit(id);
    if (count < 0 || count > 65 || capacity < 0 || capacity > 65 ||
            (capacity > 0 && count > 0 && capacity - count < seats) ||
            read_metadata(api, id, "crimson_protocol") != (social ? STEAM_SOCIAL_LOBBY_PROTOCOL : STEAM_GAME_LOBBY_PROTOCOL) ||
            read_metadata(api, id, "crimson_namespace") != String(name_space)) {
        return {};
    }
    const String world = social ? String() : read_metadata(api, id, "crimson_world", MAX_LOBBY_WORLD_BYTES);
    uint64_t advertised_owner = 0;
    // Steam exposes the actual owner only to members. Search metadata is advisory.
    if (!social && (!parse_steam_lobby_account(read_metadata(api, id, "crimson_owner"), advertised_owner) ||
                           !valid_steam_lobby_world(world) || (!map.is_empty() && world != String(map)))) {
        return {};
    }
    return SteamListing::lobby(id.ConvertToUint64(), runtime.get_app_id(), name_space, StringName(world),
            read_metadata(api, id, "crimson_name"), count, capacity, social);
}

#endif

} // namespace

bool valid_steam_lobby_namespace(const String &value) {
    if (value.is_empty() || value.length() > 64) {
        return false;
    }
    for (int64_t index = 0; index < value.length(); ++index) {
        const char32_t c = value[index];
        if (!((c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') ||
                    (c >= U'a' && c <= U'z') || c == U'_' || c == U'-')) {
            return false;
        }
    }
    return true;
}

bool valid_steam_lobby_world(const String &value) {
    if (value.is_empty() || value.length() > 128 || value.utf8().length() > MAX_LOBBY_WORLD_BYTES) {
        return false;
    }
    for (int64_t index = 0; index < value.length(); ++index) {
        if (value[index] < U' ' || value[index] == 127) {
            return false;
        }
    }
    return true;
}

bool parse_steam_lobby_account(const String &text, uint64_t &id) {
#ifdef CRIMSON_WITH_STEAM
    if (parse_decimal_id(text, id)) {
        const CSteamID account(id);
        return account.IsValid() && account.GetEAccountType() == k_EAccountTypeIndividual;
    }
#endif
    return false;
}

bool parse_steam_lobby_id(const String &text, uint64_t &id) {
#ifdef CRIMSON_WITH_STEAM
    if (parse_decimal_id(text, id)) {
        const CSteamID lobby(id);
        return lobby.IsValid() && lobby.IsLobby();
    }
#endif
    return false;
}

void SteamListing::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_kind"), &SteamListing::get_kind);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamListing::get_app_id);
    ClassDB::bind_method(D_METHOD("get_namespace"), &SteamListing::get_namespace);
    ClassDB::bind_method(D_METHOD("get_world_id"), &SteamListing::get_world_id);
    ClassDB::bind_method(D_METHOD("get_name"), &SteamListing::get_name);
    ClassDB::bind_method(D_METHOD("get_players"), &SteamListing::get_players);
    ClassDB::bind_method(D_METHOD("get_capacity"), &SteamListing::get_capacity);
    ClassDB::bind_method(D_METHOD("get_server_steam_id"), &SteamListing::get_server_steam_id);
    ClassDB::bind_method(D_METHOD("get_ping_ms"), &SteamListing::get_ping_ms);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind"), "", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id"), "", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "namespace"), "", "get_namespace");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "world_id"), "", "get_world_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "", "get_name");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "players"), "", "get_players");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "capacity"), "", "get_capacity");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "server_steam_id"), "", "get_server_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ping_ms"), "", "get_ping_ms");
    ClassDB::bind_method(D_METHOD("is_social"), &SteamListing::is_social);
    BIND_ENUM_CONSTANT(LOBBY);
    BIND_ENUM_CONSTANT(SERVER);
}

SteamListing::Kind SteamListing::get_kind() const { return kind; }
int64_t SteamListing::get_app_id() const { return app_id; }
StringName SteamListing::get_namespace() const { return name_space; }
StringName SteamListing::get_world_id() const { return world_id; }
String SteamListing::get_name() const { return name; }
int32_t SteamListing::get_players() const { return players; }
int32_t SteamListing::get_capacity() const { return capacity; }
String SteamListing::get_server_steam_id() const { return server_steam_id; }
int32_t SteamListing::get_ping_ms() const { return ping_ms; }
uint16_t SteamListing::get_server_query_port() const { return server_query_port; }
bool SteamListing::is_social() const { return social; }

Ref<SteamListing> SteamListing::lobby(uint64_t id, uint32_t app, const StringName &scope,
        const StringName &world, const String &label, int32_t count, int32_t limit, bool social) {
    Ref<SteamListing> listing;
    listing.instantiate();
    listing->set_locator("steam", lobby_id_text(id));
    listing->app_id = app;
    listing->name_space = scope;
    listing->world_id = world;
    listing->name = label;
    listing->players = count;
    listing->capacity = limit;
    listing->social = social;
    return listing;
}

Ref<SteamListing> SteamListing::party_lobby(uint64_t id, uint64_t owner, uint32_t app, const StringName &scope) {
    const auto result = lobby(id, app, scope);
    result->set_locator("steam", String("party1:") + lobby_id_text(id) + ":" + String::num_uint64(owner));
    return result;
}

Ref<SteamListing> SteamListing::server(const SteamServerRoute &route, uint32_t app, const StringName &scope,
        const StringName &world, const String &label, int32_t count, int32_t limit, int32_t ping, uint16_t query_port) {
    const String id = route.listing_id(app, scope);
    if (id.is_empty()) {
        return {};
    }
    Ref<SteamListing> listing;
    listing.instantiate();
    listing->set_locator("steam", id);
    listing->kind = SERVER;
    listing->app_id = app;
    listing->name_space = scope;
    listing->world_id = world;
    listing->name = label;
    listing->players = count;
    listing->capacity = limit;
    listing->server_steam_id = lobby_id_text(route.server_id);
    listing->ping_ms = ping;
    listing->server_query_port = query_port;
    return listing;
}

void SteamListingResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamListingResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_NO_INSTANCE_STATE, "SteamListing"),
            "", "get_value");
}

Ref<SteamListing> SteamListingResult::get_value() const { return value; }

Ref<SteamListingResult> SteamListingResult::from_listing(Error error, const Ref<SteamListing> &listing) {
    Ref<SteamListingResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = listing;
    return result;
}

void SteamListingsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamListingsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, "SteamListing",
                         PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_NO_INSTANCE_STATE), "", "get_value");
}

TypedArray<SteamListing> SteamListingsResult::get_value() const { return value.duplicate(); }

Ref<SteamListingsResult> SteamListingsResult::from_listings(Error error, const TypedArray<SteamListing> &listings) {
    Ref<SteamListingsResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = listings;
    return result;
}

void SteamListingsOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamListingsOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamListingsOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamListingsOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamListingsOperation::cancel);
    // Publish the exact terminal result to observers waiting on this operation.
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE, "",
                                              PROPERTY_USAGE_DEFAULT, "SteamListingsResult")));
}

bool SteamListingsOperation::is_finished() const { return terminal_result.is_valid(); }
Ref<SteamListingsResult> SteamListingsOperation::result() const { return terminal_result; }

Variant SteamListingsOperation::wait() {
    return terminal_result.is_valid() ? Variant(terminal_result) : Variant(Signal(this, "completed"));
}

Error SteamListingsOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (cancel_hook) {
        const auto cancel = cancel_hook;
        return cancel();
    }
    if (pending_search && pending_search->operation.ptr() == this && terminal_result.is_null()) {
        const auto observer = std::exchange(pending_search->operation, {});
        observer->finish(SteamListingsResult::from_listings(ERR_SKIP));
        return OK;
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

Ref<SteamListingsOperation> SteamListingsOperation::rejected(Error error) {
    Ref<SteamListingsOperation> operation;
    operation.instantiate();
    operation->finish(SteamListingsResult::from_listings(error));
    return operation;
}

void SteamListingsOperation::finish(const Ref<SteamListingsResult> &completed) {
    if (terminal_result.is_null()) {
        const Ref<SteamListingsOperation> keep_alive(this);
        cancel_hook = {};
        terminal_result = completed;
        emit_signal("completed", terminal_result);
    }
}

void SteamLobbyQuery::_bind_methods() {
    ClassDB::bind_method(D_METHOD("on_map", "world_id"), &SteamLobbyQuery::on_map);
    ClassDB::bind_method(D_METHOD("with_room_for", "players"), &SteamLobbyQuery::with_room_for);
    ClassDB::bind_method(D_METHOD("within", "distance"), &SteamLobbyQuery::within);
    ClassDB::bind_method(D_METHOD("limit", "count"), &SteamLobbyQuery::limit);
    ClassDB::bind_method(D_METHOD("social"), &SteamLobbyQuery::social);
    ClassDB::bind_method(D_METHOD("fetch"), &SteamLobbyQuery::fetch);
    ClassDB::bind_method(D_METHOD("fetch_operation"), &SteamLobbyQuery::fetch_operation);
    BIND_ENUM_CONSTANT(CLOSE);
    BIND_ENUM_CONSTANT(NEARBY);
    BIND_ENUM_CONSTANT(FAR);
    BIND_ENUM_CONSTANT(WORLDWIDE);
}

Ref<SteamLobbyQuery> SteamLobbyQuery::branch() const {
    Ref<SteamLobbyQuery> next;
    next.instantiate();
    next->service_id = service_id;
    next->name_space = name_space;
    next->social_only = social_only;
    next->world = world;
    next->open_seats = open_seats;
    next->distance = distance;
    next->maximum_results = maximum_results;
    next->valid = valid;
    return next;
}

Ref<SteamLobbyQuery> SteamLobbyQuery::on_map(const StringName &world_id) const {
    const auto next = branch();
    next->valid = valid && valid_steam_lobby_world(String(world_id));
    if (next->valid) {
        next->world = world_id;
    }
    return next;
}

Ref<SteamLobbyQuery> SteamLobbyQuery::with_room_for(int64_t players) const {
    const auto next = branch();
    next->valid = valid && players >= 1 && players <= 64;
    if (next->valid) {
        next->open_seats = static_cast<int32_t>(players);
    }
    return next;
}

Ref<SteamLobbyQuery> SteamLobbyQuery::within(Distance preference) const {
    const auto next = branch();
    next->valid = valid && preference >= CLOSE && preference <= WORLDWIDE;
    next->distance = preference;
    return next;
}

Ref<SteamLobbyQuery> SteamLobbyQuery::limit(int64_t count) const {
    const auto next = branch();
    next->valid = valid && count >= 1 && count <= 50;
    if (next->valid) {
        next->maximum_results = static_cast<int32_t>(count);
    }
    return next;
}

Ref<SteamLobbyQuery> SteamLobbyQuery::social() const {
    const auto next = branch();
    next->social_only = true;
    return next;
}

Variant SteamLobbyQuery::fetch() const { return fetch_operation()->wait(); }

Ref<SteamListingsOperation> SteamLobbyQuery::fetch_operation() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const Ref<SteamLobbies> service(Object::cast_to<SteamLobbies>(ObjectDB::get_instance(service_id)));
        if (service.is_valid()) {
            return service->start_search(*this);
        }
    }
#endif
    return SteamListingsOperation::rejected(ERR_UNAVAILABLE);
}

void SteamLobbies::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamLobbies::is_available);
    ClassDB::bind_method(D_METHOD("current"), &SteamLobbies::current);
    ClassDB::bind_method(D_METHOD("query", "namespace"), &SteamLobbies::query, DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("friend_lobby", "friend_id"), &SteamLobbies::friend_lobby);
    ClassDB::bind_method(D_METHOD("create", "options"), &SteamLobbies::create, DEFVAL(Ref<SteamLobbyOptions>()));
    ClassDB::bind_method(D_METHOD("join", "listing"), &SteamLobbies::join);
    // Observe acceptance in Steam without changing membership.
    ADD_SIGNAL(MethodInfo("invite_received", PropertyInfo(Variant::OBJECT, "listing", PROPERTY_HINT_NONE, "",
                                               PROPERTY_USAGE_DEFAULT, "SteamListing")));
}

bool SteamLobbies::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto active = scope.lock();
        const auto sdk = runtime.lock();
        return active && active->active && sdk && sdk->get_matchmaking();
    }
#endif
    return false;
}

Ref<SteamLobby> SteamLobbies::current() const {
    return is_available() && active_lobby.is_valid() && active_lobby->live_lobby(runtime.lock()) != 0 ?
            active_lobby : Ref<SteamLobby>();
}

Ref<SteamLobbyQuery> SteamLobbies::query(const StringName &requested_namespace) const {
    Ref<SteamLobbyQuery> query;
    query.instantiate();
    query->service_id = ObjectID(get_instance_id());
    query->name_space = requested_namespace.is_empty() ? name_space : requested_namespace;
    query->valid = valid_steam_lobby_namespace(String(query->name_space)) &&
            (!game_managed || query->name_space == name_space);
    return query;
}

Ref<SteamListingResult> SteamLobbies::friend_lobby(const String &friend_id) const {
#ifdef CRIMSON_WITH_STEAM
    if (is_available()) {
        const auto sdk = runtime.lock();
        uint64_t id = 0;
        if (!parse_steam_lobby_account(friend_id, id)) {
            return SteamListingResult::from_listing(ERR_INVALID_PARAMETER);
        }
        FriendGameInfo_t game{};
        if (!sdk->get_friends() || !sdk->get_friends()->GetFriendGamePlayed(CSteamID(id), &game) ||
                game.m_gameID.AppID() != sdk->get_app_id() || !game.m_steamIDLobby.IsValid() ||
                !game.m_steamIDLobby.IsLobby()) {
            return SteamListingResult::from_listing(ERR_DOES_NOT_EXIST);
        }
        const String known_namespace = read_metadata(*sdk->get_matchmaking(), game.m_steamIDLobby, "crimson_namespace");
        if (!known_namespace.is_empty() && (!valid_steam_lobby_namespace(known_namespace) ||
                                                (game_managed && known_namespace != String(name_space)))) {
            return SteamListingResult::from_listing(ERR_DOES_NOT_EXIST);
        }
        const bool social = read_metadata(*sdk->get_matchmaking(), game.m_steamIDLobby, "crimson_protocol") ==
                STEAM_SOCIAL_LOBBY_PROTOCOL;
        if (game_managed && social) {
            return SteamListingResult::from_listing(ERR_DOES_NOT_EXIST);
        }
        const StringName discovered_namespace = known_namespace.is_empty() && game_managed ?
                name_space : StringName(known_namespace);
        return SteamListingResult::from_listing(OK, SteamListing::lobby(game.m_steamIDLobby.ConvertToUint64(),
                sdk->get_app_id(), discovered_namespace, {}, {}, 0, 0, social));
    }
#endif
    return SteamListingResult::from_listing(ERR_UNAVAILABLE);
}

void SteamLobbies::cancel_search() {
#ifdef CRIMSON_WITH_STEAM
    if (pending_search && pending_search->operation.is_valid() &&
            pending_search->owner == ObjectID(get_instance_id())) {
        const auto operation = pending_search->operation;
        operation->cancel();
    }
#endif
}

void SteamLobbies::poll() {
#ifdef CRIMSON_WITH_STEAM
    if (!pending_search) {
        return;
    }
    const auto sdk = pending_search->runtime.lock();
    if (sdk && Time::get_singleton()->get_ticks_msec() < pending_search->deadline) {
        return;
    }
    const auto expired = std::exchange(pending_search, {});
    if (sdk) {
        sdk->unwatch_call(expired->call);
    }
    if (expired->operation.is_valid()) {
        expired->operation->finish(SteamListingsResult::from_listings(ERR_TIMEOUT));
    }
#endif
}

Ref<SteamListingsOperation> SteamLobbies::start_search(const SteamLobbyQuery &query) {
#ifdef CRIMSON_WITH_STEAM
    poll();
    if (is_available()) {
        if (!query.valid || (query.social_only && (game_managed || !query.world.is_empty()))) {
            return SteamListingsOperation::rejected(ERR_INVALID_PARAMETER);
        }
        const auto sdk = runtime.lock();
        if (pending_search || !sdk->can_track_call()) {
            return SteamListingsOperation::rejected(ERR_BUSY);
        }
        auto search = std::make_shared<LobbySearch>();
        search->runtime = sdk;
        search->operation.instantiate();
        search->owner = ObjectID(get_instance_id());
        search->deadline = Time::get_singleton()->get_ticks_msec() + 22000;
        const StringName selected_namespace = query.name_space;
        const bool social = query.social_only;
        const StringName world = query.world;
        const int32_t seats = query.open_seats;
        const int32_t limit = query.maximum_results;
        ISteamMatchmaking &api = *sdk->get_matchmaking();
        api.AddRequestLobbyListStringFilter("crimson_protocol",
                social ? STEAM_SOCIAL_LOBBY_PROTOCOL : STEAM_GAME_LOBBY_PROTOCOL, k_ELobbyComparisonEqual);
        api.AddRequestLobbyListStringFilter("crimson_namespace", String(selected_namespace).utf8().get_data(),
                k_ELobbyComparisonEqual);
        if (!world.is_empty()) {
            api.AddRequestLobbyListStringFilter("crimson_world", String(world).utf8().get_data(),
                    k_ELobbyComparisonEqual);
        }
        api.AddRequestLobbyListFilterSlotsAvailable(seats);
        api.AddRequestLobbyListDistanceFilter(static_cast<ELobbyDistanceFilter>(query.distance));
        api.AddRequestLobbyListResultCountFilter(limit);
        search->call = api.RequestLobbyList();
        if (!sdk->watch_call(search->call, LobbyMatchList_t::k_iCallback, sizeof(LobbyMatchList_t),
                    [search, selected_namespace, world, seats, limit, social](const void *data) {
                        if (pending_search != search) {
                            return;
                        }
                        pending_search.reset();
                        if (search->operation.is_null()) {
                            return;
                        }
                        if (Time::get_singleton()->get_ticks_msec() >= search->deadline) {
                            search->operation->finish(SteamListingsResult::from_listings(ERR_TIMEOUT));
                            return;
                        }
                        const auto active = search->runtime.lock();
                        if (!data || !active) {
                            search->operation->finish(SteamListingsResult::from_listings(ERR_CANT_CONNECT));
                            return;
                        }
                        LobbyMatchList_t matches{};
                        std::memcpy(&matches, data, sizeof(matches));
                        const int32_t count = static_cast<int32_t>(std::min(matches.m_nLobbiesMatching,
                                static_cast<uint32_t>(limit)));
                        TypedArray<SteamListing> listings;
                        std::set<uint64_t> seen;
                        for (int32_t index = 0; index < count; ++index) {
                            const CSteamID lobby = active->get_matchmaking()->GetLobbyByIndex(index);
                            const auto listing = search_snapshot(*active, lobby, selected_namespace, world, seats, social);
                            if (listing.is_valid() && seen.insert(lobby.ConvertToUint64()).second) {
                                listings.push_back(listing);
                            }
                        }
                        search->operation->finish(SteamListingsResult::from_listings(OK, listings));
                    })) {
            return SteamListingsOperation::rejected(ERR_CANT_CONNECT);
        }
        pending_search = search;
        return search->operation;
    }
#endif
    return SteamListingsOperation::rejected(ERR_UNAVAILABLE);
}

} // namespace crimson::steam
