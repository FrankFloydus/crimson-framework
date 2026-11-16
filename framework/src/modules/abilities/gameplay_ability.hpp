// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/abilities/ability_defaults.hpp"
#include "modules/gameplay_tags/gameplay_tag_container.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <memory>

namespace crimson::attributes {
class AbilitySystemComponent;
}

namespace crimson::gameplay_tags::bindings {
class GameplayTagContainer;
}

namespace crimson::abilities {

// Immutable class defaults shared by a grant and its activation tokens.
using AbilityDefaultTagSnapshot = std::array<gameplay_tags::GameplayTagContainer, ability_default_tag_keys.size()>;

// Native base for script-defined abilities. The component owns one instance per run.
class GameplayAbility : public godot::Node {
    GDCLASS(GameplayAbility, godot::Node)

protected:
    // The lifecycle hooks are implemented by the attached script.
    static void _bind_methods();
};

// The actor and grant information for one ability run.
class GameplayAbilityActivation : public godot::RefCounted {
    GDCLASS(GameplayAbilityActivation, godot::RefCounted)
    friend class attributes::AbilitySystemComponent;

public:
    // Identifies a tag container in the granted ability's defaults.
    enum DefaultTagField {
        ABILITY_TAGS, // Classifies the ability.
        ACTIVATION_REQUIRED_TAGS, // Configured tags required on the actor.
        ACTIVATION_BLOCKED_TAGS, // Configured actor tags that block activation.
        ACTIVATION_OWNED_TAGS, // Configured tags associated with an active run.
        BLOCK_ABILITIES_WITH_TAGS, // Classifies abilities to block while active.
        CANCEL_ABILITIES_WITH_TAGS, // Classifies active abilities to cancel.
        SOURCE_REQUIRED_TAGS, // Configured requirements for an activation source.
        SOURCE_BLOCKED_TAGS, // Configured disqualifying tags on the source.
        TARGET_REQUIRED_TAGS, // Configured requirements for an activation target.
        TARGET_BLOCKED_TAGS, // Configured disqualifying tags on the target.
    };

private:
    godot::ObjectID component_id; // Non-owning reference to the component that started the run.
    godot::ObjectID actor_id; // Non-owning reference to the component's actor at activation.
    int64_t level = 0; // Level of the grant that produced this run.
    int64_t handle = 0; // Grant handle scoped to component_id.
    uint64_t run = 0; // Distinguishes this run from any subsequent activation of the grant.
    std::shared_ptr<const AbilityDefaultTagSnapshot> default_tags; // Snapshot captured when the ability was granted.

protected:
    // Exposes actor access and completion to both script languages.
    static void _bind_methods();

public:
    // Resolves the live component, if it is still in the scene tree.
    attributes::AbilitySystemComponent *get_component() const;
    // Resolves the actor captured when this run was attempted.
    godot::Node *get_actor() const;
    // Returns this activation's fixed granted level.
    int64_t get_level() const;
    // Returns a detached tag set from the grant, or null for an unrecognized field.
    godot::Ref<gameplay_tags::bindings::GameplayTagContainer> get_default_tags(DefaultTagField field) const;
    // Reports whether this exact run was accepted and remains active.
    bool is_active() const;
    // Ends this exact run if it is active and the component still has authority.
    godot::Error end();
};

// Registers the ability script base and its returned activation context at scene initialization.
void register_gameplay_abilities(godot::ModuleInitializationLevel level);

} // namespace crimson::abilities

VARIANT_ENUM_CAST(crimson::abilities::GameplayAbilityActivation::DefaultTagField);
