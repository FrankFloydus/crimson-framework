// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#ifdef CRIMSON_WITH_STEAM

#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <array>
#include <map>
#include <memory>
#include <optional>

namespace crimson::steam {

// Native lease shared by the exact client scopes that started Steam Input.
class SteamInputSession {
public:
    // Distinguish the three independent Steam name-to-handle lookups.
    enum class NameKind { Set, Digital, Analog };

    // Identity and observed binding revision for one physical connection.
    struct Connection {
        // Stable for this connection and replaced on reconnect.
        uint64_t identity = 0;
        // Incremented when Steam announces a configuration reload.
        uint64_t binding_revision = 0;
    };

    // Acquire the current native lease or initialize the first one.
    static std::shared_ptr<SteamInputSession> acquire(const std::shared_ptr<SteamRuntime> &runtime,
            const godot::String &manifest_path, godot::Error &error);
    // Unsubscribe and shut down the SDK input interface when the last scope releases it.
    ~SteamInputSession();
    // Synchronize the SDK and reconcile connected handles once in the current Godot frame.
    void poll();
    // Return the exact current native connection records.
    const std::map<uint64_t, Connection> &connections() const;
    // Return the Steam Input interface while the lease is held.
    ISteamInput *interface() const;
    // Return the SDK runtime held by the lease for flat export resolution.
    const SteamRuntime &get_runtime() const;
    // Resolve a named handle and cache successful lookups within this native lease.
    std::optional<uint64_t> name_handle(NameKind kind, const godot::StringName &name);

private:
    // Create an uninitialized native lease only through acquire.
    SteamInputSession() = default;
    // Subscribe to SDK disconnect and configuration events before sharing the lease.
    bool subscribe(const std::shared_ptr<SteamInputSession> &owner);

    // Retain the DLL and manual callback dispatcher through input shutdown.
    std::shared_ptr<SteamRuntime> runtime;
    // Borrowed SDK interface from the retained runtime.
    ISteamInput *input = nullptr;
    // Resolved exports required for the entire lease lifetime.
    decltype(&SteamAPI_ISteamInput_Shutdown) shutdown = nullptr;
    decltype(&SteamAPI_ISteamInput_RunFrame) run_frame = nullptr;
    decltype(&SteamAPI_ISteamInput_GetConnectedControllers) get_controllers = nullptr;
    // Indicates that Init succeeded and must be matched by one Shutdown.
    bool initialized = false;
    // Exact effective manifest path, empty when using Steam's app configuration.
    godot::String manifest;
    // Dispatcher subscriptions associated with this lease.
    std::array<uint64_t, 2> subscriptions{};
    // Current Steam handles and exact connection identities.
    std::map<uint64_t, Connection> devices;
    // Monotonic connection identity within this lease.
    uint64_t next_connection = 1;
    // Resolved action set, digital action, and analog action handles.
    std::array<std::map<godot::StringName, uint64_t>, 3> names;
};

} // namespace crimson::steam

#endif
