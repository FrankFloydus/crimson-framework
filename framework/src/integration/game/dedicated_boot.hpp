// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "integration/game/game_operation.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object.hpp>

namespace crimson::integration {

class CrimsonGame;

// Starts a configured CrimsonGame as an explicit playerless authority from a headless launch.
class CrimsonDedicatedBoot : public godot::Node {
    GDCLASS(CrimsonDedicatedBoot, godot::Node)

    // Weak reference to the game authored in the dedicated boot scene.
    godot::ObjectID game_id;
    // Startup operation retained until its one terminal notification.
    godot::Ref<CrimsonGameOperation> starting;
    // Effective UDP port announced to the launcher after startup.
    int64_t port = 0;

    // Apply command-line overrides and start the server after the scene is ready.
    void start_server();
    // Announce readiness or exit with the native operation's error.
    void on_started(const godot::Dictionary &result);
    // Write the launcher error marker and exit with a failure status.
    void fail(const godot::String &message);

protected:
    // Register the typed same-scene game reference.
    static void _bind_methods();

public:
    // Queue startup only for the explicit server role.
    void _ready() override;
    // Disconnect pending startup feedback before the boot scene leaves the tree.
    void _exit_tree() override;
    // Set the game configured to own the dedicated match.
    void set_game(CrimsonGame *game);
    // Return the referenced game while it exists.
    CrimsonGame *get_game() const;
};

} // namespace crimson::integration
