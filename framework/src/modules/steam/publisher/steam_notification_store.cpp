// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_notification_store.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {

using namespace godot;

void SteamNotificationClaim::_bind_methods() {
    ClassDB::bind_static_method("SteamNotificationClaim", D_METHOD("create", "status", "session_id"),
            &SteamNotificationClaim::create, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_status"), &SteamNotificationClaim::get_status);
    ClassDB::bind_method(D_METHOD("get_session_id"), &SteamNotificationClaim::get_session_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "status", PROPERTY_HINT_ENUM, "Claimed,In Flight,Committed,Conflict,Denied",
                         PROPERTY_USAGE_READ_ONLY), "", "get_status");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "session_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_session_id");
    BIND_ENUM_CONSTANT(CLAIMED);
    BIND_ENUM_CONSTANT(IN_FLIGHT);
    BIND_ENUM_CONSTANT(COMMITTED);
    BIND_ENUM_CONSTANT(CONFLICT);
    BIND_ENUM_CONSTANT(DENIED);
}

Ref<SteamNotificationClaim> SteamNotificationClaim::create(Status status, const String &session_id) {
    Ref<SteamNotificationClaim> result;
    result.instantiate();
    result->status = status;
    result->session_id = session_id;
    return result;
}

SteamNotificationClaim::Status SteamNotificationClaim::get_status() const {
    return status;
}

String SteamNotificationClaim::get_session_id() const {
    return session_id;
}

void SteamNotificationStore::_bind_methods() {
    GDVIRTUAL_BIND(_claim, "app_id", "key", "fingerprint", "action", "target_id");
    GDVIRTUAL_BIND(_commit, "app_id", "key", "fingerprint", "action", "provider_id");
    GDVIRTUAL_BIND(_release_unsent, "app_id", "key", "fingerprint", "action");
    GDVIRTUAL_BIND(_owns_session, "app_id", "session_id");
    GDVIRTUAL_BIND(_claim_launch_opt_in, "app_id", "steam_id", "launch_id");
    GDVIRTUAL_BIND(_release_unsent_launch_opt_in, "app_id", "steam_id", "launch_id");
    BIND_ENUM_CONSTANT(CREATE);
    BIND_ENUM_CONSTANT(UPDATE);
    BIND_ENUM_CONSTANT(REMOVE);
}

Ref<SteamNotificationClaim> SteamNotificationStore::claim(int64_t app_id, const String &key,
        const String &fingerprint, Action action, const String &target_id) {
    Ref<SteamNotificationClaim> result;
    GDVIRTUAL_CALL(_claim, app_id, key, fingerprint, static_cast<int64_t>(action), target_id, result);
    return result;
}

bool SteamNotificationStore::commit(int64_t app_id, const String &key, const String &fingerprint, Action action,
        const String &provider_id) {
    bool committed = false;
    return GDVIRTUAL_CALL(_commit, app_id, key, fingerprint, static_cast<int64_t>(action), provider_id, committed) && committed;
}

bool SteamNotificationStore::release_unsent(int64_t app_id, const String &key, const String &fingerprint, Action action) {
    bool released = false;
    return GDVIRTUAL_CALL(_release_unsent, app_id, key, fingerprint, static_cast<int64_t>(action), released) && released;
}

bool SteamNotificationStore::owns_session(int64_t app_id, const String &session_id) {
    bool owned = false;
    return GDVIRTUAL_CALL(_owns_session, app_id, session_id, owned) && owned;
}

Error SteamNotificationStore::claim_launch_opt_in(int64_t app_id, const String &steam_id, const String &launch_id) {
    Error result = ERR_UNCONFIGURED;
    return GDVIRTUAL_CALL(_claim_launch_opt_in, app_id, steam_id, launch_id, result) ? result : ERR_UNCONFIGURED;
}

bool SteamNotificationStore::release_unsent_launch_opt_in(int64_t app_id, const String &steam_id, const String &launch_id) {
    bool released = false;
    return GDVIRTUAL_CALL(_release_unsent_launch_opt_in, app_id, steam_id, launch_id, released) && released;
}

} // namespace crimson::steam
