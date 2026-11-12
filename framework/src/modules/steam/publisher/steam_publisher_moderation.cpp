// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_moderation.hpp"

#include "modules/steam/publisher/steam_publisher_codec.hpp"
#include "modules/steam/publisher/steam_publisher_transport.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/char_string.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace crimson::steam {
using namespace godot;

namespace {

using publisher_codec::Json;
using publisher_codec::individual_id;
using publisher_codec::member;
using publisher_codec::parse_body;
using publisher_codec::steam_id;
using publisher_codec::unsigned_value;

// Steam accepts a 32-bit unsigned Unix timestamp or sanction duration.
constexpr uint64_t max_u32 = (std::numeric_limits<uint32_t>::max)();
// Bound one review query to a seven-day UTC window.
constexpr int64_t max_window_seconds = 7 * 24 * 60 * 60;
// Valve treats a duration of one year or more as a game ban.
constexpr int64_t maximum_suspension_seconds = 365 * 24 * 60 * 60 - 1;
// Bound decoded list work and the returned page independently of the transport byte limit.
constexpr size_t max_decoded_reports = 2048;
constexpr size_t max_page_reports = 64;

// Copy a validated Godot string to the UTF-8 bytes required by the partner API.
std::string utf8(const String &text) {
    const CharString bytes = text.utf8();
    return {bytes.get_data(), static_cast<size_t>(bytes.length())};
}

// Decode a canonical full-range decimal identifier from a script argument.
bool decimal_id(const String &text, uint64_t &id) {
    return text.length() > 0 && text.length() <= 20 && unsigned_value(Json(utf8(text)), id);
}

// Keep local audit references and Steam descriptions bounded and free of control characters.
bool valid_text(const String &text, int64_t max_bytes) {
    if (text.is_empty() || text.length() > max_bytes || text.strip_edges().is_empty()) {
        return false;
    }
    for (int64_t index = 0; index < text.length(); ++index) {
        const auto character = text[index];
        if (character < 32 || character == 127) {
            return false;
        }
    }
    return text.utf8().length() <= max_bytes;
}

// Submit numeric IDs as exact JSON uint64 values, with credentials outside the body.
void add_input_json(SteamPublisherRequest &request, const Json &data) {
    const auto encoded = data.dump(-1, ' ', false, Json::error_handler_t::replace);
    request.add("input_json", String::utf8(encoded.data(), static_cast<int64_t>(encoded.size())));
}

// Recognize the publisher response envelope and reject explicit vendor failures.
Error response_fields(const Json &root, const Json *&response) {
    response = member(&root, "response");
    if (member(&root, "error") || member(response, "error")) {
        return ERR_QUERY_FAILED;
    }
    if (!response || !response->is_object()) {
        return ERR_INVALID_DATA;
    }
    if (const auto *success = member(response, "success")) {
        if (!success->is_boolean()) {
            return ERR_INVALID_DATA;
        }
        if (!success->get<bool>()) {
            return ERR_QUERY_FAILED;
        }
    }
    if (const auto *result = member(response, "result")) {
        uint64_t value = 0;
        if (*result != "OK" && (!unsigned_value(*result, value) || value != 1)) {
            return ERR_QUERY_FAILED;
        }
    }
    return OK;
}

// Check an optional numeric echo against the exact submitted value.
bool matches_field(const Json *fields, const char *name, uint64_t expected) {
    const auto *field = member(fields, name);
    uint64_t actual = 0;
    return !field || (unsigned_value(*field, actual) && actual == expected);
}

// Decode an optional bounded integer while retaining zero for absent metadata.
bool read_optional(const Json *fields, const char *name, uint64_t limit, int64_t &output) {
    const auto *field = member(fields, name);
    if (!field) {
        return true;
    }
    uint64_t actual = 0;
    if (!unsigned_value(*field, actual) || actual > limit) {
        return false;
    }
    output = static_cast<int64_t>(actual);
    return true;
}

// Confirm only an explicitly recognized acknowledgment and reject mismatched echoes.
Error sanction_ack(const SteamPublisherReply &reply, uint64_t app, uint64_t player, uint64_t report) {
    if (reply.error != OK) {
        return reply.error;
    }
    const Json root = parse_body(reply.body);
    const Json *fields = nullptr;
    const Error decoded = response_fields(root, fields);
    if (decoded != OK) {
        return decoded;
    }
    if ((!member(fields, "success") && !member(fields, "result")) ||
            !matches_field(fields, "appid", app) || !matches_field(fields, "steamid", player) ||
            !matches_field(fields, "reportid", report)) {
        return ERR_INVALID_DATA;
    }
    return OK;
}

} // namespace

void SteamPublisherModeration::_bind_methods() {
    ClassDB::bind_method(D_METHOD("report", "player_id", "evidence"), &SteamPublisherModeration::report);
    ClassDB::bind_method(D_METHOD("reports", "from_unix", "to_unix", "after_report_id"),
            &SteamPublisherModeration::reports, DEFVAL(String("0")));
    ClassDB::bind_method(D_METHOD("suspend", "report_id", "seconds", "reason"), &SteamPublisherModeration::suspend);
    ClassDB::bind_method(D_METHOD("ban", "report_id", "reason"), &SteamPublisherModeration::ban);
    ClassDB::bind_method(D_METHOD("remove_ban", "player_id", "reason"), &SteamPublisherModeration::remove_ban);
}

Ref<SteamModerationRequest> SteamPublisherModeration::make_request(const SteamPublisherState &state,
        SteamModerationRequest::Action action) {
    Ref<SteamModerationRequest> request;
    request.instantiate();
    request->action = action;
    request->app_id = state.app_id;
    request->environment = state.environment;
    return request;
}

Error SteamPublisherModeration::authorize(const std::shared_ptr<SteamPublisherState> &state,
        const Ref<SteamModerationRequest> &request, Ref<SteamModerationAuthorization> &approval) {
    const auto policy = state->moderation_policy;
    if (policy.is_null()) {
        return ERR_UNCONFIGURED;
    }
    if (!policy->GDVIRTUAL_CALL(_authorize, request, approval) || approval.is_null() ||
            approval->request_id != ObjectID(request->get_instance_id())) {
        return ERR_UNAUTHORIZED;
    }
    return SteamPublisherHost::resolve(state) ? OK : ERR_SKIP;
}

Variant SteamPublisherModeration::report(const String &player_id, const Ref<SteamCheatEvidence> &evidence) {
    if (!publisher_on_main_thread()) {
        return SteamCheatReportResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamCheatReportResult::create(ERR_UNAVAILABLE);
    }
    uint64_t target = 0;
    if (!decimal_id(player_id, target) || !individual_id(target) || evidence.is_null() ||
            !valid_text(evidence->match_id, 128) || evidence->reason_code <= 0) {
        return SteamCheatReportResult::create(ERR_INVALID_PARAMETER);
    }
    const bool player_report = evidence->source == SteamCheatEvidence::PLAYER;
    uint64_t reporter = 0;
    if (player_report && (evidence->reporter.is_null() || !evidence->reporter->belongs_to(owner) ||
            !decimal_id(evidence->get_reporter_steam_id(), reporter) || target == reporter)) {
        return SteamCheatReportResult::create(ERR_UNAUTHORIZED);
    }

    const auto review = make_request(*owner, SteamModerationRequest::REPORT);
    review->player_id = player_id;
    review->evidence = evidence;
    Ref<SteamModerationAuthorization> approval;
    const Error authorized = authorize(owner, review, approval);
    if (authorized != OK) {
        return SteamCheatReportResult::create(authorized);
    }
    if (approval->player_id != player_id) {
        return SteamCheatReportResult::create(ERR_UNAUTHORIZED);
    }
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamCheatReportResult::create(ERR_SKIP);
    }

    const uint64_t app = static_cast<uint64_t>(owner->app_id);
    const int64_t reason = evidence->reason_code;
    Json fields = {{"appid", app}, {"steamid", target}, {"appdata", static_cast<uint64_t>(reason)},
            {"playerreport", player_report}, {"noreportid", false}};
    if (player_report) {
        fields["steamidreporter"] = reporter;
    }
    SteamPublisherRequest request{SteamPublisherEndpoint::REPORT_CHEATING};
    add_input_json(request, fields);
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(owner, request, [operation, app, target, player_id,
            match_id = evidence->match_id, reason](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamCheatReportResult::create(reply.error));
            return;
        }
        const Json root = parse_body(reply.body);
        const Json *response = nullptr;
        const Error decoded = response_fields(root, response);
        if (decoded != OK) {
            operation->finish(SteamCheatReportResult::create(decoded));
            return;
        }
        uint64_t report_id = 0;
        int64_t timestamp = 0;
        const auto *reported = member(response, "reportid");
        if (!reported || !unsigned_value(*reported, report_id) || report_id == 0 ||
                !matches_field(response, "appid", app) || !matches_field(response, "steamid", target) ||
                !read_optional(response, "timecreated", max_u32, timestamp)) {
            operation->finish(SteamCheatReportResult::create(ERR_INVALID_DATA));
            return;
        }
        Ref<SteamCheatReport> result;
        result.instantiate();
        result->report_id = String::num_uint64(report_id);
        result->app_id = static_cast<int64_t>(app);
        result->player_id = player_id;
        result->match_id = match_id;
        result->reason_code = reason;
        result->reported_at_unix = timestamp;
        operation->finish(SteamCheatReportResult::create(OK, result));
    });
    return started == OK ? operation->wait() : Variant(SteamCheatReportResult::create(started));
}

Variant SteamPublisherModeration::reports(int64_t from_unix, int64_t to_unix, const String &after_report_id) {
    if (!publisher_on_main_thread()) {
        return SteamCheatReportPageResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamCheatReportPageResult::create(ERR_UNAVAILABLE);
    }
    uint64_t after = 0;
    if (from_unix < 0 || to_unix < from_unix || static_cast<uint64_t>(to_unix) > max_u32 ||
            to_unix - from_unix > max_window_seconds || !decimal_id(after_report_id, after)) {
        return SteamCheatReportPageResult::create(ERR_INVALID_PARAMETER);
    }
    const auto review = make_request(*owner, SteamModerationRequest::READ_REPORTS);
    review->from_unix = from_unix;
    review->to_unix = to_unix;
    review->after_report_id = after_report_id;
    Ref<SteamModerationAuthorization> approval;
    const Error authorized = authorize(owner, review, approval);
    if (authorized != OK) {
        return SteamCheatReportPageResult::create(authorized);
    }
    if (after == (std::numeric_limits<uint64_t>::max)()) {
        Ref<SteamCheatReportPage> empty;
        empty.instantiate();
        return SteamCheatReportPageResult::create(OK, empty);
    }
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host) {
        return SteamCheatReportPageResult::create(ERR_SKIP);
    }

    const uint64_t app = static_cast<uint64_t>(owner->app_id);
    SteamPublisherRequest request{SteamPublisherEndpoint::LIST_CHEATING_REPORTS};
    add_input_json(request, Json{{"appid", app}, {"timebegin", from_unix}, {"timeend", to_unix},
            {"reportidmin", after == 0 ? 0 : after + 1}, {"includereports", true}, {"includebans", false}});
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(owner, request, [operation, app, from_unix, to_unix, after](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamCheatReportPageResult::create(reply.error));
            return;
        }
        const Json root = parse_body(reply.body);
        const Json *response = nullptr;
        const Error decoded = response_fields(root, response);
        const auto *rows = member(response, "reports");
        if (decoded != OK || !rows || !rows->is_array() || rows->size() > max_decoded_reports ||
                !matches_field(response, "appid", app)) {
            operation->finish(SteamCheatReportPageResult::create(decoded == OK ? ERR_INVALID_DATA : decoded));
            return;
        }

        std::vector<std::pair<uint64_t, Ref<SteamCheatReport>>> reports;
        reports.reserve(rows->size());
        for (const auto &row : *rows) {
            uint64_t id = 0;
            const auto *report_id = member(&row, "reportid");
            Ref<SteamCheatReport> report;
            report.instantiate();
            if (!report_id || !unsigned_value(*report_id, id) || id <= after ||
                    !steam_id(member(&row, "steamid"), true, report->player_id) ||
                    !matches_field(&row, "appid", app) ||
                    !read_optional(&row, "timecreated", max_u32, report->reported_at_unix) ||
                    !read_optional(&row, "appdata", (std::numeric_limits<int64_t>::max)(), report->reason_code) ||
                    (report->reported_at_unix != 0 && (report->reported_at_unix < from_unix ||
                            report->reported_at_unix > to_unix))) {
                operation->finish(SteamCheatReportPageResult::create(ERR_INVALID_DATA));
                return;
            }
            report->report_id = String::num_uint64(id);
            report->app_id = static_cast<int64_t>(app);
            reports.emplace_back(id, report);
        }
        std::sort(reports.begin(), reports.end(), [](const auto &left, const auto &right) {
            return left.first < right.first;
        });
        if (std::adjacent_find(reports.begin(), reports.end(), [](const auto &left, const auto &right) {
                return left.first == right.first;
            }) != reports.end()) {
            operation->finish(SteamCheatReportPageResult::create(ERR_INVALID_DATA));
            return;
        }
        Ref<SteamCheatReportPage> page;
        page.instantiate();
        TypedArray<SteamCheatReport> values;
        const size_t count = std::min(reports.size(), max_page_reports);
        for (size_t index = 0; index < count; ++index) {
            values.push_back(reports[index].second);
        }
        values.make_read_only();
        page->reports = values;
        if (reports.size() > count) {
            page->next_report_id = String::num_uint64(reports[count - 1].first);
        }
        operation->finish(SteamCheatReportPageResult::create(OK, page));
    });
    return started == OK ? operation->wait() : Variant(SteamCheatReportPageResult::create(started));
}

Variant SteamPublisherModeration::sanction(SteamModerationRequest::Action action, const String &target,
        int64_t seconds, const String &reason) {
    if (!publisher_on_main_thread()) {
        return SteamModerationReceiptResult::create(ERR_BUSY);
    }
    const auto owner = scope.lock();
    if (!SteamPublisherHost::resolve(owner)) {
        return SteamModerationReceiptResult::create(ERR_UNAVAILABLE);
    }
    const bool removal = action == SteamModerationRequest::REMOVE_BAN;
    uint64_t target_number = 0;
    if (!decimal_id(target, target_number) || target_number == 0 || (removal && !individual_id(target_number)) ||
            !valid_text(reason, 512) || (action == SteamModerationRequest::SUSPEND &&
                    (seconds <= 0 || seconds > maximum_suspension_seconds))) {
        return SteamModerationReceiptResult::create(ERR_INVALID_PARAMETER);
    }
    const auto policy = owner->moderation_policy;
    if (policy.is_null() || !policy->GDVIRTUAL_IS_OVERRIDDEN(_record_outcome)) {
        return SteamModerationReceiptResult::create(ERR_UNCONFIGURED);
    }
    const auto review = make_request(*owner, action);
    if (removal) {
        review->player_id = target;
    } else {
        review->report_id = target;
    }
    review->reason = reason;
    review->duration_seconds = seconds;
    Ref<SteamModerationAuthorization> approval;
    const Error authorized = authorize(owner, review, approval);
    if (authorized != OK && authorized != ERR_SKIP) {
        return SteamModerationReceiptResult::create(authorized);
    }
    uint64_t player = 0;
    if (!decimal_id(approval->player_id, player) || !individual_id(player) ||
            (removal && player != target_number) || !valid_text(approval->audit_id, 128)) {
        return SteamModerationReceiptResult::create(ERR_UNAUTHORIZED);
    }
    Ref<SteamModerationReceipt> receipt;
    receipt.instantiate();
    receipt->action = action;
    receipt->app_id = owner->app_id;
    receipt->player_id = approval->player_id;
    receipt->report_id = review->report_id;
    receipt->duration_seconds = seconds;
    receipt->audit_id = approval->audit_id;

    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const auto complete = [operation, policy, receipt](Error error) {
        receipt->acknowledged = error == OK;
        bool recorded = false;
        const bool audit_written = policy->GDVIRTUAL_CALL(_record_outcome, receipt, error, recorded) && recorded;
        operation->finish(SteamModerationReceiptResult::create(audit_written ? error : ERR_DATABASE_CANT_WRITE,
                receipt));
    };
    auto *host = SteamPublisherHost::resolve(owner);
    if (!host || authorized == ERR_SKIP) {
        complete(ERR_SKIP);
        return operation->wait();
    }
    const uint64_t app = static_cast<uint64_t>(owner->app_id);
    const uint64_t report = removal ? 0 : target_number;
    Json fields = {{"appid", app}, {"steamid", player}};
    if (!removal) {
        fields["reportid"] = report;
        fields["cheatdescription"] = utf8(reason);
        fields["duration"] = seconds;
        fields["delayban"] = false;
        fields["flags"] = 0;
    }
    SteamPublisherRequest request{removal ? SteamPublisherEndpoint::REMOVE_GAME_BAN :
            SteamPublisherEndpoint::REQUEST_GAME_BAN};
    add_input_json(request, fields);
    const Error started = host->send(owner, request, [complete, app, player, report](const SteamPublisherReply &reply) {
        complete(sanction_ack(reply, app, player, report));
    });
    if (started != OK) {
        complete(started);
    }
    return operation->wait();
}

Variant SteamPublisherModeration::suspend(const String &report_id, int64_t seconds, const String &reason) {
    return sanction(SteamModerationRequest::SUSPEND, report_id, seconds, reason);
}

Variant SteamPublisherModeration::ban(const String &report_id, const String &reason) {
    return sanction(SteamModerationRequest::BAN, report_id, 0, reason);
}

Variant SteamPublisherModeration::remove_ban(const String &player_id, const String &reason) {
    return sanction(SteamModerationRequest::REMOVE_BAN, player_id, 0, reason);
}

} // namespace crimson::steam
