// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "godot_save_provider.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t header_size = 16;
constexpr std::size_t digest_size = 32;
constexpr std::size_t frame_size = header_size + digest_size;
constexpr std::uint16_t codec_id = 0x8001;
constexpr std::array<std::uint8_t, 4> magic{'G', 'S', 'A', 'V'};
constexpr std::size_t max_slots = 64;
constexpr std::size_t max_generations = 4096;
constexpr std::size_t max_store_bytes = 64 * 1024 * 1024;

// Fill the optional ABI error buffer and return its status code.
gs_status_v1 fail(gs_error_v1 *error, gs_status_v1 code, const char *message) noexcept {
    if (error) {
        std::snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return code;
}

// Translate C++ exceptions into provider statuses before crossing the C ABI.
template<class Function>
gs_status_v1 guarded(gs_error_v1 *error, Function &&execute) noexcept {
    try {
        return execute();
    } catch (const std::bad_alloc &) {
        return fail(error, GS_LIMIT_EXCEEDED, "Example provider ran out of memory.");
    } catch (...) {
        return fail(error, GS_PROVIDER_ERROR, "Example provider callback failed.");
    }
}

// Read a little-endian integer from the frame.
template<class Integer>
Integer read_le(const std::uint8_t *bytes, std::size_t offset) {
    Integer value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value = static_cast<Integer>(value |
                (static_cast<Integer>(bytes[offset + index]) << (8 * index)));
    }
    return value;
}

// Write a little-endian integer to the frame.
template<class Integer>
void write_le(std::uint8_t *bytes, std::size_t offset, Integer value) {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (8 * index));
    }
}

// Compute the SHA-256 digest of the supplied bytes.
bool sha256(const std::uint8_t *data, std::size_t size, std::array<std::uint8_t, digest_size> &digest) {
    return size <= std::numeric_limits<ULONG>::max() &&
            BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data),
                    static_cast<ULONG>(size), digest.data(), static_cast<ULONG>(digest.size())) >= 0;
}

// Check the byte views, limits, and output sink required by the codec.
bool valid_codec_arguments(gs_bytes_v1 input, const gs_limits_v1 *limits, gs_byte_sink_v1 output) {
    return input.data && input.size != 0 && limits && output.write;
}

// Write the example provider frame and payload digest to the host sink.
gs_status_v1 GS_CALL encode(void *, gs_bytes_v1 input, const gs_limits_v1 *limits,
        gs_byte_sink_v1 output, gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!valid_codec_arguments(input, limits, output)) {
            return GS_INVALID_ARGUMENT;
        }
        if (limits->max_file_bytes < frame_size || input.size > limits->max_file_bytes ||
                input.size > std::numeric_limits<std::size_t>::max()) {
            return GS_LIMIT_EXCEEDED;
        }

        std::vector<std::uint8_t> bytes(header_size);
        std::copy(magic.begin(), magic.end(), bytes.begin());
        write_le(bytes.data(), 4, std::uint16_t{1});
        write_le(bytes.data(), 6, codec_id);
        const std::size_t input_size = static_cast<std::size_t>(input.size);
        for (std::size_t offset = 0; offset < input_size;) {
            const std::uint8_t value = input.data[offset];
            std::size_t count = 1;
            while (count < 255 && count < input_size - offset && input.data[offset + count] == value) {
                ++count;
            }
            if (limits->max_file_bytes - digest_size - bytes.size() < 2) {
                return fail(error, GS_LIMIT_EXCEEDED, "The run-length frame exceeds the file budget.");
            }
            bytes.push_back(static_cast<std::uint8_t>(count));
            bytes.push_back(value);
            offset += count;
        }
        write_le(bytes.data(), 8, static_cast<std::uint64_t>(bytes.size() - header_size));
        std::array<std::uint8_t, digest_size> digest{};
        if (!sha256(bytes.data(), bytes.size(), digest)) {
            return GS_IO_ERROR;
        }
        bytes.insert(bytes.end(), digest.begin(), digest.end());
        return output.write(output.context, {bytes.data(), bytes.size()});
    });
}

// Verify the example provider frame and send the recovered payload to the host sink.
gs_status_v1 GS_CALL decode(void *, gs_bytes_v1 input, const gs_limits_v1 *limits,
        gs_byte_sink_v1 output, gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!valid_codec_arguments(input, limits, output)) {
            return GS_INVALID_ARGUMENT;
        }
        if (input.size > limits->max_file_bytes || input.size > std::numeric_limits<std::size_t>::max()) {
            return GS_LIMIT_EXCEEDED;
        }
        const auto size = static_cast<std::size_t>(input.size);
        if (size < frame_size || !std::equal(magic.begin(), magic.end(), input.data) ||
                read_le<std::uint64_t>(input.data, 8) != size - frame_size) {
            return GS_CORRUPT;
        }
        const std::size_t digest_offset = size - digest_size;
        std::array<std::uint8_t, digest_size> digest{};
        if (!sha256(input.data, digest_offset, digest)) {
            return GS_IO_ERROR;
        }
        if (!std::equal(digest.begin(), digest.end(), input.data + digest_offset)) {
            return GS_CORRUPT;
        }
        if (read_le<std::uint16_t>(input.data, 4) != 1 ||
                read_le<std::uint16_t>(input.data, 6) != codec_id) {
            return GS_INCOMPATIBLE;
        }
        if ((digest_offset - header_size) % 2 != 0) {
            return GS_CORRUPT;
        }

        std::vector<std::uint8_t> json;
        for (std::size_t index = header_size; index < digest_offset; index += 2) {
            const std::size_t count = input.data[index];
            if (count == 0) {
                return GS_CORRUPT;
            }
            if (count > limits->max_file_bytes - json.size()) {
                return GS_LIMIT_EXCEEDED;
            }
            json.insert(json.end(), count, input.data[index + 1]);
        }
        return output.write(output.context, {json.data(), json.size()});
    });
}

using Slot = std::pair<std::string, std::string>;

// Generation metadata and encoded bytes stored by the in-memory provider.
struct Record {
    // ABI identity of the stored generation.
    gs_generation_v1 generation;
    // Encoded checkpoint bytes owned by the stored generation.
    std::vector<std::uint8_t> bytes;
};

using Generations = std::map<std::uint64_t, Record>;

// In-memory store state protected by the provider mutex.
struct MemoryStore {
    // Serializes operations on generation maps and the total byte budget.
    std::mutex mutex;
    // Stored generations grouped by profile and slot.
    std::map<Slot, Generations> slots;
    // Aggregate number of bytes retained by the provider.
    std::size_t total_bytes = 0;
};

// Check whether a slot character is a lowercase ASCII letter or decimal digit.
bool alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

// Validate one bounded profile or slot name received through the ABI.
bool valid_slot_part(gs_string_v1 part) {
    return part.data && part.size >= 1 && part.size <= 64 && alnum(part.data[0]) &&
            std::all_of(part.data, part.data + part.size, [](char c) { return alnum(c) || c == '-' || c == '_'; });
}

// Validate both components of the ABI slot reference.
bool valid_slot(const gs_slot_v1 *slot) {
    return slot && valid_slot_part(slot->profile) && valid_slot_part(slot->slot);
}

// Copy borrowed slot components into an owning key for the memory store.
Slot slot_key(const gs_slot_v1 &slot) {
    return {std::string(slot.profile.data, static_cast<std::size_t>(slot.profile.size)),
            std::string(slot.slot.data, static_cast<std::size_t>(slot.slot.size))};
}

// Check the supplied generation pointer, sequence, and hexadecimal ID.
bool valid_generation(const gs_generation_v1 *generation) {
    return generation && generation->sequence != 0 &&
            std::all_of(std::begin(generation->id), std::end(generation->id),
                    [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Compare both fields of two ABI generation references.
bool same(const gs_generation_v1 &a, const gs_generation_v1 &b) {
    return a.sequence == b.sequence && std::memcmp(a.id, b.id, sizeof(a.id)) == 0;
}

// Emit the slot's committed generations through the host callback.
gs_status_v1 GS_CALL list(void *context, const gs_slot_v1 *slot,
        gs_generation_sink_v1 output, gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!context || !valid_slot(slot) || !output.emit) {
            return GS_INVALID_ARGUMENT;
        }
        auto &store = *static_cast<MemoryStore *>(context);
        const auto key = slot_key(*slot);
        std::lock_guard<std::mutex> lock(store.mutex);
        const auto found = store.slots.find(key);
        if (found == store.slots.end()) {
            return GS_OK;
        }
        for (auto entry = found->second.rbegin(); entry != found->second.rend(); ++entry) {
            const auto status = output.emit(output.context, &entry->second.generation);
            if (status != GS_OK) {
                return status;
            }
        }
        return GS_OK;
    });
}

// Send one exact generation's bytes to the host within its budget.
gs_status_v1 GS_CALL read(void *context, const gs_slot_v1 *slot, const gs_generation_v1 *generation,
        uint64_t max_bytes, gs_byte_sink_v1 output, gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!context || !valid_slot(slot) || !valid_generation(generation) || !output.write) {
            return GS_INVALID_ARGUMENT;
        }
        auto &store = *static_cast<MemoryStore *>(context);
        const auto key = slot_key(*slot);
        std::lock_guard<std::mutex> lock(store.mutex);
        const auto found = store.slots.find(key);
        if (found == store.slots.end()) {
            return GS_NOT_FOUND;
        }
        const auto record = found->second.find(generation->sequence);
        if (record == found->second.end() || !same(record->second.generation, *generation)) {
            return GS_NOT_FOUND;
        }
        const auto &bytes = record->second.bytes;
        if (bytes.size() > max_bytes) {
            return GS_LIMIT_EXCEEDED;
        }
        return output.write(output.context, {bytes.data(), bytes.size()});
    });
}

// Check the expected slot head and publish a generation after host authorization.
gs_status_v1 GS_CALL publish(void *context, const gs_slot_v1 *slot, const gs_generation_v1 *generation,
        const gs_generation_v1 *expected_head, gs_bytes_v1 bytes, gs_commit_gate_v1 gate,
        gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!context || !valid_slot(slot) || !valid_generation(generation) ||
                (expected_head && !valid_generation(expected_head)) || !bytes.data ||
                bytes.size == 0 || !gate.begin) {
            return GS_INVALID_ARGUMENT;
        }
        auto &store = *static_cast<MemoryStore *>(context);
        const auto key = slot_key(*slot);
        std::lock_guard<std::mutex> lock(store.mutex);

        const auto found = store.slots.find(key);
        const bool has_head = found != store.slots.end() && !found->second.empty();
        if (has_head != (expected_head != nullptr) ||
                (has_head && (!same(found->second.rbegin()->second.generation, *expected_head) ||
                        generation->sequence <= expected_head->sequence))) {
            return GS_CONFLICT;
        }
        if (bytes.size > max_store_bytes - store.total_bytes ||
                (found == store.slots.end() && store.slots.size() >= max_slots) ||
                (found != store.slots.end() && found->second.size() >= max_generations)) {
            return GS_LIMIT_EXCEEDED;
        }

        Generations staged;
        staged.emplace(generation->sequence, Record{*generation,
                std::vector<std::uint8_t>(bytes.data, bytes.data + bytes.size)});
        auto staged_record = staged.extract(staged.begin());
        const auto [target, created] = store.slots.try_emplace(key);
        const auto status = gate.begin(gate.context);
        if (status != GS_OK) {
            if (created) {
                store.slots.erase(target);
            }
            return status;
        }
        // All allocations precede the gate. Insertion transfers the prepared map node.
        target->second.insert(std::move(staged_record));
        store.total_bytes += static_cast<std::size_t>(bytes.size);
        return GS_OK;
    });
}

// Remove unretained generations while the slot head still matches.
gs_status_v1 GS_CALL prune(void *context, const gs_slot_v1 *slot, const gs_generation_v1 *keep,
        uint64_t keep_count, gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!context || !valid_slot(slot) || (keep_count != 0 && !keep) || keep_count > max_generations) {
            return GS_INVALID_ARGUMENT;
        }
        if (keep_count == 0) {
            return GS_OK;
        }
        auto &store = *static_cast<MemoryStore *>(context);
        const auto key = slot_key(*slot);
        std::lock_guard<std::mutex> lock(store.mutex);
        const auto found = store.slots.find(key);
        if (found == store.slots.end() || found->second.empty()) {
            return GS_OK;
        }
        // Match the retained sequence and ID before keeping a generation.
        auto preserved = [&](const gs_generation_v1 &generation) {
            return std::any_of(keep, keep + keep_count, [&](const gs_generation_v1 &reference) {
                return same(reference, generation);
            });
        };
        auto &records = found->second;
        if (!preserved(records.rbegin()->second.generation)) {
            return GS_CONFLICT;
        }
        for (auto entry = records.begin(); entry != records.end();) {
            if (preserved(entry->second.generation)) {
                ++entry;
            } else {
                store.total_bytes -= entry->second.bytes.size();
                entry = records.erase(entry);
            }
        }
        return GS_OK;
    });
}

// Release the example provider's in-memory store context.
void GS_CALL destroy(void *context) noexcept {
    delete static_cast<MemoryStore *>(context);
}

} // namespace

// Initialize the example provider's capabilities, callbacks, and owned context.
extern "C" GS_PROVIDER_EXPORT gs_status_v1 GS_CALL godot_save_provider_v1(gs_string_v1 configuration,
        gs_provider_v1 *provider, gs_error_v1 *error) noexcept {
    return guarded(error, [&]() -> gs_status_v1 {
        if (!provider || provider->abi_version != GS_PROVIDER_ABI_V1 ||
                provider->struct_size != sizeof(gs_provider_v1)) {
            return fail(error, GS_INCOMPATIBLE, "Example provider requires ABI version 1.");
        }
        if (configuration.size > GS_MAX_CONFIGURATION_BYTES ||
                (configuration.size != 0 && !configuration.data)) {
            return GS_INVALID_ARGUMENT;
        }
        const std::string_view mode(configuration.data ? configuration.data : "",
                static_cast<std::size_t>(configuration.size));
        std::uint32_t capabilities = GS_CAP_CODEC | GS_CAP_STORE;
        if (mode == "codec") {
            capabilities = GS_CAP_CODEC;
        } else if (mode == "store") {
            capabilities = GS_CAP_STORE;
        } else if (!mode.empty() && mode != "both") {
            return fail(error, GS_INVALID_ARGUMENT, "Use codec, store, both, or empty configuration.");
        }

        if (capabilities & GS_CAP_CODEC) {
            provider->codec = {encode, decode};
        }
        if (capabilities & GS_CAP_STORE) {
            provider->context = new MemoryStore;
            provider->destroy = destroy;
            provider->store = {list, read, publish, prune};
        }
        provider->capabilities = capabilities;
        return GS_OK;
    });
}
