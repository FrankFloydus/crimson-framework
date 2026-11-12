// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/string.hpp>

namespace crimson::steam {

// The durable store's decision for one exact notification operation key.
class SteamNotificationClaim : public godot::RefCounted {
    GDCLASS(SteamNotificationClaim, godot::RefCounted)

public:
    // Atomic claim outcomes from the trusted store.
    enum Status {
        CLAIMED, // This caller owns a new operation and can submit it.
        IN_FLIGHT, // A matching key or target already has an unresolved operation.
        COMMITTED, // The exact operation previously committed and can be replayed.
        CONFLICT, // The key already identifies a different immutable request.
        DENIED, // The application does not own the requested target session.
    };

    // Construct a decision and, for a committed action, its stored provider session ID.
    static godot::Ref<SteamNotificationClaim> create(Status status, const godot::String &session_id = {});
    // Return the decision made by the durable store.
    Status get_status() const;
    // Return the committed provider session ID when present.
    godot::String get_session_id() const;

protected:
    // Bind the decision's construction and immutable fields.
    static void _bind_methods();

private:
    // Unconfigured claims deny mutations until the adapter chooses a decision.
    Status status = DENIED;
    // Exact unsigned decimal provider ID stored with a committed result.
    godot::String session_id;
};

// Trusted host adapter for durable notification operations and session ownership.
class SteamNotificationStore : public godot::RefCounted {
    GDCLASS(SteamNotificationStore, godot::RefCounted)

public:
    // The operation whose key and session target must be claimed atomically.
    enum Action {
        CREATE,
        UPDATE,
        REMOVE,
    };

protected:
    // Bind the trusted host's persistence hooks and action enum.
    static void _bind_methods();
    // Atomically claim an immutable key and serialize writes to its provider session target.
    GDVIRTUAL5R(godot::Ref<SteamNotificationClaim>, _claim, int64_t, godot::String, godot::String, int64_t, godot::String)
    // Commit the result and update app-owned session membership in the same durable transaction.
    GDVIRTUAL5R(bool, _commit, int64_t, godot::String, godot::String, int64_t, godot::String)
    // Release a matching claim only when the native transport confirms no request was scheduled.
    GDVIRTUAL4R(bool, _release_unsent, int64_t, godot::String, godot::String, int64_t)
    // Check ownership before a direct session detail request.
    GDVIRTUAL2R(bool, _owns_session, int64_t, godot::String)
    // Durably reserve notification opt-in once for the exact app, account, and launch.
    GDVIRTUAL3R(godot::Error, _claim_launch_opt_in, int64_t, godot::String, godot::String)
    // Release a launch claim only when no Steam request was scheduled.
    GDVIRTUAL3R(bool, _release_unsent_launch_opt_in, int64_t, godot::String, godot::String)

private:
    friend class SteamGameNotifications;

    // Invoke the atomic claim hook on the attached trusted adapter.
    godot::Ref<SteamNotificationClaim> claim(int64_t app_id, const godot::String &key,
            const godot::String &fingerprint, Action action, const godot::String &target_id);
    // Commit the provider receipt before exposing a successful mutation to the caller.
    bool commit(int64_t app_id, const godot::String &key, const godot::String &fingerprint, Action action,
            const godot::String &provider_id);
    // Release only the exact matching key after a synchronous no-send error.
    bool release_unsent(int64_t app_id, const godot::String &key, const godot::String &fingerprint, Action action);
    // Resolve provider session ownership from durable app state.
    bool owns_session(int64_t app_id, const godot::String &session_id);
    // Invoke the durable launch claim hook before asking Steam to prompt the account.
    godot::Error claim_launch_opt_in(int64_t app_id, const godot::String &steam_id, const godot::String &launch_id);
    // Release the exact unsent launch claim without allowing a dispatched request to retry.
    bool release_unsent_launch_opt_in(int64_t app_id, const godot::String &steam_id, const godot::String &launch_id);
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamNotificationClaim::Status);
VARIANT_ENUM_CAST(crimson::steam::SteamNotificationStore::Action);
