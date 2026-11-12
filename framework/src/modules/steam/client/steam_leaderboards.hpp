// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace crimson::steam {

class SteamRuntime;
class SteamLeaderboardScope;
class SteamLeaderboard;
class SteamLeaderboardOperation;
class SteamLeaderboardQuery;
class SteamOperation;
class SteamScoreOptions;
struct SteamClientScope;
struct SteamLeaderboardWrite;

// Finds leaderboards and owns their operation lifetime for one Steam client scope.
class SteamLeaderboards : public godot::RefCounted {
    GDCLASS(SteamLeaderboards, godot::RefCounted)

public:
    // Sort direction used when creating a board.
    enum Sort {
        ASCENDING = 1, // Lower scores rank first.
        DESCENDING = 2, // Higher scores rank first.
    };
    // Display format used when creating a board.
    enum Display {
        NUMERIC = 1, // Display a plain signed score.
        SECONDS = 2, // Interpret the score as seconds.
        MILLISECONDS = 3, // Interpret the score as milliseconds.
    };

    // Create a fresh client-scoped leaderboard lease.
    void start_scope(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &client_scope);
    // Invalidate boards and cancel pending operations for this client.
    void close();
    // Expire pending operations at their bounded deadlines.
    void tick();
    // Report whether Steam currently exposes leaderboards to this scope.
    bool is_available() const;
    // Await the first 1 to 100 global ranks of an existing board, or receive an immediate error.
    godot::Variant top(const godot::String &board_name, int64_t count = 10);
    // Await the first 1 to 100 matching friends' scores for an existing board.
    godot::Variant friends(const godot::String &board_name, int64_t count = 10);
    // Begin a branchable query whose default selection is the first ten global ranks.
    godot::Ref<SteamLeaderboardQuery> query(const godot::String &board_name) const;
    // Await a keep-best score submission to an existing board.
    godot::Variant submit_score(const godot::String &board_name, int64_t score) const;
    // Start a submission draft with game-defined score details.
    godot::Ref<SteamScoreOptions> with_details(const godot::PackedInt32Array &details) const;
    // Start a submission draft with a same-app shared replay handle.
    godot::Ref<SteamScoreOptions> with_replay(const godot::String &shared_file_handle) const;
    // Find an existing board by its Steam API name.
    godot::Ref<SteamLeaderboardOperation> find(const godot::String &name);
    // Find or create a board with the given sort and display format.
    godot::Ref<SteamLeaderboardOperation> find_or_create(const godot::String &name,
            Sort sort = DESCENDING, Display display = NUMERIC);

protected:
    // Register the leaderboard service and creation enums.
    static void _bind_methods();

private:
    friend class SteamLeaderboardQuery;
    friend class SteamScoreOptions;

    // Create an empty draft attached to the exact current scope.
    godot::Ref<SteamScoreOptions> score_options() const;

    // Start the exact Steam search or creation call after validating the name.
    static godot::Ref<SteamLeaderboardOperation> lookup(const std::shared_ptr<SteamLeaderboardScope> &owner,
            const godot::String &name, bool create, Sort sort, Display display);

    // Current lease keeps the pending map alive independently from the board handles.
    std::shared_ptr<SteamLeaderboardScope> scope;
};

// Detached score row copied from a Steam leaderboard download.
class SteamLeaderboardEntry : public godot::RefCounted {
    GDCLASS(SteamLeaderboardEntry, godot::RefCounted)

public:
    // Return the score owner's SteamID64 as a decimal string.
    godot::String get_steam_id() const;
    // Return the one-based global rank.
    int64_t get_rank() const;
    // Return the raw signed 32-bit score as a Godot integer.
    int64_t get_score() const;
    // Return a copy-on-write packed array of game-defined score details.
    godot::PackedInt32Array get_details() const;
    // Return the attached shared-file handle or an empty string.
    godot::String get_ugc_handle() const;

protected:
    // Bind the read-only row properties.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;

    // Score owner's full SteamID64.
    godot::String steam_id;
    // Position among all entries in the Steam board.
    int64_t rank = 0;
    // Game-defined score.
    int64_t score = 0;
    // Detached application-defined score metadata.
    godot::PackedInt32Array details;
    // Decimal shared-file handle when a replay is attached.
    godot::String ugc_handle;
};

// Bounded, detached, read-only result of a leaderboard query.
class SteamLeaderboardPage : public godot::RefCounted {
    GDCLASS(SteamLeaderboardPage, godot::RefCounted)

public:
    // Return the read-only array of rows copied from Steam.
    godot::TypedArray<SteamLeaderboardEntry> get_entries() const;
    // Return the number of rows in the SDK download before the display cap.
    int64_t get_total_count() const;

protected:
    // Bind the typed row collection and download count.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;

    // Rows available to the caller, sealed before publication.
    godot::TypedArray<SteamLeaderboardEntry> entries;
    // SDK result count before applying the caller's limit.
    int64_t total_count = 0;
};

// Status and typed page returned when a leaderboard read settles.
class SteamLeaderboardPageResult : public SteamResult {
    GDCLASS(SteamLeaderboardPageResult, SteamResult)

public:
    // Return the page on success, or null on failure.
    godot::Ref<SteamLeaderboardPage> get_value() const;

protected:
    // Bind the read-only typed page payload.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;
    friend class SteamLeaderboardPageOperation;

    // Construct a terminal outcome before delivering it to Godot.
    static godot::Ref<SteamLeaderboardPageResult> create(godot::Error error,
            const godot::Ref<SteamLeaderboardPage> &page = {});

    // Detached page available when the SDK read succeeded.
    godot::Ref<SteamLeaderboardPage> value;
};

// A cancellable lookup and download, or a download against an already resolved board.
class SteamLeaderboardPageOperation : public godot::RefCounted {
    GDCLASS(SteamLeaderboardPageOperation, godot::RefCounted)

public:
    // Report whether the operation has settled.
    bool is_finished() const;
    // Return the terminal result, or null while pending.
    godot::Ref<SteamLeaderboardPageResult> result() const;
    // Return the terminal result or its completion signal.
    godot::Variant wait();
    // End local observation of the active request stage.
    godot::Error cancel();

protected:
    // Bind typed completion and the retained operation methods.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;
    friend class SteamLeaderboard;
    friend class SteamLeaderboardQuery;

    // Construct an operation that settled before asynchronous work started.
    static godot::Ref<SteamLeaderboardPageOperation> failed(godot::Error error);
    // Settle the operation with an error payload.
    void fail(godot::Error error);
    // Settle once and clear cancellation before invoking observers.
    void finish(const godot::Ref<SteamLeaderboardPageResult> &result);
    // Attach the sole internal continuation or invoke it immediately.
    void when_finished(std::function<void(const godot::Ref<SteamLeaderboardPageResult> &)> notify);

    // Stored terminal outcome, null while pending.
    godot::Ref<SteamLeaderboardPageResult> outcome;
    // Cancels the currently active stage without owning that stage.
    std::function<void()> cancel_request;
    // Completes a composed query after the download stage ends.
    std::function<void(const godot::Ref<SteamLeaderboardPageResult> &)> continuation;
};

// A branchable leaderboard selection bound to its originating Steam client scope.
class SteamLeaderboardQuery : public godot::RefCounted {
    GDCLASS(SteamLeaderboardQuery, godot::RefCounted)

public:
    // Select a one-based global rank range.
    godot::Ref<SteamLeaderboardQuery> global_ranks(int64_t first_rank = 1, int64_t count = 10) const;
    // Select friends' scores, capped at the requested row count.
    godot::Ref<SteamLeaderboardQuery> friends(int64_t count = 10) const;
    // Select a range centered on the signed-in player's rank.
    godot::Ref<SteamLeaderboardQuery> around_me(int64_t before = 4, int64_t after = 5) const;
    // Select up to 100 users with distinct decimal Steam IDs.
    godot::Ref<SteamLeaderboardQuery> users(const godot::PackedStringArray &steam_ids) const;
    // Await the selected page without retaining an operation in GDScript.
    godot::Variant fetch() const;
    // Start the query and return a typed cancellable operation.
    godot::Ref<SteamLeaderboardPageOperation> fetch_operation() const;

protected:
    // Bind query modifiers and terminal methods.
    static void _bind_methods();

private:
    friend class SteamLeaderboards;

    // Identifies which arguments and SDK read path a query branch uses.
    enum class Selection { GLOBAL, FRIENDS, AROUND_ME, USERS };

    // Return a branch for the selected query mode and arguments.
    godot::Ref<SteamLeaderboardQuery> select(Selection mode, int64_t first, int64_t second,
            const godot::PackedStringArray &steam_ids = {}) const;

    // Exact originating lease, which the query never keeps alive.
    std::weak_ptr<SteamLeaderboardScope> scope;
    // Published Steam board name resolved at fetch time.
    godot::String board_name;
    // Chosen result selection.
    Selection selection = Selection::GLOBAL;
    // First rank or row count, or the number of ranks before the local player.
    int64_t first = 1;
    // Global count or the number of ranks after the local player.
    int64_t second = 10;
    // Selected users for a USERS branch.
    godot::PackedStringArray steam_ids;
};

// Detached metadata for a resolved Steam leaderboard.
class SteamLeaderboardInfo : public godot::RefCounted {
    GDCLASS(SteamLeaderboardInfo, godot::RefCounted)

public:
    // Return the published board name.
    godot::String get_name() const;
    // Return the total number of entries reported for the board.
    int64_t get_entry_count() const;
    // Return the board sort direction.
    SteamLeaderboards::Sort get_sort() const;
    // Return the board score display format.
    SteamLeaderboards::Display get_display() const;

protected:
    // Bind the read-only properties.
    static void _bind_methods();

private:
    friend class SteamLeaderboard;

    // Stores the published board name.
    godot::String name;
    // Stores the total number of entries reported for the board.
    int64_t entry_count = 0;
    // Stores the board sort direction.
    SteamLeaderboards::Sort sort = SteamLeaderboards::DESCENDING;
    // Stores the board score display format.
    SteamLeaderboards::Display display = SteamLeaderboards::NUMERIC;
};

// The outcome of reading a board metadata snapshot.
class SteamLeaderboardInfoResult : public SteamResult {
    GDCLASS(SteamLeaderboardInfoResult, SteamResult)

public:
    // Return the detached payload, or null when none was accepted.
    godot::Ref<SteamLeaderboardInfo> get_value() const;

protected:
    // Bind the typed read-only payload.
    static void _bind_methods();

private:
    friend class SteamLeaderboard;

    // Construct the terminal result before delivering it to script.
    static godot::Ref<SteamLeaderboardInfoResult> create(godot::Error error, const godot::Ref<SteamLeaderboardInfo> &value = {});

    // Typed payload retained after the Steam scope ends.
    godot::Ref<SteamLeaderboardInfo> value;
};

// A board returned by a completed lookup or creation.
class SteamLeaderboardResult : public SteamResult {
    GDCLASS(SteamLeaderboardResult, SteamResult)

public:
    // Return the detached payload, or null when none was accepted.
    godot::Ref<SteamLeaderboard> get_value() const;

protected:
    // Bind the typed read-only payload.
    static void _bind_methods();

private:
    friend class SteamLeaderboards;
    friend class SteamLeaderboardOperation;

    // Construct the terminal result before delivering it to script.
    static godot::Ref<SteamLeaderboardResult> create(godot::Error error, const godot::Ref<SteamLeaderboard> &value = {});

    // Typed payload retained after the Steam scope ends.
    godot::Ref<SteamLeaderboard> value;
};

// Retained observation of a Steam leaderboard lookup or creation.
class SteamLeaderboardOperation : public godot::RefCounted {
    GDCLASS(SteamLeaderboardOperation, godot::RefCounted)

public:
    // Report whether the operation has settled.
    bool is_finished() const;
    // Return the terminal typed result, or null while pending.
    godot::Ref<SteamLeaderboardResult> result() const;
    // Return the result immediately or the completion signal while pending.
    godot::Variant wait();
    // End local observation while Steam may still complete the action.
    godot::Error cancel();

protected:
    // Bind the operation methods and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;
    friend class SteamLeaderboards;
    friend class SteamLeaderboardQuery;
    friend class SteamScoreOptions;

    // Construct an operation that settled before starting asynchronous work.
    static godot::Ref<SteamLeaderboardOperation> failed(godot::Error error);
    // Complete once with an error result.
    void fail(godot::Error error);
    // Complete once and clear cancellation before notifying listeners.
    void finish(const godot::Ref<SteamLeaderboardResult> &result);
    // Attach the sole internal continuation or invoke it immediately.
    void when_finished(std::function<void(const godot::Ref<SteamLeaderboardResult> &)> notify);

    // Terminal typed result, null until settlement.
    godot::Ref<SteamLeaderboardResult> outcome;
    // Requests cancellation of the exact native call.
    std::function<void()> cancel_request;
    // Continues a composed request after the native stage settles.
    std::function<void(const godot::Ref<SteamLeaderboardResult> &)> continuation;
};

// Receipt for a score Steam accepted for evaluation.
class SteamScoreUpload : public godot::RefCounted {
    GDCLASS(SteamScoreUpload, godot::RefCounted)

public:
    // Return the signed 32-bit score that was submitted.
    int64_t get_score() const;
    // Return whether the submission became the current entry.
    bool get_changed() const;
    // Return the current global rank Steam reported.
    int64_t get_rank() const;
    // Return the previous rank, or zero if the player had no entry.
    int64_t get_previous_rank() const;

protected:
    // Bind the read-only properties.
    static void _bind_methods();

private:
    friend class SteamLeaderboard;

    // Stores the signed 32-bit score that was submitted.
    int64_t score = 0;
    // Stores whether the submission became the current entry.
    bool changed = false;
    // Stores the current global rank Steam reported.
    int64_t rank = 0;
    // Stores the previous rank, or zero if the player had no entry.
    int64_t previous_rank = 0;
};

// Typed result of a Steam score upload.
class SteamScoreUploadResult : public SteamResult {
    GDCLASS(SteamScoreUploadResult, SteamResult)

public:
    // Return the detached payload, or null when none was accepted.
    godot::Ref<SteamScoreUpload> get_value() const;

protected:
    // Bind the typed read-only payload.
    static void _bind_methods();

private:
    friend class SteamLeaderboard;
    friend class SteamScoreUploadOperation;

    // Construct the terminal result before delivering it to script.
    static godot::Ref<SteamScoreUploadResult> create(godot::Error error, const godot::Ref<SteamScoreUpload> &value = {});

    // Typed payload retained after the Steam scope ends.
    godot::Ref<SteamScoreUpload> value;
};

// Retained observation of a submitted Steam score.
class SteamScoreUploadOperation : public godot::RefCounted {
    GDCLASS(SteamScoreUploadOperation, godot::RefCounted)

public:
    // Report whether the operation has settled.
    bool is_finished() const;
    // Return the terminal typed result, or null while pending.
    godot::Ref<SteamScoreUploadResult> result() const;
    // Return the result immediately or the completion signal while pending.
    godot::Variant wait();
    // End local observation while Steam may still complete the action.
    godot::Error cancel();

protected:
    // Bind the operation methods and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;
    friend class SteamLeaderboard;
    friend class SteamScoreOptions;

    // Construct an operation that settled before starting asynchronous work.
    static godot::Ref<SteamScoreUploadOperation> failed(godot::Error error);
    // Complete once with an error result.
    void fail(godot::Error error);
    // Complete once and clear cancellation before notifying listeners.
    void finish(const godot::Ref<SteamScoreUploadResult> &result);
    // Attach the sole internal continuation or invoke it immediately.
    void when_finished(std::function<void(const godot::Ref<SteamScoreUploadResult> &)> notify);

    // Terminal typed result, null until settlement.
    godot::Ref<SteamScoreUploadResult> outcome;
    // Requests cancellation of the exact native call.
    std::function<void()> cancel_request;
    // Continues a composed request after the native stage settles.
    std::function<void(const godot::Ref<SteamScoreUploadResult> &)> continuation;
};

// Accepted score receipt and the optional replay outcome.
class SteamLeaderboardSubmission : public godot::RefCounted {
    GDCLASS(SteamLeaderboardSubmission, godot::RefCounted)

public:
    // Outcome of the replay stage after Steam accepted the score.
    enum ReplayState {
        NONE, // The caller supplied no replay.
        SKIPPED, // The score did not become the current entry.
        ATTACHED, // Steam attached the replay to the accepted entry.
        FAILED, // The score was accepted but the replay attachment failed.
    };

    // Return the accepted upload receipt.
    godot::Ref<SteamScoreUpload> get_upload() const;
    // Return the signed score supplied to the submission.
    int64_t get_submitted_score() const;
    // Return how the requested replay attachment resolved.
    ReplayState get_replay_state() const;

protected:
    // Bind the read-only properties.
    static void _bind_methods();

private:
    friend class SteamScoreOptions;
    friend class SteamPublisherLeaderboards;

    // Stores the accepted upload receipt.
    godot::Ref<SteamScoreUpload> upload;
    // Submitted signed score, even when the publisher response has no rank information.
    int64_t submitted_score = 0;
    // Stores how the requested replay attachment resolved.
    ReplayState replay_state = NONE;
};

// Score submission outcome with a receipt retained on replay failure.
class SteamLeaderboardSubmissionResult : public SteamResult {
    GDCLASS(SteamLeaderboardSubmissionResult, SteamResult)

public:
    // Return the detached payload, or null when none was accepted.
    godot::Ref<SteamLeaderboardSubmission> get_value() const;

protected:
    // Bind the typed read-only payload.
    static void _bind_methods();

private:
    friend class SteamLeaderboardSubmissionOperation;
    friend class SteamScoreOptions;
    friend class SteamPublisherLeaderboards;

    // Construct the terminal result before delivering it to script.
    static godot::Ref<SteamLeaderboardSubmissionResult> create(godot::Error error, const godot::Ref<SteamLeaderboardSubmission> &value = {});

    // Typed payload retained after the Steam scope ends.
    godot::Ref<SteamLeaderboardSubmission> value;
};

// Carries the typed signal used by a directly awaited score submission.
class SteamLeaderboardSubmissionOperation : public godot::RefCounted {
    GDCLASS(SteamLeaderboardSubmissionOperation, godot::RefCounted)

protected:
    // Bind the typed completion signal.
    static void _bind_methods();

private:
    friend class SteamScoreOptions;

    // Return a settled outcome or the signal that will deliver it.
    godot::Variant wait();
    // Publish the terminal result exactly once.
    void finish(const godot::Ref<SteamLeaderboardSubmissionResult> &result);

    // Terminal result while the submitting coroutine observes it.
    godot::Ref<SteamLeaderboardSubmissionResult> outcome;
};

// Independent options for one keep-best score submission within its original Steam client scope.
class SteamScoreOptions : public godot::RefCounted {
    GDCLASS(SteamScoreOptions, godot::RefCounted)

public:
    // Return a branch with up to 64 optional signed 32-bit details.
    godot::Ref<SteamScoreOptions> with_details(const godot::PackedInt32Array &details) const;
    // Return a branch that attaches a shared replay if the score becomes the current entry.
    godot::Ref<SteamScoreOptions> with_replay(const godot::String &shared_file_handle) const;
    // Await a score submission to an existing board and its optional attachment.
    godot::Variant submit_score(const godot::String &board_name, int64_t score) const;

protected:
    // Bind branch modifiers and the explicit score submission action.
    static void _bind_methods();

private:
    friend class SteamLeaderboards;

    // Copy this draft without extending the Steam scope lifetime.
    godot::Ref<SteamScoreOptions> branch() const;

    // Exact origin scope for any future submission.
    std::weak_ptr<SteamLeaderboardScope> scope;
    // Game-defined details sent with the score.
    godot::PackedInt32Array details;
    // Whether the game explicitly requested an attachment.
    bool attach_replay = false;
    // Full decimal handle supplied by Steam Remote Storage.
    godot::String shared_file_handle;
};

// Addresses a single leaderboard found within the current Steam client scope.
class SteamLeaderboard : public godot::RefCounted {
    GDCLASS(SteamLeaderboard, godot::RefCounted)

public:
    // Report whether the board still belongs to an active client scope.
    bool is_available() const;
    // Read the board name, number of entries, sort, and display format.
    godot::Ref<SteamLeaderboardInfoResult> metadata() const;
    // Download a 1-based rank range with at most 100 entries.
    godot::Ref<SteamLeaderboardPageOperation> entries(int64_t first_rank, int64_t count);
    // Download at most 100 entries centered on the signed-in user's score.
    godot::Ref<SteamLeaderboardPageOperation> around_user(int64_t before, int64_t after);
    // Download friends' scores and return at most the selected number of rows.
    godot::Ref<SteamLeaderboardPageOperation> friends(int64_t limit = 100);
    // Upload a signed 32-bit score and at most 64 details, keeping the best score by default.
    godot::Ref<SteamScoreUploadOperation> upload(int64_t score,
            const godot::PackedInt32Array &details = {}, bool force_update = false);
    // Attach an existing Steam Remote Storage shared-file UGC handle to the local score.
    godot::Ref<SteamOperation> attach_ugc(const godot::String &shared_file_handle);

protected:
    // Register the board API while keeping native SDK handles private.
    static void _bind_methods();

private:
    friend class SteamLeaderboardScope;
    friend class SteamLeaderboards;
    friend class SteamLeaderboardQuery;

    friend class SteamScoreOptions;

    // Submit a score under a raw or composed write reservation.
    godot::Ref<SteamScoreUploadOperation> upload_reserved(int64_t score,
            const godot::PackedInt32Array &details, bool force_update,
            std::shared_ptr<SteamLeaderboardWrite> reservation = {});
    // Submit an attachment under a raw or composed write reservation.
    godot::Ref<SteamOperation> attach_reserved(const godot::String &shared_file_handle,
            std::shared_ptr<SteamLeaderboardWrite> reservation = {});

    // Request and copy a bounded page for the validated leaderboard query mode.
    godot::Ref<SteamLeaderboardPageOperation> download(int mode, int first, int last, int limit,
            const std::vector<uint64_t> &steam_ids = {});

    // Weak scope binding prevents the board from reviving a closed client.
    std::weak_ptr<SteamLeaderboardScope> scope;
    // SDK leaderboard handle that is valid only within the active Steam session.
    uint64_t handle = 0;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamLeaderboards::Sort);
VARIANT_ENUM_CAST(crimson::steam::SteamLeaderboards::Display);
VARIANT_ENUM_CAST(crimson::steam::SteamLeaderboardSubmission::ReplayState);
