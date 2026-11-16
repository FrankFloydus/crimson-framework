// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/attributes/attribute_registry.hpp"
#include "modules/attributes/gameplay_attributes.hpp"
#include "modules/match_runtime/transaction_coordinator.hpp"

#include <godot_cpp/variant/packed_string_array.hpp>

#include <map>
#include <thread>

namespace crimson::attributes {
class AbilitySystemComponent;
}

namespace crimson::integration {

class GameRuntime;
class SceneService;

// Owns the coordinated attribute store and recipient-scoped views for one game attachment.
class MatchAttributes : public godot::RefCounted {
    GDCLASS(MatchAttributes, godot::RefCounted)

    // Original runtime, resolved only on its owning thread.
    godot::ObjectID runtime_id;
    // Exact scene service identity that invalidates when the game detaches.
    godot::Ref<SceneService> scope;
    // Thread that owns the game, store, and replicated view.
    const std::thread::id owner_thread = std::this_thread::get_id();
    // Validated project definitions frozen at boot.
    attributes::AttributeRegistry registry;
    // Deterministic gameplay schema digest shared by matching game builds and checkpoints.
    godot::String schema;
    // Authority coordinator owning the store for this match.
    godot::ObjectID prepared_id;
    // Changes when a session retires and existing handles become unavailable.
    int64_t match_generation = 0;
    // Changes on publication to stop stale notifications after reentrant lifecycle work.
    uint64_t publication = 0;
    // Weak references to script handles that are actively observing characters.
    std::map<godot::String, godot::ObjectID> handles;
    // Last committed authority values for notifications.
    godot::Dictionary authority_values;
    // Last confirmed client values keyed by character and attribute ID.
    godot::Dictionary client_values;

    // Resolve only the original live runtime on its owner thread.
    GameRuntime *resolve_game() const;
    // Check that the scene component belongs to this live game attachment.
    bool owns_component(attributes::AbilitySystemComponent *component) const;
    // Resolve the current authority store when reads or mutations are allowed.
    godot::Ref<match_runtime::TransactionCoordinator> authority_store(bool write) const;
    // Resolve a stored callback's service only while this exact coordinator owns the match.
    static godot::Ref<MatchAttributes> resolve_store_owner(int64_t service_id, int64_t coordinator_id, bool write);
    // Create or reuse this generation's character bridge even before values are visible.
    godot::Ref<attributes::GameplayAttributes> character_bridge(const godot::String &character_id);
    // Queue a detached operation on the active authority's own transaction.
    godot::Error queue_change(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::Dictionary &change);
    // Submit a single edit to the same authority transaction path.
    godot::Error submit_change(const godot::Dictionary &change);
    // Validate and prepare all requested edits without external side effects.
    godot::Dictionary prepare_changes(const godot::Dictionary &current, const godot::Array &operations,
            const godot::Dictionary &context) const;
    // Resolve the matching authority before a retained coordinator prepares changes.
    static godot::Dictionary prepare_store(const godot::Dictionary &current, const godot::Array &operations,
            const godot::Dictionary &context, int64_t service_id, int64_t coordinator_id);
    // Validate a complete detached restore before any state swaps.
    godot::Dictionary validate_restore(const godot::Dictionary &candidate, const godot::Dictionary &context) const;
    // Resolve the matching authority before a retained coordinator validates a restore.
    static godot::Dictionary restore_store(const godot::Dictionary &candidate, const godot::Dictionary &context,
            int64_t service_id, int64_t coordinator_id);
    // Read requested fields from the authority store or the installed client projection.
    godot::Ref<attributes::GameplayAttributeValue> read_value(const godot::StringName &attribute_id,
            const godot::String &character_id, int64_t generation) const;
    // Apply a base edit when the originating scope still owns this authority.
    godot::Error edit_value(const godot::StringName &attribute_id, double operand, bool add,
            const godot::String &character_id, int64_t generation);
    // Return all currently readable values for the character in this role.
    godot::Dictionary read_character(const godot::String &character_id) const;
    // Record committed changes only after the coordinator publishes all participating stores.
    static void on_committed(int64_t revision, const godot::PackedStringArray &changed_stores,
            int64_t service_id, int64_t coordinator_id);
    // Install the confirmed view after the coordinated runtime exposes its world.
    void on_projection_updated(int64_t revision, bool baseline);
    // Invalidate every remote value when the continuous stream is unavailable.
    void on_projection_cleared();
    // Invalidate handles at match end and suspend remote views during transitions.
    void on_phase_changed(int64_t phase);
    // Swap a complete values map before notifying any existing handles.
    void publish_values(godot::Dictionary &values, godot::Dictionary next, bool authority);

protected:
    // Register the scoped read, authority edit, and transaction operations.
    static void _bind_methods();

public:
    // Freeze the authenticated authority and client schema for a configured coordinated game.
    static godot::Ref<MatchAttributes> attach(GameRuntime *runtime);
    // Install the owning store exactly once per authority coordinator.
    godot::Error prepare();
    // Project only the committed attributes the trusted recipient may see.
    godot::Error append_projection(const godot::String &player_id, godot::Dictionary &view) const;
    // Check compatibility and typed rows before a replicate view can be acknowledged or exposed.
    bool validate_projection(const godot::Dictionary &view) const;
    // Recognize keys used only by this adapter inside a composite projection.
    static bool reserved_projection_key(const godot::String &key);

    // Open a typed handle to a known character in the current authority or client view.
    godot::Ref<attributes::GameplayAttributes> for_character(const godot::String &character_id);
    // Attach a match component to this generation's scoped authority or client view.
    godot::Error bind_component(attributes::AbilitySystemComponent *component, const godot::String &character_id);
    // Create an authority character from project sets and its explicit visibility policy.
    godot::Error add_character(const godot::String &character_id, const godot::PackedStringArray &sets,
            const godot::String &owner = {}, const godot::PackedStringArray &observers = {});
    // Remove an authority character and revoke its projected records.
    godot::Error remove_character(const godot::String &character_id);
    // Replace the authority owner and observer lists in a single committed edit.
    godot::Error set_visibility(const godot::String &character_id, const godot::String &owner,
            const godot::PackedStringArray &observers);
    // Stage a character in the same authority transaction as related game state.
    godot::Error queue_add_character(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &character_id, const godot::PackedStringArray &sets,
            const godot::String &owner = {}, const godot::PackedStringArray &observers = {});
    // Stage a character using the match component's configured attribute sets.
    godot::Error queue_add_component(const godot::Ref<match_runtime::GameTransaction> &transaction,
            attributes::AbilitySystemComponent *component, const godot::String &character_id,
            const godot::String &owner = {}, const godot::PackedStringArray &observers = {});
    // Stage removal and visibility revocation in an existing authority transaction.
    godot::Error queue_remove_character(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &character_id);
    // Stage a new owner and observer list in an existing authority transaction.
    godot::Error queue_visibility(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &character_id, const godot::String &owner,
            const godot::PackedStringArray &observers);
    // Stage a set or additive base edit in an existing authority transaction.
    godot::Error queue_base(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &character_id, const godot::StringName &attribute_id, double operand, bool add = false);
};

// Register coordinated Attributes after the native attribute types and match runtime.
void register_attributes_integration();

} // namespace crimson::integration
