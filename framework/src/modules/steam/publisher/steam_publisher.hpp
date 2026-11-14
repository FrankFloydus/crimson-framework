// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"
#include "modules/steam/publisher/steam_notification_store.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <functional>
#include <memory>

namespace crimson::steam {

class SteamPublisher;
class SteamPublisherAuth;
class SteamPublisherLeaderboards;
class SteamGameNotifications;
class SteamPublisherModeration;
class SteamModerationPolicy;
class SteamPublisherPurchases;
class SteamPurchaseStore;
class SteamOwnershipChallengeStore;
class SteamOwnershipResult;
class SteamPublisherInventory;
struct SteamPublisherPurchaseFlow;
struct SteamPublisherReply;
struct SteamPublisherRequest;
struct SteamPublisherState;
struct SteamPublisherTransport;

// Trusted scene-tree owner that resolves credentials and pumps bounded HTTPS requests.
class SteamPublisherHost : public godot::Node {
    GDCLASS(SteamPublisherHost, godot::Node)

public:
    // Create the detached trusted host.
    SteamPublisherHost();
    // Retire any remaining native HTTP requests.
    ~SteamPublisherHost() override;
    // Enable new publisher scopes for this scene-tree attachment.
    void _enter_tree() override;
    // Poll active requests and deliver terminal replies outside the pending map.
    void _process(double delta) override;
    // End pending observations and invalidate publisher scopes for this host generation.
    void _exit_tree() override;
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    // Report that this build uses the local fixture transport exclusively.
    bool _fixture_transport_enabled() const;
#endif

protected:
    // Bind the trusted host's secret resolution hook and the optional test seam.
    static void _bind_methods();
    // Resolve a secret reference through the trusted process's credential store.
    GDVIRTUAL1R(godot::String, _resolve_secret, godot::StringName)
    // Resolve the separate encrypted app-ticket key as exact bytes from the trusted credential store.
    GDVIRTUAL1R(godot::PackedByteArray, _resolve_encrypted_ticket_key, godot::StringName)
#ifdef CRIMSON_WITH_STEAM_PUBLISHER_TESTS
    // Return a canned reply in the transport-only test build, which never sends network traffic.
    GDVIRTUAL5R(godot::Dictionary, _test_response, godot::String, godot::String, godot::String, godot::String, bool)
    // Supply synthetic decrypted ownership claims in the socket-free publisher test build.
    GDVIRTUAL1R(godot::Dictionary, _test_decrypted_ownership_ticket, godot::PackedByteArray)
#endif

private:
    friend class SteamPublisher;
    friend class SteamPublisherAuth;
    friend class SteamPublisherLeaderboards;
    friend class SteamGameNotifications;
    friend class SteamPublisherModeration;
    friend class SteamPublisherPurchases;
    friend class SteamPublisherInventory;
    friend struct SteamPublisherPurchaseFlow;

    // Find the original attached host and reject retired scene-tree generations.
    static SteamPublisherHost *resolve(const std::shared_ptr<SteamPublisherState> &state);
    // Issue one fixed Steam partner request. Errors returned here do not deliver a callback.
    godot::Error send(const std::shared_ptr<SteamPublisherState> &state, const SteamPublisherRequest &request,
            std::function<void(const SteamPublisherReply &)> complete);

    // Pending transport requests owned by this exact node.
    std::unique_ptr<SteamPublisherTransport> transport;
    // Changes before teardown callbacks so a reattached node cannot revive old publishers.
    uint64_t generation = 1;
    // Guards requests during reentrant callbacks while the host leaves the tree.
    bool accepting = false;
};

// Explicit server-side configuration. Stores references to secrets, never their values.
class SteamPublisherSettings : public godot::RefCounted {
    GDCLASS(SteamPublisherSettings, godot::RefCounted)

public:
    // Select the environment for features with separate vendor endpoints.
    enum Environment {
        LIVE, // Use Valve's live publisher services.
        SANDBOX, // Select the feature's Valve sandbox where one exists.
    };

    // Create empty trusted configuration for an attached host.
    SteamPublisherSettings();
    // Release the injected trusted game adapters.
    ~SteamPublisherSettings() override;

    // Set the trusted backend's Steamworks AppID.
    void set_app_id(int64_t value);
    // Read the configured Steamworks AppID.
    int64_t get_app_id() const;
    // Select live or sandbox vendor operations.
    void set_environment(Environment value);
    // Read the selected vendor environment.
    Environment get_environment() const;
    // Set the name looked up by the host for its publisher credential.
    void set_publisher_key_ref(const godot::StringName &value);
    // Read the publisher credential reference.
    godot::StringName get_publisher_key_ref() const;
    // Set an optional reference for a separate encrypted app-ticket key.
    void set_encrypted_ticket_key_ref(const godot::StringName &value);
    // Read the encrypted app-ticket key reference.
    godot::StringName get_encrypted_ticket_key_ref() const;
    // Inject the attached trusted host that resolves secrets and pumps HTTPS.
    void set_host(SteamPublisherHost *value);
    // Read the injected host, or null if it was freed.
    SteamPublisherHost *get_host() const;
    // Inject the trusted durable store for notification sessions and operations.
    void set_notification_store(const godot::Ref<SteamNotificationStore> &value);
    // Read the optional durable notification store.
    godot::Ref<SteamNotificationStore> get_notification_store() const;
    // Inject the game policy that authorizes and audits publisher moderation.
    void set_moderation_policy(const godot::Ref<SteamModerationPolicy> &value);
    // Read the configured moderation policy.
    godot::Ref<SteamModerationPolicy> get_moderation_policy() const;
    // Inject durable publisher pricing, order, and entitlement storage.
    void set_purchase_store(const godot::Ref<SteamPurchaseStore> &value);
    // Read the optional trusted purchase store.
    godot::Ref<SteamPurchaseStore> get_purchase_store() const;
    // Inject the durable one-use challenge store for encrypted app-ticket verification.
    void set_ownership_challenge_store(const godot::Ref<SteamOwnershipChallengeStore> &value);
    // Read the configured encrypted ticket challenge store.
    godot::Ref<SteamOwnershipChallengeStore> get_ownership_challenge_store() const;

protected:
    // Bind server-side configuration properties.
    static void _bind_methods();

private:
    // Positive AppID used to scope ticket verification and writes.
    int64_t app_id = 0;
    // Selected vendor environment.
    Environment environment = LIVE;
    // Name of the publisher credential in the trusted host.
    godot::StringName publisher_key_ref;
    // Optional name of the separate encrypted ticket key.
    godot::StringName encrypted_ticket_key_ref;
    // Weak identity of the trusted scene-tree owner.
    godot::ObjectID host_id;
    // Optional durable notification storage supplied by the trusted game backend.
    godot::Ref<SteamNotificationStore> notification_store;
    // Optional app-owned authorization policy for trusted moderation.
    godot::Ref<SteamModerationPolicy> moderation_policy;
    // Optional app-owned durable storage for Steam Wallet transactions.
    godot::Ref<SteamPurchaseStore> purchase_store;
    // Optional app-owned authority for atomically consuming encrypted ticket challenges.
    godot::Ref<SteamOwnershipChallengeStore> ownership_challenge_store;
};

// An identity established by Valve and scoped to its exact issuing publisher.
class SteamIdentity : public godot::RefCounted {
    GDCLASS(SteamIdentity, godot::RefCounted)

public:
    // Return the verified individual SteamID64 as a decimal string.
    godot::String get_steam_id() const;
    // Return the SteamID64 license owner when Valve supplied one.
    godot::String get_license_owner_steam_id() const;
    // Return the AppID verified for this identity.
    int64_t get_app_id() const;

protected:
    // Bind immutable identity fields.
    static void _bind_methods();

private:
    friend class SteamPublisherAuth;
    friend class SteamPublisherLeaderboards;
    friend class SteamGameNotifications;
    friend class SteamPublisherModeration;
    friend class SteamPublisherPurchases;
    friend class SteamPublisherInventory;

    // Accept only identities minted by the exact live publisher scope.
    bool belongs_to(const std::shared_ptr<SteamPublisherState> &publisher) const;

    // Verified player ID.
    godot::String steam_id;
    // Optional verified license owner ID.
    godot::String license_owner_steam_id;
    // Configured app used in the verification request.
    int64_t app_id = 0;
    // Weak publisher scope prevents identities from reviving a retired issuer.
    std::weak_ptr<SteamPublisherState> issuer;
};

// Outcome of verifying one Steam Web API ticket.
class SteamIdentityResult : public SteamResult {
    GDCLASS(SteamIdentityResult, SteamResult)

public:
    // Return the verified identity, or null when verification failed.
    godot::Ref<SteamIdentity> get_value() const;

protected:
    // Bind the typed identity payload.
    static void _bind_methods();

private:
    friend class SteamPublisherAuth;

    // Construct the terminal typed result without reflecting vendor text.
    static godot::Ref<SteamIdentityResult> create(godot::Error error, const godot::Ref<SteamIdentity> &identity = {});

    // Detached identity available only when Valve accepted the ticket.
    godot::Ref<SteamIdentity> value;
};

// Steam's current license information for a queried app and previously verified player.
class SteamAppOwnership : public godot::RefCounted {
    GDCLASS(SteamAppOwnership, godot::RefCounted)

public:
    // Return the AppID queried for this snapshot.
    int64_t get_app_id() const;
    // Return whether Steam reports an active license for the app.
    bool get_owns_app() const;
    // Return whether Steam reports permanent ownership.
    bool get_permanent() const;
    // Return the license owner's SteamID64 when supplied.
    godot::String get_license_owner_steam_id() const;
    // Return Steam's acquisition timestamp text when supplied.
    godot::String get_acquired_at() const;
    // Return Steam's expiry text, including its permanent-license sentinel when supplied.
    godot::String get_expires_at() const;
    // Return whether the license is from Steam's PC Cafe program.
    bool get_site_license() const;
    // Return whether Steam marks the license as self-cancelled by the user.
    bool get_user_canceled() const;

protected:
    // Bind the read-only license snapshot.
    static void _bind_methods();

private:
    friend class SteamPublisherAuth;

    // AppID sent in the ownership request.
    int64_t app_id = 0;
    // Whether Steam reports a usable license.
    bool owns_app = false;
    // Whether the license is permanent.
    bool permanent = false;
    // Decimal SteamID64 of the license owner, when supplied.
    godot::String license_owner_steam_id;
    // Acquisition time text supplied by Steam.
    godot::String acquired_at;
    // License expiry text supplied by Steam.
    godot::String expires_at;
    // PC Cafe access flag.
    bool site_license = false;
    // User-cancelled license flag.
    bool user_canceled = false;
};

// Status and typed payload for one publisher ownership check.
class SteamAppOwnershipResult : public SteamResult {
    GDCLASS(SteamAppOwnershipResult, SteamResult)

public:
    // Return the completed ownership read, or null if the request failed.
    godot::Ref<SteamAppOwnership> get_value() const;

protected:
    // Bind the read-only ownership result.
    static void _bind_methods();

private:
    friend class SteamPublisherAuth;

    // Construct the terminal typed result without reflecting vendor text.
    static godot::Ref<SteamAppOwnershipResult> create(godot::Error error, const godot::Ref<SteamAppOwnership> &ownership = {});

    // Snapshot received when the Steam ownership check succeeded.
    godot::Ref<SteamAppOwnership> value;
};

// Verifies Web API tickets and checks app licenses within one trusted publisher scope.
class SteamPublisherAuth : public godot::RefCounted {
    GDCLASS(SteamPublisherAuth, godot::RefCounted)

public:
    // Await Valve verification for the configured AppID and exact supplied service audience.
    godot::Variant verify(const godot::PackedByteArray &ticket_bytes, const godot::String &audience);
    // Await the current app license for a player verified by this exact publisher.
    godot::Variant owns_app(const godot::Ref<SteamIdentity> &identity, int64_t app_id);
    // Verify encrypted ownership and consume the exact outstanding backend challenge.
    godot::Ref<SteamOwnershipResult> verify_ownership(const godot::PackedByteArray &ticket,
            const godot::PackedByteArray &expected_challenge, const godot::PackedInt64Array &dlc_ids = {});

protected:
    // Bind public trusted authentication actions.
    static void _bind_methods();

private:
    friend class SteamPublisher;

    // Weak owner keeps the authentication facade from reviving a closed backend.
    std::weak_ptr<SteamPublisherState> scope;
};

// Submits authorized, authoritative scores for players verified by the trusted backend.
class SteamPublisherLeaderboards : public godot::RefCounted {
    GDCLASS(SteamPublisherLeaderboards, godot::RefCounted)

public:
    // Await Steam's acknowledgment of a keep-best score for a verified player.
    godot::Variant submit_score(const godot::Ref<SteamIdentity> &player, int64_t board_id, int64_t score,
            const godot::PackedByteArray &details = {});

protected:
    // Bind the publisher score submission action.
    static void _bind_methods();

private:
    friend class SteamPublisher;

    // Weak owner keeps the score service from reviving a closed backend.
    std::weak_ptr<SteamPublisherState> scope;
};

class SteamPublisherResult;

// Trusted Steam publisher scope, available only in explicit server or operator builds.
class SteamPublisher : public godot::RefCounted {
    GDCLASS(SteamPublisher, godot::RefCounted)

public:
    // Create a detached publisher before accepting its validated configuration.
    SteamPublisher();
    // Invalidate the issuer when its last publisher reference goes away.
    ~SteamPublisher() override;
    // Snapshot validated settings and create one trusted publisher scope.
    static godot::Ref<SteamPublisherResult> boot(const godot::Ref<SteamPublisherSettings> &settings);
    // Report whether this publisher still belongs to its attached host generation.
    bool is_ready() const;
    // Retire the scope and its verified identities.
    void close();
    // Resolve the scoped authentication service.
    godot::Ref<SteamPublisherAuth> auth() const;
    // Resolve the scoped leaderboard submission service.
    godot::Ref<SteamPublisherLeaderboards> leaderboards() const;
    // Resolve the app's scoped game notification service.
    godot::Ref<SteamGameNotifications> game_notifications() const;
    // Resolve the app's scoped trusted moderation service.
    godot::Ref<SteamPublisherModeration> moderation() const;
    // Resolve the app's trusted Steam Wallet service.
    godot::Ref<SteamPublisherPurchases> purchases() const;
    // Resolve the trusted app-scoped Steam Inventory service.
    godot::Ref<SteamPublisherInventory> inventory() const;

protected:
    // Bind trusted publisher boot and scoped services.
    static void _bind_methods();

private:
    // Owns the immutable configuration and identity issuer for the publisher.
    std::shared_ptr<SteamPublisherState> scope;
    // Scoped service for Web API ticket verification and app ownership.
    godot::Ref<SteamPublisherAuth> auth_service;
    // Scoped service for publisher-authorized leaderboard writes.
    godot::Ref<SteamPublisherLeaderboards> leaderboard_service;
    // Scoped service for Steam game notifications and sessions.
    godot::Ref<SteamGameNotifications> notification_service;
    // Scoped service for game-authorized Steam publisher moderation.
    godot::Ref<SteamPublisherModeration> moderation_service;
    // Scoped service for trusted purchase processing.
    godot::Ref<SteamPublisherPurchases> purchase_service;
    // Scoped service for verified Steam Inventory requests.
    godot::Ref<SteamPublisherInventory> inventory_service;
};

// Outcome of starting one trusted publisher scope.
class SteamPublisherResult : public SteamResult {
    GDCLASS(SteamPublisherResult, SteamResult)

public:
    // Return the initialized publisher, or null if boot failed.
    godot::Ref<SteamPublisher> get_value() const;

protected:
    // Bind the read-only publisher payload.
    static void _bind_methods();

private:
    friend class SteamPublisher;

    // Construct the result before publishing a trusted scope.
    static godot::Ref<SteamPublisherResult> create(godot::Error error, const godot::Ref<SteamPublisher> &publisher = {});

    // Publisher created from validated trusted host settings.
    godot::Ref<SteamPublisher> value;
};

// Internal signal carrier for the directly awaited publisher actions.
class SteamPublisherCompletion : public godot::RefCounted {
    GDCLASS(SteamPublisherCompletion, godot::RefCounted)

public:
    // Return the terminal result or a signal that will deliver it.
    godot::Variant wait();
    // Settle once before notifying awaiting callers.
    void finish(const godot::Ref<SteamResult> &result);

protected:
    // Bind the completion signal consumed by direct await calls.
    static void _bind_methods();

private:
    // Terminal concrete result under the shared Steam result base.
    godot::Ref<SteamResult> outcome;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamPublisherSettings::Environment);
