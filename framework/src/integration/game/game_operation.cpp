// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/game/game_operation.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace crimson::integration {
using namespace godot;
namespace {

// Describe only bounded identifier-shaped input in logs, without emitting control characters.
String describe_id(const String &id) {
    if (id.is_empty()) {
        return "<empty>";
    }
    if (id.length() > 64) {
        return "<invalid>";
    }
    for (int64_t index = 0; index < id.length(); ++index) {
        const char32_t value = id[index];
        const bool alnum = (value >= U'a' && value <= U'z') || (value >= U'A' && value <= U'Z') ||
                (value >= U'0' && value <= U'9');
        if (!alnum && value != U'.' && value != U'_' && value != U'-') {
            return "<invalid>";
        }
    }
    return id;
}

} // namespace

void GameResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ok"), &GameResult::is_ok);
    ClassDB::bind_method(D_METHOD("get_code"), &GameResult::get_code);
    ClassDB::bind_method(D_METHOD("get_message"), &GameResult::get_message);
    ClassDB::bind_method(D_METHOD("get_reason"), &GameResult::get_reason);
    ClassDB::bind_method(D_METHOD("get_listing_id"), &GameResult::get_listing_id);
    ClassDB::bind_method(D_METHOD("get_world_id"), &GameResult::get_world_id);
    ClassDB::bind_method(D_METHOD("get_world_epoch"), &GameResult::get_world_epoch);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ok"), "", "is_ok");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "code"), "", "get_code");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "message"), "", "get_message");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "reason"), "", "get_reason");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "listing_id"), "", "get_listing_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "world_id"), "", "get_world_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "world_epoch"), "", "get_world_epoch");
}

Ref<GameResult> GameResult::from_dictionary(const Dictionary &value) {
    const bool success = value.get("ok", false);
    const String code = value.get("code", success ? "Ok" : "Failed");
    const auto result = make(success, code, value.get("message", String()));
    result->reason = value.get("reason", StringName());
    result->listing_id = value.get("listing_id", String());
    result->world_id = value.get("world_id", StringName());
    result->world_epoch = value.get("world_epoch", int64_t(0));
    return result;
}

Ref<GameResult> GameResult::make(bool ok, const String &code, const String &message) {
    Ref<GameResult> result;
    result.instantiate();
    result->ok = ok;
    result->code = code;
    result->message = message.is_empty() ?
            (ok ? String("Ready.") : String("The game action failed (") + code + ").") : message;
    return result;
}

bool GameResult::is_ok() const { return ok; }
String GameResult::get_code() const { return code; }
String GameResult::get_message() const { return message; }
StringName GameResult::get_reason() const { return reason; }
String GameResult::get_listing_id() const { return listing_id; }
StringName GameResult::get_world_id() const { return world_id; }
int64_t GameResult::get_world_epoch() const { return world_epoch; }

void GameOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &GameOperation::is_finished);
    ClassDB::bind_method(D_METHOD("get_result"), &GameOperation::get_result);
    ClassDB::bind_method(D_METHOD("wait"), &GameOperation::wait);
    ClassDB::bind_method(D_METHOD("wait_result"), &GameOperation::wait_result);
    ClassDB::bind_method(D_METHOD("get_typed_result"), &GameOperation::get_typed_result);

    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "result", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY),
            "", "get_result");
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
    ADD_SIGNAL(MethodInfo("completed_typed",
            PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "GameResult")));
}

bool GameOperation::is_finished() const {
    return finished;
}

Dictionary GameOperation::get_result() const {
    return result.duplicate(true);
}

Variant GameOperation::wait() {
    if (finished) {
        return get_result();
    }
    return Signal(this, "completed");
}

Variant GameOperation::wait_result() {
    return finished ? Variant(typed_result) : Variant(Signal(this, "completed_typed"));
}

Ref<GameResult> GameOperation::get_typed_result() const {
    return typed_result;
}

Ref<GameOperation> GameOperation::for_travel(const StringName &world) {
    Ref<GameOperation> operation;
    operation.instantiate();
    operation->diagnostic = Diagnostic::WorldTravel;
    operation->diagnostic_target = "world '" + describe_id(String(world)) + String("'");
    return operation;
}

Ref<GameOperation> GameOperation::for_checkpoint(bool restore, const String &profile, const String &slot) {
    Ref<GameOperation> operation;
    operation.instantiate();
    operation->diagnostic = restore ? Diagnostic::Restore : Diagnostic::Save;
    operation->diagnostic_target = "profile '" + describe_id(profile) + String("', slot '") + describe_id(slot) + String("'");
    return operation;
}

bool GameOperation::is_checkpoint_save() const {
    return diagnostic == Diagnostic::Save;
}

void GameOperation::finish(const Dictionary &p_result) {
    if (finished) {
        return;
    }

    // A completion listener can release the last external reference to this operation.
    const Ref<GameOperation> keep_alive(this);
    result = p_result.duplicate(true);
    finished = true;
    if (diagnostic != Diagnostic::None) {
        report_diagnostic();
    }
    typed_result = GameResult::from_dictionary(result);
    emit_signal("completed", get_result());
    emit_signal("completed_typed", typed_result);
}

void GameOperation::report_diagnostic() {
    const bool ok = result.get("ok", false);
    String code = result.get("code", "");
    const bool travel = diagnostic == Diagnostic::WorldTravel;
    const String action = travel ? "World travel" : diagnostic == Diagnostic::Save ? "Checkpoint save" : "Checkpoint restore";
    if (code.is_empty()) {
        code = ok ? "Ok" : travel ? "TravelFailed" : diagnostic == Diagnostic::Save ? "SaveFailed" : "RestoreFailed";
        result["code"] = code;
    }
    String message = result.get("message", "");
    if (message.strip_edges().is_empty()) {
        message = ok ? action + String(" completed.") :
                       action + String(" failed. See error code '") + code + String("'.");
        result["message"] = message;
    }
    if (diagnostic == Diagnostic::Save && code == "CommitUnknown" && result.has("value")) {
        message += " Inspect the exact generation in the result before retrying the save.";
        result["message"] = message;
    }
    const String warning = result.get("warning", "");
    if (ok && !warning.is_empty()) {
        UtilityFunctions::printerr("[Crimson] ", action, " (", diagnostic_target, "): ", warning);
    } else if (!ok) {
        if (code == "Cancelled" || code == "Superseded" || code == "SessionEnded") {
            UtilityFunctions::print("[Crimson] ", action, " (", diagnostic_target, ", ", code, "): ", message);
        } else {
            UtilityFunctions::printerr("[Crimson] ", action, " (", diagnostic_target, ", ", code, "): ", message);
        }
    }
}

} // namespace crimson::integration
