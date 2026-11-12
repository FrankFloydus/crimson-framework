// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/server/steam_sdr.hpp"

#include "modules/steam/lobbies/steam_lobbies.hpp"

#include <godot_cpp/variant/packed_string_array.hpp>

#include <charconv>
#include <limits>

#ifdef CRIMSON_WITH_STEAM
#include <steam_api.h>
#endif

#ifdef CRIMSON_WITH_STEAM_SDR
#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_runtime.hpp"

#include <steamdatagram_tickets.h>

#include <array>
#include <cstring>
#endif

namespace crimson::steam {
using namespace godot;
using session::SessionEndpoint;

namespace {

// The distinct hosted endpoint has an identity, a virtual port, and a registration nonce.
constexpr int32_t hosted_route_bytes = 19;
// Current binary hosted route version.
constexpr uint8_t hosted_route_version = 1;

// Parse bounded decimal values without accepting signs, spaces or partial matches.
bool parse_unsigned(const String &text, uint64_t &value) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString encoded = text.utf8();
    const char *first = encoded.get_data();
    const char *end = first + encoded.length();
    const auto parsed = std::from_chars(first, end, value);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

#ifdef CRIMSON_WITH_STEAM_SDR

// Match the exact fixed-width registration signed by the external game coordinator.
bool has_registration(const SteamDatagramRelayAuthTicket &ticket, uint64_t registration) {
    if (ticket.m_nExtraFields < 1 || ticket.m_nExtraFields > SteamDatagramRelayAuthTicket::k_nMaxExtraFields) {
        return false;
    }
    bool found = false;
    constexpr const char key[] = "crimson_registration";
    for (int index = 0; index < ticket.m_nExtraFields; ++index) {
        const auto &field = ticket.m_vecExtraFields[index];
        if (std::memcmp(field.m_szName, key, sizeof(key)) == 0) {
            if (found || field.m_eType != SteamDatagramRelayAuthTicket::ExtraField::k_EType_Fixed64 ||
                    field.m_nFixed64Value != registration) {
                return false;
            }
            found = true;
        }
    }
    return found;
}

// The relay ticket must name this client, app, certificate identity, virtual port and publication.
bool matches_ticket(SteamRuntime &runtime, const SteamHostedRoute &route, const SteamDatagramRelayAuthTicket &ticket) {
    auto *utils = runtime.get_utils();
    const uint32_t steam_time = utils ? utils->GetServerRealTime() : 0;
    return steam_time != 0 && ticket.m_rtimeTicketExpiry > steam_time && ticket.m_nAppID == runtime.get_app_id() &&
            ticket.m_identityAuthorizedClient.GetSteamID64() == runtime.get_user_id() &&
            ticket.m_identityGameserver.GetSteamID64() == route.server_id &&
            ticket.m_nRestrictToVirtualPort == route.virtual_port && has_registration(ticket, route.registration);
}

#endif

} // namespace

bool valid_steam_relay_profile(const StringName &name) {
    return String(name).length() <= 32 && valid_steam_lobby_namespace(String(name));
}

bool SteamHostedRoute::is_valid() const {
#ifdef CRIMSON_WITH_STEAM
    const CSteamID id(server_id);
    return registration != 0 && id.IsValid() &&
            (id.GetEAccountType() == k_EAccountTypeGameServer || id.GetEAccountType() == k_EAccountTypeAnonGameServer);
#else
    return false;
#endif
}

Ref<SessionEndpoint> SteamHostedRoute::endpoint() const {
    PackedByteArray route;
    if (!is_valid() || route.resize(hosted_route_bytes) != OK) {
        return {};
    }
    route[0] = hosted_route_version;
    route.encode_u64(1, server_id);
    route.encode_u16(9, virtual_port);
    route.encode_u64(11, registration);
    return SessionEndpoint::opaque("steam_hosted", route);
}

String SteamHostedRoute::listing_id(uint32_t app_id, const StringName &name_space) const {
    if (!is_valid() || app_id == 0 || !valid_steam_lobby_namespace(String(name_space)) || !valid_steam_relay_profile(profile)) {
        return {};
    }
    return String("h1:") + String::num_uint64(app_id) + ":" + String(name_space) + ":" + String(profile) + ":" +
            String::num_uint64(server_id) + ":" + String::num_uint64(virtual_port) + ":" + String::num_uint64(registration);
}

SteamHostedRoute SteamHostedRoute::from_endpoint(const Ref<SessionEndpoint> &endpoint) {
    if (endpoint.is_null() || endpoint->get_transport() != StringName("steam_hosted")) {
        return {};
    }
    const PackedByteArray bytes = endpoint->get_route();
    if (bytes.size() != hosted_route_bytes || bytes[0] != hosted_route_version) {
        return {};
    }
    const SteamHostedRoute route{static_cast<uint64_t>(bytes.decode_u64(1)),
        static_cast<uint16_t>(bytes.decode_u16(9)), static_cast<uint64_t>(bytes.decode_u64(11)), {}};
    return route.is_valid() ? route : SteamHostedRoute();
}

SteamHostedRoute SteamHostedRoute::from_listing(const String &id, uint32_t app_id, const StringName &name_space) {
    if (id.length() > 180 || app_id == 0 || !valid_steam_lobby_namespace(String(name_space))) {
        return {};
    }
    const auto fields = id.split(":", true);
    uint64_t app = 0;
    uint64_t server = 0;
    uint64_t port = 0;
    uint64_t nonce = 0;
    if (fields.size() != 7 || fields[0] != "h1" || !parse_unsigned(fields[1], app) || app != app_id ||
            fields[2] != String(name_space) || !valid_steam_relay_profile(StringName(fields[3])) ||
            !parse_unsigned(fields[4], server) || !parse_unsigned(fields[5], port) ||
            port > (std::numeric_limits<uint16_t>::max)() || !parse_unsigned(fields[6], nonce)) {
        return {};
    }
    const SteamHostedRoute route{server, static_cast<uint16_t>(port), nonce, StringName(fields[3])};
    return route.listing_id(app_id, name_space) == id ? route : SteamHostedRoute();
}

PackedByteArray steam_hosted_server_login(SteamServerRuntime &runtime, const String &listing, const StringName &world) {
#ifdef CRIMSON_WITH_STEAM_SDR
    if (!runtime.is_ready() || listing.is_empty() || !valid_steam_lobby_world(String(world))) {
        return {};
    }
    SteamDatagramGameCoordinatorServerLogin login{};
    const CharString app = (listing + String("\n") + String(world)).utf8();
    if (app.length() > static_cast<int64_t>(sizeof(login.m_appData))) {
        return {};
    }
    login.m_cbAppData = static_cast<int>(app.length());
    std::memcpy(login.m_appData, app.get_data(), login.m_cbAppData);
    std::array<uint8_t, k_cbMaxSteamDatagramGameCoordinatorServerLoginSerialized> bytes;
    int size = static_cast<int>(bytes.size());
    if (runtime.get_sockets()->GetGameCoordinatorServerLogin(&login, &size, bytes.data()) != k_EResultOK ||
            size <= 0 || static_cast<size_t>(size) > bytes.size() || login.m_nAppID != runtime.get_app_id() ||
            login.m_identity.GetSteamID64() != runtime.get_local_socket_id()) {
        return {};
    }
    PackedByteArray result;
    if (result.resize(size) != OK) {
        return {};
    }
    std::memcpy(result.ptrw(), bytes.data(), size);
    return result;
#else
    return {};
#endif
}

bool install_steam_relay_ticket(SteamRuntime &runtime, const SteamHostedRoute &route, const PackedByteArray &bytes) {
#ifdef CRIMSON_WITH_STEAM_SDR
    if (!route.is_valid() || !runtime.get_sockets() || bytes.is_empty() || bytes.size() > 4096) {
        return false;
    }
    SteamDatagramRelayAuthTicket parsed;
    return runtime.get_sockets()->ReceivedRelayAuthTicket(bytes.ptr(), static_cast<int>(bytes.size()), &parsed) &&
            matches_ticket(runtime, route, parsed) && has_steam_relay_ticket(runtime, route);
#else
    return false;
#endif
}

bool has_steam_relay_ticket(SteamRuntime &runtime, const SteamHostedRoute &route) {
#ifdef CRIMSON_WITH_STEAM_SDR
    auto *sockets = runtime.get_sockets();
    if (!route.is_valid() || !sockets) {
        return false;
    }
    SteamNetworkingIdentity server;
    server.Clear();
    server.SetSteamID64(route.server_id);
    SteamDatagramRelayAuthTicket parsed;
    return sockets->FindRelayAuthTicketForServer(server, route.virtual_port, &parsed) > 0 &&
            matches_ticket(runtime, route, parsed);
#else
    return false;
#endif
}

} // namespace crimson::steam
