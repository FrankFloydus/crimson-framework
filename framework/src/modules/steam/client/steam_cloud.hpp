// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_bytes_result.hpp"
#include "modules/steam/client/steam_operation.hpp"
#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>
#include <memory>
#include <variant>
#include <vector>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
struct SteamCloudPending;
struct SteamCloudReservations;
struct SteamCloudWriteBatchState;

// Scoped advisory write-batch hint for the active Steam AppID.
class SteamCloudWriteBatch : public godot::RefCounted {
    GDCLASS(SteamCloudWriteBatch, godot::RefCounted)

protected:
    // Bind scope status and explicit close.
    static void _bind_methods();

public:
    // Retire the owned hint, deferring to the client thread when necessary.
    ~SteamCloudWriteBatch() override;
    // Report whether this handle still owns the current Steam batch hint.
    bool is_open() const;
    // End this exact hint on the client thread. Repeated calls succeed after closure.
    godot::Error close();

private:
    friend class SteamCloud;

    // Exact native reservation shared with the owning client scope.
    std::shared_ptr<SteamCloudWriteBatchState> state;
};

// Outcome and scoped handle for starting a Cloud write-batch hint.
class SteamCloudWriteBatchResult : public SteamResult {
    GDCLASS(SteamCloudWriteBatchResult, SteamResult)

protected:
    // Bind the read-only scoped handle.
    static void _bind_methods();

public:
    // Return the handle on success, or null on failure.
    godot::Ref<SteamCloudWriteBatch> get_value() const;

private:
    friend class SteamCloud;

    // Construct the terminal status and exact batch handle.
    static godot::Ref<SteamCloudWriteBatchResult> create(godot::Error error,
            const godot::Ref<SteamCloudWriteBatch> &batch = {});

    // Advisory hint retained through this result's lifetime.
    godot::Ref<SteamCloudWriteBatch> value;
};

// Retained observation of one Cloud file share with a typed result.
class SteamCloudShareOperation : public godot::RefCounted {
    GDCLASS(SteamCloudShareOperation, godot::RefCounted)

protected:
    // Register the retained share operation and its typed completion signal.
    static void _bind_methods();

public:
    // Report whether the operation has a terminal result.
    bool is_finished() const;
    // Return the terminal result or null while observation is pending.
    godot::Ref<SteamStringResult> result() const;
    // Return the settled result or the signal to await.
    godot::Variant wait();
    // Stop observing locally while Steam finishes the share request.
    godot::Error cancel();

private:
    friend class SteamCloud;

    // Settle observation exactly once and emit completion after state is final.
    void finish(const godot::Ref<SteamStringResult> &outcome);

    // Observer-only cancellation callback with a weak native record reference.
    std::function<godot::Error()> cancel_observation;
    // Terminal share status and handle after the operation finishes.
    godot::Ref<SteamStringResult> outcome;
};

// Detached metadata and bytes for a shared Steam Cloud file.
class SteamSharedCloudFile : public godot::RefCounted {
    GDCLASS(SteamSharedCloudFile, godot::RefCounted)

protected:
    // Bind the read-only fields copied from the validated SDK result.
    static void _bind_methods();

public:
    // Return the opaque UGC handle as an unsigned decimal string.
    godot::String get_handle() const;
    // Return the validated logical filename reported by Steam.
    godot::String get_name() const;
    // Return the owner SteamID64 as a decimal string, or an empty string if unavailable.
    godot::String get_owner_steam_id() const;
    // Return the shared bytes copied within the caller's requested limit.
    godot::PackedByteArray get_bytes() const;

private:
    friend struct SteamCloudPending;

    // Copy the validated download payload before notifying observers.
    static godot::Ref<SteamSharedCloudFile> create(const godot::String &handle, const godot::String &name,
            const godot::String &owner_steam_id, godot::PackedByteArray bytes);

    // Opaque Steam UGC handle.
    godot::String handle;
    // Portable logical filename returned by Steam.
    godot::String name;
    // SteamID64 of the sharing account, when Steam reports it.
    godot::String owner_steam_id;
    // Bytes detached from the SDK download cache.
    godot::PackedByteArray bytes;
};

// Outcome and detached file from a shared Cloud download.
class SteamSharedCloudFileResult : public SteamResult {
    GDCLASS(SteamSharedCloudFileResult, SteamResult)

protected:
    // Bind the read-only downloaded file.
    static void _bind_methods();

public:
    // Return the downloaded file on success, or null on failure.
    godot::Ref<SteamSharedCloudFile> get_value() const;

private:
    friend class SteamCloud;

    // Construct a settled download result before publishing it.
    static godot::Ref<SteamSharedCloudFileResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamSharedCloudFile> &file = {});

    // Detached metadata and bytes for a successful download.
    godot::Ref<SteamSharedCloudFile> value;
};

// Retained observation of a shared Cloud download.
class SteamSharedCloudFileOperation : public godot::RefCounted {
    GDCLASS(SteamSharedCloudFileOperation, godot::RefCounted)

protected:
    // Register the retained download and its typed completion signal.
    static void _bind_methods();

public:
    // Report whether the operation has a terminal result.
    bool is_finished() const;
    // Return the terminal result or null while observation is pending.
    godot::Ref<SteamSharedCloudFileResult> result() const;
    // Return the settled result or the signal to await.
    godot::Variant wait();
    // Stop observing locally while Steam finishes and releases the native download.
    godot::Error cancel();

private:
    friend class SteamCloud;

    // Settle observation exactly once and emit completion after state is final.
    void finish(const godot::Ref<SteamSharedCloudFileResult> &outcome);

    // Observer-only cancellation callback with a weak native record reference.
    std::function<godot::Error()> cancel_observation;
    // Terminal status and detached file after the operation finishes.
    godot::Ref<SteamSharedCloudFileResult> outcome;
};

// Detached metadata for one local Steam Remote Storage file.
class SteamCloudFile : public godot::RefCounted {
    GDCLASS(SteamCloudFile, godot::RefCounted)

protected:
    // Register the read-only file fields.
    static void _bind_methods();

public:
    // Return the app-relative Steam Cloud filename.
    godot::String get_name() const;
    // Return the file size in bytes.
    int64_t get_size_bytes() const;
    // Return the last modification time in Unix seconds.
    int64_t get_modified_at_unix() const;
    // Return Steam's persisted flag for this file.
    bool is_persisted() const;

private:
    friend class SteamCloud;

    // Construct metadata copied out of the native interface.
    static godot::Ref<SteamCloudFile> create(const godot::String &name, int64_t size,
            int64_t modified, bool persisted);

    // The app-relative Remote Storage filename.
    godot::String name;
    // File size in bytes.
    int64_t size_bytes = 0;
    // Modification time in Unix seconds.
    int64_t modified_at_unix = 0;
    // Steam's flag indicating persistence in Cloud storage.
    bool persisted = false;
};

// Outcome and metadata from looking up one named Cloud file.
class SteamCloudFileResult : public SteamResult {
    GDCLASS(SteamCloudFileResult, SteamResult)

protected:
    // Bind the read-only file metadata result.
    static void _bind_methods();

public:
    // Return the metadata on success, or null on failure.
    godot::Ref<SteamCloudFile> get_value() const;

private:
    friend class SteamCloud;

    // Construct a settled file query result.
    static godot::Ref<SteamCloudFileResult> create(godot::Error error,
            const godot::Ref<SteamCloudFile> &file = {});

    // Detached metadata for the successful query.
    godot::Ref<SteamCloudFile> value;
};

// Outcome and bounded page from enumerating local Cloud files.
class SteamCloudFilesResult : public SteamResult {
    GDCLASS(SteamCloudFilesResult, SteamResult)

protected:
    // Bind the file page and total file count.
    static void _bind_methods();

public:
    // Return the read-only array of the requested file page.
    godot::TypedArray<SteamCloudFile> get_value() const;
    // Return the total count observed before copying this page.
    int64_t get_total_count() const;

private:
    friend class SteamCloud;

    // Construct a settled file page result.
    static godot::Ref<SteamCloudFilesResult> create(godot::Error error,
            const godot::TypedArray<SteamCloudFile> &files = {}, int64_t total = 0);

    // Detached snapshots for this bounded file page.
    godot::TypedArray<SteamCloudFile> value;
    // Total number of files returned by Steam at query time.
    int64_t total_count = 0;
};

// Byte limits for the signed-in account's active app storage.
class SteamCloudQuota : public godot::RefCounted {
    GDCLASS(SteamCloudQuota, godot::RefCounted)

protected:
    // Bind the read-only quota fields.
    static void _bind_methods();

public:
    // Return total capacity in bytes.
    int64_t get_total_bytes() const;
    // Return available capacity in bytes.
    int64_t get_available_bytes() const;

private:
    friend class SteamCloud;

    // Construct a detached capacity snapshot.
    static godot::Ref<SteamCloudQuota> create(int64_t total, int64_t available);

    // Total capacity in bytes.
    int64_t total_bytes = 0;
    // Free capacity in bytes.
    int64_t available_bytes = 0;
};

// Outcome and byte limits from querying the active app's Cloud storage.
class SteamCloudQuotaResult : public SteamResult {
    GDCLASS(SteamCloudQuotaResult, SteamResult)

protected:
    // Bind the read-only capacity snapshot.
    static void _bind_methods();

public:
    // Return the quota on success, or null on failure.
    godot::Ref<SteamCloudQuota> get_value() const;

private:
    friend class SteamCloud;

    // Construct a settled capacity query result.
    static godot::Ref<SteamCloudQuotaResult> create(godot::Error error, const godot::Ref<SteamCloudQuota> &quota = {});

    // Detached capacity snapshot for the successful query.
    godot::Ref<SteamCloudQuota> value;
};

// Detached local file change received after a dynamic Steam Cloud sync.
class SteamCloudChange : public godot::RefCounted {
    GDCLASS(SteamCloudChange, godot::RefCounted)

protected:
    // Bind the change enums and read-only fields.
    static void _bind_methods();

public:
    // What Steam changed on the local device.
    enum ChangeType {
        CHANGE_UNKNOWN,
        CHANGE_UPDATED,
        CHANGE_DELETED,
    };

    // How to interpret the file name or path in this change.
    enum PathType {
        PATH_UNKNOWN,
        PATH_ABSOLUTE,
        PATH_CLOUD_NAME,
    };

    // Return the name or path reported by Steam.
    godot::String get_name_or_path() const;
    // Return the kind of local change already applied by Steam.
    ChangeType get_change_type() const;
    // Return how the name or path should be interpreted.
    PathType get_path_type() const;

private:
    friend class SteamCloud;

    // Copy a dynamic-sync change before publishing it to scripts.
    static godot::Ref<SteamCloudChange> create(const godot::String &path, ChangeType change, PathType type);

    // Absolute local path or logical Remote Storage filename.
    godot::String name_or_path;
    // Kind of local change.
    ChangeType change_type = CHANGE_UNKNOWN;
    // Interpretation of the reported path.
    PathType path_type = PATH_UNKNOWN;
};

// Client-scoped access to named files in Steam Remote Storage.
class SteamCloud : public godot::RefCounted {
    GDCLASS(SteamCloud, godot::RefCounted)

protected:
    // Register the service and its dynamic Cloud sync signal.
    static void _bind_methods();

public:
    // Maximum whole-file transfer size supported by this service.
    enum { MAX_FILE_BYTES = 16 * 1024 * 1024 };
    // Maximum number of files copied per list query.
    enum { MAX_PAGE_FILES = 256 };

    // Return whether the active client exposes Steam Remote Storage.
    bool is_available() const;
    // Return Steam's account-wide Cloud sync preference.
    bool is_enabled_for_account() const;
    // Return Steam's Cloud sync preference for the active AppID.
    bool is_enabled_for_app() const;
    // Start an advisory app-wide hint for Remote Storage or Auto-Cloud writes.
    godot::Ref<SteamCloudWriteBatchResult> begin_write_batch();
    // Return the completed read result or the completion signal to await directly.
    godot::Variant read(const godot::String &name);
    // Return the completed write result or the completion signal to await directly.
    godot::Variant write(const godot::String &name, const godot::PackedByteArray &bytes);
    // Read one logical file asynchronously, completing immediately for an existing empty file.
    godot::Ref<SteamBytesOperation> read_file(const godot::String &name);
    // Submit a nonempty whole-file write and observe local completion.
    godot::Ref<SteamOperation> write_file(const godot::String &name, const godot::PackedByteArray &bytes);
    // Return the share result or a completion signal that yields the unsigned decimal UGC handle.
    godot::Variant share(const godot::String &name);
    // Share one local file and return the retained typed operation.
    godot::Ref<SteamCloudShareOperation> share_file(const godot::String &name);
    // Return the download result or a completion signal for the bounded file from this app.
    godot::Variant download_shared(const godot::String &handle, int64_t max_bytes = MAX_FILE_BYTES,
            const godot::String &expected_name = {});
    // Download one shared file by UGC handle and return the retained typed operation.
    godot::Ref<SteamSharedCloudFileOperation> download_shared_file(const godot::String &handle,
            int64_t max_bytes = MAX_FILE_BYTES, const godot::String &expected_name = {});
    // Delete a named file locally and let Steam propagate the deletion.
    godot::Error delete_file(const godot::String &name);
    // Return the detached metadata or the reason the named query failed.
    godot::Ref<SteamCloudFileResult> file_info(const godot::String &name) const;
    // Return a bounded page of local file snapshots in Steam's current enumeration order.
    godot::Ref<SteamCloudFilesResult> files(int64_t offset = 0, int64_t limit = 64) const;
    // Return the current app capacity and free bytes or the reason the query failed.
    godot::Ref<SteamCloudQuotaResult> quota() const;

private:
    friend class SteamClient;
    friend struct SteamCloudPending;

    // The optional detached payload of a completed read, share, or download.
    using TransferValue = std::variant<std::monostate, godot::PackedByteArray, godot::String,
            godot::Ref<SteamSharedCloudFile>>;
    // Deliver the terminal native outcome to the attached typed operation.
    using TransferObserver = std::function<void(godot::Error, const godot::String &, TransferValue)>;

    // Bind the fresh service to one Steam client scope.
    void configure(const std::shared_ptr<SteamRuntime> &steam_runtime, const std::shared_ptr<SteamClientScope> &owner);
    // End the app-wide hint before client teardown can notify other services' observers.
    void retire_write_batch();
    // Invalidate the service and cancel observation before returning to the client.
    void close();
    // Expire observations that have reached their local deadline.
    void tick();
    // Submit a validated read or write and supply observer-only cancellation when pending.
    godot::Error transfer(const godot::String &name, const godot::PackedByteArray *bytes,
            TransferObserver observer, std::function<godot::Error()> &cancel_observation);
    // Submit an explicit file share and retain the local filename until Steam settles.
    godot::Error submit_share(const godot::String &name, TransferObserver observer,
            std::function<godot::Error()> &cancel_observation);
    // Submit a download and retain the handle through native cache cleanup.
    godot::Error submit_download(const godot::String &handle, int64_t max_bytes, const godot::String &expected_name,
            TransferObserver observer, std::function<godot::Error()> &cancel_observation);
    // Track a pending observer for cancellation, timeout, and client teardown.
    void observe_transfer(const std::shared_ptr<SteamCloudPending> &record,
            std::function<godot::Error()> &cancel_observation);
    // Copy a bounded dynamic-sync change snapshot and signal listeners.
    void emit_local_file_changes();

    // Steam runtime shared by active client scopes.
    std::weak_ptr<SteamRuntime> runtime;
    // Lease invalidated by the client before any stop callbacks run.
    std::shared_ptr<SteamClientScope> scope;
    // Process-wide reservations shared while the SDK can still complete transfers.
    std::shared_ptr<SteamCloudReservations> reservations;
    // Exact batch started by this client scope, retained for stop and deferred close.
    std::shared_ptr<SteamCloudWriteBatchState> write_batch;
    // Weak observations indexed by this facade for stop and timeout.
    std::vector<std::weak_ptr<SteamCloudPending>> pending;
    // Exact runtime callback subscription for dynamic Cloud sync.
    uint64_t change_subscription = 0;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamCloudChange::ChangeType);
VARIANT_ENUM_CAST(crimson::steam::SteamCloudChange::PathType);
