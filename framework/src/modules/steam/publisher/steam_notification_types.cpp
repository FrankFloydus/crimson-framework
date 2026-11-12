// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_notification_types.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {

using namespace godot;

void SteamLocalizedText::_bind_methods() {
    ClassDB::bind_static_method("SteamLocalizedText", D_METHOD("create", "token", "variables"), &SteamLocalizedText::create);
    ClassDB::bind_method(D_METHOD("get_token"), &SteamLocalizedText::get_token);
    ClassDB::bind_method(D_METHOD("get_variables"), &SteamLocalizedText::get_variables);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "token", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_token");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "variables", PROPERTY_HINT_DICTIONARY_TYPE, "StringName;String", PROPERTY_USAGE_READ_ONLY),
            "", "get_variables");
}

Ref<SteamLocalizedText> SteamLocalizedText::create(const StringName &token, const SteamLocalizedText::Variables &variables) {
    Ref<SteamLocalizedText> value;
    value.instantiate();
    value->token = token;
    value->variables = Variables(variables.duplicate());
    value->variables.make_read_only();
    return value;
}

StringName SteamLocalizedText::get_token() const {
    return token;
}

SteamLocalizedText::Variables SteamLocalizedText::get_variables() const {
    return variables;
}

void SteamNotificationUserState::_bind_methods() {
    ClassDB::bind_static_method("SteamNotificationUserState", D_METHOD("create", "user", "state", "message"), &SteamNotificationUserState::create);
    ClassDB::bind_method(D_METHOD("get_user"), &SteamNotificationUserState::get_user);
    ClassDB::bind_method(D_METHOD("get_state"), &SteamNotificationUserState::get_state);
    ClassDB::bind_method(D_METHOD("get_message"), &SteamNotificationUserState::get_message);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "user", PROPERTY_HINT_RESOURCE_TYPE, "SteamIdentity", PROPERTY_USAGE_READ_ONLY),
            "", "get_user");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "state", PROPERTY_HINT_ENUM, "Waiting,Ready,Done", PROPERTY_USAGE_READ_ONLY),
            "", "get_state");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "message", PROPERTY_HINT_RESOURCE_TYPE, "SteamLocalizedText", PROPERTY_USAGE_READ_ONLY),
            "", "get_message");
    BIND_ENUM_CONSTANT(WAITING);
    BIND_ENUM_CONSTANT(READY);
    BIND_ENUM_CONSTANT(DONE);
}

Ref<SteamNotificationUserState> SteamNotificationUserState::create(const Ref<SteamIdentity> &user, State state, const Ref<SteamLocalizedText> &message) {
    Ref<SteamNotificationUserState> value;
    value.instantiate();
    value->user = user;
    value->state = state;
    value->message = message;
    return value;
}

Ref<SteamIdentity> SteamNotificationUserState::get_user() const {
    return user;
}

SteamNotificationUserState::State SteamNotificationUserState::get_state() const {
    return state;
}

Ref<SteamLocalizedText> SteamNotificationUserState::get_message() const {
    return message;
}

void SteamNotificationSessionSpec::_bind_methods() {
    ClassDB::bind_static_method("SteamNotificationSessionSpec", D_METHOD("create", "context", "title", "users"), &SteamNotificationSessionSpec::create);
    ClassDB::bind_method(D_METHOD("get_context"), &SteamNotificationSessionSpec::get_context);
    ClassDB::bind_method(D_METHOD("get_title"), &SteamNotificationSessionSpec::get_title);
    ClassDB::bind_method(D_METHOD("get_users"), &SteamNotificationSessionSpec::get_users);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "context", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_context");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "title", PROPERTY_HINT_RESOURCE_TYPE, "SteamLocalizedText", PROPERTY_USAGE_READ_ONLY),
            "", "get_title");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "users", PROPERTY_HINT_ARRAY_TYPE, "SteamNotificationUserState", PROPERTY_USAGE_READ_ONLY),
            "", "get_users");
}

Ref<SteamNotificationSessionSpec> SteamNotificationSessionSpec::create(const String &context, const Ref<SteamLocalizedText> &title, const TypedArray<SteamNotificationUserState> &users) {
    Ref<SteamNotificationSessionSpec> value;
    value.instantiate();
    value->context = context;
    value->title = title;
    value->users = TypedArray<SteamNotificationUserState>(users.duplicate());
    value->users.make_read_only();
    return value;
}

String SteamNotificationSessionSpec::get_context() const {
    return context;
}

Ref<SteamLocalizedText> SteamNotificationSessionSpec::get_title() const {
    return title;
}

TypedArray<SteamNotificationUserState> SteamNotificationSessionSpec::get_users() const {
    return users;
}

void SteamNotificationParticipant::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamNotificationParticipant::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_state"), &SteamNotificationParticipant::get_state);
    ClassDB::bind_method(D_METHOD("get_message"), &SteamNotificationParticipant::get_message);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "state", PROPERTY_HINT_ENUM, "Waiting,Ready,Done", PROPERTY_USAGE_READ_ONLY),
            "", "get_state");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "message", PROPERTY_HINT_RESOURCE_TYPE, "SteamLocalizedText", PROPERTY_USAGE_READ_ONLY),
            "", "get_message");
}

Ref<SteamNotificationParticipant> SteamNotificationParticipant::create(const String &steam_id, SteamNotificationUserState::State state, const Ref<SteamLocalizedText> &message) {
    Ref<SteamNotificationParticipant> value;
    value.instantiate();
    value->steam_id = steam_id;
    value->state = state;
    value->message = message;
    return value;
}

String SteamNotificationParticipant::get_steam_id() const {
    return steam_id;
}

SteamNotificationUserState::State SteamNotificationParticipant::get_state() const {
    return state;
}

Ref<SteamLocalizedText> SteamNotificationParticipant::get_message() const {
    return message;
}

void SteamNotificationSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_session_id"), &SteamNotificationSession::get_session_id);
    ClassDB::bind_method(D_METHOD("get_context"), &SteamNotificationSession::get_context);
    ClassDB::bind_method(D_METHOD("get_title"), &SteamNotificationSession::get_title);
    ClassDB::bind_method(D_METHOD("get_users"), &SteamNotificationSession::get_users);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "session_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_session_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "context", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_context");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "title", PROPERTY_HINT_RESOURCE_TYPE, "SteamLocalizedText", PROPERTY_USAGE_READ_ONLY),
            "", "get_title");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "users", PROPERTY_HINT_ARRAY_TYPE, "SteamNotificationParticipant", PROPERTY_USAGE_READ_ONLY),
            "", "get_users");
}

Ref<SteamNotificationSession> SteamNotificationSession::create(const String &session_id, const String &context, const Ref<SteamLocalizedText> &title, const TypedArray<SteamNotificationParticipant> &users) {
    Ref<SteamNotificationSession> value;
    value.instantiate();
    value->session_id = session_id;
    value->context = context;
    value->title = title;
    value->users = TypedArray<SteamNotificationParticipant>(users.duplicate());
    value->users.make_read_only();
    return value;
}

String SteamNotificationSession::get_session_id() const {
    return session_id;
}

String SteamNotificationSession::get_context() const {
    return context;
}

Ref<SteamLocalizedText> SteamNotificationSession::get_title() const {
    return title;
}

TypedArray<SteamNotificationParticipant> SteamNotificationSession::get_users() const {
    return users;
}

} // namespace crimson::steam
