// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace checkpoint {

using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;

// Persistence failure with a stable code and optional structured details.
class SaveError : public std::runtime_error {
public:
    // Machine-readable error code for the checkpoint result.
    std::string code;
    // Optional data such as the generation to inspect after a failed operation.
    Json details;

    // Store the machine-readable error code, diagnostic, and details.
    SaveError(std::string error_code, std::string message, Json error_details = nullptr) :
            std::runtime_error(std::move(message)), code(std::move(error_code)),
            details(std::move(error_details)) {}
};

// Budgets shared by snapshot conversion, storage, and codecs.
struct Limits {
    // Maximum encoded checkpoint size in bytes.
    size_t max_file_bytes = 8 * 1024 * 1024;
    // Maximum nesting depth in detached snapshot data.
    size_t max_depth = 48;
    // Maximum number of values in a detached snapshot.
    size_t max_nodes = 200000;
    // Maximum UTF-8 size of a single string or object key.
    size_t max_string_bytes = 1024 * 1024;
};

// Identity of one committed save within a slot.
struct Generation {
    // Monotonically increasing position of the generation within its slot.
    uint64_t sequence = 0;
    // Random identifier that distinguishes a generation at a given sequence.
    std::string id;

    // Compare both the sequence and identifier of two generations.
    bool operator==(const Generation &other) const {
        return sequence == other.sequence && id == other.id;
    }
};

// Profile and slot pair used to route checkpoint work.
struct SlotKey {
    // Profile that owns the save slot.
    std::string profile;
    // Slot identifier within the profile.
    std::string slot;

    // Compare profile and slot identity.
    bool operator==(const SlotKey &other) const {
        return profile == other.profile && slot == other.slot;
    }

    // Order slots by profile and then by slot identifier.
    bool operator<(const SlotKey &other) const {
        return profile == other.profile ? slot < other.slot : profile < other.profile;
    }
};

// App, scope, and participant versions accepted by the host.
struct Versions {
    // Stable application identifier used to validate checkpoint ownership.
    std::string app_id;
    // Stable identifier of the checkpoint scope.
    std::string scope_id;
    // Schema version of the participant collection.
    int scope_version = 1;
    // Schema version indexed by registered participant identifier.
    std::map<std::string, int> participants;
};

// Interface for encoding and decoding detached checkpoint data.
class Codec {
public:
    // Allow cleanup through a codec interface pointer.
    virtual ~Codec() = default;
    // Encode checkpoint data under the supplied resource limits.
    virtual Bytes encode(const Json &data, const Limits &limits) = 0;
    // Decode and validate checkpoint bytes under the supplied limits.
    virtual Json decode(const Bytes &bytes, const Limits &limits) = 0;
};

// Interface for generation discovery, storage, publication, and retention.
class Store {
public:
    // Allow cleanup through a store interface pointer.
    virtual ~Store() = default;
    // List a slot's committed generations in descending sequence order.
    virtual std::vector<Generation> list(const std::string &profile, const std::string &slot) = 0;
    // Read an exact committed generation subject to the byte budget.
    virtual Bytes read(const std::string &profile, const std::string &slot,
            const Generation &generation, size_t max_bytes) = 0;
    // Publish a generation after checking the expected head and receiving commit authorization.
    virtual void publish(const std::string &profile, const std::string &slot,
            const Generation &generation, const std::optional<Generation> &expected_head,
            const Bytes &bytes, const std::function<bool()> &begin_commit) = 0;
    // Remove committed generations outside the retained set when the head still matches.
    virtual void prune(const std::string &profile, const std::string &slot,
            const std::vector<Generation> &keep) = 0;
};

// Create the built-in checkpoint store for the specified local root.
std::shared_ptr<Store> make_local_store(const std::filesystem::path &root);
// Create the built-in framed JSON checkpoint codec.
std::shared_ptr<Codec> make_json_codec();
// Validate values against the checkpoint's JSON shape and resource budgets.
void validate_json(const Json &value, const Limits &limits);

// Validate checkpoint routing, generation identity, and registered participant schemas.
void validate_envelope(const Json &envelope, const SlotKey &key,
        const Generation &generation, const Versions &versions);
// Encode and publish a new checkpoint generation, then apply retention.
Generation write_checkpoint(Store &store, Codec &codec, const SlotKey &key, Json envelope,
        const Versions &versions, const Limits &limits, const std::function<bool()> &begin_commit,
        const std::function<void(const std::string &)> &progress,
        const std::optional<Generation> &preserve, std::string &warning);
// Read, decode, and validate the selected generation or current slot head.
Json read_checkpoint(Store &store, Codec &codec, const SlotKey &key,
        const std::optional<Generation> &selection, const Versions &versions, const Limits &limits,
        const std::function<void(const std::string &)> &progress);
// Return a bounded page of checkpoint metadata and availability information.
Json inspect_slot(Store &store, Codec &codec, const SlotKey &key,
        const Versions &versions, const Limits &limits,
        const std::optional<Generation> &before = std::nullopt, size_t limit = 16,
        bool structural_only = false);
// Inspect one exact generation and propagate a missing-generation result.
Json inspect_generation(Store &store, Codec &codec, const SlotKey &key, const Generation &generation,
        const Versions &versions, const Limits &limits, bool structural_only = false);

} // namespace checkpoint
