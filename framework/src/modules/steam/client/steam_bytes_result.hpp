// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <functional>

namespace crimson::steam {

// Terminal outcome and detached byte payload from one Steam action.
class SteamBytesResult : public SteamResult {
    GDCLASS(SteamBytesResult, SteamResult)

protected:
    // Bind the read-only payload property.
    static void _bind_methods();

public:
    // Return the bytes on success, or an empty array on failure.
    godot::PackedByteArray get_value() const;

private:
    friend class SteamCloud;
    friend class SteamInventory;

    // Construct the final outcome before releasing it to callers.
    static godot::Ref<SteamBytesResult> create(godot::Error error, const godot::String &message = {},
            godot::PackedByteArray bytes = {});

    // Detached bytes copied from the completed Steam action.
    godot::PackedByteArray value;
};

// Retained observation of a Steam action with a typed byte result.
class SteamBytesOperation : public godot::RefCounted {
    GDCLASS(SteamBytesOperation, godot::RefCounted)

protected:
    // Register the retained operation and its typed completion signal.
    static void _bind_methods();

public:
    // Report whether the operation has a terminal result.
    bool is_finished() const;
    // Return the terminal result or null while observation is pending.
    godot::Ref<SteamBytesResult> result() const;
    // Return the settled result or the signal to await.
    godot::Variant wait();
    // Stop observing locally while Steam finishes submitted work.
    godot::Error cancel();

private:
    friend class SteamCloud;
    friend class SteamInventory;

    // Settle observation exactly once and emit completion after state is final.
    void finish(const godot::Ref<SteamBytesResult> &outcome);

    // Observer-only cancellation callback with a weak native record reference.
    std::function<godot::Error()> cancel_observation;
    // Terminal action status and detached bytes after the operation finishes.
    godot::Ref<SteamBytesResult> outcome;
};

} // namespace crimson::steam
