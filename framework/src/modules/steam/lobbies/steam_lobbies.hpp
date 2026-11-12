// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/session/online_listing.hpp"
#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/variant/typed_array.hpp>

#include <map>
#include <functional>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
class SteamLobbyLease;
class SteamLobbies;
class SteamLobby;
class SteamServerBrowser;
class SteamServerQuery;
struct SteamServerRoute;

// Maximum UTF-8 bytes in a published Crimson world identifier.
inline constexpr int32_t MAX_LOBBY_WORLD_BYTES = 512;
// Wire tag used for game lobbies with verified gameplay routes.
inline constexpr const char *STEAM_GAME_LOBBY_PROTOCOL = "crimson-steam-v2";
// Wire tag for standalone social lobbies.
inline constexpr const char *STEAM_SOCIAL_LOBBY_PROTOCOL = "crimson-steam-social-v1";
// Check the namespace shared by Steam publication and discovery.
bool valid_steam_lobby_namespace(const godot::String &value);
// Check the world identifier shared by Steam game publication, search, and join.
bool valid_steam_lobby_world(const godot::String &value);
// Parse the decimal identifier of a valid individual Steam account.
bool parse_steam_lobby_account(const godot::String &text, uint64_t &id);
// Parse the decimal identifier of a valid Steam lobby.
bool parse_steam_lobby_id(const godot::String &text, uint64_t &id);

// Detached Steam lobby or server location. Joining always resolves the current endpoint.
class SteamListing : public session::OnlineListing {
    GDCLASS(SteamListing, session::OnlineListing)

public:
    // Steam discovery sources supported by the common locator.
    enum Kind {
        LOBBY, // An observed Steam lobby.
        SERVER, // A Steam server locator reserved for the server browser.
    };

    // Return the kind of listing to resolve.
    Kind get_kind() const;
    // Return the app under which this listing was discovered.
    int64_t get_app_id() const;
    // Return the expected game namespace.
    godot::StringName get_namespace() const;
    // Return the advertised world, if available.
    godot::StringName get_world_id() const;
    // Return the advertised display name, if available.
    godot::String get_name() const;
    // Return the advertised current member count.
    int32_t get_players() const;
    // Return the advertised member limit.
    int32_t get_capacity() const;
    // Return the server identity when supplied by a server listing.
    godot::String get_server_steam_id() const;
    // Return the measured server ping, or -1 when unavailable.
    int32_t get_ping_ms() const;
    // Return whether this locator explicitly describes a standalone social lobby.
    bool is_social() const;
    // Create a bounded lobby locator from trusted Steam callback or query data.
    static godot::Ref<SteamListing> lobby(uint64_t id, uint32_t app, const godot::StringName &name_space,
            const godot::StringName &world = {}, const godot::String &name = {}, int32_t players = 0,
            int32_t capacity = 0, bool social = false);
    // Create an opaque lobby locator pinned to its SDK-observed party beacon owner.
    static godot::Ref<SteamListing> party_lobby(uint64_t id, uint64_t owner, uint32_t app,
            const godot::StringName &name_space);
    // Create a detached discovery snapshot with a direct server certificate identity.
    static godot::Ref<SteamListing> server(const SteamServerRoute &route, uint32_t app,
            const godot::StringName &name_space, const godot::StringName &world, const godot::String &name,
            int32_t players, int32_t capacity, int32_t ping_ms, uint16_t query_port);
    // Return the native query port used to inspect this dedicated listing.
    uint16_t get_server_query_port() const;

protected:
    // Register typed read-only snapshot properties and kind constants.
    static void _bind_methods();

private:
    // Discovery source for this immutable locator.
    Kind kind = LOBBY;
    // Active Steam AppID when the provider observed this locator.
    int64_t app_id = 0;
    // Namespace expected by the originating scope.
    godot::StringName name_space;
    // World advertised by the lobby.
    godot::StringName world_id;
    // Bounded presentation label.
    godot::String name;
    // Observed lobby membership at the time of the snapshot.
    int32_t players = 0;
    // Observed membership cap at the time of the snapshot.
    int32_t capacity = 0;
    // Whether discovery established this as a social lobby.
    bool social = false;
    // Decimal dedicated Steam identity pinned by a server locator.
    godot::String server_steam_id;
    // Snapshot of measured browser latency in milliseconds.
    int32_t ping_ms = -1;
    // Native UDP query port captured by the server browser.
    uint16_t server_query_port = 0;
};

// Typed result containing one detached Steam locator when found.
class SteamListingResult : public SteamResult {
    GDCLASS(SteamListingResult, SteamResult)

public:
    // Return the locator from the completed lookup, or null on failure.
    godot::Ref<SteamListing> get_value() const;
    // Settle a native lookup with its optional immutable locator.
    static godot::Ref<SteamListingResult> from_listing(godot::Error error,
            const godot::Ref<SteamListing> &listing = {});

protected:
    // Register the typed value property.
    static void _bind_methods();

private:
    // Resolved snapshot owned by this result.
    godot::Ref<SteamListing> value;
};

// Typed result containing a bounded batch of detached Steam locators.
class SteamListingsResult : public SteamResult {
    GDCLASS(SteamListingsResult, SteamResult)

public:
    // Return a copy of the list with its immutable snapshot objects.
    godot::TypedArray<SteamListing> get_value() const;
    // Build a result after snapshotting the exact SDK search.
    static godot::Ref<SteamListingsResult> from_listings(godot::Error error,
            const godot::TypedArray<SteamListing> &listings = {});

protected:
    // Register the typed value property.
    static void _bind_methods();

private:
    // Snapshots copied before another search can update the SDK result index.
    godot::TypedArray<SteamListing> value;
};

// Cancellable observer of an exact Steam lobby search.
class SteamListingsOperation : public godot::RefCounted {
    GDCLASS(SteamListingsOperation, godot::RefCounted)

public:
    // Return whether the caller has a terminal result.
    bool is_finished() const;
    // Return the terminal result or null while pending.
    godot::Ref<SteamListingsResult> result() const;
    // Await the terminal result or read the already completed result.
    godot::Variant wait();
    // Cancel observation while the process drains the exact Steam search.
    godot::Error cancel();

protected:
    // Register the retained operation and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamLobbies;
    friend class SteamLobbyQuery;
    friend class SteamServerBrowser;
    friend class SteamServerQuery;

    // Stable result held after settlement.
    godot::Ref<SteamListingsResult> terminal_result;
    // Optional cancellation hook for the separate Steam server browser.
    std::function<godot::Error()> cancel_hook;
    // Construct an already settled preflight failure.
    static godot::Ref<SteamListingsOperation> rejected(godot::Error error);
    // Publish the result exactly once.
    void finish(const godot::Ref<SteamListingsResult> &completed);
};

// Branchable configuration for one scoped Steam lobby search.
class SteamLobbyQuery : public godot::RefCounted {
    GDCLASS(SteamLobbyQuery, godot::RefCounted)

public:
    // Region preference for the native Steam search.
    enum Distance {
        CLOSE, // Search the immediate Steam region.
        NEARBY, // Include this region and nearby regions.
        FAR, // Include a wider set of Steam regions.
        WORLDWIDE, // Include every Steam region.
    };

    // Restrict this branch to the given world ID.
    godot::Ref<SteamLobbyQuery> on_map(const godot::StringName &world_id) const;
    // Require this many open lobby seats.
    godot::Ref<SteamLobbyQuery> with_room_for(int64_t players) const;
    // Select the geographic distance preference.
    godot::Ref<SteamLobbyQuery> within(Distance distance) const;
    // Cap this branch's result count.
    godot::Ref<SteamLobbyQuery> limit(int64_t count) const;
    // Search standalone social lobbies instead of gameplay listings.
    godot::Ref<SteamLobbyQuery> social() const;
    // Run the search and await the typed result directly.
    godot::Variant fetch() const;
    // Run the same search with a retained cancellable operation.
    godot::Ref<SteamListingsOperation> fetch_operation() const;

protected:
    // Register query modifiers, terminal methods, and distances.
    static void _bind_methods();

private:
    friend class SteamLobbies;

    // Original scoped service, observed weakly by retained query branches.
    godot::ObjectID service_id;
    // Namespace pinned when the branch was built.
    godot::StringName name_space;
    // Optional world filter.
    godot::StringName world;
    // Minimum available seats, including the joining player.
    int32_t open_seats = 1;
    // Search preference for nearby regions.
    Distance distance = NEARBY;
    // Maximum compatible results.
    int32_t maximum_results = 20;
    // Whether this branch has passed all builder validation.
    bool valid = true;
    // Select standalone social publication instead of gameplay publication.
    bool social_only = false;

    // Clone detached options without changing the original branch.
    godot::Ref<SteamLobbyQuery> branch() const;
};

// Fluent creation choices for an ordinary standalone Steam lobby.
class SteamLobbyOptions : public godot::RefCounted {
    GDCLASS(SteamLobbyOptions, godot::RefCounted)

public:
    // Native Steam visibility choices for new lobbies.
    enum Visibility {
        PRIVATE, // Visible only to invitees.
        FRIENDS_ONLY, // Joinable by friends or invitees.
        PUBLIC, // Discoverable in Steam searches.
        INVISIBLE, // Discoverable in searches without friends visibility.
    };

    // Begin an invite-only lobby in the default namespace with four seats.
    static godot::Ref<SteamLobbyOptions> configure();
    // Branch with a different Steam visibility policy.
    godot::Ref<SteamLobbyOptions> with_visibility(Visibility value) const;
    // Branch with a game-defined matchmaking namespace.
    godot::Ref<SteamLobbyOptions> in_namespace(const godot::StringName &value) const;
    // Branch with a capacity from 1 through 65 including the owner.
    godot::Ref<SteamLobbyOptions> for_players(int64_t value) const;

protected:
    // Register the creation builder and its visibility constants.
    static void _bind_methods();

private:
    friend class SteamLobbies;

    // Validate all accumulated options when CreateLobby is submitted.
    bool valid = true;
    // Steam visibility applied only after compatible metadata is written.
    Visibility visibility = PRIVATE;
    // Matchmaking namespace published for discovery.
    godot::StringName name_space = "crimson";
    // Maximum member count including the local creator.
    int32_t capacity = 4;

    // Duplicate detached options for a fluent branch.
    godot::Ref<SteamLobbyOptions> branch() const;
};

// Detached identity and presentation of one current Steam lobby member.
class SteamLobbyMember : public godot::RefCounted {
    GDCLASS(SteamLobbyMember, godot::RefCounted)

public:
    // Return the member's decimal SteamID64.
    godot::String get_steam_id() const;
    // Return the locally available Steam display name.
    godot::String get_display_name() const;
    // Report whether the member owned the lobby when this snapshot was read.
    bool is_owner() const;

protected:
    // Register read-only member properties.
    static void _bind_methods();

private:
    friend class SteamLobby;

    // Decimal SDK identity, with no numeric precision loss.
    godot::String steam_id;
    // Bounded local Steam persona label.
    godot::String display_name;
    // Snapshot of live owner status when copied.
    bool owner = false;
};

// Branchable owner-managed display and application lobby metadata.
class SteamLobbyEdit : public godot::RefCounted {
    GDCLASS(SteamLobbyEdit, godot::RefCounted)

public:
    // Stage a display name of up to 127 UTF-8 bytes.
    godot::Ref<SteamLobbyEdit> named(const godot::String &name) const;
    // Stage one nonreserved metadata key and a value of up to 255 UTF-8 bytes.
    godot::Ref<SteamLobbyEdit> tag(const godot::StringName &key, const godot::String &value) const;
    // Recheck live ownership and submit each staged change to Steam.
    godot::Error submit() const;

protected:
    // Register detached edit modifiers and explicit submission.
    static void _bind_methods();

private:
    friend class SteamLobby;

    // The exact membership observed by this editor without extending it.
    godot::ObjectID lobby_id;
    // Bounded staged metadata, indexed by validated key.
    std::map<godot::String, godot::String> updates;
    // Reject submission if any earlier step failed validation.
    bool valid = true;

    // Copy the staged edits for a fluent branch.
    godot::Ref<SteamLobbyEdit> branch() const;
};

// View and social controls for one exact owned Steam lobby membership.
class SteamLobby : public godot::RefCounted {
    GDCLASS(SteamLobby, godot::RefCounted)

public:
    // Read a detached listing, refreshing presentation details while this membership is active.
    godot::Ref<SteamListing> listing() const;
    // Copy the current SDK membership, or return an empty list after closure.
    godot::TypedArray<SteamLobbyMember> members() const;
    // Stage owner-managed display metadata without writing it to Steam yet.
    godot::Ref<SteamLobbyEdit> edit() const;
    // Read one nonreserved application metadata value from the live lobby.
    godot::String tag(const godot::StringName &key) const;
    // Open or close new entry to a standalone lobby owned by this account.
    godot::Error set_joinable(bool open);
    // Leave this standalone membership.
    godot::Error leave();
    // Submit an invitation from this lobby's live owner to one selected Steam account.
    godot::Error invite(const godot::String &steam_id);
    // Open the Steam invite picker for the current owned lobby.
    godot::Error open_invite_dialog();
    // Send up to 1024 bytes of UTF-8 text to this exact lobby.
    godot::Error send_chat(const godot::String &text);

protected:
    // Register membership, social actions, and scoped observations.
    static void _bind_methods();

private:
    friend class SteamLobbyEdit;
    friend class SteamLobbies;

    // Listing captured when the exact membership was committed.
    godot::Ref<SteamListing> snapshot;
    // Service allowed to retire this standalone membership.
    godot::ObjectID service_id;
    // Exact native account reservation, independently retired by its owner.
    std::weak_ptr<SteamLobbyLease> lease;
    // Exact client scope for all public actions.
    std::weak_ptr<SteamClientScope> scope;
    // Client SDK interface without extending the underlying pipe lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Game controls admission and lifetime when it installed this membership.
    bool game_managed = false;
    // Most recent locally submitted chat timestamp in monotonic milliseconds.
    uint64_t last_sent_ms = 0;
    // Start of the bounded incoming chat observation window.
    uint64_t chat_window_ms = 0;
    // Number of accepted chat messages in that window.
    uint32_t chat_events = 0;

    // Resolve the exact live SDK membership before reading or mutating state.
    uint64_t live_lobby(const std::shared_ptr<SteamRuntime> &sdk) const;
    // Copy one validated SDK lobby member.
    godot::Ref<SteamLobbyMember> member(const std::shared_ptr<SteamRuntime> &sdk,
            uint64_t id, uint64_t owner) const;
    // Handle an exact SDK member change for this lobby.
    void on_membership(const void *data, size_t size);
    // Copy an exact SDK chat entry during the corresponding callback.
    void on_chat(const void *data, size_t size);
};

// Typed standalone create or join outcome.
class SteamLobbyResult : public SteamResult {
    GDCLASS(SteamLobbyResult, SteamResult)

public:
    // Return the established lobby on success.
    godot::Ref<SteamLobby> get_value() const;

protected:
    // Register the typed value.
    static void _bind_methods();

private:
    friend class SteamLobbyOperation;
    // Lobby retained by this result even after its membership closes.
    godot::Ref<SteamLobby> value;
};

// Cancellable observer of one exact native standalone lobby create or join.
class SteamLobbyOperation : public godot::RefCounted {
    GDCLASS(SteamLobbyOperation, godot::RefCounted)

public:
    // Return whether the result is terminal.
    bool is_finished() const;
    // Return the result or null while Steam is completing the operation.
    godot::Ref<SteamLobbyResult> result() const;
    // Await completion or return the already available result.
    godot::Variant wait();
    // Cancel the caller's observation and retire any membership acquired later.
    godot::Error cancel();

protected:
    // Register operation methods and the typed completion signal.
    static void _bind_methods();

private:
    friend class SteamLobbies;

    // Owning scoped service during this submission.
    godot::ObjectID service_id;
    // Exact outcome after settlement.
    godot::Ref<SteamLobbyResult> terminal_result;

    // Construct an immediately rejected operation.
    static godot::Ref<SteamLobbyOperation> rejected(godot::Error error);
    // Settle the typed observer exactly once after native cleanup or ownership transfer.
    void finish(godot::Error error, const godot::Ref<SteamLobby> &lobby = {});
};

// Steam lobby discovery and standalone social membership for one Steam client scope.
class SteamLobbies : public godot::RefCounted {
    GDCLASS(SteamLobbies, godot::RefCounted)

public:
    // Report whether this exact attachment can query Steam lobbies.
    bool is_available() const;
    // Return this scope's current membership, or null if none is active.
    godot::Ref<SteamLobby> current() const;
    // Build a detached search in the specified namespace, or the configured scope namespace.
    godot::Ref<SteamLobbyQuery> query(const godot::StringName &name_space = {}) const;
    // Read a compatible friend's lobby locator from the Steam client.
    godot::Ref<SteamListingResult> friend_lobby(const godot::String &friend_id) const;
    // Create a standalone social lobby, or use default options when omitted.
    godot::Ref<SteamLobbyOperation> create(const godot::Ref<SteamLobbyOptions> &options = {});
    // Join a social lobby after revalidating its live namespace and protocol.
    godot::Ref<SteamLobbyOperation> join(const godot::Ref<SteamListing> &listing);
    // Bind the standalone service to the exact native runtime and scope.
    void attach(const std::shared_ptr<SteamRuntime> &runtime,
            const std::shared_ptr<SteamClientScope> &scope);
    // Restrict this exact service to Game-published listings and Game-owned membership.
    void configure_game(const godot::StringName &name_space);
    // Observe a Game directory's exact membership after native route validation.
    void set_game_lobby(const std::shared_ptr<SteamLobbyLease> &lease, const godot::Ref<SteamListing> &listing);
    // Clear and close the exact Game membership installed previously.
    void clear_game_lobby(const std::shared_ptr<SteamLobbyLease> &lease);
    // Retire this scope's observers and standalone membership before it stops.
    void detach();
    // Time out a caller observation while keeping any pending native membership exclusive.
    void tick();
    // Expire the single process search if Steam has not settled it in time.
    static void poll();

protected:
    // Register the scoped lobby reads and query entry point.
    static void _bind_methods();

private:
    friend class SteamLobby;
    friend class SteamLobbyOperation;
    friend class SteamLobbyQuery;

    // Shared SDK runtime, weak so a retained facade cannot keep the Steam pipe open.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact game attachment token.
    std::weak_ptr<SteamClientScope> scope;
    // Default discovery namespace, pinned by a configured Game attachment.
    godot::StringName name_space = "crimson";
    // Whether membership and admission belong to a Game directory.
    bool game_managed = false;
    // Exact social membership attempt or committed reservation.
    std::shared_ptr<SteamLobbyLease> social_lease;
    // Active lobby facade for this scope.
    godot::Ref<SteamLobby> active_lobby;
    // Outstanding standalone caller observation, independent of native retirement.
    godot::Ref<SteamLobbyOperation> pending_operation;
    // Monotonic deadline for the current caller's create or join observation.
    uint64_t membership_deadline_ms = 0;
    // Subscriptions for native lobby notifications in this scope.
    uint64_t invite_subscription = 0;
    uint64_t members_subscription = 0;
    uint64_t chat_subscription = 0;

    // Start the one process-owned Steam search for these detached options.
    godot::Ref<SteamListingsOperation> start_search(const SteamLobbyQuery &query);
    // Retire this scope's caller observation while the exact SDK search drains.
    void cancel_search();
    // Start and watch one exact create or join SDK call after public preflight.
    godot::Ref<SteamLobbyOperation> start_membership(const godot::Ref<SteamLobbyOptions> &options,
            uint64_t target, const godot::StringName &selected_namespace);
    // Validate the acquired social lobby and settle only the matching observation.
    void finish_membership(const std::shared_ptr<SteamLobbyLease> &lease,
            const godot::Ref<SteamLobbyOptions> &options, uint64_t target,
            const godot::StringName &selected_namespace);
    // Retire the exact standalone submission before completing its observer.
    godot::Error cancel_membership(SteamLobbyOperation *operation, godot::Error reason = godot::ERR_SKIP);
    // Retire a live standalone or locally lost Game observation.
    godot::Error retire_lobby(SteamLobby *lobby, godot::Error reason);
    // Install a lobby view for this scope and exact committed lease.
    void set_lobby(const std::shared_ptr<SteamLobbyLease> &lease, const godot::Ref<SteamListing> &listing);
    // Observe an explicitly accepted Steam invite without joining it.
    void on_invite(const void *data, size_t size);
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamListing::Kind);
VARIANT_ENUM_CAST(crimson::steam::SteamLobbyQuery::Distance);
VARIANT_ENUM_CAST(crimson::steam::SteamLobbyOptions::Visibility);
