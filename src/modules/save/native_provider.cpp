// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "native_provider.hpp"

#ifdef _WIN32
#include "json_codec_payload.hpp"
#include "sdk/include/godot_save_provider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>

namespace checkpoint {
namespace {

constexpr std::size_t max_provider_generations = 4096;
constexpr std::array<std::uint8_t, 4> gsav_magic{'G', 'S', 'A', 'V'};

// Check that provider text is valid UTF-8 and within the Windows API size range.
bool valid_utf8(std::string_view value) {
    if (value.empty()) {
        return true;
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0) != 0;
}

// Map a provider ABI status to the corresponding checkpoint result code.
const char *status_code(gs_status_v1 status) {
    switch (status) {
        case GS_CANCELLED: return "Cancelled";
        case GS_NOT_FOUND: return "NotFound";
        case GS_BUSY: return "Busy";
        case GS_CONFLICT: return "Conflict";
        case GS_CORRUPT: return "Corrupt";
        case GS_INCOMPATIBLE: return "Incompatible";
        case GS_LIMIT_EXCEEDED: return "LimitExceeded";
        case GS_INVALID_ARGUMENT: return "InvalidArgument";
        case GS_OUT_OF_SPACE: return "OutOfSpace";
        case GS_PERMISSION_DENIED: return "PermissionDenied";
        case GS_IO_ERROR: return "IoError";
        case GS_COMMIT_UNKNOWN: return "CommitUnknown";
        case GS_INVALID_DATA: return "InvalidData";
        case GS_PROVIDER_ERROR: return "ProviderError";
        default: return nullptr;
    }
}

// Raise a checkpoint error when an ABI operation reports failure.
void require_ok(gs_status_v1 status, const gs_error_v1 &error, const char *operation) {
    if (status == GS_OK) {
        return;
    }
    const char *code = status_code(status);
    if (!code) {
        throw SaveError("ProviderError", "The native provider returned an unknown status.");
    }
    const char *end = std::find(std::begin(error.message), std::end(error.message), '\0');
    const std::string_view message(error.message, static_cast<std::size_t>(end - error.message));
    if (!message.empty() && end != std::end(error.message) && valid_utf8(message)) {
        throw SaveError(code, std::string(operation) + ": " + std::string(message));
    }
    throw SaveError(code, std::string(operation) + " failed.");
}

// Invoke a provider callback and translate any exception that crosses the ABI boundary.
template<class Function, class... Arguments>
gs_status_v1 call_provider(Function function, Arguments &&...arguments) {
    try {
        return function(std::forward<Arguments>(arguments)...);
    } catch (...) {
        throw SaveError("ProviderError", "A native provider callback threw an exception.");
    }
}

// Borrow host-owned UTF-8 text for one ABI call.
gs_string_v1 view(const std::string &value) {
    return {value.data(), value.size()};
}

// Borrow host-owned checkpoint bytes for one ABI call.
gs_bytes_v1 view(const Bytes &value) {
    return {value.data(), value.size()};
}

// Borrow profile and slot names as one ABI slot reference.
gs_slot_v1 slot_view(const std::string &profile, const std::string &slot) {
    return {view(profile), view(slot)};
}

// Check the expected lowercase hexadecimal generation identifier.
bool valid_generation_id(std::string_view id) {
    return id.size() == GS_GENERATION_ID_SIZE && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// Convert a host generation reference to the provider ABI layout.
gs_generation_v1 abi_generation(const Generation &generation) {
    if (generation.sequence == 0 || !valid_generation_id(generation.id)) {
        throw SaveError("InvalidArgument", "Invalid native provider generation reference.");
    }
    gs_generation_v1 output{generation.sequence, {}};
    std::memcpy(output.id, generation.id.data(), generation.id.size());
    return output;
}

// Owns a loaded DLL and its provider callback table.
struct LoadedProvider {
    // Windows module handle kept alive while provider services are in use.
    HMODULE module = nullptr;
    // Provider capability flags, callbacks, and owned context.
    gs_provider_v1 api{};

    // Destroy provider-owned state before unloading the DLL.
    ~LoadedProvider() {
        if (api.destroy) {
            try {
                api.destroy(api.context);
            } catch (...) {
                // A provider destructor must never escape during host shutdown.
            }
        }
        if (module) {
            FreeLibrary(module);
        }
    }
};

// Accumulates provider output and enforces its byte budget.
struct ByteCollector {
    // Initialize a collector with the maximum accepted output size.
    explicit ByteCollector(std::size_t limit) : max_bytes(limit) {}

    // Append a provider chunk or record the first collection failure.
    static gs_status_v1 GS_CALL write(void *context, gs_bytes_v1 chunk) noexcept {
        auto &collector = *static_cast<ByteCollector *>(context);
        if (collector.failure != GS_OK) {
            return collector.failure;
        }
        if (chunk.size > collector.max_bytes - collector.bytes.size()) {
            collector.failure = GS_LIMIT_EXCEEDED;
        } else if (chunk.size != 0) {
            if (!chunk.data) {
                collector.failure = GS_PROVIDER_ERROR;
                return collector.failure;
            }
            try {
                collector.bytes.insert(collector.bytes.end(), chunk.data,
                        chunk.data + static_cast<std::size_t>(chunk.size));
            } catch (...) {
                collector.failure = GS_LIMIT_EXCEEDED;
            }
        }
        return collector.failure;
    }

    // Return the ABI sink bound to this collector for one provider call.
    gs_byte_sink_v1 sink() { return {this, write}; }

    // Maximum aggregate size of the provider's byte output.
    std::size_t max_bytes;
    // Chunks collected into a contiguous byte buffer.
    Bytes bytes;
    // First sink failure returned to subsequent provider writes.
    gs_status_v1 failure = GS_OK;
};

// Collects validated generation records emitted by a provider.
struct GenerationCollector {
    // Validate and append a provider generation under the listing limit.
    static gs_status_v1 GS_CALL emit(void *context, const gs_generation_v1 *generation) noexcept {
        auto &collector = *static_cast<GenerationCollector *>(context);
        if (collector.failure != GS_OK) {
            return collector.failure;
        }
        if (collector.generations.size() >= max_provider_generations) {
            collector.failure = GS_LIMIT_EXCEEDED;
            return collector.failure;
        }
        if (!generation || generation->sequence == 0 ||
                !valid_generation_id(std::string_view(generation->id, sizeof(generation->id)))) {
            collector.failure = GS_PROVIDER_ERROR;
            return collector.failure;
        }
        try {
            collector.generations.push_back({
                generation->sequence, std::string(generation->id, sizeof(generation->id))});
        } catch (...) {
            collector.failure = GS_LIMIT_EXCEEDED;
        }
        return collector.failure;
    }

    // Validated generation records collected from the provider.
    std::vector<Generation> generations;
    // First failure seen while collecting generation records.
    gs_status_v1 failure = GS_OK;
};

// Tracks the provider's one opportunity to request commit authorization.
struct CommitGate {
    // Borrow the host's commit authorization callback for one ABI call.
    explicit CommitGate(const std::function<bool()> &begin) : begin_commit(begin) {}

    // Authorize the provider's first commit request and detect repeated calls.
    static gs_status_v1 GS_CALL enter(void *context) noexcept {
        auto &gate = *static_cast<CommitGate *>(context);
        if (gate.called) {
            gate.violated = true;
            return GS_PROVIDER_ERROR;
        }
        gate.called = true;
        try {
            gate.authorized = gate.begin_commit();
            return gate.authorized ? GS_OK : GS_CANCELLED;
        } catch (...) {
            gate.violated = true;
            return GS_PROVIDER_ERROR;
        }
    }

    // Host callback that decides whether publication can proceed.
    const std::function<bool()> &begin_commit;
    // Whether the provider has invoked the gate.
    bool called = false;
    // Whether the host approved the provider's commit.
    bool authorized = false;
    // Whether the provider called the gate incorrectly or the callback threw.
    bool violated = false;
};

// Check for the built-in GSAV frame signature.
bool has_gsav_magic(const Bytes &bytes) {
    return bytes.size() >= gsav_magic.size() &&
            std::equal(gsav_magic.begin(), gsav_magic.end(), bytes.begin());
}

// Check whether the GSAV bytes are reserved for built-in decoding.
bool reserves_builtin_codec(const Bytes &bytes) {
    return has_gsav_magic(bytes) && (bytes.size() < 8 || (bytes[6] == 1 && bytes[7] == 0));
}

// Adapter exposing a provider's codec capability through the host codec interface.
class NativeCodec final : public Codec {
public:
    // Retain the provider and prepare the built-in fallback codec.
    explicit NativeCodec(std::shared_ptr<LoadedProvider> provider) :
            provider_(std::move(provider)), builtin_(make_json_codec()) {}

    // Serialize checkpoint JSON and ask the provider to encode it.
    Bytes encode(const Json &data, const Limits &limits) override {
        const Bytes json = encode_json_payload(data, limits);
        Bytes bytes = invoke(provider_->api.codec.encode, json, limits, "Native codec encode");
        if (bytes.empty() || reserves_builtin_codec(bytes)) {
            throw SaveError("ProviderError", "The native codec emitted empty bytes or used the reserved built-in codec.");
        }
        return bytes;
    }

    // Decode supported GSAV frames or recover JSON through the provider.
    Json decode(const Bytes &bytes, const Limits &limits) override {
        if (bytes.size() > limits.max_file_bytes) {
            throw SaveError("LimitExceeded", "The save file exceeds the file size limit.");
        }
        if (has_gsav_magic(bytes)) {
            try {
                return builtin_->decode(bytes, limits);
            } catch (const SaveError &error) {
                // Check the common GSAV frame before asking a custom codec to decode it.
                if (error.code != "Incompatible" || reserves_builtin_codec(bytes)) {
                    throw;
                }
            }
        }
        const Bytes json = invoke(provider_->api.codec.decode, bytes, limits, "Native codec decode");
        return decode_json_payload(json, limits);
    }

private:
    // Call one codec operation and collect its bounded byte output.
    Bytes invoke(gs_codec_operation_v1 operation, const Bytes &input,
            const Limits &limits, const char *description) {
        const gs_limits_v1 abi_limits{
            limits.max_file_bytes, limits.max_depth, limits.max_nodes, limits.max_string_bytes};
        ByteCollector collector(limits.max_file_bytes);
        gs_error_v1 error{};
        const auto status = call_provider(operation, provider_->api.context, view(input),
                &abi_limits, collector.sink(), &error);
        require_ok(collector.failure, {}, "Native codec output");
        require_ok(status, error, description);
        return std::move(collector.bytes);
    }

    // Shared owner that keeps the provider DLL and context alive.
    std::shared_ptr<LoadedProvider> provider_;
    // Built-in codec for decoding compatible GSAV frames.
    std::shared_ptr<Codec> builtin_;
};

// Adapter exposing a provider's storage capability through the host store interface.
class NativeStore final : public Store {
public:
    // Keep the provider loaded for the lifetime of the store.
    explicit NativeStore(std::shared_ptr<LoadedProvider> provider) :
            provider_(std::move(provider)) {}

    // Collect and validate the committed generations reported by the provider.
    std::vector<Generation> list(const std::string &profile, const std::string &slot) override {
        const auto key = slot_view(profile, slot);
        GenerationCollector collector;
        const gs_generation_sink_v1 sink{&collector, GenerationCollector::emit};
        gs_error_v1 error{};
        const auto status = call_provider(provider_->api.store.list, provider_->api.context, &key, sink, &error);
        require_ok(collector.failure, {}, "Native store generation output");
        require_ok(status, error, "Native store list");
        auto &generations = collector.generations;
        std::sort(generations.begin(), generations.end(), [](const Generation &left, const Generation &right) {
            return left.sequence > right.sequence;
        });
        if (std::adjacent_find(generations.begin(), generations.end(), [](const Generation &left, const Generation &right) {
                return left.sequence == right.sequence;
            }) != generations.end()) {
            throw SaveError("Conflict", "The native store returned duplicate generation sequences.");
        }
        return std::move(generations);
    }

    // Read the exact generation through a bounded provider byte sink.
    Bytes read(const std::string &profile, const std::string &slot,
            const Generation &generation, std::size_t max_bytes) override {
        const auto key = slot_view(profile, slot);
        const auto reference = abi_generation(generation);
        ByteCollector collector(max_bytes);
        gs_error_v1 error{};
        const auto status = call_provider(provider_->api.store.read, provider_->api.context, &key,
                &reference, max_bytes, collector.sink(), &error);
        require_ok(collector.failure, {}, "Native store read output");
        require_ok(status, error, "Native store read");
        return std::move(collector.bytes);
    }

    // Ask the provider to publish using the expected head and host commit gate.
    void publish(const std::string &profile, const std::string &slot, const Generation &generation,
            const std::optional<Generation> &expected_head, const Bytes &bytes,
            const std::function<bool()> &begin_commit) override {
        const auto key = slot_view(profile, slot);
        const auto reference = abi_generation(generation);
        const auto expected = expected_head ? std::optional(abi_generation(*expected_head)) : std::nullopt;
        CommitGate gate(begin_commit);
        const gs_commit_gate_v1 abi_gate{&gate, CommitGate::enter};
        gs_error_v1 error{};
        gs_status_v1 status = GS_PROVIDER_ERROR;
        try {
            status = provider_->api.store.publish(provider_->api.context, &key, &reference,
                    expected ? &*expected : nullptr, view(bytes), abi_gate, &error);
        } catch (...) {
            throw SaveError(gate.authorized ? "CommitUnknown" : "ProviderError",
                    "The native store threw during publication.");
        }

        if ((!gate.authorized && status == GS_OK) ||
                (gate.authorized && (gate.violated || status == GS_CANCELLED ||
                        (status != GS_OK && !status_code(status))))) {
            throw SaveError("CommitUnknown", "The native store violated the publication gate contract.");
        }
        if (gate.violated) {
            throw SaveError("ProviderError", "The native store invoked the publication gate incorrectly.");
        }
        require_ok(status, error, "Native store publish");
    }

    // Pass the retained generation set to the provider's cleanup callback.
    void prune(const std::string &profile, const std::string &slot,
            const std::vector<Generation> &keep) override {
        if (keep.size() > max_provider_generations) {
            throw SaveError("LimitExceeded", "The native store retention list exceeds its limit.");
        }
        const auto key = slot_view(profile, slot);
        std::vector<gs_generation_v1> preserved;
        preserved.reserve(keep.size());
        for (const auto &generation : keep) {
            preserved.push_back(abi_generation(generation));
        }
        gs_error_v1 error{};
        require_ok(call_provider(provider_->api.store.prune, provider_->api.context, &key,
                preserved.data(), preserved.size(), &error), error, "Native store prune");
    }

private:
    // Shared owner that keeps the provider DLL and context alive.
    std::shared_ptr<LoadedProvider> provider_;
};

// Verify the provider ABI version, structure size, capabilities, and callback table.
void validate_api(const gs_provider_v1 &api) {
    constexpr uint32_t supported = GS_CAP_CODEC | GS_CAP_STORE;
    if (api.abi_version != GS_PROVIDER_ABI_V1 || api.struct_size != sizeof(gs_provider_v1) || api.reserved != 0) {
        throw SaveError("ProviderIncompatible", "The native provider reported an incompatible ABI.");
    }
    if (!api.capabilities || (api.capabilities & ~supported) || (api.context && !api.destroy)) {
        throw SaveError("ProviderIncompatible", "The native provider reported an invalid capability table.");
    }
    if ((api.capabilities & GS_CAP_CODEC) && (!api.codec.encode || !api.codec.decode)) {
        throw SaveError("ProviderIncompatible", "The native provider codec table is incomplete.");
    }
    if ((api.capabilities & GS_CAP_STORE) &&
            (!api.store.list || !api.store.read || !api.store.publish || !api.store.prune)) {
        throw SaveError("ProviderIncompatible", "The native provider store table is incomplete.");
    }
}

} // namespace

NativeProviderServices load_native_provider(const std::filesystem::path &path, const std::string &configuration) {
    if (configuration.size() > GS_MAX_CONFIGURATION_BYTES) {
        throw SaveError("LimitExceeded", "Native provider configuration exceeds 64 KiB.");
    }
    if (configuration.find('\0') != std::string::npos || !valid_utf8(configuration)) {
        throw SaveError("InvalidArgument", "Native provider configuration must be valid UTF-8.");
    }
    if (!path.is_absolute() ||
            CompareStringOrdinal(path.extension().c_str(), -1, L".dll", -1, TRUE) != CSTR_EQUAL) {
        throw SaveError("InvalidArgument", "Select the absolute path to a native provider DLL.");
    }
    auto native_path = path.lexically_normal();
    native_path.make_preferred();
    const DWORD attributes = GetFileAttributesW(native_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        throw SaveError("ProviderLoadFailed", "The selected native provider DLL could not be opened.");
    }

    auto provider = std::make_shared<LoadedProvider>();
    provider->module = LoadLibraryExW(native_path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!provider->module) {
        throw SaveError("ProviderLoadFailed", "Windows could not load the native provider DLL (error " +
                std::to_string(GetLastError()) + ").");
    }

    // Windows documents casting the address returned by GetProcAddress to its export signature.
#pragma warning(push)
#pragma warning(disable : 4191)
    const auto entry = reinterpret_cast<gs_provider_entry_v1>(
            GetProcAddress(provider->module, GS_PROVIDER_ENTRY));
#pragma warning(pop)
    if (!entry) {
        throw SaveError("ProviderIncompatible", "The DLL does not export godot_save_provider_v1.");
    }

    gs_provider_v1 api{};
    api.struct_size = sizeof(gs_provider_v1);
    api.abi_version = GS_PROVIDER_ABI_V1;
    gs_error_v1 error{};
    require_ok(call_provider(entry, view(configuration), &api, &error), error, "Native provider initialization");
    provider->api = api;
    validate_api(api);

    NativeProviderServices services;
    if (api.capabilities & GS_CAP_CODEC) {
        services.codec = std::make_shared<NativeCodec>(provider);
    }
    if (api.capabilities & GS_CAP_STORE) {
        services.store = std::make_shared<NativeStore>(provider);
    }
    return services;
}

} // namespace checkpoint

#else

namespace checkpoint {

NativeProviderServices load_native_provider(const std::filesystem::path &, const std::string &) {
    throw SaveError("UnsupportedPlatform", "Native providers are supported by the Windows build.");
}

} // namespace checkpoint

#endif
