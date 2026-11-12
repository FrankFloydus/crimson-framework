// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "inventory_models.hpp"

#include "inventory_containers.hpp"
#include "inventory_equipment.hpp"
#include "inventory_staging.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace crimson::inventory {

void register_inventory_models() {
    GDREGISTER_CLASS(SpatialInventoryContainers);
    GDREGISTER_CLASS(SpatialInventoryEquipment);
    GDREGISTER_CLASS(SpatialInventoryStaging);
}

} // namespace crimson::inventory
