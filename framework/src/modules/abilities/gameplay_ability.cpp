// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/abilities/gameplay_ability.hpp"

#include "modules/attributes/ability_system_component.hpp"
#include "modules/gameplay_tags/bindings/gameplay_tag_resources.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::abilities {

using namespace godot;
using attributes::AbilitySystemComponent;

void GameplayAbility::_bind_methods() {}

void GameplayAbilityActivation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_component"), &GameplayAbilityActivation::get_component);
    ClassDB::bind_method(D_METHOD("get_actor"), &GameplayAbilityActivation::get_actor);
    ClassDB::bind_method(D_METHOD("get_level"), &GameplayAbilityActivation::get_level);
    ClassDB::bind_method(D_METHOD("get_default_tags", "field"), &GameplayAbilityActivation::get_default_tags);
    ClassDB::bind_method(D_METHOD("is_active"), &GameplayAbilityActivation::is_active);
    ClassDB::bind_method(D_METHOD("end"), &GameplayAbilityActivation::end);
    BIND_ENUM_CONSTANT(ABILITY_TAGS);
    BIND_ENUM_CONSTANT(ACTIVATION_REQUIRED_TAGS);
    BIND_ENUM_CONSTANT(ACTIVATION_BLOCKED_TAGS);
    BIND_ENUM_CONSTANT(ACTIVATION_OWNED_TAGS);
    BIND_ENUM_CONSTANT(BLOCK_ABILITIES_WITH_TAGS);
    BIND_ENUM_CONSTANT(CANCEL_ABILITIES_WITH_TAGS);
    BIND_ENUM_CONSTANT(SOURCE_REQUIRED_TAGS);
    BIND_ENUM_CONSTANT(SOURCE_BLOCKED_TAGS);
    BIND_ENUM_CONSTANT(TARGET_REQUIRED_TAGS);
    BIND_ENUM_CONSTANT(TARGET_BLOCKED_TAGS);
}

AbilitySystemComponent *GameplayAbilityActivation::get_component() const {
    auto *component = Object::cast_to<AbilitySystemComponent>(ObjectDB::get_instance(component_id));
    return component && component->is_inside_tree() ? component : nullptr;
}

Node *GameplayAbilityActivation::get_actor() const {
    auto *actor = Object::cast_to<Node>(ObjectDB::get_instance(actor_id));
    return actor && actor->is_inside_tree() ? actor : nullptr;
}

int64_t GameplayAbilityActivation::get_level() const { return level; }

Ref<gameplay_tags::bindings::GameplayTagContainer> GameplayAbilityActivation::get_default_tags(DefaultTagField field) const {
    static_assert(TARGET_BLOCKED_TAGS + 1 == ability_default_tag_keys.size());
    const auto index = static_cast<std::size_t>(field);
    if (index >= ability_default_tag_keys.size()) {
        return {};
    }
    return gameplay_tags::bindings::GameplayTagContainer::from_value(
            default_tags ? (*default_tags)[index] : gameplay_tags::GameplayTagContainer());
}

bool GameplayAbilityActivation::is_active() const {
    const auto *component = get_component();
    return component && component->is_ability_run_active(handle, run);
}

Error GameplayAbilityActivation::end() {
    auto *component = get_component();
    return component ? component->end_ability_run(handle, run) : ERR_UNAVAILABLE;
}

void register_gameplay_abilities(ModuleInitializationLevel level) {
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        initialize_ability_defaults_setting();
        GDREGISTER_CLASS(GameplayAbility);
        GDREGISTER_ABSTRACT_CLASS(GameplayAbilityActivation);
    }
}

} // namespace crimson::abilities
