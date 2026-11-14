// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "register_inventory.hpp"

#include "inventory_actions.hpp"
#include "inventory_models.hpp"
#include "inventory_sorter.hpp"
#include "inventory_stack_adapter.hpp"
#include "inventory_trade.hpp"
#include "inventory_wallet.hpp"
#include "spatial_inventory.h"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::inventory {

void register_types(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_ABSTRACT_CLASS(godot::InventoryItemView);
    GDREGISTER_CLASS(godot::SpatialInventory);
    GDREGISTER_CLASS(godot::Inventory);
    GDREGISTER_CLASS(SpatialInventoryStackAdapter);
    GDREGISTER_CLASS(SpatialInventoryActions);
    GDREGISTER_CLASS(SpatialInventorySorter);
    GDREGISTER_CLASS(SpatialInventoryWallet);
    GDREGISTER_CLASS(SpatialInventoryTrade);
    register_inventory_models();
}

void unregister_types(godot::ModuleInitializationLevel) {
    // The binding library unregisters classes during extension shutdown.
}

} // namespace crimson::inventory
