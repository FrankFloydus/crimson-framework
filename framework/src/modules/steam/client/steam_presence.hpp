// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>

#include <functional>
#include <memory>

namespace crimson::integration {
class SteamOnlineDirectory;
}

namespace crimson::steam {

class SteamRuntime;
class SteamListing;
struct SteamClientScope;
struct SteamPresenceState;

// Owns the localized status and optional party grouping for one Steam client scope.
class SteamPresence : public godot::RefCounted {
    GDCLASS(SteamPresence, godot::RefCounted)

public:
    // Report whether the active Steam scope supports local presence reads and writes.
    bool is_available() const;
    // Apply a localized status and its optional party, replacing this owner's old status.
    godot::Error show(const godot::StringName &display_token,
            const godot::TypedDictionary<godot::String, godot::String> &values = {},
            const godot::String &party_id = {}, int64_t party_size = 0);
    // Remove only values that this owner successfully wrote and still controls.
    godot::Error clear();
    // Publish the exact eligible Game's lobby as a Steam Join Game route.
    godot::Error joinable(const godot::Ref<SteamListing> &listing);
    // Clear and suppress the current Game's route without clearing status keys.
    godot::Error clear_joinable();

protected:
    // Bind the standalone status facade and its defaults.
    static void _bind_methods();

private:
    friend class SteamClient;
    friend class SteamFriends;
    friend class integration::SteamOnlineDirectory;

    // Bind this fresh facade to a client start and its shared per-runtime status state.
    void configure(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &owner);
    // Retire owned keys before dropping the client's runtime lease.
    void close();
    // Return the scoped runtime for public main-thread requests.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Relinquish a tracked key after a successful explicit raw Friends write.
    static void forget_key(const std::shared_ptr<SteamRuntime> &active, const godot::String &key);
    // Check a key or value for nonempty C text within the SDK UTF-8 byte limit.
    static bool valid_text(const godot::CharString &text, int limit);

    // Weak runtime handle preserved through teardown for owned-key cleanup.
    std::weak_ptr<SteamRuntime> runtime;
    // Liveness guard invalidated before client teardown can reenter script.
    std::shared_ptr<SteamClientScope> scope;
    // Shared current owner and successfully published keys for this exact runtime.
    std::shared_ptr<SteamPresenceState> state;
    // Weak Game routing supplied by integration. A null listing requests removal.
    std::function<godot::Error(const godot::Ref<SteamListing> &)> game_join_route;
};

} // namespace crimson::steam
