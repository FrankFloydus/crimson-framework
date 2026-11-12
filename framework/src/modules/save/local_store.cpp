// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "save_core.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>
#include <system_error>

namespace checkpoint {
namespace {

using Path = std::filesystem::path;
constexpr DWORD kIoChunk = 1U << 20;
constexpr size_t kMaxDirectoryEntries = 4096;

// Translate a Windows file error into a checkpoint failure with operation context.
[[noreturn]] void fail_windows(const char *operation, DWORD error) {
    const char *code = "IoError";
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            code = "NotFound";
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:
            code = "PermissionDenied";
            break;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            code = "Busy";
            break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:
            code = "OutOfSpace";
            break;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            code = "Conflict";
            break;
    }
    throw SaveError(code, std::string(operation) + " (Windows error " + std::to_string(error) + ")");
}

// Check whether Windows reported a missing file or directory.
bool missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

// Convert a filesystem path into the extended Windows path used for I/O.
std::wstring native_path(const Path &path) {
    const std::wstring &name = path.native();
    if (name.compare(0, 4, L"\\\\?\\") == 0) {
        return name;
    }
    if (name.compare(0, 2, L"\\\\") == 0) {
        return L"\\\\?\\UNC\\" + name.substr(2);
    }
    return L"\\\\?\\" + name;
}

// Owns a Windows file handle and closes it on scope exit.
class FileHandle {
public:
    // Adopt an opened file handle or transfer its ownership from another wrapper.
    explicit FileHandle(HANDLE handle) : handle_(handle) {}
    // Prevent two wrappers from owning the same file handle.
    FileHandle(const FileHandle &) = delete;
    // Prevent copying ownership of a live Windows file handle.
    FileHandle &operator=(const FileHandle &) = delete;
    // Transfer ownership and invalidate the source wrapper.
    FileHandle(FileHandle &&other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}

    // Close the owned handle during cleanup.
    ~FileHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    // Return the underlying handle for Windows API calls.
    HANDLE get() const { return handle_; }

    // Close the handle early and report any close error.
    void close() {
        HANDLE handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) {
            fail_windows("Could not close save file", GetLastError());
        }
    }

private:
    // Windows file handle currently owned by this wrapper.
    HANDLE handle_;
};

// Reject a handle that points to a directory or reparse point.
void require_regular_file(const FileHandle &file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info)) {
        fail_windows("Could not inspect save file", GetLastError());
    }
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw SaveError("Corrupt", "The save path must name a regular file");
    }
}

// Check the bounded ASCII format required for local slot path components.
bool valid_slot_id(std::string_view id) {
    // Recognize the ASCII letters and digits accepted in a path component.
    const auto alnum = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    if (id.empty() || id.size() > 64 || !alnum(id.front())) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [&](char c) { return alnum(c) || c == '-' || c == '_'; });
}

// Check the expected lowercase hexadecimal generation identifier.
bool valid_generation_id(std::string_view id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
    });
}

// Build the sequence and ID portion of a generation filename.
std::string generation_stem(const Generation &generation) {
    if (generation.sequence == 0 || !valid_generation_id(generation.id)) {
        throw SaveError("InvalidArgument", "Invalid generation identifier");
    }
    return std::to_string(generation.sequence) + "-" + generation.id;
}

// Extract a generation reference from a filename with the required prefix and suffix.
std::optional<Generation> parse_name(std::wstring_view name, std::wstring_view prefix, std::wstring_view suffix) {
    if (name.size() < prefix.size() + suffix.size() || name.substr(0, prefix.size()) != prefix ||
            name.substr(name.size() - suffix.size()) != suffix) {
        return std::nullopt;
    }
    name.remove_prefix(prefix.size());
    name.remove_suffix(suffix.size());
    const size_t separator = name.find(L'-');
    if (separator == std::wstring_view::npos || separator == 0 || name.front() == L'0' ||
            name.size() - separator - 1 != 32) {
        return std::nullopt;
    }

    uint64_t sequence = 0;
    for (wchar_t c : name.substr(0, separator)) {
        if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
        const uint64_t digit = c - L'0';
        if (sequence > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        sequence = sequence * 10 + digit;
    }

    std::string id;
    id.reserve(32);
    for (wchar_t c : name.substr(separator + 1)) {
        if (!((c >= L'a' && c <= L'f') || (c >= L'0' && c <= L'9'))) {
            return std::nullopt;
        }
        id.push_back(static_cast<char>(c));
    }
    return Generation{sequence, std::move(id)};
}

// Check for an existing local directory and reject invalid directory types.
bool check_directory(const Path &path) {
    const DWORD attributes = GetFileAttributesW(native_path(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (missing(error)) {
            return false;
        }
        fail_windows("Could not inspect save directory", error);
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw SaveError("IoError", "Save root, profile and slot must be ordinary directories");
    }
    return true;
}

// Create a directory when needed and verify that it is usable.
void ensure_directory(const Path &path) {
    if (check_directory(path)) {
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(Path(native_path(path)), error);
    if (error) {
        fail_windows("Could not create save directory", static_cast<DWORD>(error.value()));
    }
    if (!check_directory(path)) {
        throw SaveError("IoError", "The save directory disappeared while opening it");
    }
}

// Committed generations and optional abandoned temporary paths found in a slot.
struct Scan {
    // Committed generation references found during the scan.
    std::vector<Generation> generations;
    // Temporary checkpoint paths eligible for cleanup.
    std::vector<Path> abandoned_temporaries;
};

// Owns a Windows directory enumeration handle.
class FindHandle {
public:
    // Adopt the Windows handle for a directory search.
    explicit FindHandle(HANDLE handle) : handle_(handle) {}
    // Prevent sharing ownership of the directory search handle.
    FindHandle(const FindHandle &) = delete;
    // Prevent copying an owned directory search handle.
    FindHandle &operator=(const FindHandle &) = delete;
    // Release the Windows directory search handle.
    ~FindHandle() { FindClose(handle_); }

private:
    // Windows search handle owned by the wrapper.
    HANDLE handle_;
};

// Discover committed generations and optionally abandoned temporary files in a slot.
Scan scan_directory(const Path &directory, bool collect_temporaries = false) {
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW(native_path(directory / L"*").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            return {};
        }
        fail_windows("Could not enumerate save generations", error);
    }
    const FindHandle find_handle(search);

    Scan result;
    size_t entry_count = 0;
    do {
        if (++entry_count > kMaxDirectoryEntries) {
            throw SaveError("LimitExceeded", "Save slot contains too many directory entries");
        }
        std::wstring_view name(data.cFileName);
        if (auto generation = parse_name(name, L"g-", L".save")) {
            result.generations.push_back(std::move(*generation));
        } else if (collect_temporaries && parse_name(name, L"t-", L".tmp") &&
                !(data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            result.abandoned_temporaries.push_back(directory / name);
        }
    } while (FindNextFileW(search, &data));
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        fail_windows("Could not enumerate save generations", error);
    }

    std::sort(result.generations.begin(), result.generations.end(),
            [](const Generation &a, const Generation &b) { return a.sequence > b.sequence; });
    for (size_t i = 1; i < result.generations.size(); ++i) {
        if (result.generations[i - 1].sequence == result.generations[i].sequence) {
            throw SaveError("Conflict", "More than one finalized generation has the same sequence");
        }
    }
    return result;
}

// Remove a regular checkpoint file while tolerating a missing path.
void delete_regular_file(const Path &path) {
    const std::wstring name = native_path(path);
    const DWORD attributes = GetFileAttributesW(name.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (missing(error)) {
            return;
        }
        fail_windows("Could not inspect generation for cleanup", error);
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        throw SaveError("IoError", "A generation marked for cleanup must be a regular file");
    }
    if (!DeleteFileW(name.c_str())) {
        const DWORD error = GetLastError();
        if (!missing(error)) {
            fail_windows("Could not clean up save generation", error);
        }
    }
}

// Tracks cleanup ownership of a checkpoint staging path.
class TemporaryFile {
public:
    // Remember the staging filename before the file is created.
    explicit TemporaryFile(std::wstring name) : name_(std::move(name)) {}
    // Prevent two wrappers from cleaning up the same staging path.
    TemporaryFile(const TemporaryFile &) = delete;
    // Prevent copying a staging path's cleanup ownership.
    TemporaryFile &operator=(const TemporaryFile &) = delete;
    // Attempt to delete the staging file while cleanup ownership is active.
    ~TemporaryFile() {
        if (owned_) {
            DeleteFileW(name_.c_str());
        }
    }

    // Return the Windows pathname for opening or publishing the staging file.
    const wchar_t *name() const { return name_.c_str(); }
    // Enable cleanup after this request creates the staging file.
    void take_ownership() { owned_ = true; }
    // Disable cleanup after the staging file moves to its committed name.
    void release_ownership() { owned_ = false; }

private:
    // Windows pathname of the staging file.
    std::wstring name_;
    // Whether this wrapper owns cleanup of the staging file.
    bool owned_ = false;
};

// Open an existing regular checkpoint file for reading.
FileHandle open_readable_file(const Path &path) {
    // Omitting FILE_SHARE_DELETE protects this immutable generation while its handle is open.
    FileHandle file(CreateFileW(native_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        fail_windows("Could not open save generation for reading", GetLastError());
    }
    require_regular_file(file);
    return file;
}

// Return the checkpoint file length using the open handle.
uint64_t file_length(const FileHandle &file) {
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length)) {
        fail_windows("Could not get save generation size", GetLastError());
    }
    if (length.QuadPart < 0) {
        throw SaveError("Corrupt", "Save generation has an invalid file size");
    }
    return static_cast<uint64_t>(length.QuadPart);
}

// Fill the requested buffer and reject a premature end of file.
void read_exact(const FileHandle &file, uint8_t *data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const DWORD request = static_cast<DWORD>(std::min<size_t>(kIoChunk, length - offset));
        DWORD count = 0;
        if (!ReadFile(file.get(), data + offset, request, &count, nullptr)) {
            fail_windows("Could not read save generation", GetLastError());
        }
        if (count == 0) {
            throw SaveError("Corrupt", "Save generation ended before its recorded size");
        }
        offset += count;
    }
}

// Read a checkpoint file after checking its encoded byte budget.
Bytes read_bytes(const Path &path, size_t max_bytes) {
    const auto file = open_readable_file(path);
    const uint64_t length = file_length(file);
    if (length > max_bytes) {
        throw SaveError("LimitExceeded", "Save generation exceeds the read limit");
    }
    Bytes bytes(static_cast<size_t>(length));
    read_exact(file, bytes.data(), bytes.size());
    return bytes;
}

// Compare a persisted file to the expected encoded bytes.
bool matches_bytes(const Path &path, const Bytes &expected) {
    const auto file = open_readable_file(path);
    if (file_length(file) != expected.size()) {
        return false;
    }
    Bytes chunk(std::min<size_t>(64 * 1024, expected.size()));
    size_t offset = 0;
    while (offset < expected.size()) {
        const size_t count = std::min(chunk.size(), expected.size() - offset);
        read_exact(file, chunk.data(), count);
        if (std::memcmp(chunk.data(), expected.data() + offset, count) != 0) {
            return false;
        }
        offset += count;
    }
    return true;
}

// Create and flush a new staging file with the complete checkpoint bytes.
void write_bytes(TemporaryFile &temporary, const Bytes &bytes) {
    FileHandle file(CreateFileW(temporary.name(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        fail_windows("Could not open staging file for writing", GetLastError());
    }
    temporary.take_ownership();

    size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<size_t>(kIoChunk, bytes.size() - offset));
        DWORD count = 0;
        if (!WriteFile(file.get(), bytes.data() + offset, request, &count, nullptr)) {
            fail_windows("Could not write save generation", GetLastError());
        }
        if (count == 0) {
            throw SaveError("IoError", "Staging file stopped accepting data");
        }
        offset += count;
    }
    if (!FlushFileBuffers(file.get())) {
        fail_windows("Could not flush staging file", GetLastError());
    }
    file.close();
}

// Acquire the exclusive slot writer handle for the commit.
FileHandle acquire_writer(const Path &directory) {
    const Path lock_path = directory / L".writer.lock";
    HANDLE handle = CreateFileW(native_path(lock_path).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        fail_windows("Could not acquire save slot writer lease", GetLastError());
    }
    return FileHandle(handle);
}

// Windows checkpoint backend with generation publication and retention.
class WindowsLocalStore final : public Store {
public:
    // Normalize and validate the root of the local checkpoint store.
    explicit WindowsLocalStore(Path root) : root_(std::move(root).lexically_normal()) {
        root_.make_preferred();
        if (!root_.is_absolute()) {
            throw SaveError("InvalidArgument", "The local save root must be an absolute path");
        }
    }

    // Return committed generations discovered in the slot directory.
    std::vector<Generation> list(const std::string &profile, const std::string &slot) override {
        const auto directory = slot_directory(profile, slot, false);
        if (!directory) {
            return {};
        }
        return scan_directory(*directory).generations;
    }

    // Read the selected generation while enforcing the encoded size limit.
    Bytes read(const std::string &profile, const std::string &slot, const Generation &generation,
            size_t max_bytes) override {
        const std::string name = "g-" + generation_stem(generation) + ".save";
        const auto directory = slot_directory(profile, slot, false);
        if (!directory) {
            throw SaveError("NotFound", "Save slot does not exist");
        }
        return read_bytes(*directory / name, max_bytes);
    }

    // Stage, verify, and publish a generation while holding the slot writer handle.
    void publish(const std::string &profile, const std::string &slot, const Generation &generation,
            const std::optional<Generation> &expected_head, const Bytes &bytes,
            const std::function<bool()> &begin_commit) override {
        const std::string stem = generation_stem(generation);
        const Path directory = *slot_directory(profile, slot, true);
        const auto writer = acquire_writer(directory);
        require_regular_file(writer);
        const Scan scan = scan_directory(directory, true);
        if (scan.generations.empty() != !expected_head ||
                (expected_head && !(scan.generations.front() == *expected_head))) {
            throw SaveError("Conflict", "Save slot head changed before publication");
        }
        if (expected_head && generation.sequence <= expected_head->sequence) {
            throw SaveError("Conflict", "A new generation must follow the current slot head");
        }

        // Only a holder of the exclusive writer lease can own a recognized temporary file.
        for (const Path &abandoned : scan.abandoned_temporaries) {
            delete_regular_file(abandoned);
        }
        const Path staging = directory / ("t-" + stem + ".tmp");
        const Path final = directory / ("g-" + stem + ".save");
        TemporaryFile temporary(native_path(staging));
        write_bytes(temporary, bytes);
        if (!matches_bytes(staging, bytes)) {
            throw SaveError("IoError", "Staging file readback differed from the encoded generation");
        }

        const std::wstring target = native_path(final);
        if (!begin_commit()) {
            throw SaveError("Cancelled", "Save was cancelled before publication");
        }
        // No replacement or cross-volume copy is permitted. Earlier generations remain intact.
        if (!MoveFileExW(temporary.name(), target.c_str(), 0)) {
            fail_windows("Could not publish save generation", GetLastError());
        }
        temporary.release_ownership();

        try {
            if (!matches_bytes(final, bytes)) {
                throw SaveError("IoError", "Final readback differed from the staged generation");
            }
        } catch (const std::exception &) {
            throw SaveError("CommitUnknown", "The generation was published but final verification failed");
        }
    }

    // Remove generations outside the retained set after rechecking the slot head.
    void prune(const std::string &profile, const std::string &slot, const std::vector<Generation> &keep) override {
        const auto directory = slot_directory(profile, slot, false);
        if (!directory || keep.empty()) {
            return;
        }
        const auto writer = acquire_writer(*directory);
        require_regular_file(writer);
        const Scan scan = scan_directory(*directory);
        if (scan.generations.empty()) {
            return;
        }
        // Keep a generation only when its sequence and ID match a retained reference.
        const auto preserved = [&](const Generation &generation) {
            return std::any_of(keep.begin(), keep.end(), [&](const Generation &item) {
                return generation == item;
            });
        };
        if (!preserved(scan.generations.front())) {
            throw SaveError("Conflict", "Save slot head changed before cleanup");
        }
        for (const Generation &generation : scan.generations) {
            if (!preserved(generation)) {
                delete_regular_file(*directory / ("g-" + generation_stem(generation) + ".save"));
            }
        }
    }

private:
    // Resolve the validated profile and slot and optionally create their directories.
    std::optional<Path> slot_directory(const std::string &profile, const std::string &slot, bool create) const {
        if (!valid_slot_id(profile) || !valid_slot_id(slot)) {
            throw SaveError("InvalidArgument", "Profile and slot identifiers must be lowercase ASCII keys");
        }
        const Path profile_path = root_ / ("p-" + profile);
        const Path slot_path = profile_path / ("s-" + slot);
        if (create) {
            ensure_directory(root_);
            ensure_directory(profile_path);
            ensure_directory(slot_path);
        } else if (!check_directory(root_) || !check_directory(profile_path) || !check_directory(slot_path)) {
            return std::nullopt;
        }
        return slot_path;
    }

    // Normalized filesystem root for this store.
    Path root_;
};

} // namespace

std::shared_ptr<Store> make_local_store(const std::filesystem::path &root) {
    return std::make_shared<WindowsLocalStore>(root);
}

} // namespace checkpoint

#else

namespace checkpoint {

std::shared_ptr<Store> make_local_store(const std::filesystem::path &) {
    throw SaveError("UnsupportedPlatform", "The bundled local store currently supports Windows");
}

} // namespace checkpoint

#endif
