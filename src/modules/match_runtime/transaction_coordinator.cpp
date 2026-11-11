// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "transaction_coordinator.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <utility>

using namespace godot;

namespace crimson::match_runtime {

namespace {

// Bounds for stored DTOs, whole transaction plans, and aggregate prepared candidates.
constexpr std::size_t MAX_DTO_NODES = 65536;
constexpr std::size_t MAX_DTO_CHARACTERS = 1048576;
constexpr std::size_t MAX_DTO_DEPTH = 32;
constexpr std::size_t MAX_CHANGES = 256;
constexpr int64_t MAX_REVISION = (std::numeric_limits<int64_t>::max)();

// Accept a compact, stable ASCII identifier suitable for store and validator lookup.
bool valid_store_id(const String &store_id) {
    if (store_id.is_empty() || store_id.length() > 64) {
        return false;
    }
    for (int64_t i = 0; i < store_id.length(); ++i) {
        const char32_t value = store_id[i];
        const bool alphanumeric = (value >= U'a' && value <= U'z') || (value >= U'A' && value <= U'Z') ||
                (value >= U'0' && value <= U'9');
        if (!alphanumeric && (i == 0 || (value != U'_' && value != U'-' && value != U'.'))) {
            return false;
        }
    }
    return true;
}

// Tracks resource use while copying a single DTO or a group of related DTOs.
struct DtoBudget {
    // Number of copied container and scalar values, including dictionary keys.
    std::size_t nodes = 0;
    // Total characters copied from all String values and keys.
    std::size_t characters = 0;
};

// Recursively copy supported data while rejecting cyclic or oversized structures.
bool copy_value(const Variant &value, Variant &output, DtoBudget &budget, std::size_t depth) {
    if (depth > MAX_DTO_DEPTH || budget.nodes == MAX_DTO_NODES) {
        return false;
    }
    ++budget.nodes;

    switch (value.get_type()) {
        case Variant::NIL:
        case Variant::BOOL:
        case Variant::INT:
            output = value;
            return true;
        case Variant::FLOAT:
            if (!std::isfinite(static_cast<double>(value))) {
                return false;
            }
            output = value;
            return true;
        case Variant::STRING: {
            const std::size_t length = static_cast<std::size_t>(String(value).length());
            if (length > MAX_DTO_CHARACTERS - budget.characters) {
                return false;
            }
            budget.characters += length;
            output = value;
            return true;
        }
        case Variant::ARRAY: {
            const Array source = value;
            if (static_cast<std::size_t>(source.size()) > MAX_DTO_NODES - budget.nodes) {
                return false;
            }
            Array detached;
            for (int64_t i = 0; i < source.size(); ++i) {
                Variant item;
                if (!copy_value(source[i], item, budget, depth + 1)) {
                    return false;
                }
                detached.push_back(item);
            }
            output = detached;
            return true;
        }
        case Variant::DICTIONARY: {
            const Dictionary source = value;
            if (static_cast<std::size_t>(source.size()) > (MAX_DTO_NODES - budget.nodes) / 2) {
                return false;
            }
            Dictionary detached;
            const Array keys = source.keys();
            for (int64_t i = 0; i < keys.size(); ++i) {
                const Variant &key = keys[i];
                Variant copied_key;
                Variant item;
                if (key.get_type() != Variant::STRING || !copy_value(key, copied_key, budget, depth + 1) ||
                        !copy_value(source[key], item, budget, depth + 1)) {
                    return false;
                }
                detached[copied_key] = item;
            }
            output = detached;
            return true;
        }
        default:
            return false;
    }
}

// Copy a dictionary within the caller's budget and expose it only on success.
bool copy_dictionary(const Dictionary &value, Dictionary &output, DtoBudget &budget) {
    Variant detached;
    if (!copy_value(value, detached, budget, 0)) {
        return false;
    }
    output = detached;
    return true;
}

// Use a bounded callback message when available, with a framework diagnostic as fallback.
String failure_message(const Dictionary &reply, const String &fallback) {
    const Variant message = reply.get("message", Variant());
    if (message.get_type() == Variant::STRING && !String(message).is_empty()) {
        return String(message).left(512);
    }
    return fallback;
}

// Prevents reentrant mutations while transaction callbacks run.
class CommitGuard {
public:
    // Close the coordinator's mutation gate.
    explicit CommitGuard(bool &committing) : committing(committing) { committing = true; }
    // Reopen the mutation gate on every exit path.
    ~CommitGuard() { committing = false; }

private:
    // Mutation flag owned by the coordinator for the lifetime of this guard.
    bool &committing;
};

} // namespace

bool copy_runtime_dto(const Dictionary &value, Dictionary &output) {
    DtoBudget budget;
    return copy_dictionary(value, output, budget);
}

void CrimsonTransaction::_bind_methods() {
    ClassDB::bind_method(D_METHOD("add_change", "store_id", "operation", "expected_revision"),
            &CrimsonTransaction::add_change);
}

Error CrimsonTransaction::add_change(const String &store_id, const Dictionary &operation, int64_t expected_revision) {
    if (owner_id == 0 || consumed || restore_base_revision >= 0 || owner_thread != std::this_thread::get_id()) {
        return ERR_UNAVAILABLE;
    }
    if (!valid_store_id(store_id) || expected_revision < 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (changes.size() == MAX_CHANGES) {
        return ERR_OUT_OF_MEMORY;
    }
    DtoBudget budget{ dto_nodes, dto_characters };
    Dictionary detached;
    if (!copy_dictionary(operation, detached, budget)) {
        return ERR_INVALID_DATA;
    }
    changes.push_back({ store_id, std::move(detached), expected_revision });
    dto_nodes = budget.nodes;
    dto_characters = budget.characters;
    return OK;
}

void CrimsonTransactionCoordinator::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_store", "store_id", "initial_state", "prepare", "validate_restore"),
            &CrimsonTransactionCoordinator::register_store, DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("register_validator", "validator_id", "watched_stores", "validator"),
            &CrimsonTransactionCoordinator::register_validator);
    ClassDB::bind_method(D_METHOD("read_store", "store_id"), &CrimsonTransactionCoordinator::read_store);
    ClassDB::bind_method(D_METHOD("get_store_revision", "store_id"), &CrimsonTransactionCoordinator::get_store_revision);
    ClassDB::bind_method(D_METHOD("get_revision"), &CrimsonTransactionCoordinator::get_revision);
    ClassDB::bind_method(D_METHOD("begin", "context"), &CrimsonTransactionCoordinator::begin, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("commit", "transaction"), &CrimsonTransactionCoordinator::commit);
    ClassDB::bind_method(D_METHOD("capture_snapshot"), &CrimsonTransactionCoordinator::capture_snapshot);
    ClassDB::bind_method(D_METHOD("prepare_restore", "snapshot", "context"),
            &CrimsonTransactionCoordinator::prepare_restore, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("publish_restore", "candidate", "defer_notification"),
            &CrimsonTransactionCoordinator::publish_restore, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("notify_restore_published", "published_revision"),
            &CrimsonTransactionCoordinator::notify_restore_published);
    ADD_SIGNAL(MethodInfo("committed", PropertyInfo(Variant::INT, "revision"),
            PropertyInfo(Variant::PACKED_STRING_ARRAY, "stores")));
}

bool CrimsonTransactionCoordinator::is_owner_thread() const {
    return owner_thread == std::this_thread::get_id();
}

bool CrimsonTransactionCoordinator::mutation_locked() const {
    return committing || pending_restore_revision >= 0;
}

Error CrimsonTransactionCoordinator::register_store(const String &store_id, const Dictionary &initial_state,
        const Callable &prepare, const Callable &validate_restore) {
    if (!is_owner_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (mutation_locked()) {
        return ERR_BUSY;
    }
    if (!valid_store_id(store_id) || !prepare.is_valid() ||
            (!validate_restore.is_null() && !validate_restore.is_valid())) {
        return ERR_INVALID_PARAMETER;
    }
    if (stores.find(store_id) != stores.end()) {
        return ERR_ALREADY_EXISTS;
    }
    Dictionary detached;
    if (!copy_runtime_dto(initial_state, detached)) {
        return ERR_INVALID_DATA;
    }
    stores.emplace(store_id, Store{ std::move(detached), prepare, validate_restore });
    return OK;
}

Error CrimsonTransactionCoordinator::register_validator(const String &validator_id,
        const PackedStringArray &watched_stores, const Callable &validator) {
    if (!is_owner_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (mutation_locked()) {
        return ERR_BUSY;
    }
    if (!valid_store_id(validator_id) || watched_stores.is_empty() ||
            static_cast<std::size_t>(watched_stores.size()) > MAX_CHANGES || !validator.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    if (has_validator(validator_id)) {
        return ERR_ALREADY_EXISTS;
    }
    std::set<String> seen;
    for (int64_t i = 0; i < watched_stores.size(); ++i) {
        const String &store_id = watched_stores[i];
        if (stores.find(store_id) == stores.end()) {
            return ERR_DOES_NOT_EXIST;
        }
        if (!seen.insert(store_id).second) {
            return ERR_INVALID_PARAMETER;
        }
    }
    validators.emplace(validator_id, Validator{ watched_stores, validator });
    return OK;
}

Error CrimsonTransactionCoordinator::register_store_with_validator(const String &store_id,
        const Dictionary &initial_state, const Callable &prepare, const String &validator_id,
        const PackedStringArray &watched_stores, const Callable &validator) {
    const Error registered = register_store(store_id, initial_state, prepare);
    if (registered != OK) {
        return registered;
    }
    const Error result = register_validator(validator_id, watched_stores, validator);
    if (result != OK) {
        stores.erase(store_id);
    }
    return result;
}

Dictionary CrimsonTransactionCoordinator::read_store(const String &store_id) const {
    if (!is_owner_thread()) {
        return {};
    }
    const auto store = stores.find(store_id);
    return store != stores.end() ? store->second.state.duplicate(true) : Dictionary();
}

Variant CrimsonTransactionCoordinator::read_store_value(const String &store_id, const String &key) const {
    if (!is_owner_thread()) {
        return {};
    }
    const auto store = stores.find(store_id);
    return store != stores.end() ? store->second.state.get(key, Variant()).duplicate(true) : Variant();
}

int64_t CrimsonTransactionCoordinator::get_store_revision(const String &store_id) const {
    if (!is_owner_thread()) {
        return -1;
    }
    const auto store = stores.find(store_id);
    return store != stores.end() ? store->second.revision : -1;
}

int64_t CrimsonTransactionCoordinator::get_revision() const {
    return is_owner_thread() ? revision : -1;
}

Ref<CrimsonTransaction> CrimsonTransactionCoordinator::begin(const Dictionary &context) {
    if (!is_owner_thread() || mutation_locked()) {
        return {};
    }
    DtoBudget budget;
    Dictionary detached;
    if (!copy_dictionary(context, detached, budget)) {
        return {};
    }
    Ref<CrimsonTransaction> transaction;
    transaction.instantiate();
    transaction->owner_id = get_instance_id();
    transaction->owner_thread = owner_thread;
    transaction->context = std::move(detached);
    transaction->dto_nodes = budget.nodes;
    transaction->dto_characters = budget.characters;
    return transaction;
}

bool CrimsonTransactionCoordinator::owns_transaction(const Ref<CrimsonTransaction> &transaction) const {
    return is_owner_thread() && transaction.is_valid() && transaction->owner_id == get_instance_id() &&
            transaction->restore_base_revision < 0;
}

bool CrimsonTransactionCoordinator::has_validator(const String &validator_id) const {
    return is_owner_thread() && validators.find(validator_id) != validators.end();
}

Dictionary CrimsonTransactionCoordinator::result(bool ok, const String &code, const String &message) const {
    Dictionary outcome;
    outcome["ok"] = ok;
    outcome["code"] = code;
    outcome["revision"] = get_revision();
    outcome["message"] = message;
    return outcome;
}

Dictionary CrimsonTransactionCoordinator::capture_snapshot() const {
    if (!is_owner_thread()) {
        return result(false, "wrong_thread", "Capture must run on the coordinator's owner thread.");
    }
    if (pending_restore_revision >= 0) {
        return result(false, "busy", "A restore is waiting for its transition to publish.");
    }

    Dictionary states;
    for (const auto &[store_id, store] : stores) {
        states[store_id] = store.state;
    }
    Dictionary detached;
    if (!copy_runtime_dto(states, detached)) {
        return result(false, "invalid_snapshot", "The aggregate store snapshot exceeds the runtime DTO budget.");
    }
    Dictionary outcome = result(true, "ok", {});
    outcome["stores"] = detached;
    return outcome;
}

Dictionary CrimsonTransactionCoordinator::prepare_restore(const Dictionary &snapshot, const Dictionary &context) {
    if (!is_owner_thread()) {
        return result(false, "wrong_thread", "Restore preparation must run on the coordinator's owner thread.");
    }
    if (mutation_locked()) {
        return result(false, "busy", "A transaction is being prepared or published.");
    }
    if (revision == MAX_REVISION) {
        return result(false, "revision_exhausted", "The coordinator cannot advance its revision.");
    }
    if (static_cast<std::size_t>(snapshot.size()) != stores.size()) {
        return result(false, "invalid_snapshot", "The snapshot must contain exactly the registered stores.");
    }

    Dictionary detached;
    Dictionary detached_context;
    if (!copy_runtime_dto(snapshot, detached) || !copy_runtime_dto(context, detached_context)) {
        return result(false, "invalid_snapshot", "The snapshot or restore context is not a bounded runtime DTO.");
    }
    for (const auto &[store_id, store] : stores) {
        if (detached.get(store_id, Variant()).get_type() != Variant::DICTIONARY) {
            return result(false, "invalid_snapshot", String("The snapshot requires a Dictionary for store ") + store_id + ".");
        }
    }

    const Ref<CrimsonTransactionCoordinator> keep_alive(this);
    CommitGuard guard(committing);
    Dictionary prepared;
    DtoBudget prepared_budget;
    for (const auto &[store_id, store] : stores) {
        const Dictionary candidate = detached[store_id];
        const bool explicit_check = !store.validate_restore.is_null();
        const Callable &check = explicit_check ? store.validate_restore : store.prepare;
        const String fallback = String("Store ") + store_id + " rejected the restore candidate.";
        if (!check.is_valid()) {
            return result(false, "restore_validation_failed", String("The restore check is unavailable for store ") + store_id + ".");
        }
        const Variant raw = explicit_check ? check.call(candidate.duplicate(true), detached_context.duplicate(true)) :
                check.call(candidate.duplicate(true), Array(), detached_context.duplicate(true));
        if (raw.get_type() != Variant::DICTIONARY) {
            return result(false, "restore_validation_failed", fallback);
        }
        const Dictionary reply = raw;
        const Variant ok = reply.get("ok", Variant());
        if (ok.get_type() != Variant::BOOL || !static_cast<bool>(ok)) {
            return result(false, "restore_validation_failed", failure_message(reply, fallback));
        }
        const Variant state = explicit_check ? Variant(candidate) : reply.get("state", Variant());
        if (state.get_type() != Variant::DICTIONARY) {
            return result(false, "restore_validation_failed", String("Restore preparation must return a Dictionary state for store ") + store_id + ".");
        }
        // Copy the callback reply before another store callback can mutate any retained reference.
        Dictionary checked;
        if (!copy_dictionary(state, checked, prepared_budget)) {
            return result(false, "restore_validation_failed", String("Restore preparation returned an invalid DTO for store ") + store_id + ".");
        }
        prepared[store_id] = checked;
    }

    Ref<CrimsonTransaction> candidate;
    candidate.instantiate();
    if (!copy_runtime_dto(prepared, candidate->restore_states)) {
        return result(false, "invalid_snapshot", "The prepared restore exceeds the runtime DTO budget.");
    }
    const String invalid = validate_prepared_states(candidate->restore_states, detached_context);
    if (!invalid.is_empty()) {
        return result(false, "validation_failed", invalid);
    }
    candidate->owner_id = get_instance_id();
    candidate->owner_thread = owner_thread;
    candidate->restore_base_revision = revision;
    candidate->restore_validator_count = validators.size();
    Dictionary outcome = result(true, "ok", {});
    outcome["candidate"] = candidate;
    return outcome;
}

Dictionary CrimsonTransactionCoordinator::publish_restore(const Ref<CrimsonTransaction> &candidate, bool defer_notification) {
    if (!is_owner_thread()) {
        return result(false, "wrong_thread", "Restore publication must run on the coordinator's owner thread.");
    }
    if (mutation_locked()) {
        return result(false, "busy", "A transaction is being prepared or published.");
    }
    if (candidate.is_null() || candidate->owner_id != get_instance_id() || candidate->restore_base_revision < 0) {
        return result(false, "invalid_restore", "The prepared restore belongs to a different coordinator or was never prepared.");
    }
    if (candidate->consumed) {
        return result(false, "consumed_transaction", "The prepared restore has already been submitted.");
    }
    candidate->consumed = true;
    if (candidate->restore_base_revision != revision || candidate->restore_validator_count != validators.size() ||
            static_cast<std::size_t>(candidate->restore_states.size()) != stores.size()) {
        candidate->restore_states.clear();
        return result(false, "stale_revision", "The store registry or committed revision changed after restore preparation.");
    }

    const Ref<CrimsonTransactionCoordinator> keep_alive(this);
    CommitGuard guard(committing);
    PackedStringArray changed_stores;
    for (auto &[store_id, store] : stores) {
        store.state = candidate->restore_states[store_id];
        ++store.revision;
        changed_stores.append(store_id);
    }
    candidate->restore_states.clear();
    ++revision;
    if (defer_notification) {
        pending_restore_revision = revision;
        pending_restore_stores = std::move(changed_stores);
    } else {
        emit_signal("committed", revision, changed_stores);
    }
    return result(true, "ok", {});
}

Error CrimsonTransactionCoordinator::notify_restore_published(int64_t published_revision) {
    if (!is_owner_thread()) {
        return ERR_UNAVAILABLE;
    }
    if (committing) {
        return ERR_BUSY;
    }
    if (pending_restore_revision < 0 || pending_restore_revision != published_revision) {
        return ERR_INVALID_PARAMETER;
    }

    const Ref<CrimsonTransactionCoordinator> keep_alive(this);
    CommitGuard guard(committing);
    const PackedStringArray changed_stores = std::move(pending_restore_stores);
    pending_restore_revision = -1;
    emit_signal("committed", published_revision, changed_stores);
    return OK;
}

Dictionary CrimsonTransactionCoordinator::commit(const Ref<CrimsonTransaction> &transaction) {
    if (!is_owner_thread()) {
        return result(false, "wrong_thread", "Commit must run on the coordinator's owner thread.");
    }
    if (mutation_locked()) {
        return result(false, "busy", "A transaction is being prepared or published.");
    }
    if (!owns_transaction(transaction)) {
        return result(false, "invalid_transaction", "The transaction belongs to a different coordinator or was never begun.");
    }
    if (transaction->consumed) {
        return result(false, "consumed_transaction", "The transaction has already been submitted.");
    }

    const Ref<CrimsonTransactionCoordinator> keep_alive(this);
    CommitGuard guard(committing);
    transaction->consumed = true;
    auto changes = std::move(transaction->changes);
    Dictionary context = std::move(transaction->context);
    if (changes.empty()) {
        return result(false, "empty_transaction", "The transaction has no changes.");
    }
    if (revision == MAX_REVISION) {
        return result(false, "revision_exhausted", "The coordinator cannot advance its revision.");
    }

    std::map<String, Array> operations_by_store;
    for (const auto &change : changes) {
        const auto store = stores.find(change.store_id);
        if (store == stores.end()) {
            return result(false, "unknown_store", String("Unregistered store ") + change.store_id + ".");
        }
        if (store->second.revision != change.expected_revision) {
            return result(false, "stale_revision", String("The revision changed for store ") + change.store_id + ".");
        }
        operations_by_store[change.store_id].push_back(change.operation);
    }

    // Pairs a validated candidate with the store it will replace at publication.
    struct PreparedStore {
        // Destination store retained in the coordinator's stable map.
        Store *store;
        // Detached candidate ready to install after all validators succeed.
        Dictionary state;
    };
    std::vector<PreparedStore> prepared;
    prepared.reserve(operations_by_store.size());
    PackedStringArray changed_stores;
    Dictionary prepared_states;
    DtoBudget prepared_budget;

    for (const auto &[store_id, operations] : operations_by_store) {
        Store &store = stores.find(store_id)->second;
        if (!store.prepare.is_valid()) {
            return result(false, "prepare_failed", String("The prepare callback is unavailable for store ") + store_id + ".");
        }
        const Variant raw = store.prepare.call(store.state.duplicate(true), operations, context.duplicate(true));
        if (raw.get_type() != Variant::DICTIONARY) {
            return result(false, "invalid_prepared_state", String("Prepare must return a Dictionary for store ") + store_id + ".");
        }
        const Dictionary reply = raw;
        const Variant ok = reply.get("ok", Variant());
        if (ok.get_type() != Variant::BOOL) {
            return result(false, "invalid_prepared_state", String("Prepare must return a boolean ok field for store ") + store_id + ".");
        }
        if (!static_cast<bool>(ok)) {
            return result(false, "prepare_failed",
                    failure_message(reply, String("Prepare rejected changes for store ") + store_id + "."));
        }

        const Variant state = reply.get("state", Variant());
        Dictionary detached;
        if (state.get_type() != Variant::DICTIONARY || !copy_dictionary(state, detached, prepared_budget)) {
            return result(false, "invalid_prepared_state", String("Prepare returned an invalid DTO for store ") + store_id + ".");
        }
        prepared_states[store_id] = detached;
        prepared.push_back({ &store, std::move(detached) });
        changed_stores.push_back(store_id);
    }

    const String invalid = validate_prepared_states(prepared_states, context);
    if (!invalid.is_empty()) {
        return result(false, "validation_failed", invalid);
    }

    // Every state was detached before reaching this point. There are no user calls between swaps.
    for (PreparedStore &entry : prepared) {
        entry.store->state = std::move(entry.state);
        ++entry.store->revision;
    }
    ++revision;
    emit_signal("committed", revision, changed_stores);
    return result(true, "ok", {});
}

String CrimsonTransactionCoordinator::validate_prepared_states(const Dictionary &prepared, const Dictionary &context) const {
    for (const auto &[validator_id, validator] : validators) {
        bool relevant = false;
        for (int64_t i = 0; i < validator.watched_stores.size(); ++i) {
            if (prepared.has(validator.watched_stores[i])) {
                relevant = true;
                break;
            }
        }
        if (!relevant) {
            continue;
        }
        const String fallback = String("Validator ") + validator_id + " rejected the prepared changes.";
        if (!validator.validate.is_valid()) {
            return String("Validator ") + validator_id + " is unavailable.";
        }
        const Variant raw = validator.validate.call(prepared.duplicate(true), context.duplicate(true));
        if (raw.get_type() != Variant::DICTIONARY) {
            return fallback;
        }
        const Dictionary reply = raw;
        const Variant ok = reply.get("ok", Variant());
        if (ok.get_type() != Variant::BOOL || !static_cast<bool>(ok)) {
            return failure_message(reply, fallback);
        }
    }

    return {};
}

} // namespace crimson::match_runtime
