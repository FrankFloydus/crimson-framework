// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/variant/packed_string_array.hpp>

namespace crimson::steam {

// The acknowledged subscription state of one Workshop item.
class SteamWorkshopSubscription : public godot::RefCounted {
    GDCLASS(SteamWorkshopSubscription, godot::RefCounted)

protected:
    // Bind the read-only Workshop fields.
    static void _bind_methods();

public:
    // Return the canonical unsigned decimal published file ID.
    godot::String get_published_id() const;
    // Return the acknowledged subscription state.
    bool is_subscribed() const;

private:
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Create a detached snapshot before delivering it to the caller.
    static godot::Ref<SteamWorkshopSubscription> create(const godot::String &published_id, bool subscribed);

    // The exact published file ID.
    godot::String published_id;
    // Whether Steam confirmed a subscription.
    bool subscribed = false;
};

// Outcome and detached data for a Workshop subscription.
class SteamWorkshopSubscriptionResult : public SteamResult {
    GDCLASS(SteamWorkshopSubscriptionResult, SteamResult)

protected:
    // Bind the read-only Workshop result.
    static void _bind_methods();

public:
    // Return the result snapshot if Steam supplied one.
    godot::Ref<SteamWorkshopSubscription> get_value() const;

private:
    friend class SteamWorkshopItemRef;
    friend class SteamWorkshopPublishRequest;
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Construct a terminal outcome and its detached payload when available.
    static godot::Ref<SteamWorkshopSubscriptionResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamWorkshopSubscription> &snapshot = {});

    // Snapshot associated with the reported outcome.
    godot::Ref<SteamWorkshopSubscription> value;
};

// Detached local installation metadata for a current Workshop item.
class SteamWorkshopInstall : public godot::RefCounted {
    GDCLASS(SteamWorkshopInstall, godot::RefCounted)

protected:
    // Bind the read-only Workshop fields.
    static void _bind_methods();

public:
    // Return the canonical unsigned decimal published file ID.
    godot::String get_published_id() const;
    // Return the absolute directory Steam reports for this item.
    godot::String get_directory() const;
    // Return the installed content size in bytes.
    int64_t get_size_bytes() const;

private:
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Create a detached snapshot before delivering it to the caller.
    static godot::Ref<SteamWorkshopInstall> create(const godot::String &published_id, const godot::String &directory, int64_t size_bytes);

    // The exact published file ID.
    godot::String published_id;
    // The absolute content directory reported by Steam.
    godot::String directory;
    // Size of the installed content in bytes.
    int64_t size_bytes = 0;
};

// Outcome and detached data for a Workshop install.
class SteamWorkshopInstallResult : public SteamResult {
    GDCLASS(SteamWorkshopInstallResult, SteamResult)

protected:
    // Bind the read-only Workshop result.
    static void _bind_methods();

public:
    // Return the result snapshot if Steam supplied one.
    godot::Ref<SteamWorkshopInstall> get_value() const;

private:
    friend class SteamWorkshopItemRef;
    friend class SteamWorkshopPublishRequest;
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Construct a terminal outcome and its detached payload when available.
    static godot::Ref<SteamWorkshopInstallResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamWorkshopInstall> &snapshot = {});

    // Snapshot associated with the reported outcome.
    godot::Ref<SteamWorkshopInstall> value;
};

// The acknowledged vote on one Workshop item.
class SteamWorkshopVote : public godot::RefCounted {
    GDCLASS(SteamWorkshopVote, godot::RefCounted)

protected:
    // Bind the read-only Workshop fields.
    static void _bind_methods();

public:
    // Return the canonical unsigned decimal published file ID.
    godot::String get_published_id() const;
    // Return whether Steam confirmed an upvote.
    bool is_up() const;

private:
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Create a detached snapshot before delivering it to the caller.
    static godot::Ref<SteamWorkshopVote> create(const godot::String &published_id, bool up);

    // The exact published file ID.
    godot::String published_id;
    // Whether Steam confirmed an upvote.
    bool up = false;
};

// Outcome and detached data for a Workshop vote.
class SteamWorkshopVoteResult : public SteamResult {
    GDCLASS(SteamWorkshopVoteResult, SteamResult)

protected:
    // Bind the read-only Workshop result.
    static void _bind_methods();

public:
    // Return the result snapshot if Steam supplied one.
    godot::Ref<SteamWorkshopVote> get_value() const;

private:
    friend class SteamWorkshopItemRef;
    friend class SteamWorkshopPublishRequest;
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Construct a terminal outcome and its detached payload when available.
    static godot::Ref<SteamWorkshopVoteResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamWorkshopVote> &snapshot = {});

    // Snapshot associated with the reported outcome.
    godot::Ref<SteamWorkshopVote> value;
};

// Identity and Steam agreement state for a Workshop publication.
class SteamWorkshopPublication : public godot::RefCounted {
    GDCLASS(SteamWorkshopPublication, godot::RefCounted)

protected:
    // Bind the read-only Workshop fields.
    static void _bind_methods();

public:
    // Return the canonical unsigned decimal published file ID.
    godot::String get_published_id() const;
    // Return whether Steam requires the author to accept its Workshop agreement.
    bool get_needs_legal_agreement() const;

private:
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Create a detached snapshot before delivering it to the caller.
    static godot::Ref<SteamWorkshopPublication> create(const godot::String &published_id, bool needs_legal_agreement);

    // The exact published file ID available for editing or retry.
    godot::String published_id;
    // Whether the author needs to accept the Steam Workshop legal agreement.
    bool needs_legal_agreement = false;
};

// Outcome and detached data for a Workshop publication.
class SteamWorkshopPublicationResult : public SteamResult {
    GDCLASS(SteamWorkshopPublicationResult, SteamResult)

protected:
    // Bind the read-only Workshop result.
    static void _bind_methods();

public:
    // Return the result snapshot if Steam supplied one.
    godot::Ref<SteamWorkshopPublication> get_value() const;

private:
    friend class SteamWorkshopItemRef;
    friend class SteamWorkshopPublishRequest;
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Construct a terminal outcome and its detached payload when available.
    static godot::Ref<SteamWorkshopPublicationResult> create(godot::Error error, const godot::String &message = {},
            const godot::Ref<SteamWorkshopPublication> &snapshot = {});

    // Snapshot associated with the reported outcome.
    godot::Ref<SteamWorkshopPublication> value;
};

// Bounded list of subscribed Workshop item IDs for the current app.
class SteamWorkshopSubscriptions : public godot::RefCounted {
    GDCLASS(SteamWorkshopSubscriptions, godot::RefCounted)

protected:
    // Bind the read-only Steam snapshot.
    static void _bind_methods();

public:
    // Return decimal published file IDs in Steam's subscription order.
    godot::PackedStringArray get_published_ids() const;
    // Return the maximum subscription count observed while copying the snapshot.
    int64_t get_total_count() const;
    // Report whether more subscriptions were observed than could be copied.
    bool is_truncated() const;

private:
    friend struct SteamWorkshopScope;

    // Construct a detached snapshot before publishing the outcome.
    static godot::Ref<SteamWorkshopSubscriptions> create(const godot::PackedStringArray &published_ids, int64_t total_count, bool truncated);

    // Detached IDs copied from Steam's current app list.
    godot::PackedStringArray published_ids;
    // Count observed around the bounded SDK enumeration.
    int64_t total_count = 0;
    // Whether the returned list omits observed subscriptions.
    bool truncated = false;
};

// Outcome and detached snapshot for an app-scoped subscription enumeration.
class SteamWorkshopSubscriptionsResult : public SteamResult {
    GDCLASS(SteamWorkshopSubscriptionsResult, SteamResult)

protected:
    // Bind the read-only snapshot result.
    static void _bind_methods();

public:
    // Return the snapshot on success, or null on failure.
    godot::Ref<SteamWorkshopSubscriptions> get_value() const;

private:
    friend class SteamWorkshop;
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Construct a terminal result with a snapshot when Steam supplies one.
    static godot::Ref<SteamWorkshopSubscriptionsResult> create(godot::Error error, const godot::Ref<SteamWorkshopSubscriptions> &snapshot = {});

    // Detached data for the successful query.
    godot::Ref<SteamWorkshopSubscriptions> value;
};

// The signed-in user's Workshop legal agreement status for the exact AppID.
class SteamWorkshopLegalAgreementStatus : public godot::RefCounted {
    GDCLASS(SteamWorkshopLegalAgreementStatus, godot::RefCounted)

protected:
    // Bind the read-only Steam snapshot.
    static void _bind_methods();

public:
    // Return the AppID Steam confirmed for this status.
    int64_t get_app_id() const;
    // Return the version of the signed agreement when applicable.
    int64_t get_version() const;
    // Return the signed agreement's Unix timestamp when available.
    int64_t get_accepted_at_unix() const;
    // Report whether Steam says the user accepted the reported version.
    bool is_accepted() const;
    // Report whether the user needs to accept the latest agreement.
    bool get_needs_action() const;

private:
    friend struct SteamWorkshopScope;

    // Construct a detached snapshot before publishing the outcome.
    static godot::Ref<SteamWorkshopLegalAgreementStatus> create(int64_t app_id, int64_t version, int64_t accepted_at_unix, bool accepted, bool needs_action);

    // Exact active AppID reported by Steam.
    int64_t app_id = 0;
    // Agreement version reported by Steam, or zero when unavailable.
    int64_t version = 0;
    // Time Steam recorded the agreement action, or zero when unavailable.
    int64_t accepted_at_unix = 0;
    // Whether the reported agreement version was accepted.
    bool accepted = false;
    // Whether Steam requires user action for the latest agreement.
    bool needs_action = false;
};

// Outcome and detached snapshot for an exact Workshop agreement query.
class SteamWorkshopLegalAgreementStatusResult : public SteamResult {
    GDCLASS(SteamWorkshopLegalAgreementStatusResult, SteamResult)

protected:
    // Bind the read-only snapshot result.
    static void _bind_methods();

public:
    // Return the snapshot on success, or null on failure.
    godot::Ref<SteamWorkshopLegalAgreementStatus> get_value() const;

private:
    friend class SteamWorkshop;
    friend struct SteamWorkshopScope;
    friend struct SteamWorkshopTask;

    // Construct a terminal result with a snapshot when Steam supplies one.
    static godot::Ref<SteamWorkshopLegalAgreementStatusResult> create(godot::Error error, const godot::Ref<SteamWorkshopLegalAgreementStatus> &snapshot = {});

    // Detached data for the successful query.
    godot::Ref<SteamWorkshopLegalAgreementStatus> value;
};

} // namespace crimson::steam
