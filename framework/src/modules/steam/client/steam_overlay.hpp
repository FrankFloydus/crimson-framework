// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Opens Steam-owned panes and observes process overlay focus from one client scope.
class SteamOverlay : public godot::RefCounted {
    GDCLASS(SteamOverlay, godot::RefCounted)

public:
    // Report whether Steam has loaded the overlay and allows the user to open it.
    bool is_ready() const;
    // Return the latest observed overlay activation state for the running app.
    bool is_active() const;
    // Request the current game's achievement pane.
    godot::Error open_achievements() const;
    // Request the browse-only store page. Zero selects the running AppID.
    godot::Error open_store(int64_t app_id = 0) const;
    // Request an individual Steam profile using a decimal SteamID64.
    godot::Error open_profile(const godot::String &steam_id) const;
    // Request a page in the overlay browser using an absolute HTTP or HTTPS URL.
    godot::Error open_web(const godot::String &url) const;
    // Request the pane for an existing server-started trade with this SteamID64.
    godot::Error open_trade(const godot::String &partner_steam_id) const;

    // Attach this facade to one live scope without retaining it.
    bool attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Unsubscribe and make this facade unavailable before the owning scope is released.
    void detach();

protected:
    // Register the overlay queries, requests, and activation signal.
    static void _bind_methods();

private:
    // Lock the exact live runtime only on Godot's main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Resolve a live request scope or report its availability error.
    godot::Error request_runtime(std::shared_ptr<SteamRuntime> &active) const;
    // Validate the target and issue a Steam user pane request.
    godot::Error open_user_pane(const char *pane, const godot::String &steam_id) const;

    // Process runtime used by this facade's original scope.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact liveness for the client that supplied this facade.
    std::weak_ptr<SteamClientScope> client_scope;
    // Opaque process observer ID, zero when detached.
    uint64_t activation_subscription = 0;
};

} // namespace crimson::steam
