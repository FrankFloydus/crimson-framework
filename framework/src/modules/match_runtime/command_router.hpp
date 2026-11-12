// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "possession.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <thread>

namespace crimson::match_runtime {

// Dispatches validated gameplay intents using trusted match player identities.
class CommandRouter : public godot::RefCounted {
    GDCLASS(CommandRouter, godot::RefCounted)

public:
    // Bind a match, its initial world epoch, and the possession reader.
    godot::Error configure(const godot::String &match_id, int64_t world_epoch,
            const godot::Ref<PlayerPossession> &possessor);
    // Open or close command admission at the authority's lifecycle boundary.
    godot::Error set_accepting_commands(bool enabled);
    // Set a newer world epoch, clear sequence cursors, and close command admission.
    godot::Error advance_world(int64_t world_epoch);
    // Clear a player's sequence cursor after trusted ingress revokes the old binding.
    godot::Error reset_player_sequence(const godot::String &player_id);
    // Register a command handler and whether its intent needs a primary character.
    godot::Error register_handler(const godot::String &command_type, const godot::Callable &handler,
            bool requires_primary = false);
    // Validate a trusted player's intent and invoke its registered handler once.
    godot::Dictionary submit(const godot::String &player_id, const godot::Dictionary &intent);

protected:
    // Expose configuration, registration, and submission to Godot.
    static void _bind_methods();

private:
    // Stable identity of the match targeted by commands.
    godot::String match_id;
    // Active world version used to reject commands for a previous scene.
    int64_t world_epoch = 0;
    // Possession service used to verify the character and assignment revision.
    godot::Ref<PlayerPossession> possessor;
    // Callable and primary-character requirement indexed by command type.
    godot::Dictionary handlers;
    // Highest dispatched command sequence for each active player stream.
    godot::Dictionary player_sequences;
    // Thread that owns this router and its Godot callbacks.
    const std::thread::id owner_thread = std::this_thread::get_id();
    // Authority gate that controls whether new intents can reach handlers.
    bool accepting_commands = false;
    // Whether a handler is running and reentrant calls must wait.
    bool dispatching = false;

    // Check the owner thread, dispatch guard, and match configuration.
    godot::Error check_configuration_call() const;
};

} // namespace crimson::match_runtime
