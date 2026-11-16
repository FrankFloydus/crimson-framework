// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <optional>

namespace crimson::attributes {

// Detached immutable reading from an authority or recipient view.
class GameplayAttributeValue : public godot::RefCounted {
    GDCLASS(GameplayAttributeValue, godot::RefCounted)

    double current = 0; // Confirmed effective value.
    std::optional<double> base; // Available on authority readings.

protected:
    // Registers read-only value accessors.
    static void _bind_methods();

public:
    // Creates a detached reading for a native owner or adapter.
    static godot::Ref<GameplayAttributeValue> make(double current, std::optional<double> base = {});
    // Returns the confirmed effective value.
    double get_current() const;
    // Reports whether this reading contains the authority's base value.
    bool has_base() const;
    // Returns the base value or zero when it is unavailable.
    double get_base() const;
};

class GameplayAttributeWatch;

// A typed handle over standalone attributes or a game-owned coordinated character.
class GameplayAttributes : public godot::RefCounted {
    GDCLASS(GameplayAttributes, godot::RefCounted)

    godot::Dictionary values; // Independent standalone base values by qualified String ID.
    godot::Callable read_bridge; // Reads a detached value from an adapter.
    godot::Callable edit_bridge; // Commits a base edit through an adapter when available.
    godot::ObjectID publisher_id; // Checked before inspecting native callables that borrow this object.
    bool bridged = false; // Distinguishes expired adapter callbacks from a standalone owner.
    bool notifying = false; // Gates direct mutation during publication callbacks.

    // Applies an addition or replacement through the native owner.
    godot::Error edit_base(const godot::StringName &id, double operand, bool add);

protected:
    // Registers factories, typed reads, authority edits, and the notification.
    static void _bind_methods();

public:
    // Creates a standalone owner from a validated project registry and selected set IDs.
    static godot::Ref<GameplayAttributes> from_sets(const godot::PackedStringArray &set_ids);
    // Creates a handle guarded by its native callback owner's lifetime. A missing edit is read-only.
    static godot::Ref<GameplayAttributes> create_bridge(godot::ObjectID publisher, const godot::Callable &read,
            const godot::Callable &edit = {});
    // Returns a detached reading, or null when unknown or unavailable.
    godot::Ref<GameplayAttributeValue> get_attribute(const godot::StringName &id) const;
    // Commits a finite base replacement through the authority.
    godot::Error set_base(const godot::StringName &id, double value);
    // Commits a base change through the authority when the result is finite.
    godot::Error add_base(const godot::StringName &id, double delta);
    // Creates a retainable filtered observer for one exact attribute ID.
    godot::Ref<GameplayAttributeWatch> watch_attribute(const godot::StringName &id);
    // Publishes a complete, committed change. Equal readings are ignored.
    void notify_changed(const godot::StringName &id, const godot::Ref<GameplayAttributeValue> &previous,
            const godot::Ref<GameplayAttributeValue> &current);
};

// Filters publication to one attribute without duplicating gameplay state.
class GameplayAttributeWatch : public godot::RefCounted {
    GDCLASS(GameplayAttributeWatch, godot::RefCounted)
    friend class GameplayAttributes;

    godot::Ref<GameplayAttributes> source; // Keeps the observed handle alive.
    godot::StringName attribute_id; // Exact ID passed through this watch.

    // Subscribes to an already scoped owner and one qualified ID.
    void bind_source(const godot::Ref<GameplayAttributes> &attributes, const godot::StringName &id);
    // Forwards the committed change if its ID matches.
    void on_attribute(const godot::StringName &id, const godot::Ref<GameplayAttributeValue> &previous,
            const godot::Ref<GameplayAttributeValue> &current);

protected:
    // Registers the filtered change signal and value read.
    static void _bind_methods();

public:
    // Releases the filtered signal connection.
    ~GameplayAttributeWatch() override;
    // Reads the latest detached value or null if unavailable.
    godot::Ref<GameplayAttributeValue> get_value() const;
};

// Registers the standalone Attributes API during the scene initialization level.
void register_gameplay_attributes(godot::ModuleInitializationLevel level);

} // namespace crimson::attributes
