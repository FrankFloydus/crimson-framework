// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "gameplay_attributes.hpp"
#include "modules/abilities/gameplay_ability.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/script.hpp>

#include <functional>
#include <map>

namespace crimson::attributes {

// An actor's live attributes within one assigned set.
class GameplayAttributeSet : public godot::RefCounted {
    GDCLASS(GameplayAttributeSet, godot::RefCounted)
    friend class AbilitySystemComponent;

    godot::StringName id; // Stable set ID.
    godot::Ref<GameplayAttributes> view; // Component-scoped view with guarded access to its source.

    // Qualifies a local ID without allowing the caller to address another set.
    godot::StringName qualify(const godot::StringName &attribute_id) const;

protected:
    // Registers the set's ID and scoped attribute operations.
    static void _bind_methods();

public:
    // Returns this set's stable ID.
    godot::StringName get_id() const;
    // Reads a confirmed value by its local attribute ID.
    godot::Ref<GameplayAttributeValue> get_attribute(const godot::StringName &attribute_id) const;
    // Replaces a base value through the component's source.
    godot::Error set_base(const godot::StringName &attribute_id, double value);
    // Adds a finite delta to a base value through the component's source.
    godot::Error add_base(const godot::StringName &attribute_id, double delta);
    // Observes one local attribute, including the first confirmed match reading.
    godot::Ref<GameplayAttributeWatch> watch_attribute(const godot::StringName &attribute_id);
};

// Attaches attribute sets and their gameplay access to an actor scene.
class AbilitySystemComponent : public godot::Node {
    GDCLASS(AbilitySystemComponent, godot::Node)
    friend class abilities::GameplayAbilityActivation;

public:
    // Chooses how the actor obtains its live values.
    enum AttributeSource {
        LOCAL, // Own independent values for this scene instance.
        MATCH, // Use a character in the coordinated match store.
    };

private:
    // One granted definition and, while in progress, the associated execution.
    struct AbilityGrant {
        godot::Ref<godot::Script> definition; // Script used to create each new execution.
        std::shared_ptr<const abilities::AbilityDefaultTagSnapshot> default_tags; // Frozen defaults for every run on this grant.
        int64_t level = 0; // Game-authored level held until this grant is cleared.
        godot::ObjectID execution; // Ability node owned by this component for the current attempt.
        godot::Ref<abilities::GameplayAbilityActivation> activation; // Set during both pending and active attempts.
        bool started = false; // True once the activation hook can observe the run as active.
    };

    godot::PackedStringArray set_ids; // Configured project set IDs from the actor scene.
    AttributeSource source = LOCAL; // Determines whether this component ever creates a local store.
    godot::Ref<GameplayAttributes> store; // The local owner or explicitly attached match character.
    godot::Ref<GameplayAttributes> view; // Stable facade for set views and watches.
    bool initialized = false; // Prevents scene settings edits after runtime initialization.
    std::map<int64_t, AbilityGrant> ability_grants; // Grants keyed by their opaque handles.
    uint64_t last_ability_run = 0; // Run tokens are not reused within this component.
    std::function<bool()> ability_authority; // Native predicate for coordinated actors.
    bool ability_authority_scoped = false; // An expired scope may never fall back to local authority.
    bool ability_scene_active = false; // Exit disables mutations before running cleanup hooks.
    bool retiring_abilities = false; // Prevents new work while cancellation callbacks are being drained.

    // Checks the qualified ID belongs to one of this actor's assigned sets.
    bool has_qualified_set(const godot::StringName &attribute_id) const;
    // Reads through the live source while the actor is in the tree.
    godot::Ref<GameplayAttributeValue> read_source(const godot::StringName &attribute_id) const;
    // Applies an authority-side edit through the live source.
    godot::Error edit_source(const godot::StringName &attribute_id, double operand, bool add);
    // Publishes a confirmed change through the actor's filtered view.
    void on_source_changed(const godot::StringName &attribute_id, const godot::Ref<GameplayAttributeValue> &previous,
            const godot::Ref<GameplayAttributeValue> &current);
    // Installs the component's single source and wires confirmed notifications.
    godot::Error attach_source(const godot::Ref<GameplayAttributes> &attributes);
    // Checks scene lifecycle and authority for public ability mutations.
    godot::Error ability_mutation_status() const;
    // Looks up the exact in-progress attempt, even before its activation hook.
    AbilityGrant *find_ability_run(int64_t handle, uint64_t run);
    // Tests both the token and the lifetime of the running ability node.
    bool is_ability_run_active(int64_t handle, uint64_t run) const;
    // Completes the attempt authorized by a particular activation context.
    godot::Error end_ability_run(int64_t handle, uint64_t run);
    // Clears run state before user code and then retires the execution node.
    static void finish_ability_run(AbilityGrant &grant, bool canceled);

protected:
    // Registers scene configuration and attribute access.
    static void _bind_methods();
    // Initializes local values before the actor's Ready callback.
    void _notification(int what);

public:
    // Disconnects from the live source before this actor is freed.
    ~AbilitySystemComponent() override;
    // Assigns authored set IDs before the component starts.
    void set_attribute_sets(const godot::PackedStringArray &sets);
    // Returns the actor's configured set IDs.
    godot::PackedStringArray get_attribute_sets() const;
    // Selects the source before runtime initialization.
    void set_attribute_source(AttributeSource mode);
    // Returns the selected attribute source.
    AttributeSource get_attribute_source() const;
    // Resolves an assigned set on this actor, including an awaiting Match set.
    godot::Ref<GameplayAttributeSet> get_attribute_set(const godot::StringName &set_id) const;
    // Reads one exact qualified ID for generic access and generated helpers.
    godot::Ref<GameplayAttributeValue> get_attribute(const godot::StringName &attribute_id) const;
    // Replaces a base through the authority for one exact qualified ID.
    godot::Error set_base(const godot::StringName &attribute_id, double value);
    // Adds a delta through the authority for one exact qualified ID.
    godot::Error add_base(const godot::StringName &attribute_id, double delta);
    // Watches one exact qualified ID without copying ownership or state.
    godot::Ref<GameplayAttributeWatch> watch_attribute(const godot::StringName &attribute_id);
    // Supplies the coordinated source through the native match adapter once per component.
    godot::Error bind_match_attributes(const godot::Ref<GameplayAttributes> &attributes);
    // Updates the native authority scope and cancels work if the scope no longer authorizes it.
    void set_ability_authority(std::function<bool()> predicate);
    // Reports whether a coordinator has ever scoped this component's ability mutations.
    bool has_ability_authority_scope() const;
    // Cancels in-progress work without revoking grants or changing the authority scope.
    void retire_ability_activations();
    // Grants an instantiable GameplayAbility script at a positive level or returns zero on failure.
    int64_t give_ability(const godot::Ref<godot::Script> &definition, int64_t level = 1);
    // Opens one new attempt for an inactive grant and runs its script hooks.
    godot::Error try_activate_ability(int64_t handle);
    // Cancels an active run or aborts an attempt that has not yet started.
    godot::Error cancel_ability(int64_t handle);
    // Revokes a grant and cancels any in-progress run.
    godot::Error clear_ability(int64_t handle);
    // Reports whether this grant currently has a live, accepted run.
    bool is_ability_active(int64_t handle) const;
    // Returns the fixed granted level or zero for an unknown handle.
    int64_t get_ability_level(int64_t handle) const;
};

} // namespace crimson::attributes

VARIANT_ENUM_CAST(crimson::attributes::AbilitySystemComponent::AttributeSource);
