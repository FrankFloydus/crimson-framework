// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "save_coordinator.hpp"

#include "godot_dto.hpp"
#include "native_provider.hpp"
#include "save_worker.hpp"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <charconv>
#include <deque>
#include <limits>
#include <utility>

namespace checkpoint {

using namespace godot;

namespace {

constexpr size_t max_pending_jobs = 3;
constexpr size_t max_events = 24;
constexpr size_t max_participants = 64;
constexpr size_t max_migrations = 256;
constexpr size_t max_metadata_bytes = 8192;
constexpr const char *invalid_slot_message =
        "Profile and slot IDs must start with a lowercase letter or digit and use up to 64 lowercase letters, digits, underscores, or hyphens.";

// Schema and live adapter binding for one checkpoint participant.
struct Participant {
    // Godot object ID of the currently bound adapter.
    uint64_t adapter = 0;
    // Target schema version for the participant payload.
    int version = 1;
    // Whether a restore must supply a payload for this participant.
    bool required = true;
    // Upgrade callbacks indexed by the version they accept.
    std::map<int, Callable> migrations;
};

// Queued coordinator request that has yet to reach the worker.
struct Pending {
    // Detached job passed to the worker after admission and capture.
    Job job;
    // Whether the request uses autosave metadata and supersession rules.
    bool autosave = false;
    // Whether the queued save has a complete detached snapshot.
    bool captured = false;
    // World epoch that admitted the request.
    uint64_t epoch = 0;
};

// Coordinator-side tracking for one dispatched job.
struct Active {
    // Ticket shared by the worker job and coordinator events.
    int64_t ticket = 0;
    // Operation kind used for event routing and fencing.
    Kind kind = Kind::Save;
    // Profile and slot targeted by the active operation.
    SlotKey key;
    // World epoch checked before activating a restored candidate.
    uint64_t epoch = 0;
    // Cancellation and commit state shared with the worker.
    std::shared_ptr<OperationControl> control;
    // Explicit generation reference or page cursor for the active operation.
    std::optional<Generation> selection;
    // Whether a staged load waits for explicit activation.
    bool prepare_only = false;
};

// Detached load candidate held until activation or disposal.
struct PreparedRestore {
    // Staged gameplay data or detached Godot object owned by the coordinator.
    Variant candidate;
    // Godot object ID of the host that owns the candidate's lifecycle.
    uint64_t host = 0;
    // Worker outcome carrying the validated generation reference.
    Outcome result;
};

// Sets the coordinator's callback fence for the current scope.
class CallbackGuard {
public:
    // Mark that a host or adapter callback is in progress.
    explicit CallbackGuard(bool &inside) : flag(inside) { flag = true; }
    // Clear the callback fence when the scope exits.
    ~CallbackGuard() { flag = false; }

private:
    // Coordinator flag that protects host and adapter callback scopes.
    bool &flag;
};

// Check the bounded identifier format accepted by coordinator routing.
bool valid_id(const std::string &id, bool namespaced = false) {
    // Every identifier begins with a lowercase ASCII letter or digit.
    auto base = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    return !id.empty() && id.size() <= 64 && base(id.front()) &&
            std::all_of(id.begin(), id.end(), [&](char c) {
                return base(c) || c == '-' || c == '_' || (namespaced && c == '.');
            });
}

// Count nested values for the aggregate upgraded payload budget.
size_t json_nodes(const Json &value) {
    size_t count = 1;
    if (value.is_structured()) {
        for (const Json &child : value) {
            count += json_nodes(child);
        }
    }
    return count;
}

// Check that the schema version fits the supported positive integer range.
bool valid_version(int64_t version) {
    return version >= 1 && version <= std::numeric_limits<int>::max();
}

// Validate the sequence and ID strings of an explicit generation reference.
Generation parse_generation(const std::string &sequence, const std::string &id) {
    uint64_t number = 0;
    const auto parsed = std::from_chars(sequence.data(), sequence.data() + sequence.size(), number);
    const bool valid_hex = id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
    if (parsed.ec != std::errc{} || parsed.ptr != sequence.data() + sequence.size() ||
            number == 0 || std::to_string(number) != sequence || !valid_hex) {
        throw SaveError("InvalidArgument", "Select a generation by its sequence and ID.");
    }
    return {number, id};
}

// Resolve a required Godot object or raise the supplied failure code.
Object *live_object(uint64_t id, const char *code, const std::string &message) {
    Object *object = id ? ObjectDB::get_instance(id) : nullptr;
    if (object == nullptr) {
        throw SaveError(code, message);
    }
    return object;
}

// Check that an adapter exposes capture and staging callbacks.
bool valid_adapter(Object *adapter) {
    return adapter && adapter->has_method("save_capture") && adapter->has_method("save_stage");
}

// Preserve explicit callback failure detail while keeping the coordinator's action and code.
std::string callback_failure(const Dictionary &result, const std::string &message) {
    const Variant ok = result.get("ok", Variant());
    const Variant detail = result.get("message", Variant());
    if (ok.get_type() == Variant::BOOL && !static_cast<bool>(ok) && detail.get_type() == Variant::STRING) {
        const String explanation = String(detail).strip_edges();
        if (!explanation.is_empty()) {
            return message + " " + utf8(explanation.left(512));
        }
    }
    return message;
}

// Extract a successful callback's value or raise its operation failure.
Variant result_value(const Variant &result, const char *code, const std::string &message) {
    if (result.get_type() == Variant::DICTIONARY) {
        const Dictionary value = result;
        const Variant ok = value.get("ok", Variant());
        if (ok.get_type() == Variant::BOOL && static_cast<bool>(ok) && value.has("value")) {
            return value["value"];
        }
        throw SaveError(code, callback_failure(value, message));
    }
    throw SaveError(code, message);
}

// Require explicit success from a restore callback, with optional structured failure detail.
void require_true(const Variant &value, const std::string &message) {
    if (value.get_type() == Variant::BOOL && static_cast<bool>(value)) {
        return;
    }
    if (value.get_type() == Variant::DICTIONARY) {
        throw SaveError("RestoreFailed", callback_failure(Dictionary(value), message));
    }
    throw SaveError("RestoreFailed", message);
}

// Convert a worker outcome to the Godot result shape for its operation kind.
Dictionary result_dictionary(const Outcome &outcome, Kind kind) {
    Dictionary result;
    const bool ok = outcome.code == "Ok";
    result["ok"] = ok;
    result["code"] = godot_string(outcome.code);
    if (!outcome.message.empty()) {
        result["message"] = godot_string(outcome.message);
    }
    if (!outcome.warning.empty()) {
        result["warning"] = godot_string(outcome.warning);
    }
    if (ok && kind == Kind::List) {
        result["value"] = to_variant(outcome.value.at("generations"));
        result["status"] = to_variant(outcome.value.at("status"));
        result["truncated"] = to_variant(outcome.value.at("truncated"));
        result["next"] = to_variant(outcome.value.at("next"));
    } else if (!outcome.value.is_null()) {
        result["value"] = to_variant(outcome.value);
    }
    return result;
}

} // namespace

// Owns the coordinator's queues, schema bindings, and restore state.
struct SaveCoordinator::Impl {
    // Thread allowed to configure the coordinator and call Godot adapters.
    const std::thread::id owner = std::this_thread::get_id();
    // Storage backend copied into the worker at initialization.
    std::shared_ptr<Store> store;
    // Checkpoint codec copied into the worker at initialization.
    std::shared_ptr<Codec> codec;
    // Lazily initialized worker owned by this coordinator.
    std::unique_ptr<SaveWorker> worker;
    // Application, scope, and participant versions accepted by this host.
    Versions versions;
    // Resource limits applied to checkpoint conversion and I/O.
    Limits limits;
    // Godot object ID of the current restore host.
    uint64_t host = 0;
    // Current world revision used to reject stale restore publication.
    uint64_t epoch = 1;
    // Next positive request ticket assigned by the coordinator.
    int64_t next_ticket = 1;
    // Whether an admitted gameplay operation has frozen schema configuration.
    bool schemas_frozen = false;
    // Whether a host or participant callback currently fences reentrant changes.
    bool in_callback = false;
    // Terminal notifications still pending in the current delivery pass.
    size_t pending_delivery = 0;
    // Whether an admitted load is preventing new gameplay operations.
    bool load_fence = false;
    // Machine-readable reason for the most recent rejected request.
    std::string admission_error;
    // Human-readable reason retained for native orchestrators.
    std::string admission_message;
    // Declared participants and their current live bindings.
    std::map<std::string, Participant> participants;
    // Whole-scope upgrade callbacks indexed by their source versions.
    std::map<int, Callable> scope_migrations;
    // Total registered scope and participant upgrade steps.
    size_t migration_count = 0;
    // Previously validated generation per slot protected during retention.
    std::map<SlotKey, Generation> protected_generations;
    // Slots awaiting inspection to settle an ambiguous publication.
    std::map<SlotKey, Generation> unresolved_slots;
    // Slot whose explicitly restored historical generation constrains subsequent saves.
    std::optional<SlotKey> recovered_slot;
    // Admitted requests waiting for capture or worker dispatch.
    std::deque<Pending> queue;
    // Coordinator-side state of the currently dispatched request.
    std::optional<Active> active;
    // Worker load completion held until progress listeners receive their events.
    std::optional<Event> deferred_load;
    // Staged restore candidate waiting for activation or cancellation.
    std::optional<PreparedRestore> prepared;
    // Whether the prepared load notification still needs delivery.
    bool prepared_notice = false;
    // Queued ticket and phase pairs for the next signal delivery pass.
    std::vector<std::pair<int64_t, std::string>> progress;
    // Terminal outcome awaiting delivery to a Godot listener.
    struct Completion {
        // Request ticket that receives the terminal result.
        int64_t ticket;
        // Operation kind used to choose the result shape.
        Kind kind;
        // Detached result converted before signal delivery.
        Outcome outcome;
    };
    // Terminal outcomes buffered for main-thread notification.
    std::vector<Completion> completions;

    // Attempt to cancel active work before shutting down the worker.
    ~Impl() {
        if (active) {
            active->control->cancel();
        }
        worker.reset();
    }

    // Check whether the caller is on the coordinator's owning thread.
    bool on_main() const { return owner == std::this_thread::get_id(); }
    // Check whether storage and codec configuration can still change.
    bool mutable_services() const { return on_main() && !worker && !in_callback; }
    // Check whether participant schemas and migrations can still change.
    bool mutable_schema() const { return on_main() && !schemas_frozen && !in_callback; }
    // Count admitted requests that are queued or active.
    size_t count() const { return queue.size() + (active.has_value() ? 1 : 0); }

    // Check that gameplay callbacks and admitted work allow rebinding.
    bool mutable_bindings() const {
        return on_main() && !in_callback && (!active || !requires_world(active->kind)) &&
                std::none_of(queue.begin(), queue.end(), [](const Pending &pending) {
                    return requires_world(pending.job.kind);
                });
    }

    // Check that a host and all declared adapters still exist.
    bool bound_world() const {
        return host && ObjectDB::get_instance(host) && !participants.empty() &&
                std::all_of(participants.begin(), participants.end(), [](const auto &entry) {
                    return entry.second.adapter && ObjectDB::get_instance(entry.second.adapter);
                });
    }

    // Remember the admission error and return an invalid ticket.
    int64_t reject(const std::string &code, const std::string &message) {
        admission_error = code;
        admission_message = message;
        return 0;
    }

    // Report a synchronous setup rejection, which has no ticket or terminal signal.
    bool reject_setup(const char *method, const std::string &code, const std::string &message) {
        if (on_main()) {
            reject(code, message);
        }
        UtilityFunctions::printerr("[Crimson] SaveCoordinator.", method, ": ", godot_string(message));
        return false;
    }

    // Check configuration, callback fences, and queue limits before admission.
    bool can_admit(Kind kind) {
        if (in_callback) {
            reject("Busy", "A checkpoint callback is still running. Submit the request after it returns.");
            return false;
        }
        if (load_fence && requires_world(kind)) {
            reject("Busy", "A checkpoint load is already pending. Wait for it to finish before retrying.");
            return false;
        }
        if (!store || !codec || versions.app_id.empty() || versions.scope_id.empty() ||
                (requires_world(kind) && !bound_world())) {
            reject("Unconfigured", "The checkpoint coordinator is missing its storage configuration or live checkpoint bindings.");
            return false;
        }
        if (completions.size() + progress.size() >= max_events) {
            reject("Busy", "The checkpoint event queue is full. Wait for pending operations to finish.");
            return false;
        }
        if (next_ticket == std::numeric_limits<int64_t>::max()) {
            reject("Busy", "The checkpoint coordinator has exhausted its operation tickets. Start a new game instance.");
            return false;
        }
        return true;
    }

    // Create a ticket, snapshot request configuration, and enqueue the job.
    int64_t admit(Pending pending) {
        if (!worker) {
            try {
                worker = std::make_unique<SaveWorker>(store, codec);
            } catch (const std::exception &) {
                return reject("WorkerUnavailable", "The checkpoint coordinator could not start its storage worker.");
            }
        }
        const int64_t ticket = next_ticket++;
        pending.job.ticket = ticket;
        pending.job.control = std::make_shared<OperationControl>();
        pending.job.versions = versions;
        pending.job.limits = limits;
        pending.job.structural_only = !requires_world(pending.job.kind) && participants.empty();
        pending.epoch = epoch;
        if (requires_world(pending.job.kind)) {
            schemas_frozen = true;
        }
        queue.push_back(std::move(pending));
        admission_error.clear();
        admission_message.clear();
        progress.emplace_back(ticket, "Queued");
        return ticket;
    }

    // Queue the result of an undispatched request and release any load fence.
    void finish_pending(const Pending &pending, const std::string &code, const std::string &message) {
        completions.push_back({pending.job.ticket, pending.job.kind, {nullptr, code, message, {}}});
        if (pending.job.kind == Kind::Load) {
            load_fence = false;
        }
    }

    // Send the queue head to the worker once its capture and slot checks pass.
    void dispatch() {
        if (active || queue.empty()) {
            return;
        }
        Pending &head = queue.front();
        if (head.job.kind == Kind::Save && !head.captured) {
            return;
        }
        if (head.job.kind == Kind::Save && unresolved_slots.count(head.job.key)) {
            finish_pending(head, "CommitUnknown", "Inspect the slot before attempting another save.");
            queue.pop_front();
            return;
        }
        auto preserve = protected_generations.find(head.job.key);
        if (preserve != protected_generations.end()) {
            head.job.preserve = preserve->second;
        }
        active = Active{head.job.ticket, head.job.kind, head.job.key, head.epoch, head.job.control,
            head.job.selection, head.job.prepare_only};
        Job job = std::move(head.job);
        queue.pop_front();
        worker->submit(std::move(job));
    }

    // Capture all participant payloads at one host revision into detached JSON.
    Json capture(const Pending &pending, const std::string &revision) {
        Json meta = pending.job.envelope;
        meta["kind"] = pending.autosave ? "auto" : "manual";
        meta["capture_revision"] = revision;
        if (meta.dump().size() > max_metadata_bytes) {
            throw SaveError("LimitExceeded", "Save metadata exceeds its byte budget.");
        }
        size_t remaining = limits.max_file_bytes - 1024 - meta.dump().size();
        Json records = Json::array();
        for (const auto &[id, registration] : participants) {
            Object *adapter = live_object(registration.adapter, "CaptureFailed",
                    "The capture adapter for participant " + id + " is no longer available.");
            const Variant data = result_value(adapter->call("save_capture"), "CaptureFailed",
                    "Capture failed for participant " + id + ".");
            if (remaining <= 256) {
                throw SaveError("LimitExceeded", "The snapshot exceeds its byte budget.");
            }
            Limits participant_limits = limits;
            participant_limits.max_file_bytes = remaining - 256;
            Json record = {{"id", id}, {"version", registration.version},
                {"data", to_json(data, participant_limits)}};
            const size_t size = record.dump().size();
            if (size + 1 > remaining) {
                throw SaveError("LimitExceeded", "The snapshot exceeds its byte budget.");
            }
            remaining -= size + 1;
            records.push_back(std::move(record));
        }
        Json envelope = {
            {"format_version", 1}, {"app_id", versions.app_id}, {"profile_id", pending.job.key.profile},
            {"slot_id", pending.job.key.slot}, {"scope_id", versions.scope_id},
            {"scope_version", versions.scope_version}, {"meta", std::move(meta)},
            {"participants", std::move(records)}
        };
        validate_json(envelope, limits);
        return envelope;
    }

    // Apply registered scope upgrades in version order.
    Json migrate_scope(const Json &envelope, const Active &operation) const {
        int version = envelope.at("scope_version").get<int>();
        Json records = envelope.at("participants");
        while (version < versions.scope_version) {
            operation.control->check_cancelled();
            auto it = scope_migrations.find(version);
            if (it == scope_migrations.end() || !it->second.is_valid()) {
                throw SaveError("Incompatible", "The scope migration from version " + std::to_string(version) + " is unavailable.");
            }
            const Variant value = result_value(it->second.call(to_variant(records)), "MigrationFailed",
                    "The scope migration rejected this checkpoint.");
            records = to_json(value, limits);
            ++version;
        }
        return records;
    }

    // Validate participant presence and upgrade detached payloads for staging.
    std::map<std::string, Json> prepare_payloads(const Json &envelope, const Active &operation) const {
        Json upgraded = {
            {"format_version", 1}, {"app_id", versions.app_id}, {"profile_id", operation.key.profile},
            {"slot_id", operation.key.slot}, {"scope_id", versions.scope_id},
            {"scope_version", versions.scope_version}, {"generation", envelope.at("generation")},
            {"meta", envelope.at("meta")}, {"participants", migrate_scope(envelope, operation)}
        };
        const Json &generation = upgraded.at("generation");
        validate_envelope(upgraded, operation.key, parse_generation(
                generation.at("sequence").get<std::string>(), generation.at("id").get<std::string>()), versions);

        std::map<std::string, const Json *> records;
        for (const Json &record : upgraded.at("participants")) {
            records.emplace(record.at("id").get<std::string>(), &record);
        }

        std::map<std::string, Json> payloads;
        size_t remaining = limits.max_file_bytes;
        size_t remaining_nodes = limits.max_nodes;
        // Charge the upgraded payload against the shared budget before staging it.
        auto retain_payload = [&](const std::string &id, Json value) {
            const size_t size = value.dump().size();
            const size_t nodes = json_nodes(value);
            if (size > remaining || nodes > remaining_nodes) {
                throw SaveError("LimitExceeded", "The upgraded participant data exceeds its aggregate budget.");
            }
            remaining -= size;
            remaining_nodes -= nodes;
            payloads.emplace(id, std::move(value));
        };
        for (const auto &[id, registration] : participants) {
            operation.control->check_cancelled();
            auto record = records.find(id);
            if (record == records.end()) {
                if (registration.required) {
                    throw SaveError("MissingParticipant", "Required participant data is missing for " + id + ".");
                }
                Object *adapter = live_object(registration.adapter, "RestoreFailed",
                        "The restore adapter for participant " + id + " is no longer available.");
                if (!adapter->has_method("save_default")) {
                    throw SaveError("MissingParticipant", "The optional participant has no default for " + id + ".");
                }
                const Variant value = result_value(adapter->call("save_default"), "RestoreFailed",
                        "The default failed for participant " + id + ".");
                Limits participant_limits = limits;
                participant_limits.max_file_bytes = remaining;
                participant_limits.max_nodes = remaining_nodes;
                retain_payload(id, to_json(value, participant_limits));
                continue;
            }
            Json value = record->second->at("data");
            int version = record->second->at("version").get<int>();
            Limits participant_limits = limits;
            participant_limits.max_file_bytes = remaining;
            participant_limits.max_nodes = remaining_nodes;
            while (version < registration.version) {
                auto migration = registration.migrations.find(version);
                if (migration == registration.migrations.end() || !migration->second.is_valid()) {
                    throw SaveError("Incompatible", "The migration from version " + std::to_string(version) +
                            " is unavailable for participant " + id + ".");
                }
                const Variant migrated = result_value(migration->second.call(to_variant(value)), "MigrationFailed",
                        "The migration rejected participant " + id + ".");
                value = to_json(migrated, participant_limits);
                operation.control->check_cancelled();
                ++version;
            }
            retain_payload(id, std::move(value));
        }
        return payloads;
    }

    // Ask the host to release a candidate or schedule a detached node for cleanup.
    void discard_candidate(const Variant &candidate, uint64_t owner_id) {
        Object *host_object = owner_id ? ObjectDB::get_instance(owner_id) : nullptr;
        if (host_object && host_object->has_method("save_discard_candidate")) {
            host_object->call("save_discard_candidate", candidate);
            return;
        }
        if (candidate.get_type() == Variant::OBJECT) {
            Object *object = candidate;
            Node *node = Object::cast_to<Node>(object);
            if (node && !node->get_parent() && !node->is_inside_tree() && !node->is_queued_for_deletion()) {
                node->queue_free();
            }
        }
    }

    // Stage and link upgraded checkpoint data into a detached candidate.
    bool prepare_restore(Outcome &outcome, const Active &operation) {
        CallbackGuard guard(in_callback);
        Variant candidate;
        bool owns_candidate = false;
        try {
            operation.control->check_cancelled();
            if (operation.epoch != epoch) {
                throw SaveError("StaleWorld", "The active world changed before restore.");
            }
            progress.emplace_back(operation.ticket, "Migrating");
            auto payloads = prepare_payloads(outcome.value, operation);
            progress.emplace_back(operation.ticket, "Staging");
            Object *host_object = live_object(host, "RestoreFailed", "The restore host is no longer available to create a candidate.");
            candidate = result_value(host_object->call("save_create_candidate"), "RestoreFailed",
                    "The host could not create a detached candidate.");
            owns_candidate = true;
            if (candidate.get_type() == Variant::OBJECT) {
                Object *object = candidate;
                Node *node = Object::cast_to<Node>(object);
                if (!object || (node && (node->get_parent() || node->is_inside_tree() || node->is_queued_for_deletion()))) {
                    throw SaveError("RestoreFailed", "The host must return a live, detached candidate.");
                }
            }
            for (const auto &[id, registration] : participants) {
                Object *adapter = live_object(registration.adapter, "RestoreFailed",
                        "The staging adapter for participant " + id + " is no longer available.");
                require_true(adapter->call("save_stage", to_variant(payloads.at(id)), candidate),
                        "Staging failed for participant " + id + ".");
            }
            for (const auto &[id, registration] : participants) {
                Object *adapter = live_object(registration.adapter, "RestoreFailed",
                        "The linking adapter for participant " + id + " is no longer available.");
                if (adapter->has_method("save_link")) {
                    require_true(adapter->call("save_link", candidate), "Linking failed for participant " + id + ".");
                }
            }
            Json generation = outcome.value.at("generation");
            outcome.value.swap(generation);
            prepared.emplace(PreparedRestore{candidate, host, std::move(outcome)});
            return true;
        } catch (const SaveError &error) {
            outcome = {nullptr, error.code, error.what(), {}};
        } catch (const std::exception &) {
            outcome = {nullptr, "RestoreFailed", "The checkpoint could not be prepared.", {}};
        }
        if (owns_candidate) {
            discard_candidate(candidate, host);
        }
        return false;
    }

    // Complete the active request, release its load fence, and queue the outcome.
    void finish_active(Outcome outcome) {
        active->control->finish();
        if (active->kind == Kind::Load) {
            load_fence = false;
        }
        completions.push_back({active->ticket, active->kind, std::move(outcome)});
        active.reset();
    }

    // Discard a prepared restore and finish its ticket with the supplied failure.
    void abort_prepared(const char *code, const char *message) {
        CallbackGuard guard(in_callback);
        discard_candidate(prepared->candidate, prepared->host);
        prepared.reset();
        prepared_notice = false;
        finish_active({nullptr, code, message, {}});
    }

    // Validate a staged candidate, then publish it or preserve the validated slot head.
    bool finish_prepared(bool publish, String *diagnostic = nullptr) {
        CallbackGuard guard(in_callback);
        Outcome outcome = std::move(prepared->result);
        try {
            active->control->check_cancelled();
            if (active->epoch != epoch || prepared->host != host) {
                throw SaveError("StaleWorld", "The active world changed before publication.");
            }
            if (!publish && outcome.historical) {
                throw SaveError("Incompatible", "The selected checkpoint is no longer the slot head.");
            }
            Object *host_object = live_object(prepared->host, "RestoreFailed", "The restore host is no longer available to validate the candidate.");
            if (prepared->candidate.get_type() == Variant::OBJECT) {
                Object *object = prepared->candidate;
                Node *node = Object::cast_to<Node>(object);
                if (!object || (node && (node->get_parent() || node->is_inside_tree() || node->is_queued_for_deletion()))) {
                    throw SaveError("RestoreFailed", "The staged candidate is no longer detached and available.");
                }
            }
            if (host_object->has_method("save_validate_candidate")) {
                require_true(host_object->call("save_validate_candidate", prepared->candidate),
                        "The host rejected the staged checkpoint.");
            }
            const Generation reference = parse_generation(outcome.value.at("sequence").get<std::string>(),
                    outcome.value.at("id").get<std::string>());
            std::map<SlotKey, Generation> staged_pin;
            staged_pin.emplace(active->key, reference);
            std::optional<SlotKey> staged_recovery;
            if (outcome.historical) {
                staged_recovery = active->key;
            }
            progress.emplace_back(active->ticket, publish ? "Activating" : "Validating");
            if (!active->control->begin_commit()) {
                throw SaveError("Cancelled", "The load was cancelled.");
            }
            if (publish) {
                host_object = live_object(prepared->host, "RestoreFailed", "The restore host is no longer available to publish the candidate.");
                require_true(host_object->call("save_publish", prepared->candidate), "The host rejected checkpoint publication.");
                ++epoch;
                recovered_slot.swap(staged_recovery);
            }
            protected_generations.erase(active->key);
            protected_generations.insert(staged_pin.extract(staged_pin.begin()));
        } catch (const SaveError &error) {
            outcome = {nullptr, error.code, error.what(), {}};
        } catch (const std::exception &) {
            outcome = {nullptr, "RestoreFailed", "The prepared checkpoint could not be finalized.", {}};
        }
        const bool ok = outcome.code == "Ok";
        if (diagnostic) {
            if (!ok) {
                *diagnostic = godot_string(outcome.message);
            } else if (publish && outcome.historical) {
                *diagnostic = "The restore selected a backup generation. Save to a different slot.";
            }
        }
        if (!ok || !publish) {
            discard_candidate(prepared->candidate, prepared->host);
        }
        prepared.reset();
        prepared_notice = false;
        finish_active(std::move(outcome));
        return ok;
    }

    // Clear an ambiguous save marker after inspection establishes the generation's status.
    void reconcile_inspection(const Outcome &outcome) {
        const auto unresolved = unresolved_slots.find(active->key);
        if (unresolved == unresolved_slots.end() || unresolved->second.sequence == 0) {
            return;
        }
        if (active->kind == Kind::Inspect && active->selection == unresolved->second) {
            if (outcome.code == "NotFound" ||
                    (outcome.code == "Ok" && outcome.value.at("status") == "Available")) {
                unresolved_slots.erase(unresolved);
            }
            return;
        }
        if (active->kind != Kind::List || outcome.code != "Ok") {
            return;
        }
        const Json &entries = outcome.value.at("generations");
        const auto expected_sequence = std::to_string(unresolved->second.sequence);
        const auto attempted = std::find_if(entries.begin(), entries.end(), [&](const Json &entry) {
            return entry.at("sequence") == expected_sequence && entry.at("id") == unresolved->second.id;
        });
        const bool found_and_valid = attempted != entries.end() && attempted->at("status") == "Available";
        const bool absence_confirmed = attempted == entries.end() && !active->selection &&
                !outcome.value.at("truncated").get<bool>();
        if (found_and_valid || absence_confirmed) {
            unresolved_slots.erase(unresolved);
        }
    }

    // Route worker progress and results to delivery, staging, or save reconciliation.
    void accept_event(Event event, bool prepare_now = false) {
        if (!active || active->ticket != event.ticket) {
            return;
        }
        if (!event.outcome) {
            progress.emplace_back(event.ticket, std::move(event.phase));
            return;
        }
        Outcome &outcome = *event.outcome;
        if (active->kind == Kind::Load && outcome.code == "Ok") {
            if (!prepare_now) {
                // Let listeners see worker progress before entering the main-thread restore.
                deferred_load = std::move(event);
                return;
            }
            if (prepare_restore(outcome, *active)) {
                if (active->prepare_only) {
                    prepared_notice = true;
                    progress.emplace_back(event.ticket, "Prepared");
                } else {
                    finish_prepared(true);
                }
                return;
            }
        } else if (active->kind == Kind::Save && outcome.code == "CommitUnknown") {
            Generation reference;
            if (outcome.value.is_object()) {
                reference = parse_generation(outcome.value.at("sequence").get<std::string>(),
                        outcome.value.at("id").get<std::string>());
            }
            unresolved_slots[active->key] = std::move(reference);
        } else if (!requires_world(active->kind)) {
            reconcile_inspection(outcome);
        }
        finish_active(std::move(outcome));
    }

};

SaveCoordinator::SaveCoordinator() : impl(std::make_unique<Impl>()) {}
SaveCoordinator::~SaveCoordinator() = default;

void SaveCoordinator::_ready() {
    set_process(true);
}

void SaveCoordinator::_exit_tree() {
    if (impl->prepared && !impl->in_callback) {
        impl->active->control->cancel();
        impl->abort_prepared("Cancelled", "The coordinator left the scene tree.");
    }
}

void SaveCoordinator::_process(double) {
    if (impl->prepared && impl->active->control->cancelled()) {
        impl->abort_prepared("Cancelled", "The prepared load was cancelled.");
    }
    if (impl->deferred_load) {
        Event event = std::move(*impl->deferred_load);
        impl->deferred_load.reset();
        impl->accept_event(std::move(event), true);
    }
    if (impl->worker) {
        for (Event &event : impl->worker->take_events()) {
            impl->accept_event(std::move(event));
        }
    }
    auto progress = std::move(impl->progress);
    impl->progress.clear();
    auto completions = std::move(impl->completions);
    impl->completions.clear();
    impl->pending_delivery = completions.size();
    for (const auto &[ticket, phase] : progress) {
        emit_signal("operation_progress", ticket, godot_string(phase));
    }
    if (impl->prepared_notice) {
        impl->prepared_notice = false;
        if (impl->prepared && !impl->active->control->cancelled()) {
            const int64_t ticket = impl->active->ticket;
            const Variant generation = to_variant(impl->prepared->result.value);
            emit_signal("load_prepared", ticket, generation);
        }
    }
    for (const Impl::Completion &completion : completions) {
        Dictionary result;
        try {
            result = result_dictionary(completion.outcome, completion.kind);
        } catch (const SaveError &error) {
            result["ok"] = false;
            result["code"] = godot_string(error.code);
            result["message"] = "The operation result could not be represented as Godot data.";
        }
        --impl->pending_delivery;
        emit_signal("operation_finished", completion.ticket, result);
    }
    impl->dispatch();
}

bool SaveCoordinator::configure(const String &app_id, const String &root) {
    if (!impl->mutable_services()) {
        return impl->reject_setup("configure", "Busy", "Configure save services on the main thread before submitting requests.");
    }
    auto id = utf8(app_id);
    if (!valid_id(id, true)) {
        return impl->reject_setup("configure", "InvalidArgument", "Use a valid lowercase application ID.");
    }
    const auto path = utf8(root);
    if (path.rfind("user://", 0) != 0 || path.size() <= 7 ||
            path.find('\0') != std::string::npos || path.find("..") != std::string::npos ||
            path.find('\\') != std::string::npos || path.find(':', 7) != std::string::npos ||
            path.find("//", 7) != std::string::npos || path[7] == '/') {
        return impl->reject_setup("configure", "InvalidArgument", "Choose a canonical subdirectory under user:// for the save root.");
    }
    try {
        const String native = ProjectSettings::get_singleton()->globalize_path(root);
        auto store = make_local_store(std::filesystem::u8path(utf8(native)));
        auto codec = make_json_codec();
        impl->versions.app_id = std::move(id);
        impl->store = std::move(store);
        impl->codec = std::move(codec);
        impl->admission_error.clear();
        impl->admission_message.clear();
        return true;
    } catch (const SaveError &error) {
        return impl->reject_setup("configure", error.code, error.what());
    } catch (const std::exception &) {
        return impl->reject_setup("configure", "IoError", "The local save store or codec could not be initialized.");
    }
}

bool SaveCoordinator::configure_scope(const String &scope_id, int64_t version) {
    if (!impl->mutable_schema()) {
        return impl->reject_setup("configure_scope", "Busy", "Configure the save scope on the main thread before admitting a save or load.");
    }
    const auto id = utf8(scope_id);
    if (!valid_id(id, true) || !valid_version(version)) {
        return impl->reject_setup("configure_scope", "InvalidArgument", "Use a valid lowercase scope ID and a positive schema version.");
    }
    impl->versions.scope_id = id;
    impl->versions.scope_version = static_cast<int>(version);
    return true;
}

bool SaveCoordinator::set_host(Object *host) {
    if (!impl->mutable_bindings()) {
        return impl->reject_setup("set_host", "Busy", "Wait for saves, loads, and callbacks to finish before rebinding the host on the main thread.");
    }
    if (!host || !host->has_method("save_create_candidate") || !host->has_method("save_publish")) {
        return impl->reject_setup("set_host", "InvalidArgument", "Supply a live host with save_create_candidate() and save_publish() methods.");
    }
    impl->host = host->get_instance_id();
    ++impl->epoch;
    return true;
}

bool SaveCoordinator::declare_participant(const String &id, int64_t version, bool required) {
    if (!impl->mutable_schema()) {
        return impl->reject_setup("declare_participant", "Busy", "Declare participant schemas on the main thread before admitting a save or load.");
    }
    if (impl->participants.size() >= max_participants) {
        return impl->reject_setup("declare_participant", "LimitExceeded", "The checkpoint participant limit has been reached.");
    }
    const std::string name = utf8(id);
    if (!valid_id(name, true) || !valid_version(version)) {
        return impl->reject_setup("declare_participant", "InvalidArgument", "Use a valid lowercase participant ID and a positive schema version.");
    }
    if (impl->participants.count(name)) {
        return impl->reject_setup("declare_participant", "AlreadyExists", "A checkpoint participant already uses ID '" + name + "'.");
    }
    impl->participants.emplace(name, Participant{0, static_cast<int>(version), required, {}});
    impl->versions.participants.emplace(name, static_cast<int>(version));
    return true;
}

bool SaveCoordinator::bind_participant(const String &id, Object *adapter) {
    if (!impl->mutable_bindings()) {
        return impl->reject_setup("bind_participant", "Busy", "Wait for saves, loads, and callbacks to finish before binding a participant on the main thread.");
    }
    if (!valid_adapter(adapter)) {
        return impl->reject_setup("bind_participant", "InvalidArgument", "Supply a live adapter with save_capture() and save_stage() methods.");
    }
    auto participant = impl->participants.find(utf8(id));
    if (participant == impl->participants.end()) {
        return impl->reject_setup("bind_participant", "NotFound", "Declare the participant ID before binding its adapter.");
    }
    participant->second.adapter = adapter->get_instance_id();
    ++impl->epoch;
    return true;
}

bool SaveCoordinator::register_participant(const String &id, int64_t version, Object *adapter, bool required) {
    if (!impl->mutable_bindings()) {
        return impl->reject_setup("register_participant", "Busy", "Wait for saves, loads, and callbacks to finish before registering a participant on the main thread.");
    }
    if (!valid_adapter(adapter)) {
        return impl->reject_setup("register_participant", "InvalidArgument", "Supply a live adapter with save_capture() and save_stage() methods.");
    }
    return declare_participant(id, version, required) && bind_participant(id, adapter);
}

bool SaveCoordinator::register_migration(const String &id, int64_t from, const Callable &migration) {
    if (!impl->mutable_schema()) {
        return impl->reject_setup("register_migration", "Busy", "Register migrations on the main thread before admitting a save or load.");
    }
    const auto participant = impl->participants.find(utf8(id));
    if (participant == impl->participants.end()) {
        return impl->reject_setup("register_migration", "NotFound", "Declare the participant ID before registering a migration for it.");
    }
    if (!valid_version(from) || from >= participant->second.version || !migration.is_valid()) {
        return impl->reject_setup("register_migration", "InvalidArgument", "Supply a valid callback and a positive version earlier than the participant version.");
    }
    if (impl->migration_count >= max_migrations) {
        return impl->reject_setup("register_migration", "LimitExceeded", "The checkpoint migration limit has been reached.");
    }
    if (!participant->second.migrations.emplace(static_cast<int>(from), migration).second) {
        return impl->reject_setup("register_migration", "AlreadyExists", "A migration already handles that version for the participant.");
    }
    ++impl->migration_count;
    return true;
}

bool SaveCoordinator::register_scope_migration(int64_t from, const Callable &migration) {
    if (!impl->mutable_schema()) {
        return impl->reject_setup("register_scope_migration", "Busy", "Register migrations on the main thread before admitting a save or load.");
    }
    if (!valid_version(from) || from >= impl->versions.scope_version || !migration.is_valid()) {
        return impl->reject_setup("register_scope_migration", "InvalidArgument", "Supply a valid callback and a positive version earlier than the scope version.");
    }
    if (impl->migration_count >= max_migrations) {
        return impl->reject_setup("register_scope_migration", "LimitExceeded", "The checkpoint migration limit has been reached.");
    }
    if (!impl->scope_migrations.emplace(static_cast<int>(from), migration).second) {
        return impl->reject_setup("register_scope_migration", "AlreadyExists", "A migration already handles that scope version.");
    }
    ++impl->migration_count;
    return true;
}

int64_t SaveCoordinator::queue_save(const String &profile, const String &slot, const Dictionary &metadata, bool autosave) {
    if (!impl->on_main() || !impl->can_admit(Kind::Save)) {
        return 0;
    }
    Pending pending;
    pending.job.kind = Kind::Save;
    pending.job.key = {utf8(profile), utf8(slot)};
    pending.autosave = autosave;
    if (!valid_id(pending.job.key.profile) || !valid_id(pending.job.key.slot)) {
        return impl->reject("InvalidArgument", invalid_slot_message);
    }
    if (impl->unresolved_slots.count(pending.job.key)) {
        return impl->reject("CommitUnknown", "A previous write to this slot is unresolved. Inspect the exact generation before retrying.");
    }
    if (impl->recovered_slot == pending.job.key) {
        return impl->reject("RecoveryRequiresNewSlot", "The active checkpoint was restored from a backup. Save to a different slot.");
    }

    const auto superseded = autosave ? std::find_if(impl->queue.begin(), impl->queue.end(), [&](const Pending &old) {
        return old.autosave && !old.captured && old.epoch == impl->epoch && old.job.key == pending.job.key;
    }) : impl->queue.end();
    if (superseded == impl->queue.end() && impl->count() >= max_pending_jobs) {
        return impl->reject("Busy", "The checkpoint save queue is full. Wait for pending storage operations before retrying.");
    }
    try {
        Limits metadata_limits = impl->limits;
        metadata_limits.max_file_bytes = max_metadata_bytes;
        pending.job.envelope = to_json(metadata, metadata_limits);
    } catch (const SaveError &error) {
        return impl->reject(error.code, error.what());
    } catch (const std::exception &) {
        return impl->reject("InvalidData", "Checkpoint metadata could not be converted to JSON-compatible data.");
    }
    if (superseded != impl->queue.end()) {
        impl->finish_pending(*superseded, "Superseded", "A more recent autosave replaced this intent.");
        impl->queue.erase(superseded);
    }
    return impl->admit(std::move(pending));
}

int64_t SaveCoordinator::request_save(const String &profile, const String &slot, const Dictionary &metadata) {
    return queue_save(profile, slot, metadata, false);
}

int64_t SaveCoordinator::request_autosave(const String &profile, const String &slot, const Dictionary &metadata) {
    return queue_save(profile, slot, metadata, true);
}

int64_t SaveCoordinator::request_load(const String &profile, const String &slot, const String &sequence, const String &id) {
    return queue_load(profile, slot, sequence, id, false);
}

int64_t SaveCoordinator::request_prepare_load(const String &profile, const String &slot, const String &sequence, const String &id) {
    return queue_load(profile, slot, sequence, id, true);
}

int64_t SaveCoordinator::queue_load(const String &profile, const String &slot,
        const String &sequence, const String &id, bool prepare_only) {
    if (!impl->on_main() || !impl->can_admit(Kind::Load)) {
        return 0;
    }
    Pending pending;
    pending.job.kind = Kind::Load;
    pending.job.prepare_only = prepare_only;
    pending.job.key = {utf8(profile), utf8(slot)};
    if (!valid_id(pending.job.key.profile) || !valid_id(pending.job.key.slot)) {
        return impl->reject("InvalidArgument", invalid_slot_message);
    }
    if (!sequence.is_empty() || !id.is_empty()) {
        try {
            pending.job.selection = parse_generation(utf8(sequence), utf8(id));
        } catch (const SaveError &error) {
            return impl->reject(error.code, error.what());
        }
    }
    auto it = impl->queue.begin();
    while (it != impl->queue.end()) {
        if (it->autosave && !it->captured) {
            impl->finish_pending(*it, "Superseded", "Loading superseded an uncaptured autosave.");
            it = impl->queue.erase(it);
        } else {
            ++it;
        }
    }
    const int64_t ticket = impl->admit(std::move(pending));
    impl->load_fence = ticket != 0;
    return ticket;
}

int64_t SaveCoordinator::request_list(const String &profile, const String &slot,
        const String &before_sequence, const String &before_id, int64_t limit) {
    if (!impl->on_main() || !impl->can_admit(Kind::List)) {
        return 0;
    }
    if (impl->count() >= max_pending_jobs) {
        return impl->reject("Busy", "The checkpoint queue is full. Wait for pending operations before listing the slot.");
    }
    Pending pending;
    pending.job.kind = Kind::List;
    pending.job.key = {utf8(profile), utf8(slot)};
    if (!valid_id(pending.job.key.profile) || !valid_id(pending.job.key.slot)) {
        return impl->reject("InvalidArgument", invalid_slot_message);
    }
    if (limit < 1) {
        return impl->reject("InvalidArgument", "The checkpoint list page size must be positive.");
    }
    pending.job.page_size = static_cast<size_t>(std::min<int64_t>(limit, 16));
    if (!before_sequence.is_empty() || !before_id.is_empty()) {
        try {
            pending.job.selection = parse_generation(utf8(before_sequence), utf8(before_id));
        } catch (const SaveError &error) {
            return impl->reject(error.code, error.what());
        }
    }
    return impl->admit(std::move(pending));
}

int64_t SaveCoordinator::request_inspect_generation(const String &profile, const String &slot,
        const String &sequence, const String &id) {
    if (!impl->on_main() || !impl->can_admit(Kind::Inspect)) {
        return 0;
    }
    if (impl->count() >= max_pending_jobs) {
        return impl->reject("Busy", "The checkpoint queue is full. Wait for pending operations before inspecting a generation.");
    }
    Pending pending;
    pending.job.kind = Kind::Inspect;
    pending.job.key = {utf8(profile), utf8(slot)};
    if (!valid_id(pending.job.key.profile) || !valid_id(pending.job.key.slot)) {
        return impl->reject("InvalidArgument", invalid_slot_message);
    }
    try {
        pending.job.selection = parse_generation(utf8(sequence), utf8(id));
    } catch (const SaveError &error) {
        return impl->reject(error.code, error.what());
    }
    return impl->admit(std::move(pending));
}

bool SaveCoordinator::capture_pending(const String &revision) {
    if (!impl->on_main() || impl->in_callback) {
        return false;
    }
    auto it = std::find_if(impl->queue.begin(), impl->queue.end(), [](const Pending &pending) {
        return pending.job.kind == Kind::Save && !pending.captured;
    });
    if (it == impl->queue.end()) {
        return false;
    }
    CallbackGuard guard(impl->in_callback);
    try {
        const auto capture_revision = utf8(revision);
        if (it->epoch != impl->epoch) {
            throw SaveError("StaleWorld", "The active world changed before capture.");
        }
        if (capture_revision.size() > 128) {
            throw SaveError("InvalidArgument", "The capture revision label is too long.");
        }
        impl->progress.emplace_back(it->job.ticket, "Capturing");
        it->job.envelope = impl->capture(*it, capture_revision);
        it->captured = true;
        return true;
    } catch (const SaveError &error) {
        impl->finish_pending(*it, error.code, error.what());
    } catch (const std::exception &) {
        impl->finish_pending(*it, "CaptureFailed", "The checkpoint could not be captured.");
    }
    impl->queue.erase(it);
    return false;
}

String SaveCoordinator::cancel(int64_t ticket) {
    if (!impl->on_main()) {
        return "UnknownTicket";
    }
    if (impl->in_callback) {
        return "TooLate";
    }
    const auto it = std::find_if(impl->queue.begin(), impl->queue.end(), [&](const Pending &pending) {
        return pending.job.ticket == ticket;
    });
    if (it != impl->queue.end()) {
        impl->finish_pending(*it, "Cancelled", "The operation was cancelled.");
        impl->queue.erase(it);
        return "Cancelled";
    }
    if (impl->active && impl->active->ticket == ticket) {
        return impl->active->control->cancel() ? "Cancelled" : "TooLate";
    }
    return "UnknownTicket";
}

String SaveCoordinator::get_last_admission_error() const {
    return impl->on_main() ? godot_string(impl->admission_error) : String("WrongThread");
}

String SaveCoordinator::get_last_admission_message() const {
    return impl->on_main() ? godot_string(impl->admission_message) :
                            String("Submit checkpoint operations from the main thread.");
}

bool SaveCoordinator::is_idle() const {
    return impl->on_main() && impl->count() == 0 && impl->completions.empty() && impl->pending_delivery == 0;
}

bool SaveCoordinator::invalidate_world() {
    if (!impl->on_main() || impl->in_callback) {
        return false;
    }
    if (impl->active && requires_world(impl->active->kind) && !impl->active->control->cancel()) {
        return false;
    }
    ++impl->epoch;
    impl->recovered_slot.reset();
    auto it = impl->queue.begin();
    while (it != impl->queue.end()) {
        if (!requires_world(it->job.kind)) {
            ++it;
        } else {
            impl->finish_pending(*it, "StaleWorld", "The host invalidated the world before completion.");
            it = impl->queue.erase(it);
        }
    }
    return true;
}

bool SaveCoordinator::set_services(std::shared_ptr<Store> store, std::shared_ptr<Codec> codec) {
    if (!impl->mutable_services()) {
        return impl->reject_setup("set_services", "Busy", "Install save services on the main thread before submitting requests.");
    }
    if (!store || !codec) {
        return impl->reject_setup("set_services", "InvalidArgument", "Supply both a checkpoint store and a codec.");
    }
    impl->store = std::move(store);
    impl->codec = std::move(codec);
    return true;
}

Variant SaveCoordinator::get_prepared_candidate(int64_t ticket) const {
    if (!impl->on_main() || impl->in_callback || !impl->prepared || !impl->active ||
            !impl->active->prepare_only || impl->active->ticket != ticket || impl->active->control->cancelled()) {
        return Variant();
    }
    return impl->prepared->candidate;
}

bool SaveCoordinator::activate_prepared_load(int64_t ticket) {
    String diagnostic;
    const bool activated = activate_prepared_load(ticket, diagnostic);
    if (!activated) {
        UtilityFunctions::printerr("[Crimson] SaveCoordinator.activate_prepared_load: ", diagnostic);
    }
    return activated;
}

bool SaveCoordinator::activate_prepared_load(int64_t ticket, String &diagnostic) {
    if (!impl->on_main()) {
        diagnostic = "Activate a prepared checkpoint on the main thread.";
        return false;
    }
    if (impl->in_callback) {
        diagnostic = "A checkpoint callback is still running.";
        return false;
    }
    if (!impl->prepared || !impl->active || !impl->active->prepare_only || impl->active->ticket != ticket) {
        diagnostic = "The selected checkpoint is no longer prepared for activation.";
        return false;
    }
    if (impl->active->control->cancelled()) {
        diagnostic = "The prepared checkpoint was cancelled before activation.";
        return false;
    }
    diagnostic = String();
    return impl->finish_prepared(true, &diagnostic);
}

bool SaveCoordinator::validate_prepared_load_for_save(int64_t ticket) {
    if (!impl->on_main() || impl->in_callback) {
        return impl->reject_setup("validate_prepared_load_for_save", "Busy", "Validate a checkpoint on the main thread after its callbacks finish.");
    }
    if (!impl->prepared || !impl->active || !impl->active->prepare_only || !impl->active->selection ||
            impl->active->ticket != ticket || impl->active->control->cancelled()) {
        return impl->reject_setup("validate_prepared_load_for_save", "InvalidArgument", "Supply an active, prepared ticket selected by exact generation identity.");
    }
    return impl->finish_prepared(false);
}

bool SaveCoordinator::load_native_provider(const String &path, const String &configuration) {
    if (!impl->mutable_services()) {
        return impl->reject_setup("load_native_provider", "Busy", "Install a native provider on the main thread before submitting requests.");
    }
    if (!impl->store || !impl->codec) {
        return impl->reject_setup("load_native_provider", "Unconfigured", "Configure the save coordinator before installing a native provider.");
    }
    try {
        const String native = ProjectSettings::get_singleton()->globalize_path(path);
        const std::filesystem::path provider_path = std::filesystem::u8path(utf8(native));
        auto services = checkpoint::load_native_provider(provider_path, utf8(configuration));
        if (services.store) {
            impl->store = std::move(services.store);
        }
        if (services.codec) {
            impl->codec = std::move(services.codec);
        }
        impl->admission_error.clear();
        impl->admission_message.clear();
        return true;
    } catch (const SaveError &error) {
        return impl->reject_setup("load_native_provider", error.code, error.what());
    } catch (const std::exception &) {
        return impl->reject_setup("load_native_provider", "IoError", "The native save provider could not be initialized.");
    }
}

void SaveCoordinator::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "app_id", "root"), &SaveCoordinator::configure,
            DEFVAL(String("user://godot-save-system")));
    ClassDB::bind_method(D_METHOD("configure_scope", "scope_id", "version"), &SaveCoordinator::configure_scope);
    ClassDB::bind_method(D_METHOD("set_host", "host"), &SaveCoordinator::set_host);
    ClassDB::bind_method(D_METHOD("declare_participant", "id", "version", "required"),
            &SaveCoordinator::declare_participant, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("bind_participant", "id", "adapter"), &SaveCoordinator::bind_participant);
    ClassDB::bind_method(D_METHOD("register_participant", "id", "version", "adapter", "required"),
            &SaveCoordinator::register_participant, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("register_migration", "id", "from_version", "migration"), &SaveCoordinator::register_migration);
    ClassDB::bind_method(D_METHOD("register_scope_migration", "from_version", "migration"), &SaveCoordinator::register_scope_migration);
    ClassDB::bind_method(D_METHOD("request_save", "profile", "slot", "metadata"), &SaveCoordinator::request_save, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("request_autosave", "profile", "slot", "metadata"), &SaveCoordinator::request_autosave, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("request_load", "profile", "slot", "sequence", "generation_id"), &SaveCoordinator::request_load,
            DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("request_prepare_load", "profile", "slot", "sequence", "generation_id"),
            &SaveCoordinator::request_prepare_load, DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_prepared_candidate", "ticket"), &SaveCoordinator::get_prepared_candidate);
    ClassDB::bind_method(D_METHOD("activate_prepared_load", "ticket"),
            static_cast<bool (SaveCoordinator::*)(int64_t)>(&SaveCoordinator::activate_prepared_load));
    ClassDB::bind_method(D_METHOD("validate_prepared_load_for_save", "ticket"), &SaveCoordinator::validate_prepared_load_for_save);
    ClassDB::bind_method(D_METHOD("request_list", "profile", "slot", "before_sequence", "before_generation_id", "limit"),
            &SaveCoordinator::request_list, DEFVAL(String()), DEFVAL(String()), DEFVAL(16));
    ClassDB::bind_method(D_METHOD("request_inspect_generation", "profile", "slot", "sequence", "generation_id"),
            &SaveCoordinator::request_inspect_generation);
    ClassDB::bind_method(D_METHOD("load_native_provider", "path", "configuration"),
            &SaveCoordinator::load_native_provider, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("capture_pending", "revision"), &SaveCoordinator::capture_pending, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("cancel", "ticket"), &SaveCoordinator::cancel);
    ClassDB::bind_method(D_METHOD("get_last_admission_error"), &SaveCoordinator::get_last_admission_error);
    ClassDB::bind_method(D_METHOD("get_last_admission_message"), &SaveCoordinator::get_last_admission_message);
    ClassDB::bind_method(D_METHOD("is_idle"), &SaveCoordinator::is_idle);
    ClassDB::bind_method(D_METHOD("invalidate_world"), &SaveCoordinator::invalidate_world);
    ADD_SIGNAL(MethodInfo("operation_progress", PropertyInfo(Variant::INT, "ticket"), PropertyInfo(Variant::STRING, "phase")));
    ADD_SIGNAL(MethodInfo("operation_finished", PropertyInfo(Variant::INT, "ticket"), PropertyInfo(Variant::DICTIONARY, "result")));
    ADD_SIGNAL(MethodInfo("load_prepared", PropertyInfo(Variant::INT, "ticket"), PropertyInfo(Variant::DICTIONARY, "generation")));
}

} // namespace checkpoint
