// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "save_core.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <random>
#include <unordered_set>
#include <utility>

namespace checkpoint {
namespace {

constexpr std::uint64_t format_version = 1;
constexpr std::size_t inspection_limit = 16;
constexpr std::size_t retention_scan_limit = 16;
constexpr std::size_t previous_generations_to_keep = 2;
constexpr std::size_t inspection_metadata_limit = 8 * 1024;

enum class ParticipantValidation { Registered, HistoricalScope, Structural };
enum class MissingGeneration { Report, Propagate };

// Validate routing, identity, versions, and participant records for the requested inspection mode.
void validate_envelope_fields(const Json &envelope, const SlotKey &key, const Generation &generation,
        const Versions &versions, ParticipantValidation validation);

// Read a required JSON object member and reject a malformed envelope.
const Json &field(const Json &object, const char *name) {
    if (!object.is_object()) {
        throw SaveError("Corrupt", "An envelope record must be an object.");
    }
    const auto found = object.find(name);
    if (found == object.end()) {
        throw SaveError("Corrupt", std::string("Missing envelope field: ") + name);
    }
    return *found;
}

// Read a required envelope member as a JSON string.
const std::string &string_field(const Json &object, const char *name) {
    const Json &value = field(object, name);
    if (!value.is_string()) {
        throw SaveError("Corrupt", std::string("Expected a string for: ") + name);
    }
    return value.get_ref<const std::string &>();
}

// Read a required positive schema version from the envelope.
std::uint64_t positive_version(const Json &object, const char *name) {
    const Json &value = field(object, name);
    if (value.is_number_unsigned()) {
        const auto version = value.get<std::uint64_t>();
        if (version != 0) {
            return version;
        }
    } else if (value.is_number_integer()) {
        const auto version = value.get<std::int64_t>();
        if (version > 0) {
            return static_cast<std::uint64_t>(version);
        }
    }
    throw SaveError("Corrupt", std::string("Expected a positive integer version for: ") + name);
}

// Generate a random lowercase hexadecimal generation identifier.
std::string new_generation_id() {
    std::array<unsigned char, 16> random_bytes{};
    try {
        std::random_device source;
        if (source.entropy() <= 0) {
            throw std::runtime_error("No nondeterministic entropy is reported.");
        }
        std::uniform_int_distribution<unsigned int> octet(0, 255);
        for (unsigned char &byte : random_bytes) {
            byte = static_cast<unsigned char>(octet(source));
        }
    } catch (const std::exception &) {
        throw SaveError("IoError", "Unable to generate a random generation ID.");
    }
    constexpr char hex_digits[] = "0123456789abcdef";
    std::string id;
    id.reserve(random_bytes.size() * 2);
    for (const unsigned char byte : random_bytes) {
        id.push_back(hex_digits[byte >> 4]);
        id.push_back(hex_digits[byte & 0x0f]);
    }
    return id;
}

// Encode a generation's exact sequence and identifier for a checkpoint result.
Json generation_json(const Generation &generation) {
    return {{"sequence", std::to_string(generation.sequence)}, {"id", generation.id}};
}

// Read and decode a generation, then validate its envelope for the slot.
Json read_validated(Store &store, Codec &codec, const SlotKey &key,
        const Generation &generation, const Versions &versions, const Limits &limits,
        ParticipantValidation validation = ParticipantValidation::HistoricalScope) {
    const Bytes bytes = store.read(key.profile, key.slot, generation, limits.max_file_bytes);
    Json envelope = codec.decode(bytes, limits);
    validate_envelope_fields(envelope, key, generation, versions, validation);
    return envelope;
}

// Send progress to the caller when it supplied a callback.
void report(const std::function<void(const std::string &)> &progress, const char *phase) {
    if (progress) {
        progress(phase);
    }
}

// Convert a persistence failure into the corresponding inspection status.
const char *inspection_status(const SaveError &error) {
    if (error.code == "Corrupt") {
        return "Corrupt";
    }
    if (error.code == "Incompatible") {
        return "Incompatible";
    }
    return "Unavailable";
}

// Keep the active head and protected or compatible backups before pruning the slot.
void retain_generations(Store &store, Codec &codec, const SlotKey &key,
        const Versions &versions, const Limits &limits, const Generation &published,
        const std::vector<Generation> &prior, const std::optional<Generation> &preserve,
        std::string &warning) {
    std::vector<Generation> keep{published};
    std::size_t compatible_kept = 0;
    std::size_t scanned = 0;
    for (const auto &generation : prior) {
        if (scanned == retention_scan_limit) {
            keep.push_back(generation);
            continue;
        }
        ++scanned;
        try {
            read_validated(store, codec, key, generation, versions, limits);
        } catch (const SaveError &) {
            // Keep evidence and intact files that this build cannot verify.
            keep.push_back(generation);
            continue;
        }
        const bool protected_source = preserve && generation == *preserve;
        if (protected_source || compatible_kept < previous_generations_to_keep) {
            keep.push_back(generation);
            ++compatible_kept;
        }
    }
    // The store rechecks the retained head under the writer lease before deleting.
    store.prune(key.profile, key.slot, keep);
    if (scanned < prior.size()) {
        warning = "Backup cleanup deferred for generations outside the inspection limit.";
    }
}

void validate_envelope_fields(const Json &envelope, const SlotKey &key,
        const Generation &generation, const Versions &versions, ParticipantValidation validation) {
    const auto saved_format = positive_version(envelope, "format_version");
    if (saved_format != format_version) {
        throw SaveError("Incompatible", "The save uses an unsupported envelope version.");
    }

    if (string_field(envelope, "app_id") != versions.app_id ||
            string_field(envelope, "profile_id") != key.profile ||
            string_field(envelope, "slot_id") != key.slot) {
        throw SaveError("Corrupt", "Save routing does not match its slot.");
    }
    const Json &identity = field(envelope, "generation");
    if (string_field(identity, "sequence") != std::to_string(generation.sequence) ||
            string_field(identity, "id") != generation.id) {
        throw SaveError("Corrupt", "Envelope generation does not match its filename.");
    }
    const auto saved_scope = positive_version(envelope, "scope_version");
    const auto current_scope = static_cast<std::uint64_t>(versions.scope_version);
    if (string_field(envelope, "scope_id") != versions.scope_id || saved_scope > current_scope) {
        throw SaveError("Incompatible", "The save requires a different checkpoint scope or version.");
    }
    const bool defer_registration = validation == ParticipantValidation::Structural ||
            (validation == ParticipantValidation::HistoricalScope && saved_scope < current_scope);
    if (!field(envelope, "meta").is_object()) {
        throw SaveError("Corrupt", "Save metadata must be an object.");
    }
    const Json &participants = field(envelope, "participants");
    if (!participants.is_array()) {
        throw SaveError("Corrupt", "Save participants must be an array.");
    }
    std::unordered_set<std::string> seen;
    for (const Json &participant : participants) {
        const auto &id = string_field(participant, "id");
        const auto saved_version = positive_version(participant, "version");
        if (id.empty() || !seen.insert(id).second) {
            throw SaveError("Corrupt", "Save participant IDs must be nonempty and unique.");
        }
        field(participant, "data");
        if (!defer_registration) {
            const auto current = versions.participants.find(id);
            if (current == versions.participants.end() ||
                    saved_version > static_cast<std::uint64_t>(current->second)) {
                throw SaveError("Incompatible", "The save requires unsupported participant data.");
            }
        }
    }
}

// Produce an inspection row with bounded metadata or a classified failure.
Json inspect_entry(Store &store, Codec &codec, const SlotKey &key, const Generation &generation,
        const Versions &versions, const Limits &limits, bool structural_only, MissingGeneration missing) {
    Json entry = generation_json(generation);
    try {
        const auto validation = structural_only ? ParticipantValidation::Structural : ParticipantValidation::HistoricalScope;
        Json envelope = read_validated(store, codec, key, generation, versions, limits, validation);
        entry["status"] = "Available";
        auto &metadata = envelope["meta"];
        if (metadata.dump().size() <= inspection_metadata_limit) {
            entry["meta"] = std::move(metadata);
        } else {
            entry["meta"] = Json::object();
            entry["meta_truncated"] = true;
        }
    } catch (const SaveError &error) {
        if (missing == MissingGeneration::Propagate && error.code == "NotFound") {
            throw;
        }
        entry["status"] = inspection_status(error);
        entry["code"] = error.code;
        entry["message"] = error.what();
    }
    return entry;
}

} // namespace

void validate_envelope(const Json &envelope, const SlotKey &key,
        const Generation &generation, const Versions &versions) {
    validate_envelope_fields(envelope, key, generation, versions, ParticipantValidation::Registered);
}

Generation write_checkpoint(Store &store, Codec &codec, const SlotKey &key, Json envelope,
        const Versions &versions, const Limits &limits, const std::function<bool()> &begin_commit,
        const std::function<void(const std::string &)> &progress,
        const std::optional<Generation> &preserve, std::string &warning) {
    warning.clear();
    report(progress, "Checking");
    const auto prior = store.list(key.profile, key.slot);
    std::optional<Generation> expected_head;
    if (!prior.empty()) {
        expected_head = prior.front();
        const bool proven_source = preserve && *expected_head == *preserve;
        const Json head = read_validated(store, codec, key, *expected_head, versions, limits);
        if (!proven_source && positive_version(head, "scope_version") <
                static_cast<std::uint64_t>(versions.scope_version)) {
            throw SaveError("Incompatible", "This slot contains an older checkpoint that needs validation before saving.",
                    {{"upgrade_source", generation_json(*expected_head)}});
        }
        if (expected_head->sequence == (std::numeric_limits<std::uint64_t>::max)()) {
            throw SaveError("LimitExceeded", "The slot exhausted its generation sequence.");
        }
    }

    const Generation generation{expected_head ? expected_head->sequence + 1 : 1, new_generation_id()};
    envelope["generation"] = generation_json(generation);
    validate_envelope(envelope, key, generation, versions);
    if (positive_version(envelope, "scope_version") != static_cast<std::uint64_t>(versions.scope_version)) {
        throw SaveError("InvalidData", "New checkpoints must use the current scope version.");
    }

    report(progress, "Encoding");
    const Bytes bytes = codec.encode(envelope, limits);
    report(progress, "Writing");
    try {
        store.publish(key.profile, key.slot, generation, expected_head, bytes, begin_commit);
    } catch (const SaveError &error) {
        if (error.code != "CommitUnknown") {
            throw;
        }
        throw SaveError(error.code, error.what(), generation_json(generation));
    }

    // Retention is deliberately outside the commit outcome.
    try {
        retain_generations(store, codec, key, versions, limits, generation, prior, preserve, warning);
    } catch (const std::exception &error) {
        warning = std::string("Checkpoint published. Backup cleanup failed: ") + error.what();
    }
    return generation;
}

Json read_checkpoint(Store &store, Codec &codec, const SlotKey &key,
        const std::optional<Generation> &selection, const Versions &versions, const Limits &limits,
        const std::function<void(const std::string &)> &progress) {
    report(progress, "Reading");
    auto generation = selection;
    if (!generation) {
        const auto generations = store.list(key.profile, key.slot);
        if (generations.empty()) {
            throw SaveError("NotFound", "The slot has no committed checkpoint.");
        }
        generation = generations.front();
    }
    const Bytes bytes = store.read(key.profile, key.slot, *generation, limits.max_file_bytes);
    report(progress, "Verifying");
    Json envelope = codec.decode(bytes, limits);
    validate_envelope_fields(envelope, key, *generation, versions, ParticipantValidation::HistoricalScope);
    return envelope;
}

Json inspect_slot(Store &store, Codec &codec, const SlotKey &key,
        const Versions &versions, const Limits &limits, const std::optional<Generation> &before,
        size_t limit, bool structural_only) {
    if (limit == 0 || (before && before->sequence == 0)) {
        throw SaveError("InvalidArgument", "Inspection requires a positive page size and a valid cursor.");
    }

    const auto generations = store.list(key.profile, key.slot);
    const auto first = before ? std::find_if(generations.begin(), generations.end(), [&](const Generation &item) {
        return item.sequence < before->sequence;
    }) : generations.begin();

    const size_t page_size = std::min(limit, inspection_limit);
    Json entries = Json::array();
    auto item = first;
    for (; item != generations.end() && entries.size() < page_size; ++item) {
        entries.push_back(inspect_entry(store, codec, key, *item, versions, limits,
                structural_only, MissingGeneration::Report));
    }

    const bool truncated = item != generations.end();
    Json next = truncated ? generation_json(*std::prev(item)) : Json(nullptr);
    Json status = "Empty";
    if (!generations.empty()) {
        if (!entries.empty() && first == generations.begin()) {
            status = entries.front().at("status");
        } else {
            status = inspect_entry(store, codec, key, generations.front(), versions, limits,
                    structural_only, MissingGeneration::Report).at("status");
        }
    }
    return {{"status", std::move(status)}, {"generations", std::move(entries)},
        {"truncated", truncated}, {"next", std::move(next)}};
}

Json inspect_generation(Store &store, Codec &codec, const SlotKey &key, const Generation &generation,
        const Versions &versions, const Limits &limits, bool structural_only) {
    return inspect_entry(store, codec, key, generation, versions, limits,
            structural_only, MissingGeneration::Propagate);
}

} // namespace checkpoint
