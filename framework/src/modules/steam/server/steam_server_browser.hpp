// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/lobbies/steam_lobbies.hpp"
#include "modules/steam/server/steam_server.hpp"

#include <godot_cpp/variant/packed_string_array.hpp>

namespace crimson::steam {

// Detached snapshot from a direct Steam server details query.
class SteamServerDetails : public godot::RefCounted {
    GDCLASS(SteamServerDetails, godot::RefCounted)

public:
    // Return the fresh listing whose address and Steam identity were rechecked.
    godot::Ref<SteamListing> get_listing() const;
    // Return the measured latency in milliseconds.
    int32_t get_ping_ms() const;
    // Return a detached list of bounded advertised tags.
    godot::PackedStringArray get_tags() const;

protected:
    // Register read-only server details.
    static void _bind_methods();

private:
    friend class SteamServerBrowser;

    // Fresh compatible server snapshot.
    godot::Ref<SteamListing> listing;
    // Bounded vendor metadata copied before returning its callback.
    godot::PackedStringArray tags;
};

// Terminal typed result of refreshing one server.
class SteamServerDetailsResult : public SteamResult {
    GDCLASS(SteamServerDetailsResult, SteamResult)

public:
    // Return the refreshed details, or null on failure.
    godot::Ref<SteamServerDetails> get_value() const;

protected:
    // Register the typed result value.
    static void _bind_methods();

private:
    friend class SteamServerDetailsOperation;

    // Optional successful server details.
    godot::Ref<SteamServerDetails> value;
};

// One cancellable direct server-details query.
class SteamServerDetailsOperation : public godot::RefCounted {
    GDCLASS(SteamServerDetailsOperation, godot::RefCounted)

public:
    // Return whether the query has a terminal result.
    bool is_finished() const;
    // Return the terminal result, or null while pending.
    godot::Ref<SteamServerDetailsResult> result() const;
    // Await completion or read the already settled result.
    godot::Variant wait();
    // Cancel the exact native query before settling its observer.
    godot::Error cancel();

protected:
    // Register the typed operation and completion signal.
    static void _bind_methods();

private:
    friend class SteamServerBrowser;

    // Terminal result after Steam no longer owns the query.
    godot::Ref<SteamServerDetailsResult> terminal_result;
    // Exact scoped native cancellation action.
    std::function<godot::Error()> cancel_hook;

    // Build an already settled local failure.
    static godot::Ref<SteamServerDetailsOperation> rejected(godot::Error error);
    // Settle the observer once after releasing the native request.
    void finish(godot::Error error, const godot::Ref<SteamServerDetails> &details = {});
};

// Branchable Steam dedicated browser filters for one client scope.
class SteamServerQuery : public godot::RefCounted {
    GDCLASS(SteamServerQuery, godot::RefCounted)

public:
    // Match the stable Crimson world ID advertised by the server.
    godot::Ref<SteamServerQuery> on_map(const godot::StringName &world) const;
    // Require this many free seats in the Steam snapshot.
    godot::Ref<SteamServerQuery> with_room_for(int64_t players) const;
    // Require a ping at or below the given number of milliseconds.
    godot::Ref<SteamServerQuery> under_latency(int64_t milliseconds) const;
    // Return up to 50 compatible server snapshots.
    godot::Ref<SteamServerQuery> limit(int64_t count) const;
    // Run the bounded query and await its typed result.
    godot::Variant fetch(int64_t timeout_ms = 5000) const;
    // Run the same query with an explicit cancellable operation.
    godot::Ref<SteamListingsOperation> fetch_operation(int64_t timeout_ms = 5000) const;

protected:
    // Register the fluent filters and bounded query actions.
    static void _bind_methods();

private:
    friend class SteamServerBrowser;

    // Exact browser scope, observed without retaining its client.
    godot::ObjectID browser_id;
    // Steam Internet or LAN browse source.
    SteamServer::Advertisement source = SteamServer::INTERNET;
    // Project namespace pinned by the service or selected for this standalone query.
    godot::StringName name_space;
    // Optional exact stable world filter.
    godot::StringName world;
    // Minimum open seats including the player who would join.
    int32_t seats = 1;
    // Optional upper ping bound, zero means any observed latency.
    int32_t maximum_ping = 0;
    // Maximum compatible listings returned by this query.
    int32_t maximum_results = 50;
    // Validate every modifier on the terminal submission.
    bool valid = true;

    // Copy detached options without mutating the original query.
    godot::Ref<SteamServerQuery> branch() const;
};

// Scoped Steam client facade for dedicated server discovery and selected-server refresh.
class SteamServerBrowser : public godot::RefCounted {
    GDCLASS(SteamServerBrowser, godot::RefCounted)

public:
    // Initialize a detached service without starting the SDK.
    SteamServerBrowser();
    // Release native observers and queries before freeing their callback objects.
    ~SteamServerBrowser() override;
    // Report whether the exact signed-in client can query the server browser.
    bool is_available() const;
    // Begin a detached Internet or LAN search for the scope's namespace.
    godot::Ref<SteamServerQuery> query(SteamServer::Advertisement source = SteamServer::INTERNET,
            const godot::StringName &name_space = {}) const;
    // Refresh a selected typed server locator through its Steam query port.
    godot::Ref<SteamServerDetailsOperation> inspect(const godot::Ref<SteamListing> &listing, int64_t timeout_ms = 3000);
    // Install the service for this exact Steam client generation.
    void attach(const std::shared_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &scope,
            const godot::StringName &game_namespace);
    // Release native requests and retire their caller observations before scope shutdown.
    void detach();
    // Settle copied SDK results and deadlines outside native callback frames.
    void tick();

protected:
    // Register availability, query creation and explicit details refresh.
    static void _bind_methods();

private:
    friend class SteamServerQuery;

    // SDK callback objects and native handle ownership for this scope.
    struct State;
    std::unique_ptr<State> state;
    // Shared Steam runtime, weak so a retained facade cannot keep it alive.
    std::weak_ptr<SteamRuntime> runtime;
    // Liveness of the exact owning SteamClient start.
    std::weak_ptr<SteamClientScope> scope;
    // Fixed project namespace for a Game, empty for a standalone scope.
    godot::StringName game_namespace;

    // Submit one bounded native list query.
    godot::Ref<SteamListingsOperation> start_query(const SteamServerQuery &query, int64_t timeout_ms);
    // Retire the exact list observer after releasing its native handle.
    godot::Error cancel_list();
    // Retire the exact details observer after releasing its native handle.
    godot::Error cancel_inspect();
};

} // namespace crimson::steam
