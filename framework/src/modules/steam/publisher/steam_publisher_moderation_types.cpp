// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_moderation.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {
using namespace godot;

namespace {

// Expose data to scripts without storing it on an instance or enabling edits.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

} // namespace

Ref<SteamCheatEvidence> SteamCheatEvidence::player(const String &match_id, int64_t reason_code,
        const Ref<SteamIdentity> &reporter) {
    auto evidence = server(match_id, reason_code);
    evidence->source = PLAYER;
    evidence->reporter = reporter;
    return evidence;
}

Ref<SteamCheatEvidence> SteamCheatEvidence::server(const String &match_id, int64_t reason_code) {
    Ref<SteamCheatEvidence> evidence;
    evidence.instantiate();
    evidence->match_id = match_id;
    evidence->reason_code = reason_code;
    return evidence;
}

void SteamCheatEvidence::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("player", "match_id", "reason_code", "reporter"),
            &SteamCheatEvidence::player);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("server", "match_id", "reason_code"),
            &SteamCheatEvidence::server);
    ClassDB::bind_method(D_METHOD("get_match_id"), &SteamCheatEvidence::get_match_id);
    ClassDB::bind_method(D_METHOD("get_reason_code"), &SteamCheatEvidence::get_reason_code);
    ClassDB::bind_method(D_METHOD("get_source"), &SteamCheatEvidence::get_source);
    ClassDB::bind_method(D_METHOD("get_reporter_steam_id"), &SteamCheatEvidence::get_reporter_steam_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "match_id", PROPERTY_HINT_NONE, "", read_only), "", "get_match_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "reason_code", PROPERTY_HINT_NONE, "", read_only), "", "get_reason_code");
    PropertyInfo source_info = GetTypeInfo<Source>::get_class_info();
    source_info.name = "source";
    source_info.usage |= read_only;
    ADD_PROPERTY(source_info, "", "get_source");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "reporter_steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_reporter_steam_id");

    BIND_ENUM_CONSTANT(PLAYER);
    BIND_ENUM_CONSTANT(SERVER);
}

String SteamCheatEvidence::get_match_id() const {
    return match_id;
}

int64_t SteamCheatEvidence::get_reason_code() const {
    return reason_code;
}

SteamCheatEvidence::Source SteamCheatEvidence::get_source() const {
    return source;
}

String SteamCheatEvidence::get_reporter_steam_id() const {
    return reporter.is_valid() ? reporter->get_steam_id() : String();
}

void SteamModerationRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_action"), &SteamModerationRequest::get_action);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamModerationRequest::get_app_id);
    ClassDB::bind_method(D_METHOD("get_environment"), &SteamModerationRequest::get_environment);
    ClassDB::bind_method(D_METHOD("get_player_id"), &SteamModerationRequest::get_player_id);
    ClassDB::bind_method(D_METHOD("get_report_id"), &SteamModerationRequest::get_report_id);
    ClassDB::bind_method(D_METHOD("get_reason"), &SteamModerationRequest::get_reason);
    ClassDB::bind_method(D_METHOD("get_duration_seconds"), &SteamModerationRequest::get_duration_seconds);
    ClassDB::bind_method(D_METHOD("get_evidence"), &SteamModerationRequest::get_evidence);
    ClassDB::bind_method(D_METHOD("get_from_unix"), &SteamModerationRequest::get_from_unix);
    ClassDB::bind_method(D_METHOD("get_to_unix"), &SteamModerationRequest::get_to_unix);
    ClassDB::bind_method(D_METHOD("get_after_report_id"), &SteamModerationRequest::get_after_report_id);
    PropertyInfo action_info = GetTypeInfo<Action>::get_class_info();
    action_info.name = "action";
    action_info.usage |= read_only;
    ADD_PROPERTY(action_info, "", "get_action");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
    PropertyInfo environment_info = GetTypeInfo<SteamPublisherSettings::Environment>::get_class_info();
    environment_info.name = "environment";
    environment_info.usage |= read_only;
    ADD_PROPERTY(environment_info, "", "get_environment");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_id", PROPERTY_HINT_NONE, "", read_only), "", "get_player_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "report_id", PROPERTY_HINT_NONE, "", read_only), "", "get_report_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "reason", PROPERTY_HINT_NONE, "", read_only), "", "get_reason");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "duration_seconds", PROPERTY_HINT_NONE, "", read_only), "", "get_duration_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "evidence", PROPERTY_HINT_RESOURCE_TYPE, SteamCheatEvidence::get_class_static(), read_only), "", "get_evidence");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "from_unix", PROPERTY_HINT_NONE, "", read_only), "", "get_from_unix");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "to_unix", PROPERTY_HINT_NONE, "", read_only), "", "get_to_unix");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "after_report_id", PROPERTY_HINT_NONE, "", read_only), "", "get_after_report_id");

    BIND_ENUM_CONSTANT(REPORT);
    BIND_ENUM_CONSTANT(READ_REPORTS);
    BIND_ENUM_CONSTANT(SUSPEND);
    BIND_ENUM_CONSTANT(BAN);
    BIND_ENUM_CONSTANT(REMOVE_BAN);
}

SteamModerationRequest::Action SteamModerationRequest::get_action() const {
    return action;
}

int64_t SteamModerationRequest::get_app_id() const {
    return app_id;
}

SteamPublisherSettings::Environment SteamModerationRequest::get_environment() const {
    return environment;
}

String SteamModerationRequest::get_player_id() const {
    return player_id;
}

String SteamModerationRequest::get_report_id() const {
    return report_id;
}

String SteamModerationRequest::get_reason() const {
    return reason;
}

int64_t SteamModerationRequest::get_duration_seconds() const {
    return duration_seconds;
}

Ref<SteamCheatEvidence> SteamModerationRequest::get_evidence() const {
    return evidence;
}

int64_t SteamModerationRequest::get_from_unix() const {
    return from_unix;
}

int64_t SteamModerationRequest::get_to_unix() const {
    return to_unix;
}

String SteamModerationRequest::get_after_report_id() const {
    return after_report_id;
}

Ref<SteamModerationAuthorization> SteamModerationAuthorization::approve(
        const Ref<SteamModerationRequest> &request, const String &player_id, const String &audit_id) {
    if (request.is_null()) {
        return {};
    }
    Ref<SteamModerationAuthorization> authorization;
    authorization.instantiate();
    authorization->request_id = ObjectID(request->get_instance_id());
    authorization->player_id = player_id.is_empty() ? request->get_player_id() : player_id;
    authorization->audit_id = audit_id;
    return authorization;
}

void SteamModerationAuthorization::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("approve", "request", "player_id", "audit_id"),
            &SteamModerationAuthorization::approve, DEFVAL(String()), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_player_id"), &SteamModerationAuthorization::get_player_id);
    ClassDB::bind_method(D_METHOD("get_audit_id"), &SteamModerationAuthorization::get_audit_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_id", PROPERTY_HINT_NONE, "", read_only), "", "get_player_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "audit_id", PROPERTY_HINT_NONE, "", read_only), "", "get_audit_id");
}

String SteamModerationAuthorization::get_player_id() const {
    return player_id;
}

String SteamModerationAuthorization::get_audit_id() const {
    return audit_id;
}

void SteamModerationReceipt::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_action"), &SteamModerationReceipt::get_action);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamModerationReceipt::get_app_id);
    ClassDB::bind_method(D_METHOD("get_player_id"), &SteamModerationReceipt::get_player_id);
    ClassDB::bind_method(D_METHOD("get_report_id"), &SteamModerationReceipt::get_report_id);
    ClassDB::bind_method(D_METHOD("get_duration_seconds"), &SteamModerationReceipt::get_duration_seconds);
    ClassDB::bind_method(D_METHOD("get_audit_id"), &SteamModerationReceipt::get_audit_id);
    ClassDB::bind_method(D_METHOD("get_acknowledged"), &SteamModerationReceipt::get_acknowledged);
    PropertyInfo action_info = GetTypeInfo<SteamModerationRequest::Action>::get_class_info();
    action_info.name = "action";
    action_info.usage |= read_only;
    ADD_PROPERTY(action_info, "", "get_action");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_id", PROPERTY_HINT_NONE, "", read_only), "", "get_player_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "report_id", PROPERTY_HINT_NONE, "", read_only), "", "get_report_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "duration_seconds", PROPERTY_HINT_NONE, "", read_only), "", "get_duration_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "audit_id", PROPERTY_HINT_NONE, "", read_only), "", "get_audit_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "acknowledged", PROPERTY_HINT_NONE, "", read_only), "", "get_acknowledged");
}

SteamModerationRequest::Action SteamModerationReceipt::get_action() const {
    return action;
}

int64_t SteamModerationReceipt::get_app_id() const {
    return app_id;
}

String SteamModerationReceipt::get_player_id() const {
    return player_id;
}

String SteamModerationReceipt::get_report_id() const {
    return report_id;
}

int64_t SteamModerationReceipt::get_duration_seconds() const {
    return duration_seconds;
}

String SteamModerationReceipt::get_audit_id() const {
    return audit_id;
}

bool SteamModerationReceipt::get_acknowledged() const {
    return acknowledged;
}

void SteamCheatReport::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_report_id"), &SteamCheatReport::get_report_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamCheatReport::get_app_id);
    ClassDB::bind_method(D_METHOD("get_player_id"), &SteamCheatReport::get_player_id);
    ClassDB::bind_method(D_METHOD("get_match_id"), &SteamCheatReport::get_match_id);
    ClassDB::bind_method(D_METHOD("get_reason_code"), &SteamCheatReport::get_reason_code);
    ClassDB::bind_method(D_METHOD("get_reported_at_unix"), &SteamCheatReport::get_reported_at_unix);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "report_id", PROPERTY_HINT_NONE, "", read_only), "", "get_report_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_id", PROPERTY_HINT_NONE, "", read_only), "", "get_player_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "match_id", PROPERTY_HINT_NONE, "", read_only), "", "get_match_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "reason_code", PROPERTY_HINT_NONE, "", read_only), "", "get_reason_code");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "reported_at_unix", PROPERTY_HINT_NONE, "", read_only), "", "get_reported_at_unix");
}

String SteamCheatReport::get_report_id() const {
    return report_id;
}

int64_t SteamCheatReport::get_app_id() const {
    return app_id;
}

String SteamCheatReport::get_player_id() const {
    return player_id;
}

String SteamCheatReport::get_match_id() const {
    return match_id;
}

int64_t SteamCheatReport::get_reason_code() const {
    return reason_code;
}

int64_t SteamCheatReport::get_reported_at_unix() const {
    return reported_at_unix;
}

SteamCheatReportPage::SteamCheatReportPage() {
    reports.make_read_only();
}

void SteamCheatReportPage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_reports"), &SteamCheatReportPage::get_reports);
    ClassDB::bind_method(D_METHOD("get_next_report_id"), &SteamCheatReportPage::get_next_report_id);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "reports", PROPERTY_HINT_ARRAY_TYPE, SteamCheatReport::get_class_static(), read_only), "", "get_reports");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "next_report_id", PROPERTY_HINT_NONE, "", read_only), "", "get_next_report_id");
}

TypedArray<SteamCheatReport> SteamCheatReportPage::get_reports() const {
    return reports;
}

String SteamCheatReportPage::get_next_report_id() const {
    return next_report_id;
}

void SteamModerationPolicy::_bind_methods() {
    GDVIRTUAL_BIND(_authorize, "request");
    GDVIRTUAL_BIND(_record_outcome, "receipt", "error");
}

void SteamCheatReportResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamCheatReportResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamCheatReport::get_class_static(), read_only), "", "get_value");
}

Ref<SteamCheatReport> SteamCheatReportResult::get_value() const {
    return value;
}

Ref<SteamCheatReportResult> SteamCheatReportResult::create(Error error, const Ref<SteamCheatReport> &value) {
    Ref<SteamCheatReportResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamCheatReportPageResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamCheatReportPageResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamCheatReportPage::get_class_static(), read_only), "", "get_value");
}

Ref<SteamCheatReportPage> SteamCheatReportPageResult::get_value() const {
    return value;
}

Ref<SteamCheatReportPageResult> SteamCheatReportPageResult::create(Error error, const Ref<SteamCheatReportPage> &value) {
    Ref<SteamCheatReportPageResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamModerationReceiptResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamModerationReceiptResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamModerationReceipt::get_class_static(), read_only), "", "get_value");
}

Ref<SteamModerationReceipt> SteamModerationReceiptResult::get_value() const {
    return value;
}

Ref<SteamModerationReceiptResult> SteamModerationReceiptResult::create(Error error, const Ref<SteamModerationReceipt> &value) {
    Ref<SteamModerationReceiptResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

} // namespace crimson::steam
