// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "possession.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <limits>

using namespace godot;

namespace crimson::match_runtime {
namespace {

constexpr const char *store_id = "possession"; // Reserved ID of the authoritative assignment store.
constexpr const char *validator_id = "possession.references"; // Watches the assignment, player, and character stores.

// Holds the detached values read from one player assignment.
struct Assignment {
    // The primary character ID, which can be empty for an unassigned player.
    String character;

    // Revision of this player's primary assignment in the active world epoch.
    int64_t revision = 0;
};

// Converts a store row into its primary character and revision.
Assignment read_assignment(const Variant &value) {
    if (value.get_type() != Variant::DICTIONARY) {
        return {};
    }
    const Dictionary row = value;
    return {row.get("character_id", String()), row.get("revision", 0)};
}

// Looks up a player in an assignment dictionary.
Assignment get_assignment(const Dictionary &assignments, const String &player) {
    return read_assignment(assignments.get(player, Variant()));
}

// Finds the player whose primary assignment references the character.
String find_controller(const Dictionary &assignments, const String &character) {
    if (character.is_empty()) {
        return {};
    }
    const Array players = assignments.keys();
    for (int64_t i = 0; i < players.size(); ++i) {
        const String player = players[i];
        if (get_assignment(assignments, player).character == character) {
            return player;
        }
    }
    return {};
}

// Checks that the stable ID refers to an existing game record.
bool has_record(const Dictionary &records, const String &id) {
    return records.get(id, Variant()).get_type() == Variant::DICTIONARY;
}

// Returns a preparation or validation rejection for the coordinator.
Dictionary rejected(const String &message) {
    Dictionary result;
    result["ok"] = false;
    result["message"] = message;
    return result;
}

// Returns the same outcome shape for errors raised before commit.
Dictionary request_error(const String &code, const String &message, int64_t revision) {
    Dictionary result = rejected(message);
    result["code"] = code;
    result["revision"] = revision;
    return result;
}

} // namespace

void CrimsonPossession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "coordinator", "players_store", "characters_store", "eligibility"),
            &CrimsonPossession::configure, DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("get_primary", "player_id"), &CrimsonPossession::get_primary);
    ClassDB::bind_method(D_METHOD("get_controller", "character_id"), &CrimsonPossession::get_controller);
    ClassDB::bind_method(D_METHOD("get_revision", "player_id"), &CrimsonPossession::get_revision);
    ClassDB::bind_method(D_METHOD("queue_primary_change", "transaction", "player_id", "character_id", "expected_revision"),
            &CrimsonPossession::queue_primary_change);
    ClassDB::bind_method(D_METHOD("try_set_primary", "player_id", "character_id", "expected_revision"),
            &CrimsonPossession::try_set_primary);

    // Standard object-bound Callables look up this object by ID. The coordinator
    // can reject a change safely if the owning Possession service is gone.
    ClassDB::bind_method(D_METHOD("_prepare_store", "current", "operations", "context"),
            &CrimsonPossession::prepare_store);
    ClassDB::bind_method(D_METHOD("_validate_references", "prepared", "context"),
            &CrimsonPossession::validate_references);
    ClassDB::bind_method(D_METHOD("_on_committed", "revision", "stores"),
            &CrimsonPossession::on_committed);
    ADD_SIGNAL(MethodInfo("primary_changed", PropertyInfo(Variant::STRING, "player_id"),
            PropertyInfo(Variant::STRING, "old_id"), PropertyInfo(Variant::STRING, "new_id"),
            PropertyInfo(Variant::INT, "revision")));
}

Error CrimsonPossession::configure(const Ref<CrimsonTransactionCoordinator> &new_coordinator,
        const String &new_players_store, const String &new_characters_store, const Callable &new_eligibility) {
    if (!on_owner_thread()) {
        return ERR_BUSY;
    }
    if (coordinator.is_valid()) {
        return ERR_ALREADY_IN_USE;
    }
    if (new_coordinator.is_null() || new_players_store == new_characters_store ||
            new_players_store == store_id || new_characters_store == store_id ||
            new_coordinator->get_store_revision(new_players_store) < 0 ||
            new_coordinator->get_store_revision(new_characters_store) < 0 ||
            (!new_eligibility.is_null() && !new_eligibility.is_valid())) {
        return ERR_INVALID_PARAMETER;
    }

    const Callable committed_callback(this, "_on_committed");
    const Error connected = new_coordinator->connect("committed", committed_callback);
    if (connected != OK) {
        return connected;
    }
    PackedStringArray watched_stores;
    watched_stores.append(store_id);
    watched_stores.append(new_players_store);
    watched_stores.append(new_characters_store);
    const Error registered = new_coordinator->register_store_with_validator(store_id, {},
            Callable(this, "_prepare_store"), validator_id, watched_stores, Callable(this, "_validate_references"));
    if (registered != OK) {
        new_coordinator->disconnect("committed", committed_callback);
        return registered;
    }
    coordinator = new_coordinator;
    players_store = new_players_store;
    characters_store = new_characters_store;
    eligibility = new_eligibility;
    return OK;
}

String CrimsonPossession::get_primary(const String &player_id) const {
    return on_owner_thread() && coordinator.is_valid() ?
            read_assignment(coordinator->read_store_value(store_id, player_id)).character : String();
}

String CrimsonPossession::get_controller(const String &character_id) const {
    return on_owner_thread() && coordinator.is_valid() ?
            find_controller(coordinator->read_store(store_id), character_id) : String();
}

int64_t CrimsonPossession::get_revision(const String &player_id) const {
    return on_owner_thread() && coordinator.is_valid() ?
            read_assignment(coordinator->read_store_value(store_id, player_id)).revision : 0;
}

Error CrimsonPossession::queue_primary_change(const Ref<CrimsonTransaction> &transaction,
        const String &player_id, const String &character_id, int64_t expected_revision) {
    if (!on_owner_thread()) {
        return ERR_BUSY;
    }
    if (coordinator.is_null()) {
        return ERR_UNCONFIGURED;
    }
    if (!coordinator->owns_transaction(transaction) || player_id.is_empty() || expected_revision < 0) {
        return ERR_INVALID_PARAMETER;
    }
    Dictionary operation;
    operation["player_id"] = player_id;
    operation["character_id"] = character_id;
    operation["expected_revision"] = expected_revision;
    return transaction->add_change(store_id, operation, coordinator->get_store_revision(store_id));
}

Dictionary CrimsonPossession::try_set_primary(const String &player_id, const String &character_id, int64_t expected_revision) {
    if (!on_owner_thread()) {
        return request_error("busy", "The possession service belongs to another thread.", -1);
    }
    if (coordinator.is_null()) {
        return request_error("unconfigured", "Configure possession before changing control.", 0);
    }
    const Ref<CrimsonPossession> keep_alive(this);
    Dictionary context;
    context["player_id"] = player_id;
    const Ref<CrimsonTransaction> transaction = coordinator->begin(context);
    if (transaction.is_null()) {
        return request_error("busy", "The coordinator cannot begin a possession transaction.", coordinator->get_revision());
    }
    if (queue_primary_change(transaction, player_id, character_id, expected_revision) != OK) {
        return request_error("invalid_change", "The possession change could not be queued.", coordinator->get_revision());
    }
    return coordinator->commit(transaction);
}

bool CrimsonPossession::on_owner_thread() const {
    return std::this_thread::get_id() == owner_thread;
}

Dictionary CrimsonPossession::prepare_store(const Dictionary &current, const Array &operations, const Dictionary &context) {
    if (!on_owner_thread() || coordinator.is_null()) {
        return rejected("The possession store is unavailable.");
    }
    const Ref<CrimsonPossession> keep_alive(this);
    const Dictionary players = coordinator->read_store(players_store);
    const Dictionary characters = coordinator->read_store(characters_store);
    Dictionary next = current.duplicate(true);

    for (int64_t i = 0; i < operations.size(); ++i) {
        const Variant raw = operations[i];
        if (raw.get_type() != Variant::DICTIONARY) {
            return rejected("Invalid possession operation.");
        }
        const Dictionary operation = raw;
        const Variant raw_player = operation.get("player_id", Variant());
        const Variant raw_character = operation.get("character_id", Variant());
        const Variant raw_revision = operation.get("expected_revision", Variant());
        if (operation.size() != 3 || raw_player.get_type() != Variant::STRING ||
                raw_character.get_type() != Variant::STRING || raw_revision.get_type() != Variant::INT) {
            return rejected("A possession operation requires a player, character, and expected revision.");
        }
        const String player = raw_player;
        const String character = raw_character;
        const int64_t expected_revision = raw_revision;
        if (player.is_empty() || !has_record(players, player)) {
            return rejected("The player record is missing.");
        }
        const Assignment previous = get_assignment(next, player);
        if (previous.revision != expected_revision) {
            return rejected("The possession revision has changed.");
        }
        if (!character.is_empty() && !has_record(characters, character)) {
            return rejected("The character record is missing.");
        }
        if (previous.character == character) {
            continue;
        }
        const String controller = find_controller(next, character);
        if (!controller.is_empty() && controller != player) {
            return rejected("The character already has a primary controller.");
        }
        if (!eligibility.is_null()) {
            if (!eligibility.is_valid()) {
                return rejected("The possession eligibility policy is unavailable.");
            }
            const Variant allowed = eligibility.call(player, previous.character, character, context.duplicate(true));
            if (allowed.get_type() != Variant::BOOL || !bool(allowed)) {
                return rejected("The possession eligibility policy rejected the change.");
            }
        }
        if (previous.revision == (std::numeric_limits<int64_t>::max)()) {
            return rejected("The possession revision limit has been reached.");
        }
        Dictionary assignment;
        assignment["character_id"] = character;
        assignment["revision"] = previous.revision + 1;
        next[player] = assignment;
    }

    Dictionary result;
    result["ok"] = true;
    result["state"] = next;
    return result;
}

Dictionary CrimsonPossession::validate_references(const Dictionary &prepared, const Dictionary &) const {
    if (!on_owner_thread() || coordinator.is_null()) {
        return rejected("The possession store is unavailable.");
    }
    // Prefer a prepared replacement and read committed state for untouched stores.
    const auto resulting_store = [&](const String &id) -> Dictionary {
        return prepared.has(id) ? Dictionary(prepared[id]) : coordinator->read_store(id);
    };
    const Dictionary assignments = resulting_store(store_id);
    const Dictionary players = resulting_store(players_store);
    const Dictionary characters = resulting_store(characters_store);

    Dictionary controllers;
    const Array player_ids = assignments.keys();
    for (int64_t i = 0; i < player_ids.size(); ++i) {
        const String player = player_ids[i];
        const Variant raw_row = assignments[player];
        if (player.is_empty() || raw_row.get_type() != Variant::DICTIONARY) {
            return rejected("An assignment must be keyed by a nonempty player ID and contain a Dictionary.");
        }
        const Dictionary row = raw_row;
        const Variant raw_character = row.get("character_id", Variant());
        const Variant raw_revision = row.get("revision", Variant());
        if (row.size() != 2 || raw_character.get_type() != Variant::STRING ||
                raw_revision.get_type() != Variant::INT || static_cast<int64_t>(raw_revision) <= 0) {
            return rejected("An assignment must contain a character ID and a positive integer revision.");
        }
        const String character = raw_character;
        if (character.is_empty()) {
            continue;
        }
        if (!has_record(players, player) || !has_record(characters, character)) {
            return rejected("Release the primary assignment in the same transaction that removes its player or character.");
        }
        if (controllers.has(character)) {
            return rejected("A character cannot have more than one primary controller.");
        }
        controllers[character] = player;
    }
    Dictionary result;
    result["ok"] = true;
    return result;
}

void CrimsonPossession::on_committed(int64_t revision, const PackedStringArray &stores) {
    if (!on_owner_thread() || coordinator.is_null() || coordinator->get_revision() != revision || !stores.has(store_id)) {
        return;
    }
    const int64_t store_revision = coordinator->get_store_revision(store_id);
    if (store_revision <= last_store_revision) {
        return;
    }
    const Ref<CrimsonPossession> keep_alive(this);
    const Dictionary previous = last_published;
    last_published = coordinator->read_store(store_id);
    last_store_revision = store_revision;
    Array players = last_published.keys();
    const Array previous_players = previous.keys();
    for (int64_t i = 0; i < previous_players.size(); ++i) {
        if (!last_published.has(previous_players[i])) {
            players.push_back(previous_players[i]);
        }
    }
    for (int64_t i = 0; i < players.size(); ++i) {
        const String player = players[i];
        const Assignment before = get_assignment(previous, player);
        const Assignment after = get_assignment(last_published, player);
        if (before.character != after.character) {
            emit_signal("primary_changed", player, before.character, after.character, after.revision);
        }
    }
}

} // namespace crimson::match_runtime
