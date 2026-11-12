// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/workshop/steam_workshop_mutation_types.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::steam {

using namespace godot;

void SteamWorkshopSubscription::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_published_id"), &SteamWorkshopSubscription::get_published_id);
    ClassDB::bind_method(D_METHOD("is_subscribed"), &SteamWorkshopSubscription::is_subscribed);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "published_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_published_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "subscribed", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_subscribed");
}

String SteamWorkshopSubscription::get_published_id() const {
    return published_id;
}

bool SteamWorkshopSubscription::is_subscribed() const {
    return subscribed;
}

Ref<SteamWorkshopSubscription> SteamWorkshopSubscription::create(const String &published_id, bool subscribed) {
    Ref<SteamWorkshopSubscription> snapshot;
    snapshot.instantiate();
    snapshot->published_id = published_id;
    snapshot->subscribed = subscribed;
    return snapshot;
}

void SteamWorkshopSubscriptionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopSubscriptionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopSubscription",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopSubscription> SteamWorkshopSubscriptionResult::get_value() const {
    return value;
}

Ref<SteamWorkshopSubscriptionResult> SteamWorkshopSubscriptionResult::create(Error error, const String &message,
        const Ref<SteamWorkshopSubscription> &snapshot) {
    Ref<SteamWorkshopSubscriptionResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = snapshot;
    return result;
}

void SteamWorkshopInstall::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_published_id"), &SteamWorkshopInstall::get_published_id);
    ClassDB::bind_method(D_METHOD("get_directory"), &SteamWorkshopInstall::get_directory);
    ClassDB::bind_method(D_METHOD("get_size_bytes"), &SteamWorkshopInstall::get_size_bytes);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "published_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_published_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "directory", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_directory");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "size_bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_size_bytes");
}

String SteamWorkshopInstall::get_published_id() const {
    return published_id;
}

String SteamWorkshopInstall::get_directory() const {
    return directory;
}

int64_t SteamWorkshopInstall::get_size_bytes() const {
    return size_bytes;
}

Ref<SteamWorkshopInstall> SteamWorkshopInstall::create(const String &published_id, const String &directory, int64_t size_bytes) {
    Ref<SteamWorkshopInstall> snapshot;
    snapshot.instantiate();
    snapshot->published_id = published_id;
    snapshot->directory = directory;
    snapshot->size_bytes = size_bytes;
    return snapshot;
}

void SteamWorkshopInstallResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopInstallResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopInstall",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopInstall> SteamWorkshopInstallResult::get_value() const {
    return value;
}

Ref<SteamWorkshopInstallResult> SteamWorkshopInstallResult::create(Error error, const String &message,
        const Ref<SteamWorkshopInstall> &snapshot) {
    Ref<SteamWorkshopInstallResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = snapshot;
    return result;
}

void SteamWorkshopVote::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_published_id"), &SteamWorkshopVote::get_published_id);
    ClassDB::bind_method(D_METHOD("is_up"), &SteamWorkshopVote::is_up);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "published_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_published_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "up", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_up");
}

String SteamWorkshopVote::get_published_id() const {
    return published_id;
}

bool SteamWorkshopVote::is_up() const {
    return up;
}

Ref<SteamWorkshopVote> SteamWorkshopVote::create(const String &published_id, bool up) {
    Ref<SteamWorkshopVote> snapshot;
    snapshot.instantiate();
    snapshot->published_id = published_id;
    snapshot->up = up;
    return snapshot;
}

void SteamWorkshopVoteResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopVoteResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopVote",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopVote> SteamWorkshopVoteResult::get_value() const {
    return value;
}

Ref<SteamWorkshopVoteResult> SteamWorkshopVoteResult::create(Error error, const String &message,
        const Ref<SteamWorkshopVote> &snapshot) {
    Ref<SteamWorkshopVoteResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = snapshot;
    return result;
}

void SteamWorkshopPublication::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_published_id"), &SteamWorkshopPublication::get_published_id);
    ClassDB::bind_method(D_METHOD("get_needs_legal_agreement"), &SteamWorkshopPublication::get_needs_legal_agreement);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "published_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_published_id");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "needs_legal_agreement", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_needs_legal_agreement");
}

String SteamWorkshopPublication::get_published_id() const {
    return published_id;
}

bool SteamWorkshopPublication::get_needs_legal_agreement() const {
    return needs_legal_agreement;
}

Ref<SteamWorkshopPublication> SteamWorkshopPublication::create(const String &published_id, bool needs_legal_agreement) {
    Ref<SteamWorkshopPublication> snapshot;
    snapshot.instantiate();
    snapshot->published_id = published_id;
    snapshot->needs_legal_agreement = needs_legal_agreement;
    return snapshot;
}

void SteamWorkshopPublicationResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopPublicationResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopPublication",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopPublication> SteamWorkshopPublicationResult::get_value() const {
    return value;
}

Ref<SteamWorkshopPublicationResult> SteamWorkshopPublicationResult::create(Error error, const String &message,
        const Ref<SteamWorkshopPublication> &snapshot) {
    Ref<SteamWorkshopPublicationResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = snapshot;
    return result;
}

void SteamWorkshopSubscriptions::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_published_ids"), &SteamWorkshopSubscriptions::get_published_ids);
    ClassDB::bind_method(D_METHOD("get_total_count"), &SteamWorkshopSubscriptions::get_total_count);
    ClassDB::bind_method(D_METHOD("is_truncated"), &SteamWorkshopSubscriptions::is_truncated);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "published_ids", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_published_ids");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "total_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_total_count");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "truncated", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_truncated");
}

PackedStringArray SteamWorkshopSubscriptions::get_published_ids() const {
    return published_ids;
}

int64_t SteamWorkshopSubscriptions::get_total_count() const {
    return total_count;
}

bool SteamWorkshopSubscriptions::is_truncated() const {
    return truncated;
}

Ref<SteamWorkshopSubscriptions> SteamWorkshopSubscriptions::create(const PackedStringArray &published_ids, int64_t total_count, bool truncated) {
    Ref<SteamWorkshopSubscriptions> snapshot;
    snapshot.instantiate();
    snapshot->published_ids = published_ids;
    snapshot->total_count = total_count;
    snapshot->truncated = truncated;
    return snapshot;
}

void SteamWorkshopSubscriptionsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopSubscriptionsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopSubscriptions",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopSubscriptions> SteamWorkshopSubscriptionsResult::get_value() const {
    return value;
}

Ref<SteamWorkshopSubscriptionsResult> SteamWorkshopSubscriptionsResult::create(Error error, const Ref<SteamWorkshopSubscriptions> &snapshot) {
    Ref<SteamWorkshopSubscriptionsResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = snapshot;
    return result;
}

void SteamWorkshopLegalAgreementStatus::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamWorkshopLegalAgreementStatus::get_app_id);
    ClassDB::bind_method(D_METHOD("get_version"), &SteamWorkshopLegalAgreementStatus::get_version);
    ClassDB::bind_method(D_METHOD("get_accepted_at_unix"), &SteamWorkshopLegalAgreementStatus::get_accepted_at_unix);
    ClassDB::bind_method(D_METHOD("is_accepted"), &SteamWorkshopLegalAgreementStatus::is_accepted);
    ClassDB::bind_method(D_METHOD("get_needs_action"), &SteamWorkshopLegalAgreementStatus::get_needs_action);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_version");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "accepted_at_unix", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_accepted_at_unix");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "accepted", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "is_accepted");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "needs_action", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_needs_action");
}

int64_t SteamWorkshopLegalAgreementStatus::get_app_id() const {
    return app_id;
}

int64_t SteamWorkshopLegalAgreementStatus::get_version() const {
    return version;
}

int64_t SteamWorkshopLegalAgreementStatus::get_accepted_at_unix() const {
    return accepted_at_unix;
}

bool SteamWorkshopLegalAgreementStatus::is_accepted() const {
    return accepted;
}

bool SteamWorkshopLegalAgreementStatus::get_needs_action() const {
    return needs_action;
}

Ref<SteamWorkshopLegalAgreementStatus> SteamWorkshopLegalAgreementStatus::create(int64_t app_id, int64_t version, int64_t accepted_at_unix, bool accepted, bool needs_action) {
    Ref<SteamWorkshopLegalAgreementStatus> snapshot;
    snapshot.instantiate();
    snapshot->app_id = app_id;
    snapshot->version = version;
    snapshot->accepted_at_unix = accepted_at_unix;
    snapshot->accepted = accepted;
    snapshot->needs_action = needs_action;
    return snapshot;
}

void SteamWorkshopLegalAgreementStatusResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamWorkshopLegalAgreementStatusResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamWorkshopLegalAgreementStatus",
                         PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamWorkshopLegalAgreementStatus> SteamWorkshopLegalAgreementStatusResult::get_value() const {
    return value;
}

Ref<SteamWorkshopLegalAgreementStatusResult> SteamWorkshopLegalAgreementStatusResult::create(Error error, const Ref<SteamWorkshopLegalAgreementStatus> &snapshot) {
    Ref<SteamWorkshopLegalAgreementStatusResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = snapshot;
    return result;
}

} // namespace crimson::steam
