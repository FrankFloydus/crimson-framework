// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "ability_system_component.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

namespace crimson::attributes {

using namespace godot;

void GameplayAttributeSet::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_id"), &GameplayAttributeSet::get_id);
    ClassDB::bind_method(D_METHOD("get_attribute", "attribute_id"), &GameplayAttributeSet::get_attribute);
    ClassDB::bind_method(D_METHOD("set_base", "attribute_id", "value"), &GameplayAttributeSet::set_base);
    ClassDB::bind_method(D_METHOD("add_base", "attribute_id", "delta"), &GameplayAttributeSet::add_base);
    ClassDB::bind_method(D_METHOD("watch_attribute", "attribute_id"), &GameplayAttributeSet::watch_attribute);
}

StringName GameplayAttributeSet::get_id() const { return id; }

StringName GameplayAttributeSet::qualify(const StringName &attribute_id) const {
    const String local(attribute_id);
    return local.is_empty() || local.contains(".") ? StringName() : StringName(String(id) + "." + local);
}

Ref<GameplayAttributeValue> GameplayAttributeSet::get_attribute(const StringName &attribute_id) const {
    const StringName qualified = qualify(attribute_id);
    return qualified.is_empty() ? Ref<GameplayAttributeValue>() : view->get_attribute(qualified);
}

Error GameplayAttributeSet::set_base(const StringName &attribute_id, double value) {
    const StringName qualified = qualify(attribute_id);
    return qualified.is_empty() ? ERR_INVALID_PARAMETER : view->set_base(qualified, value);
}

Error GameplayAttributeSet::add_base(const StringName &attribute_id, double delta) {
    const StringName qualified = qualify(attribute_id);
    return qualified.is_empty() ? ERR_INVALID_PARAMETER : view->add_base(qualified, delta);
}

Ref<GameplayAttributeWatch> GameplayAttributeSet::watch_attribute(const StringName &attribute_id) {
    const StringName qualified = qualify(attribute_id);
    return qualified.is_empty() ? Ref<GameplayAttributeWatch>() : view->watch_attribute(qualified);
}

void AbilitySystemComponent::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_attribute_sets", "sets"), &AbilitySystemComponent::set_attribute_sets);
    ClassDB::bind_method(D_METHOD("get_attribute_sets"), &AbilitySystemComponent::get_attribute_sets);
    ClassDB::bind_method(D_METHOD("set_attribute_source", "mode"), &AbilitySystemComponent::set_attribute_source);
    ClassDB::bind_method(D_METHOD("get_attribute_source"), &AbilitySystemComponent::get_attribute_source);
    ClassDB::bind_method(D_METHOD("get_attribute_set", "set_id"), &AbilitySystemComponent::get_attribute_set);
    ClassDB::bind_method(D_METHOD("get_attribute", "attribute_id"), &AbilitySystemComponent::get_attribute);
    ClassDB::bind_method(D_METHOD("set_base", "attribute_id", "value"), &AbilitySystemComponent::set_base);
    ClassDB::bind_method(D_METHOD("add_base", "attribute_id", "delta"), &AbilitySystemComponent::add_base);
    ClassDB::bind_method(D_METHOD("watch_attribute", "attribute_id"), &AbilitySystemComponent::watch_attribute);
    ClassDB::bind_method(D_METHOD("_on_source_changed", "id", "previous", "current"), &AbilitySystemComponent::on_source_changed);
    ClassDB::bind_method(D_METHOD("give_ability", "definition", "level"), &AbilitySystemComponent::give_ability, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("try_activate_ability", "handle"), &AbilitySystemComponent::try_activate_ability);
    ClassDB::bind_method(D_METHOD("cancel_ability", "handle"), &AbilitySystemComponent::cancel_ability);
    ClassDB::bind_method(D_METHOD("clear_ability", "handle"), &AbilitySystemComponent::clear_ability);
    ClassDB::bind_method(D_METHOD("is_ability_active", "handle"), &AbilitySystemComponent::is_ability_active);
    ClassDB::bind_method(D_METHOD("get_ability_level", "handle"), &AbilitySystemComponent::get_ability_level);
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "attribute_sets"), "set_attribute_sets", "get_attribute_sets");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "attribute_source", PROPERTY_HINT_ENUM, "Local,Match"),
            "set_attribute_source", "get_attribute_source");
    BIND_ENUM_CONSTANT(LOCAL);
    BIND_ENUM_CONSTANT(MATCH);
}

void AbilitySystemComponent::_notification(int what) {
    if (what == NOTIFICATION_ENTER_TREE) {
        ability_scene_active = !Engine::get_singleton()->is_editor_hint();
    } else if (what == NOTIFICATION_EXIT_TREE) {
        ability_scene_active = false;
        retire_ability_activations();
    }
    if (what != NOTIFICATION_READY || initialized || Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    initialized = true;
    view = GameplayAttributes::create_bridge(ObjectID(get_instance_id()), callable_mp(this, &AbilitySystemComponent::read_source),
            callable_mp(this, &AbilitySystemComponent::edit_source));
    if (source == LOCAL) {
        const Ref<GameplayAttributes> local = GameplayAttributes::from_sets(set_ids);
        if (local.is_valid()) {
            attach_source(local);
        }
    }
}

AbilitySystemComponent::~AbilitySystemComponent() {
    const Callable source_subscription(this, "_on_source_changed");
    if (store.is_valid() && store->is_connected("attribute_changed", source_subscription)) {
        store->disconnect("attribute_changed", source_subscription);
    }
}

void AbilitySystemComponent::set_attribute_sets(const PackedStringArray &sets) {
    if (!initialized && store.is_null()) set_ids = sets;
}

PackedStringArray AbilitySystemComponent::get_attribute_sets() const { return set_ids; }

void AbilitySystemComponent::set_attribute_source(AttributeSource mode) {
    if (!initialized && store.is_null() && (mode == LOCAL || mode == MATCH)) source = mode;
}

AbilitySystemComponent::AttributeSource AbilitySystemComponent::get_attribute_source() const { return source; }

Ref<GameplayAttributeSet> AbilitySystemComponent::get_attribute_set(const StringName &set_id) const {
    if (!is_inside_tree() || view.is_null() || !set_ids.has(String(set_id)) || (source == LOCAL && store.is_null())) {
        return {};
    }
    Ref<GameplayAttributeSet> result;
    result.instantiate();
    result->id = set_id;
    result->view = view;
    return result;
}

Ref<GameplayAttributeValue> AbilitySystemComponent::get_attribute(const StringName &attribute_id) const {
    return view.is_valid() ? view->get_attribute(attribute_id) : Ref<GameplayAttributeValue>();
}

Error AbilitySystemComponent::set_base(const StringName &attribute_id, double value) {
    return view.is_valid() ? view->set_base(attribute_id, value) : ERR_UNAVAILABLE;
}

Error AbilitySystemComponent::add_base(const StringName &attribute_id, double delta) {
    return view.is_valid() ? view->add_base(attribute_id, delta) : ERR_UNAVAILABLE;
}

Ref<GameplayAttributeWatch> AbilitySystemComponent::watch_attribute(const StringName &attribute_id) {
    return view.is_valid() && is_inside_tree() && has_qualified_set(attribute_id) ? view->watch_attribute(attribute_id) :
            Ref<GameplayAttributeWatch>();
}

bool AbilitySystemComponent::has_qualified_set(const StringName &attribute_id) const {
    const String qualified(attribute_id);
    const int64_t separator = qualified.find(".");
    return separator > 0 && separator + 1 < qualified.length() && set_ids.has(qualified.substr(0, separator));
}

Ref<GameplayAttributeValue> AbilitySystemComponent::read_source(const StringName &attribute_id) const {
    return is_inside_tree() && store.is_valid() && has_qualified_set(attribute_id) ? store->get_attribute(attribute_id) :
            Ref<GameplayAttributeValue>();
}

Error AbilitySystemComponent::edit_source(const StringName &attribute_id, double operand, bool add) {
    if (!is_inside_tree() || store.is_null()) return ERR_UNAVAILABLE;
    if (!has_qualified_set(attribute_id)) return ERR_DOES_NOT_EXIST;
    return add ? store->add_base(attribute_id, operand) : store->set_base(attribute_id, operand);
}

void AbilitySystemComponent::on_source_changed(const StringName &attribute_id,
        const Ref<GameplayAttributeValue> &previous, const Ref<GameplayAttributeValue> &current) {
    if (view.is_valid() && is_inside_tree() && has_qualified_set(attribute_id)) {
        view->notify_changed(attribute_id, previous, current);
    }
}

Error AbilitySystemComponent::attach_source(const Ref<GameplayAttributes> &attributes) {
    if (attributes.is_null()) return ERR_INVALID_PARAMETER;
    if (store.is_valid()) return store == attributes ? OK : ERR_ALREADY_IN_USE;
    const Callable source_subscription(this, "_on_source_changed");
    const Error connected = attributes->connect("attribute_changed", source_subscription);
    if (connected == OK) store = attributes;
    return connected;
}

Error AbilitySystemComponent::bind_match_attributes(const Ref<GameplayAttributes> &attributes) {
    return source == MATCH ? attach_source(attributes) : ERR_INVALID_PARAMETER;
}

} // namespace crimson::attributes
