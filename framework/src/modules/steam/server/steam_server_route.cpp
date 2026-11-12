// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_server_route.hpp"

#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/variant/packed_string_array.hpp>

#ifdef CRIMSON_WITH_STEAM
#include <steam_api.h>
#endif

#include <charconv>
#include <limits>

namespace crimson::steam {
using namespace godot;
using session::SessionEndpoint;

namespace {

// Direct transport is separate from the existing Steam virtual-port route.
constexpr int32_t route_size = 15;
// Wire version checked before any binary fields are decoded.
constexpr uint8_t route_version = 1;

// Parse a canonical decimal field without treating user strings as floating-point values.
bool parse_decimal(const String &text, uint64_t &value) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString ascii = text.utf8();
    const char *first = ascii.get_data();
    const char *end = first + ascii.length();
    const auto parsed = std::from_chars(first, end, value);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

} // namespace

String steam_server_namespace_tag(const StringName &name_space) {
    return String("cns-") + String(name_space);
}

String steam_server_world_tag(const StringName &world) {
    return String("cw-") + String(world).sha256_text().substr(0, 16);
}

bool SteamServerRoute::is_valid() const {
#ifdef CRIMSON_WITH_STEAM
    const uint32_t first_octet = ipv4 >> 24;
    const CSteamID id(server_id);
    return first_octet > 0 && first_octet < 224 && port != 0 && id.IsValid() &&
            (id.GetEAccountType() == k_EAccountTypeGameServer || id.GetEAccountType() == k_EAccountTypeAnonGameServer);
#else
    return false;
#endif
}

Ref<SessionEndpoint> SteamServerRoute::endpoint() const {
    PackedByteArray bytes;
    if (!is_valid() || bytes.resize(route_size) != OK) {
        return {};
    }
    bytes[0] = route_version;
    bytes.encode_u64(1, server_id);
    bytes.encode_u16(9, port);
    bytes.encode_u32(11, ipv4);
    return SessionEndpoint::opaque("steam_server", bytes);
}

String SteamServerRoute::listing_id(uint32_t app_id, const StringName &name_space) const {
    if (!is_valid() || app_id == 0 || !valid_steam_lobby_namespace(String(name_space))) {
        return {};
    }
    return String("s1:") + String::num_uint64(app_id) + ":" + String(name_space) + ":" +
            String::num_uint64(server_id) + ":" + String::num_uint64(ipv4) + ":" + String::num_uint64(port);
}

SteamServerRoute SteamServerRoute::from_endpoint(const Ref<SessionEndpoint> &target) {
    if (target.is_null() || target->get_transport() != StringName("steam_server")) {
        return {};
    }
    const PackedByteArray bytes = target->get_route();
    if (bytes.size() != route_size || bytes[0] != route_version) {
        return {};
    }
    const SteamServerRoute route{static_cast<uint64_t>(bytes.decode_u64(1)),
        static_cast<uint32_t>(bytes.decode_u32(11)), static_cast<uint16_t>(bytes.decode_u16(9))};
    return route.is_valid() ? route : SteamServerRoute();
}

SteamServerRoute SteamServerRoute::from_listing(const String &id, uint32_t expected_app,
        const StringName &expected_namespace) {
    if (id.length() > 128) {
        return {};
    }
    const auto parts = id.split(":", true);
    uint64_t app = 0;
    uint64_t server = 0;
    uint64_t ipv4 = 0;
    uint64_t port = 0;
    if (parts.size() != 6 || parts[0] != "s1" || !parse_decimal(parts[1], app) || app != expected_app ||
            parts[2] != String(expected_namespace) || !parse_decimal(parts[3], server) ||
            !parse_decimal(parts[4], ipv4) || ipv4 > (std::numeric_limits<uint32_t>::max)() ||
            !parse_decimal(parts[5], port) || port > (std::numeric_limits<uint16_t>::max)()) {
        return {};
    }
    const SteamServerRoute route{server, static_cast<uint32_t>(ipv4), static_cast<uint16_t>(port)};
    return route.is_valid() ? route : SteamServerRoute();
}

} // namespace crimson::steam
