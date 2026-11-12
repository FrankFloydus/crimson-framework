// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_presence.hpp"
#include "modules/steam/client/steam_client_scope.hpp"
#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <cstring>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"

#include <limits>
#include <map>
#include <set>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Preserves the exact spelling and cached value of a successfully issued Steam key.
struct PresenceKey {
    // Key spelling passed to Steam.
    String name;
    // Value owned by the current managed status.
    String value;
};

// Keys indexed by their normalized spelling to prevent case aliases within a managed status.
using PresenceKeys = std::map<String, PresenceKey>;

// Borrows the Friends interface while the caller holds the exact Steam runtime.
struct PresenceAccess {
    // Resolve both mutation and observation before a managed status can be written.
    explicit PresenceAccess(const std::shared_ptr<SteamRuntime> &active) :
            friends(active ? active->get_friends() : nullptr), user(active ? active->get_user_id() : 0) {
        if (friends) {
            get_value = active->symbol<decltype(get_value)>("SteamAPI_ISteamFriends_GetFriendRichPresence");
            set_value = active->symbol<decltype(set_value)>("SteamAPI_ISteamFriends_SetRichPresence");
            get_count = active->symbol<decltype(get_count)>("SteamAPI_ISteamFriends_GetFriendRichPresenceKeyCount");
            by_index = active->symbol<decltype(by_index)>("SteamAPI_ISteamFriends_GetFriendRichPresenceKeyByIndex");
        }
    }

    // Report whether Steam supports scoped writes and cached value comparisons.
    bool can_write() const {
        return friends && user && get_value && set_value;
    }

    // Report whether Steam can additionally enumerate the local key budget.
    bool can_show() const {
        return can_write() && get_count && by_index;
    }

    // Copy the cached value before another Steam call can invalidate the borrowed text.
    String read(const String &key) const {
        const CharString name = key.utf8();
        const char *value = get_value(friends, user, name.get_data());
        return value ? String::utf8(value) : String();
    }

    // Submit one key to Steam. A null value removes only this key.
    bool write(const String &key, const String *value) const {
        const CharString name = key.utf8();
        const CharString text = value ? value->utf8() : CharString();
        return set_value(friends, name.get_data(), value ? text.get_data() : nullptr);
    }

    // Borrowed Steam Friends interface from this exact runtime.
    ISteamFriends *friends;
    // Local SteamID64 used for cached presence reads.
    uint64_t user;
    // Reads a cached Rich Presence value.
    decltype(&SteamAPI_ISteamFriends_GetFriendRichPresence) get_value = nullptr;
    // Sets or removes one Rich Presence value.
    decltype(&SteamAPI_ISteamFriends_SetRichPresence) set_value = nullptr;
    // Counts the local account's Rich Presence keys.
    decltype(&SteamAPI_ISteamFriends_GetFriendRichPresenceKeyCount) get_count = nullptr;
    // Reads a local Rich Presence key by its cache index.
    decltype(&SteamAPI_ISteamFriends_GetFriendRichPresenceKeyByIndex) by_index = nullptr;
};

// Weak rendezvous for scopes attached to the one native Steam client runtime.
std::weak_ptr<SteamPresenceState> current_presence;

} // namespace

// Tracks one exact managed status owner and its successfully issued values.
struct SteamPresenceState {
    // Runtime identity checked whenever another Steam scope attaches.
    std::weak_ptr<SteamRuntime> runtime;
    // Facade instance allowed to replace or clear the current managed status.
    ObjectID owner;
    // Successfully issued keys that remain owned by that exact facade.
    PresenceKeys keys;
};

namespace {

// Test whether an incoming replacement key belongs to Steam or the Game provider.
bool reserved_substitution(const String &key) {
    return key == "connect" || key == "status" || key.begins_with("steam_");
}

// Count the union of remaining cached keys and the desired status before changing Steam state.
Error check_key_budget(const PresenceAccess &access, const PresenceKeys &owned, const PresenceKeys &desired) {
    const int count = access.get_count(access.friends, access.user);
    if (count < 0 || count > k_cchMaxRichPresenceKeys) {
        return FAILED;
    }
    std::set<String> projected;
    for (int index = 0; index < count; ++index) {
        const char *name = access.by_index(access.friends, access.user, index);
        if (!name || !*name) {
            return FAILED;
        }
        projected.insert(String::utf8(name).to_lower());
    }
    for (const auto &entry : owned) {
        if (desired.count(entry.first) == 0 && access.read(entry.second.name) == entry.second.value) {
            projected.erase(entry.first);
        }
    }
    for (const auto &entry : desired) {
        projected.insert(entry.first);
    }
    return projected.size() <= k_cchMaxRichPresenceKeys ? OK : ERR_BUSY;
}

// Clear tracked keys that still match Steam's cache, preserving any values replaced elsewhere.
Error clear_keys(const PresenceAccess &access, SteamPresenceState &state, const PresenceKeys &keep) {
    Error result = OK;
    auto entry = state.keys.begin();
    while (entry != state.keys.end()) {
        if (keep.count(entry->first) != 0) {
            ++entry;
            continue;
        }
        const PresenceKey &key = entry->second;
        if (access.read(key.name) == key.value && !access.write(key.name, nullptr)) {
            result = FAILED;
            ++entry;
        } else {
            entry = state.keys.erase(entry);
        }
    }
    if (state.keys.empty()) {
        state.owner = ObjectID();
    }
    return result;
}

} // namespace
#endif

void SteamPresence::_bind_methods() {
    ClassDB::bind_method(D_METHOD("joinable", "listing"), &SteamPresence::joinable);
    ClassDB::bind_method(D_METHOD("clear_joinable"), &SteamPresence::clear_joinable);
    const TypedDictionary<String, String> no_values;
    ClassDB::bind_method(D_METHOD("is_available"), &SteamPresence::is_available);
    ClassDB::bind_method(D_METHOD("show", "display_token", "values", "party_id", "party_size"), &SteamPresence::show,
            DEFVAL(no_values), DEFVAL(String()), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("clear"), &SteamPresence::clear);
}

bool SteamPresence::valid_text(const CharString &text, int limit) {
    const int64_t length = text.length();
    return length > 0 && length < limit &&
            std::memchr(text.get_data(), 0, static_cast<size_t>(length)) == nullptr;
}

void SteamPresence::configure(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &owner) {
#ifdef CRIMSON_WITH_STEAM
    runtime = active;
    scope = owner;
    state = current_presence.lock();
    if (!state || state->runtime.lock() != active) {
        state = std::make_shared<SteamPresenceState>();
        state->runtime = active;
        current_presence = state;
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamPresence::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && scope && scope->active) {
        return runtime.lock();
    }
#endif
    return {};
}

bool SteamPresence::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    return active && state && PresenceAccess(active).can_show();
#else
    return false;
#endif
}

Error SteamPresence::show(const StringName &display_token, const TypedDictionary<String, String> &values,
        const String &party_id, int64_t party_size) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto active = active_runtime();
    const PresenceAccess access(active);
    if (!state || !access.can_show()) {
        return ERR_UNAVAILABLE;
    }
    const String token(display_token);
    const bool grouped = !party_id.is_empty();
    if (!token.begins_with("#") || token.length() <= 1 || !valid_text(token.utf8(), k_cchMaxRichPresenceValueLength) ||
            (grouped && (party_size <= 0 || party_size > (std::numeric_limits<int32_t>::max)() ||
                                !valid_text(party_id.utf8(), k_cchMaxRichPresenceValueLength))) ||
            (!grouped && party_size != 0) || values.size() > k_cchMaxRichPresenceKeys - (grouped ? 3 : 1)) {
        return ERR_INVALID_PARAMETER;
    }

    PresenceKeys desired;
    const Array keys = values.keys();
    for (int64_t index = 0; index < keys.size(); ++index) {
        const String key = keys[index];
        const String normalized = key.to_lower();
        const String value = values[key];
        if (reserved_substitution(normalized) || !valid_text(key.utf8(), k_cchMaxRichPresenceKeyLength) ||
                !valid_text(value.utf8(), k_cchMaxRichPresenceValueLength) ||
                !desired.emplace(normalized, PresenceKey{key, value}).second) {
            return ERR_INVALID_PARAMETER;
        }
    }
    if (grouped) {
        desired.emplace("steam_player_group", PresenceKey{"steam_player_group", party_id});
        desired.emplace("steam_player_group_size", PresenceKey{"steam_player_group_size", String::num_int64(party_size)});
    }
    const String display_key = "steam_display";
    desired.emplace(display_key, PresenceKey{display_key, token});

    const ObjectID id(get_instance_id());
    if (state->owner.is_valid() && state->owner != id) {
        return ERR_BUSY;
    }
    const Error budget = check_key_budget(access, state->keys, desired);
    if (budget != OK) {
        return budget;
    }
    if (clear_keys(access, *state, desired) != OK) {
        return FAILED;
    }

    // Publish the display token after substitutions and grouping have been accepted.
    const auto publish = [&access, &id, this](const String &normalized, const PresenceKey &value) {
        const auto tracked = state->keys.find(normalized);
        const String spelling = tracked == state->keys.end() ? value.name : tracked->second.name;
        if (!access.write(spelling, &value.value)) {
            return false;
        }
        state->owner = id;
        state->keys[normalized] = {spelling, value.value};
        return true;
    };
    for (const auto &entry : desired) {
        if (entry.first != display_key && !publish(entry.first, entry.second)) {
            return FAILED;
        }
    }
    return publish(display_key, desired.at(display_key)) ? OK : FAILED;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamPresence::clear() {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    const auto active = active_runtime();
    const PresenceAccess access(active);
    if (!state || !access.can_write()) {
        return ERR_UNAVAILABLE;
    }
    return state->owner == ObjectID(get_instance_id()) ? clear_keys(access, *state, {}) : OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamPresence::close() {
    game_join_route = {};
#ifdef CRIMSON_WITH_STEAM
    if (state && state->owner == ObjectID(get_instance_id())) {
        const auto active = runtime.lock();
        const PresenceAccess access(active);
        if (access.can_write()) {
            clear_keys(access, *state, {});
        }
        state->keys.clear();
        state->owner = ObjectID();
    }
    state.reset();
    scope.reset();
    runtime.reset();
#endif
}

Error SteamPresence::joinable(const Ref<SteamListing> &listing) {
    if (listing.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    return is_available() && game_join_route ? game_join_route(listing) : ERR_UNAVAILABLE;
}

Error SteamPresence::clear_joinable() {
    return is_available() && game_join_route ? game_join_route({}) : ERR_UNAVAILABLE;
}

void SteamPresence::forget_key(const std::shared_ptr<SteamRuntime> &active, const String &key) {
#ifdef CRIMSON_WITH_STEAM
    const auto state = current_presence.lock();
    if (state && state->runtime.lock() == active) {
        state->keys.erase(key.to_lower());
        if (state->keys.empty()) {
            state->owner = ObjectID();
        }
    }
#endif
}

} // namespace crimson::steam
