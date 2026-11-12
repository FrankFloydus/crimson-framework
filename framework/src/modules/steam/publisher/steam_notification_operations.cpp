// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_game_notifications.hpp"
#include "modules/steam/publisher/steam_publisher_transport.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

namespace crimson::steam {

using namespace godot;

void SteamNotificationSessionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamNotificationSessionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamNotificationSession",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamNotificationSession> SteamNotificationSessionResult::get_value() const {
    return value;
}

Ref<SteamNotificationSessionResult> SteamNotificationSessionResult::create(Error error, const String &message, const Ref<SteamNotificationSession> &snapshot) {
    Ref<SteamNotificationSessionResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = snapshot;
    return result;
}

void SteamNotificationSessionsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamNotificationSessionsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, "SteamNotificationSession",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

TypedArray<SteamNotificationSession> SteamNotificationSessionsResult::get_value() const {
    return value;
}

Ref<SteamNotificationSessionsResult> SteamNotificationSessionsResult::create(Error error, const String &message, const TypedArray<SteamNotificationSession> &snapshot) {
    Ref<SteamNotificationSessionsResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = snapshot;
    result->value.make_read_only();
    return result;
}

void SteamNotificationBoolOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamNotificationBoolOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamNotificationBoolOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamNotificationBoolOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamNotificationBoolOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamBoolResult")));
}

bool SteamNotificationBoolOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamBoolResult> SteamNotificationBoolOperation::result() const {
    return outcome;
}

Variant SteamNotificationBoolOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamNotificationBoolOperation::cancel() {
    if (!publisher_on_main_thread()) {
        return ERR_BUSY;
    }
    if (!pending) {
        return ERR_DOES_NOT_EXIST;
    }
    finish(SteamBoolResult::create(ERR_SKIP));
    return OK;
}

void SteamNotificationBoolOperation::finish(const Ref<SteamBoolResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamNotificationBoolOperation> keep_alive(this);
    pending = false;
    outcome = result;
    emit_signal("completed", outcome);
}

void SteamNotificationSessionOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamNotificationSessionOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamNotificationSessionOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamNotificationSessionOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamNotificationSessionOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamNotificationSessionResult")));
}

bool SteamNotificationSessionOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamNotificationSessionResult> SteamNotificationSessionOperation::result() const {
    return outcome;
}

Variant SteamNotificationSessionOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamNotificationSessionOperation::cancel() {
    if (!publisher_on_main_thread()) {
        return ERR_BUSY;
    }
    if (!pending) {
        return ERR_DOES_NOT_EXIST;
    }
    finish(SteamNotificationSessionResult::create(ERR_SKIP));
    return OK;
}

void SteamNotificationSessionOperation::finish(const Ref<SteamNotificationSessionResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamNotificationSessionOperation> keep_alive(this);
    pending = false;
    outcome = result;
    emit_signal("completed", outcome);
}

void SteamNotificationSessionsOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamNotificationSessionsOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamNotificationSessionsOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamNotificationSessionsOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamNotificationSessionsOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamNotificationSessionsResult")));
}

bool SteamNotificationSessionsOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamNotificationSessionsResult> SteamNotificationSessionsOperation::result() const {
    return outcome;
}

Variant SteamNotificationSessionsOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

Error SteamNotificationSessionsOperation::cancel() {
    if (!publisher_on_main_thread()) {
        return ERR_BUSY;
    }
    if (!pending) {
        return ERR_DOES_NOT_EXIST;
    }
    finish(SteamNotificationSessionsResult::create(ERR_SKIP));
    return OK;
}

void SteamNotificationSessionsOperation::finish(const Ref<SteamNotificationSessionsResult> &result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamNotificationSessionsOperation> keep_alive(this);
    pending = false;
    outcome = result;
    emit_signal("completed", outcome);
}

} // namespace crimson::steam
