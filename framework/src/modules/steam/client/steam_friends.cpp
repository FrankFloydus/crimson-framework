// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_friends.hpp"
#include "modules/steam/client/steam_presence.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <string>
#endif

namespace crimson::steam {
using namespace godot;

namespace {

// Detached Friends snapshots carry no editable instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

} // namespace

void SteamFriend::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamFriend::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_display_name"), &SteamFriend::get_display_name);
    ClassDB::bind_method(D_METHOD("is_online"), &SteamFriend::is_online);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name", PROPERTY_HINT_NONE, "", read_only), "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "online", PROPERTY_HINT_NONE, "", read_only), "", "is_online");
}

String SteamFriend::get_steam_id() const {
    return steam_id;
}

String SteamFriend::get_display_name() const {
    return display_name;
}

bool SteamFriend::is_online() const {
    return online;
}

void SteamFriendsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamFriendsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE,
                         SteamFriend::get_class_static(), read_only), "", "get_value");
}

TypedArray<SteamFriend> SteamFriendsResult::get_value() const {
    return value;
}

Ref<SteamFriendsResult> SteamFriendsResult::create(Error error, TypedArray<SteamFriend> friends) {
    Ref<SteamFriendsResult> result;
    result.instantiate();
    result->set_outcome(error);
    friends.make_read_only();
    result->value = friends;
    return result;
}

void SteamFriends::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamFriends::is_available);
    ClassDB::bind_method(D_METHOD("list"), &SteamFriends::list);
    ClassDB::bind_method(D_METHOD("rich_presence", "key"), &SteamFriends::rich_presence);
    ClassDB::bind_method(D_METHOD("set_rich_presence", "key", "value"), &SteamFriends::set_rich_presence);
    ClassDB::bind_method(D_METHOD("clear_rich_presence", "key"), &SteamFriends::clear_rich_presence);
}

Ref<SteamFriendsResult> SteamFriends::list() const {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return SteamFriendsResult::create(ERR_BUSY);
    }
    const auto active = runtime.lock();
    ISteamFriends *friends = active && active->is_healthy() ? active->get_friends() : nullptr;
    if (!friends) {
        return SteamFriendsResult::create(ERR_UNAVAILABLE);
    }
    const auto count = active->symbol<decltype(&SteamAPI_ISteamFriends_GetFriendCount)>("SteamAPI_ISteamFriends_GetFriendCount");
    const auto by_index = active->symbol<decltype(&SteamAPI_ISteamFriends_GetFriendByIndex)>(
            "SteamAPI_ISteamFriends_GetFriendByIndex");
    const auto name = active->symbol<decltype(&SteamAPI_ISteamFriends_GetFriendPersonaName)>(
            "SteamAPI_ISteamFriends_GetFriendPersonaName");
    const auto persona = active->symbol<decltype(&SteamAPI_ISteamFriends_GetFriendPersonaState)>(
            "SteamAPI_ISteamFriends_GetFriendPersonaState");
    if (!count || !by_index || !name || !persona) {
        return SteamFriendsResult::create(ERR_UNAVAILABLE);
    }

    const int total = count(friends, k_EFriendFlagImmediate);
    if (total < 0) {
        return SteamFriendsResult::create(ERR_UNAVAILABLE);
    }
    TypedArray<SteamFriend> entries;
    for (int index = 0; index < total; ++index) {
        const auto id = by_index(friends, index, k_EFriendFlagImmediate);
        const CSteamID user(id);
        if (!user.IsValid() || !user.BIndividualAccount()) {
            continue;
        }
        Ref<SteamFriend> entry;
        entry.instantiate();
        entry->steam_id = String::utf8(std::to_string(id).c_str());
        const char *cached_name = name(friends, id);
        entry->display_name = cached_name ? String::utf8(cached_name) : String();
        const EPersonaState status = persona(friends, id);
        entry->online = status > k_EPersonaStateOffline && status < k_EPersonaStateMax && status != k_EPersonaStateInvisible;
        entries.push_back(entry);
    }
    return SteamFriendsResult::create(OK, entries);
#else
    return SteamFriendsResult::create(ERR_UNAVAILABLE);
#endif
}

bool SteamFriends::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    return active && active->is_healthy() && active->get_friends();
#else
    return false;
#endif
}

String SteamFriends::rich_presence(const String &key) const {
#ifdef CRIMSON_WITH_STEAM
    const CharString name = key.utf8();
    const auto active = SteamRuntime::on_main_thread() ? runtime.lock() : nullptr;
    if (active && active->is_healthy() && active->get_friends() && SteamPresence::valid_text(name, k_cchMaxRichPresenceKeyLength)) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamFriends_GetFriendRichPresence)>(
                "SteamAPI_ISteamFriends_GetFriendRichPresence");
        const char *value = read ? read(active->get_friends(), active->get_user_id(), name.get_data()) : nullptr;
        return value ? String::utf8(value) : String();
    }
#endif
    return {};
}

Error SteamFriends::set_rich_presence(const String &key, const String &value) {
#ifdef CRIMSON_WITH_STEAM
    const CharString encoded = value.utf8();
    if (!SteamPresence::valid_text(encoded, k_cchMaxRichPresenceValueLength)) {
        return ERR_INVALID_PARAMETER;
    }
    return write_presence(key, encoded.get_data());
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamFriends::clear_rich_presence(const String &key) {
    return write_presence(key, nullptr);
}

Error SteamFriends::write_presence(const String &key, const char *value) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const CharString name = key.utf8();
    if (!SteamPresence::valid_text(name, k_cchMaxRichPresenceKeyLength) || key.to_lower() == "connect") {
        return ERR_INVALID_PARAMETER;
    }
    const auto active = runtime.lock();
    if (!active || !active->is_healthy() || !active->get_friends()) {
        return ERR_UNAVAILABLE;
    }
    const auto set = active->symbol<decltype(&SteamAPI_ISteamFriends_SetRichPresence)>(
            "SteamAPI_ISteamFriends_SetRichPresence");
    if (!set) {
        return ERR_UNAVAILABLE;
    }
    if (!set(active->get_friends(), name.get_data(), value)) {
        return FAILED;
    }
    SteamPresence::forget_key(active, key);
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

} // namespace crimson::steam
