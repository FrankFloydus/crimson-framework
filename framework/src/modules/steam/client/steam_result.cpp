// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace crimson::steam {
using namespace godot;

void SteamResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ok"), &SteamResult::is_ok);
    ClassDB::bind_method(D_METHOD("get_error"), &SteamResult::get_error);
    ClassDB::bind_method(D_METHOD("get_message"), &SteamResult::get_message);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ok", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "is_ok");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "error", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_error");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "message", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_message");
}

Ref<SteamResult> SteamResult::from_error(Error status, const String &diagnostic) {
    Ref<SteamResult> result;
    result.instantiate();
    result->set_outcome(status, diagnostic);
    return result;
}

bool SteamResult::is_ok() const {
    return error == OK;
}

Error SteamResult::get_error() const {
    return error;
}

String SteamResult::get_message() const {
    return message;
}

void SteamResult::set_outcome(Error status, const String &diagnostic) {
    error = status;
    message = status != OK && diagnostic.is_empty() ? UtilityFunctions::error_string(status) : diagnostic;
}

void SteamBoolResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamBoolResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamBoolResult> SteamBoolResult::create(Error status, bool confirmed, const String &diagnostic) {
    Ref<SteamBoolResult> result;
    result.instantiate();
    result->set_outcome(status, diagnostic);
    result->value = status == OK && confirmed;
    return result;
}

bool SteamBoolResult::get_value() const {
    return value;
}

void SteamStringResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamStringResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_value");
}

Ref<SteamStringResult> SteamStringResult::create(Error status, const String &text, const String &diagnostic) {
    Ref<SteamStringResult> result;
    result.instantiate();
    result->set_outcome(status, diagnostic);
    if (status == OK) {
        result->value = text;
    }
    return result;
}

String SteamStringResult::get_value() const {
    return value;
}

} // namespace crimson::steam
