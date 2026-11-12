// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

namespace crimson::inventory {

namespace detail {

class InventoryCallback final : public godot::CallableCustom {
    // Native commit closure invoked through this Godot callable.
    std::function<godot::Error()> callback;
    // Thread allowed to invoke the synchronous commit closure.
    const std::thread::id owner_thread = std::this_thread::get_id();

    // Compares custom callables by instance identity.
    static bool equal(const CallableCustom *left, const CallableCustom *right) {
        return left == right;
    }

    // Orders custom callables by instance identity.
    static bool less(const CallableCustom *left, const CallableCustom *right) {
        return std::less<const CallableCustom *>()(left, right);
    }

public:
    // Wraps a native commit closure as a thread-bound Godot callable.
    explicit InventoryCallback(std::function<godot::Error()> function) : callback(std::move(function)) {}

    // Returns whether the wrapped commit closure exists.
    bool is_valid() const override { return static_cast<bool>(callback); }
    // Returns the empty owner ID for this standalone callable.
    godot::ObjectID get_object() const override { return {}; }
    // Returns the display name of the custom callable.
    godot::String get_as_text() const override { return "Inventory transaction callback"; }
    // Returns the custom callable identity equality function.
    CompareEqualFunc get_compare_equal_func() const override { return &equal; }
    // Returns the custom callable ordering function.
    CompareLessFunc get_compare_less_func() const override { return &less; }

    // Returns an identity hash for the custom callable.
    uint32_t hash() const override {
        return static_cast<uint32_t>(std::hash<const InventoryCallback *>()(this));
    }

    // Returns zero as the required callback argument count.
    int get_argument_count(bool &valid) const override {
        valid = true;
        return 0;
    }

    // Checks arity and thread ownership before invoking the commit closure.
    void call(const godot::Variant **, int count, godot::Variant &result, GDExtensionCallError &error) const override {
        if (count != 0) {
            error = {GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS, count, 0};
            return;
        }
        error = {GDEXTENSION_CALL_OK, 0, 0};
        // The host may retain the Callable, but a placement commit belongs to the thread
        // that is running the synchronous inventory transaction.
        if (std::this_thread::get_id() != owner_thread) {
            result = static_cast<int64_t>(godot::ERR_BUSY);
            return;
        }
        result = static_cast<int64_t>(callback());
    }
};

} // namespace detail

// Creates a Godot callable for a synchronous native commit.
inline godot::Callable inventory_callback(std::function<godot::Error()> function) {
    return godot::Callable(memnew(detail::InventoryCallback(std::move(function))));
}

} // namespace crimson::inventory
