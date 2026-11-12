// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace crimson::inventory {

class SpatialInventoryWallet : public godot::RefCounted {
    GDCLASS(SpatialInventoryWallet, godot::RefCounted)

    // Committed amounts by actor and currency ID.
    godot::Dictionary balances;
    // Guards state changes against reentrant callbacks.
    bool busy = false;

    // Updates one actor's stored balance for the given currency.
    void write_balance(int64_t actor, const godot::StringName &currency, int64_t amount);

protected:
    // Allows a Godot subclass to provide the authoritative wallet balance.
    GDVIRTUAL2RC(int64_t, _get_balance, int64_t, godot::StringName)
    // Allows a Godot subclass to commit a checked wallet batch.
    GDVIRTUAL2R(godot::Error, _apply_balance_changes, godot::TypedArray<godot::Dictionary>, godot::Callable)
    // Registers Godot methods, properties, and signals declared by this type.
    static void _bind_methods();

public:
    // Returns the current balance for the actor and currency.
    virtual int64_t get_balance(int64_t actor, const godot::StringName &currency) const;
    // Applies an expected-value update to the actor's currency balance.
    godot::Error set_balance(int64_t actor, const godot::StringName &currency, int64_t amount);
    // Validates and applies a batch of balance changes with an optional paired commit.
    virtual godot::Error apply_balance_changes(const godot::Array &changes, const godot::Callable &commit = {});
};

} // namespace crimson::inventory
