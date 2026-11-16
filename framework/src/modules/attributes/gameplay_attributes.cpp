// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "gameplay_attributes.hpp"
#include "ability_system_component.hpp"
#include "attribute_registry.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

namespace crimson::attributes {
namespace {

using namespace godot;

// Declares an immutable reading Object in a typed signal signature.
PropertyInfo reading_property(const char *name) {
    return PropertyInfo(Variant::OBJECT, name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "GameplayAttributeValue");
}

// Compares effective readings, including loss or gain of authority visibility.
bool same_reading(const Ref<GameplayAttributeValue> &left, const Ref<GameplayAttributeValue> &right) {
    if (left.is_null() || right.is_null()) return left.is_null() && right.is_null();
    return left->get_current() == right->get_current() && left->has_base() == right->has_base() &&
            (!left->has_base() || left->get_base() == right->get_base());
}

} // namespace

using namespace godot;

void GameplayAttributeValue::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_current"), &GameplayAttributeValue::get_current);
    ClassDB::bind_method(D_METHOD("has_base"), &GameplayAttributeValue::has_base);
    ClassDB::bind_method(D_METHOD("get_base"), &GameplayAttributeValue::get_base);
}

Ref<GameplayAttributeValue> GameplayAttributeValue::make(double current, std::optional<double> base) {
    Ref<GameplayAttributeValue> reading;
    reading.instantiate();
    reading->current = current;
    reading->base = base;
    return reading;
}

double GameplayAttributeValue::get_current() const { return current; }
bool GameplayAttributeValue::has_base() const { return base.has_value(); }
double GameplayAttributeValue::get_base() const { return base.value_or(0); }

void GameplayAttributes::_bind_methods() {
    ClassDB::bind_static_method("GameplayAttributes", D_METHOD("from_sets", "set_ids"), &GameplayAttributes::from_sets);
    ClassDB::bind_method(D_METHOD("get_attribute", "id"), &GameplayAttributes::get_attribute);
    ClassDB::bind_method(D_METHOD("set_base", "id", "value"), &GameplayAttributes::set_base);
    ClassDB::bind_method(D_METHOD("add_base", "id", "delta"), &GameplayAttributes::add_base);
    ClassDB::bind_method(D_METHOD("watch_attribute", "id"), &GameplayAttributes::watch_attribute);
    ADD_SIGNAL(MethodInfo("attribute_changed", PropertyInfo(Variant::STRING_NAME, "id"),
            reading_property("previous"), reading_property("current")));
}

Ref<GameplayAttributes> GameplayAttributes::from_sets(const PackedStringArray &set_ids) {
    AttributeRegistry schema;
    Dictionary initial;
    String problem;
    if (!load_project_attribute_registry(schema, problem) || !make_attribute_values(schema, set_ids, initial, problem)) {
        UtilityFunctions::push_error(String("[Crimson] Could not create Gameplay Attributes. ") + problem);
        return {};
    }
    Ref<GameplayAttributes> result;
    result.instantiate();
    result->values = initial;
    return result;
}

Ref<GameplayAttributes> GameplayAttributes::create_bridge(ObjectID publisher, const Callable &read, const Callable &edit) {
    Ref<GameplayAttributes> result;
    result.instantiate();
    result->bridged = true;
    result->publisher_id = publisher;
    result->read_bridge = read;
    result->edit_bridge = edit;
    return result;
}

Ref<GameplayAttributeValue> GameplayAttributes::get_attribute(const StringName &id) const {
    if (bridged) {
        return ObjectDB::get_instance(publisher_id) && read_bridge.is_valid() ?
                Ref<GameplayAttributeValue>(read_bridge.call(id)) : Ref<GameplayAttributeValue>();
    }
    const Variant value = values.get(String(id), Variant());
    return value.get_type() == Variant::NIL ? Ref<GameplayAttributeValue>() :
            GameplayAttributeValue::make(double(value), double(value));
}

Error GameplayAttributes::edit_base(const StringName &id, double operand, bool add) {
    if (notifying) return ERR_BUSY;
    if (!std::isfinite(operand)) return ERR_INVALID_PARAMETER;
    if (bridged) {
        if (!ObjectDB::get_instance(publisher_id) || !edit_bridge.is_valid()) return ERR_UNAUTHORIZED;
        const Ref<GameplayAttributes> retained(this);
        const Variant status = edit_bridge.call(id, operand, add);
        return status.get_type() == Variant::INT ? static_cast<Error>(int64_t(status)) : FAILED;
    }
    const String key(id);
    if (!values.has(key)) return ERR_DOES_NOT_EXIST;
    const double previous = values[key];
    double next = 0;
    const Error status = evaluate_attribute_base(previous, operand, add, next);
    if (status != OK || next == previous) return status;
    values[key] = next;
    notify_changed(id, GameplayAttributeValue::make(previous, previous), GameplayAttributeValue::make(next, next));
    return OK;
}

Error GameplayAttributes::set_base(const StringName &id, double value) { return edit_base(id, value, false); }
Error GameplayAttributes::add_base(const StringName &id, double delta) { return edit_base(id, delta, true); }

Ref<GameplayAttributeWatch> GameplayAttributes::watch_attribute(const StringName &id) {
    if (id.is_empty()) return {};
    Ref<GameplayAttributeWatch> watch;
    watch.instantiate();
    watch->bind_source(Ref<GameplayAttributes>(this), id);
    return watch;
}

void GameplayAttributes::notify_changed(const StringName &id, const Ref<GameplayAttributeValue> &previous,
        const Ref<GameplayAttributeValue> &current) {
    if (same_reading(previous, current)) return;
    const Ref<GameplayAttributes> retained(this);
    const bool already_notifying = notifying;
    notifying = true;
    emit_signal("attribute_changed", id, previous, current);
    notifying = already_notifying;
}

void GameplayAttributeWatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &GameplayAttributeWatch::get_value);
    ClassDB::bind_method(D_METHOD("_on_attribute_changed", "id", "previous", "current"), &GameplayAttributeWatch::on_attribute);
    ADD_SIGNAL(MethodInfo("changed", reading_property("previous"), reading_property("current")));
}

void GameplayAttributeWatch::bind_source(const Ref<GameplayAttributes> &attributes, const StringName &id) {
    source = attributes;
    attribute_id = id;
    source->connect("attribute_changed", Callable(this, "_on_attribute_changed"));
}

GameplayAttributeWatch::~GameplayAttributeWatch() {
    const Callable subscription(this, "_on_attribute_changed");
    if (source.is_valid() && source->is_connected("attribute_changed", subscription)) {
        source->disconnect("attribute_changed", subscription);
    }
}

void GameplayAttributeWatch::on_attribute(const StringName &id, const Ref<GameplayAttributeValue> &previous,
        const Ref<GameplayAttributeValue> &current) {
    if (id == attribute_id) {
        // Keep the watch alive even if a listener drops its last reference during the signal.
        const Ref<GameplayAttributeWatch> retained(this);
        emit_signal("changed", previous, current);
    }
}

Ref<GameplayAttributeValue> GameplayAttributeWatch::get_value() const {
    return source.is_valid() ? source->get_attribute(attribute_id) : Ref<GameplayAttributeValue>();
}

void register_gameplay_attributes(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    initialize_attribute_registry_setting();
    GDREGISTER_ABSTRACT_CLASS(GameplayAttributeValue);
    GDREGISTER_ABSTRACT_CLASS(GameplayAttributeWatch);
    GDREGISTER_ABSTRACT_CLASS(GameplayAttributes);
    GDREGISTER_ABSTRACT_CLASS(GameplayAttributeSet);
    GDREGISTER_CLASS(AbilitySystemComponent);
}

} // namespace crimson::attributes
