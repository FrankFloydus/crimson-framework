// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_server_browser.hpp"

#include "modules/steam/server/steam_server_route.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <array>
#include <set>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Keep both kinds of discovery work bounded even if Steam never completes.
bool valid_timeout(int64_t milliseconds) {
    return milliseconds >= 100 && milliseconds <= 15000;
}

#ifdef CRIMSON_WITH_STEAM

// Bound queries shared by standalone and Game client scopes on the process pipe.
size_t pending_lists = 0;
size_t pending_inspections = 0;
constexpr size_t maximum_lists = 4;
constexpr size_t maximum_inspections = 8;
constexpr size_t maximum_responses = 256;

// Copy a terminated SDK string without reading beyond the native field.
String copy_sdk_text(const char *bytes, size_t capacity) {
    size_t length = 0;
    while (length < capacity && bytes[length] != '\0') {
        ++length;
    }
    String text;
    return length < capacity && text.parse_utf8(bytes, static_cast<int64_t>(length)) == OK ? text : String();
}

// SDK details are discovery observations. The connection later authenticates this locator.
Ref<SteamListing> copy_listing(const gameserveritem_t &entry, uint32_t app, const StringName &name_space,
        const StringName &world, int32_t seats, int32_t maximum_ping, PackedStringArray &tags) {
    const SteamServerRoute route{entry.m_steamID.ConvertToUint64(),
        entry.m_NetAdr.GetIP(), entry.m_NetAdr.GetConnectionPort()};
    const uint16_t query_port = entry.m_NetAdr.GetQueryPort();
    if (entry.m_nAppID != app || !entry.m_bHadSuccessfulResponse || entry.m_bPassword || !route.is_valid() ||
            query_port == 0 || entry.m_nMaxPlayers < 1 || entry.m_nMaxPlayers > 64 ||
            entry.m_nPlayers < 0 || entry.m_nPlayers > entry.m_nMaxPlayers ||
            entry.m_nMaxPlayers - entry.m_nPlayers < seats || entry.m_nPing < 0 || entry.m_nPing > 60000 ||
            (maximum_ping > 0 && entry.m_nPing > maximum_ping)) {
        return {};
    }
    tags = copy_sdk_text(entry.m_szGameTags, sizeof(entry.m_szGameTags)).split(",", false);
    if (!tags.has(STEAM_DIRECT_SERVER_PROTOCOL) || !tags.has(steam_server_namespace_tag(name_space)) ||
            (!world.is_empty() && !tags.has(steam_server_world_tag(world)))) {
        return {};
    }
    StringName selected_world = world;
    if (selected_world.is_empty()) {
        const String map = copy_sdk_text(entry.m_szMap, sizeof(entry.m_szMap));
        if (valid_steam_lobby_world(map) && tags.has(steam_server_world_tag(StringName(map)))) {
            selected_world = StringName(map);
        }
    }
    return SteamListing::server(route, app, name_space, selected_world,
            copy_sdk_text(entry.GetName(), k_cbMaxGameServerName), entry.m_nPlayers,
            entry.m_nMaxPlayers, entry.m_nPing, query_port);
}

// SDK-owned request entry point, with native snapshots ready for the next Godot tick.
class ListRequest final : public ISteamMatchmakingServerListResponse {
public:
    // Keep the SDK pipe alive until the query handle has been released.
    std::shared_ptr<SteamRuntime> sdk;
    // Exact matchmaking servers interface that created this request.
    ISteamMatchmakingServers *api = nullptr;
    // Steam handle returned from the request, including during early callbacks.
    HServerListRequest handle = nullptr;
    // Handle observed by a callback before submission returns.
    HServerListRequest observed = nullptr;
    // Caller observer, settled after native cleanup.
    Ref<SteamListingsOperation> operation;
    // Monotonic caller deadline for these detached settings.
    uint64_t deadline = 0;
    // Active project's app and namespace for validation.
    uint32_t app = 0;
    StringName name_space;
    // Optional stable world and occupancy/latency filters.
    StringName world;
    int32_t seats = 1;
    int32_t maximum_ping = 0;
    // Number of verified observations to retain.
    int32_t limit = 50;
    // Stop processing after a bounded number of raw vendor responses.
    size_t responses = 0;
    // Distinguish an ordinary empty search from a transport error.
    bool done = false;
    Error error = OK;
    // Unique observed listings and their detached snapshots.
    std::set<String> seen;
    TypedArray<SteamListing> values;
    // Owned SDK filters, retained until the exact Internet query releases them.
    std::array<MatchMakingKeyValuePair_t, 2> filters;
    // Filter pointers into this request's own storage.
    std::array<MatchMakingKeyValuePair_t *, 2> filter_pointers{&filters[0], &filters[1]};

    // Release even a completed list handle exactly once.
    ~ListRequest() {
        done = true;
        if (handle) {
            api->ReleaseRequest(std::exchange(handle, nullptr));
        }
        --pending_lists;
    }

    // Copy valid details during the exact native response callback.
    void ServerResponded(HServerListRequest request, int index) override {
        if (!matches(request) || index < 0 || index >= api->GetServerCount(request)) {
            return;
        }
        ++responses;
        const auto *details = api->GetServerDetails(request, index);
        if (details) {
            PackedStringArray tags;
            const auto listing = copy_listing(*details, app, name_space, world, seats, maximum_ping, tags);
            if (listing.is_valid() && seen.insert(listing->get_id()).second) {
                values.push_back(listing);
            }
        }
        if (values.size() >= limit || responses >= maximum_responses) {
            done = true;
        }
    }

    // Bound work regardless of whether a particular candidate answered.
    void ServerFailedToRespond(HServerListRequest request, int) override {
        if (matches(request) && ++responses >= maximum_responses) {
            done = true;
        }
    }

    // Record only this exact SDK search completion and leave dispatch to Godot.
    void RefreshComplete(HServerListRequest request, EMatchMakingServerResponse status) override {
        if (matches(request)) {
            done = true;
            if (status == eServerFailedToRespond && values.is_empty()) {
                error = ERR_CANT_CONNECT;
            }
        }
    }

private:
    // Verify native ownership and the caller deadline before snapshotting any response.
    bool matches(HServerListRequest request) {
        if (!SteamRuntime::on_main_thread() || done || !request || (handle && handle != request) ||
                (observed && observed != request) || Time::get_singleton()->get_ticks_msec() >= deadline) {
            return false;
        }
        observed = request;
        return true;
    }
};

// Own one native ping callback and its exact selected locator.
class InspectRequest final : public ISteamMatchmakingPingResponse {
public:
    // Keep the originating SDK pipe alive through cancellation.
    std::shared_ptr<SteamRuntime> sdk;
    // SDK interface that submitted this exact inspection.
    ISteamMatchmakingServers *api = nullptr;
    // Steam handle retained only while the response may still arrive.
    HServerQuery handle = HSERVERQUERY_INVALID;
    // Exact typed observer for this selection.
    Ref<SteamServerDetailsOperation> operation;
    // Selected listing captured before the direct Steam query.
    Ref<SteamListing> selected;
    // Absolute monotonic observation deadline.
    uint64_t deadline = 0;
    // Whether the SDK consumed its pending native query.
    bool native_done = false;
    // Terminal error if the selected query did not produce a valid response.
    Error error = OK;
    // Fresh server and metadata snapshots, without any SDK-owned pointers.
    Ref<SteamListing> listing;
    PackedStringArray tags;

    // Steam consumes completed ping handles. Cancel only while still outstanding.
    ~InspectRequest() {
        if (!native_done && handle != HSERVERQUERY_INVALID) {
            api->CancelServerQuery(std::exchange(handle, HSERVERQUERY_INVALID));
        }
        --pending_inspections;
    }

    // Verify the advertised identity, address and query port again before copying.
    void ServerResponded(gameserveritem_t &entry) override {
        if (!SteamRuntime::on_main_thread() || std::exchange(native_done, true)) {
            return;
        }
        if (Time::get_singleton()->get_ticks_msec() >= deadline) {
            error = ERR_TIMEOUT;
            return;
        }
        const auto fresh = copy_listing(entry, static_cast<uint32_t>(selected->get_app_id()),
                selected->get_namespace(), {}, 0, 0, tags);
        if (fresh.is_null() || fresh->get_id() != selected->get_id() ||
                fresh->get_server_query_port() != selected->get_server_query_port()) {
            error = ERR_INVALID_DATA;
            return;
        }
        listing = fresh;
    }

    // Record a vendor failure without invoking a script inside its callback.
    void ServerFailedToRespond() override {
        if (SteamRuntime::on_main_thread() && !std::exchange(native_done, true)) {
            error = Time::get_singleton()->get_ticks_msec() >= deadline ? ERR_TIMEOUT : ERR_CANT_CONNECT;
        }
    }
};

#endif

} // namespace

struct SteamServerBrowser::State {
#ifdef CRIMSON_WITH_STEAM
    // Optional matchmaking server interface resolved from this client pipe.
    ISteamMatchmakingServers *api = nullptr;
    // Exact outstanding list and details requests for this facade.
    std::unique_ptr<ListRequest> list;
    std::unique_ptr<InspectRequest> inspect;
#endif
};

void SteamServerDetails::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_listing"), &SteamServerDetails::get_listing);
    ClassDB::bind_method(D_METHOD("get_ping_ms"), &SteamServerDetails::get_ping_ms);
    ClassDB::bind_method(D_METHOD("get_tags"), &SteamServerDetails::get_tags);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "listing", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_NO_INSTANCE_STATE, "SteamListing"), "", "get_listing");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ping_ms"), "", "get_ping_ms");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "tags"), "", "get_tags");
}

Ref<SteamListing> SteamServerDetails::get_listing() const { return listing; }
int32_t SteamServerDetails::get_ping_ms() const { return listing.is_valid() ? listing->get_ping_ms() : -1; }
PackedStringArray SteamServerDetails::get_tags() const { return tags; }

void SteamServerDetailsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamServerDetailsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_NO_INSTANCE_STATE, "SteamServerDetails"), "", "get_value");
}

Ref<SteamServerDetails> SteamServerDetailsResult::get_value() const { return value; }

void SteamServerDetailsOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamServerDetailsOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamServerDetailsOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamServerDetailsOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamServerDetailsOperation::cancel);
    // Publish a terminal result after retiring the exact native query.
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE, "",
                                              PROPERTY_USAGE_DEFAULT, "SteamServerDetailsResult")));
}

bool SteamServerDetailsOperation::is_finished() const { return terminal_result.is_valid(); }
Ref<SteamServerDetailsResult> SteamServerDetailsOperation::result() const { return terminal_result; }

Variant SteamServerDetailsOperation::wait() {
    return terminal_result.is_valid() ? Variant(terminal_result) : Variant(Signal(this, "completed"));
}

Error SteamServerDetailsOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (cancel_hook) {
        const auto cancel = cancel_hook;
        return cancel();
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

Ref<SteamServerDetailsOperation> SteamServerDetailsOperation::rejected(Error error) {
    Ref<SteamServerDetailsOperation> operation;
    operation.instantiate();
    operation->finish(error);
    return operation;
}

void SteamServerDetailsOperation::finish(Error error, const Ref<SteamServerDetails> &details) {
    if (terminal_result.is_null()) {
        const Ref<SteamServerDetailsOperation> keep_alive(this);
        cancel_hook = {};
        terminal_result.instantiate();
        terminal_result->set_outcome(error);
        terminal_result->value = details;
        emit_signal("completed", terminal_result);
    }
}

void SteamServerQuery::_bind_methods() {
    ClassDB::bind_method(D_METHOD("on_map", "world_id"), &SteamServerQuery::on_map);
    ClassDB::bind_method(D_METHOD("with_room_for", "players"), &SteamServerQuery::with_room_for);
    ClassDB::bind_method(D_METHOD("under_latency", "milliseconds"), &SteamServerQuery::under_latency);
    ClassDB::bind_method(D_METHOD("limit", "count"), &SteamServerQuery::limit);
    ClassDB::bind_method(D_METHOD("fetch", "timeout_ms"), &SteamServerQuery::fetch, DEFVAL(5000));
    ClassDB::bind_method(D_METHOD("fetch_operation", "timeout_ms"), &SteamServerQuery::fetch_operation, DEFVAL(5000));
}

Ref<SteamServerQuery> SteamServerQuery::branch() const {
    Ref<SteamServerQuery> next;
    next.instantiate();
    next->browser_id = browser_id;
    next->source = source;
    next->name_space = name_space;
    next->world = world;
    next->seats = seats;
    next->maximum_ping = maximum_ping;
    next->maximum_results = maximum_results;
    next->valid = valid;
    return next;
}

Ref<SteamServerQuery> SteamServerQuery::on_map(const StringName &world_id) const {
    const auto next = branch();
    next->valid = valid && valid_steam_lobby_world(String(world_id));
    next->world = world_id;
    return next;
}

Ref<SteamServerQuery> SteamServerQuery::with_room_for(int64_t players) const {
    const auto next = branch();
    next->valid = valid && players >= 1 && players <= 64;
    if (next->valid) {
        next->seats = static_cast<int32_t>(players);
    }
    return next;
}

Ref<SteamServerQuery> SteamServerQuery::under_latency(int64_t milliseconds) const {
    const auto next = branch();
    next->valid = valid && milliseconds >= 1 && milliseconds <= 60000;
    if (next->valid) {
        next->maximum_ping = static_cast<int32_t>(milliseconds);
    }
    return next;
}

Ref<SteamServerQuery> SteamServerQuery::limit(int64_t count) const {
    const auto next = branch();
    next->valid = valid && count >= 1 && count <= 50;
    if (next->valid) {
        next->maximum_results = static_cast<int32_t>(count);
    }
    return next;
}

Variant SteamServerQuery::fetch(int64_t timeout_ms) const { return fetch_operation(timeout_ms)->wait(); }

Ref<SteamListingsOperation> SteamServerQuery::fetch_operation(int64_t timeout_ms) const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const Ref<SteamServerBrowser> browser(Object::cast_to<SteamServerBrowser>(ObjectDB::get_instance(browser_id)));
        if (browser.is_valid()) {
            return browser->start_query(*this, timeout_ms);
        }
    }
#endif
    return SteamListingsOperation::rejected(ERR_UNAVAILABLE);
}

SteamServerBrowser::SteamServerBrowser() : state(std::make_unique<State>()) {}
SteamServerBrowser::~SteamServerBrowser() = default;

void SteamServerBrowser::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamServerBrowser::is_available);
    ClassDB::bind_method(D_METHOD("query", "source", "namespace"), &SteamServerBrowser::query,
            DEFVAL(SteamServer::INTERNET), DEFVAL(StringName()));
    ClassDB::bind_method(D_METHOD("inspect", "listing", "timeout_ms"), &SteamServerBrowser::inspect, DEFVAL(3000));
}

void SteamServerBrowser::attach(const std::shared_ptr<SteamRuntime> &sdk,
        const std::shared_ptr<SteamClientScope> &attachment, const StringName &name_space) {
    runtime = sdk;
    scope = attachment;
    game_namespace = name_space;
#ifdef CRIMSON_WITH_STEAM
    const auto get = sdk->symbol<decltype(&SteamAPI_SteamMatchmakingServers_v003)>(
            "SteamAPI_SteamMatchmakingServers_v003");
    state->api = get ? get() : nullptr;
#endif
}

bool SteamServerBrowser::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto live = scope.lock();
        return live && live->active && !runtime.expired() && state->api;
    }
#endif
    return false;
}

Ref<SteamServerQuery> SteamServerBrowser::query(SteamServer::Advertisement source, const StringName &requested) const {
    Ref<SteamServerQuery> query;
    query.instantiate();
    query->browser_id = ObjectID(get_instance_id());
    query->source = source;
    query->name_space = requested.is_empty() ? game_namespace : requested;
    if (query->name_space.is_empty()) {
        query->name_space = StringName("crimson");
    }
    query->valid = (source == SteamServer::LAN || source == SteamServer::INTERNET) &&
            valid_steam_lobby_namespace(String(query->name_space)) &&
            (game_namespace.is_empty() || query->name_space == game_namespace);
    return query;
}

Ref<SteamListingsOperation> SteamServerBrowser::start_query(const SteamServerQuery &query, int64_t timeout_ms) {
    if (!is_available()) {
        return SteamListingsOperation::rejected(ERR_UNAVAILABLE);
    }
    if (!query.valid || !valid_timeout(timeout_ms)) {
        return SteamListingsOperation::rejected(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    if (state->list || pending_lists >= maximum_lists) {
        return SteamListingsOperation::rejected(ERR_BUSY);
    }
    auto request = std::make_unique<ListRequest>();
    ++pending_lists;
    request->sdk = runtime.lock();
    request->api = state->api;
    request->operation.instantiate();
    request->deadline = Time::get_singleton()->get_ticks_msec() + static_cast<uint64_t>(timeout_ms);
    request->app = request->sdk->get_app_id();
    request->name_space = query.name_space;
    request->world = query.world;
    request->seats = query.seats;
    request->maximum_ping = query.maximum_ping;
    request->limit = query.maximum_results;

    if (query.source == SteamServer::LAN) {
        request->handle = state->api->RequestLANServerList(request->app, request.get());
    } else {
        String required_tags = String(STEAM_DIRECT_SERVER_PROTOCOL) + "," + steam_server_namespace_tag(query.name_space);
        if (!query.world.is_empty()) {
            required_tags += "," + steam_server_world_tag(query.world);
        }
        request->filters[0] = MatchMakingKeyValuePair_t("gametagsand", required_tags.utf8().get_data());
        request->filters[1] = MatchMakingKeyValuePair_t("dedicated", "");
        request->handle = state->api->RequestInternetServerList(request->app, request->filter_pointers.data(),
                static_cast<uint32_t>(request->filter_pointers.size()), request.get());
    }
    if (!request->handle || (request->observed && request->observed != request->handle)) {
        return SteamListingsOperation::rejected(ERR_CANT_CONNECT);
    }
    const ObjectID self(get_instance_id());
    request->operation->cancel_hook = [self]() {
        const Ref<SteamServerBrowser> browser(Object::cast_to<SteamServerBrowser>(ObjectDB::get_instance(self)));
        return browser.is_valid() ? browser->cancel_list() : ERR_DOES_NOT_EXIST;
    };
    const auto operation = request->operation;
    state->list = std::move(request);
    return operation;
#else
    return SteamListingsOperation::rejected(ERR_UNAVAILABLE);
#endif
}

Ref<SteamServerDetailsOperation> SteamServerBrowser::inspect(const Ref<SteamListing> &listing, int64_t timeout_ms) {
    if (!is_available()) {
        return SteamServerDetailsOperation::rejected(ERR_UNAVAILABLE);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    if (!valid_timeout(timeout_ms) || listing.is_null() || listing->get_kind() != SteamListing::SERVER ||
            listing->get_app_id() != sdk->get_app_id() || listing->get_server_query_port() == 0 ||
            (!game_namespace.is_empty() && game_namespace != listing->get_namespace())) {
        return SteamServerDetailsOperation::rejected(ERR_INVALID_PARAMETER);
    }
    const auto route = SteamServerRoute::from_listing(listing->get_id(), sdk->get_app_id(), listing->get_namespace());
    if (!route.is_valid()) {
        return SteamServerDetailsOperation::rejected(ERR_INVALID_PARAMETER);
    }
    if (state->inspect || pending_inspections >= maximum_inspections) {
        return SteamServerDetailsOperation::rejected(ERR_BUSY);
    }
    auto request = std::make_unique<InspectRequest>();
    ++pending_inspections;
    request->sdk = sdk;
    request->api = state->api;
    request->operation.instantiate();
    request->selected = listing;
    request->deadline = Time::get_singleton()->get_ticks_msec() + static_cast<uint64_t>(timeout_ms);
    request->handle = state->api->PingServer(route.ipv4, listing->get_server_query_port(), request.get());
    if (request->handle == HSERVERQUERY_INVALID) {
        return SteamServerDetailsOperation::rejected(ERR_CANT_CONNECT);
    }
    const ObjectID self(get_instance_id());
    request->operation->cancel_hook = [self]() {
        const Ref<SteamServerBrowser> browser(Object::cast_to<SteamServerBrowser>(ObjectDB::get_instance(self)));
        return browser.is_valid() ? browser->cancel_inspect() : ERR_DOES_NOT_EXIST;
    };
    const auto operation = request->operation;
    state->inspect = std::move(request);
    return operation;
#else
    return SteamServerDetailsOperation::rejected(ERR_UNAVAILABLE);
#endif
}

Error SteamServerBrowser::cancel_list() {
#ifdef CRIMSON_WITH_STEAM
    if (state->list) {
        const auto operation = state->list->operation;
        state->list.reset();
        operation->finish(SteamListingsResult::from_listings(ERR_SKIP));
        return OK;
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

Error SteamServerBrowser::cancel_inspect() {
#ifdef CRIMSON_WITH_STEAM
    if (state->inspect) {
        const auto operation = state->inspect->operation;
        state->inspect.reset();
        operation->finish(ERR_SKIP);
        return OK;
    }
#endif
    return ERR_DOES_NOT_EXIST;
}

void SteamServerBrowser::detach() {
    scope.reset();
    runtime.reset();
#ifdef CRIMSON_WITH_STEAM
    state->api = nullptr;
    const auto list = state->list ? state->list->operation : Ref<SteamListingsOperation>();
    const auto inspect = state->inspect ? state->inspect->operation : Ref<SteamServerDetailsOperation>();
    state->list.reset();
    state->inspect.reset();
    if (list.is_valid()) {
        list->finish(SteamListingsResult::from_listings(ERR_SKIP));
    }
    if (inspect.is_valid()) {
        inspect->finish(ERR_SKIP);
    }
#endif
}

void SteamServerBrowser::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (!is_available()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    Ref<SteamListingsOperation> list_operation;
    Ref<SteamListingsResult> list_result;
    if (const auto &list = state->list; list && (list->done || now >= list->deadline)) {
        list_operation = list->operation;
        const Error error = !list->done && list->values.is_empty() ? ERR_TIMEOUT : list->error;
        list_result = SteamListingsResult::from_listings(error, list->values);
        state->list.reset();
    }
    Ref<SteamServerDetailsOperation> inspect_operation;
    Ref<SteamServerDetails> details;
    Error inspect_error = ERR_TIMEOUT;
    if (const auto &inspect = state->inspect; inspect && (inspect->native_done || now >= inspect->deadline)) {
        inspect_operation = inspect->operation;
        if (inspect->native_done) {
            inspect_error = inspect->error;
            if (inspect->listing.is_valid()) {
                details.instantiate();
                details->listing = inspect->listing;
                details->tags = inspect->tags;
            }
        }
        state->inspect.reset();
    }
    if (list_operation.is_valid()) {
        list_operation->finish(list_result);
    }
    if (inspect_operation.is_valid()) {
        if (is_available()) {
            inspect_operation->finish(inspect_error, details);
        } else {
            inspect_operation->finish(ERR_SKIP);
        }
    }
#endif
}

} // namespace crimson::steam
