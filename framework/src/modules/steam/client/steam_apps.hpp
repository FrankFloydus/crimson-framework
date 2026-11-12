// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Detached metadata for a single DLC entry in Steam's app catalog.
class SteamDlcInfo : public godot::RefCounted {
    GDCLASS(SteamDlcInfo, godot::RefCounted)

public:
    // Return the entry's Steam DLC AppID.
    int64_t get_app_id() const;
    // Return the catalog name copied from Steam.
    godot::String get_display_name() const;
    // Report Steam's store visibility flag for this entry.
    bool is_available() const;

protected:
    // Bind the read-only catalog entry for Godot consumers.
    static void _bind_methods();

private:
    friend class SteamApps;

    // Positive unsigned 32-bit AppID when copied from a valid Steam catalog row.
    int64_t app_id = 0;
    // Detached display name supplied by Steam.
    godot::String display_name;
    // Store visibility flag supplied by Steam for the DLC.
    bool available = false;
};

// Snapshot of the current download state for one optional DLC.
class SteamDlcProgress : public godot::RefCounted {
    GDCLASS(SteamDlcProgress, godot::RefCounted)

public:
    // Report whether Steam is actively downloading the DLC.
    bool is_active() const;
    // Return bytes downloaded for an active transfer or zero for an inactive one.
    int64_t get_downloaded_bytes() const;
    // Return the total byte count when known, or zero otherwise.
    int64_t get_total_bytes() const;

protected:
    // Bind the read-only transfer snapshot for Godot consumers.
    static void _bind_methods();

private:
    friend class SteamDlcProgressResult;

    // Whether the SDK reported an active download.
    bool active = false;
    // Nonnegative transferred byte count, zero when the transfer is inactive.
    int64_t downloaded_bytes = 0;
    // Nonnegative total byte count, zero when inactive or unknown.
    int64_t total_bytes = 0;
};

// Outcome of reading a bounded snapshot of the active app's Steam DLC catalog.
class SteamDlcListResult : public SteamResult {
    GDCLASS(SteamDlcListResult, SteamResult)

public:
    // Return a detached typed array of read-only catalog entries.
    godot::TypedArray<SteamDlcInfo> get_value() const;

protected:
    // Bind the typed catalog payload to Godot.
    static void _bind_methods();

private:
    friend class SteamApps;

    // Create a terminal catalog result with an empty payload on failure.
    static godot::Ref<SteamDlcListResult> create(godot::Error error, const godot::String &message,
            const godot::TypedArray<SteamDlcInfo> &items = {});

    // Detached read-only entries held for the lifetime of the result.
    godot::TypedArray<SteamDlcInfo> items;
};

// Outcome of querying the current download state for one DLC.
class SteamDlcProgressResult : public SteamResult {
    GDCLASS(SteamDlcProgressResult, SteamResult)

public:
    // Return the read-only progress snapshot, or null on failure.
    godot::Ref<SteamDlcProgress> get_value() const;

protected:
    // Bind the typed download progress payload to Godot.
    static void _bind_methods();

private:
    friend class SteamApps;

    // Create a terminal progress result and construct its payload on success.
    static godot::Ref<SteamDlcProgressResult> create(godot::Error error, const godot::String &message,
            bool active = false, int64_t downloaded_bytes = 0, int64_t total_bytes = 0);

    // Optional progress data captured when Steam answered the query.
    godot::Ref<SteamDlcProgress> value;
};

// Reads app and DLC metadata from a started Steam client scope.
class SteamApps : public godot::RefCounted {
    GDCLASS(SteamApps, godot::RefCounted)

public:
    // Report whether the scope still exposes the installed Apps interface.
    bool is_available() const;
    // Return the selected Steam game language or an empty string if unavailable.
    godot::String current_language() const;
    // Return the app's languages in Steam's order.
    godot::PackedStringArray available_languages() const;
    // Return the current named beta branch or an empty string for the default branch.
    godot::String beta_branch() const;
    // Return Steam's build ID, or zero if it has no build ID for this process.
    int64_t build_id() const;
    // Read the opaque decimal Game Notifications session ID supplied by Steam, if present.
    godot::String notification_session_id() const;
    // Check the local account's subscription to the specified positive AppID.
    bool is_subscribed_to_app(int64_t app_id) const;
    // Read Steam's local ownership hint for the supplied DLC AppID.
    bool owns_dlc(int64_t dlc_app_id) const;
    // Check that the local account owns and has installed the specified DLC.
    bool is_dlc_installed(int64_t app_id) const;
    // Read a bounded detached snapshot of the active app's Steam DLC catalog.
    godot::Ref<SteamDlcListResult> dlc() const;
    // Issue an optional DLC installation request for a locally owned DLC AppID.
    godot::Error install_dlc(int64_t dlc_app_id);
    // Issue an optional DLC removal request for the caller-selected DLC AppID.
    godot::Error uninstall_dlc(int64_t dlc_app_id);
    // Read a detached progress snapshot for a selected DLC, including an inactive state.
    godot::Ref<SteamDlcProgressResult> download_progress(int64_t dlc_app_id) const;

protected:
    // Register the typed Apps facade with Godot.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Attach the metadata facade and subscribe to installation events for this scope.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Cancel the exact installation observer and release references to the retired scope.
    void detach();
    // Acquire the owning scope's runtime only while it is active on the main thread.
    std::shared_ptr<SteamRuntime> active_runtime() const;

    // Observe this scope's runtime without extending its lifetime.
    std::weak_ptr<SteamRuntime> runtime;
    // Liveness of the exact client start that owns this service.
    std::weak_ptr<SteamClientScope> client_scope;
    // Manual-dispatch registration token for this scope's DLC installation events.
    uint64_t installed_subscription = 0;
    // Manual-dispatch registration for Steam URL query updates while the game is running.
    uint64_t notification_subscription = 0;
    // Last notification session observed so repeated Steam URL events do not reopen it.
    godot::String last_notification_session;
};

} // namespace crimson::steam
