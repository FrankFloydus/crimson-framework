// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/publisher/steam_notification_store.hpp"
#include "modules/steam/publisher/steam_notification_types.hpp"

#include <godot_cpp/variant/variant.hpp>

#include <functional>
#include <memory>

namespace crimson::steam {

struct SteamNotificationMutation;

// Outcome and detached session receipt.
class SteamNotificationSessionResult : public SteamResult {
    GDCLASS(SteamNotificationSessionResult, SteamResult)

public:
    // Return the detached session receipt available for this outcome.
    godot::Ref<SteamNotificationSession> get_value() const;

protected:
    // Bind the read-only notification result.
    static void _bind_methods();

private:
    friend class SteamGameNotifications;
    friend class SteamNotificationSessionOperation;

    // Construct a settled result with its detached payload.
    static godot::Ref<SteamNotificationSessionResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamNotificationSession> &snapshot = {});

    // The detached session receipt.
    godot::Ref<SteamNotificationSession> value;
};

// Outcome and read-only list of detached sessions.
class SteamNotificationSessionsResult : public SteamResult {
    GDCLASS(SteamNotificationSessionsResult, SteamResult)

public:
    // Return the read-only list of detached sessions available for this outcome.
    godot::TypedArray<SteamNotificationSession> get_value() const;

protected:
    // Bind the read-only notification result.
    static void _bind_methods();

private:
    friend class SteamGameNotifications;
    friend class SteamNotificationSessionsOperation;

    // Construct a settled result with its detached payload.
    static godot::Ref<SteamNotificationSessionsResult> create(godot::Error error, const godot::String &message = {},
            const godot::TypedArray<SteamNotificationSession> &snapshot = {});

    // The read-only list of detached sessions.
    godot::TypedArray<SteamNotificationSession> value;
};

// Retained observation of one boolean notification acknowledgment.
class SteamNotificationBoolOperation : public godot::RefCounted {
    GDCLASS(SteamNotificationBoolOperation, godot::RefCounted)

public:
    // Report whether a terminal result is available.
    bool is_finished() const;
    // Return the typed result, or null while pending.
    godot::Ref<SteamBoolResult> result() const;
    // Return the terminal typed result or its completion signal.
    godot::Variant wait();
    // End observation without undoing a submitted Steam request.
    godot::Error cancel();

protected:
    // Bind the public operation and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamGameNotifications;

    // Settle this observer once and emit completion after publishing the result.
    void finish(const godot::Ref<SteamBoolResult> &result);

    // True only for an operation created by the trusted notification service.
    bool pending = false;
    // Terminal typed result after success, failure, or observer cancellation.
    godot::Ref<SteamBoolResult> outcome;
};

// Retained observation of one notification session.
class SteamNotificationSessionOperation : public godot::RefCounted {
    GDCLASS(SteamNotificationSessionOperation, godot::RefCounted)

public:
    // Report whether a terminal result is available.
    bool is_finished() const;
    // Return the typed result, or null while pending.
    godot::Ref<SteamNotificationSessionResult> result() const;
    // Return the terminal typed result or its completion signal.
    godot::Variant wait();
    // End observation without undoing a submitted Steam request.
    godot::Error cancel();

protected:
    // Bind the public operation and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamGameNotifications;

    // Settle this observer once and emit completion after publishing the result.
    void finish(const godot::Ref<SteamNotificationSessionResult> &result);

    // True only for an operation created by the trusted notification service.
    bool pending = false;
    // Terminal typed result after success, failure, or observer cancellation.
    godot::Ref<SteamNotificationSessionResult> outcome;
};

// Retained observation of a bounded list of notification sessions.
class SteamNotificationSessionsOperation : public godot::RefCounted {
    GDCLASS(SteamNotificationSessionsOperation, godot::RefCounted)

public:
    // Report whether a terminal result is available.
    bool is_finished() const;
    // Return the typed result, or null while pending.
    godot::Ref<SteamNotificationSessionsResult> result() const;
    // Return the terminal typed result or its completion signal.
    godot::Variant wait();
    // End observation without undoing a submitted Steam request.
    godot::Error cancel();

protected:
    // Bind the public operation and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamGameNotifications;

    // Settle this observer once and emit completion after publishing the result.
    void finish(const godot::Ref<SteamNotificationSessionsResult> &result);

    // True only for an operation created by the trusted notification service.
    bool pending = false;
    // Terminal typed result after success, failure, or observer cancellation.
    godot::Ref<SteamNotificationSessionsResult> outcome;
};

// Trusted app-scoped access to Steam's asynchronous game notifications.
class SteamGameNotifications : public godot::RefCounted {
    GDCLASS(SteamGameNotifications, godot::RefCounted)

public:
    // Ask Steam to register notifications once for the verified account and trusted launch ID.
    godot::Ref<SteamNotificationBoolOperation> request_opt_in(const godot::Ref<SteamIdentity> &user,
            const godot::String &launch_id = {});
    // Create one notification session with a durable backend operation key.
    godot::Ref<SteamNotificationSessionOperation> create(const godot::Ref<SteamNotificationSessionSpec> &spec,
            const godot::String &operation_key);
    // Replace the owned session's participant state using a durable operation key.
    godot::Ref<SteamNotificationSessionOperation> update(const godot::String &session_id,
            const godot::Ref<SteamNotificationSessionSpec> &spec, const godot::String &operation_key);
    // Read bounded sessions for a player verified by this exact publisher.
    godot::Ref<SteamNotificationSessionsOperation> sessions_for(const godot::Ref<SteamIdentity> &user);
    // Read one session owned by the app in the durable store.
    godot::Ref<SteamNotificationSessionOperation> details(const godot::String &session_id);
    // Remove an owned session using a durable operation key.
    godot::Ref<SteamNotificationBoolOperation> remove(const godot::String &session_id, const godot::String &operation_key);

protected:
    // Bind the trusted notification actions.
    static void _bind_methods();

private:
    friend class SteamPublisher;

    // Deliver a settled mutation, retaining any known session on a commit failure.
    using MutationCompletion = std::function<void(godot::Error, const godot::String &,
            const godot::Ref<SteamNotificationSession> &)>;

    // Build and validate the request from immutable app-scoped input.
    godot::Error encode_mutation(const std::shared_ptr<SteamPublisherState> &owner,
            const godot::Ref<SteamNotificationSessionSpec> &spec, SteamNotificationMutation &mutation) const;
    // Claim, submit, and commit one immutable request without retrying ambiguous writes.
    void dispatch_mutation(SteamNotificationStore::Action action, const godot::String &session_id,
            const godot::Ref<SteamNotificationSessionSpec> &spec, const godot::String &operation_key,
            MutationCompletion complete);
    // Start either form of durable session write using the same typed observer.
    godot::Ref<SteamNotificationSessionOperation> change_session(SteamNotificationStore::Action action,
            const godot::String &session_id, const godot::Ref<SteamNotificationSessionSpec> &spec,
            const godot::String &operation_key);

    // Weak owner prevents a retained service from reviving a closed publisher.
    std::weak_ptr<SteamPublisherState> scope;
};

} // namespace crimson::steam
