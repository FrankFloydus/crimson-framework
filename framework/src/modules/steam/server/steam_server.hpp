// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::steam {

// Branchable configuration for an authenticated headless Steam authority.
class SteamServer : public godot::RefCounted {
    GDCLASS(SteamServer, godot::RefCounted)

public:
    // Select how clients obtain this server's direct endpoint.
    enum Advertisement {
        DIRECT, // Resolve an explicit authenticated route without Steam discovery.
        LAN, // Reserved for secure LAN-only advertisement, unavailable with the current SDK.
        INTERNET, // Explicitly enable Steam server advertisement.
    };

    // Set a display name and separate gameplay and Steam query UDP ports.
    static godot::Ref<SteamServer> configure(const godot::String &name = "Crimson Server",
            int64_t game_port = 27015, int64_t query_port = 27016);
    // Return a branch using the default certificate and Steam ticket admission policy.
    godot::Ref<SteamServer> secure() const;
    // Return a branch bound to one literal IPv4 interface, or wildcard "*".
    godot::Ref<SteamServer> bind_to(const godot::String &address) const;
    // Return a branch selecting explicit direct, LAN-only, or Internet discovery policy.
    godot::Ref<SteamServer> advertise_on(Advertisement scope) const;
    // Return a branch selecting an operator-supplied login environment variable.
    godot::Ref<SteamServer> with_login(const godot::StringName &secret_ref) const;
    // Return a branch with the game's master-server version, description, and install folder.
    godot::Ref<SteamServer> with_product_metadata(const godot::String &version,
            const godot::String &description, const godot::String &install_dir) const;
    // Return a branch selecting an operator-provisioned hosted SDR profile.
    godot::Ref<SteamServer> via_sdr(const godot::StringName &profile) const;

    // Validate all selections, including independent gameplay and query ports.
    bool is_valid() const;
    // Return the bounded Steam browser display name.
    godot::String get_server_name() const;
    // Return the host-order IPv4 interface, with zero meaning all interfaces.
    uint32_t get_bind_ipv4() const;
    // Return the game UDP listen port.
    uint16_t get_game_port() const;
    // Return the distinct Steam browser UDP query port.
    uint16_t get_query_port() const;
    // Return the explicit visibility policy.
    Advertisement get_advertisement() const;
    // Return the operator's environment key without resolving or disclosing its secret.
    godot::StringName get_login_ref() const;
    // Return the version advertised to Steam's master server.
    godot::String get_product_version() const;
    // Return the game's browser description.
    godot::String get_game_description() const;
    // Return the game's install folder name for Steam's browser metadata.
    godot::String get_install_dir() const;
    // Return the named hosted relay deployment, or empty for direct IP.
    godot::StringName get_relay_profile() const;

    // Parse a literal IPv4 without DNS, returning host-order octets.
    static bool parse_ipv4(const godot::String &text, uint32_t &address);

protected:
    // Register the detached configuration and advertisement enum.
    static void _bind_methods();

private:
    // Steam's server browser display name.
    godot::String server_name = "Crimson Server";
    // Host-order interface address, zero for wildcard.
    uint32_t bind_ipv4 = 0;
    // Steam Networking Sockets UDP port.
    uint16_t game_port = 27015;
    // Separate Valve-managed server query port.
    uint16_t query_port = 27016;
    // Direct secure authorities start without publication to Steam search.
    Advertisement advertisement = DIRECT;
    // Environment variable name containing an optional game server login token.
    godot::StringName login_ref;
    // Legacy master server version until the game supplies its own.
    godot::String product_version = "1.0.0.0";
    // Legacy browser game description until the game supplies its own.
    godot::String game_description = "Crimson";
    // Legacy game install folder until the game supplies its own.
    godot::String install_dir = "crimson";
    // Select a hosted dedicated relay deployment configured outside the game client.
    godot::StringName relay_profile;
    // Sticky validation result shared by each fluent branch.
    bool valid = true;

    // Duplicate detached settings without any native Steam server lease.
    godot::Ref<SteamServer> branch() const;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamServer::Advertisement);
