// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_cloud.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/object.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#endif

namespace crimson::steam {

using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Cap retained write buffers across all Steam client facades.
constexpr size_t maximum_transfers = 4;
// Bound how long an observer waits before detaching from native work.
constexpr uint64_t observation_timeout_ms = 30000;
// Bound the snapshot of one dynamic-sync notification.
constexpr int32_t maximum_local_changes = 256;
// Bound the UTF-8 path copied for each dynamic-sync change.
constexpr size_t maximum_change_path_bytes = 4096;

// Keep Steam loaded while using one borrowed Remote Storage interface.
struct CloudAccess {
    // Strong runtime lease for the duration of one main-thread action.
    std::shared_ptr<SteamRuntime> runtime;
    // Borrowed Steam Remote Storage interface.
    ISteamRemoteStorage *storage = nullptr;
};

// Return Remote Storage only for an active main-thread scope.
CloudAccess access_storage(const std::weak_ptr<SteamRuntime> &scope) {
    if (!SteamRuntime::on_main_thread()) {
        return {};
    }
    const auto runtime = scope.lock();
    if (!runtime) {
        return {};
    }
    const auto get_storage = runtime->symbol<decltype(&SteamAPI_SteamRemoteStorage_v016)>(
            "SteamAPI_SteamRemoteStorage_v016");
    return {runtime, get_storage ? get_storage() : nullptr};
}

// Require the owning client to remain active for public facade calls.
CloudAccess access_storage(const std::weak_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &scope) {
    if (!SteamRuntime::on_main_thread() || !scope || !scope->active) {
        return {};
    }
    return access_storage(runtime);
}

// Copy a bounded, NUL-terminated SDK path before the next SDK call.
String copy_sdk_path(const char *path, size_t limit) {
    if (!path) {
        return {};
    }
    size_t length = 0;
    while (length < limit && path[length] != '\0') {
        ++length;
    }
    return length < limit ? String::utf8(path, static_cast<int64_t>(length)) : String();
}

// Translate Steam's dynamic-sync change classification into stable Godot values.
SteamCloudChange::ChangeType cloud_change_type(ERemoteStorageLocalFileChange type) {
    switch (type) {
        case k_ERemoteStorageLocalFileChange_FileUpdated: return SteamCloudChange::CHANGE_UPDATED;
        case k_ERemoteStorageLocalFileChange_FileDeleted: return SteamCloudChange::CHANGE_DELETED;
        default: return SteamCloudChange::CHANGE_UNKNOWN;
    }
}

// Translate how Steam represents a dynamic-sync file name or path.
SteamCloudChange::PathType cloud_path_type(ERemoteStorageFilePathType type) {
    switch (type) {
        case k_ERemoteStorageFilePathType_Absolute: return SteamCloudChange::PATH_ABSOLUTE;
        case k_ERemoteStorageFilePathType_APIFilename: return SteamCloudChange::PATH_CLOUD_NAME;
        default: return SteamCloudChange::PATH_UNKNOWN;
    }
}

// Validate a portable logical filename and fold Windows case aliases for reservations.
std::string reservation_key(const CharString &name) {
    if (name.length() <= 0 || name.length() >= k_cchFilenameMax) {
        return {};
    }
    std::string key(name.get_data(), static_cast<size_t>(name.length()));
    for (char &character : key) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        } else if (!((character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') ||
                           character == '.' || character == '_' || character == '-' || character == '/')) {
            return {};
        }
    }
    size_t offset = 0;
    for (;;) {
        const size_t slash = key.find('/', offset);
        const auto segment = std::string_view(key).substr(offset, slash - offset);
        if (segment.empty() || segment.back() == '.') {
            return {};
        }
        const auto stem = segment.substr(0, segment.find('.'));
        const bool numbered_device = stem.size() == 4 && stem.back() >= '1' && stem.back() <= '9' &&
                (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt");
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || numbered_device) {
            return {};
        }
        if (slash == std::string::npos) {
            return key;
        }
        offset = slash + 1;
    }
}

// Validate an unsigned decimal Steam handle without narrowing to a Godot int.
bool parse_ugc_handle(const CharString &text, UGCHandle_t &handle) {
    if (text.length() < 1 || text.length() > 20 || text.get_data()[0] == '0') {
        return false;
    }
    const char *first = text.get_data();
    const char *last = first + text.length();
    const auto parsed = std::from_chars(first, last, handle);
    return parsed.ec == std::errc() && parsed.ptr == last && handle != 0 && handle != k_UGCHandleInvalid;
}

} // namespace

// Process-wide filename reservations for submitted Cloud transfers.
struct SteamCloudReservations {
    // Normalized logical paths held until their Steam API callbacks settle.
    std::set<std::string> names;
    // Exact advisory Steam batch shared across client scopes.
    std::weak_ptr<SteamCloudWriteBatchState> batch;
};

// Track one app-wide hint until the client thread retires it.
struct SteamCloudWriteBatchState {
    // Weak runtime keeps a retained handle from extending Steam client lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Reservation owner shared by active Cloud facades.
    std::weak_ptr<SteamCloudReservations> reservations;
    // True while this exact handle still owns the native hint.
    std::atomic_bool open{true};
    // A dropped handle can request retirement from a non-client thread.
    std::atomic_bool abandoned{false};

    // Retire this exact generation and close the SDK hint once on the client thread.
    Error close() {
        if (!open.load()) {
            return OK;
        }
        if (!SteamRuntime::on_main_thread()) {
            return ERR_BUSY;
        }
        open.store(false);
        const auto owner = reservations.lock();
        if (!owner || owner->batch.lock().get() != this) {
            return ERR_UNAVAILABLE;
        }
        owner->batch.reset();
        const auto access = access_storage(runtime);
        if (!access.storage) {
            return ERR_UNAVAILABLE;
        }
        const auto end = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_EndFileWriteBatch)>(
                "SteamAPI_ISteamRemoteStorage_EndFileWriteBatch");
        return end && end(access.storage) ? OK : ERR_UNAVAILABLE;
    }
};

namespace {

// Reuse the reservation pool for every scope of the active Steam client.
std::weak_ptr<SteamCloudReservations> process_reservations;

} // namespace

// Hold the native submission through completion independently of its Godot observer.
struct SteamCloudPending {
    // Weak runtime reference avoids a cycle with its own callback watcher.
    std::weak_ptr<SteamRuntime> runtime;
    // Shared reservation pool kept alive through the SDK call.
    std::shared_ptr<SteamCloudReservations> reservations;
    // Reserved normalized filename, cleared before notifying callers.
    std::string key;
    // Local operation to settle, or null after cancellation.
    SteamCloud::TransferObserver observer;
    // Backing bytes kept alive through asynchronous write completion.
    PackedByteArray write_data;
    // Steam call handle returned at submission.
    SteamAPICall_t call = k_uAPICallInvalid;
    // Upper bound for data returned by this whole-file read.
    uint32_t read_size = 0;
    // Caller-supplied file name used to validate a share or an optional download match.
    String expected_name;
    // Exact native content handle submitted for download.
    UGCHandle_t download_handle = k_UGCHandleInvalid;
    // Exact active Steam AppID expected in a download result.
    AppId_t download_app_id = k_uAppIdInvalid;
    // Deadline for local observation in Godot's monotonic milliseconds.
    uint64_t deadline = 0;

    // Release the filename if Steam shuts down before reporting completion.
    ~SteamCloudPending() {
        release_reservation();
    }

    // Release the filename before callbacks can submit another transfer for it.
    void release_reservation() {
        if (!key.empty()) {
            reservations->names.erase(key);
            key.clear();
        }
    }

    // End local observation while retaining any submitted SDK work.
    void settle_observer(Error error, const String &message = {}, SteamCloud::TransferValue data = {}) {
        if (const auto settle = std::exchange(observer, {})) {
            if (error != ERR_SKIP && error != ERR_TIMEOUT && Time::get_singleton()->get_ticks_msec() >= deadline) {
                settle(ERR_TIMEOUT, {}, {});
            } else {
                settle(error, message, std::move(data));
            }
        }
    }

    // Cancel only this local observer. The SDK call retains the record.
    Error cancel() {
        if (!observer) {
            return ERR_DOES_NOT_EXIST;
        }
        settle_observer(ERR_SKIP);
        return OK;
    }

    // Handle local write completion and allow another operation on this file.
    void finish_write(const void *payload) {
        RemoteStorageFileWriteAsyncComplete_t result{};
        if (payload) {
            std::memcpy(&result, payload, sizeof(result));
        }
        write_data = PackedByteArray();
        release_reservation();
        if (result.m_eResult == k_EResultOK) {
            settle_observer(OK);
        } else {
            settle_observer(ERR_FILE_CANT_WRITE, "Steam rejected the local write (EResult " +
                    String::num_int64(result.m_eResult) + ").");
        }
    }

    // Consume the SDK read result, even when its local observer was cancelled.
    void finish_read(const void *payload) {
        RemoteStorageFileReadAsyncComplete_t result{};
        if (payload) {
            std::memcpy(&result, payload, sizeof(result));
        }
        PackedByteArray data;
        Error error = ERR_FILE_CANT_READ;
        if (result.m_eResult == k_EResultOK && result.m_hFileReadAsync == call &&
                result.m_nOffset == 0 && result.m_cubRead <= read_size) {
            const auto access = access_storage(runtime);
            if (access.storage) {
                const auto read_complete = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileReadAsyncComplete)>(
                        "SteamAPI_ISteamRemoteStorage_FileReadAsyncComplete");
                uint8_t empty = 0;
                if (read_complete && data.resize(result.m_cubRead) == OK &&
                        read_complete(access.storage, call, data.is_empty() ? &empty : data.ptrw(), result.m_cubRead)) {
                    error = result.m_cubRead == read_size ? OK : ERR_FILE_CANT_READ;
                }
            }
        }
        release_reservation();
        if (error == OK) {
            settle_observer(OK, {}, std::move(data));
        } else {
            settle_observer(error, "Steam could not complete the local read (EResult " +
                    String::num_int64(result.m_eResult) + ").");
        }
    }

    // Validate the exact shared file response before exposing a transferable UGC handle.
    void finish_share(const void *payload) {
        RemoteStorageFileShareResult_t result{};
        if (payload) {
            std::memcpy(&result, payload, sizeof(result));
        }
        release_reservation();
        if (result.m_eResult != k_EResultOK || result.m_hFile == 0 || result.m_hFile == k_UGCHandleInvalid ||
                copy_sdk_path(result.m_rgchFilename, sizeof(result.m_rgchFilename)) != expected_name) {
            settle_observer(ERR_FILE_CANT_WRITE, "Steam could not share the requested Cloud file (EResult " +
                    String::num_int64(result.m_eResult) + ").");
            return;
        }
        settle_observer(OK, {}, String::utf8(std::to_string(result.m_hFile).c_str()));
    }

    // Consume or close every successful cached download, then publish only bounded matching bytes.
    void finish_download(const void *payload) {
        RemoteStorageDownloadUGCResult_t result{};
        if (payload) {
            std::memcpy(&result, payload, sizeof(result));
        }
        Error error = ERR_FILE_CANT_READ;
        PackedByteArray bytes;
        String name;
        if (result.m_eResult == k_EResultOK && result.m_hFile == download_handle) {
            const auto access = access_storage(runtime);
            const auto read = access.storage
                    ? access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_UGCRead)>(
                            "SteamAPI_ISteamRemoteStorage_UGCRead")
                    : nullptr;
            if (read) {
                uint8_t discard = 0;
                bool copy_bytes = false;
                int32_t copy_size = 0;
                if (observer) {
                    name = copy_sdk_path(result.m_pchFileName, sizeof(result.m_pchFileName));
                    const int32_t size = result.m_nSizeInBytes;
                    copy_bytes = result.m_nAppID == download_app_id &&
                            !reservation_key(name.utf8()).empty() && (expected_name.is_empty() || name == expected_name) &&
                            size >= 0 && static_cast<uint32_t>(size) <= read_size && bytes.resize(size) == OK;
                    copy_size = copy_bytes ? size : 0;
                }
                const int32_t copied = read(access.storage, result.m_hFile, copy_size > 0 ? bytes.ptrw() : &discard,
                        copy_size, 0, k_EUGCRead_Close);
                if (copy_bytes && copied == copy_size) {
                    error = OK;
                }
            }
        }
        release_reservation();
        if (error == OK) {
            const String handle = String::utf8(std::to_string(result.m_hFile).c_str());
            const String owner = result.m_ulSteamIDOwner != 0
                    ? String::utf8(std::to_string(result.m_ulSteamIDOwner).c_str())
                    : String();
            settle_observer(OK, {}, SteamSharedCloudFile::create(handle, name, owner, std::move(bytes)));
        } else {
            settle_observer(error, "Steam could not read matching shared Cloud bytes within the requested limit (EResult " +
                    String::num_int64(result.m_eResult) + ").");
        }
    }
};
#endif

void SteamCloudWriteBatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_open"), &SteamCloudWriteBatch::is_open);
    ClassDB::bind_method(D_METHOD("close"), &SteamCloudWriteBatch::close);
}

SteamCloudWriteBatch::~SteamCloudWriteBatch() {
#ifdef CRIMSON_WITH_STEAM
    if (state) {
        state->abandoned.store(true);
        if (SteamRuntime::on_main_thread()) {
            state->close();
        }
    }
#endif
}

bool SteamCloudWriteBatch::is_open() const {
#ifdef CRIMSON_WITH_STEAM
    return state && state->open.load();
#else
    return false;
#endif
}

Error SteamCloudWriteBatch::close() {
#ifdef CRIMSON_WITH_STEAM
    return state ? state->close() : OK;
#else
    return OK;
#endif
}

void SteamCloudWriteBatchResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamCloudWriteBatchResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamCloudWriteBatch",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamCloudWriteBatch> SteamCloudWriteBatchResult::get_value() const {
    return value;
}

Ref<SteamCloudWriteBatchResult> SteamCloudWriteBatchResult::create(Error error,
        const Ref<SteamCloudWriteBatch> &batch) {
    Ref<SteamCloudWriteBatchResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = batch;
    return result;
}

void SteamCloudShareOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamCloudShareOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamCloudShareOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamCloudShareOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamCloudShareOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamStringResult")));
}

bool SteamCloudShareOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamStringResult> SteamCloudShareOperation::result() const {
    return outcome;
}

Variant SteamCloudShareOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamCloudShareOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
#endif
    const auto cancel = cancel_observation;
    return cancel ? cancel() : ERR_DOES_NOT_EXIST;
}

void SteamCloudShareOperation::finish(const Ref<SteamStringResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamCloudShareOperation> keep_alive(this);
    outcome = result;
    cancel_observation = {};
    emit_signal("completed", outcome);
}

void SteamSharedCloudFile::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_handle"), &SteamSharedCloudFile::get_handle);
    ClassDB::bind_method(D_METHOD("get_name"), &SteamSharedCloudFile::get_name);
    ClassDB::bind_method(D_METHOD("get_owner_steam_id"), &SteamSharedCloudFile::get_owner_steam_id);
    ClassDB::bind_method(D_METHOD("get_bytes"), &SteamSharedCloudFile::get_bytes);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "handle", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_handle");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_name");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "owner_steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_owner_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_bytes");
}

String SteamSharedCloudFile::get_handle() const {
    return handle;
}

String SteamSharedCloudFile::get_name() const {
    return name;
}

String SteamSharedCloudFile::get_owner_steam_id() const {
    return owner_steam_id;
}

PackedByteArray SteamSharedCloudFile::get_bytes() const {
    return bytes;
}

Ref<SteamSharedCloudFile> SteamSharedCloudFile::create(const String &handle, const String &name,
        const String &owner_steam_id, PackedByteArray bytes) {
    Ref<SteamSharedCloudFile> file;
    file.instantiate();
    file->handle = handle;
    file->name = name;
    file->owner_steam_id = owner_steam_id;
    file->bytes = std::move(bytes);
    return file;
}

void SteamSharedCloudFileResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamSharedCloudFileResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamSharedCloudFile",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamSharedCloudFile> SteamSharedCloudFileResult::get_value() const {
    return value;
}

Ref<SteamSharedCloudFileResult> SteamSharedCloudFileResult::create(Error error, const String &message,
        const Ref<SteamSharedCloudFile> &file) {
    Ref<SteamSharedCloudFileResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = file;
    return result;
}

void SteamSharedCloudFileOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamSharedCloudFileOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamSharedCloudFileOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamSharedCloudFileOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamSharedCloudFileOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamSharedCloudFileResult")));
}

bool SteamSharedCloudFileOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamSharedCloudFileResult> SteamSharedCloudFileOperation::result() const {
    return outcome;
}

Variant SteamSharedCloudFileOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamSharedCloudFileOperation::cancel() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
#endif
    const auto cancel = cancel_observation;
    return cancel ? cancel() : ERR_DOES_NOT_EXIST;
}

void SteamSharedCloudFileOperation::finish(const Ref<SteamSharedCloudFileResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamSharedCloudFileOperation> keep_alive(this);
    outcome = result;
    cancel_observation = {};
    emit_signal("completed", outcome);
}

void SteamCloudFile::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_name"), &SteamCloudFile::get_name);
    ClassDB::bind_method(D_METHOD("get_size_bytes"), &SteamCloudFile::get_size_bytes);
    ClassDB::bind_method(D_METHOD("get_modified_at_unix"), &SteamCloudFile::get_modified_at_unix);
    ClassDB::bind_method(D_METHOD("is_persisted"), &SteamCloudFile::is_persisted);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_name");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "size_bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_size_bytes");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "modified_at_unix", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_modified_at_unix");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "persisted", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_persisted");
}

String SteamCloudFile::get_name() const {
    return name;
}

int64_t SteamCloudFile::get_size_bytes() const {
    return size_bytes;
}

int64_t SteamCloudFile::get_modified_at_unix() const {
    return modified_at_unix;
}

bool SteamCloudFile::is_persisted() const {
    return persisted;
}

Ref<SteamCloudFile> SteamCloudFile::create(const String &name, int64_t size, int64_t modified, bool persisted) {
    Ref<SteamCloudFile> file;
    file.instantiate();
    file->name = name;
    file->size_bytes = size;
    file->modified_at_unix = modified;
    file->persisted = persisted;
    return file;
}

void SteamCloudFileResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamCloudFileResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamCloudFile",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamCloudFile> SteamCloudFileResult::get_value() const {
    return value;
}

Ref<SteamCloudFileResult> SteamCloudFileResult::create(Error error, const Ref<SteamCloudFile> &file) {
    Ref<SteamCloudFileResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = file;
    return result;
}

void SteamCloudFilesResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamCloudFilesResult::get_value);
    ClassDB::bind_method(D_METHOD("get_total_count"), &SteamCloudFilesResult::get_total_count);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, "SteamCloudFile",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_total_count");
}

TypedArray<SteamCloudFile> SteamCloudFilesResult::get_value() const {
    return value;
}

int64_t SteamCloudFilesResult::get_total_count() const {
    return total_count;
}

Ref<SteamCloudFilesResult> SteamCloudFilesResult::create(Error error, const TypedArray<SteamCloudFile> &files,
        int64_t total) {
    Ref<SteamCloudFilesResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = files;
    result->value.make_read_only();
    result->total_count = total;
    return result;
}

void SteamCloudQuota::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_total_bytes"), &SteamCloudQuota::get_total_bytes);
    ClassDB::bind_method(D_METHOD("get_available_bytes"), &SteamCloudQuota::get_available_bytes);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total_bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_total_bytes");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "available_bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_available_bytes");
}

int64_t SteamCloudQuota::get_total_bytes() const {
    return total_bytes;
}

int64_t SteamCloudQuota::get_available_bytes() const {
    return available_bytes;
}

Ref<SteamCloudQuota> SteamCloudQuota::create(int64_t total, int64_t available) {
    Ref<SteamCloudQuota> quota;
    quota.instantiate();
    quota->total_bytes = total;
    quota->available_bytes = available;
    return quota;
}

void SteamCloudQuotaResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamCloudQuotaResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamCloudQuota",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamCloudQuota> SteamCloudQuotaResult::get_value() const {
    return value;
}

Ref<SteamCloudQuotaResult> SteamCloudQuotaResult::create(Error error, const Ref<SteamCloudQuota> &quota) {
    Ref<SteamCloudQuotaResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = quota;
    return result;
}

void SteamCloudChange::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_name_or_path"), &SteamCloudChange::get_name_or_path);
    ClassDB::bind_method(D_METHOD("get_change_type"), &SteamCloudChange::get_change_type);
    ClassDB::bind_method(D_METHOD("get_path_type"), &SteamCloudChange::get_path_type);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "name_or_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_name_or_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "change_type", PROPERTY_HINT_ENUM, "Unknown,Updated,Deleted",
                         PROPERTY_USAGE_READ_ONLY), "", "get_change_type");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "path_type", PROPERTY_HINT_ENUM, "Unknown,Absolute,Cloud Name",
                         PROPERTY_USAGE_READ_ONLY), "", "get_path_type");
    BIND_ENUM_CONSTANT(CHANGE_UNKNOWN);
    BIND_ENUM_CONSTANT(CHANGE_UPDATED);
    BIND_ENUM_CONSTANT(CHANGE_DELETED);
    BIND_ENUM_CONSTANT(PATH_UNKNOWN);
    BIND_ENUM_CONSTANT(PATH_ABSOLUTE);
    BIND_ENUM_CONSTANT(PATH_CLOUD_NAME);
}

String SteamCloudChange::get_name_or_path() const {
    return name_or_path;
}

SteamCloudChange::ChangeType SteamCloudChange::get_change_type() const {
    return change_type;
}

SteamCloudChange::PathType SteamCloudChange::get_path_type() const {
    return path_type;
}

Ref<SteamCloudChange> SteamCloudChange::create(const String &path, ChangeType change, PathType type) {
    Ref<SteamCloudChange> result;
    result.instantiate();
    result->name_or_path = path;
    result->change_type = change;
    result->path_type = type;
    return result;
}

void SteamCloud::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamCloud::is_available);
    ClassDB::bind_method(D_METHOD("is_enabled_for_account"), &SteamCloud::is_enabled_for_account);
    ClassDB::bind_method(D_METHOD("is_enabled_for_app"), &SteamCloud::is_enabled_for_app);
    ClassDB::bind_method(D_METHOD("begin_write_batch"), &SteamCloud::begin_write_batch);
    ClassDB::bind_method(D_METHOD("read", "name"), &SteamCloud::read);
    ClassDB::bind_method(D_METHOD("write", "name", "bytes"), &SteamCloud::write);
    ClassDB::bind_method(D_METHOD("read_file", "name"), &SteamCloud::read_file);
    ClassDB::bind_method(D_METHOD("write_file", "name", "bytes"), &SteamCloud::write_file);
    ClassDB::bind_method(D_METHOD("share", "name"), &SteamCloud::share);
    ClassDB::bind_method(D_METHOD("share_file", "name"), &SteamCloud::share_file);
    ClassDB::bind_method(D_METHOD("download_shared", "handle", "max_bytes", "expected_name"),
            &SteamCloud::download_shared, DEFVAL(MAX_FILE_BYTES), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("download_shared_file", "handle", "max_bytes", "expected_name"),
            &SteamCloud::download_shared_file, DEFVAL(MAX_FILE_BYTES), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("delete_file", "name"), &SteamCloud::delete_file);
    ClassDB::bind_method(D_METHOD("file_info", "name"), &SteamCloud::file_info);
    ClassDB::bind_method(D_METHOD("files", "offset", "limit"), &SteamCloud::files, DEFVAL(0), DEFVAL(64));
    ClassDB::bind_method(D_METHOD("quota"), &SteamCloud::quota);

    BIND_CONSTANT(MAX_FILE_BYTES);
    BIND_CONSTANT(MAX_PAGE_FILES);
    ADD_SIGNAL(MethodInfo("local_files_changed", PropertyInfo(Variant::ARRAY, "changes", PROPERTY_HINT_ARRAY_TYPE,
                                                   "SteamCloudChange"),
            PropertyInfo(Variant::BOOL, "truncated")));
}

void SteamCloud::configure(const std::shared_ptr<SteamRuntime> &steam_runtime, const std::shared_ptr<SteamClientScope> &owner) {
#ifdef CRIMSON_WITH_STEAM
    runtime = steam_runtime;
    scope = owner;
    reservations = process_reservations.lock();
    if (!reservations) {
        reservations = std::make_shared<SteamCloudReservations>();
        process_reservations = reservations;
    }
    const ObjectID self(get_instance_id());
    change_subscription = steam_runtime->subscribe(RemoteStorageLocalFileChange_t::k_iCallback,
            [self](const void *, size_t size) {
                if (size == 0 || size == sizeof(RemoteStorageLocalFileChange_t)) {
                    const Ref<SteamCloud> cloud(Object::cast_to<SteamCloud>(ObjectDB::get_instance(self)));
                    if (cloud.is_valid()) {
                        cloud->emit_local_file_changes();
                    }
                }
            });
#endif
}

void SteamCloud::retire_write_batch() {
#ifdef CRIMSON_WITH_STEAM
    if (const auto batch = std::exchange(write_batch, {})) {
        batch->close();
    }
#endif
}

void SteamCloud::close() {
#ifdef CRIMSON_WITH_STEAM
    retire_write_batch();
    const auto active = runtime.lock();
    const uint64_t subscription = std::exchange(change_subscription, 0);
    if (active && subscription != 0) {
        active->unsubscribe(subscription);
    }
    runtime.reset();
    scope.reset();
    reservations.reset();
    const auto closed = std::exchange(pending, {});
    for (const auto &observation : closed) {
        if (const auto transfer = observation.lock()) {
            transfer->settle_observer(ERR_SKIP);
        }
    }
#endif
}

void SteamCloud::tick() {
#ifdef CRIMSON_WITH_STEAM
    if (write_batch && write_batch->abandoned.load()) {
        retire_write_batch();
    }
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                          [](const auto &observation) { return observation.expired(); }),
            pending.end());
    if (pending.empty()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    // Snapshot expired observations before their signals can mutate this facade.
    std::vector<std::shared_ptr<SteamCloudPending>> expired;
    for (const auto &observation : pending) {
        if (auto transfer = observation.lock(); transfer && transfer->observer && now >= transfer->deadline) {
            expired.push_back(std::move(transfer));
        }
    }
    for (const auto &transfer : expired) {
        transfer->settle_observer(ERR_TIMEOUT);
    }
#endif
}

bool SteamCloud::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return access_storage(runtime, scope).storage != nullptr;
#else
    return false;
#endif
}

Ref<SteamCloudWriteBatchResult> SteamCloud::begin_write_batch() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamCloudWriteBatchResult::create(ERR_BUSY);
    }
    const auto access = access_storage(runtime, scope);
    if (!access.storage || !reservations) {
        return SteamCloudWriteBatchResult::create(ERR_UNAVAILABLE);
    }
    if (const auto active = reservations->batch.lock()) {
        if (!active->abandoned.load()) {
            return SteamCloudWriteBatchResult::create(ERR_BUSY);
        }
        active->close();
    }
    const auto begin = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_BeginFileWriteBatch)>(
            "SteamAPI_ISteamRemoteStorage_BeginFileWriteBatch");
    const auto end = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_EndFileWriteBatch)>(
            "SteamAPI_ISteamRemoteStorage_EndFileWriteBatch");
    if (!begin || !end) {
        return SteamCloudWriteBatchResult::create(ERR_UNAVAILABLE);
    }
    auto state = std::make_shared<SteamCloudWriteBatchState>();
    Ref<SteamCloudWriteBatch> batch;
    batch.instantiate();
    if (!begin(access.storage)) {
        return SteamCloudWriteBatchResult::create(ERR_BUSY);
    }
    state->runtime = runtime;
    state->reservations = reservations;
    batch->state = state;
    reservations->batch = state;
    write_batch = std::move(state);
    return SteamCloudWriteBatchResult::create(OK, batch);
#else
    return SteamCloudWriteBatchResult::create(ERR_UNAVAILABLE);
#endif
}

bool SteamCloud::is_enabled_for_account() const {
#ifdef CRIMSON_WITH_STEAM
    const auto access = access_storage(runtime, scope);
    if (access.storage) {
        const auto enabled = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount)>(
                "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount");
        return enabled && enabled(access.storage);
    }
#endif
    return false;
}

bool SteamCloud::is_enabled_for_app() const {
#ifdef CRIMSON_WITH_STEAM
    const auto access = access_storage(runtime, scope);
    if (access.storage) {
        const auto enabled = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp)>(
                "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp");
        return enabled && enabled(access.storage);
    }
#endif
    return false;
}

Variant SteamCloud::read(const String &name) {
    return read_file(name)->wait();
}

Variant SteamCloud::write(const String &name, const PackedByteArray &bytes) {
    return write_file(name, bytes)->wait();
}

Ref<SteamBytesOperation> SteamCloud::read_file(const String &name) {
    Ref<SteamBytesOperation> operation;
    operation.instantiate();
    const Error submitted = transfer(name, nullptr, [operation](Error error, const String &message, TransferValue value) {
        auto *bytes = std::get_if<PackedByteArray>(&value);
        operation->finish(SteamBytesResult::create(error, message, bytes ? std::move(*bytes) : PackedByteArray()));
    }, operation->cancel_observation);
    if (!operation->cancel_observation) {
        operation->finish(SteamBytesResult::create(submitted));
    }
    return operation;
}

Ref<SteamOperation> SteamCloud::write_file(const String &name, const PackedByteArray &bytes) {
    Ref<SteamOperation> operation;
    operation.instantiate();
    const Error submitted = transfer(name, &bytes, [operation](Error error, const String &message, TransferValue) {
        operation->finish(SteamResult::from_error(error, message));
    }, operation->cancel_observation);
    if (!operation->cancel_observation) {
        operation->finish(SteamResult::from_error(submitted));
    }
    return operation;
}

Variant SteamCloud::share(const String &name) {
    return share_file(name)->wait();
}

Ref<SteamCloudShareOperation> SteamCloud::share_file(const String &name) {
    Ref<SteamCloudShareOperation> operation;
    operation.instantiate();
    const Error submitted = submit_share(name, [operation](Error error, const String &message, TransferValue value) {
        const auto *handle = std::get_if<String>(&value);
        operation->finish(SteamStringResult::create(error, handle ? *handle : String(), message));
    }, operation->cancel_observation);
    if (!operation->cancel_observation) {
        operation->finish(SteamStringResult::create(submitted));
    }
    return operation;
}

Variant SteamCloud::download_shared(const String &handle, int64_t max_bytes, const String &expected_name) {
    return download_shared_file(handle, max_bytes, expected_name)->wait();
}

Ref<SteamSharedCloudFileOperation> SteamCloud::download_shared_file(const String &handle, int64_t max_bytes,
        const String &expected_name) {
    Ref<SteamSharedCloudFileOperation> operation;
    operation.instantiate();
    const Error submitted = submit_download(handle, max_bytes, expected_name,
            [operation](Error error, const String &message, TransferValue value) {
                const auto *file = std::get_if<Ref<SteamSharedCloudFile>>(&value);
                operation->finish(SteamSharedCloudFileResult::create(error, message,
                        file ? *file : Ref<SteamSharedCloudFile>()));
            }, operation->cancel_observation);
    if (!operation->cancel_observation) {
        operation->finish(SteamSharedCloudFileResult::create(submitted));
    }
    return operation;
}

Error SteamCloud::transfer(const String &name, const PackedByteArray *bytes, TransferObserver observer,
        std::function<Error()> &cancel_observation) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return ERR_UNAVAILABLE;
    }
    const CharString encoded = name.utf8();
    std::string key = reservation_key(encoded);
    if (key.empty() || (bytes && (bytes->is_empty() || bytes->size() > MAX_FILE_BYTES))) {
        return ERR_INVALID_PARAMETER;
    }
    if (reservations->names.count(key) != 0) {
        return ERR_BUSY;
    }

    uint32_t read_size = 0;
    if (!bytes) {
        const auto exists = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileExists)>(
                "SteamAPI_ISteamRemoteStorage_FileExists");
        const auto get_size = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileSize)>(
                "SteamAPI_ISteamRemoteStorage_GetFileSize");
        if (!exists || !get_size) {
            return ERR_UNAVAILABLE;
        }
        if (!exists(access.storage, encoded.get_data())) {
            return ERR_FILE_NOT_FOUND;
        }
        const int32_t size = get_size(access.storage, encoded.get_data());
        if (size <= 0 || size > MAX_FILE_BYTES) {
            return size == 0 ? OK : ERR_FILE_CANT_READ;
        }
        read_size = static_cast<uint32_t>(size);
    }
    if (reservations->names.size() >= maximum_transfers || !access.runtime->can_track_call()) {
        return ERR_BUSY;
    }

    auto record = std::make_shared<SteamCloudPending>();
    record->runtime = runtime;
    record->reservations = reservations;
    record->key = std::move(key);
    reservations->names.insert(record->key);
    record->observer = std::move(observer);
    bool watching = false;
    if (bytes) {
        const auto write = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileWriteAsync)>(
                "SteamAPI_ISteamRemoteStorage_FileWriteAsync");
        if (!write) {
            return ERR_UNAVAILABLE;
        }
        record->write_data = bytes->duplicate();
        record->call = write(access.storage, encoded.get_data(), record->write_data.ptr(),
                static_cast<uint32_t>(record->write_data.size()));
        watching = access.runtime->watch_call(record->call, RemoteStorageFileWriteAsyncComplete_t::k_iCallback,
                sizeof(RemoteStorageFileWriteAsyncComplete_t), [record](const void *result) { record->finish_write(result); });
    } else {
        const auto read = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileReadAsync)>(
                "SteamAPI_ISteamRemoteStorage_FileReadAsync");
        const auto read_complete = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileReadAsyncComplete)>(
                "SteamAPI_ISteamRemoteStorage_FileReadAsyncComplete");
        if (!read || !read_complete) {
            return ERR_UNAVAILABLE;
        }
        record->read_size = read_size;
        record->call = read(access.storage, encoded.get_data(), 0, read_size);
        watching = access.runtime->watch_call(record->call, RemoteStorageFileReadAsyncComplete_t::k_iCallback,
                sizeof(RemoteStorageFileReadAsyncComplete_t), [record](const void *result) { record->finish_read(result); });
    }
    if (!watching) {
        return bytes ? ERR_FILE_CANT_WRITE : ERR_FILE_CANT_READ;
    }
    observe_transfer(record, cancel_observation);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamCloud::submit_share(const String &name, TransferObserver observer,
        std::function<Error()> &cancel_observation) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return ERR_UNAVAILABLE;
    }
    const CharString encoded = name.utf8();
    std::string key = reservation_key(encoded);
    if (key.empty()) {
        return ERR_INVALID_PARAMETER;
    }
    if (reservations->names.count(key) != 0 || reservations->names.size() >= maximum_transfers ||
            !access.runtime->can_track_call()) {
        return ERR_BUSY;
    }
    const auto share = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileShare)>(
            "SteamAPI_ISteamRemoteStorage_FileShare");
    const auto exists = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileExists)>(
            "SteamAPI_ISteamRemoteStorage_FileExists");
    const auto get_size = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileSize)>(
            "SteamAPI_ISteamRemoteStorage_GetFileSize");
    if (!share || !exists || !get_size) {
        return ERR_UNAVAILABLE;
    }
    if (!exists(access.storage, encoded.get_data())) {
        return ERR_FILE_NOT_FOUND;
    }
    const int32_t size = get_size(access.storage, encoded.get_data());
    if (size < 0 || size > MAX_FILE_BYTES) {
        return ERR_FILE_CANT_READ;
    }

    auto record = std::make_shared<SteamCloudPending>();
    record->runtime = runtime;
    record->reservations = reservations;
    record->key = std::move(key);
    reservations->names.insert(record->key);
    record->observer = std::move(observer);
    record->expected_name = name;
    record->call = share(access.storage, encoded.get_data());
    if (!access.runtime->watch_call(record->call, RemoteStorageFileShareResult_t::k_iCallback,
            sizeof(RemoteStorageFileShareResult_t), [record](const void *result) { record->finish_share(result); })) {
        return ERR_FILE_CANT_WRITE;
    }
    observe_transfer(record, cancel_observation);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamCloud::submit_download(const String &handle, int64_t max_bytes, const String &expected_name,
        TransferObserver observer, std::function<Error()> &cancel_observation) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return ERR_UNAVAILABLE;
    }
    UGCHandle_t content = k_UGCHandleInvalid;
    const CharString encoded = handle.utf8();
    if (!parse_ugc_handle(encoded, content) || max_bytes < 1 || max_bytes > MAX_FILE_BYTES ||
            (!expected_name.is_empty() && reservation_key(expected_name.utf8()).empty())) {
        return ERR_INVALID_PARAMETER;
    }
    // The colon keeps download reservations separate from any valid Cloud filename.
    const std::string key = "download:" + std::string(encoded.get_data(), static_cast<size_t>(encoded.length()));
    if (reservations->names.count(key) != 0 || reservations->names.size() >= maximum_transfers ||
            !access.runtime->can_track_call()) {
        return ERR_BUSY;
    }
    const auto download = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_UGCDownload)>(
            "SteamAPI_ISteamRemoteStorage_UGCDownload");
    const auto read = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_UGCRead)>(
            "SteamAPI_ISteamRemoteStorage_UGCRead");
    if (!download || !read) {
        return ERR_UNAVAILABLE;
    }

    auto record = std::make_shared<SteamCloudPending>();
    record->runtime = runtime;
    record->reservations = reservations;
    record->key = key;
    reservations->names.insert(record->key);
    record->observer = std::move(observer);
    record->read_size = static_cast<uint32_t>(max_bytes);
    record->expected_name = expected_name;
    record->download_handle = content;
    record->download_app_id = access.runtime->get_app_id();
    record->call = download(access.storage, content, 0);
    if (!access.runtime->watch_call(record->call, RemoteStorageDownloadUGCResult_t::k_iCallback,
            sizeof(RemoteStorageDownloadUGCResult_t), [record](const void *result) { record->finish_download(result); })) {
        return ERR_FILE_CANT_READ;
    }
    observe_transfer(record, cancel_observation);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamCloud::observe_transfer(const std::shared_ptr<SteamCloudPending> &record,
        std::function<Error()> &cancel_observation) {
#ifdef CRIMSON_WITH_STEAM
    record->deadline = Time::get_singleton()->get_ticks_msec() + observation_timeout_ms;
    cancel_observation = [weak = std::weak_ptr<SteamCloudPending>(record)]() {
        const auto transfer = weak.lock();
        return transfer ? transfer->cancel() : ERR_DOES_NOT_EXIST;
    };
    pending.emplace_back(record);
#endif
}

Error SteamCloud::delete_file(const String &name) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return ERR_UNAVAILABLE;
    }
    const CharString encoded = name.utf8();
    const std::string key = reservation_key(encoded);
    if (key.empty()) {
        return ERR_INVALID_PARAMETER;
    }
    if (reservations->names.count(key) != 0) {
        return ERR_BUSY;
    }
    const auto exists = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileExists)>(
            "SteamAPI_ISteamRemoteStorage_FileExists");
    const auto remove = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileDelete)>(
            "SteamAPI_ISteamRemoteStorage_FileDelete");
    if (!exists || !remove) {
        return ERR_UNAVAILABLE;
    }
    if (!exists(access.storage, encoded.get_data())) {
        return ERR_FILE_NOT_FOUND;
    }
    return remove(access.storage, encoded.get_data()) ? OK : ERR_FILE_CANT_WRITE;
#else
    return ERR_UNAVAILABLE;
#endif
}

Ref<SteamCloudFileResult> SteamCloud::file_info(const String &name) const {
#ifdef CRIMSON_WITH_STEAM
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return SteamCloudFileResult::create(ERR_UNAVAILABLE);
    }
    const CharString encoded = name.utf8();
    if (reservation_key(encoded).empty()) {
        return SteamCloudFileResult::create(ERR_INVALID_PARAMETER);
    }
    const auto exists = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FileExists)>(
            "SteamAPI_ISteamRemoteStorage_FileExists");
    const auto size = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileSize)>(
            "SteamAPI_ISteamRemoteStorage_GetFileSize");
    const auto timestamp = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileTimestamp)>(
            "SteamAPI_ISteamRemoteStorage_GetFileTimestamp");
    const auto persisted = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FilePersisted)>(
            "SteamAPI_ISteamRemoteStorage_FilePersisted");
    if (!exists || !size || !timestamp || !persisted) {
        return SteamCloudFileResult::create(ERR_UNAVAILABLE);
    }
    if (!exists(access.storage, encoded.get_data())) {
        return SteamCloudFileResult::create(ERR_FILE_NOT_FOUND);
    }
    const int32_t length = size(access.storage, encoded.get_data());
    if (length < 0) {
        return SteamCloudFileResult::create(ERR_FILE_CANT_READ);
    }
    return SteamCloudFileResult::create(OK, SteamCloudFile::create(name, length,
            timestamp(access.storage, encoded.get_data()), persisted(access.storage, encoded.get_data())));
#else
    return SteamCloudFileResult::create(ERR_UNAVAILABLE);
#endif
}

Ref<SteamCloudFilesResult> SteamCloud::files(int64_t offset, int64_t limit) const {
#ifdef CRIMSON_WITH_STEAM
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return SteamCloudFilesResult::create(ERR_UNAVAILABLE);
    }
    if (offset < 0 || limit < 1 || limit > MAX_PAGE_FILES) {
        return SteamCloudFilesResult::create(ERR_INVALID_PARAMETER);
    }
    const auto count = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileCount)>(
            "SteamAPI_ISteamRemoteStorage_GetFileCount");
    const auto entry = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileNameAndSize)>(
            "SteamAPI_ISteamRemoteStorage_GetFileNameAndSize");
    const auto timestamp = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetFileTimestamp)>(
            "SteamAPI_ISteamRemoteStorage_GetFileTimestamp");
    const auto persisted = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_FilePersisted)>(
            "SteamAPI_ISteamRemoteStorage_FilePersisted");
    if (!count || !entry || !timestamp || !persisted) {
        return SteamCloudFilesResult::create(ERR_UNAVAILABLE);
    }
    const int32_t total = count(access.storage);
    if (total < 0) {
        return SteamCloudFilesResult::create(ERR_FILE_CANT_READ);
    }
    TypedArray<SteamCloudFile> files;
    const int64_t page_count = (std::min)(limit, total - offset);
    for (int64_t index = 0; index < page_count; ++index) {
        int32_t size = -1;
        const char *raw = entry(access.storage, static_cast<int32_t>(offset + index), &size);
        const String name = copy_sdk_path(raw, k_cchFilenameMax);
        if (name.is_empty() || size < 0) {
            return SteamCloudFilesResult::create(ERR_FILE_CANT_READ);
        }
        const CharString encoded = name.utf8();
        files.push_back(SteamCloudFile::create(name, size, timestamp(access.storage, encoded.get_data()),
                persisted(access.storage, encoded.get_data())));
    }
    return SteamCloudFilesResult::create(OK, files, total);
#else
    return SteamCloudFilesResult::create(ERR_UNAVAILABLE);
#endif
}

Ref<SteamCloudQuotaResult> SteamCloud::quota() const {
#ifdef CRIMSON_WITH_STEAM
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return SteamCloudQuotaResult::create(ERR_UNAVAILABLE);
    }
    const auto get_quota = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetQuota)>(
            "SteamAPI_ISteamRemoteStorage_GetQuota");
    uint64 total = 0;
    uint64 available = 0;
    if (!get_quota) {
        return SteamCloudQuotaResult::create(ERR_UNAVAILABLE);
    }
    if (!get_quota(access.storage, &total, &available) || available > total ||
            total > static_cast<uint64>((std::numeric_limits<int64_t>::max)())) {
        return SteamCloudQuotaResult::create(FAILED);
    }
    return SteamCloudQuotaResult::create(OK, SteamCloudQuota::create(static_cast<int64_t>(total),
            static_cast<int64_t>(available)));
#else
    return SteamCloudQuotaResult::create(ERR_UNAVAILABLE);
#endif
}

void SteamCloud::emit_local_file_changes() {
#ifdef CRIMSON_WITH_STEAM
    const auto access = access_storage(runtime, scope);
    if (!access.storage) {
        return;
    }
    const auto count = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetLocalFileChangeCount)>(
            "SteamAPI_ISteamRemoteStorage_GetLocalFileChangeCount");
    const auto change = access.runtime->symbol<decltype(&SteamAPI_ISteamRemoteStorage_GetLocalFileChange)>(
            "SteamAPI_ISteamRemoteStorage_GetLocalFileChange");
    if (!count || !change) {
        return;
    }
    const int32_t total = count(access.storage);
    bool truncated = total < 0 || total > maximum_local_changes;
    TypedArray<SteamCloudChange> changes;
    for (int32_t index = 0; index < (std::min)(total, maximum_local_changes); ++index) {
        ERemoteStorageLocalFileChange change_type = k_ERemoteStorageLocalFileChange_Invalid;
        ERemoteStorageFilePathType path_type = k_ERemoteStorageFilePathType_Invalid;
        const String path = copy_sdk_path(change(access.storage, index, &change_type, &path_type),
                maximum_change_path_bytes);
        if (path.is_empty()) {
            truncated = true;
            continue;
        }
        changes.push_back(SteamCloudChange::create(path, cloud_change_type(change_type), cloud_path_type(path_type)));
    }
    changes.make_read_only();
    emit_signal("local_files_changed", changes, truncated);
#endif
}

} // namespace crimson::steam
