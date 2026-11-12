// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <functional>

namespace crimson::steam {

// One published item definition copied from the Steam catalog.
class SteamItemDefinition : public godot::RefCounted {
    GDCLASS(SteamItemDefinition, godot::RefCounted)

public:
    // Initialize any collection properties as read-only.
    SteamItemDefinition();
    // Return the published numeric item definition ID.
    int64_t get_definition_id() const;
    // Return steam's localized display name for this item.
    godot::String get_display_name() const;
    // Return the item type configured in the Steam definition.
    godot::StringName get_type() const;
    // Return the typed read-only dictionary of published properties.
    godot::TypedDictionary<godot::String, godot::String> get_properties() const;

protected:
    // Register the typed inventory properties with Godot.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // The published numeric item definition ID.
    int64_t definition_id = 0;
    // Steam's localized display name for this item.
    godot::String display_name;
    // The item type configured in the Steam definition.
    godot::StringName type;
    // The typed read-only dictionary of published properties.
    godot::TypedDictionary<godot::String, godot::String> properties;
};

// A detached item instance and stack state from Steam.
class SteamItemInstance : public godot::RefCounted {
    GDCLASS(SteamItemInstance, godot::RefCounted)

public:
    // Return the opaque instance ID in unsigned decimal form.
    godot::String get_instance_id() const;
    // Return the published definition associated with this instance.
    int64_t get_definition_id() const;
    // Return the current stack quantity reported by Steam.
    int64_t get_quantity() const;
    // Return the Steam item status and action confirmation bits.
    int64_t get_flags() const;

protected:
    // Register the typed inventory properties with Godot.
    static void _bind_methods();

private:
    friend class SteamInventory;
    friend class SteamInventoryVerifier;
    friend class SteamPublisherInventory;

    // The opaque instance ID in unsigned decimal form.
    godot::String instance_id;
    // The published definition associated with this instance.
    int64_t definition_id = 0;
    // The current stack quantity reported by Steam.
    int64_t quantity = 0;
    // The Steam item status and action confirmation bits.
    int64_t flags = 0;
};

// An owned item instance and positive quantity to exchange.
class SteamItemQuantity : public godot::RefCounted {
    GDCLASS(SteamItemQuantity, godot::RefCounted)

public:
    // Pair an instance and quantity for craft(), which validates the pair before dispatch.
    static godot::Ref<SteamItemQuantity> make(const godot::String &instance_id, int64_t quantity);
    // Return the opaque instance ID in unsigned decimal form.
    godot::String get_instance_id() const;
    // Return the requested positive exchange quantity.
    int64_t get_quantity() const;

protected:
    // Register the typed inventory properties with Godot.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // The opaque instance ID in unsigned decimal form.
    godot::String instance_id;
    // The requested positive exchange quantity.
    int64_t quantity = 0;
};

// A detached inventory read and its observation metadata.
class SteamInventorySnapshot : public godot::RefCounted {
    GDCLASS(SteamInventorySnapshot, godot::RefCounted)

public:
    // Freshness evidence reported by Steam for this snapshot.
    enum Freshness {
        FRESHNESS_UNCONFIRMED, // Steam supplied no evidence that the view is fresh or cached.
        FRESHNESS_FRESH, // Steam announced a newer full inventory result for this request.
        FRESHNESS_CACHED, // Steam reported an expired result but supplied readable item data.
        FRESHNESS_VERIFIED_PROOF, // The server verified a signed selection within its proof age limit.
    };

    // Initialize any collection properties as read-only.
    SteamInventorySnapshot();
    // Return the read-only array of present item instances.
    godot::TypedArray<SteamItemInstance> get_items() const;
    // Return the SDK evidence available about the age of this view.
    Freshness get_freshness() const;
    // Return steam's result timestamp in Unix seconds, or zero if unavailable.
    int64_t get_observed_at_unix() const;

protected:
    // Register the typed inventory properties with Godot.
    static void _bind_methods();

private:
    friend class SteamInventory;
    friend class SteamInventoryVerifier;
    friend class SteamPublisherInventory;

    // The read-only array of present item instances.
    godot::TypedArray<SteamItemInstance> items;
    // The SDK evidence available about the age of this view.
    Freshness freshness = FRESHNESS_UNCONFIRMED;
    // Steam's result timestamp in Unix seconds, or zero if unavailable.
    int64_t observed_at_unix = 0;
};

// The confirmed item rows returned by a Steam mutation.
class SteamInventoryChange : public godot::RefCounted {
    GDCLASS(SteamInventoryChange, godot::RefCounted)

public:
    // Initialize any collection properties as read-only.
    SteamInventoryChange();
    // Return the read-only array of item rows Steam granted or created.
    godot::TypedArray<SteamItemInstance> get_granted() const;
    // Return the read-only array of existing item rows updated by Steam.
    godot::TypedArray<SteamItemInstance> get_changed() const;
    // Return the decimal instance IDs Steam reports as removed.
    godot::PackedStringArray get_removed_instance_ids() const;

protected:
    // Register the typed inventory properties with Godot.
    static void _bind_methods();

private:
    friend class SteamInventory;
    friend class SteamPublisherInventory;

    // The read-only array of item rows Steam granted or created.
    godot::TypedArray<SteamItemInstance> granted;
    // The read-only array of existing item rows updated by Steam.
    godot::TypedArray<SteamItemInstance> changed;
    // The decimal instance IDs Steam reports as removed.
    godot::PackedStringArray removed_instance_ids;
};

// Terminal status and typed payload from a Steam inventory request.
class SteamItemDefinitionsResult : public SteamResult {
    GDCLASS(SteamItemDefinitionsResult, SteamResult)

public:
    // Initialize an empty read-only catalog for script-created results.
    SteamItemDefinitionsResult();
    // Return the typed catalog, empty on error.
    godot::TypedArray<SteamItemDefinition> get_value() const;

protected:
    // Bind the typed payload property.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // Construct the detached outcome before delivering it to any observer.
    static godot::Ref<SteamItemDefinitionsResult> create(godot::Error error,
            const godot::String &message = {}, godot::TypedArray<SteamItemDefinition> payload = {});

    // The typed catalog, empty on error.
    godot::TypedArray<SteamItemDefinition> value;
};

// Terminal status and typed payload from a Steam inventory request.
class SteamItemDefinitionResult : public SteamResult {
    GDCLASS(SteamItemDefinitionResult, SteamResult)

public:
    // Return the requested definition, or null on failure.
    godot::Ref<SteamItemDefinition> get_value() const;

protected:
    // Bind the typed payload property.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // Construct the detached outcome before delivering it to any observer.
    static godot::Ref<SteamItemDefinitionResult> create(godot::Error error,
            const godot::String &message = {}, godot::Ref<SteamItemDefinition> payload = {});

    // The requested definition, or null on failure.
    godot::Ref<SteamItemDefinition> value;
};

// Terminal status and typed payload from a Steam inventory request.
class SteamInventorySnapshotResult : public SteamResult {
    GDCLASS(SteamInventorySnapshotResult, SteamResult)

public:
    // Return the detached inventory snapshot, or null on failure.
    godot::Ref<SteamInventorySnapshot> get_value() const;

protected:
    // Bind the typed payload property.
    static void _bind_methods();

private:
    friend class SteamInventory;
    friend class SteamInventoryVerifier;
    friend class SteamPublisherInventory;

    // Construct the detached outcome before delivering it to any observer.
    static godot::Ref<SteamInventorySnapshotResult> create(godot::Error error,
            const godot::String &message = {}, godot::Ref<SteamInventorySnapshot> payload = {});

    // The detached inventory snapshot, or null on failure.
    godot::Ref<SteamInventorySnapshot> value;
};

// Terminal status and typed payload from a Steam inventory request.
class SteamInventoryChangeResult : public SteamResult {
    GDCLASS(SteamInventoryChangeResult, SteamResult)

public:
    // Return the accepted inventory change, or null on failure.
    godot::Ref<SteamInventoryChange> get_value() const;

protected:
    // Bind the typed payload property.
    static void _bind_methods();

private:
    friend class SteamInventory;
    friend class SteamPublisherInventory;

    // Construct the detached outcome before delivering it to any observer.
    static godot::Ref<SteamInventoryChangeResult> create(godot::Error error,
            const godot::String &message = {}, godot::Ref<SteamInventoryChange> payload = {});

    // The accepted inventory change, or null on failure.
    godot::Ref<SteamInventoryChange> value;
};

// Retains the exact completion observed by a directly awaited inventory method.
class SteamItemDefinitionsOperation : public godot::RefCounted {
    GDCLASS(SteamItemDefinitionsOperation, godot::RefCounted)

public:
    // Report whether the operation reached a terminal result.
    bool is_finished() const;
    // Return the typed result, or null while waiting.
    godot::Ref<SteamItemDefinitionsResult> result() const;
    // Return the finished result or the signal to await.
    godot::Variant wait();

protected:
    // Register the typed completion signal and its result readers.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // Store the terminal outcome before notifying listeners exactly once.
    void finish(const godot::Ref<SteamItemDefinitionsResult> &finished_result);

    // The settled payload, retained independently of the native result handle.
    godot::Ref<SteamItemDefinitionsResult> outcome;
};

// Retains the exact completion observed by a directly awaited inventory method.
class SteamItemDefinitionOperation : public godot::RefCounted {
    GDCLASS(SteamItemDefinitionOperation, godot::RefCounted)

public:
    // Report whether the operation reached a terminal result.
    bool is_finished() const;
    // Return the typed result, or null while waiting.
    godot::Ref<SteamItemDefinitionResult> result() const;
    // Return the finished result or the signal to await.
    godot::Variant wait();

protected:
    // Register the typed completion signal and its result readers.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // Store the terminal outcome before notifying listeners exactly once.
    void finish(const godot::Ref<SteamItemDefinitionResult> &finished_result);

    // The settled payload, retained independently of the native result handle.
    godot::Ref<SteamItemDefinitionResult> outcome;
};

// Retains the exact completion observed by a directly awaited inventory method.
class SteamInventorySnapshotOperation : public godot::RefCounted {
    GDCLASS(SteamInventorySnapshotOperation, godot::RefCounted)

public:
    // Report whether the operation reached a terminal result.
    bool is_finished() const;
    // Return the typed result, or null while waiting.
    godot::Ref<SteamInventorySnapshotResult> result() const;
    // Return the finished result or the signal to await.
    godot::Variant wait();

protected:
    // Register the typed completion signal and its result readers.
    static void _bind_methods();

private:
    friend class SteamInventory;
    friend class SteamInventoryVerifier;

    // Store the terminal outcome before notifying listeners exactly once.
    void finish(const godot::Ref<SteamInventorySnapshotResult> &finished_result);

    // The settled payload, retained independently of the native result handle.
    godot::Ref<SteamInventorySnapshotResult> outcome;
};

// Retains the exact completion observed by a directly awaited inventory method.
class SteamInventoryChangeOperation : public godot::RefCounted {
    GDCLASS(SteamInventoryChangeOperation, godot::RefCounted)

public:
    // Report whether the operation reached a terminal result.
    bool is_finished() const;
    // Return the typed result, or null while waiting.
    godot::Ref<SteamInventoryChangeResult> result() const;
    // Return the finished result or the signal to await.
    godot::Variant wait();

protected:
    // Register the typed completion signal and its result readers.
    static void _bind_methods();

private:
    friend class SteamInventory;

    // Store the terminal outcome before notifying listeners exactly once.
    void finish(const godot::Ref<SteamInventoryChangeResult> &finished_result);

    // The settled payload, retained independently of the native result handle.
    godot::Ref<SteamInventoryChangeResult> outcome;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamInventorySnapshot::Freshness);
