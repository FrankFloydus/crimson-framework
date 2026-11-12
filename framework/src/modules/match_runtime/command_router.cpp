// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "command_router.hpp"

#include "transaction_coordinator.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::match_runtime {
namespace {

constexpr int64_t MAX_ID_LENGTH = 128;

// Check that a routing identifier is nonempty, bounded, and already trimmed.
bool valid_id(const String &id) {
    return !id.is_empty() && id.length() <= MAX_ID_LENGTH && id.strip_edges() == id;
}

// Build the result returned when a command cannot reach its handler.
Dictionary rejected(const char *code, const char *message) {
    Dictionary result;
    result["ok"] = false;
    result["code"] = code;
    result["message"] = message;
    return result;
}

// Validated routing fields extracted from an incoming command.
struct Intent {
    // Match targeted by the incoming intent.
    String match_id;
    // World version observed by the sender.
    int64_t world_epoch = 0;
    // Positive sequence in the player's current command stream.
    int64_t sequence = 0;
    // Registered operation requested by the player.
    String command_type;
    // Character targeted by a command that asserts a possession assignment.
    String character_id;
    // Assignment version observed by the sender.
    int64_t possession_revision = 0;
    // Whether the intent supplies both possession assertion fields.
    bool has_primary = false;
};

// Validate the command envelope and extract its routing fields.
bool read_intent(const Dictionary &source, Intent &intent) {
    const Variant match_id = source.get("match_id", Variant());
    const Variant world_epoch = source.get("world_epoch", Variant());
    const Variant sequence = source.get("sequence", Variant());
    const Variant command_type = source.get("type", Variant());
    if (match_id.get_type() != Variant::STRING || world_epoch.get_type() != Variant::INT ||
            sequence.get_type() != Variant::INT || command_type.get_type() != Variant::STRING ||
            source.get("payload", Variant()).get_type() != Variant::DICTIONARY) {
        return false;
    }
    intent.match_id = match_id;
    intent.world_epoch = world_epoch;
    intent.sequence = sequence;
    intent.command_type = command_type;
    if (!valid_id(intent.match_id) || intent.world_epoch <= 0 || intent.sequence <= 0 ||
            !valid_id(intent.command_type)) {
        return false;
    }

    const bool has_character = source.has("character_id");
    const bool has_revision = source.has("possession_revision");
    if (has_character != has_revision || source.size() != (has_character ? 7 : 5)) {
        return false;
    }
    if (has_character) {
        const Variant character_id = source["character_id"];
        const Variant revision = source["possession_revision"];
        if (character_id.get_type() != Variant::STRING || revision.get_type() != Variant::INT) {
            return false;
        }
        intent.character_id = character_id;
        intent.possession_revision = revision;
        intent.has_primary = true;
        return valid_id(intent.character_id) && intent.possession_revision >= 0;
    }
    return true;
}

// Prevents command dispatch and configuration from reentering a handler.
class DispatchScope {
public:
    // Set the dispatch guard before calling game code.
    explicit DispatchScope(bool &dispatching) : active(dispatching) { active = true; }
    // Clear the dispatch guard after game code returns.
    ~DispatchScope() { active = false; }

private:
    // Router dispatch flag held by this guard.
    bool &active;
};

} // namespace

void CommandRouter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "match_id", "world_epoch", "possessor"),
            &CommandRouter::configure);
    ClassDB::bind_method(D_METHOD("set_accepting_commands", "enabled"),
            &CommandRouter::set_accepting_commands);
    ClassDB::bind_method(D_METHOD("advance_world", "world_epoch"), &CommandRouter::advance_world);
    ClassDB::bind_method(D_METHOD("reset_player_sequence", "player_id"),
            &CommandRouter::reset_player_sequence);
    ClassDB::bind_method(D_METHOD("register_handler", "command_type", "handler", "requires_primary"),
            &CommandRouter::register_handler, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("submit", "player_id", "intent"), &CommandRouter::submit);
}

Error CommandRouter::configure(const String &new_match_id, int64_t new_world_epoch,
        const Ref<PlayerPossession> &new_possessor) {
    if (std::this_thread::get_id() != owner_thread || dispatching) {
        return ERR_BUSY;
    }
    if (world_epoch != 0) {
        return ERR_ALREADY_IN_USE;
    }
    if (!valid_id(new_match_id) || new_world_epoch <= 0 || new_possessor.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    match_id = new_match_id;
    world_epoch = new_world_epoch;
    possessor = new_possessor;
    return OK;
}

Error CommandRouter::check_configuration_call() const {
    if (std::this_thread::get_id() != owner_thread || dispatching) {
        return ERR_BUSY;
    }
    return world_epoch == 0 ? ERR_UNCONFIGURED : OK;
}

Error CommandRouter::set_accepting_commands(bool enabled) {
    const Error status = check_configuration_call();
    if (status != OK) {
        return status;
    }
    accepting_commands = enabled;
    return OK;
}

Error CommandRouter::advance_world(int64_t new_world_epoch) {
    const Error status = check_configuration_call();
    if (status != OK) {
        return status;
    }
    if (new_world_epoch <= world_epoch) {
        return ERR_INVALID_PARAMETER;
    }
    accepting_commands = false;
    world_epoch = new_world_epoch;
    player_sequences.clear();
    return OK;
}

Error CommandRouter::reset_player_sequence(const String &player_id) {
    const Error status = check_configuration_call();
    if (status != OK) {
        return status;
    }
    if (!valid_id(player_id)) {
        return ERR_INVALID_PARAMETER;
    }
    player_sequences.erase(player_id);
    return OK;
}

Error CommandRouter::register_handler(const String &command_type, const Callable &handler,
        bool requires_primary) {
    const Error status = check_configuration_call();
    if (status != OK) {
        return status;
    }
    if (!valid_id(command_type) || !handler.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    if (handlers.has(command_type)) {
        return ERR_ALREADY_EXISTS;
    }
    Dictionary entry;
    entry["callback"] = handler;
    entry["requires_primary"] = requires_primary;
    handlers[command_type] = entry;
    return OK;
}

Dictionary CommandRouter::submit(const String &player_id, const Dictionary &raw_intent) {
    const Error status = check_configuration_call();
    if (status == ERR_BUSY) {
        return rejected("Busy", "The command router is already dispatching or belongs to another thread.");
    }
    if (status != OK) {
        return rejected("Unconfigured", "Configure the command router for an active match first.");
    }
    if (!accepting_commands) {
        return rejected("CommandsClosed", "The match is not accepting commands.");
    }
    Intent intent;
    if (!valid_id(player_id) || !read_intent(raw_intent, intent)) {
        return rejected("InvalidIntent", "The player ID or command envelope is invalid.");
    }
    if (intent.match_id != match_id) {
        return rejected("WrongMatch", "The command targets a different match.");
    }
    if (intent.world_epoch != world_epoch) {
        return rejected("StaleWorld", "The command targets a different world epoch.");
    }
    if (!handlers.has(intent.command_type)) {
        return rejected("UnknownCommand", "The command type has no registered handler.");
    }

    const Dictionary entry = handlers[intent.command_type];
    if (bool(entry["requires_primary"]) && !intent.has_primary) {
        return rejected("InvalidIntent", "The command requires a character ID and possession revision.");
    }
    if (intent.sequence <= int64_t(player_sequences.get(player_id, int64_t(0)))) {
        return rejected("Replay", "The command sequence was already dispatched or superseded.");
    }
    if (intent.has_primary) {
        const String primary = possessor->get_primary(player_id);
        if (primary.is_empty()) {
            return rejected("NoPrimary", "The player has no primary character.");
        }
        if (intent.character_id != primary || intent.possession_revision != possessor->get_revision(player_id)) {
            return rejected("StalePossession", "The command targets a previous possession assignment.");
        }
    }

    Dictionary dispatched_intent;
    if (!copy_runtime_dto(raw_intent, dispatched_intent)) {
        return rejected("InvalidIntent", "The command payload must contain bounded plain data.");
    }
    const Callable handler = entry["callback"];
    if (!handler.is_valid()) {
        return rejected("HandlerUnavailable", "The registered handler is no longer available.");
    }

    // Consume the sequence before running game code, including when the handler rejects the command.
    const Ref<CommandRouter> keep_alive(this);
    const DispatchScope dispatch_scope(dispatching);
    player_sequences[player_id] = intent.sequence;
    const Variant raw_result = handler.call(player_id, dispatched_intent);
    Dictionary result;
    if (raw_result.get_type() != Variant::DICTIONARY || !copy_runtime_dto(raw_result, result) ||
            result.get("ok", Variant()).get_type() != Variant::BOOL ||
            result.get("code", "").get_type() != Variant::STRING ||
            result.get("message", "").get_type() != Variant::STRING) {
        return rejected("InvalidHandlerResult", "The handler must return plain data with a boolean ok field.");
    }
    if (!result.has("code")) {
        result["code"] = bool(result["ok"]) ? "Ok" : "Rejected";
    }
    if (!result.has("message")) {
        result["message"] = "";
    }
    return result;
}

} // namespace crimson::match_runtime
