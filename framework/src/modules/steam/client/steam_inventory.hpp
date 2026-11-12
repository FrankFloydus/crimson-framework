// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_inventory_types.hpp"

#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;
struct SteamInventoryRequest;
struct SteamInventoryState;

// Reads a signed-in player's Steam inventory and submits configured item actions.
class SteamInventory : public godot::RefCounted {
    GDCLASS(SteamInventory, godot::RefCounted)

public:
    // Initialize a detached service until a Steam client starts.
    SteamInventory();
    // Destroy the detached native bookkeeping.
    ~SteamInventory();

    // Await the known Steam item definitions in a typed catalog result.
    godot::Variant definitions();
    // Await one known definition by its positive published ID.
    godot::Variant definition(int64_t definition_id);
    // Await a snapshot of the signed-in player's full inventory.
    godot::Variant items();
    // Await a snapshot of the selected owned instances.
    godot::Variant items_by_id(const godot::PackedStringArray &instance_ids);
    // Await the rows Steam grants for the account's eligible configured promotions.
    godot::Variant claim_promotions();
    // Await the rows Steam grants for a single configured promotion.
    godot::Variant claim_promotion(int64_t definition_id);
    // Await evaluation of the published playtime drop generator.
    godot::Variant claim_playtime_drop(int64_t generator_id);
    // Await Steam's atomic exchange of the owned ingredients for one output item.
    godot::Variant craft(int64_t output_definition_id, const godot::TypedArray<SteamItemQuantity> &ingredients);
    // Await Steam's permanent consumption of the requested stack quantity.
    godot::Variant consume(const godot::String &instance_id, int64_t quantity = 1);
    // Await movement of the requested quantity from one stack into a new instance.
    godot::Variant split(const godot::String &instance_id, int64_t quantity);
    // Await movement of the requested quantity between distinct owned stacks.
    godot::Variant merge(const godot::String &from_instance_id, const godot::String &into_instance_id,
            int64_t quantity);
    // Await bounded, signed Steam result bytes containing exactly the selected instances.
    godot::Variant proof(const godot::PackedStringArray &instance_ids);

protected:
    // Register the awaitable inventory actions and typed change signals.
    static void _bind_methods();

private:
    friend class SteamClient;

    // Receives a detached catalog view or its error after a definition load settles.
    using CatalogObserver = std::function<void(godot::Error, const godot::TypedArray<SteamItemDefinition> &)>;

    // Subscribe this new client scope to app inventory callbacks without loading its catalog.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Release every owned result and subscription before settling retired observers.
    void close();
    // Retire expired native requests before notifying their observers.
    void tick();
    // Resolve the runtime only for the main thread and this exact live start.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Share one lazy catalog load across pending definition requests in this scope.
    void request_catalog(CatalogObserver observer);
    // Read a detached catalog from the definition cache Steam made available.
    godot::Error read_catalog(const SteamRuntime &active, godot::TypedArray<SteamItemDefinition> &catalog);
    // Invalidate the old catalog and settle readers after an SDK definition update.
    void on_definitions_updated();
    // Record Steam's freshness evidence only for a full result this scope owns.
    void on_full_update(int32_t handle);
    // Copy a completed result, release its handle, and settle the exact observer.
    void on_result_ready(int32_t handle, int status);
    // Dispatch one request after its argument validation and reserve its native result.
    void submit(SteamInventoryRequest request);
    // Await a submitted full or selected read with a typed snapshot result.
    godot::Variant observe_snapshot(SteamInventoryRequest request);
    // Await a submitted mutation with its typed row changes.
    godot::Variant observe_change(SteamInventoryRequest request);
    // Validate and submit a stack transfer, with an empty destination for a split.
    godot::Variant transfer(const godot::String &from_instance_id, const godot::String &into_instance_id,
            int64_t quantity, bool split_stack);

    // The signed-in client runtime, retained only by its owning clients.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact scope liveness for this client start.
    std::weak_ptr<SteamClientScope> client_scope;
    // Main-thread callback subscriptions, owned results, and lazy catalog state.
    std::unique_ptr<SteamInventoryState> state;
};

} // namespace crimson::steam
