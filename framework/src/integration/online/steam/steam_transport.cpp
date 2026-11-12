// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_transport.hpp"
#include "modules/steam/client/steam_networking.hpp"

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_route.hpp"
#include "modules/steam/server/steam_server_runtime.hpp"
#include "modules/steam/server/steam_sdr.hpp"
#include <steam/isteamnetworkingsockets.h>
#endif

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <vector>

using namespace godot;
using crimson::session::SessionEndpoint;
using crimson::session::SessionTransportResult;

namespace crimson::integration {
using steam::SteamRuntime;
using steam::SteamServer;
using steam::SteamServerRuntime;
#ifdef CRIMSON_WITH_STEAM
using steam::SteamServerRoute;
using steam::SteamHostedRoute;
#endif
using steam::SteamSocketRuntime;

namespace {

constexpr int32_t authority_id = 1;
constexpr int32_t max_remote = 64;
constexpr int32_t max_channel = 84;
constexpr int32_t max_packet_size = 128 * 1024;

// A complete packet awaiting Godot, with sender derived from the physical socket.
struct ReceivedPacket {
    // SDK-bound Godot sender.
    int32_t peer;
    // Logical Godot channel.
    int32_t channel;
    // Logical Godot delivery mode.
    MultiplayerPeer::TransferMode mode;
    // Owned bytes copied from the released SDK message.
    std::vector<uint8_t> payload;
};

#ifdef CRIMSON_WITH_STEAM

constexpr int32_t max_virtual_port = 65535;
constexpr size_t max_inbox_packets = 256;
constexpr size_t max_inbox_bytes = 2 * 1024 * 1024;
constexpr size_t receive_per_poll = 64;
constexpr uint64_t connect_timeout_ms = 12000;
constexpr uint64_t assignment_timeout_ms = 5000;
constexpr int32_t secure_mask = k_nSteamNetworkConnectionInfoFlags_Unauthenticated |
        k_nSteamNetworkConnectionInfoFlags_Unencrypted;
constexpr uint8_t wire_version = 1;
constexpr uint8_t route_version = 1;
constexpr int32_t route_size = 11;
constexpr size_t frame_size = 8;

// Local token identifies a native connection lifetime even when Steam recycles a handle.
int64_t next_socket_token = 0;

enum class Frame : uint8_t { WELCOME = 1, READY = 2, DATA = 3 };
enum class ConnectionStage { CONNECTING, WAIT_WELCOME, WAIT_READY, READY };

// Check the SDK's canonical individual Steam account format.
bool valid_user(uint64_t id) {
    const CSteamID account(id);
    return account.IsValid() && account.GetEAccountType() == k_EAccountTypeIndividual;
}

// Check the two Steam account types that can identify a certified dedicated server.
bool valid_server(uint64_t id) {
    const CSteamID account(id);
    return account.IsValid() && (account.GetEAccountType() == k_EAccountTypeGameServer ||
                                        account.GetEAccountType() == k_EAccountTypeAnonGameServer);
}

// Disallow wildcard, broadcast and multicast addresses as connection targets.
bool valid_direct_ipv4(uint32_t address) {
    const uint32_t first_octet = address >> 24;
    return first_octet > 0 && first_octet < 224;
}

// Validate a Steam virtual port, including Valve's default port zero.
bool valid_virtual_port(int32_t port) {
    return port >= 0 && port <= max_virtual_port;
}

// Encode the little-endian control value or ordered sequence in a native header.
std::array<uint8_t, frame_size> make_header(Frame kind, int32_t channel, MultiplayerPeer::TransferMode mode,
        uint32_t value) {
    return { wire_version, static_cast<uint8_t>(kind), static_cast<uint8_t>(channel), static_cast<uint8_t>(mode),
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 24) };
}

// Read the fixed four-byte value after the validated native header prefix.
uint32_t frame_value(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[4]) | (static_cast<uint32_t>(bytes[5]) << 8) |
            (static_cast<uint32_t>(bytes[6]) << 16) | (static_cast<uint32_t>(bytes[7]) << 24);
}

// Preserve recent unreliable-ordered delivery across a uint32 sequence wrap.
bool is_newer(uint32_t sequence, uint32_t last) {
    return sequence != 0 && (last == 0 || (sequence - last != 0 && sequence - last < 0x80000000u));
}

// Decode only an exact versioned Steam endpoint with a valid individual authority account.
bool parse_route(const Ref<SessionEndpoint> &endpoint, uint64_t &owner, int32_t &port) {
    if (endpoint.is_null() || endpoint->get_transport() != StringName("steam")) {
        return false;
    }
    const PackedByteArray bytes = endpoint->get_route();
    if (bytes.size() != route_size || bytes[0] != route_version) {
        return false;
    }
    owner = 0;
    for (int32_t index = 0; index < 8; ++index) {
        owner |= static_cast<uint64_t>(bytes[index + 1]) << (index * 8);
    }
    port = static_cast<int32_t>(bytes[9]) | (static_cast<int32_t>(bytes[10]) << 8);
    return valid_user(owner);
}

// Translate SDK backpressure and connection errors to their Godot equivalents.
Error send_result(EResult result, bool reliable) {
    switch (result) {
        case k_EResultOK:
            return OK;
        case k_EResultIgnored:
            return reliable ? ERR_CONNECTION_ERROR : OK;
        case k_EResultLimitExceeded:
            return reliable ? ERR_BUSY : OK;
        case k_EResultNoConnection:
        case k_EResultInvalidState:
            return ERR_CONNECTION_ERROR;
        case k_EResultInvalidParam:
            return ERR_INVALID_PARAMETER;
        default:
            return FAILED;
    }
}

// Always return received message storage to the SDK, including after a reentrant close.
void release_message(SteamNetworkingMessage_t *message) {
    if (message) {
        message->Release();
    }
}

#endif

} // namespace

struct SteamPeer::State {
    // Local Godot connection state exposed by the extension.
    ConnectionStatus status = CONNECTION_DISCONNECTED;
    // Invalidates the rest of a poll when a signal listener closes the session.
    uint64_t generation = 0;
    // Whether the local process owns the authority listener.
    bool server = false;
    // Whether new physical clients must be refused.
    bool refusing = false;
    // Local Godot ID after authority setup or guest assignment.
    int32_t local_id = 0;
    // Outgoing logical channel selected by Godot.
    int32_t channel = 0;
    // Outgoing Godot target selector.
    int32_t target = 0;
    // Outgoing reliability selected by Godot.
    TransferMode mode = TRANSFER_MODE_RELIABLE;
    // Fully received Godot packets in delivery order.
    std::deque<ReceivedPacket> inbox;
    // Storage backing the packet pointer returned by the most recent read.
    std::vector<uint8_t> last_packet;
    // Aggregate bytes awaiting Godot reads.
    size_t inbox_bytes = 0;

#ifdef CRIMSON_WITH_STEAM
    // One SDK connection and the authority assignment made over its secure channel.
    struct Connection {
        // Local SDK userdata token for the current handle lifetime.
        int64_t token = 0;
        // Absolute deadline for Steam setup or native ID assignment.
        uint64_t deadline = 0;
        // Stage required before reading application packets or exposing the identity.
        ConnectionStage stage = ConnectionStage::CONNECTING;
        // Secure remote Steam ID from Steam Networking Sockets.
        uint64_t steam_id = 0;
        // Physical Godot peer ID assigned by the authority.
        int32_t peer_id = 0;
        // Last received unreliable-ordered sequence for each logical channel.
        std::array<uint32_t, max_channel + 1> received_sequence{};
    };

    // Keeps the centrally pumped Steam API alive until sockets and subscriptions are released.
    std::shared_ptr<SteamSocketRuntime> runtime;
    // Callback subscription owned by this peer.
    uint64_t subscription = 0;
    // Steam authority listener, invalid on guests.
    HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
    // Receives this peer's messages without sharing a queue with another game scope.
    HSteamNetPollGroup poll_group = k_HSteamNetPollGroup_Invalid;
    // Handle to the selected Steam authority on a guest.
    HSteamNetConnection authority_connection = k_HSteamNetConnection_Invalid;
    // Steam ID authenticated by the resolved client route.
    uint64_t expected_authority = 0;
    // Direct or hosted dedicated clients require a certified GameServer account.
    bool dedicated_route = false;
    // Number of pending and ready guest sockets permitted by this host.
    int32_t remote_limit = 0;
    // Next unused guest ID for the lifetime of this listener.
    int64_t next_peer_id = authority_id + 1;
    // Native connection state indexed by the Steam handle, never a client-supplied ID.
    std::map<HSteamNetConnection, Connection> connections;
    // Ready peers retired since the last Godot poll.
    std::vector<int32_t> disconnected;
    // Outgoing sequence for each logical unreliable-ordered channel.
    std::array<uint32_t, max_channel + 1> sent_sequence{};

    // Resolve the current SDK interface without keeping a pointer past runtime shutdown.
    ISteamNetworkingSockets *sockets() const;
    // Find a live physical connection by its assigned Godot ID.
    HSteamNetConnection find_ready(int32_t peer_id) const;
    // Claim a Steam handle, install a lifetime token, and attach it to this poll group.
    bool attach(HSteamNetConnection handle);
    // Dispatch an owned live Steam status without publishing Godot signals from the SDK callback.
    void on_status(const SteamNetConnectionStatusChangedCallback_t &event);
    // Validate the Steam certificate-backed identity and enter native ID assignment.
    void connected(HSteamNetConnection handle, Connection &connection, const SteamNetConnectionInfo_t &info);
    // Queue a validated Steam message or advance native ID assignment.
    void receive(SteamPeer &peer, const SteamNetworkingMessage_t &message);
    // Send a fixed reliable WELCOME or READY assignment frame.
    Error send_assignment(HSteamNetConnection handle, Frame kind, int32_t peer_id) const;
    // Release one Steam handle and remove pending input and identity for that connection.
    void retire(HSteamNetConnection handle, bool notify, bool linger);
#endif
};

#ifdef CRIMSON_WITH_STEAM

ISteamNetworkingSockets *SteamPeer::State::sockets() const {
    return runtime ? runtime->get_sockets() : nullptr;
}

HSteamNetConnection SteamPeer::State::find_ready(int32_t peer_id) const {
    for (const auto &[handle, connection] : connections) {
        if (connection.stage == ConnectionStage::READY && connection.peer_id == peer_id) {
            return handle;
        }
    }
    return k_HSteamNetConnection_Invalid;
}

bool SteamPeer::State::attach(HSteamNetConnection handle) {
    ISteamNetworkingSockets *sdk = sockets();
    if (!sdk || handle == k_HSteamNetConnection_Invalid || next_socket_token == std::numeric_limits<int64_t>::max()) {
        return false;
    }
    const int64_t token = ++next_socket_token;
    if (!sdk->SetConnectionUserData(handle, token) || !sdk->SetConnectionPollGroup(handle, poll_group)) {
        return false;
    }
    Connection connection;
    connection.token = token;
    connection.deadline = Time::get_singleton()->get_ticks_msec() + connect_timeout_ms;
    return connections.emplace(handle, connection).second;
}

void SteamPeer::State::on_status(const SteamNetConnectionStatusChangedCallback_t &event) {
    ISteamNetworkingSockets *sdk = sockets();
    const HSteamNetConnection handle = event.m_hConn;
    auto found = connections.find(handle);
    const bool known = found != connections.end();
    if (!sdk || (!known && (!server || listener == k_HSteamListenSocket_Invalid ||
            event.m_info.m_hListenSocket != listener || event.m_info.m_eState != k_ESteamNetworkingConnectionState_Connecting))) {
        return;
    }

    SteamNetConnectionInfo_t info{};
    if (!sdk->GetConnectionInfo(handle, &info)) {
        if (known) {
            retire(handle, true, false);
        }
        return;
    }
    if (!known) {
        if (info.m_hListenSocket != listener) {
            return;
        }
        if (info.m_eState != k_ESteamNetworkingConnectionState_Connecting || refusing ||
                connections.size() >= static_cast<size_t>(remote_limit) || !attach(handle)) {
            sdk->CloseConnection(handle, 0, nullptr, false);
            return;
        }
        if (sdk->AcceptConnection(handle) != k_EResultOK) {
            retire(handle, false, false);
        }
        return;
    }

    Connection &connection = found->second;
    if (info.m_nUserData != connection.token) {
        return;
    }
    switch (info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connected:
            if (connection.stage == ConnectionStage::CONNECTING) {
                connected(handle, connection, info);
            }
            break;
        case k_ESteamNetworkingConnectionState_Connecting:
        case k_ESteamNetworkingConnectionState_FindingRoute:
            break;
        default:
            retire(handle, true, false);
            break;
    }
}

void SteamPeer::State::connected(HSteamNetConnection handle, Connection &connection,
        const SteamNetConnectionInfo_t &info) {
    const uint64_t identity = info.m_identityRemote.GetSteamID64();
    const bool allowed_identity = !server && dedicated_route ? valid_server(identity) : valid_user(identity);
    if ((info.m_nFlags & secure_mask) != 0 || !allowed_identity || identity == runtime->get_local_socket_id() ||
            (!server && identity != expected_authority)) {
        retire(handle, false, false);
        return;
    }
    if (server) {
        const bool duplicate = std::any_of(connections.begin(), connections.end(), [identity](const auto &entry) {
            return entry.second.steam_id == identity;
        });
        if (refusing || duplicate || next_peer_id > std::numeric_limits<int32_t>::max()) {
            retire(handle, false, false);
            return;
        }
    }

    connection.steam_id = identity;
    connection.deadline = Time::get_singleton()->get_ticks_msec() + assignment_timeout_ms;
    if (!server) {
        connection.peer_id = authority_id;
        connection.stage = ConnectionStage::WAIT_WELCOME;
        return;
    }
    connection.peer_id = static_cast<int32_t>(next_peer_id++);
    connection.stage = ConnectionStage::WAIT_READY;
    if (send_assignment(handle, Frame::WELCOME, connection.peer_id) != OK) {
        retire(handle, false, false);
    }
}

Error SteamPeer::State::send_assignment(HSteamNetConnection handle, Frame kind, int32_t peer_id) const {
    ISteamNetworkingSockets *sdk = sockets();
    if (!sdk) {
        return ERR_UNAVAILABLE;
    }
    const auto frame = make_header(kind, 0, TRANSFER_MODE_RELIABLE, static_cast<uint32_t>(peer_id));
    return send_result(sdk->SendMessageToConnection(handle, frame.data(), static_cast<uint32_t>(frame.size()),
            k_nSteamNetworkingSend_ReliableNoNagle, nullptr), true);
}

void SteamPeer::State::receive(SteamPeer &peer, const SteamNetworkingMessage_t &message) {
    const HSteamNetConnection handle = message.m_conn;
    auto found = connections.find(handle);
    if (found == connections.end() || found->second.token != message.m_nConnUserData) {
        return;
    }
    if (found->second.stage == ConnectionStage::CONNECTING) {
        SteamNetConnectionInfo_t info{};
        ISteamNetworkingSockets *sdk = sockets();
        if (!sdk || !sdk->GetConnectionInfo(handle, &info) || info.m_nUserData != found->second.token ||
                info.m_eState != k_ESteamNetworkingConnectionState_Connected) {
            return;
        }
        connected(handle, found->second, info);
        found = connections.find(handle);
        if (found == connections.end()) {
            return;
        }
    }

    Connection &connection = found->second;
    const auto *bytes = static_cast<const uint8_t *>(message.m_pData);
    if (connection.steam_id != message.m_identityPeer.GetSteamID64() || !bytes ||
            message.m_cbSize < static_cast<int32_t>(frame_size) ||
            message.m_cbSize > max_packet_size + static_cast<int32_t>(frame_size) || bytes[0] != wire_version ||
            bytes[3] > static_cast<uint8_t>(TRANSFER_MODE_RELIABLE)) {
        retire(handle, true, false);
        return;
    }

    const auto kind = static_cast<Frame>(bytes[1]);
    const int32_t received_channel = bytes[2];
    const auto received_mode = static_cast<TransferMode>(bytes[3]);
    const uint32_t value = frame_value(bytes);
    const bool reliable = (message.m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
    if (connection.stage != ConnectionStage::READY) {
        const bool assignment = reliable && received_channel == 0 && received_mode == TRANSFER_MODE_RELIABLE &&
                message.m_cbSize == static_cast<int32_t>(frame_size) && value > authority_id &&
                value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
        if (!assignment) {
            retire(handle, false, false);
            return;
        }
        if (connection.stage == ConnectionStage::WAIT_WELCOME && kind == Frame::WELCOME) {
            if (send_assignment(handle, Frame::READY, static_cast<int32_t>(value)) != OK) {
                retire(handle, false, false);
                return;
            }
            local_id = static_cast<int32_t>(value);
            status = CONNECTION_CONNECTED;
        } else if (connection.stage != ConnectionStage::WAIT_READY || kind != Frame::READY || refusing ||
                value != static_cast<uint32_t>(connection.peer_id)) {
            retire(handle, false, false);
            return;
        }
        connection.stage = ConnectionStage::READY;
        connection.deadline = 0;
        peer.emit_signal("peer_connected", connection.peer_id);
        return;
    }

    const size_t payload_size = static_cast<size_t>(message.m_cbSize) - frame_size;
    if (kind != Frame::DATA || payload_size == 0 || received_channel > max_channel ||
            reliable != (received_mode == TRANSFER_MODE_RELIABLE) ||
            (received_mode != TRANSFER_MODE_UNRELIABLE_ORDERED && value != 0)) {
        retire(handle, true, false);
        return;
    }
    if (received_mode == TRANSFER_MODE_UNRELIABLE_ORDERED) {
        uint32_t &last = connection.received_sequence[received_channel];
        if (!is_newer(value, last)) {
            return;
        }
        last = value;
    }
    if (inbox.size() >= max_inbox_packets || payload_size > max_inbox_bytes - inbox_bytes) {
        if (reliable) {
            retire(handle, true, false);
        }
        return;
    }
    inbox.push_back(ReceivedPacket{ connection.peer_id, received_channel, received_mode,
            std::vector<uint8_t>(bytes + frame_size, bytes + message.m_cbSize) });
    inbox_bytes += payload_size;
}

void SteamPeer::State::retire(HSteamNetConnection handle, bool notify, bool linger) {
    const auto found = connections.find(handle);
    if (found == connections.end()) {
        return;
    }
    const int32_t peer_id = found->second.peer_id;
    const bool ready = found->second.stage == ConnectionStage::READY;
    connections.erase(found);
    if (ready) {
        for (auto packet = inbox.begin(); packet != inbox.end();) {
            if (packet->peer == peer_id) {
                inbox_bytes -= packet->payload.size();
                packet = inbox.erase(packet);
            } else {
                ++packet;
            }
        }
        if (notify && server) {
            disconnected.push_back(peer_id);
        }
    }
    if (handle == authority_connection) {
        authority_connection = k_HSteamNetConnection_Invalid;
        status = CONNECTION_DISCONNECTED;
        local_id = 0;
    }
    if (ISteamNetworkingSockets *sdk = sockets()) {
        sdk->CloseConnection(handle, 0, nullptr, linger);
    }
}

#endif

SteamPeer::SteamPeer() : state(std::make_unique<State>()) {}

SteamPeer::~SteamPeer() {
    _close();
}

void SteamPeer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_remote_steam_id", "peer_id"), &SteamPeer::get_remote_steam_id);
}

Error SteamPeer::open(const std::shared_ptr<SteamSocketRuntime> &runtime, int32_t port,
        int32_t remote_limit, uint64_t expected_authority, std::optional<uint32_t> direct_ipv4, bool hosted,
        bool prefer_relay) {
#ifdef CRIMSON_WITH_STEAM
#ifndef CRIMSON_WITH_STEAM_SDR
    if (hosted) {
        return ERR_UNAVAILABLE;
    }
#endif
    if (state->runtime) {
        return ERR_ALREADY_IN_USE;
    }
    const bool host = expected_authority == 0;
    const bool direct = direct_ipv4.has_value();
    const bool dedicated = direct || hosted;
    if (!valid_virtual_port(port) || (direct && (port == 0 || hosted)) ||
            (host && (remote_limit < 1 || remote_limit > max_remote)) ||
            (!host && (dedicated ? !valid_server(expected_authority) : !valid_user(expected_authority))) ||
            (!host && direct && !valid_direct_ipv4(*direct_ipv4))) {
        return ERR_INVALID_PARAMETER;
    }
    ISteamNetworkingSockets *sdk = runtime ? runtime->get_sockets() : nullptr;
    const uint64_t local = sdk ? runtime->get_local_socket_id() : 0;
    if (!sdk || (host && dedicated ? local != 0 && !valid_server(local) : !valid_user(local))) {
        return ERR_UNCONFIGURED;
    }
    if (!host && expected_authority == local) {
        return ERR_INVALID_PARAMETER;
    }

    state->runtime = runtime;
    state->server = host;
    state->dedicated_route = dedicated;
    state->expected_authority = expected_authority;
    state->remote_limit = remote_limit;
    state->poll_group = sdk->CreatePollGroup();
    if (state->poll_group == k_HSteamNetPollGroup_Invalid) {
        _close();
        return ERR_CANT_CREATE;
    }

    const ObjectID id(get_instance_id());
    const uint64_t generation = ++state->generation;
    state->subscription = runtime->subscribe(SteamNetConnectionStatusChangedCallback_t::k_iCallback,
            [id, generation](const void *bytes, size_t size) {
        auto *peer = Object::cast_to<SteamPeer>(ObjectDB::get_instance(id));
        if (!peer || peer->state->generation != generation || !bytes ||
                size != sizeof(SteamNetConnectionStatusChangedCallback_t)) {
            return;
        }
        SteamNetConnectionStatusChangedCallback_t event{};
        std::memcpy(&event, bytes, sizeof(event));
        peer->state->on_status(event);
    });
    if (state->subscription == 0) {
        _close();
        return ERR_CANT_CREATE;
    }

    SteamNetworkingConfigValue_t security[3];
    security[0].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
    SteamNetworkingIPAddr address;
    address.Clear();
    if (direct) {
        security[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 0);
        security[2].SetInt32(k_ESteamNetworkingConfig_IPLocalHost_AllowWithoutAuth, 0);
        address.SetIPv4(*direct_ipv4, static_cast<uint16_t>(port));
    }
    if (state->server) {
        int p2p_options = 1;
        if (!dedicated && prefer_relay) {
            // Steam scores usable routes in milliseconds. Bias ICE without disabling its fallback.
            constexpr int32_t relay_preference_ms = 500;
            security[p2p_options++].SetInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Penalty, relay_preference_ms);
        }
        if (hosted) {
            state->listener = sdk->CreateHostedDedicatedServerListenSocket(port, 1, security);
        } else {
            state->listener = direct ? sdk->CreateListenSocketIP(address, 3, security) :
                                       sdk->CreateListenSocketP2P(port, p2p_options, security);
        }
        if (state->listener == k_HSteamListenSocket_Invalid) {
            _close();
            return ERR_CANT_CREATE;
        }
        state->local_id = authority_id;
        state->status = CONNECTION_CONNECTED;
        return OK;
    }

    SteamNetworkingIdentity remote;
    remote.Clear();
    remote.SetSteamID64(expected_authority);
    HSteamNetConnection handle = k_HSteamNetConnection_Invalid;
    if (hosted) {
        handle = sdk->ConnectToHostedDedicatedServer(remote, port, 1, security);
    } else {
        handle = direct ? sdk->ConnectByIPAddress(address, 3, security) : sdk->ConnectP2P(remote, port, 1, security);
    }
    if (handle == k_HSteamNetConnection_Invalid) {
        _close();
        return ERR_CANT_CONNECT;
    }
    if (!state->attach(handle)) {
        sdk->CloseConnection(handle, 0, nullptr, false);
        _close();
        return ERR_CANT_CONNECT;
    }
    state->authority_connection = handle;
    state->status = CONNECTION_CONNECTING;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

uint64_t SteamPeer::get_remote_steam_id(int32_t peer_id) const {
#ifdef CRIMSON_WITH_STEAM
    ISteamNetworkingSockets *sdk = state->sockets();
    const HSteamNetConnection handle = state->find_ready(peer_id);
    if (!sdk || handle == k_HSteamNetConnection_Invalid) {
        return 0;
    }
    const State::Connection &connection = state->connections.at(handle);
    SteamNetConnectionInfo_t info{};
    if (sdk->GetConnectionInfo(handle, &info) && info.m_eState == k_ESteamNetworkingConnectionState_Connected &&
            info.m_nUserData == connection.token && (info.m_nFlags & secure_mask) == 0 &&
            info.m_identityRemote.GetSteamID64() == connection.steam_id) {
        return connection.steam_id;
    }
#endif
    return 0;
}

void SteamPeer::disconnect_after_flush(int32_t peer_id) {
#ifdef CRIMSON_WITH_STEAM
    if (state->server) {
        state->retire(state->find_ready(peer_id), true, true);
    }
#endif
}

Error SteamPeer::_get_packet(const uint8_t **buffer, int32_t *size) {
    if (state->inbox.empty()) {
        return ERR_UNAVAILABLE;
    }
    state->last_packet = std::move(state->inbox.front().payload);
    state->inbox.pop_front();
    state->inbox_bytes -= state->last_packet.size();
    *buffer = state->last_packet.data();
    *size = static_cast<int32_t>(state->last_packet.size());
    return OK;
}

Error SteamPeer::_put_packet(const uint8_t *buffer, int32_t size) {
    if (!buffer || size <= 0 || size > max_packet_size || state->channel < 0 || state->channel > max_channel ||
            state->mode < TRANSFER_MODE_UNRELIABLE || state->mode > TRANSFER_MODE_RELIABLE) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    ISteamNetworkingSockets *sdk = state->sockets();
    if (!sdk || state->status != CONNECTION_CONNECTED) {
        return ERR_UNCONFIGURED;
    }
    if (state->target > 0 && state->find_ready(state->target) == k_HSteamNetConnection_Invalid) {
        return ERR_DOES_NOT_EXIST;
    }
    uint32_t sequence = 0;
    if (state->mode == TRANSFER_MODE_UNRELIABLE_ORDERED) {
        uint32_t &sent = state->sent_sequence[state->channel];
        if (++sent == 0) {
            ++sent;
        }
        sequence = sent;
    }
    const auto header = make_header(Frame::DATA, state->channel, state->mode, sequence);
    std::vector<uint8_t> frame(frame_size + static_cast<size_t>(size));
    std::copy(header.begin(), header.end(), frame.begin());
    std::copy_n(buffer, size, frame.begin() + frame_size);
    const bool reliable = state->mode == TRANSFER_MODE_RELIABLE;
    const int32_t flags = reliable ? k_nSteamNetworkingSend_ReliableNoNagle : k_nSteamNetworkingSend_UnreliableNoDelay;
    Error result = OK;
    for (const auto &[handle, connection] : state->connections) {
        if (connection.stage != ConnectionStage::READY || (state->target > 0 && state->target != connection.peer_id) ||
                (state->target < 0 && -static_cast<int64_t>(state->target) == connection.peer_id)) {
            continue;
        }
        const Error sent = send_result(sdk->SendMessageToConnection(handle, frame.data(),
                static_cast<uint32_t>(frame.size()), flags, nullptr), reliable);
        if (result == OK) {
            result = sent;
        }
    }
    return result;
#else
    return ERR_UNAVAILABLE;
#endif
}

int32_t SteamPeer::_get_available_packet_count() const {
    return static_cast<int32_t>(state->inbox.size());
}

int32_t SteamPeer::_get_max_packet_size() const {
    return max_packet_size;
}

int32_t SteamPeer::_get_packet_channel() const {
    return state->inbox.empty() ? 0 : state->inbox.front().channel;
}

SteamPeer::TransferMode SteamPeer::_get_packet_mode() const {
    return state->inbox.empty() ? TRANSFER_MODE_UNRELIABLE : state->inbox.front().mode;
}

void SteamPeer::_set_transfer_channel(int32_t channel) {
    state->channel = channel;
}

int32_t SteamPeer::_get_transfer_channel() const {
    return state->channel;
}

void SteamPeer::_set_transfer_mode(TransferMode mode) {
    state->mode = mode;
}

SteamPeer::TransferMode SteamPeer::_get_transfer_mode() const {
    return state->mode;
}

void SteamPeer::_set_target_peer(int32_t peer_id) {
    state->target = peer_id;
}

int32_t SteamPeer::_get_packet_peer() const {
    return state->inbox.empty() ? 0 : state->inbox.front().peer;
}

bool SteamPeer::_is_server() const {
    return state->server;
}

void SteamPeer::_poll() {
#ifdef CRIMSON_WITH_STEAM
    if (state->status == CONNECTION_DISCONNECTED) {
        return;
    }
    ISteamNetworkingSockets *sdk = state->sockets();
    if (!sdk) {
        _close();
        return;
    }
    const Ref<SteamPeer> keep_alive(this);
    // Reentrant Godot signals can close this peer while SDK messages still need release.
    const auto keep_runtime = state->runtime;
    const uint64_t generation = state->generation;
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    std::array<HSteamNetConnection, max_remote> expired;
    size_t expired_count = 0;
    for (const auto &[handle, connection] : state->connections) {
        if (connection.deadline != 0 && now >= connection.deadline) {
            expired[expired_count++] = handle;
        }
    }
    for (size_t index = 0; index < expired_count; ++index) {
        state->retire(expired[index], false, false);
    }
    if (state->status == CONNECTION_DISCONNECTED) {
        return;
    }

    std::vector<int32_t> disconnected;
    disconnected.swap(state->disconnected);
    for (int32_t peer_id : disconnected) {
        emit_signal("peer_disconnected", peer_id);
        if (state->generation != generation) {
            return;
        }
    }

    std::array<SteamNetworkingMessage_t *, receive_per_poll> messages{};
    const int32_t count = sdk->ReceiveMessagesOnPollGroup(state->poll_group, messages.data(),
            static_cast<int32_t>(messages.size()));
    if (count < 0) {
        _close();
        return;
    }
    for (int32_t index = 0; index < count; ++index) {
        const std::unique_ptr<SteamNetworkingMessage_t, decltype(&release_message)> message(messages[index], release_message);
        if (message && state->generation == generation && state->status != CONNECTION_DISCONNECTED) {
            state->receive(*this, *message);
        }
    }
#endif
}

void SteamPeer::_close() {
    const uint64_t generation = state->generation + 1;
#ifdef CRIMSON_WITH_STEAM
    if (state->runtime && state->subscription != 0) {
        state->runtime->unsubscribe(state->subscription);
    }
    if (ISteamNetworkingSockets *sdk = state->sockets()) {
        for (const auto &[handle, connection] : state->connections) {
            sdk->CloseConnection(handle, 0, nullptr, false);
        }
        if (state->listener != k_HSteamListenSocket_Invalid) {
            sdk->CloseListenSocket(state->listener);
        }
        if (state->poll_group != k_HSteamNetPollGroup_Invalid) {
            sdk->DestroyPollGroup(state->poll_group);
        }
    }
#endif
    *state = State{};
    state->generation = generation;
}

void SteamPeer::_disconnect_peer(int32_t peer_id, bool force) {
#ifdef CRIMSON_WITH_STEAM
    state->retire(state->find_ready(peer_id), !force, false);
#endif
}

int32_t SteamPeer::_get_unique_id() const {
    return state->local_id;
}

void SteamPeer::_set_refuse_new_connections(bool enabled) {
    state->refusing = enabled;
}

bool SteamPeer::_is_refusing_new_connections() const {
    return state->refusing;
}

bool SteamPeer::_is_server_relay_supported() const {
    return true;
}

SteamPeer::ConnectionStatus SteamPeer::_get_connection_status() const {
    return state->status;
}

void SteamSessionTransport::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("route", "owner_id", "virtual_port"),
            &SteamSessionTransport::route, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_host_endpoint"), &SteamSessionTransport::get_host_endpoint);
    ClassDB::bind_method(D_METHOD("get_host_remote_limit"), &SteamSessionTransport::get_host_remote_limit);
    ClassDB::bind_method(D_METHOD("get_remote_steam_id", "peer_id"), &SteamSessionTransport::get_remote_steam_id);
    ClassDB::bind_method(D_METHOD("open_server", "port", "remote_limit", "bind_address"),
            &SteamSessionTransport::open_server);
    ClassDB::bind_method(D_METHOD("open_client", "endpoint"), &SteamSessionTransport::open_client);
}

Error SteamSessionTransport::configure(const std::shared_ptr<SteamRuntime> &p_runtime, int32_t p_virtual_port,
        bool prefer_relay) {
#ifdef CRIMSON_WITH_STEAM
    if (get_active_peer()) {
        return ERR_BUSY;
    }
    if (!valid_virtual_port(p_virtual_port)) {
        return ERR_INVALID_PARAMETER;
    }
    if (!p_runtime || !p_runtime->get_sockets() || !valid_user(p_runtime->get_user_id())) {
        return ERR_UNCONFIGURED;
    }
    p_runtime->warm_relay();
    runtime = p_runtime;
    server_runtime.reset();
    hosted_relay = false;
    hosted_virtual_port = 0;
    hosted_endpoint.unref();
    hosted_client_endpoint.unref();
    virtual_port = p_virtual_port;
    prefer_relay_routes = prefer_relay;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SteamSessionTransport::configure_dedicated(const std::shared_ptr<SteamServerRuntime> &selected,
        const Ref<SteamServer> &settings, int32_t relay_virtual_port) {
#ifdef CRIMSON_WITH_STEAM
    if (get_active_peer()) {
        return ERR_BUSY;
    }
    if (!selected || !selected->get_sockets() || settings.is_null() || !settings->is_valid() ||
            !valid_virtual_port(relay_virtual_port)) {
        return ERR_UNCONFIGURED;
    }
    runtime.reset();
    prefer_relay_routes = false;
    server_runtime = selected;
    dedicated_ipv4 = settings->get_bind_ipv4();
    dedicated_port = settings->get_game_port();
    hosted_relay = !settings->get_relay_profile().is_empty();
    hosted_virtual_port = relay_virtual_port;
    hosted_endpoint.unref();
    hosted_client_endpoint.unref();
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

bool SteamSessionTransport::retire_client_runtime() {
#ifdef CRIMSON_WITH_STEAM
    if (!runtime) {
        return false;
    }
    const Ref<SteamPeer> peer(get_active_peer());
    active_peer = ObjectID();
    hosted_client_endpoint.unref();
    runtime.reset();
    if (peer.is_valid()) {
        peer->_close();
    }
    return peer.is_valid();
#else
    return false;
#endif
}

void SteamSessionTransport::set_hosted_endpoint(const Ref<SessionEndpoint> &endpoint) {
    if (hosted_relay) {
        hosted_endpoint = endpoint;
    }
}

void SteamSessionTransport::set_hosted_client_endpoint(const Ref<SessionEndpoint> &endpoint) {
    hosted_client_endpoint = endpoint;
}

Ref<SessionEndpoint> SteamSessionTransport::route(uint64_t owner_id, int32_t virtual_port) {
#ifdef CRIMSON_WITH_STEAM
    if (!valid_user(owner_id) || !valid_virtual_port(virtual_port)) {
        return {};
    }
    PackedByteArray bytes;
    if (bytes.resize(route_size) != OK) {
        return {};
    }
    bytes[0] = route_version;
    for (int32_t index = 0; index < 8; ++index) {
        bytes[index + 1] = static_cast<uint8_t>(owner_id >> (index * 8));
    }
    bytes[9] = static_cast<uint8_t>(virtual_port);
    bytes[10] = static_cast<uint8_t>(virtual_port >> 8);
    return SessionEndpoint::opaque("steam", bytes);
#else
    return {};
#endif
}

SteamPeer *SteamSessionTransport::get_active_peer() const {
    if (!active_peer.is_valid()) {
        return nullptr;
    }
    auto *peer = Object::cast_to<SteamPeer>(ObjectDB::get_instance(active_peer));
    return peer && peer->_get_connection_status() != MultiplayerPeer::CONNECTION_DISCONNECTED ? peer : nullptr;
}

Ref<SessionEndpoint> SteamSessionTransport::get_host_endpoint() const {
#ifdef CRIMSON_WITH_STEAM
    const SteamPeer *peer = get_active_peer();
    if (peer && peer->_is_server()) {
        if (server_runtime) {
            if (hosted_relay) {
                return hosted_endpoint;
            }
            const uint32_t address = dedicated_ipv4 != 0 ? dedicated_ipv4 : server_runtime->get_public_ipv4();
            return SteamServerRoute{server_runtime->get_local_socket_id(), address, dedicated_port}.endpoint();
        }
        if (runtime) {
            return route(runtime->get_user_id(), virtual_port);
        }
    }
#endif
    return {};
}

int32_t SteamSessionTransport::get_host_remote_limit() const {
#ifdef CRIMSON_WITH_STEAM
    const SteamPeer *peer = get_active_peer();
    if (peer && peer->_is_server()) {
        return peer->state->remote_limit;
    }
#endif
    return 0;
}

uint64_t SteamSessionTransport::get_remote_steam_id(int32_t peer_id) const {
    const SteamPeer *peer = get_active_peer();
    return peer ? peer->get_remote_steam_id(peer_id) : 0;
}

Ref<steam::SteamConnectionPath> SteamSessionTransport::get_peer_path(int32_t peer_id,
        uint64_t admitted_account) const {
#ifdef CRIMSON_WITH_STEAM
    auto *peer = SteamRuntime::on_main_thread() ? get_active_peer() : nullptr;
    auto *sdk = peer ? peer->state->sockets() : nullptr;
    const auto handle = sdk ? peer->state->find_ready(peer_id) : k_HSteamNetConnection_Invalid;
    if (handle != k_HSteamNetConnection_Invalid) {
        const auto &connection = peer->state->connections.at(handle);
        if (connection.steam_id == admitted_account) {
            return steam::sample_steam_path(*sdk, handle, connection.token, admitted_account);
        }
    }
#endif
    return {};
}

Ref<SessionTransportResult> SteamSessionTransport::open_server(int64_t, int64_t remote_limit,
        const String &bind_address) {
#ifdef CRIMSON_WITH_STEAM
    if (!server_runtime && (!runtime || !runtime->is_healthy())) {
        return SessionTransportResult::failed(ERR_UNAVAILABLE);
    }
    uint32_t requested_bind = 0;
    if (remote_limit < 1 || remote_limit > max_remote ||
            (bind_address != "*" && (!server_runtime || !SteamServer::parse_ipv4(bind_address, requested_bind) ||
                                            requested_bind != dedicated_ipv4))) {
        return SessionTransportResult::failed(ERR_INVALID_PARAMETER);
    }
    if (get_active_peer()) {
        return SessionTransportResult::failed(ERR_BUSY);
    }
    Ref<SteamPeer> peer;
    peer.instantiate();
    hosted_endpoint.unref();
    Error error = ERR_UNAVAILABLE;
    if (server_runtime) {
        const std::optional<uint32_t> address = hosted_relay ? std::nullopt : std::optional<uint32_t>(dedicated_ipv4);
        error = peer->open(server_runtime, hosted_relay ? hosted_virtual_port : dedicated_port,
                static_cast<int32_t>(remote_limit), 0, address, hosted_relay);
    } else {
        error = peer->open(runtime, virtual_port, static_cast<int32_t>(remote_limit), 0, {}, false, prefer_relay_routes);
    }
    if (error != OK) {
        return SessionTransportResult::failed(error);
    }
    active_peer = ObjectID(peer->get_instance_id());
    return SessionTransportResult::opened(peer);
#else
    return SessionTransportResult::failed(ERR_UNAVAILABLE);
#endif
}

Ref<SessionTransportResult> SteamSessionTransport::open_client(const Ref<SessionEndpoint> &endpoint) {
#ifdef CRIMSON_WITH_STEAM
    if (!runtime || !runtime->is_healthy()) {
        return SessionTransportResult::failed(ERR_UNAVAILABLE);
    }
    uint64_t owner_id = 0;
    int32_t port = 0;
    const auto direct = SteamServerRoute::from_endpoint(endpoint);
    const auto hosted = SteamHostedRoute::from_endpoint(endpoint);
    if (hosted.is_valid() &&
            (!runtime || endpoint != hosted_client_endpoint || !steam::has_steam_relay_ticket(*runtime, hosted))) {
        return SessionTransportResult::failed(ERR_UNAUTHORIZED);
    }
    if (!hosted.is_valid() && !direct.is_valid() && !parse_route(endpoint, owner_id, port)) {
        return SessionTransportResult::failed(ERR_INVALID_PARAMETER);
    }
    if (get_active_peer()) {
        return SessionTransportResult::failed(ERR_BUSY);
    }
    Ref<SteamPeer> peer;
    peer.instantiate();
    Error error = ERR_UNAVAILABLE;
    if (hosted.is_valid()) {
        hosted_client_endpoint.unref();
        error = peer->open(runtime, hosted.virtual_port, 0, hosted.server_id, {}, true);
    } else if (direct.is_valid()) {
        error = peer->open(runtime, direct.port, 0, direct.server_id, direct.ipv4);
    } else {
        error = peer->open(runtime, port, 0, owner_id);
    }
    if (error != OK) {
        return SessionTransportResult::failed(error);
    }
    active_peer = ObjectID(peer->get_instance_id());
    return SessionTransportResult::opened(peer);
#else
    return SessionTransportResult::failed(ERR_UNAVAILABLE);
#endif
}

void SteamSessionTransport::disconnect_after_flush(const Ref<MultiplayerPeer> &peer, int32_t peer_id) {
    SteamPeer *active = get_active_peer();
    if (active && peer.ptr() == active) {
        active->disconnect_after_flush(peer_id);
    }
}

} // namespace crimson::integration
