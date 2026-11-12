// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_leaderboards.hpp"
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/client/steam_operation.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <type_traits>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <variant>
#include <string>
#include <vector>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Detached leaderboard properties are read-only and carry no saved instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

} // namespace

#ifdef CRIMSON_WITH_STEAM
// Owns a single process-local leaderboard write and follows it across replay attachment.
struct SteamLeaderboardWrite {
    // Exact native runtime for this reservation.
    std::weak_ptr<SteamRuntime> runtime;
};

namespace {

// Bound the number of copied rows in any returned page.
constexpr int maximum_rows = 100;
// Resolve pending operations even if Steam never sends the call result.
constexpr uint64_t timeout_ms = 15000;
// Steam scores and their attachments share one write reservation across local scopes.
std::weak_ptr<SteamLeaderboardWrite> writing;
// Steam accepts one selected-user download at a time across all leases.
std::weak_ptr<SteamRuntime> downloading_users_runtime;

// Keep the SDK-owned outcome reachable until Steam acknowledges the call.
enum class Retention {
    NONE, // No native data requires processing after the operation stops waiting.
    DOWNLOAD, // Read all rows to release the SDK's downloaded entry list.
    USERS_DOWNLOAD, // Read all rows and release the selected-user gate on Steam's result.
    WRITE, // Retain the write reservation until Steam settles the submitted call.
};

// Acquire the write reservation for this runtime without queueing a side effect.
std::shared_ptr<SteamLeaderboardWrite> reserve_write(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto current = writing.lock();
    if (current && current->runtime.lock() == runtime) {
        return {};
    }
    const auto reservation = std::make_shared<SteamLeaderboardWrite>();
    reservation->runtime = runtime;
    writing = reservation;
    return reservation;
}

// Validate a Steam signed 32-bit score and at most 64 signed 32-bit details.
bool valid_score(int64_t score, const PackedInt32Array &details) {
    return score >= (std::numeric_limits<int32>::min)() && score <= (std::numeric_limits<int32>::max)() &&
            details.size() <= k_cLeaderboardDetailsMax;
}

// Map Steam attachment outcomes to Godot's operation error contract.
Error attachment_error(EResult result) {
    switch (result) {
        case k_EResultOK:
            return OK;
        case k_EResultTimeout:
            return ERR_TIMEOUT;
        case k_EResultInvalidParam:
            return ERR_INVALID_PARAMETER;
        case k_EResultAccessDenied:
            return ERR_UNAUTHORIZED;
        default:
            return FAILED;
    }
}

// Resolve this optional interface from an active Steam client.
ISteamUserStats *find_user_stats(const std::shared_ptr<SteamRuntime> &active) {
    if (!active) {
        return nullptr;
    }
    const auto find = active->symbol<decltype(&SteamAPI_SteamUserStats_v013)>("SteamAPI_SteamUserStats_v013");
    return find ? find() : nullptr;
}

// Validate an exact nonempty UTF-8 name before handing it to the C SDK.
bool valid_board_name(const CharString &name) {
    const int64_t length = name.length();
    return length > 0 && length < k_cchLeaderboardNameMax &&
            std::memchr(name.get_data(), 0, static_cast<size_t>(length)) == nullptr;
}

// Convert an unsigned SDK identifier without narrowing it to Godot's signed integer.
String decimal(uint64_t value) {
    return String::utf8(std::to_string(value).c_str());
}

// Parse a shared-file UGC handle represented as an exact decimal string.
bool parse_ugc(const String &input, UGCHandle_t &handle) {
    if (input.is_empty() || input.length() > 20) {
        return false;
    }
    const CharString bytes = input.utf8();
    const char *first = bytes.get_data();
    const char *last = first + bytes.length();
    const auto result = std::from_chars(first, last, handle);
    return result.ec == std::errc() && result.ptr == last && handle != 0 && handle != k_UGCHandleInvalid;
}

// Validate and normalize up to 100 unique individual SteamID64 values.
bool parse_users(const PackedStringArray &inputs, std::vector<uint64_t> &users) {
    if (inputs.is_empty() || inputs.size() > maximum_rows) {
        return false;
    }
    std::set<uint64_t> seen;
    for (int64_t index = 0; index < inputs.size(); ++index) {
        const String input = inputs[index];
        if (input.is_empty() || input.length() > 20) {
            return false;
        }
        const CharString bytes = input.utf8();
        uint64_t value = 0;
        const char *first = bytes.get_data();
        const char *last = first + bytes.length();
        const auto parsed = std::from_chars(first, last, value);
        const CSteamID user(value);
        if (parsed.ec != std::errc() || parsed.ptr != last || !user.IsValid() ||
                !user.BIndividualAccount() || !seen.insert(value).second) {
            return false;
        }
        users.push_back(value);
    }
    return true;
}

// Check the rank bounds without narrowing or overflowing Godot integers.
bool valid_ranks(int64_t first, int64_t count) {
    return count >= 1 && count <= maximum_rows && first >= 1 &&
            first <= (std::numeric_limits<int>::max)() - count + 1;
}

// Check an around-user query's offsets and combined row cap.
bool valid_around(int64_t before, int64_t after) {
    return before >= 0 && before < maximum_rows && after >= 0 && after < maximum_rows &&
            before + after < maximum_rows;
}

// Allow a composed operation to cancel its current stage without owning it.
template <typename Operation>
std::function<void()> cancel_stage(const Ref<Operation> &stage) {
    const ObjectID id(stage->get_instance_id());
    return [id]() {
        const Ref<Operation> live(Object::cast_to<Operation>(ObjectDB::get_instance(id)));
        if (live.is_valid()) {
            live->cancel();
        }
    };
}

} // namespace

// Owns the pending operations that belong to one attached leaderboard service.
class SteamLeaderboardScope : public std::enable_shared_from_this<SteamLeaderboardScope> {
public:
    // Observe the runtime for this lease without extending its lifetime.
    SteamLeaderboardScope(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &client_scope) :
            runtime(active), client_scope(client_scope) {}

    // Read the runtime only on the scene thread where the lease is managed.
    std::shared_ptr<SteamRuntime> get_runtime() const {
        return SteamRuntime::on_main_thread() && client_scope && client_scope->active ? runtime.lock() : nullptr;
    }

    // Attach a newly submitted SDK call to its exact result type and local operation.
    template <typename Result, typename Operation, typename Complete>
    Ref<Operation> watch(const std::shared_ptr<SteamRuntime> &active, SteamAPICall_t call,
            Retention retention, Complete complete, std::shared_ptr<SteamLeaderboardWrite> reservation = {}) {
        if (call == k_uAPICallInvalid) {
            return failed_operation<Operation>(FAILED);
        }
        const auto owner = weak_from_this();
        const std::weak_ptr<SteamRuntime> weak_runtime = active;
        const bool watched = active->watch_call(call, Result::k_iCallback, sizeof(Result),
                [owner, weak_runtime, call, retention, complete = std::move(complete),
                        reservation = std::move(reservation)](const void *raw) mutable {
                    const auto active = weak_runtime.lock();
                    reservation.reset();
                    if (retention == Retention::USERS_DOWNLOAD && downloading_users_runtime.lock() == active) {
                        downloading_users_runtime.reset();
                    }
                    const auto scope = owner.lock();
                    auto pending = scope ? scope->requests.extract(call) : decltype(requests)::node_type{};
                    Ref<Operation> operation;
                    if (!pending.empty()) {
                        operation = std::get<Ref<Operation>>(pending.mapped().operation);
                    }
                    const bool download = retention == Retention::DOWNLOAD || retention == Retention::USERS_DOWNLOAD;
                    if (operation.is_null() && !download) {
                        return;
                    }
                    if (raw && active) {
                        Result result{};
                        std::memcpy(&result, raw, sizeof(result));
                        const auto outcome = complete(active, scope, result, operation.is_valid());
                        if (operation.is_valid()) {
                            operation->finish(outcome);
                        }
                    } else if (operation.is_valid()) {
                        fail_operation(operation, FAILED);
                    }
                });
        if (!watched) {
            return failed_operation<Operation>(ERR_BUSY);
        }
        if (retention == Retention::USERS_DOWNLOAD) {
            downloading_users_runtime = active;
        }
        Ref<Operation> operation;
        operation.instantiate();
        const auto cancel = [owner, call]() -> Error {
            if (const auto scope = owner.lock()) {
                scope->cancel(call, ERR_SKIP);
                return OK;
            }
            return ERR_DOES_NOT_EXIST;
        };
        if constexpr (std::is_same_v<Operation, SteamOperation>) {
            operation->cancel_observation = cancel;
        } else {
            operation->cancel_request = cancel;
        }
        requests.emplace(call, Pending{operation, Time::get_singleton()->get_ticks_msec() + timeout_ms, retention});
        return operation;
    }

    // Consume the full SDK result and publish only the caller's requested, immutable rows.
    static Ref<SteamLeaderboardPageResult> take_entries(const std::shared_ptr<SteamRuntime> &active,
            const LeaderboardScoresDownloaded_t &result, SteamLeaderboard_t expected, int limit) {
        ISteamUserStats *stats = find_user_stats(active);
        const auto read = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry)>(
                "SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry");
        if (!stats || !read || result.m_cEntryCount < 0) {
            return SteamLeaderboardPageResult::create(FAILED);
        }
        bool success = result.m_hSteamLeaderboard == expected;
        Ref<SteamLeaderboardPage> page;
        if (limit > 0) {
            page.instantiate();
        }
        for (int index = 0; index < result.m_cEntryCount; ++index) {
            LeaderboardEntry_t entry{};
            std::array<int32, k_cLeaderboardDetailsMax> details{};
            const bool copy = index < limit && success;
            if (!read(stats, result.m_hSteamLeaderboardEntries, index, &entry,
                        copy ? details.data() : nullptr, copy ? k_cLeaderboardDetailsMax : 0)) {
                success = false;
                continue;
            }
            if (copy) {
                Ref<SteamLeaderboardEntry> row;
                row.instantiate();
                row->steam_id = decimal(entry.m_steamIDUser.ConvertToUint64());
                row->rank = entry.m_nGlobalRank;
                row->score = entry.m_nScore;
                const int length = std::clamp(entry.m_cDetails, 0, static_cast<int>(details.size()));
                row->details.resize(length);
                for (int detail = 0; detail < length; ++detail) {
                    row->details.set(detail, details[detail]);
                }
                if (entry.m_hUGC != k_UGCHandleInvalid && entry.m_hUGC != 0) {
                    row->ugc_handle = decimal(entry.m_hUGC);
                }
                page->entries.push_back(row);
            }
        }
        if (page.is_null()) {
            return {};
        }
        if (!success) {
            return SteamLeaderboardPageResult::create(FAILED);
        }
        page->entries.make_read_only();
        page->total_count = result.m_cEntryCount;
        return SteamLeaderboardPageResult::create(OK, page);
    }

    // Cancel all local operations after invalidating board handles and unwinding observers.
    void close() {
        const auto active = runtime.lock();
        runtime.reset();
        client_scope.reset();
        auto pending = std::move(requests);
        requests.clear();
        if (active) {
            for (const auto &entry : pending) {
                if (entry.second.retention == Retention::NONE) {
                    active->unwatch_call(entry.first);
                }
            }
        }
        for (const auto &entry : pending) {
            entry.second.fail(ERR_SKIP);
        }
    }

    // Complete expired operations outside the map iteration because signals can reenter the client.
    void tick() {
        const uint64_t now = Time::get_singleton()->get_ticks_msec();
        std::vector<SteamAPICall_t> expired;
        for (const auto &entry : requests) {
            if (entry.second.deadline <= now) {
                expired.push_back(entry.first);
            }
        }
        for (const SteamAPICall_t call : expired) {
            cancel(call, ERR_TIMEOUT);
        }
    }

private:
    // Complete an operation with the error shape declared by its binding.
    template <typename Operation>
    static void fail_operation(const Ref<Operation> &operation, Error error) {
        if constexpr (std::is_same_v<Operation, SteamOperation>) {
            operation->finish(SteamResult::from_error(error));
        } else {
            operation->fail(error);
        }
    }

    // Construct an already-settled operation for failed native submission.
    template <typename Operation>
    static Ref<Operation> failed_operation(Error error) {
        Ref<Operation> operation;
        operation.instantiate();
        fail_operation(operation, error);
        return operation;
    }

    // Track one call until it completes or its local operation stops waiting.
    struct Pending {
        // Operation owned by this scope while pending.
        std::variant<Ref<SteamLeaderboardOperation>, Ref<SteamLeaderboardPageOperation>,
                Ref<SteamScoreUploadOperation>, Ref<SteamOperation>> operation;
        // Deadline measured in Godot's monotonic ticks.
        uint64_t deadline = 0;
        // SDK cleanup obligation after local cancellation.
        Retention retention = Retention::NONE;

        // Settle the recorded operation after removing it from the owning map.
        void fail(Error error) const {
            std::visit([error](const auto &pending) { fail_operation(pending, error); }, operation);
        }
    };

    // Remove the operation before running any listener and leave cleanup observers when required.
    void cancel(SteamAPICall_t call, Error error) {
        auto pending = requests.extract(call);
        if (pending.empty()) {
            return;
        }
        if (pending.mapped().retention == Retention::NONE) {
            if (const auto active = runtime.lock()) {
                active->unwatch_call(call);
            }
        }
        pending.mapped().fail(error);
    }

    // Weak runtime binding invalidated before pending results are delivered on scope close.
    std::weak_ptr<SteamRuntime> runtime;
    // Shared liveness flag invalidated before the client closes any service.
    std::shared_ptr<SteamClientScope> client_scope;
    // Operations keyed by their exact native SDK call handles.
    std::map<SteamAPICall_t, Pending> requests;
};
#endif

void SteamLeaderboardEntry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamLeaderboardEntry::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_rank"), &SteamLeaderboardEntry::get_rank);
    ClassDB::bind_method(D_METHOD("get_score"), &SteamLeaderboardEntry::get_score);
    ClassDB::bind_method(D_METHOD("get_details"), &SteamLeaderboardEntry::get_details);
    ClassDB::bind_method(D_METHOD("get_ugc_handle"), &SteamLeaderboardEntry::get_ugc_handle);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "rank", PROPERTY_HINT_NONE, "", read_only), "", "get_rank");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "score", PROPERTY_HINT_NONE, "", read_only), "", "get_score");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "details", PROPERTY_HINT_NONE, "", read_only), "", "get_details");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "ugc_handle", PROPERTY_HINT_NONE, "", read_only), "", "get_ugc_handle");
}

String SteamLeaderboardEntry::get_steam_id() const {
    return steam_id;
}

int64_t SteamLeaderboardEntry::get_rank() const {
    return rank;
}

int64_t SteamLeaderboardEntry::get_score() const {
    return score;
}

PackedInt32Array SteamLeaderboardEntry::get_details() const {
    return details;
}

String SteamLeaderboardEntry::get_ugc_handle() const {
    return ugc_handle;
}

void SteamLeaderboardPage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_entries"), &SteamLeaderboardPage::get_entries);
    ClassDB::bind_method(D_METHOD("get_total_count"), &SteamLeaderboardPage::get_total_count);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_ARRAY_TYPE,
                         SteamLeaderboardEntry::get_class_static(), read_only), "", "get_entries");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total_count", PROPERTY_HINT_NONE, "", read_only), "", "get_total_count");
}

TypedArray<SteamLeaderboardEntry> SteamLeaderboardPage::get_entries() const {
    return entries;
}

int64_t SteamLeaderboardPage::get_total_count() const {
    return total_count;
}

void SteamLeaderboardPageResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamLeaderboardPageResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamLeaderboardPage::get_class_static(), read_only), "", "get_value");
}

Ref<SteamLeaderboardPage> SteamLeaderboardPageResult::get_value() const {
    return value;
}

Ref<SteamLeaderboardPageResult> SteamLeaderboardPageResult::create(Error error, const Ref<SteamLeaderboardPage> &page) {
    Ref<SteamLeaderboardPageResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = page;
    return result;
}

void SteamLeaderboardPageOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamLeaderboardPageOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamLeaderboardPageOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamLeaderboardPageOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamLeaderboardPageOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            SteamLeaderboardPageResult::get_class_static())));
}

bool SteamLeaderboardPageOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamLeaderboardPageResult> SteamLeaderboardPageOperation::result() const {
    return outcome;
}

Variant SteamLeaderboardPageOperation::wait() {
    return is_finished() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamLeaderboardPageOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (!cancel_request) {
        return ERR_DOES_NOT_EXIST;
    }
    const auto request = cancel_request;
    request();
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamLeaderboardPageOperation> SteamLeaderboardPageOperation::failed(Error error) {
    Ref<SteamLeaderboardPageOperation> operation;
    operation.instantiate();
    operation->fail(error);
    return operation;
}

void SteamLeaderboardPageOperation::fail(Error error) {
    finish(SteamLeaderboardPageResult::create(error));
}

void SteamLeaderboardPageOperation::finish(const Ref<SteamLeaderboardPageResult> &result) {
    if (is_finished()) {
        return;
    }
    const Ref<SteamLeaderboardPageOperation> keep_alive(this);
    outcome = result;
    cancel_request = {};
    if (auto notify = std::exchange(continuation, {})) {
        notify(outcome);
    }
    emit_signal("completed", outcome);
}

void SteamLeaderboardPageOperation::when_finished(
        std::function<void(const Ref<SteamLeaderboardPageResult> &)> notify) {
    if (is_finished()) {
        notify(outcome);
    } else {
        continuation = std::move(notify);
    }
}

void SteamLeaderboardQuery::_bind_methods() {
    ClassDB::bind_method(D_METHOD("global_ranks", "first_rank", "count"), &SteamLeaderboardQuery::global_ranks,
            DEFVAL(1), DEFVAL(10));
    ClassDB::bind_method(D_METHOD("friends", "count"), &SteamLeaderboardQuery::friends, DEFVAL(10));
    ClassDB::bind_method(D_METHOD("around_me", "before", "after"), &SteamLeaderboardQuery::around_me,
            DEFVAL(4), DEFVAL(5));
    ClassDB::bind_method(D_METHOD("users", "steam_ids"), &SteamLeaderboardQuery::users);
    ClassDB::bind_method(D_METHOD("fetch"), &SteamLeaderboardQuery::fetch);
    ClassDB::bind_method(D_METHOD("fetch_operation"), &SteamLeaderboardQuery::fetch_operation);
}

Ref<SteamLeaderboardQuery> SteamLeaderboardQuery::select(Selection mode, int64_t first, int64_t second,
        const PackedStringArray &steam_ids) const {
    Ref<SteamLeaderboardQuery> branch;
    branch.instantiate();
    branch->scope = scope;
    branch->board_name = board_name;
    branch->selection = mode;
    branch->first = first;
    branch->second = second;
    branch->steam_ids = steam_ids;
    return branch;
}

Ref<SteamLeaderboardQuery> SteamLeaderboardQuery::global_ranks(int64_t first_rank, int64_t count) const {
    return select(Selection::GLOBAL, first_rank, count);
}

Ref<SteamLeaderboardQuery> SteamLeaderboardQuery::friends(int64_t count) const {
    return select(Selection::FRIENDS, count, 0);
}

Ref<SteamLeaderboardQuery> SteamLeaderboardQuery::around_me(int64_t before, int64_t after) const {
    return select(Selection::AROUND_ME, before, after);
}

Ref<SteamLeaderboardQuery> SteamLeaderboardQuery::users(const PackedStringArray &steam_ids) const {
    return select(Selection::USERS, 0, 0, steam_ids);
}

Variant SteamLeaderboardQuery::fetch() const {
    return fetch_operation()->wait();
}

Ref<SteamLeaderboardPageOperation> SteamLeaderboardQuery::fetch_operation() const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamLeaderboardPageOperation::failed(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!owner || !owner->get_runtime()) {
        return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
    }
    std::vector<uint64_t> users;
    if ((selection == Selection::GLOBAL && !valid_ranks(first, second)) ||
            (selection == Selection::FRIENDS && !valid_ranks(1, first)) ||
            (selection == Selection::AROUND_ME && !valid_around(first, second)) ||
            (selection == Selection::USERS && !parse_users(steam_ids, users))) {
        return SteamLeaderboardPageOperation::failed(ERR_INVALID_PARAMETER);
    }
    const auto lookup = SteamLeaderboards::lookup(owner, board_name, false,
            SteamLeaderboards::DESCENDING, SteamLeaderboards::NUMERIC);
    Ref<SteamLeaderboardPageOperation> operation;
    operation.instantiate();
    operation->cancel_request = cancel_stage(lookup);
    lookup->when_finished([operation, mode = selection, first = first, second = second,
                                  users = std::move(users)](const Ref<SteamLeaderboardResult> &result) {
        if (!result->is_ok()) {
            operation->fail(result->get_error());
            return;
        }
        const Ref<SteamLeaderboard> board = result->get_value();
        Ref<SteamLeaderboardPageOperation> download;
        switch (mode) {
            case Selection::GLOBAL:
                download = board->entries(first, second);
                break;
            case Selection::FRIENDS:
                download = board->friends(first);
                break;
            case Selection::AROUND_ME:
                download = board->around_user(first, second);
                break;
            case Selection::USERS:
                download = board->download(k_ELeaderboardDataRequestUsers, 0, 0, static_cast<int>(users.size()), users);
                break;
        }
        operation->cancel_request = cancel_stage(download);
        download->when_finished([operation](const Ref<SteamLeaderboardPageResult> &page) {
            operation->finish(page);
        });
    });
    return operation;
#else
    return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
#endif
}

void SteamLeaderboardInfo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_name"), &SteamLeaderboardInfo::get_name);
    ClassDB::bind_method(D_METHOD("get_entry_count"), &SteamLeaderboardInfo::get_entry_count);
    ClassDB::bind_method(D_METHOD("get_sort"), &SteamLeaderboardInfo::get_sort);
    ClassDB::bind_method(D_METHOD("get_display"), &SteamLeaderboardInfo::get_display);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", read_only), "", "get_name");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "entry_count", PROPERTY_HINT_NONE, "", read_only), "", "get_entry_count");
    PropertyInfo sort_property = GetTypeInfo<SteamLeaderboards::Sort>::get_class_info();
    sort_property.name = "sort";
    sort_property.usage |= read_only;
    ADD_PROPERTY(sort_property, "", "get_sort");
    PropertyInfo display_property = GetTypeInfo<SteamLeaderboards::Display>::get_class_info();
    display_property.name = "display";
    display_property.usage |= read_only;
    ADD_PROPERTY(display_property, "", "get_display");
}

String SteamLeaderboardInfo::get_name() const {
    return name;
}

int64_t SteamLeaderboardInfo::get_entry_count() const {
    return entry_count;
}

SteamLeaderboards::Sort SteamLeaderboardInfo::get_sort() const {
    return sort;
}

SteamLeaderboards::Display SteamLeaderboardInfo::get_display() const {
    return display;
}

void SteamLeaderboardInfoResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamLeaderboardInfoResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamLeaderboardInfo::get_class_static(), read_only), "", "get_value");
}

Ref<SteamLeaderboardInfo> SteamLeaderboardInfoResult::get_value() const {
    return value;
}

Ref<SteamLeaderboardInfoResult> SteamLeaderboardInfoResult::create(Error error, const Ref<SteamLeaderboardInfo> &value) {
    Ref<SteamLeaderboardInfoResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamLeaderboardResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamLeaderboardResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamLeaderboard::get_class_static(), read_only), "", "get_value");
}

Ref<SteamLeaderboard> SteamLeaderboardResult::get_value() const {
    return value;
}

Ref<SteamLeaderboardResult> SteamLeaderboardResult::create(Error error, const Ref<SteamLeaderboard> &value) {
    Ref<SteamLeaderboardResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamLeaderboardOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamLeaderboardOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamLeaderboardOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamLeaderboardOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamLeaderboardOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            SteamLeaderboardResult::get_class_static())));
}

bool SteamLeaderboardOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamLeaderboardResult> SteamLeaderboardOperation::result() const {
    return outcome;
}

Variant SteamLeaderboardOperation::wait() {
    return is_finished() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamLeaderboardOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (!cancel_request) {
        return ERR_DOES_NOT_EXIST;
    }
    const auto request = cancel_request;
    request();
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamLeaderboardOperation> SteamLeaderboardOperation::failed(Error error) {
    Ref<SteamLeaderboardOperation> operation;
    operation.instantiate();
    operation->fail(error);
    return operation;
}

void SteamLeaderboardOperation::fail(Error error) {
    finish(SteamLeaderboardResult::create(error));
}

void SteamLeaderboardOperation::finish(const Ref<SteamLeaderboardResult> &result) {
    if (is_finished()) {
        return;
    }
    const Ref<SteamLeaderboardOperation> keep_alive(this);
    outcome = result;
    cancel_request = {};
    if (auto notify = std::exchange(continuation, {})) {
        notify(outcome);
    }
    emit_signal("completed", outcome);
}

void SteamLeaderboardOperation::when_finished(
        std::function<void(const Ref<SteamLeaderboardResult> &)> notify) {
    if (is_finished()) {
        notify(outcome);
    } else {
        continuation = std::move(notify);
    }
}

void SteamScoreUpload::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_score"), &SteamScoreUpload::get_score);
    ClassDB::bind_method(D_METHOD("get_changed"), &SteamScoreUpload::get_changed);
    ClassDB::bind_method(D_METHOD("get_rank"), &SteamScoreUpload::get_rank);
    ClassDB::bind_method(D_METHOD("get_previous_rank"), &SteamScoreUpload::get_previous_rank);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "score", PROPERTY_HINT_NONE, "", read_only), "", "get_score");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "changed", PROPERTY_HINT_NONE, "", read_only), "", "get_changed");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "rank", PROPERTY_HINT_NONE, "", read_only), "", "get_rank");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "previous_rank", PROPERTY_HINT_NONE, "", read_only), "", "get_previous_rank");
}

int64_t SteamScoreUpload::get_score() const {
    return score;
}

bool SteamScoreUpload::get_changed() const {
    return changed;
}

int64_t SteamScoreUpload::get_rank() const {
    return rank;
}

int64_t SteamScoreUpload::get_previous_rank() const {
    return previous_rank;
}

void SteamScoreUploadResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamScoreUploadResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamScoreUpload::get_class_static(), read_only), "", "get_value");
}

Ref<SteamScoreUpload> SteamScoreUploadResult::get_value() const {
    return value;
}

Ref<SteamScoreUploadResult> SteamScoreUploadResult::create(Error error, const Ref<SteamScoreUpload> &value) {
    Ref<SteamScoreUploadResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamScoreUploadOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamScoreUploadOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamScoreUploadOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamScoreUploadOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamScoreUploadOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            SteamScoreUploadResult::get_class_static())));
}

bool SteamScoreUploadOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamScoreUploadResult> SteamScoreUploadOperation::result() const {
    return outcome;
}

Variant SteamScoreUploadOperation::wait() {
    return is_finished() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamScoreUploadOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    if (!cancel_request) {
        return ERR_DOES_NOT_EXIST;
    }
    const auto request = cancel_request;
    request();
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamScoreUploadOperation> SteamScoreUploadOperation::failed(Error error) {
    Ref<SteamScoreUploadOperation> operation;
    operation.instantiate();
    operation->fail(error);
    return operation;
}

void SteamScoreUploadOperation::fail(Error error) {
    finish(SteamScoreUploadResult::create(error));
}

void SteamScoreUploadOperation::finish(const Ref<SteamScoreUploadResult> &result) {
    if (is_finished()) {
        return;
    }
    const Ref<SteamScoreUploadOperation> keep_alive(this);
    outcome = result;
    cancel_request = {};
    if (auto notify = std::exchange(continuation, {})) {
        notify(outcome);
    }
    emit_signal("completed", outcome);
}

void SteamScoreUploadOperation::when_finished(
        std::function<void(const Ref<SteamScoreUploadResult> &)> notify) {
    if (is_finished()) {
        notify(outcome);
    } else {
        continuation = std::move(notify);
    }
}

void SteamLeaderboardSubmission::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_upload"), &SteamLeaderboardSubmission::get_upload);
    ClassDB::bind_method(D_METHOD("get_submitted_score"), &SteamLeaderboardSubmission::get_submitted_score);
    ClassDB::bind_method(D_METHOD("get_replay_state"), &SteamLeaderboardSubmission::get_replay_state);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "upload", PROPERTY_HINT_RESOURCE_TYPE, SteamScoreUpload::get_class_static(), read_only), "", "get_upload");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "submitted_score", PROPERTY_HINT_NONE, "", read_only), "", "get_submitted_score");
    PropertyInfo replay_state_property = GetTypeInfo<ReplayState>::get_class_info();
    replay_state_property.name = "replay_state";
    replay_state_property.usage |= read_only;
    ADD_PROPERTY(replay_state_property, "", "get_replay_state");
    BIND_ENUM_CONSTANT(NONE);
    BIND_ENUM_CONSTANT(SKIPPED);
    BIND_ENUM_CONSTANT(ATTACHED);
    BIND_ENUM_CONSTANT(FAILED);
}

Ref<SteamScoreUpload> SteamLeaderboardSubmission::get_upload() const {
    return upload;
}

int64_t SteamLeaderboardSubmission::get_submitted_score() const {
    return submitted_score;
}

SteamLeaderboardSubmission::ReplayState SteamLeaderboardSubmission::get_replay_state() const {
    return replay_state;
}

void SteamLeaderboardSubmissionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamLeaderboardSubmissionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamLeaderboardSubmission::get_class_static(), read_only), "", "get_value");
}

Ref<SteamLeaderboardSubmission> SteamLeaderboardSubmissionResult::get_value() const {
    return value;
}

Ref<SteamLeaderboardSubmissionResult> SteamLeaderboardSubmissionResult::create(Error error, const Ref<SteamLeaderboardSubmission> &value) {
    Ref<SteamLeaderboardSubmissionResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamLeaderboardSubmissionOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            SteamLeaderboardSubmissionResult::get_class_static())));
}

Variant SteamLeaderboardSubmissionOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamLeaderboardSubmissionOperation::finish(const Ref<SteamLeaderboardSubmissionResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamLeaderboardSubmissionOperation> keep_alive(this);
    outcome = result;
    emit_signal("completed", outcome);
}

void SteamScoreOptions::_bind_methods() {
    ClassDB::bind_method(D_METHOD("with_details", "details"), &SteamScoreOptions::with_details);
    ClassDB::bind_method(D_METHOD("with_replay", "shared_file_handle"), &SteamScoreOptions::with_replay);
    ClassDB::bind_method(D_METHOD("submit_score", "board_name", "score"), &SteamScoreOptions::submit_score);
}

Ref<SteamScoreOptions> SteamScoreOptions::branch() const {
    Ref<SteamScoreOptions> next;
    next.instantiate();
    next->scope = scope;
    next->details = details;
    next->attach_replay = attach_replay;
    next->shared_file_handle = shared_file_handle;
    return next;
}

Ref<SteamScoreOptions> SteamScoreOptions::with_details(const PackedInt32Array &details) const {
    const auto next = branch();
    next->details = details;
    return next;
}

Ref<SteamScoreOptions> SteamScoreOptions::with_replay(const String &shared_file_handle) const {
    const auto next = branch();
    next->attach_replay = true;
    next->shared_file_handle = shared_file_handle;
    return next;
}

Variant SteamScoreOptions::submit_score(const String &board_name, int64_t score) const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamLeaderboardSubmissionResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!owner || !owner->get_runtime()) {
        return SteamLeaderboardSubmissionResult::create(ERR_UNAVAILABLE);
    }
    UGCHandle_t validated_handle = 0;
    if (!valid_score(score, details) || (attach_replay && !parse_ugc(shared_file_handle, validated_handle))) {
        return SteamLeaderboardSubmissionResult::create(ERR_INVALID_PARAMETER);
    }
    const auto lookup = SteamLeaderboards::lookup(owner, board_name, false, SteamLeaderboards::DESCENDING,
            SteamLeaderboards::NUMERIC);
    Ref<SteamLeaderboardSubmissionOperation> operation;
    operation.instantiate();
    lookup->when_finished([operation, score, details = details, replay = attach_replay,
                                  shared_file = shared_file_handle](const Ref<SteamLeaderboardResult> &result) {
        if (!result->is_ok()) {
            operation->finish(SteamLeaderboardSubmissionResult::create(result->get_error()));
            return;
        }
        const auto board = result->get_value();
        std::shared_ptr<SteamLeaderboardWrite> reservation;
        if (replay) {
            const auto origin = board->scope.lock();
            const auto active = origin ? origin->get_runtime() : nullptr;
            if (!active) {
                operation->finish(SteamLeaderboardSubmissionResult::create(ERR_UNAVAILABLE));
                return;
            }
            reservation = reserve_write(active);
            if (!reservation) {
                operation->finish(SteamLeaderboardSubmissionResult::create(ERR_BUSY));
                return;
            }
        }

        const auto upload = board->upload_reserved(score, details, false, reservation);
        upload->when_finished([operation, board, replay, shared_file,
                                      reservation = std::move(reservation)](const Ref<SteamScoreUploadResult> &result) mutable {
            if (!result->is_ok()) {
                reservation.reset();
                operation->finish(SteamLeaderboardSubmissionResult::create(result->get_error()));
                return;
            }
            Ref<SteamLeaderboardSubmission> submission;
            submission.instantiate();
            submission->upload = result->get_value();
            submission->submitted_score = submission->upload->get_score();
            if (!replay || !submission->upload->get_changed()) {
                submission->replay_state = replay ? SteamLeaderboardSubmission::SKIPPED : SteamLeaderboardSubmission::NONE;
                reservation.reset();
                operation->finish(SteamLeaderboardSubmissionResult::create(OK, submission));
                return;
            }
            const auto attachment = board->attach_reserved(shared_file, reservation);
            attachment->when_finished([operation, submission,
                                              reservation = std::move(reservation)](const Ref<SteamResult> &attached) mutable {
                submission->replay_state = attached->is_ok() ? SteamLeaderboardSubmission::ATTACHED :
                        SteamLeaderboardSubmission::FAILED;
                reservation.reset();
                operation->finish(SteamLeaderboardSubmissionResult::create(attached->get_error(), submission));
            });
        });
    });
    return operation->wait();
#else
    return SteamLeaderboardSubmissionResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamLeaderboard::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamLeaderboard::is_available);
    ClassDB::bind_method(D_METHOD("metadata"), &SteamLeaderboard::metadata);
    ClassDB::bind_method(D_METHOD("entries", "first_rank", "count"), &SteamLeaderboard::entries);
    ClassDB::bind_method(D_METHOD("around_user", "before", "after"), &SteamLeaderboard::around_user);
    ClassDB::bind_method(D_METHOD("friends", "limit"), &SteamLeaderboard::friends, DEFVAL(100));
    ClassDB::bind_method(D_METHOD("upload", "score", "details", "force_update"),
            &SteamLeaderboard::upload, DEFVAL(PackedInt32Array()), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("attach_ugc", "shared_file_handle"), &SteamLeaderboard::attach_ugc);
}

bool SteamLeaderboard::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto owner = scope.lock();
        return handle != 0 && owner && find_user_stats(owner->get_runtime()) != nullptr;
    }
#endif
    return false;
}

Ref<SteamLeaderboardInfoResult> SteamLeaderboard::metadata() const {
#ifdef CRIMSON_WITH_STEAM
    if (!is_available()) {
        return SteamLeaderboardInfoResult::create(ERR_UNAVAILABLE);
    }
    const auto active = scope.lock()->get_runtime();
    ISteamUserStats *stats = find_user_stats(active);
    const auto name = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetLeaderboardName)>(
            "SteamAPI_ISteamUserStats_GetLeaderboardName");
    const auto entries = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetLeaderboardEntryCount)>(
            "SteamAPI_ISteamUserStats_GetLeaderboardEntryCount");
    const auto sort = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetLeaderboardSortMethod)>(
            "SteamAPI_ISteamUserStats_GetLeaderboardSortMethod");
    const auto display = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetLeaderboardDisplayType)>(
            "SteamAPI_ISteamUserStats_GetLeaderboardDisplayType");
    if (!name || !entries || !sort || !display) {
        return SteamLeaderboardInfoResult::create(ERR_UNAVAILABLE);
    }
    const char *native_name = name(stats, handle);
    if (!native_name || !*native_name) {
        return SteamLeaderboardInfoResult::create(FAILED);
    }
    const String copied_name = String::utf8(native_name);
    const auto count = entries(stats, handle);
    const auto sorting = sort(stats, handle);
    const auto format = display(stats, handle);
    if (count < 0 || (sorting != k_ELeaderboardSortMethodAscending && sorting != k_ELeaderboardSortMethodDescending) ||
            (format != k_ELeaderboardDisplayTypeNumeric && format != k_ELeaderboardDisplayTypeTimeSeconds &&
                    format != k_ELeaderboardDisplayTypeTimeMilliSeconds)) {
        return SteamLeaderboardInfoResult::create(FAILED);
    }
    Ref<SteamLeaderboardInfo> info;
    info.instantiate();
    info->name = copied_name;
    info->entry_count = count;
    info->sort = static_cast<SteamLeaderboards::Sort>(sorting);
    info->display = static_cast<SteamLeaderboards::Display>(format);
    return SteamLeaderboardInfoResult::create(OK, info);
#else
    return SteamLeaderboardInfoResult::create(ERR_UNAVAILABLE);
#endif
}

Ref<SteamLeaderboardPageOperation> SteamLeaderboard::entries(int64_t first_rank, int64_t count) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_ranks(first_rank, count)) {
        return SteamLeaderboardPageOperation::failed(ERR_INVALID_PARAMETER);
    }
    return download(k_ELeaderboardDataRequestGlobal, static_cast<int>(first_rank),
            static_cast<int>(first_rank + count - 1), static_cast<int>(count));
#else
    return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
#endif
}

Ref<SteamLeaderboardPageOperation> SteamLeaderboard::around_user(int64_t before, int64_t after) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_around(before, after)) {
        return SteamLeaderboardPageOperation::failed(ERR_INVALID_PARAMETER);
    }
    return download(k_ELeaderboardDataRequestGlobalAroundUser, -static_cast<int>(before), static_cast<int>(after),
            static_cast<int>(before + after + 1));
#else
    return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
#endif
}

Ref<SteamLeaderboardPageOperation> SteamLeaderboard::friends(int64_t limit) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_ranks(1, limit)) {
        return SteamLeaderboardPageOperation::failed(ERR_INVALID_PARAMETER);
    }
    return download(k_ELeaderboardDataRequestFriends, 0, 0, static_cast<int>(limit));
#else
    return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
#endif
}

Ref<SteamLeaderboardPageOperation> SteamLeaderboard::download(int mode, int first, int last, int limit,
        const std::vector<uint64_t> &steam_ids) {
#ifdef CRIMSON_WITH_STEAM
    if (!is_available()) {
        return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
    }
    const auto owner = scope.lock();
    const auto active = owner->get_runtime();
    const bool selected = mode == k_ELeaderboardDataRequestUsers;
    if (!active->can_track_call() || (selected && downloading_users_runtime.lock() == active)) {
        return SteamLeaderboardPageOperation::failed(ERR_BUSY);
    }
    const auto read = active->symbol<decltype(&SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry)>(
            "SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry");
    if (!read) {
        return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
    }
    ISteamUserStats *stats = find_user_stats(active);
    SteamAPICall_t call;
    if (selected) {
        const auto start = active->symbol<decltype(&SteamAPI_ISteamUserStats_DownloadLeaderboardEntriesForUsers)>(
                "SteamAPI_ISteamUserStats_DownloadLeaderboardEntriesForUsers");
        if (!start) {
            return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
        }
        std::vector<CSteamID> users(steam_ids.begin(), steam_ids.end());
        call = start(stats, handle, users.data(), static_cast<int>(users.size()));
    } else {
        const auto start = active->symbol<decltype(&SteamAPI_ISteamUserStats_DownloadLeaderboardEntries)>(
                "SteamAPI_ISteamUserStats_DownloadLeaderboardEntries");
        if (!start) {
            return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
        }
        call = start(stats, handle, static_cast<ELeaderboardDataRequest>(mode), first, last);
    }
    return owner->watch<LeaderboardScoresDownloaded_t, SteamLeaderboardPageOperation>(active, call,
            selected ? Retention::USERS_DOWNLOAD : Retention::DOWNLOAD,
            [expected = handle, limit](const auto &active, const auto &, const auto &result, bool deliver) {
                return SteamLeaderboardScope::take_entries(active, result, expected, deliver ? limit : 0);
            });
#else
    return SteamLeaderboardPageOperation::failed(ERR_UNAVAILABLE);
#endif
}

Ref<SteamScoreUploadOperation> SteamLeaderboard::upload(int64_t score, const PackedInt32Array &details, bool force_update) {
    return upload_reserved(score, details, force_update);
}

Ref<SteamScoreUploadOperation> SteamLeaderboard::upload_reserved(int64_t score, const PackedInt32Array &details,
        bool force_update, std::shared_ptr<SteamLeaderboardWrite> reservation) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_score(score, details)) {
        return SteamScoreUploadOperation::failed(ERR_INVALID_PARAMETER);
    }
    if (!is_available()) {
        return SteamScoreUploadOperation::failed(ERR_UNAVAILABLE);
    }
    const auto owner = scope.lock();
    const auto active = owner->get_runtime();
    if (!active->can_track_call()) {
        return SteamScoreUploadOperation::failed(ERR_BUSY);
    }
    const auto start = active->symbol<decltype(&SteamAPI_ISteamUserStats_UploadLeaderboardScore)>(
            "SteamAPI_ISteamUserStats_UploadLeaderboardScore");
    if (!start) {
        return SteamScoreUploadOperation::failed(ERR_UNAVAILABLE);
    }
    if (!reservation) {
        reservation = reserve_write(active);
    }
    if (!reservation || reservation->runtime.lock() != active) {
        return SteamScoreUploadOperation::failed(ERR_BUSY);
    }
    const auto method = force_update ? k_ELeaderboardUploadScoreMethodForceUpdate : k_ELeaderboardUploadScoreMethodKeepBest;
    const SteamAPICall_t call = start(find_user_stats(active), handle, method, static_cast<int32>(score),
            details.ptr(), static_cast<int>(details.size()));
    return owner->watch<LeaderboardScoreUploaded_t, SteamScoreUploadOperation>(active, call, Retention::WRITE,
            [expected = handle](const auto &, const auto &, const auto &result, bool) {
                if (!result.m_bSuccess || result.m_hSteamLeaderboard != expected) {
                    return SteamScoreUploadResult::create(FAILED);
                }
                Ref<SteamScoreUpload> upload;
                upload.instantiate();
                upload->score = result.m_nScore;
                upload->changed = result.m_bScoreChanged != 0;
                upload->rank = result.m_nGlobalRankNew;
                upload->previous_rank = result.m_nGlobalRankPrevious;
                return SteamScoreUploadResult::create(OK, upload);
            }, std::move(reservation));
#else
    return SteamScoreUploadOperation::failed(ERR_UNAVAILABLE);
#endif
}

Ref<SteamOperation> SteamLeaderboard::attach_ugc(const String &shared_file_handle) {
    return attach_reserved(shared_file_handle);
}

Ref<SteamOperation> SteamLeaderboard::attach_reserved(const String &shared_file_handle,
        std::shared_ptr<SteamLeaderboardWrite> reservation) {
    const auto failed = [](Error error) {
        Ref<SteamOperation> operation;
        operation.instantiate();
        operation->finish(SteamResult::from_error(error));
        return operation;
    };
#ifdef CRIMSON_WITH_STEAM
    UGCHandle_t ugc = 0;
    if (!parse_ugc(shared_file_handle, ugc)) {
        return failed(ERR_INVALID_PARAMETER);
    }
    if (!is_available()) {
        return failed(ERR_UNAVAILABLE);
    }
    const auto owner = scope.lock();
    const auto active = owner->get_runtime();
    if (!active->can_track_call()) {
        return failed(ERR_BUSY);
    }
    const auto start = active->symbol<decltype(&SteamAPI_ISteamUserStats_AttachLeaderboardUGC)>(
            "SteamAPI_ISteamUserStats_AttachLeaderboardUGC");
    if (!start) {
        return failed(ERR_UNAVAILABLE);
    }
    if (!reservation) {
        reservation = reserve_write(active);
    }
    if (!reservation || reservation->runtime.lock() != active) {
        return failed(ERR_BUSY);
    }
    const SteamAPICall_t call = start(find_user_stats(active), handle, ugc);
    return owner->watch<LeaderboardUGCSet_t, SteamOperation>(active, call, Retention::WRITE,
            [expected = handle](const auto &, const auto &, const auto &result, bool) {
                if (result.m_hSteamLeaderboard != expected) {
                    return SteamResult::from_error(FAILED, "Steam returned an attachment result for a different board.");
                }
                const Error error = attachment_error(result.m_eResult);
                return SteamResult::from_error(error, error == OK ? String() : "Steam rejected the replay attachment.");
            }, std::move(reservation));
#else
    return failed(ERR_UNAVAILABLE);
#endif
}

void SteamLeaderboards::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamLeaderboards::is_available);
    ClassDB::bind_method(D_METHOD("top", "board_name", "count"), &SteamLeaderboards::top, DEFVAL(10));
    ClassDB::bind_method(D_METHOD("friends", "board_name", "count"), &SteamLeaderboards::friends, DEFVAL(10));
    ClassDB::bind_method(D_METHOD("query", "board_name"), &SteamLeaderboards::query);
    ClassDB::bind_method(D_METHOD("submit_score", "board_name", "score"), &SteamLeaderboards::submit_score);
    ClassDB::bind_method(D_METHOD("with_details", "details"), &SteamLeaderboards::with_details);
    ClassDB::bind_method(D_METHOD("with_replay", "shared_file_handle"), &SteamLeaderboards::with_replay);
    ClassDB::bind_method(D_METHOD("find", "name"), &SteamLeaderboards::find);
    ClassDB::bind_method(D_METHOD("find_or_create", "name", "sort", "display"), &SteamLeaderboards::find_or_create,
            DEFVAL(DESCENDING), DEFVAL(NUMERIC));
    BIND_ENUM_CONSTANT(ASCENDING);
    BIND_ENUM_CONSTANT(DESCENDING);
    BIND_ENUM_CONSTANT(NUMERIC);
    BIND_ENUM_CONSTANT(SECONDS);
    BIND_ENUM_CONSTANT(MILLISECONDS);
}

void SteamLeaderboards::start_scope(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &client_scope) {
#ifdef CRIMSON_WITH_STEAM
    scope = std::make_shared<SteamLeaderboardScope>(active, client_scope);
#endif
}

void SteamLeaderboards::close() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto retired = std::exchange(scope, nullptr)) {
        retired->close();
    }
#endif
}

void SteamLeaderboards::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = scope) {
        active->tick();
    }
#endif
}

bool SteamLeaderboards::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return SteamRuntime::on_main_thread() && scope && find_user_stats(scope->get_runtime()) != nullptr;
#else
    return false;
#endif
}

Variant SteamLeaderboards::top(const String &board_name, int64_t count) {
    return query(board_name)->global_ranks(1, count)->fetch();
}

Variant SteamLeaderboards::friends(const String &board_name, int64_t count) {
    return query(board_name)->friends(count)->fetch();
}

Ref<SteamLeaderboardQuery> SteamLeaderboards::query(const String &board_name) const {
    Ref<SteamLeaderboardQuery> query;
    query.instantiate();
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        query->scope = scope;
    }
#endif
    query->board_name = board_name;
    return query;
}

Ref<SteamScoreOptions> SteamLeaderboards::score_options() const {
    Ref<SteamScoreOptions> options;
    options.instantiate();
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        options->scope = scope;
    }
#endif
    return options;
}

Variant SteamLeaderboards::submit_score(const String &board_name, int64_t score) const {
    return score_options()->submit_score(board_name, score);
}

Ref<SteamScoreOptions> SteamLeaderboards::with_details(const PackedInt32Array &details) const {
    return score_options()->with_details(details);
}

Ref<SteamScoreOptions> SteamLeaderboards::with_replay(const String &shared_file_handle) const {
    return score_options()->with_replay(shared_file_handle);
}

Ref<SteamLeaderboardOperation> SteamLeaderboards::find(const String &name) {
    return lookup(scope, name, false, DESCENDING, NUMERIC);
}

Ref<SteamLeaderboardOperation> SteamLeaderboards::find_or_create(const String &name, Sort sort, Display display) {
    if ((sort != ASCENDING && sort != DESCENDING) ||
            (display != NUMERIC && display != SECONDS && display != MILLISECONDS)) {
        return SteamLeaderboardOperation::failed(ERR_INVALID_PARAMETER);
    }
    return lookup(scope, name, true, sort, display);
}

Ref<SteamLeaderboardOperation> SteamLeaderboards::lookup(const std::shared_ptr<SteamLeaderboardScope> &owner,
        const String &name, bool create, Sort sort, Display display) {
#ifdef CRIMSON_WITH_STEAM
    const CharString encoded = name.utf8();
    if (!valid_board_name(encoded)) {
        return SteamLeaderboardOperation::failed(ERR_INVALID_PARAMETER);
    }
    if (!SteamRuntime::on_main_thread()) {
        return SteamLeaderboardOperation::failed(ERR_BUSY);
    }
    const auto active = owner ? owner->get_runtime() : nullptr;
    ISteamUserStats *stats = find_user_stats(active);
    if (!stats) {
        return SteamLeaderboardOperation::failed(ERR_UNAVAILABLE);
    }
    if (!active->can_track_call()) {
        return SteamLeaderboardOperation::failed(ERR_BUSY);
    }
    SteamAPICall_t call = k_uAPICallInvalid;
    if (create) {
        const auto start = active->symbol<decltype(&SteamAPI_ISteamUserStats_FindOrCreateLeaderboard)>(
                "SteamAPI_ISteamUserStats_FindOrCreateLeaderboard");
        if (!start) {
            return SteamLeaderboardOperation::failed(ERR_UNAVAILABLE);
        }
        call = start(stats, encoded.get_data(), static_cast<ELeaderboardSortMethod>(sort),
                static_cast<ELeaderboardDisplayType>(display));
    } else {
        const auto start = active->symbol<decltype(&SteamAPI_ISteamUserStats_FindLeaderboard)>(
                "SteamAPI_ISteamUserStats_FindLeaderboard");
        if (!start) {
            return SteamLeaderboardOperation::failed(ERR_UNAVAILABLE);
        }
        call = start(stats, encoded.get_data());
    }
    return owner->watch<LeaderboardFindResult_t, SteamLeaderboardOperation>(active, call, Retention::NONE,
            [](const auto &, const auto &scope, const auto &result, bool) {
                if (!result.m_bLeaderboardFound || !result.m_hSteamLeaderboard) {
                    return SteamLeaderboardResult::create(ERR_DOES_NOT_EXIST);
                }
                Ref<SteamLeaderboard> board;
                board.instantiate();
                board->scope = scope;
                board->handle = result.m_hSteamLeaderboard;
                return SteamLeaderboardResult::create(OK, board);
            });
#else
    return SteamLeaderboardOperation::failed(ERR_UNAVAILABLE);
#endif
}

} // namespace crimson::steam
