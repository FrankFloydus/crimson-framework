// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_operation.hpp"
#include "modules/match_runtime/transaction_coordinator.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <thread>

namespace crimson::integration {

class GameRuntime;
class SceneService;
class MatchInventory;

// A trusted pickup description and its source edit in the game's world store.
class InventoryPickup : public godot::RefCounted {
    GDCLASS(InventoryPickup, godot::RefCounted)

    friend class GameInventory;

    // Instance ID to place in the player's grid.
    godot::StringName item_id;
    // Unrotated footprint in cells.
    godot::Vector2i footprint;
    // Game-owned store responsible for the source world item.
    godot::String source_store;
    // Detached source operation to include in the same transaction.
    godot::Dictionary source_edit;

protected:
    // Register the pickup description factory.
    static void _bind_methods();

public:
    // Describe one eligible item's geometry and its committed source edit.
    static godot::Ref<InventoryPickup> take(const godot::StringName &item_id,
            const godot::Vector2i &footprint, const godot::String &source_store,
            const godot::Dictionary &source_edit);
};

// Supplies game-owned pickup eligibility, geometry, and source state.
class InventoryPickupSource : public godot::RefCounted {
    GDCLASS(InventoryPickupSource, godot::RefCounted)

    friend class GameInventory;

    // Install this provider's source stores before authority play begins.
    godot::Error register_stores(const godot::Ref<match_runtime::TransactionCoordinator> &stores);
    // Resolve an eligible item and its source edit using committed authority data.
    godot::Ref<InventoryPickup> resolve(const godot::String &player_id, const godot::StringName &pickup_id,
            const godot::Ref<match_runtime::TransactionCoordinator> &stores);

protected:
    // Optional setup hook for registering game-owned source stores on a fresh coordinator.
    GDVIRTUAL1R(godot::Error, _register_stores, godot::Ref<match_runtime::TransactionCoordinator>)
    // Read committed state and return the pickup the trusted player may take, or null to reject.
    GDVIRTUAL3R(godot::Ref<InventoryPickup>, _resolve_pickup, godot::String, godot::StringName,
            godot::Ref<match_runtime::TransactionCoordinator>)
    // Register the game-owned policy hooks.
    static void _bind_methods();
};

// Branchable backpack settings for a coordinated game.
class InventoryConfig : public godot::RefCounted {
    GDCLASS(InventoryConfig, godot::RefCounted)

    friend class GameInventory;

    // Initial backpack dimensions in cells.
    godot::Vector2i size;
    // Optional trusted policy for world pickups.
    godot::Ref<InventoryPickupSource> source;

protected:
    // Register the config factory and branch modifier.
    static void _bind_methods();

public:
    // Return valid initial dimensions, or null when the grid size is invalid.
    static godot::Ref<InventoryConfig> backpack(const godot::Vector2i &size);
    // Copy these settings with a different world pickup provider.
    godot::Ref<InventoryConfig> pickups(const godot::Ref<InventoryPickupSource> &source) const;
};

// Terminal authority outcome and placement receipt from one pickup.
class InventoryResult : public godot::RefCounted {
    GDCLASS(InventoryResult, godot::RefCounted)

    // Whether the pickup committed.
    bool ok = false;
    // Stable outcome code.
    godot::String code;
    // Human-readable outcome.
    godot::String message;
    // Item instance that committed, or empty on rejection.
    godot::StringName item_id;
    // Committed top-left cell, or (-1, -1) when no item was placed.
    godot::Vector2i origin = {-1, -1};
    // Coordinator revision after commit, or -1 on rejection.
    int64_t revision = -1;

protected:
    // Register read-only result fields.
    static void _bind_methods();

public:
    // Adapt the existing bounded command outcome into a typed receipt.
    static godot::Ref<InventoryResult> from_dictionary(const godot::Dictionary &outcome);
    // Whether both source consumption and placement committed.
    bool is_ok() const;
    // Return the machine-readable outcome code.
    godot::String get_code() const;
    // Return the human-readable explanation.
    godot::String get_message() const;
    // Return the committed item instance ID.
    godot::StringName get_item_id() const;
    // Return the placed item's top-left cell.
    godot::Vector2i get_origin() const;
    // Return the coordinator revision of the committed pickup.
    int64_t get_revision() const;
};

// Adapts the command operation's pending dictionary signal to a typed receipt.
class InventoryPickupWaiter : public godot::RefCounted {
    GDCLASS(InventoryPickupWaiter, godot::RefCounted)

    // Emit a typed receipt while the source signal's bound reference keeps this waiter alive.
    static void complete(const godot::Dictionary &outcome, const godot::Ref<InventoryPickupWaiter> &waiter);

protected:
    // Register the typed completion signal.
    static void _bind_methods();

public:
    // Return the terminal receipt or a signal that delivers it.
    static godot::Variant wait(const godot::Ref<GameOperation> &operation);
};

// Game-scoped factory for a match-bound local backpack.
class GameInventory : public godot::RefCounted {
    GDCLASS(GameInventory, godot::RefCounted)

    friend class MatchInventory;

    // Runtime instance captured at attachment.
    godot::ObjectID runtime_id;
    // Exact scene service token used to reject a detached or replaced game scope.
    godot::Ref<SceneService> scope;
    // Configuration selected when the game was attached.
    godot::Ref<InventoryConfig> config;
    // Thread that owns the runtime, stores, and command registration.
    std::thread::id owner_thread;
    // Coordinator on which the source and backpack stores were last prepared.
    godot::ObjectID prepared_id;
    // Result of store setup on that coordinator, retained to prevent repeating user registration.
    godot::Error prepared_status = godot::ERR_UNCONFIGURED;
    // Advances when the runtime starts or retires a match.
    uint64_t match_generation = 0;

    // Resolve the runtime while it still owns the captured scope.
    GameRuntime *resolve_game() const;
    // Invalidate backpack handles when a new match starts or the current one retires.
    void on_phase_changed(int64_t phase);
    // Apply a validated player-grid edit to detached store state.
    godot::Dictionary prepare_backpacks(const godot::Dictionary &current, const godot::Array &operations,
            const godot::Dictionary &context) const;
    // Validate every restored player's grid before any match state changes.
    godot::Dictionary validate_backpacks(const godot::Dictionary &candidate,
            const godot::Dictionary &context) const;
    // Resolve a trusted player's pickup and commit both source and destination changes.
    godot::Dictionary take_pickup(const godot::String &player_id, const godot::Dictionary &intent);
    // Submit a match-bound local pickup intent through the normal command path.
    godot::Variant submit_pickup(uint64_t generation, const godot::String &player_id,
            const godot::StringName &pickup_id);

protected:
    // Register the local backpack accessor.
    static void _bind_methods();

public:
    // Install the shared pickup command on an attached coordinated runtime.
    static godot::Ref<GameInventory> attach(GameRuntime *runtime, const godot::Ref<InventoryConfig> &config);
    // Register inventory and source stores once for the prospective authority match.
    godot::Error prepare();
    // Resolve the local player's current backpack after the match is ready.
    godot::Ref<MatchInventory> backpack();
};

// A local player's backpack handle for one match generation.
class MatchInventory : public godot::RefCounted {
    GDCLASS(MatchInventory, godot::RefCounted)

    friend class GameInventory;

    // Owning scoped inventory service.
    godot::Ref<GameInventory> owner;
    // Exact match generation captured when the handle was resolved.
    uint64_t generation = 0;
    // Trusted local player ID resolved from the runtime at handle creation.
    godot::String player_id;

protected:
    // Register the typed pickup action.
    static void _bind_methods();

public:
    // Request a pickup and return a terminal InventoryResult or its completion signal.
    godot::Variant pick_up(const godot::StringName &pickup_id);
};

// Register the coordinated inventory bindings after standalone inventory and game operations.
void register_inventory_integration();

} // namespace crimson::integration
