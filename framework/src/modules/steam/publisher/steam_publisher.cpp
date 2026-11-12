// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/publisher/steam_publisher_transport.hpp"
#include "modules/steam/publisher/steam_game_notifications.hpp"
#include "modules/steam/publisher/steam_ownership.hpp"
#include "modules/steam/publisher/steam_publisher_inventory.hpp"
#include "modules/steam/publisher/steam_publisher_codec.hpp"
#include "modules/steam/client/steam_leaderboards.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <charconv>
#include <limits>
#include <string>
#include <string_view>

namespace crimson::steam {
using namespace godot;

namespace publisher_codec {

// Parse only bounded transport replies and keep parser diagnostics out of caller messages.
Json parse_body(const PackedByteArray &body) {
    return body.is_empty() ? Json() : Json::parse(body.ptr(), body.ptr() + body.size(), nullptr, false);
}

// Read a property from an object without constructing a missing value.
const Json *member(const Json *object, const char *name) {
    if (!object || !object->is_object()) {
        return nullptr;
    }
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &*found;
}

// Decode a canonical unsigned decimal string or an exact JSON uint64.
bool unsigned_value(const Json &node, uint64_t &output) {
    if (node.is_number_unsigned()) {
        output = node.get<uint64_t>();
        return true;
    }
    if (!node.is_string()) {
        return false;
    }
    const auto &text = node.get_ref<const std::string &>();
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) {
        return false;
    }
    const char *end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, output);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

// Validate the public individual account layout from the Steamworks CSteamID contract.
bool individual_id(uint64_t id) {
    const uint64_t universe = id >> 56;
    const uint64_t account_type = (id >> 52) & 15;
    const uint64_t instance = (id >> 32) & 0xfffff;
    return universe == 1 && account_type == 1 && instance <= 4 && static_cast<uint32_t>(id) != 0;
}

// Read a verified account ID, allowing Valve to omit an optional license owner.
bool steam_id(const Json *node, bool required, String &output) {
    if (!node || node->is_null()) {
        return !required;
    }
    uint64_t id = 0;
    if (!unsigned_value(*node, id)) {
        return false;
    }
    if (id == 0 && !required) {
        return true;
    }
    if (!individual_id(id)) {
        return false;
    }
    output = String::utf8(std::to_string(id).c_str());
    return true;
}

} // namespace publisher_codec

using namespace publisher_codec;

namespace {

// Published identity and license snapshots are immutable.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;
// Maximum ticket size from AuthTicketForWebApiResponse_t in Steamworks SDK 1.65.
constexpr int64_t maximum_ticket_bytes = 2560;
// Bound service audiences and credential references passed through trusted configuration.
constexpr int64_t maximum_label_bytes = 128;

// Check positive AppID and board IDs without signed truncation.
bool valid_u32(int64_t value) {
    return value > 0 && value <= (std::numeric_limits<uint32_t>::max)();
}

// Accept a nonempty UTF-8 label with no embedded null or ASCII control characters.
bool valid_label(const String &value) {
    if (value.is_empty() || value.length() > maximum_label_bytes) {
        return false;
    }
    const CharString utf8 = value.utf8();
    if (utf8.length() > maximum_label_bytes) {
        return false;
    }
    for (int64_t index = 0; index < utf8.length(); ++index) {
        const auto byte = static_cast<uint8_t>(utf8[index]);
        if (byte < 0x20 || byte == 0x7f) {
            return false;
        }
    }
    return true;
}

// Copy a bounded documented text field when Valve supplied it.
bool optional_text(const Json *node, String &output) {
    if (!node) {
        return true;
    }
    if (!node->is_string()) {
        return false;
    }
    const auto &text = node->get_ref<const std::string &>();
    if (text.size() > 128 || text.find(char(0)) != std::string::npos) {
        return false;
    }
    output = String::utf8(text.data(), text.size());
    return true;
}

// Copy an optional documented ownership flag without coercing its type.
bool optional_bool(const Json *node, bool &output) {
    if (!node) {
        return true;
    }
    if (!node->is_boolean()) {
        return false;
    }
    output = node->get<bool>();
    return true;
}

// Read the license envelope or the documented flat ownership fields.
const Json *ownership_fields(const Json &root) {
    if (const Json *ownership = member(&root, "appownership")) {
        return ownership;
    }
    if (const Json *response = member(&root, "response")) {
        return response;
    }
    return &root;
}

// Treat a successful HTTP response as a submission acknowledgment with no inferred score change.
bool score_acknowledged(const PackedByteArray &body) {
    if (body.is_empty()) {
        return true;
    }
    const Json root = parse_body(body);
    if (!root.is_object()) {
        return false;
    }
    const Json *response = member(&root, "response");
    if (member(&root, "error") || member(response, "error")) {
        return false;
    }
    const Json *success = member(response ? response : &root, "success");
    return !success || (success->is_boolean() && success->get<bool>());
}

} // namespace

SteamPublisherSettings::SteamPublisherSettings() = default;
SteamPublisherSettings::~SteamPublisherSettings() = default;

void SteamPublisherSettings::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_app_id", "app_id"), &SteamPublisherSettings::set_app_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamPublisherSettings::get_app_id);
    ClassDB::bind_method(D_METHOD("set_environment", "environment"), &SteamPublisherSettings::set_environment);
    ClassDB::bind_method(D_METHOD("get_environment"), &SteamPublisherSettings::get_environment);
    ClassDB::bind_method(D_METHOD("set_publisher_key_ref", "reference"), &SteamPublisherSettings::set_publisher_key_ref);
    ClassDB::bind_method(D_METHOD("get_publisher_key_ref"), &SteamPublisherSettings::get_publisher_key_ref);
    ClassDB::bind_method(D_METHOD("set_encrypted_ticket_key_ref", "reference"), &SteamPublisherSettings::set_encrypted_ticket_key_ref);
    ClassDB::bind_method(D_METHOD("get_encrypted_ticket_key_ref"), &SteamPublisherSettings::get_encrypted_ticket_key_ref);
    ClassDB::bind_method(D_METHOD("set_host", "host"), &SteamPublisherSettings::set_host);
    ClassDB::bind_method(D_METHOD("get_host"), &SteamPublisherSettings::get_host);
    ClassDB::bind_method(D_METHOD("set_notification_store", "store"), &SteamPublisherSettings::set_notification_store);
    ClassDB::bind_method(D_METHOD("get_notification_store"), &SteamPublisherSettings::get_notification_store);
    ClassDB::bind_method(D_METHOD("set_moderation_policy", "policy"), &SteamPublisherSettings::set_moderation_policy);
    ClassDB::bind_method(D_METHOD("get_moderation_policy"), &SteamPublisherSettings::get_moderation_policy);
    ClassDB::bind_method(D_METHOD("set_purchase_store", "store"), &SteamPublisherSettings::set_purchase_store);
    ClassDB::bind_method(D_METHOD("get_purchase_store"), &SteamPublisherSettings::get_purchase_store);
    ClassDB::bind_method(D_METHOD("set_ownership_challenge_store", "store"),
            &SteamPublisherSettings::set_ownership_challenge_store);
    ClassDB::bind_method(D_METHOD("get_ownership_challenge_store"), &SteamPublisherSettings::get_ownership_challenge_store);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id"), "set_app_id", "get_app_id");
    PropertyInfo environment_property = GetTypeInfo<Environment>::get_class_info();
    environment_property.name = "environment";
    ADD_PROPERTY(environment_property, "set_environment", "get_environment");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "publisher_key_ref"), "set_publisher_key_ref", "get_publisher_key_ref");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "encrypted_ticket_key_ref"), "set_encrypted_ticket_key_ref", "get_encrypted_ticket_key_ref");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "host", PROPERTY_HINT_NODE_TYPE, SteamPublisherHost::get_class_static(),
                         PROPERTY_USAGE_NO_INSTANCE_STATE), "set_host", "get_host");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "notification_store", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamNotificationStore::get_class_static(), PROPERTY_USAGE_NO_INSTANCE_STATE),
            "set_notification_store", "get_notification_store");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "moderation_policy", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamModerationPolicy::get_class_static(), PROPERTY_USAGE_NO_INSTANCE_STATE),
            "set_moderation_policy", "get_moderation_policy");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "purchase_store", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamPurchaseStore::get_class_static(), PROPERTY_USAGE_NO_INSTANCE_STATE),
            "set_purchase_store", "get_purchase_store");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "ownership_challenge_store", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamOwnershipChallengeStore::get_class_static(), PROPERTY_USAGE_NO_INSTANCE_STATE),
            "set_ownership_challenge_store", "get_ownership_challenge_store");
    BIND_ENUM_CONSTANT(LIVE);
    BIND_ENUM_CONSTANT(SANDBOX);
}

void SteamPublisherSettings::set_app_id(int64_t value) {
    app_id = value;
}

int64_t SteamPublisherSettings::get_app_id() const {
    return app_id;
}

void SteamPublisherSettings::set_environment(Environment value) {
    environment = value;
}

SteamPublisherSettings::Environment SteamPublisherSettings::get_environment() const {
    return environment;
}

void SteamPublisherSettings::set_publisher_key_ref(const StringName &value) {
    publisher_key_ref = value;
}

StringName SteamPublisherSettings::get_publisher_key_ref() const {
    return publisher_key_ref;
}

void SteamPublisherSettings::set_encrypted_ticket_key_ref(const StringName &value) {
    encrypted_ticket_key_ref = value;
}

StringName SteamPublisherSettings::get_encrypted_ticket_key_ref() const {
    return encrypted_ticket_key_ref;
}

void SteamPublisherSettings::set_host(SteamPublisherHost *value) {
    host_id = value ? ObjectID(value->get_instance_id()) : ObjectID();
}

SteamPublisherHost *SteamPublisherSettings::get_host() const {
    return Object::cast_to<SteamPublisherHost>(ObjectDB::get_instance(host_id));
}

void SteamPublisherSettings::set_notification_store(const Ref<SteamNotificationStore> &value) {
    notification_store = value;
}

Ref<SteamNotificationStore> SteamPublisherSettings::get_notification_store() const {
    return notification_store;
}

void SteamPublisherSettings::set_moderation_policy(const Ref<SteamModerationPolicy> &value) {
    moderation_policy = value;
}

Ref<SteamModerationPolicy> SteamPublisherSettings::get_moderation_policy() const {
    return moderation_policy;
}

void SteamPublisherSettings::set_purchase_store(const Ref<SteamPurchaseStore> &value) {
    purchase_store = value;
}

Ref<SteamPurchaseStore> SteamPublisherSettings::get_purchase_store() const {
    return purchase_store;
}

void SteamPublisherSettings::set_ownership_challenge_store(const Ref<SteamOwnershipChallengeStore> &value) {
    ownership_challenge_store = value;
}

Ref<SteamOwnershipChallengeStore> SteamPublisherSettings::get_ownership_challenge_store() const {
    return ownership_challenge_store;
}

void SteamIdentity::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamIdentity::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_license_owner_steam_id"), &SteamIdentity::get_license_owner_steam_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamIdentity::get_app_id);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "license_owner_steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_license_owner_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
}

String SteamIdentity::get_steam_id() const {
    return steam_id;
}

String SteamIdentity::get_license_owner_steam_id() const {
    return license_owner_steam_id;
}

int64_t SteamIdentity::get_app_id() const {
    return app_id;
}

bool SteamIdentity::belongs_to(const std::shared_ptr<SteamPublisherState> &publisher) const {
    return publisher && issuer.lock() == publisher && app_id == publisher->app_id && !steam_id.is_empty();
}

void SteamIdentityResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamIdentityResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamIdentity::get_class_static(), read_only), "", "get_value");
}

Ref<SteamIdentity> SteamIdentityResult::get_value() const {
    return value;
}

Ref<SteamIdentityResult> SteamIdentityResult::create(Error error, const Ref<SteamIdentity> &value) {
    Ref<SteamIdentityResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamAppOwnership::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamAppOwnership::get_app_id);
    ClassDB::bind_method(D_METHOD("get_owns_app"), &SteamAppOwnership::get_owns_app);
    ClassDB::bind_method(D_METHOD("get_permanent"), &SteamAppOwnership::get_permanent);
    ClassDB::bind_method(D_METHOD("get_license_owner_steam_id"), &SteamAppOwnership::get_license_owner_steam_id);
    ClassDB::bind_method(D_METHOD("get_acquired_at"), &SteamAppOwnership::get_acquired_at);
    ClassDB::bind_method(D_METHOD("get_expires_at"), &SteamAppOwnership::get_expires_at);
    ClassDB::bind_method(D_METHOD("get_site_license"), &SteamAppOwnership::get_site_license);
    ClassDB::bind_method(D_METHOD("get_user_canceled"), &SteamAppOwnership::get_user_canceled);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", read_only), "", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "owns_app", PROPERTY_HINT_NONE, "", read_only), "", "get_owns_app");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "permanent", PROPERTY_HINT_NONE, "", read_only), "", "get_permanent");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "license_owner_steam_id", PROPERTY_HINT_NONE, "", read_only), "", "get_license_owner_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "acquired_at", PROPERTY_HINT_NONE, "", read_only), "", "get_acquired_at");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "expires_at", PROPERTY_HINT_NONE, "", read_only), "", "get_expires_at");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "site_license", PROPERTY_HINT_NONE, "", read_only), "", "get_site_license");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "user_canceled", PROPERTY_HINT_NONE, "", read_only), "", "get_user_canceled");
}

int64_t SteamAppOwnership::get_app_id() const {
    return app_id;
}

bool SteamAppOwnership::get_owns_app() const {
    return owns_app;
}

bool SteamAppOwnership::get_permanent() const {
    return permanent;
}

String SteamAppOwnership::get_license_owner_steam_id() const {
    return license_owner_steam_id;
}

String SteamAppOwnership::get_acquired_at() const {
    return acquired_at;
}

String SteamAppOwnership::get_expires_at() const {
    return expires_at;
}

bool SteamAppOwnership::get_site_license() const {
    return site_license;
}

bool SteamAppOwnership::get_user_canceled() const {
    return user_canceled;
}

void SteamAppOwnershipResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamAppOwnershipResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamAppOwnership::get_class_static(), read_only), "", "get_value");
}

Ref<SteamAppOwnership> SteamAppOwnershipResult::get_value() const {
    return value;
}

Ref<SteamAppOwnershipResult> SteamAppOwnershipResult::create(Error error, const Ref<SteamAppOwnership> &value) {
    Ref<SteamAppOwnershipResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamPublisherAuth::_bind_methods() {
    ClassDB::bind_method(D_METHOD("verify_ownership", "ticket", "expected_challenge", "dlc_ids"),
            &SteamPublisherAuth::verify_ownership, DEFVAL(PackedInt64Array()));
    ClassDB::bind_method(D_METHOD("verify", "ticket_bytes", "audience"), &SteamPublisherAuth::verify);
    ClassDB::bind_method(D_METHOD("owns_app", "identity", "app_id"), &SteamPublisherAuth::owns_app);
}

Variant SteamPublisherAuth::verify(const PackedByteArray &ticket_bytes, const String &audience) {
    if (!publisher_on_main_thread()) {
        return SteamIdentityResult::create(ERR_BUSY);
    }
    const auto issuer = scope.lock();
    auto *host = SteamPublisherHost::resolve(issuer);
    if (!host) {
        return SteamIdentityResult::create(ERR_UNAVAILABLE);
    }
    if (ticket_bytes.is_empty() || ticket_bytes.size() > maximum_ticket_bytes || !valid_label(audience)) {
        return SteamIdentityResult::create(ERR_INVALID_PARAMETER);
    }
    SteamPublisherRequest request{SteamPublisherEndpoint::VERIFY_TICKET};
    request.add("appid", String::num_int64(issuer->app_id));
    request.add("ticket", ticket_bytes.hex_encode());
    request.add("identity", audience);

    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(issuer, request, [operation, issuer = scope, audience](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamIdentityResult::create(reply.error));
            return;
        }
        const auto owner = issuer.lock();
        if (!owner || !owner->live) {
            operation->finish(SteamIdentityResult::create(ERR_SKIP));
            return;
        }
        const Json root = parse_body(reply.body);
        const Json *response = member(&root, "response");
        const Json *params = member(response, "params");
        const Json *result = member(params, "result");
        if (member(response, "error") || (result && result->is_string() && *result != "OK")) {
            operation->finish(SteamIdentityResult::create(ERR_UNAUTHORIZED));
            return;
        }
        if (!result || !result->is_string()) {
            operation->finish(SteamIdentityResult::create(ERR_INVALID_DATA));
            return;
        }
        if (const Json *returned_app = member(params, "appid")) {
            uint64_t app = 0;
            if (!unsigned_value(*returned_app, app) || app != static_cast<uint64_t>(owner->app_id)) {
                operation->finish(SteamIdentityResult::create(ERR_UNAUTHORIZED));
                return;
            }
        }
        if (const Json *returned_audience = member(params, "identity")) {
            String verified_audience;
            if (!optional_text(returned_audience, verified_audience) || verified_audience != audience) {
                operation->finish(SteamIdentityResult::create(ERR_UNAUTHORIZED));
                return;
            }
        }
        Ref<SteamIdentity> identity;
        identity.instantiate();
        if (!steam_id(member(params, "steamid"), true, identity->steam_id) ||
                !steam_id(member(params, "ownersteamid"), false, identity->license_owner_steam_id)) {
            operation->finish(SteamIdentityResult::create(ERR_INVALID_DATA));
            return;
        }
        identity->app_id = owner->app_id;
        identity->issuer = owner;
        operation->finish(SteamIdentityResult::create(OK, identity));
    });
    return started == OK ? operation->wait() : Variant(SteamIdentityResult::create(started));
}

Variant SteamPublisherAuth::owns_app(const Ref<SteamIdentity> &identity, int64_t app_id) {
    if (!publisher_on_main_thread()) {
        return SteamAppOwnershipResult::create(ERR_BUSY);
    }
    const auto issuer = scope.lock();
    auto *host = SteamPublisherHost::resolve(issuer);
    if (!host) {
        return SteamAppOwnershipResult::create(ERR_UNAVAILABLE);
    }
    if (!valid_u32(app_id)) {
        return SteamAppOwnershipResult::create(ERR_INVALID_PARAMETER);
    }
    if (identity.is_null() || !identity->belongs_to(issuer)) {
        return SteamAppOwnershipResult::create(ERR_UNAUTHORIZED);
    }
    SteamPublisherRequest request{SteamPublisherEndpoint::CHECK_APP};
    request.add("appid", String::num_int64(app_id));
    request.add("steamid", identity->steam_id);

    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(issuer, request, [operation, app_id](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamAppOwnershipResult::create(reply.error));
            return;
        }
        const Json root = parse_body(reply.body);
        const Json *fields = ownership_fields(root);
        const Json *owns = member(fields, "ownsapp");
        if (!owns || !owns->is_boolean()) {
            operation->finish(SteamAppOwnershipResult::create(ERR_INVALID_DATA));
            return;
        }
        if (const Json *returned_app = member(fields, "appid")) {
            uint64_t app = 0;
            if (!unsigned_value(*returned_app, app) || app != static_cast<uint64_t>(app_id)) {
                operation->finish(SteamAppOwnershipResult::create(ERR_INVALID_DATA));
                return;
            }
        }
        Ref<SteamAppOwnership> ownership;
        ownership.instantiate();
        ownership->app_id = app_id;
        ownership->owns_app = owns->get<bool>();
        if (!steam_id(member(fields, "ownersteamid"), false, ownership->license_owner_steam_id) ||
                !optional_bool(member(fields, "permanent"), ownership->permanent) ||
                !optional_bool(member(fields, "sitelicense"), ownership->site_license) ||
                !optional_bool(member(fields, "usercanceled"), ownership->user_canceled) ||
                !optional_text(member(fields, "timestamp"), ownership->acquired_at) ||
                !optional_text(member(fields, "timeexpires"), ownership->expires_at)) {
            operation->finish(SteamAppOwnershipResult::create(ERR_INVALID_DATA));
            return;
        }
        operation->finish(SteamAppOwnershipResult::create(OK, ownership));
    });
    return started == OK ? operation->wait() : Variant(SteamAppOwnershipResult::create(started));
}

void SteamPublisherLeaderboards::_bind_methods() {
    ClassDB::bind_method(D_METHOD("submit_score", "player", "board_id", "score", "details"),
            &SteamPublisherLeaderboards::submit_score, DEFVAL(PackedByteArray()));
}

Variant SteamPublisherLeaderboards::submit_score(const Ref<SteamIdentity> &player, int64_t board_id, int64_t score,
        const PackedByteArray &details) {
    if (!publisher_on_main_thread()) {
        return SteamLeaderboardSubmissionResult::create(ERR_BUSY);
    }
    const auto issuer = scope.lock();
    auto *host = SteamPublisherHost::resolve(issuer);
    if (!host) {
        return SteamLeaderboardSubmissionResult::create(ERR_UNAVAILABLE);
    }
    if (player.is_null() || !player->belongs_to(issuer)) {
        return SteamLeaderboardSubmissionResult::create(ERR_UNAUTHORIZED);
    }
    if (!valid_u32(board_id) || score < (std::numeric_limits<int32_t>::min)() ||
            score > (std::numeric_limits<int32_t>::max)() || details.size() > 256) {
        return SteamLeaderboardSubmissionResult::create(ERR_INVALID_PARAMETER);
    }

    SteamPublisherRequest request{SteamPublisherEndpoint::SUBMIT_SCORE};
    request.add("appid", String::num_int64(issuer->app_id));
    request.add("leaderboardid", String::num_int64(board_id));
    request.add("steamid", player->steam_id);
    request.add("score", String::num_int64(score));
    request.add("scoremethod", "KeepBest");
    if (!details.is_empty()) {
        request.add_bytes("details", details);
    }
    Ref<SteamPublisherCompletion> operation;
    operation.instantiate();
    const Error started = host->send(issuer, request, [operation, score](const SteamPublisherReply &reply) {
        if (reply.error != OK) {
            operation->finish(SteamLeaderboardSubmissionResult::create(reply.error));
            return;
        }
        if (!score_acknowledged(reply.body)) {
            operation->finish(SteamLeaderboardSubmissionResult::create(ERR_INVALID_DATA));
            return;
        }
        Ref<SteamLeaderboardSubmission> submission;
        submission.instantiate();
        submission->submitted_score = score;
        operation->finish(SteamLeaderboardSubmissionResult::create(OK, submission));
    });
    return started == OK ? operation->wait() : Variant(SteamLeaderboardSubmissionResult::create(started));
}

void SteamPublisher::_bind_methods() {
    ClassDB::bind_static_method("SteamPublisher", D_METHOD("boot", "settings"), &SteamPublisher::boot);
    ClassDB::bind_method(D_METHOD("is_ready"), &SteamPublisher::is_ready);
    ClassDB::bind_method(D_METHOD("close"), &SteamPublisher::close);
    ClassDB::bind_method(D_METHOD("auth"), &SteamPublisher::auth);
    ClassDB::bind_method(D_METHOD("leaderboards"), &SteamPublisher::leaderboards);
    ClassDB::bind_method(D_METHOD("game_notifications"), &SteamPublisher::game_notifications);
    ClassDB::bind_method(D_METHOD("moderation"), &SteamPublisher::moderation);
    ClassDB::bind_method(D_METHOD("purchases"), &SteamPublisher::purchases);
    ClassDB::bind_method(D_METHOD("inventory"), &SteamPublisher::inventory);
}

SteamPublisher::SteamPublisher() = default;

SteamPublisher::~SteamPublisher() {
    if (scope) {
        scope->live = false;
    }
}

Ref<SteamPublisherResult> SteamPublisher::boot(const Ref<SteamPublisherSettings> &settings) {
    if (!publisher_on_main_thread()) {
        return SteamPublisherResult::create(ERR_BUSY);
    }
    if (settings.is_null() || !valid_u32(settings->get_app_id()) ||
            (settings->get_publisher_key_ref().is_empty() && settings->get_encrypted_ticket_key_ref().is_empty()) ||
            (!settings->get_publisher_key_ref().is_empty() && !valid_label(String(settings->get_publisher_key_ref()))) ||
            (!settings->get_encrypted_ticket_key_ref().is_empty() &&
                    !valid_label(String(settings->get_encrypted_ticket_key_ref()))) ||
            (settings->get_environment() != SteamPublisherSettings::LIVE &&
                    settings->get_environment() != SteamPublisherSettings::SANDBOX)) {
        return SteamPublisherResult::create(ERR_INVALID_PARAMETER);
    }
    auto *host = settings->get_host();
    if (!host || !host->accepting || !host->is_inside_tree() || host->is_queued_for_deletion()) {
        return SteamPublisherResult::create(ERR_UNCONFIGURED);
    }
    const auto state = std::make_shared<SteamPublisherState>();
    state->app_id = settings->get_app_id();
    state->environment = settings->get_environment();
    state->publisher_key_ref = settings->get_publisher_key_ref();
    state->encrypted_ticket_key_ref = settings->get_encrypted_ticket_key_ref();
    state->notification_store = settings->get_notification_store();
    state->moderation_policy = settings->get_moderation_policy();
    state->purchase_store = settings->get_purchase_store();
    state->ownership_challenge_store = settings->get_ownership_challenge_store();
    state->host = ObjectID(host->get_instance_id());
    state->host_generation = host->generation;
    Ref<SteamPublisher> publisher;
    publisher.instantiate();
    publisher->scope = state;
    publisher->auth_service.instantiate();
    publisher->auth_service->scope = state;
    publisher->leaderboard_service.instantiate();
    publisher->leaderboard_service->scope = state;
    publisher->notification_service.instantiate();
    publisher->notification_service->scope = state;
    publisher->moderation_service.instantiate();
    publisher->moderation_service->scope = state;
    publisher->purchase_service.instantiate();
    publisher->purchase_service->scope = state;
    publisher->inventory_service.instantiate();
    publisher->inventory_service->scope = state;
    return SteamPublisherResult::create(OK, publisher);
}

bool SteamPublisher::is_ready() const {
    return publisher_on_main_thread() && SteamPublisherHost::resolve(scope) != nullptr;
}

void SteamPublisher::close() {
    if (!publisher_on_main_thread()) {
        return;
    }
    if (scope) {
        scope->live = false;
        scope.reset();
    }
    auth_service.unref();
    leaderboard_service.unref();
    notification_service.unref();
    moderation_service.unref();
    purchase_service.unref();
    inventory_service.unref();
}

Ref<SteamPublisherAuth> SteamPublisher::auth() const {
    return is_ready() ? auth_service : Ref<SteamPublisherAuth>();
}

Ref<SteamPublisherLeaderboards> SteamPublisher::leaderboards() const {
    return is_ready() ? leaderboard_service : Ref<SteamPublisherLeaderboards>();
}

Ref<SteamGameNotifications> SteamPublisher::game_notifications() const {
    return is_ready() ? notification_service : Ref<SteamGameNotifications>();
}

Ref<SteamPublisherModeration> SteamPublisher::moderation() const {
    return is_ready() ? moderation_service : Ref<SteamPublisherModeration>();
}

Ref<SteamPublisherPurchases> SteamPublisher::purchases() const {
    return is_ready() ? purchase_service : Ref<SteamPublisherPurchases>();
}

Ref<SteamPublisherInventory> SteamPublisher::inventory() const {
    return is_ready() ? inventory_service : Ref<SteamPublisherInventory>();
}

void SteamPublisherResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamPublisherResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE,
                         SteamPublisher::get_class_static(), read_only), "", "get_value");
}

Ref<SteamPublisher> SteamPublisherResult::get_value() const {
    return value;
}

Ref<SteamPublisherResult> SteamPublisherResult::create(Error error, const Ref<SteamPublisher> &value) {
    Ref<SteamPublisherResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = value;
    return result;
}

void SteamPublisherCompletion::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            SteamResult::get_class_static())));
}

Variant SteamPublisherCompletion::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamPublisherCompletion::finish(const Ref<SteamResult> &result) {
    if (outcome.is_null()) {
        const Ref<SteamPublisherCompletion> keep_alive(this);
        outcome = result;
        emit_signal("completed", outcome);
    }
}

} // namespace crimson::steam
