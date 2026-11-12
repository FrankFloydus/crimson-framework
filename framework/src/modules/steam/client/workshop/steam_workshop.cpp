// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/workshop/steam_workshop.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/object.hpp>

#include <charconv>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#endif

namespace crimson::steam {

using namespace godot;

namespace {

// Bound query text and cursor to one small UTF-8 SDK parameter.
constexpr int64_t maximum_filter_bytes = 255;
// Bound the number of required tags on one branch.
constexpr size_t maximum_tags = 16;

// Validate the UTF-8 argument passed to the C API, with no interior terminator.
bool bounded_text(const String &value, int64_t max_bytes, bool allow_empty) {
    const CharString encoded = value.utf8();
    const int64_t length = encoded.length();
    return (allow_empty || length > 0) && length <= max_bytes &&
            (length == 0 || std::memchr(encoded.get_data(), '\0', static_cast<size_t>(length)) == nullptr);
}

#ifdef CRIMSON_WITH_STEAM
// Bound native handles retained by one client scope, including cleanup-only calls.
constexpr size_t maximum_queries = 12;
// Limit the time an observer waits while Steam handles an exact query call.
constexpr uint64_t query_timeout_ms = 20000;
// Bound mutation, installation, and publication observers in one scope.
constexpr size_t maximum_tasks = 12;
// Bound previously verified item IDs used for synchronous install snapshots.
constexpr size_t maximum_verified_items = 512;
// Bound outstanding callback-only downloads across scopes sharing the runtime.
constexpr size_t maximum_downloads = 8;
// Allow time for a large content transfer while retaining a finite local deadline.
constexpr uint64_t download_timeout_ms = 120000;
// Let a bounded publication wait long enough for Steam to stage and upload its content.
constexpr uint64_t publication_timeout_ms = 180000;
// Bound the absolute local content path copied from Steam.
constexpr size_t maximum_install_path = 4096;
// Steam accepts one preview image smaller than one MiB and at least 16 bytes long.
constexpr uintmax_t maximum_preview_bytes = 1024 * 1024;

// Keep a borrowed Workshop interface alive for the duration of one native action.
struct WorkshopAccess {
    // Runtime lease retained for this SDK action.
    std::shared_ptr<SteamRuntime> runtime;
    // SDK Workshop interface for the active client.
    ISteamUGC *ugc = nullptr;
};

// Resolve the Workshop interface on the Steam client thread.
WorkshopAccess access_workshop(const std::weak_ptr<SteamRuntime> &weak) {
    if (!SteamRuntime::on_main_thread()) {
        return {};
    }
    const auto runtime = weak.lock();
    if (!runtime) {
        return {};
    }
    const auto get_ugc = runtime->symbol<decltype(&SteamAPI_SteamUGC_v021)>("SteamAPI_SteamUGC_v021");
    return {runtime, get_ugc ? get_ugc() : nullptr};
}

// Parse a published file ID without narrowing to a Godot signed integer.
bool parse_published_id(const String &input, PublishedFileId_t &id) {
    const CharString text = input.utf8();
    const char *first = text.get_data();
    if (text.length() < 1 || text.length() > 20 || first[0] == '0') {
        return false;
    }
    const char *last = first + text.length();
    const auto parsed = std::from_chars(first, last, id);
    return parsed.ec == std::errc() && parsed.ptr == last && id != k_PublishedFileIdInvalid;
}

// Preserve unsigned SDK identifiers in their canonical decimal form.
String decimal_id(uint64_t id) {
    return String::utf8(std::to_string(id).c_str());
}

// Convert Godot's UTF-8 path to the host filesystem representation.
std::filesystem::path native_path(const String &path) {
    const CharString bytes = path.utf8();
    return std::filesystem::u8path(bytes.get_data(), bytes.get_data() + bytes.length());
}

// Copy a fixed-size Steam UTF-8 field only when it has a terminator inside its own buffer.
template <size_t Size>
bool copy_native_text(const char (&source)[Size], String &value) {
    const auto *end = static_cast<const char *>(std::memchr(source, '\0', Size));
    if (!end) {
        return false;
    }
    if (end == source) {
        value = String();
        return true;
    }
    return value.parse_utf8(source, static_cast<int64_t>(end - source)) == OK;
}

// Translate a terminal native query result into an observable Godot error.
Error query_error(EResult result) {
    switch (result) {
        case k_EResultOK: return OK;
        case k_EResultFileNotFound: return ERR_FILE_NOT_FOUND;
        case k_EResultAccessDenied: return ERR_UNAUTHORIZED;
        case k_EResultInvalidParam: return ERR_INVALID_PARAMETER;
        case k_EResultTimeout: return ERR_TIMEOUT;
        case k_EResultServiceUnavailable: return ERR_UNAVAILABLE;
        default: return FAILED;
    }
}

// Resolve the native order requested by the public query branch.
EUGCQuery native_sort(SteamWorkshopQuery::Sort sort) {
    switch (sort) {
        case SteamWorkshopQuery::RELEVANCE: return k_EUGCQuery_RankedByTextSearch;
        case SteamWorkshopQuery::NEWEST: return k_EUGCQuery_RankedByPublicationDate;
        default: return k_EUGCQuery_RankedByVote;
    }
}

// Map the public visibility choice to Steam's independent enum values.
ERemoteStoragePublishedFileVisibility native_visibility(SteamWorkshopPublishRequest::Visibility visibility) {
    switch (visibility) {
        case SteamWorkshopPublishRequest::FRIENDS_ONLY: return k_ERemoteStoragePublishedFileVisibilityFriendsOnly;
        case SteamWorkshopPublishRequest::PUBLIC: return k_ERemoteStoragePublishedFileVisibilityPublic;
        case SteamWorkshopPublishRequest::UNLISTED: return k_ERemoteStoragePublishedFileVisibilityUnlisted;
        default: return k_ERemoteStoragePublishedFileVisibilityPrivate;
    }
}

// Resolve all update exports before creating an item or beginning an edit.
struct WorkshopUpdateApi {
    // Open a mutable SDK update handle for one item.
    decltype(&SteamAPI_ISteamUGC_StartItemUpdate) start;
    // Set the replacement item title.
    decltype(&SteamAPI_ISteamUGC_SetItemTitle) title;
    // Set the replacement item description.
    decltype(&SteamAPI_ISteamUGC_SetItemDescription) description;
    // Set the local content folder to upload.
    decltype(&SteamAPI_ISteamUGC_SetItemContent) content;
    // Set the local preview image to upload.
    decltype(&SteamAPI_ISteamUGC_SetItemPreview) preview;
    // Set the replacement Workshop tags.
    decltype(&SteamAPI_ISteamUGC_SetItemTags) tags;
    // Set the audience of the publication.
    decltype(&SteamAPI_ISteamUGC_SetItemVisibility) visibility;
    // Submit the prepared handle to Steam.
    decltype(&SteamAPI_ISteamUGC_SubmitItemUpdate) submit;

    // Require all public update actions on the supported interface.
    bool available() const {
        return start && title && description && content && preview && tags && visibility && submit;
    }
};

// Copy the Workshop update exports from the initialized Steam runtime.
WorkshopUpdateApi update_api(const WorkshopAccess &workshop) {
    return {
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_StartItemUpdate)>("SteamAPI_ISteamUGC_StartItemUpdate"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetItemTitle)>("SteamAPI_ISteamUGC_SetItemTitle"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetItemDescription)>("SteamAPI_ISteamUGC_SetItemDescription"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetItemContent)>("SteamAPI_ISteamUGC_SetItemContent"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetItemPreview)>("SteamAPI_ISteamUGC_SetItemPreview"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetItemTags)>("SteamAPI_ISteamUGC_SetItemTags"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetItemVisibility)>("SteamAPI_ISteamUGC_SetItemVisibility"),
        workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SubmitItemUpdate)>("SteamAPI_ISteamUGC_SubmitItemUpdate"),
    };
}

// Share exact native download and update reservations across clients of one runtime.
struct WorkshopReservations {
    // Item IDs held until their matching native callback arrives.
    std::set<PublishedFileId_t> downloads;
    // Item IDs held until the exact pending update finishes.
    std::set<PublishedFileId_t> updates;
};

// Reuse live reservations without keeping them alive after runtime shutdown.
std::weak_ptr<WorkshopReservations> active_reservations;
#endif

} // namespace

#ifdef CRIMSON_WITH_STEAM
// Track one exact Steam query independently of the waiting script and facade.
struct SteamWorkshopQueryCall {
    // Weak native runtime avoids a cycle with its own call watcher.
    std::weak_ptr<SteamRuntime> runtime;
    // Scope owns observation only while this reference is retained.
    Ref<SteamWorkshopAction> action;
    // Native query handle owned through callback decoding and released once.
    UGCQueryHandle_t handle = k_UGCQueryHandleInvalid;
    // Exact item expected for an item query, or invalid for a page query.
    PublishedFileId_t published_id = k_PublishedFileIdInvalid;
    // Only data for this consumer AppID may enter a result.
    AppId_t app_id = k_uAppIdInvalid;
    // Request cursor used to detect repeated continuation at the end of paging.
    String cursor;
    // Remember consumer-app validation for later local installation checks.
    std::function<void(PublishedFileId_t)> remember;
    // Local observer deadline. Cleanup continues if Steam completes later.
    uint64_t deadline = 0;

    // Retire any handle left by a synchronous submission failure.
    ~SteamWorkshopQueryCall() {
        release_query();
    }

    // Release the owned query before user callbacks can submit another request.
    void release_query() {
        const auto query = std::exchange(handle, k_UGCQueryHandleInvalid);
        if (query == k_UGCQueryHandleInvalid) {
            return;
        }
        const auto access = access_workshop(runtime);
        if (access.ugc) {
            const auto release = access.runtime->symbol<decltype(&SteamAPI_ISteamUGC_ReleaseQueryUGCRequest)>(
                    "SteamAPI_ISteamUGC_ReleaseQueryUGCRequest");
            if (release) {
                release(access.ugc, query);
            }
        }
    }

    // Publish a terminal result once and discard any later observation.
    void settle(const Ref<SteamResult> &result) {
        const Ref<SteamWorkshopAction> target = std::exchange(action, {});
        if (target.is_valid()) {
            target->finish(result);
        }
    }

    // Construct the appropriate typed error while keeping the native watcher alive.
    void stop_observing(Error error, const String &message = {}) {
        if (action.is_null()) {
            return;
        }
        if (published_id == k_PublishedFileIdInvalid) {
            settle(SteamWorkshopPageResult::create(error, message));
        } else {
            settle(SteamWorkshopItemResult::create(error, message));
        }
    }

    // Copy one successful row for the expected consumer app while the query remains valid.
    Ref<SteamWorkshopItem> copy_item(const SteamUGCDetails_t &details) const {
        if (details.m_eResult != k_EResultOK || details.m_nPublishedFileId == k_PublishedFileIdInvalid ||
                details.m_nConsumerAppID != app_id || details.m_bBanned) {
            return {};
        }
        String title;
        String description;
        if (!copy_native_text(details.m_rgchTitle, title) || !copy_native_text(details.m_rgchDescription, description)) {
            return {};
        }
        const String author = details.m_ulSteamIDOwner != 0 ? decimal_id(details.m_ulSteamIDOwner) : String();
        if (remember) {
            remember(details.m_nPublishedFileId);
        }
        return SteamWorkshopItem::create(decimal_id(details.m_nPublishedFileId), title, description, author);
    }

    // Decode the callback's exact owned query and release it before resuming scripts.
    void complete(const void *payload) {
        if (action.is_null()) {
            release_query();
            return;
        }
        if (Time::get_singleton()->get_ticks_msec() >= deadline) {
            release_query();
            stop_observing(ERR_TIMEOUT);
            return;
        }
        SteamUGCQueryCompleted_t result{};
        if (payload) {
            std::memcpy(&result, payload, sizeof(result));
        }
        if (!payload || result.m_handle != handle || result.m_eResult != k_EResultOK ||
                result.m_unNumResultsReturned > kNumUGCResultsPerPage) {
            const Error error = payload && result.m_handle == handle && result.m_eResult != k_EResultOK
                    ? query_error(result.m_eResult)
                    : ERR_CANT_CONNECT;
            release_query();
            stop_observing(error, "Steam Workshop query failed (EResult " + String::num_int64(result.m_eResult) + ").");
            return;
        }

        const auto access = access_workshop(runtime);
        const auto get_row = access.ugc
                ? access.runtime->symbol<decltype(&SteamAPI_ISteamUGC_GetQueryUGCResult)>(
                        "SteamAPI_ISteamUGC_GetQueryUGCResult")
                : nullptr;
        if (!get_row) {
            release_query();
            stop_observing(ERR_UNAVAILABLE);
            return;
        }
        if (published_id != k_PublishedFileIdInvalid) {
            SteamUGCDetails_t details{};
            Ref<SteamWorkshopItem> item;
            Error error = ERR_FILE_NOT_FOUND;
            if (result.m_unNumResultsReturned == 1 && get_row(access.ugc, handle, 0, &details) &&
                    details.m_nPublishedFileId == published_id) {
                item = copy_item(details);
                error = item.is_valid() ? OK : ERR_FILE_NOT_FOUND;
                if (details.m_eResult != k_EResultOK) {
                    error = query_error(details.m_eResult);
                }
            }
            release_query();
            settle(SteamWorkshopItemResult::create(error, {}, item));
            return;
        }

        TypedArray<SteamWorkshopItem> items;
        for (uint32 index = 0; index < result.m_unNumResultsReturned; ++index) {
            SteamUGCDetails_t details{};
            if (get_row(access.ugc, handle, index, &details)) {
                const auto item = copy_item(details);
                if (item.is_valid()) {
                    items.push_back(item);
                }
            }
        }
        String next;
        const bool valid_cursor = copy_native_text(result.m_rgchNextCursor, next);
        if (next == cursor || next == "*") {
            next = String();
        }
        release_query();
        if (!valid_cursor) {
            stop_observing(ERR_INVALID_DATA, "Steam Workshop returned an invalid continuation cursor.");
            return;
        }
        settle(SteamWorkshopPageResult::create(OK, {}, SteamWorkshopPage::create(items, next)));
    }
};

// Retain one typed Workshop action through SDK completion and local scope retirement.
struct SteamWorkshopTask {
    // Each public action keeps its own terminal result type.
    enum class Kind { SUBSCRIPTION, INSTALL, VOTE, PUBLICATION, LEGAL_AGREEMENT };

    // Type of terminal result this observer expects.
    Kind kind = Kind::SUBSCRIPTION;
    // Signal owner while a script still observes this action.
    Ref<SteamWorkshopAction> action;
    // Runtime reference for exact download subscription cleanup.
    std::weak_ptr<SteamRuntime> runtime;
    // ID selected or created for this action.
    PublishedFileId_t id = k_PublishedFileIdInvalid;
    // Exact consumer app for callback-only download completion.
    AppId_t app_id = k_uAppIdInvalid;
    // Local deadline in monotonic milliseconds.
    uint64_t deadline = 0;
    // Download reservation pool kept alive through the matching native event.
    std::shared_ptr<WorkshopReservations> download_reservation;
    // Update reservation kept through its exact native completion.
    std::shared_ptr<WorkshopReservations> update_reservation;
    // Manual callback observer for the exact AppID and item download.
    uint64_t download_subscription = 0;
    // Whether a publication has an exact existing or successfully created item ID.
    bool publication_id_known = false;
    // Whether Steam says the author must accept the Workshop legal agreement.
    bool needs_legal_agreement = false;

    // Release local download ownership on submission failure or runtime shutdown.
    ~SteamWorkshopTask() {
        retire_download();
        retire_update();
    }

    // Release the exact callback subscription and reservation before notifying scripts.
    void retire_download() {
        if (const uint64_t subscription = std::exchange(download_subscription, 0)) {
            if (const auto active = runtime.lock()) {
                active->unsubscribe(subscription);
            }
        }
        if (const auto reserved = std::exchange(download_reservation, {})) {
            reserved->downloads.erase(id);
        }
    }

    // Release the publication's item reservation before resuming scripts.
    void retire_update() {
        if (const auto reserved = std::exchange(update_reservation, {})) {
            reserved->updates.erase(id);
        }
    }

    // Settle once and release the signal owner from any cleanup-only watcher.
    void finish(const Ref<SteamResult> &result) {
        const Ref<SteamWorkshopAction> observer = std::exchange(action, {});
        if (observer.is_valid()) {
            observer->finish(result);
        }
    }

    // Publish the correct typed error and preserve a known publication ID for recovery.
    void fail(Error error, const String &message = {}) {
        if (action.is_null()) {
            return;
        }
        switch (kind) {
            case Kind::SUBSCRIPTION: finish(SteamWorkshopSubscriptionResult::create(error, message)); break;
            case Kind::INSTALL: finish(SteamWorkshopInstallResult::create(error, message)); break;
            case Kind::VOTE: finish(SteamWorkshopVoteResult::create(error, message)); break;
            case Kind::LEGAL_AGREEMENT: finish(SteamWorkshopLegalAgreementStatusResult::create(error)); break;
            case Kind::PUBLICATION: {
                const auto publication = publication_id_known || needs_legal_agreement
                        ? SteamWorkshopPublication::create(publication_id_known ? decimal_id(id) : String(),
                                needs_legal_agreement)
                        : Ref<SteamWorkshopPublication>();
                finish(SteamWorkshopPublicationResult::create(error, message, publication));
                break;
            }
        }
    }

    // Settle an expired observer before a late callback can deliver or advance work.
    bool observe_before_deadline() {
        if (action.is_valid() && Time::get_singleton()->get_ticks_msec() >= deadline) {
            fail(ERR_TIMEOUT);
        }
        return action.is_valid();
    }
};

// Own this client's pending query observations and gate new SDK calls.
struct SteamWorkshopScope : public std::enable_shared_from_this<SteamWorkshopScope> {
    // Shared liveness token invalidated before SteamClient emits stop signals.
    std::shared_ptr<SteamClientScope> client_scope;
    // Runtime lease is owned by the Steam client, not by query drafts.
    std::weak_ptr<SteamRuntime> runtime;
    // Pending records stay alive in the exact native call registry.
    std::vector<std::weak_ptr<SteamWorkshopQueryCall>> pending;
    // Typed actions retained by their exact call or callback and observed by this scope.
    std::vector<std::weak_ptr<SteamWorkshopTask>> tasks;
    // IDs already verified to belong to this consumer app, oldest first.
    std::vector<PublishedFileId_t> verified_items;
    // Runtime-wide reservations for callback-only downloads and item updates.
    std::shared_ptr<WorkshopReservations> reservations;
    // Native subscription for same-app installation events.
    uint64_t install_subscription = 0;
    // A stopped scope cannot submit work even when a reentrant callback retains it.
    bool closed = false;

    // Return Workshop only while this owner can submit new work.
    WorkshopAccess access() const {
        return SteamRuntime::on_main_thread() && client_scope && client_scope->active && !closed
                ? access_workshop(runtime) : WorkshopAccess();
    }

    // Report whether this published ID has been validated for the consumer app.
    bool verified(PublishedFileId_t id) const {
        return std::find(verified_items.begin(), verified_items.end(), id) != verified_items.end();
    }

    // Retain a bounded set of same-app IDs for synchronous install snapshots.
    void remember(PublishedFileId_t id) {
        if (closed || id == k_PublishedFileIdInvalid || verified(id)) {
            return;
        }
        if (verified_items.size() >= maximum_verified_items) {
            verified_items.erase(verified_items.begin());
        }
        verified_items.push_back(id);
    }

    // Forget drained records and report whether another query fits the per-scope cap.
    bool can_submit(const WorkshopAccess &workshop) {
        pending.erase(std::remove_if(pending.begin(), pending.end(),
                              [](const auto &entry) {
                                  const auto record = entry.lock();
                                  return !record || record->handle == k_UGCQueryHandleInvalid;
                              }), pending.end());
        return pending.size() < maximum_queries && workshop.runtime->can_track_call();
    }

    // Submit a constructed query and establish its exact native watcher.
    Error send(const WorkshopAccess &workshop, const std::shared_ptr<SteamWorkshopQueryCall> &record) {
        const auto submit = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SendQueryUGCRequest)>(
                "SteamAPI_ISteamUGC_SendQueryUGCRequest");
        const auto long_description = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetReturnLongDescription)>(
                "SteamAPI_ISteamUGC_SetReturnLongDescription");
        if (!submit || !long_description) {
            return ERR_UNAVAILABLE;
        }
        if (!long_description(workshop.ugc, record->handle, true)) {
            return ERR_INVALID_PARAMETER;
        }
        record->remember = [weak = weak_from_this()](PublishedFileId_t id) {
            if (const auto owner = weak.lock()) {
                owner->remember(id);
            }
        };
        const auto call = submit(workshop.ugc, record->handle);
        if (!workshop.runtime->watch_call(call, SteamUGCQueryCompleted_t::k_iCallback, sizeof(SteamUGCQueryCompleted_t),
                [record](const void *payload) { record->complete(payload); })) {
            return ERR_CANT_CONNECT;
        }
        record->deadline = Time::get_singleton()->get_ticks_msec() + query_timeout_ms;
        pending.emplace_back(record);
        return OK;
    }

    // Build and submit one bounded cursor query for ready-to-use items in this app.
    Error fetch_page(const SteamWorkshopQuery &query, const Ref<SteamWorkshopAction> &action) {
        const auto workshop = access();
        if (!workshop.ugc) {
            return ERR_UNAVAILABLE;
        }
        const auto sort = query.has_sort ? query.sorting :
                (query.text.is_empty() ? SteamWorkshopQuery::RATING : SteamWorkshopQuery::RELEVANCE);
        if (!query.valid || (sort == SteamWorkshopQuery::RELEVANCE && query.text.is_empty())) {
            return ERR_INVALID_PARAMETER;
        }
        if (!can_submit(workshop)) {
            return ERR_BUSY;
        }
        const auto create = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_CreateQueryAllUGCRequestCursor)>(
                "SteamAPI_ISteamUGC_CreateQueryAllUGCRequestCursor");
        const auto release = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_ReleaseQueryUGCRequest)>(
                "SteamAPI_ISteamUGC_ReleaseQueryUGCRequest");
        const auto set_text = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetSearchText)>(
                "SteamAPI_ISteamUGC_SetSearchText");
        const auto add_tag = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_AddRequiredTag)>(
                "SteamAPI_ISteamUGC_AddRequiredTag");
        if (!create || !release || (!query.text.is_empty() && !set_text) || (!query.tags.empty() && !add_tag)) {
            return ERR_UNAVAILABLE;
        }
        auto record = std::make_shared<SteamWorkshopQueryCall>();
        record->runtime = runtime;
        record->app_id = workshop.runtime->get_app_id();
        record->action = action;
        record->cursor = query.cursor;
        const CharString cursor = query.cursor.utf8();
        record->handle = create(workshop.ugc, native_sort(sort), k_EUGCMatchingUGCType_Items_ReadyToUse,
                k_uAppIdInvalid, record->app_id, query.cursor.is_empty() ? nullptr : cursor.get_data());
        if (record->handle == k_UGCQueryHandleInvalid) {
            return ERR_UNAVAILABLE;
        }
        if (!query.text.is_empty() && !set_text(workshop.ugc, record->handle, query.text.utf8().get_data())) {
            return ERR_INVALID_PARAMETER;
        }
        for (const auto &tag : query.tags) {
            if (!add_tag(workshop.ugc, record->handle, tag.utf8().get_data())) {
                return ERR_INVALID_PARAMETER;
            }
        }
        return send(workshop, record);
    }

    // Build and submit an exact published-file lookup for this client app.
    Error fetch_item(const String &id_text, const Ref<SteamWorkshopAction> &action) {
        const auto workshop = access();
        if (!workshop.ugc) {
            return ERR_UNAVAILABLE;
        }
        PublishedFileId_t id = k_PublishedFileIdInvalid;
        if (!parse_published_id(id_text, id)) {
            return ERR_INVALID_PARAMETER;
        }
        if (!can_submit(workshop)) {
            return ERR_BUSY;
        }
        const auto create = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest)>(
                "SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest");
        const auto release = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_ReleaseQueryUGCRequest)>(
                "SteamAPI_ISteamUGC_ReleaseQueryUGCRequest");
        if (!create || !release) {
            return ERR_UNAVAILABLE;
        }
        auto record = std::make_shared<SteamWorkshopQueryCall>();
        record->runtime = runtime;
        record->app_id = workshop.runtime->get_app_id();
        record->published_id = id;
        record->action = action;
        record->handle = create(workshop.ugc, &id, 1);
        return record->handle != k_UGCQueryHandleInvalid ? send(workshop, record) : ERR_UNAVAILABLE;
    }

    // Check capacity before retaining a compound action or starting native work.
    bool track_task(const std::shared_ptr<SteamWorkshopTask> &task) {
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                            [](const auto &entry) { return entry.expired(); }), tasks.end());
        if (tasks.size() >= maximum_tasks) {
            task->fail(ERR_BUSY);
            return false;
        }
        task->deadline = Time::get_singleton()->get_ticks_msec() + query_timeout_ms;
        tasks.emplace_back(task);
        return true;
    }

    // Verify the consumer AppID before any action using an ID supplied by the caller.
    void verify_item(const std::shared_ptr<SteamWorkshopTask> &task,
            std::function<void(SteamWorkshopScope &)> next) {
        if (verified(task->id)) {
            next(*this);
            return;
        }
        Ref<SteamWorkshopAction> lookup;
        lookup.instantiate();
        lookup->when_finished([weak = weak_from_this(), task, next = std::move(next)](const Ref<SteamResult> &result) {
            const auto owner = weak.lock();
            if (!task->observe_before_deadline()) {
                return;
            }
            if (result->is_ok() && owner && owner->access().ugc && owner->verified(task->id)) {
                next(*owner);
            } else {
                task->fail(result->is_ok() ? ERR_SKIP : result->get_error(), result->get_message());
            }
        });
        const Error submitted = fetch_item(decimal_id(task->id), lookup);
        if (submitted != OK) {
            task->fail(submitted);
        }
    }

    // Watch the exact subscription acknowledgement for the verified item.
    template <typename NativeResult>
    void watch_subscription(const WorkshopAccess &workshop, SteamAPICall_t call,
            const std::shared_ptr<SteamWorkshopTask> &task, bool subscribed) {
        if (!workshop.runtime->watch_call(call, NativeResult::k_iCallback, sizeof(NativeResult),
                [task, subscribed](const void *payload) {
                    if (!task->observe_before_deadline()) {
                        return;
                    }
                    NativeResult result{};
                    if (payload) {
                        std::memcpy(&result, payload, sizeof(result));
                    }
                    if (!payload || result.m_nPublishedFileId != task->id) {
                        task->fail(ERR_CANT_CONNECT);
                    } else if (result.m_eResult != k_EResultOK) {
                        task->fail(query_error(result.m_eResult));
                    } else if (task->action.is_valid()) {
                        task->finish(SteamWorkshopSubscriptionResult::create(OK, {},
                                SteamWorkshopSubscription::create(decimal_id(task->id), subscribed)));
                    }
                })) {
            task->fail(ERR_CANT_CONNECT);
        }
    }

    // Submit one subscription change for an item validated in this app.
    void change_subscription(const std::shared_ptr<SteamWorkshopTask> &task, bool subscribed) {
        const auto workshop = access();
        if (!workshop.ugc) {
            task->fail(ERR_UNAVAILABLE);
            return;
        }
        const auto change = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SubscribeItem)>(subscribed
                ? "SteamAPI_ISteamUGC_SubscribeItem" : "SteamAPI_ISteamUGC_UnsubscribeItem");
        if (!change || !workshop.runtime->can_track_call()) {
            task->fail(change ? ERR_BUSY : ERR_UNAVAILABLE);
            return;
        }
        const auto call = change(workshop.ugc, task->id);
        if (subscribed) {
            watch_subscription<RemoteStorageSubscribePublishedFileResult_t>(workshop, call, task, true);
        } else {
            watch_subscription<RemoteStorageUnsubscribePublishedFileResult_t>(workshop, call, task, false);
        }
    }

    // Submit one explicit vote and validate both the acknowledged item and direction.
    void vote(const std::shared_ptr<SteamWorkshopTask> &task, bool up) {
        const auto workshop = access();
        if (!workshop.ugc) {
            task->fail(ERR_UNAVAILABLE);
            return;
        }
        const auto submit = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_SetUserItemVote)>(
                "SteamAPI_ISteamUGC_SetUserItemVote");
        if (!submit || !workshop.runtime->can_track_call()) {
            task->fail(submit ? ERR_BUSY : ERR_UNAVAILABLE);
            return;
        }
        const auto call = submit(workshop.ugc, task->id, up);
        if (!workshop.runtime->watch_call(call, SetUserItemVoteResult_t::k_iCallback, sizeof(SetUserItemVoteResult_t),
                [task, up](const void *payload) {
                    if (!task->observe_before_deadline()) {
                        return;
                    }
                    SetUserItemVoteResult_t result{};
                    if (payload) {
                        std::memcpy(&result, payload, sizeof(result));
                    }
                    if (!payload || result.m_nPublishedFileId != task->id) {
                        task->fail(ERR_CANT_CONNECT);
                    } else if (result.m_eResult != k_EResultOK) {
                        task->fail(query_error(result.m_eResult));
                    } else if (result.m_bVoteUp != up) {
                        task->fail(ERR_INVALID_DATA);
                    } else if (task->action.is_valid()) {
                        task->finish(SteamWorkshopVoteResult::create(OK, {},
                                SteamWorkshopVote::create(decimal_id(task->id), up)));
                    }
                })) {
            task->fail(ERR_CANT_CONNECT);
        }
    }

    // Copy an up-to-date Workshop directory only for an explicitly inspected same-app item.
    Ref<SteamWorkshopInstall> read_install(const WorkshopAccess &workshop, PublishedFileId_t id) const {
        if (!workshop.ugc || !verified(id) || (reservations && reservations->downloads.count(id) != 0)) {
            return {};
        }
        const auto get_state = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_GetItemState)>(
                "SteamAPI_ISteamUGC_GetItemState");
        const auto get_info = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_GetItemInstallInfo)>(
                "SteamAPI_ISteamUGC_GetItemInstallInfo");
        if (!get_state || !get_info) {
            return {};
        }
        const uint32_t state = get_state(workshop.ugc, id);
        constexpr uint32_t unavailable = k_EItemStateLegacyItem | k_EItemStateNeedsUpdate |
                k_EItemStateDownloading | k_EItemStateDownloadPending;
        if ((state & k_EItemStateInstalled) == 0 || (state & unavailable) != 0) {
            return {};
        }
        uint64 size = 0;
        uint32 timestamp = 0;
        char directory[maximum_install_path]{};
        String path;
        if (!get_info(workshop.ugc, id, &size, directory, sizeof(directory), &timestamp) ||
                size > static_cast<uint64>((std::numeric_limits<int64_t>::max)()) || !copy_native_text(directory, path)) {
            return {};
        }
        const auto native = native_path(path);
        std::error_code error;
        if (!native.is_absolute() || !std::filesystem::is_directory(native, error)) {
            return {};
        }
        return SteamWorkshopInstall::create(decimal_id(id), path, static_cast<int64_t>(size));
    }

    // Return a current installed directory for a previously validated item.
    Ref<SteamWorkshopInstall> installed(const String &id_text) const {
        const auto workshop = access();
        PublishedFileId_t id = k_PublishedFileIdInvalid;
        return workshop.ugc && parse_published_id(id_text, id) ? read_install(workshop, id) : Ref<SteamWorkshopInstall>();
    }

    // Download a verified item and wait for the matching app and published ID.
    void install(const std::shared_ptr<SteamWorkshopTask> &task, bool high_priority) {
        const auto workshop = access();
        if (!workshop.ugc) {
            task->fail(ERR_UNAVAILABLE);
            return;
        }
        if (const auto current = read_install(workshop, task->id); current.is_valid()) {
            task->finish(SteamWorkshopInstallResult::create(OK, {}, current));
            return;
        }
        if (!reservations || reservations->downloads.size() >= maximum_downloads || reservations->downloads.count(task->id) != 0) {
            task->fail(ERR_BUSY);
            return;
        }
        const auto download = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_DownloadItem)>(
                "SteamAPI_ISteamUGC_DownloadItem");
        if (!download) {
            task->fail(ERR_UNAVAILABLE);
            return;
        }
        reservations->downloads.insert(task->id);
        task->download_reservation = reservations;
        task->app_id = workshop.runtime->get_app_id();
        task->deadline = Time::get_singleton()->get_ticks_msec() + download_timeout_ms;
        task->download_subscription = workshop.runtime->subscribe(DownloadItemResult_t::k_iCallback,
                [weak = weak_from_this(), task](const void *payload, size_t size) {
                    DownloadItemResult_t result{};
                    if (!payload || size != sizeof(result)) {
                        return;
                    }
                    std::memcpy(&result, payload, sizeof(result));
                    if (result.m_unAppID != task->app_id || result.m_nPublishedFileId != task->id) {
                        return;
                    }
                    task->retire_download();
                    if (!task->observe_before_deadline()) {
                        return;
                    }
                    if (result.m_eResult != k_EResultOK) {
                        task->fail(query_error(result.m_eResult));
                        return;
                    }
                    const auto owner = weak.lock();
                    if (!owner || !owner->access().ugc) {
                        task->fail(ERR_SKIP);
                        return;
                    }
                    owner->remember(task->id);
                    const auto installed = owner->read_install(owner->access(), task->id);
                    task->finish(SteamWorkshopInstallResult::create(installed.is_valid() ? OK : ERR_FILE_CANT_READ,
                            {}, installed));
                });
        if (task->download_subscription == 0 || !download(workshop.ugc, task->id, high_priority)) {
            task->retire_download();
            task->fail(ERR_CANT_CONNECT);
        }
    }

    // Start a typed item action with preflight ownership checks and one local observer.
    Variant start_item_task(const String &id_text, SteamWorkshopTask::Kind kind, bool option) {
        Ref<SteamWorkshopAction> observer;
        observer.instantiate();
        auto task = std::make_shared<SteamWorkshopTask>();
        task->action = observer;
        task->kind = kind;
        task->runtime = runtime;
        if (!access().ugc) {
            task->fail(ERR_UNAVAILABLE);
        } else if (!parse_published_id(id_text, task->id)) {
            task->fail(ERR_INVALID_PARAMETER);
        } else if (track_task(task)) {
            verify_item(task, [task, option](SteamWorkshopScope &owner) {
                switch (task->kind) {
                    case SteamWorkshopTask::Kind::SUBSCRIPTION: owner.change_subscription(task, option); break;
                    case SteamWorkshopTask::Kind::INSTALL: owner.install(task, option); break;
                    case SteamWorkshopTask::Kind::VOTE: owner.vote(task, option); break;
                    case SteamWorkshopTask::Kind::PUBLICATION:
                    case SteamWorkshopTask::Kind::LEGAL_AGREEMENT: task->fail(ERR_INVALID_PARAMETER); break;
                }
            });
        }
        return observer->wait();
    }

    // Copy a bounded set of the current app's subscription IDs in Steam's order.
    Ref<SteamWorkshopSubscriptionsResult> subscriptions(int64_t limit, bool include_locally_disabled) {
        const auto workshop = access();
        if (!workshop.ugc) {
            return SteamWorkshopSubscriptionsResult::create(ERR_UNAVAILABLE);
        }
        if (limit < 1 || limit > SteamWorkshop::MAX_SUBSCRIPTIONS) {
            return SteamWorkshopSubscriptionsResult::create(ERR_INVALID_PARAMETER);
        }
        const auto count = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_GetNumSubscribedItems)>(
                "SteamAPI_ISteamUGC_GetNumSubscribedItems");
        const auto get = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_GetSubscribedItems)>(
                "SteamAPI_ISteamUGC_GetSubscribedItems");
        if (!count || !get) {
            return SteamWorkshopSubscriptionsResult::create(ERR_UNAVAILABLE);
        }
        const uint32_t before = count(workshop.ugc, include_locally_disabled);
        const uint32_t capacity = (std::min)(before, static_cast<uint32_t>(limit));
        std::vector<PublishedFileId_t> ids(capacity);
        const uint32_t copied = capacity > 0 ? get(workshop.ugc, ids.data(), capacity, include_locally_disabled) : 0;
        const uint32_t after = count(workshop.ugc, include_locally_disabled);
        if (copied > capacity) {
            return SteamWorkshopSubscriptionsResult::create(ERR_INVALID_DATA);
        }
        PackedStringArray published_ids;
        std::set<PublishedFileId_t> seen;
        for (uint32_t index = 0; index < copied; ++index) {
            const auto id = ids[index];
            if (id == k_PublishedFileIdInvalid || !seen.insert(id).second) {
                return SteamWorkshopSubscriptionsResult::create(ERR_INVALID_DATA);
            }
            published_ids.append(decimal_id(id));
        }
        for (uint32_t index = 0; index < copied; ++index) {
            remember(ids[index]);
        }
        const uint32_t total = (std::max)((std::max)(before, after), copied);
        return SteamWorkshopSubscriptionsResult::create(OK,
                SteamWorkshopSubscriptions::create(published_ids, total, total > copied));
    }

    // Observe the legal status for the exact current AppID and settle on scope stop or timeout.
    Variant legal_agreement_status() {
        const auto workshop = access();
        if (!workshop.ugc) {
            return SteamWorkshopLegalAgreementStatusResult::create(ERR_UNAVAILABLE);
        }
        const auto get = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_GetWorkshopEULAStatus)>(
                "SteamAPI_ISteamUGC_GetWorkshopEULAStatus");
        if (!get || !workshop.runtime->can_track_call()) {
            return SteamWorkshopLegalAgreementStatusResult::create(get ? ERR_BUSY : ERR_UNAVAILABLE);
        }
        Ref<SteamWorkshopAction> observer;
        observer.instantiate();
        auto task = std::make_shared<SteamWorkshopTask>();
        task->action = observer;
        task->kind = SteamWorkshopTask::Kind::LEGAL_AGREEMENT;
        task->app_id = workshop.runtime->get_app_id();
        if (!track_task(task)) {
            return observer->wait();
        }
        const auto call = get(workshop.ugc);
        if (!workshop.runtime->watch_call(call, WorkshopEULAStatus_t::k_iCallback, sizeof(WorkshopEULAStatus_t),
                [task](const void *payload) {
                    if (!task->observe_before_deadline()) {
                        return;
                    }
                    WorkshopEULAStatus_t result{};
                    if (!payload) {
                        task->fail(ERR_CANT_CONNECT);
                        return;
                    }
                    std::memcpy(&result, payload, sizeof(result));
                    if (result.m_eResult != k_EResultOK) {
                        task->fail(query_error(result.m_eResult));
                    } else if (result.m_nAppID != task->app_id) {
                        task->fail(ERR_INVALID_DATA);
                    } else {
                        task->finish(SteamWorkshopLegalAgreementStatusResult::create(OK,
                                SteamWorkshopLegalAgreementStatus::create(result.m_nAppID, result.m_unVersion,
                                        result.m_rtAction, result.m_bAccepted, result.m_bNeedsAction)));
                    }
                })) {
            task->fail(ERR_CANT_CONNECT);
        }
        return observer->wait();
    }

    // Leave the user's legal action entirely in Steam's explicitly requested overlay.
    Error show_legal_agreement() const {
        const auto workshop = access();
        if (!workshop.ugc) {
            return ERR_UNAVAILABLE;
        }
        const auto show = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_ShowWorkshopEULA)>(
                "SteamAPI_ISteamUGC_ShowWorkshopEULA");
        return show && show(workshop.ugc) ? OK : ERR_UNAVAILABLE;
    }

    // Validate a local upload path and resolve Godot's resource or user directory aliases.
    Error resolve_upload_path(String &path, bool content_folder) const {
        if (!bounded_text(path, maximum_install_path - 1, false)) {
            return ERR_INVALID_PARAMETER;
        }
        if (path.begins_with("res://") || path.begins_with("user://")) {
            path = ProjectSettings::get_singleton()->globalize_path(path);
        }
        if (!bounded_text(path, maximum_install_path - 1, false)) {
            return ERR_INVALID_PARAMETER;
        }
        const auto local = native_path(path);
        if (!local.is_absolute()) {
            return ERR_INVALID_PARAMETER;
        }
        std::error_code error;
        if (content_folder) {
            return std::filesystem::is_directory(local, error) ? OK : ERR_FILE_NOT_FOUND;
        }
        if (!std::filesystem::is_regular_file(local, error)) {
            return ERR_FILE_NOT_FOUND;
        }
        const auto size = std::filesystem::file_size(local, error);
        return !error && size >= 16 && size < maximum_preview_bytes ? OK : ERR_INVALID_PARAMETER;
    }

    // Check every supplied field before the SDK can create or modify an item.
    Error validate_publication(SteamWorkshopPublishRequest &draft, const String &notes) const {
        if (!draft.valid || !bounded_text(notes, k_cchPublishedDocumentChangeDescriptionMax - 1, true) ||
                (!draft.editing && (!draft.title || !draft.directory)) ||
                (draft.title && !bounded_text(*draft.title, k_cchPublishedDocumentTitleMax - 1, false)) ||
                (draft.description && !bounded_text(*draft.description, k_cchPublishedDocumentDescriptionMax - 1, true))) {
            return ERR_INVALID_PARAMETER;
        }
        if (draft.audience && (*draft.audience < SteamWorkshopPublishRequest::PRIVATE ||
                *draft.audience > SteamWorkshopPublishRequest::UNLISTED)) {
            return ERR_INVALID_PARAMETER;
        }
        size_t tag_bytes = 0;
        for (const auto &tag : draft.tags) {
            if (!bounded_text(tag, maximum_filter_bytes, false)) {
                return ERR_INVALID_PARAMETER;
            }
            const CharString encoded = tag.utf8();
            const char *first = encoded.get_data();
            const char *last = first + encoded.length();
            if (std::any_of(first, last, [](unsigned char value) {
                        return value < 32 || value == 127 || value == ',';
                    })) {
                return ERR_INVALID_PARAMETER;
            }
            tag_bytes += static_cast<size_t>(encoded.length()) + 1;
        }
        if (tag_bytes >= k_cchTagListMax) {
            return ERR_INVALID_PARAMETER;
        }
        if (draft.directory) {
            const Error content = resolve_upload_path(*draft.directory, true);
            if (content != OK) {
                return content;
            }
        }
        return draft.preview ? resolve_upload_path(*draft.preview, false) : OK;
    }

    // Apply only supplied fields to the SDK update handle.
    bool prepare_update(const WorkshopAccess &workshop, const WorkshopUpdateApi &api, UGCUpdateHandle_t update,
            const SteamWorkshopPublishRequest &draft) const {
        if ((draft.title && !api.title(workshop.ugc, update, draft.title->utf8().get_data())) ||
                (draft.description && !api.description(workshop.ugc, update, draft.description->utf8().get_data())) ||
                (draft.directory && !api.content(workshop.ugc, update, draft.directory->utf8().get_data())) ||
                (draft.preview && !api.preview(workshop.ugc, update, draft.preview->utf8().get_data()))) {
            return false;
        }
        if (draft.audience || !draft.editing) {
            const auto visibility = draft.audience.value_or(SteamWorkshopPublishRequest::PRIVATE);
            if (!api.visibility(workshop.ugc, update, native_visibility(visibility))) {
                return false;
            }
        }
        if (draft.tags.empty()) {
            return true;
        }
        std::vector<CharString> encoded_tags;
        encoded_tags.reserve(draft.tags.size());
        for (const auto &tag : draft.tags) {
            encoded_tags.push_back(tag.utf8());
        }
        std::vector<const char *> tags;
        tags.reserve(encoded_tags.size());
        for (const auto &tag : encoded_tags) {
            tags.push_back(tag.get_data());
        }
        const SteamParamStringArray_t arguments{tags.data(), static_cast<int32>(tags.size())};
        return api.tags(workshop.ugc, update, &arguments, false);
    }

    // Submit the verified or newly created item and preserve its ID on later failure.
    void submit_update(const std::shared_ptr<SteamWorkshopTask> &task,
            const Ref<SteamWorkshopPublishRequest> &draft, const String &notes) {
        const auto workshop = access();
        if (!workshop.ugc) {
            task->fail(ERR_SKIP);
            return;
        }
        const auto api = update_api(workshop);
        if (!api.available()) {
            task->fail(ERR_UNAVAILABLE);
            return;
        }
        if (!workshop.runtime->can_track_call() || reservations->updates.count(task->id) != 0) {
            task->fail(ERR_BUSY);
            return;
        }
        reservations->updates.insert(task->id);
        task->update_reservation = reservations;
        const auto update = api.start(workshop.ugc, workshop.runtime->get_app_id(), task->id);
        if (update == k_UGCUpdateHandleInvalid || !prepare_update(workshop, api, update, *draft.ptr())) {
            task->retire_update();
            task->fail(ERR_INVALID_PARAMETER, "Steam could not prepare the Workshop update.");
            return;
        }
        const CharString encoded = notes.utf8();
        const auto call = api.submit(workshop.ugc, update, notes.is_empty() ? nullptr : encoded.get_data());
        if (!workshop.runtime->watch_call(call, SubmitItemUpdateResult_t::k_iCallback, sizeof(SubmitItemUpdateResult_t),
                [task](const void *payload) {
                    SubmitItemUpdateResult_t result{};
                    if (payload) {
                        std::memcpy(&result, payload, sizeof(result));
                    }
                    task->retire_update();
                    if (payload && result.m_nPublishedFileId == task->id) {
                        task->needs_legal_agreement = result.m_bUserNeedsToAcceptWorkshopLegalAgreement;
                    }
                    if (!task->observe_before_deadline()) {
                        return;
                    }
                    if (!payload || result.m_nPublishedFileId != task->id) {
                        task->fail(ERR_CANT_CONNECT, "Steam did not acknowledge the expected Workshop item.");
                        return;
                    }
                    if (task->needs_legal_agreement) {
                        task->fail(ERR_UNAUTHORIZED, "The author must accept the Steam Workshop legal agreement.");
                    } else if (result.m_eResult != k_EResultOK) {
                        task->fail(query_error(result.m_eResult), "Steam rejected the Workshop update (EResult " +
                                String::num_int64(result.m_eResult) + ").");
                    } else if (task->action.is_valid()) {
                        task->finish(SteamWorkshopPublicationResult::create(OK, {},
                                SteamWorkshopPublication::create(decimal_id(task->id), false)));
                    }
                })) {
            task->retire_update();
            task->fail(ERR_CANT_CONNECT, "Steam did not provide an observable Workshop update handle.");
        }
    }

    // Create a new Workshop item once, then upload only after exact native acknowledgement.
    void create_publication(const WorkshopAccess &workshop, const std::shared_ptr<SteamWorkshopTask> &task,
            const Ref<SteamWorkshopPublishRequest> &draft, const String &notes) {
        const auto create = workshop.runtime->symbol<decltype(&SteamAPI_ISteamUGC_CreateItem)>(
                "SteamAPI_ISteamUGC_CreateItem");
        if (!create || !update_api(workshop).available()) {
            task->fail(ERR_UNAVAILABLE);
            return;
        }
        if (!workshop.runtime->can_track_call()) {
            task->fail(ERR_BUSY);
            return;
        }
        const auto call = create(workshop.ugc, workshop.runtime->get_app_id(), k_EWorkshopFileTypeCommunity);
        if (!workshop.runtime->watch_call(call, CreateItemResult_t::k_iCallback, sizeof(CreateItemResult_t),
                [weak = weak_from_this(), task, draft, notes](const void *payload) {
                    CreateItemResult_t result{};
                    if (!payload) {
                        if (task->observe_before_deadline()) {
                            task->fail(ERR_CANT_CONNECT, "Steam did not confirm whether the Workshop item was created.");
                        }
                        return;
                    }
                    std::memcpy(&result, payload, sizeof(result));
                    task->needs_legal_agreement = result.m_bUserNeedsToAcceptWorkshopLegalAgreement;
                    if (result.m_nPublishedFileId != k_PublishedFileIdInvalid &&
                            (result.m_eResult == k_EResultOK || task->needs_legal_agreement)) {
                        task->id = result.m_nPublishedFileId;
                        task->publication_id_known = true;
                    }
                    const auto owner = weak.lock();
                    if (owner && task->publication_id_known) {
                        owner->remember(task->id);
                    }
                    if (!task->observe_before_deadline()) {
                        return;
                    }
                    if (task->needs_legal_agreement) {
                        task->fail(ERR_UNAUTHORIZED, "The author must accept the Steam Workshop legal agreement.");
                    } else if (result.m_eResult != k_EResultOK || !task->publication_id_known) {
                        const Error error = result.m_eResult == k_EResultOK ? ERR_INVALID_DATA : query_error(result.m_eResult);
                        task->fail(error, "Steam did not complete Workshop item creation (EResult " +
                                String::num_int64(result.m_eResult) + ").");
                    } else if (owner) {
                        owner->submit_update(task, draft, notes);
                    } else {
                        task->fail(ERR_SKIP);
                    }
                })) {
            task->fail(ERR_CANT_CONNECT, "Steam did not provide an observable Workshop creation handle.");
        }
    }

    // Start one explicit publish or edit without repeating uncertain native creation.
    Variant publish(const SteamWorkshopPublishRequest &request, const String &notes) {
        Ref<SteamWorkshopAction> observer;
        observer.instantiate();
        auto task = std::make_shared<SteamWorkshopTask>();
        task->kind = SteamWorkshopTask::Kind::PUBLICATION;
        task->action = observer;
        task->runtime = runtime;
        const auto workshop = access();
        if (!workshop.ugc) {
            task->fail(ERR_UNAVAILABLE);
            return observer->wait();
        }
        const auto draft = request.branch();
        const Error validated = validate_publication(*draft.ptr(), notes);
        if (validated != OK || (draft->editing && !parse_published_id(draft->published_id, task->id))) {
            task->fail(validated != OK ? validated : ERR_INVALID_PARAMETER,
                    "Validate the Workshop draft fields and local upload paths before submitting.");
            return observer->wait();
        }
        if (!track_task(task)) {
            return observer->wait();
        }
        task->deadline = Time::get_singleton()->get_ticks_msec() + publication_timeout_ms;
        if (draft->editing) {
            verify_item(task, [task, draft, notes](SteamWorkshopScope &owner) {
                task->publication_id_known = true;
                owner.submit_update(task, draft, notes);
            });
        } else {
            create_publication(workshop, task, draft, notes);
        }
        return observer->wait();
    }

    // Expire observers without dropping native query cleanup.
    void tick() {
        const uint64_t now = Time::get_singleton()->get_ticks_msec();
        std::vector<std::shared_ptr<SteamWorkshopQueryCall>> expired;
        for (const auto &entry : pending) {
            if (auto record = entry.lock(); record && record->action.is_valid() && now >= record->deadline) {
                expired.push_back(std::move(record));
            }
        }
        std::vector<std::shared_ptr<SteamWorkshopTask>> expired_tasks;
        for (const auto &entry : tasks) {
            if (auto task = entry.lock(); task && task->action.is_valid() && now >= task->deadline) {
                expired_tasks.push_back(std::move(task));
            }
        }
        for (const auto &record : expired) {
            record->stop_observing(ERR_TIMEOUT);
        }
        for (const auto &task : expired_tasks) {
            task->fail(ERR_TIMEOUT);
        }
    }

    // Invalidate this owner before settling its outstanding local observers.
    void close() {
        closed = true;
        const auto closing = std::exchange(pending, {});
        const auto closing_tasks = std::exchange(tasks, {});
        if (const auto active = runtime.lock()) {
            if (const uint64_t subscription = std::exchange(install_subscription, 0)) {
                active->unsubscribe(subscription);
            }
        }
        for (const auto &entry : closing_tasks) {
            if (const auto task = entry.lock()) {
                task->fail(ERR_SKIP);
            }
        }
        for (const auto &entry : closing) {
            if (const auto record = entry.lock()) {
                record->stop_observing(ERR_SKIP);
            }
        }
    }
};
#endif

void SteamWorkshopQuery::_bind_methods() {
    ClassDB::bind_method(D_METHOD("search", "text"), &SteamWorkshopQuery::search);
    ClassDB::bind_method(D_METHOD("tagged", "tag"), &SteamWorkshopQuery::tagged);
    ClassDB::bind_method(D_METHOD("sort_by", "sort"), &SteamWorkshopQuery::sort_by);
    ClassDB::bind_method(D_METHOD("after", "cursor"), &SteamWorkshopQuery::after);
    ClassDB::bind_method(D_METHOD("fetch"), &SteamWorkshopQuery::fetch);
    BIND_ENUM_CONSTANT(RELEVANCE);
    BIND_ENUM_CONSTANT(RATING);
    BIND_ENUM_CONSTANT(NEWEST);
}

Ref<SteamWorkshopQuery> SteamWorkshopQuery::branch() const {
    Ref<SteamWorkshopQuery> query;
    query.instantiate();
    query->owner = owner;
    query->text = text;
    query->tags = tags;
    query->cursor = cursor;
    query->sorting = sorting;
    query->has_sort = has_sort;
    query->valid = valid;
    return query;
}

Ref<SteamWorkshopQuery> SteamWorkshopQuery::search(const String &text) const {
    const auto query = branch();
    query->cursor = String();
    query->valid = valid && bounded_text(text, maximum_filter_bytes, true);
    if (query->valid) {
        query->text = text;
    }
    return query;
}

Ref<SteamWorkshopQuery> SteamWorkshopQuery::tagged(const String &tag) const {
    const auto query = branch();
    query->cursor = String();
    query->valid = valid && tags.size() < maximum_tags && bounded_text(tag, maximum_filter_bytes, false);
    if (query->valid && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        query->tags.push_back(tag);
    }
    return query;
}

Ref<SteamWorkshopQuery> SteamWorkshopQuery::sort_by(Sort sort) const {
    const auto query = branch();
    query->cursor = String();
    query->valid = valid && (sort == RELEVANCE || sort == RATING || sort == NEWEST);
    query->sorting = sort;
    query->has_sort = true;
    return query;
}

Ref<SteamWorkshopQuery> SteamWorkshopQuery::after(const String &cursor) const {
    const auto query = branch();
    query->valid = valid && bounded_text(cursor, maximum_filter_bytes, true);
    if (query->valid) {
        query->cursor = cursor;
    }
    return query;
}

Variant SteamWorkshopQuery::fetch() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        Ref<SteamWorkshopAction> action;
        action.instantiate();
        const Error error = scope->fetch_page(*this, action);
        return error == OK ? action->wait() : Variant(SteamWorkshopPageResult::create(error));
    }
#endif
    return SteamWorkshopPageResult::create(ERR_UNAVAILABLE);
}

void SteamWorkshopItemRef::_bind_methods() {
    ClassDB::bind_method(D_METHOD("fetch"), &SteamWorkshopItemRef::fetch);
    ClassDB::bind_method(D_METHOD("subscribe"), &SteamWorkshopItemRef::subscribe);
    ClassDB::bind_method(D_METHOD("unsubscribe"), &SteamWorkshopItemRef::unsubscribe);
    ClassDB::bind_method(D_METHOD("install", "high_priority"), &SteamWorkshopItemRef::install, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("installed"), &SteamWorkshopItemRef::installed);
    ClassDB::bind_method(D_METHOD("vote", "up"), &SteamWorkshopItemRef::vote);
    ClassDB::bind_method(D_METHOD("edit"), &SteamWorkshopItemRef::edit);
}

Variant SteamWorkshopItemRef::fetch() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        Ref<SteamWorkshopAction> action;
        action.instantiate();
        const Error error = scope->fetch_item(published_id, action);
        return error == OK ? action->wait() : Variant(SteamWorkshopItemResult::create(error));
    }
#endif
    return SteamWorkshopItemResult::create(ERR_UNAVAILABLE);
}

Variant SteamWorkshopItemRef::subscribe() const {
    return change_subscription(true);
}

Variant SteamWorkshopItemRef::unsubscribe() const {
    return change_subscription(false);
}

Variant SteamWorkshopItemRef::change_subscription(bool subscribed) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        return scope->start_item_task(published_id, SteamWorkshopTask::Kind::SUBSCRIPTION, subscribed);
    }
#endif
    return SteamWorkshopSubscriptionResult::create(ERR_UNAVAILABLE);
}

Variant SteamWorkshopItemRef::install(bool high_priority) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        return scope->start_item_task(published_id, SteamWorkshopTask::Kind::INSTALL, high_priority);
    }
#endif
    return SteamWorkshopInstallResult::create(ERR_UNAVAILABLE);
}

Ref<SteamWorkshopInstall> SteamWorkshopItemRef::installed() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        return scope->installed(published_id);
    }
#endif
    return {};
}

Variant SteamWorkshopItemRef::vote(bool up) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        return scope->start_item_task(published_id, SteamWorkshopTask::Kind::VOTE, up);
    }
#endif
    return SteamWorkshopVoteResult::create(ERR_UNAVAILABLE);
}

Ref<SteamWorkshopPublishRequest> SteamWorkshopItemRef::edit() const {
    Ref<SteamWorkshopPublishRequest> request;
    request.instantiate();
    request->owner = owner;
    request->editing = true;
    request->published_id = published_id;
    return request;
}

Variant SteamWorkshopPublishRequest::submit(const String &change_notes) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto scope = owner.lock()) {
        return scope->publish(*this, change_notes);
    }
#endif
    return SteamWorkshopPublicationResult::create(ERR_UNAVAILABLE);
}

void SteamWorkshop::_bind_methods() {
    ClassDB::bind_method(D_METHOD("query"), &SteamWorkshop::query);
    ClassDB::bind_method(D_METHOD("item", "published_id"), &SteamWorkshop::item);
    ClassDB::bind_method(D_METHOD("new_item"), &SteamWorkshop::new_item);
    ClassDB::bind_method(D_METHOD("subscriptions", "limit", "include_locally_disabled"),
            &SteamWorkshop::subscriptions, DEFVAL(128), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("legal_agreement_status"), &SteamWorkshop::legal_agreement_status);
    ClassDB::bind_method(D_METHOD("show_legal_agreement"), &SteamWorkshop::show_legal_agreement);
    BIND_CONSTANT(MAX_SUBSCRIPTIONS);
    ADD_SIGNAL(MethodInfo("install_changed", PropertyInfo(Variant::STRING, "published_id"),
            PropertyInfo(Variant::OBJECT, "install", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopInstall")));
}

Ref<SteamWorkshopQuery> SteamWorkshop::query() const {
    Ref<SteamWorkshopQuery> query;
    query.instantiate();
    query->owner = scope;
    return query;
}

Ref<SteamWorkshopItemRef> SteamWorkshop::item(const String &published_id) const {
    Ref<SteamWorkshopItemRef> item;
    item.instantiate();
    item->owner = scope;
    item->published_id = published_id;
    return item;
}

Ref<SteamWorkshopPublishRequest> SteamWorkshop::new_item() const {
    Ref<SteamWorkshopPublishRequest> request;
    request.instantiate();
    request->owner = scope;
    return request;
}

Ref<SteamWorkshopSubscriptionsResult> SteamWorkshop::subscriptions(int64_t limit, bool include_locally_disabled) const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = scope) {
        return active->subscriptions(limit, include_locally_disabled);
    }
#endif
    return SteamWorkshopSubscriptionsResult::create(ERR_UNAVAILABLE);
}

Variant SteamWorkshop::legal_agreement_status() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = scope) {
        return active->legal_agreement_status();
    }
#endif
    return SteamWorkshopLegalAgreementStatusResult::create(ERR_UNAVAILABLE);
}

Error SteamWorkshop::show_legal_agreement() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto active = scope) {
        return active->show_legal_agreement();
    }
#endif
    return ERR_UNAVAILABLE;
}

void SteamWorkshop::configure(const std::shared_ptr<SteamRuntime> &runtime,
        const std::shared_ptr<SteamClientScope> &client_scope) {
#ifdef CRIMSON_WITH_STEAM
    scope = std::make_shared<SteamWorkshopScope>();
    scope->runtime = runtime;
    scope->client_scope = client_scope;
    scope->reservations = active_reservations.lock();
    if (!scope->reservations) {
        scope->reservations = std::make_shared<WorkshopReservations>();
        active_reservations = scope->reservations;
    }
    const ObjectID self(get_instance_id());
    scope->install_subscription = runtime->subscribe(ItemInstalled_t::k_iCallback,
            [self, weak = std::weak_ptr<SteamWorkshopScope>(scope)](const void *payload, size_t size) {
                ItemInstalled_t result{};
                if (!payload || size != sizeof(result)) {
                    return;
                }
                std::memcpy(&result, payload, sizeof(result));
                const auto owner = weak.lock();
                const auto active = owner ? owner->access() : WorkshopAccess();
                if (!active.ugc || result.m_unAppID != active.runtime->get_app_id() ||
                        result.m_nPublishedFileId == k_PublishedFileIdInvalid) {
                    return;
                }
                owner->remember(result.m_nPublishedFileId);
                const Ref<SteamWorkshop> service(Object::cast_to<SteamWorkshop>(ObjectDB::get_instance(self)));
                if (service.is_valid()) {
                    // Leave filesystem inspection and its Steam usage marker to an explicit caller.
                    service->emit_signal("install_changed", decimal_id(result.m_nPublishedFileId),
                            Ref<SteamWorkshopInstall>());
                }
            });
#endif
}

void SteamWorkshop::close() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto closing = std::exchange(scope, {})) {
        closing->close();
    }
#endif
}

void SteamWorkshop::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto current = scope) {
        current->tick();
    }
#endif
}

} // namespace crimson::steam
