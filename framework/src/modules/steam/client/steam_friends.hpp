// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <memory>

namespace crimson::steam {

class SteamRuntime;

// Detached information about an immediate friend in the signed-in player's cache.
class SteamFriend : public godot::RefCounted {
    GDCLASS(SteamFriend, godot::RefCounted)

public:
    // Return the friend's full SteamID64 as a decimal string.
    godot::String get_steam_id() const;
    // Return the cached Steam persona name.
    godot::String get_display_name() const;
    // Report whether Steam currently presents the friend as online.
    bool is_online() const;

protected:
    // Bind the read-only snapshot fields.
    static void _bind_methods();

private:
    friend class SteamFriends;

    // Full decimal SteamID64 copied from the friend record.
    godot::String steam_id;
    // Detached persona name, empty if Steam supplies none.
    godot::String display_name;
    // Last online state reported by the local Steam cache.
    bool online = false;
};

// Terminal outcome and detached collection from one local Friends read.
class SteamFriendsResult : public SteamResult {
    GDCLASS(SteamFriendsResult, SteamResult)

public:
    // Return the read-only typed friend array, empty if Steam reports none or the read fails.
    godot::TypedArray<SteamFriend> get_value() const;

protected:
    // Bind the typed collection payload.
    static void _bind_methods();

private:
    friend class SteamFriends;

    // Create a terminal result from an owned detached collection.
    static godot::Ref<SteamFriendsResult> create(godot::Error error, godot::TypedArray<SteamFriend> friends = {});

    // Snapshot collection sealed before exposure to script.
    godot::TypedArray<SteamFriend> value;
};

// Updates and queries Steam Rich Presence for the signed-in account.
class SteamFriends : public godot::RefCounted {
    GDCLASS(SteamFriends, godot::RefCounted)

public:
    // Report whether the scope still exposes the installed Friends interface.
    bool is_available() const;
    // Snapshot immediate friends from the local Steam cache without requesting user data.
    godot::Ref<SteamFriendsResult> list() const;
    // Read the signed-in account's cached value for a Rich Presence key.
    godot::String rich_presence(const godot::String &key) const;
    // Set one nonempty key and value after validating the SDK byte limits.
    godot::Error set_rich_presence(const godot::String &key, const godot::String &value);
    // Remove one key while preserving all other Rich Presence values.
    godot::Error clear_rich_presence(const godot::String &key);

protected:
    // Register the typed Friends facade with Godot.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Observe this scope's runtime without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;

    // Submit one Rich Presence write. A null value clears the key.
    godot::Error write_presence(const godot::String &key, const char *value);
};

} // namespace crimson::steam
