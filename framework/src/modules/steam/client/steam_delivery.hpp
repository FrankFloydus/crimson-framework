// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>

namespace crimson::steam {

// Shared Steam Sockets and Messages delivery choices.
class SteamDelivery : public godot::Object {
    GDCLASS(SteamDelivery, godot::Object)

public:
    // Transport delivery policy applied to one frame.
    enum Mode {
        RELIABLE, // Reliable ordered delivery under the native channel contract.
        UNRELIABLE, // Best-effort delivery for replaceable data.
    };

protected:
    // Register the two transport delivery choices.
    static void _bind_methods();
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamDelivery::Mode);
