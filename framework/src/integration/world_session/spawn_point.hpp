// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/marker3d.hpp>

namespace crimson::integration {

// Marks an authored player placement in the active world.
class PlayerSpawnPoint : public godot::Marker3D {
    GDCLASS(PlayerSpawnPoint, godot::Marker3D)

protected:
    // Uses the inherited marker transform for authoring.
    static void _bind_methods() {}
};

} // namespace crimson::integration
