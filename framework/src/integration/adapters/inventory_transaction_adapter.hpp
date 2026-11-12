// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/match_runtime/transaction_coordinator.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

namespace crimson::integration {

class InventoryTransactionAdapter : public godot::RefCounted {
    GDCLASS(InventoryTransactionAdapter, godot::RefCounted)

    // Transaction coordinator that owns this inventory's committed store.
    godot::Ref<match_runtime::TransactionCoordinator> coordinator;
    // Registered key for the inventory snapshot.
    godot::String store_id;

    // Queues an edit on a transaction owned by the configured coordinator.
    godot::Error queue_operation(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::Dictionary &operation) const;

protected:
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Validates a native grid snapshot and registers its authoritative store.
    godot::Error configure(const godot::Ref<match_runtime::TransactionCoordinator> &p_coordinator,
            const godot::String &p_store_id, const godot::Dictionary &initial_snapshot);
    // Returns the committed snapshot in native SpatialInventory format.
    godot::Dictionary get_snapshot() const;
    // Queues placement of an item in the configured inventory store.
    godot::Error queue_place(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &item_id, const godot::Vector2i &footprint,
            const godot::Vector2i &origin, bool rotated = false) const;
    // Queues a position or rotation change for an existing item.
    godot::Error queue_move(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &item_id, const godot::Vector2i &origin, bool rotated = false) const;
    // Queues removal of an item from the configured inventory store.
    godot::Error queue_remove(const godot::Ref<match_runtime::GameTransaction> &transaction,
            const godot::String &item_id) const;
};

} // namespace crimson::integration
