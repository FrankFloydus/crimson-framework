// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/attributes/ability_system_component.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <atomic>
#include <limits>
#include <utility>

namespace crimson::attributes {
namespace {

using namespace godot;
using abilities::GameplayAbility;
using abilities::GameplayAbilityActivation;

// Allocate handles that cannot accidentally address a different actor's grant.
int64_t allocate_ability_handle() {
    static std::atomic<int64_t> last_handle{0};
    auto previous = last_handle.load(std::memory_order_relaxed);
    while (previous < std::numeric_limits<int64_t>::max()) {
        if (last_handle.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) {
            return previous + 1;
        }
    }
    return 0;
}

// Resolve only still-live executions after a call that might have run game code.
GameplayAbility *ability_execution(ObjectID id) {
    return Object::cast_to<GameplayAbility>(ObjectDB::get_instance(id));
}

// Queue owned nodes for deletion without freeing an instance during one of its callbacks.
void discard_ability_execution(ObjectID id) {
    if (auto *instance = ability_execution(id); instance && !instance->is_queued_for_deletion()) {
        instance->queue_free();
    }
}

// Check the common script resource contract before any grant is stored.
bool valid_definition(const Ref<Script> &definition) {
    return definition.is_valid() && definition->can_instantiate() &&
            definition->get_instance_base_type() == GameplayAbility::get_class_static() &&
            definition->has_script_method("on_activated");
}

// Invoke a script method and preserve call errors such as an invalid hook signature.
bool call_ability_hook(GameplayAbility *instance, const StringName &method, const Ref<GameplayAbilityActivation> &activation,
        Variant &result, const Variant *ended = nullptr) {
    Variant target(instance);
    const Variant context(activation);
    const Variant *arguments[] = { &context, ended };
    GDExtensionCallError error{};
    target.callp(method, arguments, ended ? 2 : 1, result, error);
    return error.error == GDEXTENSION_CALL_OK;
}

// Report malformed cleanup hooks while allowing the already-settled run to retire.
void call_ability_cleanup(GameplayAbility *instance, const StringName &method, const Ref<GameplayAbilityActivation> &activation,
        const Variant *canceled = nullptr) {
    const Ref<Script> definition = instance->get_script();
    Variant result;
    if (!call_ability_hook(instance, method, activation, result, canceled)) {
        const String path = definition.is_valid() ? definition->get_path() : String();
        const String location = path.is_empty() ? String() : String(" in ") + path;
        UtilityFunctions::push_error(String("[Crimson] Could not run Gameplay Ability hook ") + String(method) + location +
                String(". Check the hook's parameters."));
    }
}

} // namespace

using namespace godot;
using abilities::GameplayAbilityActivation;

Error AbilitySystemComponent::ability_mutation_status() const {
    const auto *actor = get_parent();
    if (!ability_scene_active || !is_inside_tree() || is_queued_for_deletion() ||
            (actor && actor->is_queued_for_deletion()) || retiring_abilities) {
        return ERR_UNAVAILABLE;
    }
    if (ability_authority_scoped || source == MATCH) {
        return ability_authority && ability_authority() ? OK : ERR_UNAUTHORIZED;
    }
    return OK;
}

void AbilitySystemComponent::set_ability_authority(std::function<bool()> predicate) {
    ability_authority_scoped = true;
    ability_authority = std::move(predicate);
    if (!ability_authority || !ability_authority()) {
        retire_ability_activations();
    }
}

bool AbilitySystemComponent::has_ability_authority_scope() const { return ability_authority_scoped; }

void AbilitySystemComponent::retire_ability_activations() {
    if (retiring_abilities) {
        return;
    }
    const ObjectID component_id(get_instance_id());
    retiring_abilities = true;
    for (auto &entry : ability_grants) {
        // Public grant mutations are blocked until all cleanup hooks return.
        finish_ability_run(entry.second, true);
        if (!ObjectDB::get_instance(component_id)) {
            return;
        }
    }
    retiring_abilities = false;
}

int64_t AbilitySystemComponent::give_ability(const Ref<Script> &definition, int64_t level) {
    if (ability_mutation_status() != OK || level <= 0 || !valid_definition(definition)) {
        return 0;
    }
    Dictionary settings;
    String error;
    if (!abilities::load_ability_defaults(definition, settings, error)) {
        UtilityFunctions::push_error("[Crimson] Could not grant ability " + definition->get_path() + ". " + error);
        return 0;
    }
    std::shared_ptr<abilities::AbilityDefaultTagSnapshot> defaults;
    if (!settings.is_empty()) {
        defaults = std::make_shared<abilities::AbilityDefaultTagSnapshot>();
        for (std::size_t index = 0; index < abilities::ability_default_tag_keys.size(); ++index) {
            const PackedStringArray names = settings.get(abilities::ability_default_tag_keys[index], PackedStringArray());
            for (const String &name : names) {
                const CharString encoded = name.utf8();
                (*defaults)[index].add(gameplay_tags::GameplayTag::from_name(
                        std::string_view(encoded.get_data(), static_cast<std::size_t>(encoded.length()))));
            }
        }
    }
    const int64_t handle = allocate_ability_handle();
    if (handle == 0) {
        return 0;
    }
    auto &grant = ability_grants[handle];
    grant.definition = definition;
    grant.default_tags = std::move(defaults);
    grant.level = level;
    return handle;
}

AbilitySystemComponent::AbilityGrant *AbilitySystemComponent::find_ability_run(int64_t handle, uint64_t run) {
    const auto found = ability_grants.find(handle);
    return found != ability_grants.end() && found->second.activation.is_valid() && found->second.activation->run == run ?
            &found->second : nullptr;
}

bool AbilitySystemComponent::is_ability_run_active(int64_t handle, uint64_t run) const {
    const auto found = ability_grants.find(handle);
    if (!ability_scene_active || found == ability_grants.end() || !found->second.started ||
            found->second.activation.is_null() || found->second.activation->run != run) {
        return false;
    }
    const auto *instance = ability_execution(found->second.execution);
    return instance && instance->is_inside_tree() && instance->get_parent() == this;
}

Error AbilitySystemComponent::try_activate_ability(int64_t handle) {
    if (const Error allowed = ability_mutation_status(); allowed != OK) {
        return allowed;
    }
    const ObjectID component_id(get_instance_id());
    const auto live_component = [component_id]() {
        return Object::cast_to<AbilitySystemComponent>(ObjectDB::get_instance(component_id));
    };

    auto found = ability_grants.find(handle);
    if (found == ability_grants.end()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (found->second.activation.is_valid()) {
        if (!found->second.started || is_ability_active(handle)) {
            return ERR_ALREADY_IN_USE;
        }
        // The execution may have been removed by its script. Its cleanup can reenter the component.
        finish_ability_run(found->second, true);
        auto *component = live_component();
        if (!component) {
            return ERR_UNAVAILABLE;
        }
        if (const Error allowed = component->ability_mutation_status(); allowed != OK) {
            return allowed;
        }
        found = component->ability_grants.find(handle);
        if (found == component->ability_grants.end()) {
            return ERR_DOES_NOT_EXIST;
        }
        if (found->second.activation.is_valid()) {
            return ERR_ALREADY_IN_USE;
        }
    }
    auto &grant = found->second;
    const Ref<Script> definition = grant.definition;
    if (!valid_definition(definition) || last_ability_run == std::numeric_limits<uint64_t>::max()) {
        return ERR_CANT_CREATE;
    }

    const uint64_t run = ++last_ability_run;
    Ref<GameplayAbilityActivation> activation;
    activation.instantiate();
    activation->component_id = component_id;
    if (const auto *actor = get_parent()) {
        activation->actor_id = ObjectID(actor->get_instance_id());
    }
    activation->level = grant.level;
    activation->default_tags = grant.default_tags;
    activation->handle = handle;
    activation->run = run;

    auto *instance = memnew(abilities::GameplayAbility);
    const ObjectID execution_id(instance->get_instance_id());
    grant.execution = execution_id;
    grant.activation = activation;

    const auto current_attempt = [live_component, handle, run]() -> AbilityGrant * {
        auto *component = live_component();
        return component ? component->find_ability_run(handle, run) : nullptr;
    };
    const auto discard_attempt = [current_attempt, execution_id](Error status) {
        if (auto *attempt = current_attempt()) {
            finish_ability_run(*attempt, true);
        } else {
            discard_ability_execution(execution_id);
        }
        return status;
    };

    instance->set_script(definition);
    instance = ability_execution(execution_id);
    if (!current_attempt() || !instance || instance->is_queued_for_deletion() || instance->get_parent() ||
            !definition->instance_has(instance) || !instance->has_method("on_activated")) {
        return discard_attempt(ERR_CANT_CREATE);
    }

    Variant result;
    if (instance->has_method("on_can_activate")) {
        if (!call_ability_hook(instance, "on_can_activate", activation, result) || result.get_type() != Variant::BOOL) {
            return discard_attempt(ERR_INVALID_DATA);
        }
        if (!static_cast<bool>(result)) {
            return discard_attempt(ERR_UNAUTHORIZED);
        }
    }

    auto *component = live_component();
    auto *attempt = current_attempt();
    instance = ability_execution(execution_id);
    if (!component || !attempt || !instance || instance->is_queued_for_deletion() || instance->get_parent()) {
        return discard_attempt(ERR_UNAVAILABLE);
    }
    if (const Error allowed = component->ability_mutation_status(); allowed != OK) {
        return discard_attempt(allowed);
    }
    attempt->started = true;
    component->add_child(instance, false, Node::INTERNAL_MODE_BACK);
    component = live_component();
    instance = ability_execution(execution_id);
    if (!component || !instance || instance->is_queued_for_deletion() || !component->is_ability_run_active(handle, run)) {
        return discard_attempt(ERR_UNAVAILABLE);
    }
    if (const Error allowed = component->ability_mutation_status(); allowed != OK) {
        return discard_attempt(allowed);
    }
    return call_ability_hook(instance, "on_activated", activation, result) ? OK : discard_attempt(ERR_INVALID_DATA);
}

void AbilitySystemComponent::finish_ability_run(AbilityGrant &grant, bool canceled) {
    const ObjectID execution_id = std::exchange(grant.execution, ObjectID());
    const bool started = std::exchange(grant.started, false);
    const Ref<GameplayAbilityActivation> activation = std::move(grant.activation);
    auto *instance = ability_execution(execution_id);
    if (started && instance) {
        if (canceled && instance->has_method("on_canceled")) {
            call_ability_cleanup(instance, "on_canceled", activation);
            instance = ability_execution(execution_id);
        }
        if (instance && instance->has_method("on_ended")) {
            const Variant ended(canceled);
            call_ability_cleanup(instance, "on_ended", activation, &ended);
        }
    }
    discard_ability_execution(execution_id);
}

Error AbilitySystemComponent::end_ability_run(int64_t handle, uint64_t run) {
    auto *grant = find_ability_run(handle, run);
    if (!grant || !is_ability_run_active(handle, run)) {
        return ERR_UNAVAILABLE;
    }
    if (const Error allowed = ability_mutation_status(); allowed != OK) {
        return allowed;
    }
    finish_ability_run(*grant, false);
    return OK;
}

Error AbilitySystemComponent::cancel_ability(int64_t handle) {
    if (const Error allowed = ability_mutation_status(); allowed != OK) {
        return allowed;
    }
    const auto found = ability_grants.find(handle);
    if (found == ability_grants.end()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (found->second.activation.is_null()) {
        return ERR_UNAVAILABLE;
    }
    finish_ability_run(found->second, true);
    return OK;
}

Error AbilitySystemComponent::clear_ability(int64_t handle) {
    if (const Error allowed = ability_mutation_status(); allowed != OK) {
        return allowed;
    }
    const auto found = ability_grants.find(handle);
    if (found == ability_grants.end()) {
        return ERR_DOES_NOT_EXIST;
    }
    AbilityGrant grant = std::move(found->second);
    ability_grants.erase(found);
    finish_ability_run(grant, true);
    return OK;
}

bool AbilitySystemComponent::is_ability_active(int64_t handle) const {
    const auto found = ability_grants.find(handle);
    return found != ability_grants.end() && found->second.activation.is_valid() &&
            is_ability_run_active(handle, found->second.activation->run);
}

int64_t AbilitySystemComponent::get_ability_level(int64_t handle) const {
    const auto found = ability_grants.find(handle);
    return found != ability_grants.end() ? found->second.level : 0;
}

} // namespace crimson::attributes
