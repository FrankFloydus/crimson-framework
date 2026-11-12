// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_inventory_types.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <utility>

namespace crimson::steam {
using namespace godot;

namespace {

// Show snapshot properties to scripts without storing instance state.
constexpr uint32_t read_only = PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY;

} // namespace

SteamItemDefinition::SteamItemDefinition() {
    properties.make_read_only();
}

void SteamItemDefinition::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_definition_id"), &SteamItemDefinition::get_definition_id);
    ClassDB::bind_method(D_METHOD("get_display_name"), &SteamItemDefinition::get_display_name);
    ClassDB::bind_method(D_METHOD("get_type"), &SteamItemDefinition::get_type);
    ClassDB::bind_method(D_METHOD("get_properties"), &SteamItemDefinition::get_properties);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "definition_id", PROPERTY_HINT_NONE, "", read_only),
            "", "get_definition_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name", PROPERTY_HINT_NONE, "", read_only),
            "", "get_display_name");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "type", PROPERTY_HINT_NONE, "", read_only),
            "", "get_type");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "properties", PROPERTY_HINT_DICTIONARY_TYPE, "String;String", read_only),
            "", "get_properties");
}

int64_t SteamItemDefinition::get_definition_id() const {
    return definition_id;
}

String SteamItemDefinition::get_display_name() const {
    return display_name;
}

StringName SteamItemDefinition::get_type() const {
    return type;
}

TypedDictionary<String, String> SteamItemDefinition::get_properties() const {
    return properties;
}

void SteamItemInstance::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_instance_id"), &SteamItemInstance::get_instance_id);
    ClassDB::bind_method(D_METHOD("get_definition_id"), &SteamItemInstance::get_definition_id);
    ClassDB::bind_method(D_METHOD("get_quantity"), &SteamItemInstance::get_quantity);
    ClassDB::bind_method(D_METHOD("get_flags"), &SteamItemInstance::get_flags);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "instance_id", PROPERTY_HINT_NONE, "", read_only),
            "", "get_instance_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "definition_id", PROPERTY_HINT_NONE, "", read_only),
            "", "get_definition_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "quantity", PROPERTY_HINT_NONE, "", read_only),
            "", "get_quantity");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "flags", PROPERTY_HINT_NONE, "", read_only),
            "", "get_flags");
}

String SteamItemInstance::get_instance_id() const {
    return instance_id;
}

int64_t SteamItemInstance::get_definition_id() const {
    return definition_id;
}

int64_t SteamItemInstance::get_quantity() const {
    return quantity;
}

int64_t SteamItemInstance::get_flags() const {
    return flags;
}

Ref<SteamItemQuantity> SteamItemQuantity::make(const String &instance_id, int64_t quantity) {
    Ref<SteamItemQuantity> result;
    result.instantiate();
    result->instance_id = instance_id;
    result->quantity = quantity;
    return result;
}

void SteamItemQuantity::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("make", "instance_id", "quantity"), &SteamItemQuantity::make);
    ClassDB::bind_method(D_METHOD("get_instance_id"), &SteamItemQuantity::get_instance_id);
    ClassDB::bind_method(D_METHOD("get_quantity"), &SteamItemQuantity::get_quantity);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "instance_id", PROPERTY_HINT_NONE, "", read_only),
            "", "get_instance_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "quantity", PROPERTY_HINT_NONE, "", read_only),
            "", "get_quantity");
}

String SteamItemQuantity::get_instance_id() const {
    return instance_id;
}

int64_t SteamItemQuantity::get_quantity() const {
    return quantity;
}

SteamInventorySnapshot::SteamInventorySnapshot() {
    items.make_read_only();
}

void SteamInventorySnapshot::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_items"), &SteamInventorySnapshot::get_items);
    ClassDB::bind_method(D_METHOD("get_freshness"), &SteamInventorySnapshot::get_freshness);
    ClassDB::bind_method(D_METHOD("get_observed_at_unix"), &SteamInventorySnapshot::get_observed_at_unix);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "items", PROPERTY_HINT_ARRAY_TYPE, SteamItemInstance::get_class_static(), read_only),
            "", "get_items");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "freshness", PROPERTY_HINT_ENUM,
                         "Unconfirmed,Fresh,Cached,Verified Proof", read_only),
            "", "get_freshness");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "observed_at_unix", PROPERTY_HINT_NONE, "", read_only),
            "", "get_observed_at_unix");

    BIND_ENUM_CONSTANT(FRESHNESS_UNCONFIRMED);
    BIND_ENUM_CONSTANT(FRESHNESS_FRESH);
    BIND_ENUM_CONSTANT(FRESHNESS_CACHED);
    BIND_ENUM_CONSTANT(FRESHNESS_VERIFIED_PROOF);
}

TypedArray<SteamItemInstance> SteamInventorySnapshot::get_items() const {
    return items;
}

SteamInventorySnapshot::Freshness SteamInventorySnapshot::get_freshness() const {
    return freshness;
}

int64_t SteamInventorySnapshot::get_observed_at_unix() const {
    return observed_at_unix;
}

SteamInventoryChange::SteamInventoryChange() {
    granted.make_read_only();
    changed.make_read_only();
}

void SteamInventoryChange::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_granted"), &SteamInventoryChange::get_granted);
    ClassDB::bind_method(D_METHOD("get_changed"), &SteamInventoryChange::get_changed);
    ClassDB::bind_method(D_METHOD("get_removed_instance_ids"), &SteamInventoryChange::get_removed_instance_ids);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "granted", PROPERTY_HINT_ARRAY_TYPE, SteamItemInstance::get_class_static(), read_only),
            "", "get_granted");
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "changed", PROPERTY_HINT_ARRAY_TYPE, SteamItemInstance::get_class_static(), read_only),
            "", "get_changed");
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "removed_instance_ids", PROPERTY_HINT_NONE, "", read_only),
            "", "get_removed_instance_ids");
}

TypedArray<SteamItemInstance> SteamInventoryChange::get_granted() const {
    return granted;
}

TypedArray<SteamItemInstance> SteamInventoryChange::get_changed() const {
    return changed;
}

PackedStringArray SteamInventoryChange::get_removed_instance_ids() const {
    return removed_instance_ids;
}

SteamItemDefinitionsResult::SteamItemDefinitionsResult() {
    value.make_read_only();
}

void SteamItemDefinitionsResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamItemDefinitionsResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "value", PROPERTY_HINT_ARRAY_TYPE, SteamItemDefinition::get_class_static(), read_only),
            "", "get_value");
}

TypedArray<SteamItemDefinition> SteamItemDefinitionsResult::get_value() const {
    return value;
}

Ref<SteamItemDefinitionsResult> SteamItemDefinitionsResult::create(Error error, const String &message, TypedArray<SteamItemDefinition> payload) {
    Ref<SteamItemDefinitionsResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    payload.make_read_only();
    result->value = std::move(payload);
    return result;
}

void SteamItemDefinitionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamItemDefinitionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, SteamItemDefinition::get_class_static(), read_only),
            "", "get_value");
}

Ref<SteamItemDefinition> SteamItemDefinitionResult::get_value() const {
    return value;
}

Ref<SteamItemDefinitionResult> SteamItemDefinitionResult::create(Error error, const String &message, Ref<SteamItemDefinition> payload) {
    Ref<SteamItemDefinitionResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = std::move(payload);
    return result;
}

void SteamInventorySnapshotResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamInventorySnapshotResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, SteamInventorySnapshot::get_class_static(), read_only),
            "", "get_value");
}

Ref<SteamInventorySnapshot> SteamInventorySnapshotResult::get_value() const {
    return value;
}

Ref<SteamInventorySnapshotResult> SteamInventorySnapshotResult::create(Error error, const String &message, Ref<SteamInventorySnapshot> payload) {
    Ref<SteamInventorySnapshotResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = std::move(payload);
    return result;
}

void SteamInventoryChangeResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamInventoryChangeResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, SteamInventoryChange::get_class_static(), read_only),
            "", "get_value");
}

Ref<SteamInventoryChange> SteamInventoryChangeResult::get_value() const {
    return value;
}

Ref<SteamInventoryChangeResult> SteamInventoryChangeResult::create(Error error, const String &message, Ref<SteamInventoryChange> payload) {
    Ref<SteamInventoryChangeResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = std::move(payload);
    return result;
}

void SteamItemDefinitionsOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamItemDefinitionsOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamItemDefinitionsOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamItemDefinitionsOperation::wait);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, SteamItemDefinitionsResult::get_class_static())));
}

bool SteamItemDefinitionsOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamItemDefinitionsResult> SteamItemDefinitionsOperation::result() const {
    return outcome;
}

Variant SteamItemDefinitionsOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamItemDefinitionsOperation::finish(const Ref<SteamItemDefinitionsResult> &finished_result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamItemDefinitionsOperation> keep_alive(this);
    outcome = finished_result;
    emit_signal("completed", outcome);
}

void SteamItemDefinitionOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamItemDefinitionOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamItemDefinitionOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamItemDefinitionOperation::wait);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, SteamItemDefinitionResult::get_class_static())));
}

bool SteamItemDefinitionOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamItemDefinitionResult> SteamItemDefinitionOperation::result() const {
    return outcome;
}

Variant SteamItemDefinitionOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamItemDefinitionOperation::finish(const Ref<SteamItemDefinitionResult> &finished_result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamItemDefinitionOperation> keep_alive(this);
    outcome = finished_result;
    emit_signal("completed", outcome);
}

void SteamInventorySnapshotOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamInventorySnapshotOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamInventorySnapshotOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamInventorySnapshotOperation::wait);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, SteamInventorySnapshotResult::get_class_static())));
}

bool SteamInventorySnapshotOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamInventorySnapshotResult> SteamInventorySnapshotOperation::result() const {
    return outcome;
}

Variant SteamInventorySnapshotOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamInventorySnapshotOperation::finish(const Ref<SteamInventorySnapshotResult> &finished_result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamInventorySnapshotOperation> keep_alive(this);
    outcome = finished_result;
    emit_signal("completed", outcome);
}

void SteamInventoryChangeOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamInventoryChangeOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamInventoryChangeOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamInventoryChangeOperation::wait);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result",
            PROPERTY_HINT_RESOURCE_TYPE, SteamInventoryChangeResult::get_class_static())));
}

bool SteamInventoryChangeOperation::is_finished() const {
    return outcome.is_valid();
}

Ref<SteamInventoryChangeResult> SteamInventoryChangeOperation::result() const {
    return outcome;
}

Variant SteamInventoryChangeOperation::wait() {
    return outcome.is_valid() ? Variant(outcome) : Variant(Signal(this, "completed"));
}

void SteamInventoryChangeOperation::finish(const Ref<SteamInventoryChangeResult> &finished_result) {
    if (outcome.is_valid()) {
        return;
    }
    const Ref<SteamInventoryChangeOperation> keep_alive(this);
    outcome = finished_result;
    emit_signal("completed", outcome);
}

} // namespace crimson::steam
