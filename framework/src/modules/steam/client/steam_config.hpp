// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/string.hpp>

namespace crimson::steam {

// Serializable Steam settings shared by standalone and game-owned code-first boot.
class SteamConfig : public godot::Resource {
    GDCLASS(SteamConfig, godot::Resource)

    // Zero uses the launch AppID. Spacewar requires an explicit test group.
    int64_t app_id = 0;
    // Namespace isolating lobbies under the shared Spacewar AppID.
    godot::String test_group;
    // Permit public discovery for this game profile.
    bool discoverable = false;

protected:
    // Register the typed configuration and branch methods.
    static void _bind_methods();

public:
    // Create a development profile that requires AppID 480 and the selected test group.
    static godot::Ref<SteamConfig> spacewar(const godot::String &group);
    // Return an independent profile with public game discovery.
    godot::Ref<SteamConfig> public_games() const;
    // Validate AppID range and the Spacewar test-group contract.
    bool is_valid() const;
    // Set the AppID to verify at boot. Zero accepts the launch context.
    void set_app_id(int64_t value);
    // Return the configured expected AppID.
    int64_t get_app_id() const;
    // Assign the test group used by Spacewar games.
    void set_test_group(const godot::String &value);
    // Return the Spacewar test group.
    godot::String get_test_group() const;
    // Select whether game lobbies are publicly discoverable.
    void set_public(bool enabled);
    // Report whether the profile permits public game discovery.
    bool is_public() const;
};

} // namespace crimson::steam
