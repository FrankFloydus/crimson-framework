// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/publisher/steam_publisher.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

// Game-owned evidence for one authenticated Steam player report or server finding.
class SteamCheatEvidence : public godot::RefCounted {
    GDCLASS(SteamCheatEvidence, godot::RefCounted)

public:
    // Establish whether evidence is a player report or a server finding.
    enum Source {
        PLAYER, // A Steam player verified by this publisher.
        SERVER, // The game server verified by the authorization policy.
    };

    // Pair a verified reporter with the game-owned match and reason.
    static godot::Ref<SteamCheatEvidence> player(const godot::String &match_id, int64_t reason_code,
            const godot::Ref<SteamIdentity> &reporter);
    // Describe authoritative evidence for policy review.
    static godot::Ref<SteamCheatEvidence> server(const godot::String &match_id, int64_t reason_code);
    // Return the game-owned match reference for review.
    godot::String get_match_id() const;
    // Return the positive game-defined reason code sent as Steam app data.
    int64_t get_reason_code() const;
    // Return whether a player or the authoritative game server submitted the evidence.
    Source get_source() const;
    // Return the authenticated reporter's SteamID64, or empty for server evidence.
    godot::String get_reporter_steam_id() const;

protected:
    // Register the immutable record fields and its typed metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // Exact authenticated player identity retained for issuer validation.
    godot::Ref<SteamIdentity> reporter;
    // The game-owned match reference for review.
    godot::String match_id;
    // The positive game-defined reason code sent as Steam app data.
    int64_t reason_code = 0;
    // Whether a player or the authoritative game server submitted the evidence.
    Source source = SERVER;
};

// Immutable moderation facts handed to the injected authorization policy.
class SteamModerationRequest : public godot::RefCounted {
    GDCLASS(SteamModerationRequest, godot::RefCounted)

public:
    // Action selected by the trusted moderation caller.
    enum Action {
        REPORT, // Submit authenticated cheating evidence for review.
        READ_REPORTS, // Read reports in a bounded UTC window.
        SUSPEND, // Request a time-limited game sanction after review.
        BAN, // Request a permanent game ban after review.
        REMOVE_BAN, // Request removal of the app's game ban.
    };

    // Return the exact moderation action the trusted caller wants to perform.
    Action get_action() const;
    // Return the trusted publisher AppID.
    int64_t get_app_id() const;
    // Return the trusted publisher environment selection.
    SteamPublisherSettings::Environment get_environment() const;
    // Return the requested target SteamID64 when known.
    godot::String get_player_id() const;
    // Return the reviewed Valve report ID for a suspension or permanent ban.
    godot::String get_report_id() const;
    // Return the requested sanction or removal reason supplied to the policy.
    godot::String get_reason() const;
    // Return the requested suspension duration, in seconds.
    int64_t get_duration_seconds() const;
    // Return the report evidence, or null for other actions.
    godot::Ref<SteamCheatEvidence> get_evidence() const;
    // Return the inclusive UTC report-window start in Unix seconds.
    int64_t get_from_unix() const;
    // Return the inclusive UTC report-window end in Unix seconds.
    int64_t get_to_unix() const;
    // Return the exclusive decimal report cursor for a read.
    godot::String get_after_report_id() const;

protected:
    // Register the immutable record fields and its typed metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // The exact moderation action the trusted caller wants to perform.
    Action action = REPORT;
    // The trusted publisher AppID.
    int64_t app_id = 0;
    // The trusted publisher environment selection.
    SteamPublisherSettings::Environment environment = SteamPublisherSettings::LIVE;
    // The requested target SteamID64 when known.
    godot::String player_id;
    // The reviewed Valve report ID for a suspension or permanent ban.
    godot::String report_id;
    // The requested sanction or removal reason supplied to the policy.
    godot::String reason;
    // The requested suspension duration, in seconds.
    int64_t duration_seconds = 0;
    // The report evidence, or null for other actions.
    godot::Ref<SteamCheatEvidence> evidence;
    // The inclusive UTC report-window start in Unix seconds.
    int64_t from_unix = 0;
    // The inclusive UTC report-window end in Unix seconds.
    int64_t to_unix = 0;
    // The exclusive decimal report cursor for a read.
    godot::String after_report_id;
};

// A game policy's approval for one exact immutable moderation request.
class SteamModerationAuthorization : public godot::RefCounted {
    GDCLASS(SteamModerationAuthorization, godot::RefCounted)

public:
    // Approve the exact request after the game checks and records authorization.
    static godot::Ref<SteamModerationAuthorization> approve(const godot::Ref<SteamModerationRequest> &request,
            const godot::String &player_id = {}, const godot::String &audit_id = {});
    // Return the policy's resolved target SteamID64 when the action addresses a player.
    godot::String get_player_id() const;
    // Return the trusted backend's durable audit reference for a reviewed action.
    godot::String get_audit_id() const;

protected:
    // Register the immutable record fields and its typed metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // Identity of the immutable request approved by the policy.
    godot::ObjectID request_id;
    // The policy's resolved target SteamID64 when the action addresses a player.
    godot::String player_id;
    // The trusted backend's durable audit reference for a reviewed action.
    godot::String audit_id;
};

// The terminal audit details for one reviewed game sanction or removal.
class SteamModerationReceipt : public godot::RefCounted {
    GDCLASS(SteamModerationReceipt, godot::RefCounted)

public:
    // Return the exact reviewed action recorded by the policy.
    SteamModerationRequest::Action get_action() const;
    // Return the trusted AppID that scoped this action.
    int64_t get_app_id() const;
    // Return the target SteamID64 resolved by the policy.
    godot::String get_player_id() const;
    // Return the reviewed Valve report ID, or empty for removal.
    godot::String get_report_id() const;
    // Return the suspension duration in seconds, or zero otherwise.
    int64_t get_duration_seconds() const;
    // Return the game-owned audit reference reserved before dispatch.
    godot::String get_audit_id() const;
    // Return whether a recognized Steam reply acknowledged the action.
    bool get_acknowledged() const;

protected:
    // Register the immutable record fields and its typed metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // The exact reviewed action recorded by the policy.
    SteamModerationRequest::Action action = SteamModerationRequest::BAN;
    // The trusted AppID that scoped this action.
    int64_t app_id = 0;
    // The target SteamID64 resolved by the policy.
    godot::String player_id;
    // The reviewed Valve report ID, or empty for removal.
    godot::String report_id;
    // The suspension duration in seconds, or zero otherwise.
    int64_t duration_seconds = 0;
    // The game-owned audit reference reserved before dispatch.
    godot::String audit_id;
    // Whether a recognized Steam reply acknowledged the action.
    bool acknowledged = false;
};

// One detached Valve report identity and its known app and target context.
class SteamCheatReport : public godot::RefCounted {
    GDCLASS(SteamCheatReport, godot::RefCounted)

public:
    // Return valve's report ID in unsigned decimal form.
    godot::String get_report_id() const;
    // Return the publisher AppID that scoped the report.
    int64_t get_app_id() const;
    // Return the reported player's SteamID64.
    godot::String get_player_id() const;
    // Return the game-owned match reference when available from local submission.
    godot::String get_match_id() const;
    // Return the game-defined report reason code, or zero when unavailable.
    int64_t get_reason_code() const;
    // Return the Valve report timestamp in Unix seconds, or zero when unavailable.
    int64_t get_reported_at_unix() const;

protected:
    // Register the immutable record fields and its typed metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // Valve's report ID in unsigned decimal form.
    godot::String report_id;
    // The publisher AppID that scoped the report.
    int64_t app_id = 0;
    // The reported player's SteamID64.
    godot::String player_id;
    // The game-owned match reference when available from local submission.
    godot::String match_id;
    // The game-defined report reason code, or zero when unavailable.
    int64_t reason_code = 0;
    // The Valve report timestamp in Unix seconds, or zero when unavailable.
    int64_t reported_at_unix = 0;
};

// A bounded read-only page of Valve reports in increasing report ID order.
class SteamCheatReportPage : public godot::RefCounted {
    GDCLASS(SteamCheatReportPage, godot::RefCounted)

public:
    // Initialize the empty report array as read-only.
    SteamCheatReportPage();
    // Return the detached reports for this page.
    godot::TypedArray<SteamCheatReport> get_reports() const;
    // Return the exclusive continuation cursor, or empty when no additional row is known.
    godot::String get_next_report_id() const;

protected:
    // Register the immutable record fields and its typed metadata.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // The detached reports for this page.
    godot::TypedArray<SteamCheatReport> reports;
    // The exclusive continuation cursor, or empty when no additional row is known.
    godot::String next_report_id;
};

// Trusted game authorization and audit hooks, configured only in the publisher host.
class SteamModerationPolicy : public godot::RefCounted {
    GDCLASS(SteamModerationPolicy, godot::RefCounted)

protected:
    // Register the fail-closed game-owned authorization and audit hooks.
    static void _bind_methods();
    // Resolve and reserve one exact request in the game's trusted authorization and audit store.
    GDVIRTUAL1R(godot::Ref<SteamModerationAuthorization>, _authorize, godot::Ref<SteamModerationRequest>)
    // Report whether the game durably recorded the completion of an authorized action.
    GDVIRTUAL2R(bool, _record_outcome, godot::Ref<SteamModerationReceipt>, godot::Error)

private:
    friend class SteamPublisherModeration;
};

// Typed outcome for a trusted Steam moderation action.
class SteamCheatReportResult : public SteamResult {
    GDCLASS(SteamCheatReportResult, SteamResult)

public:
    // Return the acknowledged Steam report, or null on failure.
    godot::Ref<SteamCheatReport> get_value() const;

protected:
    // Register the typed result payload.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // Build the terminal result without exposing the vendor response body.
    static godot::Ref<SteamCheatReportResult> create(godot::Error error, const godot::Ref<SteamCheatReport> &value = {});

    // The acknowledged Steam report, or null on failure.
    godot::Ref<SteamCheatReport> value;
};

// Typed outcome for a trusted Steam moderation action.
class SteamCheatReportPageResult : public SteamResult {
    GDCLASS(SteamCheatReportPageResult, SteamResult)

public:
    // Return the detached report page, or null on failure.
    godot::Ref<SteamCheatReportPage> get_value() const;

protected:
    // Register the typed result payload.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // Build the terminal result without exposing the vendor response body.
    static godot::Ref<SteamCheatReportPageResult> create(godot::Error error, const godot::Ref<SteamCheatReportPage> &value = {});

    // The detached report page, or null on failure.
    godot::Ref<SteamCheatReportPage> value;
};

// Typed outcome for a trusted Steam moderation action.
class SteamModerationReceiptResult : public SteamResult {
    GDCLASS(SteamModerationReceiptResult, SteamResult)

public:
    // Return the approved action's audit receipt, including when its remote outcome is unresolved. Null when authorization was not granted.
    godot::Ref<SteamModerationReceipt> get_value() const;

protected:
    // Register the typed result payload.
    static void _bind_methods();

private:
    friend class SteamPublisherModeration;

    // Build the terminal result without exposing the vendor response body.
    static godot::Ref<SteamModerationReceiptResult> create(godot::Error error, const godot::Ref<SteamModerationReceipt> &value = {});

    // The approved action's audit receipt, including when its remote outcome is unresolved. Null when authorization was not granted.
    godot::Ref<SteamModerationReceipt> value;
};

// Trusted Steam cheating-report review and app-scoped game sanctions.
class SteamPublisherModeration : public godot::RefCounted {
    GDCLASS(SteamPublisherModeration, godot::RefCounted)

public:
    // Await a report acknowledgment for game-authorized evidence and an eligible target.
    godot::Variant report(const godot::String &player_id, const godot::Ref<SteamCheatEvidence> &evidence);
    // Await a bounded page of publisher reports for a UTC window and exclusive cursor.
    godot::Variant reports(int64_t from_unix, int64_t to_unix, const godot::String &after_report_id = "0");
    // Await a policy-reviewed suspension shorter than 365 days for the resolved player.
    godot::Variant suspend(const godot::String &report_id, int64_t seconds, const godot::String &reason);
    // Await the request for a permanent game ban on the policy-resolved player.
    godot::Variant ban(const godot::String &report_id, const godot::String &reason);
    // Await removal for the policy-authorized player and configured app.
    godot::Variant remove_ban(const godot::String &player_id, const godot::String &reason);

protected:
    // Register directly awaited trusted moderation actions.
    static void _bind_methods();

private:
    friend class SteamPublisher;

    // Capture the app, environment, and selected action before policy review.
    static godot::Ref<SteamModerationRequest> make_request(const SteamPublisherState &state,
            SteamModerationRequest::Action action);
    // Authorize the exact request through the injected game policy.
    static godot::Error authorize(const std::shared_ptr<SteamPublisherState> &state,
            const godot::Ref<SteamModerationRequest> &request, godot::Ref<SteamModerationAuthorization> &approval);
    // Submit an explicitly authorized sanction or removal and settle its audit receipt.
    godot::Variant sanction(SteamModerationRequest::Action action, const godot::String &target,
            int64_t seconds, const godot::String &reason);

    // Exact trusted publisher scope, without keeping it alive after close.
    std::weak_ptr<SteamPublisherState> scope;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamCheatEvidence::Source);
VARIANT_ENUM_CAST(crimson::steam::SteamModerationRequest::Action);
