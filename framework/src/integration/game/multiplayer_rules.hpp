// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <cstdint>

#include <godot_cpp/classes/resource.hpp>

namespace crimson::integration {

// Session rules for a listen game, independent of the authored spawn placements.
class CrimsonMultiplayerRules : public godot::Resource {
    GDCLASS(CrimsonMultiplayerRules, godot::Resource)

    // Total player capacity, including the host.
    int64_t max_players = 4;
    // Whether guests may join once the host is already playing.
    bool join_in_progress = true;
    // UDP port used for listening and direct-address connections.
    int64_t port = 7000;

protected:
    // Bind the authoring properties and branchable configuration methods.
    static void _bind_methods();

public:
    // Create the default listen co-op rules.
    static godot::Ref<CrimsonMultiplayerRules> listen_coop();
    // Copy the rules for use in another definition or session.
    godot::Ref<CrimsonMultiplayerRules> copy_rules() const;
    // Return an independent rule set with the given total capacity.
    godot::Ref<CrimsonMultiplayerRules> with_max_players(int64_t value) const;
    // Return an independent rule set with the given admission policy.
    godot::Ref<CrimsonMultiplayerRules> allow_join_in_progress(bool enabled = true) const;
    // Return an independent rule set with the supplied listen port.
    godot::Ref<CrimsonMultiplayerRules> on_port(int64_t value) const;

    // Set the total player limit for future sessions.
    void set_max_players(int64_t value);
    // Get the total player limit, including the host.
    int64_t get_max_players() const;
    // Set whether an active game permits late admission.
    void set_join_in_progress(bool enabled);
    // Get the authored late-admission policy.
    bool get_join_in_progress() const;
    // Set the default UDP port for future sessions.
    void set_port(int64_t value);
    // Get the configured UDP port.
    int64_t get_port() const;
};

} // namespace crimson::integration
