// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/eos/eos_transport.hpp"

#include "integration/online/eos/eos_runtime.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <cstring>

#ifdef CRIMSON_WITH_EOS
#include <eos_connect.h>
#include <eos_p2p.h>
#include <eos_sdk.h>
#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/time.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#endif

namespace crimson::integration {
namespace {

// EOS currently serializes Product User IDs as short printable strings.
constexpr int64_t max_product_user_length = 32;
// EOS P2P socket name limit excluding the terminator.
constexpr int64_t max_socket_length = 32;
// Serialized endpoint format version.
constexpr uint8_t route_version = 1;
// One large reliable frame can span several EOS datagrams.
constexpr int32_t max_frame_bytes = 1024 * 1024;

// Validate the ASCII characters allowed in a P2P socket name and printable user ID.
bool valid_route_part(const godot::String &value, int64_t limit, bool socket_name) {
    if (value.is_empty() || value.length() > limit) {
        return false;
    }
    for (int64_t index = 0; index < value.length(); ++index) {
        const char32_t ch = value[index];
        const bool alnum = (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z') || (ch >= U'0' && ch <= U'9');
        if (!alnum && !(socket_name && (ch == U'-' || ch == U'_' || ch == U' ' || ch == U'+' || ch == U'=' || ch == U'.'))) {
            return false;
        }
    }
    return true;
}

// Decode and validate a versioned EOS route before it can reach the SDK.
bool decode_route(const godot::Ref<session::CrimsonSessionEndpoint> &endpoint, godot::String &host, godot::String &socket) {
    if (endpoint.is_null() || endpoint->get_transport() != godot::StringName("eos")) {
        return false;
    }
    const godot::PackedByteArray data = endpoint->get_route();
    if (data.size() < 4 || data[0] != route_version) {
        return false;
    }
    const int64_t host_size = data[1];
    const int64_t socket_size = data.size() - 2 - host_size;
    if (host_size < 1 || host_size > max_product_user_length || socket_size < 1 || socket_size > max_socket_length) {
        return false;
    }
    const char *bytes = reinterpret_cast<const char *>(data.ptr());
    host = godot::String::utf8(bytes + 2, host_size);
    socket = godot::String::utf8(bytes + 2 + host_size, socket_size);
    return host.length() == host_size && socket.length() == socket_size &&
            valid_route_part(host, max_product_user_length, false) && valid_route_part(socket, max_socket_length, true);
}

#ifdef CRIMSON_WITH_EOS
// Keep channel zero reliable control and Godot messages in the same EOS ordering lane.
constexpr uint8_t control_channel = 3;
// Three EOS lanes per Godot channel isolate the three delivery modes.
constexpr int32_t max_channel = 84;
// Transport format discriminator, encoded explicitly in little endian.
constexpr uint32_t wire_magic = 0x31534543;
// Magic, type, 64-bit link token, sequence, total frame size, and offset.
constexpr size_t header_bytes = 25;
// Payload capacity of one vendor datagram.
constexpr size_t fragment_bytes = EOS_P2P_MAX_PACKET_SIZE - header_bytes;
// Limit how much transport traffic one poll can consume.
constexpr size_t poll_limit = 256;
// Bound all queued complete frames and all partly received frames separately.
constexpr size_t queue_budget = 4 * 1024 * 1024;
// Cap allocation overhead for many tiny packets and fragments.
constexpr size_t queued_packet_limit = 1024;
constexpr size_t partial_limit = 128;
// One session has at most 64 remote players.
constexpr int32_t max_remotes = 64;
// Limit how long an incomplete handshake, frame, or graceful disconnect can persist.
constexpr uint64_t handshake_ms = 10000;
constexpr uint64_t fragment_ms = 5000;
constexpr uint64_t closing_ms = 1500;
// Bound expensive host-star relay work sourced from each physical connection.
constexpr uint64_t receive_window_ms = 1000;
constexpr size_t receive_window_bytes = 2 * 1024 * 1024;
constexpr size_t receive_window_packets = 2048;

static_assert(godot::MultiplayerPeer::TRANSFER_MODE_UNRELIABLE == 0);
static_assert(godot::MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED == 1);
static_assert(godot::MultiplayerPeer::TRANSFER_MODE_RELIABLE == 2);

// Protocol messages are always bound to the SDK's remote Product User ID and socket.
enum class Kind : uint8_t { Challenge = 1, Hello, Welcome, Ready, Data, Close, CloseAck };

// A decoded datagram header. Control messages leave the frame fields zero.
struct Header {
    Kind kind;
    uint64_t token;
    uint32_t sequence;
    uint32_t total;
    uint32_t offset;
};

// Write an unsigned 32-bit value in the transport's fixed byte order.
constexpr void put_u32(uint8_t *out, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) {
        out[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

// Write an unsigned 64-bit value in the transport's fixed byte order.
constexpr void put_u64(uint8_t *out, uint64_t value) {
    put_u32(out, static_cast<uint32_t>(value));
    put_u32(out + 4, static_cast<uint32_t>(value >> 32));
}

// Read an unsigned 32-bit value from an already size-checked buffer.
constexpr uint32_t get_u32(const uint8_t *in) {
    return uint32_t(in[0]) | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16) | (uint32_t(in[3]) << 24);
}

// Read an unsigned 64-bit value from an already size-checked buffer.
constexpr uint64_t get_u64(const uint8_t *in) {
    return uint64_t(get_u32(in)) | (uint64_t(get_u32(in + 4)) << 32);
}

// Write the wire header without copying a native struct or padding.
constexpr void encode_header(uint8_t *out, const Header &header) {
    put_u32(out, wire_magic);
    out[4] = static_cast<uint8_t>(header.kind);
    put_u64(out + 5, header.token);
    put_u32(out + 13, header.sequence);
    put_u32(out + 17, header.total);
    put_u32(out + 21, header.offset);
}

// Reject unknown formats and message types before reading frame fields.
constexpr bool decode_header(const uint8_t *in, size_t size, Header &header) {
    if (size < header_bytes || size > EOS_P2P_MAX_PACKET_SIZE || get_u32(in) != wire_magic ||
            in[4] < static_cast<uint8_t>(Kind::Challenge) || in[4] > static_cast<uint8_t>(Kind::CloseAck)) {
        return false;
    }
    header = { static_cast<Kind>(in[4]), get_u64(in + 5), get_u32(in + 13), get_u32(in + 17), get_u32(in + 21) };
    return header.token != 0;
}

// Compare sequence counters across wrap, reserving zero as uninitialized.
constexpr bool newer(uint32_t sequence, uint32_t previous) {
    return sequence != 0 && (previous == 0 || (sequence - previous != 0 && sequence - previous < 0x80000000U));
}

// Compile-time regression for the wire format, truncated frames, and sequence wrap.
constexpr bool verify_wire_header() {
    std::array<uint8_t, header_bytes> bytes{};
    Header decoded{};
    encode_header(bytes.data(), { Kind::Data, 0x123456789abcdef0ULL, 7, 8000, 1145 });
    return decode_header(bytes.data(), bytes.size(), decoded) && decoded.kind == Kind::Data &&
            decoded.token == 0x123456789abcdef0ULL && decoded.sequence == 7 && decoded.total == 8000 &&
            decoded.offset == 1145 && !decode_header(bytes.data(), bytes.size() - 1, decoded) &&
            !decode_header(bytes.data(), EOS_P2P_MAX_PACKET_SIZE + 1, decoded) &&
            newer(1, UINT32_MAX) && !newer(UINT32_MAX, 1) && !newer(7, 7) && !newer(0, UINT32_MAX);
}

static_assert(verify_wire_header());

// Produce a per-connection random token without falling back to a predictable value.
uint64_t random_token() {
    godot::Ref<godot::Crypto> crypto;
    crypto.instantiate();
    const godot::PackedByteArray bytes = crypto->generate_random_bytes(8);
    return bytes.size() == 8 ? get_u64(bytes.ptr()) : 0;
}

// Give each listener a fresh socket so old listings and queued traffic cannot address a later match.
godot::String fresh_socket(const godot::String &prefix) {
    const uint64_t token = random_token();
    if (!token) {
        return {};
    }
    constexpr char digits[] = "0123456789abcdef";
    char suffix[17]{};
    for (size_t index = 0; index < 16; ++index) {
        suffix[index] = digits[(token >> ((15 - index) * 4)) & 15];
    }
    return prefix.substr(0, max_socket_length - 17) + "-" + suffix;
}

// Convert a valid Godot transfer channel and mode into an EOS lane.
uint8_t wire_channel(int32_t channel, godot::MultiplayerPeer::TransferMode mode) {
    return static_cast<uint8_t>(1 + channel * 3 + static_cast<int32_t>(mode));
}

// Translate immediate SDK send/open outcomes into Godot transport errors.
godot::Error sdk_error(EOS_EResult result) {
    switch (result) {
        case EOS_EResult::EOS_Success: return godot::OK;
        case EOS_EResult::EOS_InvalidParameters: return godot::ERR_INVALID_PARAMETER;
        case EOS_EResult::EOS_LimitExceeded: return godot::ERR_BUSY;
        case EOS_EResult::EOS_NoConnection: return godot::ERR_CONNECTION_ERROR;
        case EOS_EResult::EOS_InvalidUser: return godot::ERR_UNAUTHORIZED;
        default: return godot::FAILED;
    }
}

// EOS ReceivePacket has no socket filter and relay policy is platform-wide. One peer owns this interface.
std::set<EOS_HP2P> readers;

#define CRIMSON_EOS_TRANSPORT_FUNCTIONS(F) \
    F(EOS_Platform_GetP2PInterface) \
    F(EOS_Platform_GetConnectInterface) \
    F(EOS_Connect_GetLoginStatus) \
    F(EOS_ProductUserId_FromString) \
    F(EOS_ProductUserId_ToString) \
    F(EOS_P2P_SendPacket) \
    F(EOS_P2P_ReceivePacket) \
    F(EOS_P2P_AcceptConnection) \
    F(EOS_P2P_CloseConnection) \
    F(EOS_P2P_SetRelayControl) \
    F(EOS_P2P_AddNotifyPeerConnectionRequest) \
    F(EOS_P2P_RemoveNotifyPeerConnectionRequest) \
    F(EOS_P2P_AddNotifyPeerConnectionEstablished) \
    F(EOS_P2P_RemoveNotifyPeerConnectionEstablished) \
    F(EOS_P2P_AddNotifyPeerConnectionClosed) \
    F(EOS_P2P_RemoveNotifyPeerConnectionClosed)

// Resolves only the SDK exports required by the peer from the addon-local runtime.
struct P2PApi {
    // Each typed pointer refers to the corresponding EOS API export.
#define CRIMSON_EOS_DECLARE(name) decltype(&::name) name = nullptr;
    CRIMSON_EOS_TRANSPORT_FUNCTIONS(CRIMSON_EOS_DECLARE)
#undef CRIMSON_EOS_DECLARE

    // Resolve every required symbol before touching any EOS interfaces.
    bool load(const EosPlatform &platform) {
#define CRIMSON_EOS_RESOLVE(name) \
        name = reinterpret_cast<decltype(name)>(platform.get_proc_address(#name)); \
        if (!name) { return false; }
        CRIMSON_EOS_TRANSPORT_FUNCTIONS(CRIMSON_EOS_RESOLVE)
#undef CRIMSON_EOS_RESOLVE
        return true;
    }

    // Copy an SDK-authenticated user ID into a canonical, bounded connection key.
    std::string canonical(EOS_ProductUserId user) const {
        std::array<char, EOS_PRODUCTUSERID_MAX_LENGTH + 1> text{};
        int32_t size = static_cast<int32_t>(text.size());
        if (!user || EOS_ProductUserId_ToString(user, text.data(), &size) != EOS_EResult::EOS_Success ||
                size < 2 || size > static_cast<int32_t>(text.size()) || text[static_cast<size_t>(size - 1)] != 0) {
            return {};
        }
        return std::string(text.data(), static_cast<size_t>(size - 1));
    }
};
#undef CRIMSON_EOS_TRANSPORT_FUNCTIONS

// A single in-order reliable frame while its bounded fragments arrive.
struct Partial {
    // Sequence shared by all fragments in the frame.
    uint32_t sequence = 0;
    // Offset expected on the next ordered fragment.
    uint32_t received = 0;
    // Time at which the incomplete allocation must be released.
    uint64_t deadline = 0;
    // Allocated only after checking the per-peer and per-transport budgets.
    std::vector<uint8_t> data;
};

// An EOS connection identified by the remote PUID supplied by the SDK.
struct Link {
    // Actual EOS connection identity for send and close operations.
    EOS_ProductUserId user = nullptr;
    // Assigned Godot ID, zero until the host allocates an ID.
    int32_t id = 0;
    // Token sent by the client and echoed by the authority in its welcome.
    uint64_t client_token = 0;
    // Token assigned by the host to this connection incarnation.
    uint64_t token = 0;
    // Handshake or graceful close deadline, zero for open links.
    uint64_t deadline = 0;
    // True after the link is visible to Godot.
    bool announced = false;
    // True while waiting for a reliable close acknowledgment.
    bool waiting_close_ack = false;
    // True after receipt of a reliable close control frame.
    bool got_close = false;
    // True when the SDK closes the link, allowing one final bounded receive batch.
    bool sdk_closed = false;
    // True after a terminal error or once the SDK closure has been drained.
    bool drop = false;
    // Actual path reported by the SDK, including on reconnection.
    EOS_ENetworkConnectionType path = EOS_ENetworkConnectionType::EOS_NCT_NoConnection;
    // Most recent complete sequence on each ordered lane.
    std::array<uint32_t, 256> last_sequence{};
    // Incomplete reliable frame per active lane.
    std::map<uint8_t, Partial> partials;
    // Bytes currently reserved by this peer's incomplete frames.
    size_t reserved = 0;
    // Fixed interval over which the physical sender is allowed a bounded burst.
    uint64_t receive_until = 0;
    // Bytes and packet count already consumed by this sender in the interval.
    size_t received_bytes = 0;
    size_t received_packets = 0;
};
#endif

} // namespace

using namespace godot;

struct CrimsonEOSPeer::State {
    // Godot delivery mode used on subsequent sends.
    TransferMode mode = TRANSFER_MODE_RELIABLE;
    // Godot logical channel used on subsequent sends.
    int32_t channel = 0;
    // Godot recipient, broadcast, or broadcast exclusion.
    int32_t target = 0;
    // Local Godot identity, set by host or handshake.
    int32_t self = 0;
    // Current engine-facing connection status.
    ConnectionStatus status = CONNECTION_DISCONNECTED;
    // Whether this peer owns the authority.
    bool host = false;
    // Rejects new physical connections while leaving announced peers active.
    bool refusing = false;

#ifdef CRIMSON_WITH_EOS
    // A complete incoming Godot packet and the physical sender from the SDK.
    struct Packet {
        // Remote Godot peer mapped from the authenticated EOS identity.
        int32_t sender;
        // Original Godot transfer channel.
        int32_t channel;
        // Original Godot transfer mode.
        TransferMode mode;
        // Complete reassembled bytes.
        std::vector<uint8_t> data;
    };

    // Configured platform stays alive for the entire peer.
    std::shared_ptr<EosPlatform> platform;
    // Fully resolved SDK exports used by this peer.
    P2PApi api;
    // EOS P2P scope whose local receive queue this peer owns.
    EOS_HP2P p2p = nullptr;
    // Logged-in SDK identity used on the receive and send APIs.
    EOS_ProductUserId local_user = nullptr;
    // Canonical local identity used to release the receive queue lease.
    std::string local_key;
    // Listener socket advertised by the directory.
    EOS_P2P_SocketId socket{};
    // Registered SDK event notifications, removed before peer state destruction.
    EOS_NotificationId requested = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId established = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId closed = EOS_INVALID_NOTIFICATIONID;
    // Authority-configured limit, including unfinished handshakes.
    int32_t remote_limit = 0;
    // Assign each guest a fresh Godot ID for the lifetime of the listener.
    int64_t next_peer = 2;
    // One live physical connection per SDK-authenticated remote identity.
    std::map<std::string, Link> links;
    // Reliable and unreliable ordered outgoing counters per lane.
    std::array<uint32_t, 256> outgoing{};
    // Complete packets awaiting engine consumption.
    std::deque<Packet> packets;
    // Backing storage for the last packet returned to Godot.
    std::vector<uint8_t> read_buffer;
    // Bytes in the engine receive queue.
    size_t queued_bytes = 0;
    // Bytes and frame count reserved by incomplete reliable frames.
    size_t partial_bytes = 0;
    size_t partial_count = 0;

    // Compare fixed-size SDK socket names without trusting network terminators.
    bool owns_socket(const EOS_P2P_SocketId *candidate) const {
        return candidate && std::strncmp(socket.SocketName, candidate->SocketName, sizeof(socket.SocketName)) == 0;
    }

    // Look up the currently announced physical connection for a Godot ID.
    auto find_peer(int32_t id) {
        return std::find_if(links.begin(), links.end(), [id](const auto &entry) {
            return entry.second.announced && entry.second.id == id;
        });
    }

    // Send one SDK-sized datagram to an explicitly accepted connection.
    Error send(EOS_ProductUserId remote, uint8_t lane, bool reliable, const uint8_t *data, size_t size) {
        EOS_P2P_SendPacketOptions options{};
        options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
        options.LocalUserId = local_user;
        options.RemoteUserId = remote;
        options.SocketId = &socket;
        options.Channel = lane;
        options.DataLengthBytes = static_cast<uint32_t>(size);
        options.Data = data;
        options.bAllowDelayedDelivery = reliable ? EOS_TRUE : EOS_FALSE;
        options.Reliability = reliable ? EOS_EPacketReliability::EOS_PR_ReliableOrdered : EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
        options.bDisableAutoAcceptConnection = EOS_TRUE;
        return sdk_error(api.EOS_P2P_SendPacket(p2p, &options));
    }

    // Serialize a control frame on Godot's reliable channel zero lane.
    Error control(const Link &link, Kind kind, uint64_t token) {
        std::array<uint8_t, header_bytes + 12> bytes{};
        encode_header(bytes.data(), { kind, token, 0, 0, 0 });
        size_t size = header_bytes;
        if (kind == Kind::Hello) {
            put_u64(bytes.data() + header_bytes, link.client_token);
            size += 8;
        } else if (kind == Kind::Welcome) {
            put_u32(bytes.data() + header_bytes, static_cast<uint32_t>(link.id));
            put_u64(bytes.data() + header_bytes + 4, link.token);
            size += 12;
        }
        return send(link.user, control_channel, true, bytes.data(), size);
    }

    // Accept only the specific remote and socket selected by this peer.
    Error accept(EOS_ProductUserId remote) {
        EOS_P2P_AcceptConnectionOptions options{};
        options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
        options.LocalUserId = local_user;
        options.RemoteUserId = remote;
        options.SocketId = &socket;
        return sdk_error(api.EOS_P2P_AcceptConnection(p2p, &options));
    }

    // Close just this remote's connection on the leased socket.
    void close_remote(EOS_ProductUserId remote) {
        EOS_P2P_CloseConnectionOptions options{};
        options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
        options.LocalUserId = local_user;
        options.RemoteUserId = remote;
        options.SocketId = &socket;
        api.EOS_P2P_CloseConnection(p2p, &options);
    }

    // Accept a new SDK-authenticated sender only when a bounded seat is available.
    static void EOS_CALL incoming(const EOS_P2P_OnIncomingConnectionRequestInfo *info) {
        auto &self = *static_cast<State *>(info->ClientData);
        if (!self.owns_socket(info->SocketId)) {
            return;
        }
        const std::string key = self.api.canonical(info->RemoteUserId);
        if (key.empty() || key == self.local_key || self.links.count(key)) {
            return;
        }
        if (!self.host || self.refusing || self.links.size() >= static_cast<size_t>(self.remote_limit)) {
            self.close_remote(info->RemoteUserId);
            return;
        }
        Link link;
        link.user = info->RemoteUserId;
        link.token = random_token();
        link.deadline = Time::get_singleton()->get_ticks_msec() + handshake_ms;
        if (!link.token) {
            self.close_remote(info->RemoteUserId);
            return;
        }
        const auto entry = self.links.emplace(key, std::move(link)).first;
        if (self.accept(info->RemoteUserId) != OK) {
            self.links.erase(entry);
        }
    }

    // Record the observed route without exposing the user's address.
    static void EOS_CALL connection_established(const EOS_P2P_OnPeerConnectionEstablishedInfo *info) {
        auto &self = *static_cast<State *>(info->ClientData);
        if (!self.owns_socket(info->SocketId)) {
            return;
        }
        const auto remote = self.links.find(self.api.canonical(info->RemoteUserId));
        if (remote != self.links.end()) {
            auto &link = remote->second;
            link.path = info->NetworkType;
            if (self.host && !link.announced && link.client_token == 0) {
                link.drop |= self.control(link, Kind::Challenge, link.token) != OK;
            }
        }
    }

    // Defer Godot disconnect notifications until poll has drained queued frames.
    static void EOS_CALL connection_closed(const EOS_P2P_OnRemoteConnectionClosedInfo *info) {
        auto &self = *static_cast<State *>(info->ClientData);
        if (!self.owns_socket(info->SocketId) || info->Reason == EOS_EConnectionClosedReason::EOS_CCR_ClosedByLocalUser) {
            return;
        }
        const auto remote = self.links.find(self.api.canonical(info->RemoteUserId));
        if (remote != self.links.end()) {
            remote->second.sdk_closed = true;
        }
    }

    // Install socket-scoped notifications after reserving the local receive queue.
    bool subscribe() {
        if (host) {
            EOS_P2P_AddNotifyPeerConnectionRequestOptions incoming_options{};
            incoming_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
            incoming_options.LocalUserId = local_user;
            incoming_options.SocketId = &socket;
            requested = api.EOS_P2P_AddNotifyPeerConnectionRequest(p2p, &incoming_options, this, &incoming);
        }
        EOS_P2P_AddNotifyPeerConnectionEstablishedOptions established_options{};
        established_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
        established_options.LocalUserId = local_user;
        established_options.SocketId = &socket;
        established = api.EOS_P2P_AddNotifyPeerConnectionEstablished(p2p, &established_options, this, &connection_established);
        EOS_P2P_AddNotifyPeerConnectionClosedOptions closed_options{};
        closed_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
        closed_options.LocalUserId = local_user;
        closed_options.SocketId = &socket;
        closed = api.EOS_P2P_AddNotifyPeerConnectionClosed(p2p, &closed_options, this, &connection_closed);
        return (!host || requested != EOS_INVALID_NOTIFICATIONID) &&
                established != EOS_INVALID_NOTIFICATIONID && closed != EOS_INVALID_NOTIFICATIONID;
    }

    // Remove all SDK subscriptions and release receive ownership before releasing the platform.
    void shutdown() {
        if (platform && platform->get_handle() && p2p) {
            if (requested != EOS_INVALID_NOTIFICATIONID) {
                api.EOS_P2P_RemoveNotifyPeerConnectionRequest(p2p, requested);
            }
            if (established != EOS_INVALID_NOTIFICATIONID) {
                api.EOS_P2P_RemoveNotifyPeerConnectionEstablished(p2p, established);
            }
            if (closed != EOS_INVALID_NOTIFICATIONID) {
                api.EOS_P2P_RemoveNotifyPeerConnectionClosed(p2p, closed);
            }
            for (const auto &entry : links) {
                close_remote(entry.second.user);
            }
        }
        readers.erase(p2p);
        p2p = nullptr;
        platform.reset();
        links.clear();
        packets.clear();
        queued_bytes = partial_bytes = partial_count = 0;
        requested = established = closed = EOS_INVALID_NOTIFICATIONID;
    }

    // Release one reserved frame from all associated budgets.
    void erase_partial(Link &link, std::map<uint8_t, Partial>::iterator partial) {
        const size_t size = partial->second.data.size();
        link.reserved -= size;
        partial_bytes -= size;
        --partial_count;
        link.partials.erase(partial);
    }

    // Queue a complete packet while preserving receive ordering and byte limits.
    bool queue(Link &link, uint8_t lane, std::vector<uint8_t> data) {
        if (packets.size() >= queued_packet_limit || data.size() > queue_budget - queued_bytes) {
            return false;
        }
        queued_bytes += data.size();
        packets.push_back({ link.id, (lane - 1) / 3, static_cast<TransferMode>((lane - 1) % 3), std::move(data) });
        return true;
    }

    // Receive one data fragment from a completed physical handshake.
    void receive_data(Link &link, uint8_t lane, const Header &header, const uint8_t *payload, size_t size, uint64_t now) {
        if (!link.announced || link.got_close || link.waiting_close_ack || lane == 0 || header.token != link.token ||
                header.total == 0 || header.total > max_frame_bytes || header.offset >= header.total ||
                size != (std::min)(fragment_bytes, size_t(header.total - header.offset))) {
            return;
        }
        const auto mode = static_cast<TransferMode>((lane - 1) % 3);
        const bool ordered = mode != TRANSFER_MODE_UNRELIABLE;
        if (ordered && !newer(header.sequence, link.last_sequence[lane])) {
            return;
        }
        auto partial = link.partials.find(lane);
        if (partial != link.partials.end() && header.offset == 0) {
            // An ordered lane cannot start another frame before completing this one.
            link.drop = true;
            return;
        }
        if (header.total == size) {
            if (queue(link, lane, std::vector<uint8_t>(payload, payload + size)) && ordered) {
                link.last_sequence[lane] = header.sequence;
            } else if (mode == TRANSFER_MODE_RELIABLE && link.last_sequence[lane] != header.sequence) {
                link.drop = true;
            }
            return;
        }
        if (mode != TRANSFER_MODE_RELIABLE) {
            return;
        }
        if (header.offset == 0) {
            if (header.total > size_t(max_frame_bytes) - link.reserved ||
                    header.total > queue_budget - partial_bytes || partial_count >= partial_limit) {
                link.drop = true;
                return;
            }
            Partial next;
            next.sequence = header.sequence;
            next.deadline = now + fragment_ms;
            next.data.resize(header.total);
            link.reserved += header.total;
            partial_bytes += header.total;
            ++partial_count;
            partial = link.partials.emplace(lane, std::move(next)).first;
        }
        if (partial == link.partials.end() || partial->second.deadline <= now ||
                partial->second.sequence != header.sequence || partial->second.data.size() != header.total ||
                partial->second.received != header.offset) {
            return;
        }
        auto &frame = partial->second;
        std::memcpy(frame.data.data() + frame.received, payload, size);
        frame.received += static_cast<uint32_t>(size);
        if (frame.received == header.total) {
            const bool queued = queue(link, lane, std::move(frame.data));
            link.reserved -= header.total;
            partial_bytes -= header.total;
            --partial_count;
            link.partials.erase(partial);
            link.last_sequence[lane] = header.sequence;
            link.drop |= !queued;
        }
    }

    // Handle authority assignment, physical handshake completion, and reliable close.
    void receive_control(CrimsonEOSPeer &peer, Link &link, const Header &header, const uint8_t *payload, size_t size,
            uint64_t now) {
        const size_t expected = header.kind == Kind::Welcome ? 12 : header.kind == Kind::Hello ? 8 : 0;
        if (header.sequence || header.total || header.offset || size != expected) {
            return;
        }
        if (header.kind == Kind::Challenge && !host && !link.announced) {
            link.drop |= control(link, Kind::Hello, header.token) != OK;
        } else if (header.kind == Kind::Hello && host && !link.announced && !refusing && link.client_token == 0 &&
                header.token == link.token) {
            if (next_peer > INT32_MAX || !(link.client_token = get_u64(payload))) {
                link.drop = true;
                return;
            }
            link.id = static_cast<int32_t>(next_peer++);
            link.drop = control(link, Kind::Welcome, link.client_token) != OK;
        } else if (header.kind == Kind::Welcome && !host && !link.announced && header.token == link.client_token) {
            const uint32_t id = get_u32(payload);
            link.token = get_u64(payload + 4);
            if (id <= 1 || id > INT32_MAX || !link.token || control(link, Kind::Ready, link.token) != OK) {
                link.drop = true;
                return;
            }
            self = static_cast<int32_t>(id);
            status = CONNECTION_CONNECTED;
            link.announced = true;
            link.deadline = 0;
            peer.emit_signal("peer_connected", 1);
        } else if (header.kind == Kind::Ready && host && !link.announced && header.token == link.token) {
            if (refusing) {
                link.drop = true;
                return;
            }
            link.announced = true;
            link.deadline = 0;
            peer.emit_signal("peer_connected", link.id);
        } else if (header.kind == Kind::Close && link.announced && header.token == link.token) {
            link.got_close = true;
            link.deadline = now + closing_ms;
        } else if (header.kind == Kind::CloseAck && link.waiting_close_ack && header.token == link.token) {
            link.drop = true;
        }
    }

    // Fragment large reliable frames while keeping small unreliable sends atomic.
    Error send_frame(Link &link, uint8_t lane, const uint8_t *data, size_t size) {
        if (link.drop || link.got_close || link.waiting_close_ack) {
            return ERR_CONNECTION_ERROR;
        }
        const bool reliable = mode == TRANSFER_MODE_RELIABLE;
        uint32_t sequence = ++outgoing[lane];
        if (sequence == 0) {
            sequence = ++outgoing[lane];
        }
        std::array<uint8_t, EOS_P2P_MAX_PACKET_SIZE> datagram{};
        for (size_t offset = 0; offset < size; offset += fragment_bytes) {
            const size_t length = (std::min)(fragment_bytes, size - offset);
            encode_header(datagram.data(), { Kind::Data, link.token, sequence, static_cast<uint32_t>(size),
                static_cast<uint32_t>(offset) });
            std::memcpy(datagram.data() + header_bytes, data + offset, length);
            const Error error = send(link.user, lane, reliable, datagram.data(), header_bytes + length);
            if (error != OK) {
                link.drop |= reliable && offset != 0;
                return error;
            }
        }
        return OK;
    }

    // Remove a connection and all of its queued and partially received data.
    void remove(CrimsonEOSPeer &peer, std::map<std::string, Link>::iterator entry, bool notify) {
        auto &link = entry->second;
        const int32_t id = link.id;
        const bool announced = link.announced;
        if (p2p && platform && platform->get_handle()) {
            close_remote(link.user);
        }
        partial_bytes -= link.reserved;
        partial_count -= link.partials.size();
        if (id) {
            for (auto packet = packets.begin(); packet != packets.end();) {
                if (packet->sender == id) {
                    queued_bytes -= packet->data.size();
                    packet = packets.erase(packet);
                } else {
                    ++packet;
                }
            }
        }
        links.erase(entry);
        if (!host) {
            status = CONNECTION_DISCONNECTED;
            shutdown();
        }
        if (notify && announced) {
            peer.emit_signal("peer_disconnected", id);
        }
    }

    // Drain no more than one bounded batch from the leased EOS receive queue.
    void receive(CrimsonEOSPeer &peer, uint64_t now) {
        std::array<uint8_t, EOS_P2P_MAX_PACKET_SIZE> bytes{};
        EOS_P2P_ReceivePacketOptions options{};
        options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
        options.LocalUserId = local_user;
        options.MaxDataSizeBytes = static_cast<uint32_t>(bytes.size());
        for (size_t count = 0; count < poll_limit && status != CONNECTION_DISCONNECTED; ++count) {
            EOS_ProductUserId sender = nullptr;
            EOS_P2P_SocketId packet_socket{};
            packet_socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
            uint8_t lane = 0;
            uint32_t size = 0;
            if (api.EOS_P2P_ReceivePacket(p2p, &options, &sender, &packet_socket, &lane, bytes.data(), &size) != EOS_EResult::EOS_Success) {
                break;
            }
            if (!owns_socket(&packet_socket)) {
                continue;
            }
            const auto entry = links.find(api.canonical(sender));
            if (entry == links.end()) {
                continue;
            }
            auto &link = entry->second;
            if (link.drop) {
                continue;
            }
            if (now >= link.receive_until) {
                link.receive_until = now + receive_window_ms;
                link.received_bytes = link.received_packets = 0;
            }
            link.received_bytes += size;
            if (++link.received_packets > receive_window_packets || link.received_bytes > receive_window_bytes) {
                link.drop = true;
                continue;
            }
            Header header{};
            if (!decode_header(bytes.data(), size, header)) {
                continue;
            }
            if (header.kind == Kind::Data) {
                receive_data(link, lane, header, bytes.data() + header_bytes, size - header_bytes, now);
            } else if (lane == control_channel) {
                receive_control(peer, link, header, bytes.data() + header_bytes, size - header_bytes, now);
            }
        }
    }

    // Finish physical closes only after older received packets have reached Godot.
    void finish_poll(CrimsonEOSPeer &peer, uint64_t now) {
        auto entry = links.begin();
        while (entry != links.end() && status != CONNECTION_DISCONNECTED) {
            auto &link = entry->second;
            link.drop |= link.sdk_closed;
            for (auto frame = link.partials.begin(); frame != link.partials.end();) {
                if (frame->second.deadline <= now) {
                    erase_partial(link, frame++);
                    link.drop = true;
                } else {
                    ++frame;
                }
            }
            const bool expired = link.deadline && link.deadline <= now;
            if (!expired && !link.drop && !link.got_close) {
                ++entry;
                continue;
            }
            const int32_t id = link.id;
            if (!expired && std::any_of(packets.begin(), packets.end(), [id](const Packet &packet) {
                return packet.sender == id;
            })) {
                ++entry;
                continue;
            }
            if (link.got_close) {
                control(link, Kind::CloseAck, link.token);
                // Keep the EOS reliable queue alive while the sender consumes the acknowledgment.
                link.got_close = false;
                link.waiting_close_ack = true;
                if (!expired && !link.drop) {
                    ++entry;
                    continue;
                }
            }
            remove(peer, entry, true);
            // The Godot signal can close or mutate this transport reentrantly.
            entry = links.begin();
        }
    }
#endif
};

CrimsonEOSPeer::CrimsonEOSPeer() : state(std::make_unique<State>()) {}

CrimsonEOSPeer::~CrimsonEOSPeer() {
    _close();
}

void CrimsonEOSPeer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_remote_product_user_id", "peer_id"), &CrimsonEOSPeer::get_remote_product_user_id);
    ClassDB::bind_method(D_METHOD("get_peer_path", "peer_id"), &CrimsonEOSPeer::get_peer_path);
}

Error CrimsonEOSPeer::open(const std::shared_ptr<EosPlatform> &platform, const String &local_user, const String &socket,
        const String &remote_host, int32_t remote_limit, bool force_relay) {
#ifdef CRIMSON_WITH_EOS
    if (!platform || !platform->get_handle() || !valid_route_part(local_user, max_product_user_length, false) ||
            !valid_route_part(socket, max_socket_length, true)) {
        return ERR_INVALID_PARAMETER;
    }
    auto &s = *state;
    if (!s.api.load(*platform)) {
        return ERR_UNAVAILABLE;
    }
    s.platform = platform;
    const EOS_HPlatform handle = platform->get_handle();
    const EOS_HConnect connect = s.api.EOS_Platform_GetConnectInterface(handle);
    const CharString local_utf8 = local_user.utf8();
    s.local_user = s.api.EOS_ProductUserId_FromString(local_utf8.get_data());
    if (!connect || !s.local_user || s.api.EOS_Connect_GetLoginStatus(connect, s.local_user) != EOS_ELoginStatus::EOS_LS_LoggedIn) {
        return ERR_UNAUTHORIZED;
    }
    s.local_key = s.api.canonical(s.local_user);
    const EOS_HP2P p2p = s.api.EOS_Platform_GetP2PInterface(handle);
    if (!p2p || s.local_key.empty()) {
        return ERR_UNAVAILABLE;
    }
    if (!readers.insert(p2p).second) {
        return ERR_ALREADY_IN_USE;
    }
    s.p2p = p2p;
    s.socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    const CharString socket_utf8 = socket.utf8();
    std::memcpy(s.socket.SocketName, socket_utf8.get_data(), static_cast<size_t>(socket_utf8.length()));
    s.host = remote_host.is_empty();
    s.remote_limit = remote_limit;
    s.refusing = s.host;
    s.self = s.host ? 1 : 0;
    s.status = s.host ? CONNECTION_CONNECTED : CONNECTION_CONNECTING;
    EOS_P2P_SetRelayControlOptions relay{};
    relay.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
    relay.RelayControl = force_relay ? EOS_ERelayControl::EOS_RC_ForceRelays : EOS_ERelayControl::EOS_RC_AllowRelays;
    const Error relay_error = sdk_error(s.api.EOS_P2P_SetRelayControl(p2p, &relay));
    if (relay_error != OK) {
        _close();
        return relay_error;
    }
    if (!s.subscribe()) {
        _close();
        return ERR_CANT_CREATE;
    }
    if (!s.host) {
        Link link;
        const CharString host_utf8 = remote_host.utf8();
        link.user = s.api.EOS_ProductUserId_FromString(host_utf8.get_data());
        link.id = 1;
        link.client_token = random_token();
        link.deadline = Time::get_singleton()->get_ticks_msec() + handshake_ms;
        const std::string key = s.api.canonical(link.user);
        if (key.empty() || key == s.local_key || !link.client_token) {
            _close();
            return ERR_INVALID_PARAMETER;
        }
        const auto &remote = s.links.emplace(key, std::move(link)).first->second;
        const Error error = s.accept(remote.user);
        if (error != OK) {
            _close();
            return error;
        }
    }
    return OK;
#else
    (void)platform;
    (void)local_user;
    (void)socket;
    (void)remote_host;
    (void)remote_limit;
    (void)force_relay;
    return ERR_UNAVAILABLE;
#endif
}

String CrimsonEOSPeer::get_remote_product_user_id(int32_t peer_id) const {
#ifdef CRIMSON_WITH_EOS
    const auto remote = state->find_peer(peer_id);
    return remote != state->links.end() ? String::utf8(remote->first.c_str()) : String();
#else
    (void)peer_id;
    return {};
#endif
}

StringName CrimsonEOSPeer::get_peer_path(int32_t peer_id) const {
#ifdef CRIMSON_WITH_EOS
    const auto remote = state->find_peer(peer_id);
    if (remote != state->links.end()) {
        if (remote->second.path == EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection) {
            return StringName("relay");
        }
        if (remote->second.path == EOS_ENetworkConnectionType::EOS_NCT_DirectConnection) {
            return StringName("direct");
        }
    }
#else
    (void)peer_id;
#endif
    return StringName("unknown");
}

void CrimsonEOSPeer::disconnect_after_flush(int32_t peer_id) {
#ifdef CRIMSON_WITH_EOS
    const auto remote = state->find_peer(peer_id);
    if (remote == state->links.end() || remote->second.waiting_close_ack) {
        return;
    }
    auto &link = remote->second;
    link.waiting_close_ack = true;
    link.deadline = Time::get_singleton()->get_ticks_msec() + closing_ms;
    link.drop |= state->control(link, Kind::Close, link.token) != OK;
#else
    (void)peer_id;
#endif
}

Error CrimsonEOSPeer::_get_packet(const uint8_t **buffer, int32_t *size) {
#ifdef CRIMSON_WITH_EOS
    auto &s = *state;
    if (!s.packets.empty()) {
        s.read_buffer = std::move(s.packets.front().data);
        s.packets.pop_front();
        s.queued_bytes -= s.read_buffer.size();
        *buffer = s.read_buffer.data();
        *size = static_cast<int32_t>(s.read_buffer.size());
        return OK;
    }
#endif
    *buffer = nullptr;
    *size = 0;
    return ERR_UNAVAILABLE;
}

Error CrimsonEOSPeer::_put_packet(const uint8_t *buffer, int32_t size) {
#ifdef CRIMSON_WITH_EOS
    auto &s = *state;
    if (s.status != CONNECTION_CONNECTED || !s.platform || !s.platform->get_handle()) {
        return ERR_CONNECTION_ERROR;
    }
    if (!buffer || size <= 0 || size > max_frame_bytes || s.channel < 0 || s.channel > max_channel ||
            s.mode < TRANSFER_MODE_UNRELIABLE || s.mode > TRANSFER_MODE_RELIABLE ||
            (s.mode != TRANSFER_MODE_RELIABLE && static_cast<size_t>(size) > fragment_bytes)) {
        return ERR_INVALID_PARAMETER;
    }
    const uint8_t lane = wire_channel(s.channel, s.mode);
    if (s.target > 0) {
        const auto remote = s.find_peer(s.target);
        return remote != s.links.end() ? s.send_frame(remote->second, lane, buffer, static_cast<size_t>(size)) :
                ERR_INVALID_PARAMETER;
    }
    Error result = OK;
    for (auto &entry : s.links) {
        auto &link = entry.second;
        if (link.announced && int64_t(link.id) != -int64_t(s.target)) {
            const Error error = s.send_frame(link, lane, buffer, static_cast<size_t>(size));
            if (error != OK) {
                result = error;
            }
        }
    }
    return result;
#else
    (void)buffer;
    (void)size;
    return ERR_UNAVAILABLE;
#endif
}

int32_t CrimsonEOSPeer::_get_available_packet_count() const {
#ifdef CRIMSON_WITH_EOS
    return static_cast<int32_t>(state->packets.size());
#else
    return 0;
#endif
}

int32_t CrimsonEOSPeer::_get_max_packet_size() const {
    return max_frame_bytes;
}

int32_t CrimsonEOSPeer::_get_packet_channel() const {
#ifdef CRIMSON_WITH_EOS
    return state->packets.empty() ? 0 : state->packets.front().channel;
#else
    return 0;
#endif
}

MultiplayerPeer::TransferMode CrimsonEOSPeer::_get_packet_mode() const {
#ifdef CRIMSON_WITH_EOS
    return state->packets.empty() ? TRANSFER_MODE_RELIABLE : state->packets.front().mode;
#else
    return TRANSFER_MODE_RELIABLE;
#endif
}

void CrimsonEOSPeer::_set_transfer_channel(int32_t channel) { state->channel = channel; }
int32_t CrimsonEOSPeer::_get_transfer_channel() const { return state->channel; }
void CrimsonEOSPeer::_set_transfer_mode(TransferMode mode) { state->mode = mode; }
MultiplayerPeer::TransferMode CrimsonEOSPeer::_get_transfer_mode() const { return state->mode; }
void CrimsonEOSPeer::_set_target_peer(int32_t peer) { state->target = peer; }

int32_t CrimsonEOSPeer::_get_packet_peer() const {
#ifdef CRIMSON_WITH_EOS
    return state->packets.empty() ? 0 : state->packets.front().sender;
#else
    return 0;
#endif
}

bool CrimsonEOSPeer::_is_server() const { return state->host; }

void CrimsonEOSPeer::_poll() {
#ifdef CRIMSON_WITH_EOS
    if (state->status == CONNECTION_DISCONNECTED) {
        return;
    }
    if (!state->platform || !state->platform->get_handle()) {
        _close();
        return;
    }
    const Ref<CrimsonEOSPeer> keep_alive(this);
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    state->receive(*this, now);
    state->finish_poll(*this, now);
#endif
}

void CrimsonEOSPeer::_close() {
    state->status = CONNECTION_DISCONNECTED;
#ifdef CRIMSON_WITH_EOS
    state->shutdown();
#endif
}

void CrimsonEOSPeer::_disconnect_peer(int32_t peer, bool force) {
#ifdef CRIMSON_WITH_EOS
    if (!force) {
        disconnect_after_flush(peer);
        return;
    }
    const auto remote = state->find_peer(peer);
    if (remote != state->links.end()) {
        state->remove(*this, remote, false);
    }
#else
    (void)peer;
    (void)force;
#endif
}

int32_t CrimsonEOSPeer::_get_unique_id() const { return state->self; }
void CrimsonEOSPeer::_set_refuse_new_connections(bool enable) { state->refusing = enable; }
bool CrimsonEOSPeer::_is_refusing_new_connections() const { return state->refusing; }
bool CrimsonEOSPeer::_is_server_relay_supported() const { return true; }
MultiplayerPeer::ConnectionStatus CrimsonEOSPeer::_get_connection_status() const { return state->status; }

void CrimsonEOSSessionTransport::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("route", "host_user", "socket"), &CrimsonEOSSessionTransport::route);
    ClassDB::bind_method(D_METHOD("get_host_endpoint"), &CrimsonEOSSessionTransport::get_host_endpoint);
}

Error CrimsonEOSSessionTransport::configure(const std::shared_ptr<EosPlatform> &p_platform, const String &p_local_user,
        const String &p_socket, bool p_force_relay) {
#ifdef CRIMSON_WITH_EOS
    if (get_active_peer()) {
        return ERR_BUSY;
    }
    if (!p_platform || !p_platform->get_handle() || !valid_route_part(p_local_user, max_product_user_length, false) ||
            !valid_route_part(p_socket, max_socket_length, true)) {
        return ERR_INVALID_PARAMETER;
    }
    platform = p_platform;
    local_user = p_local_user;
    socket_prefix = p_socket;
    force_relay = p_force_relay;
    return OK;
#else
    (void)p_platform;
    (void)p_local_user;
    (void)p_socket;
    (void)p_force_relay;
    return ERR_UNAVAILABLE;
#endif
}

Ref<session::CrimsonSessionEndpoint> CrimsonEOSSessionTransport::route(const String &host_user, const String &socket) {
    if (!valid_route_part(host_user, max_product_user_length, false) || !valid_route_part(socket, max_socket_length, true)) {
        return {};
    }
    const CharString host_bytes = host_user.utf8();
    const CharString socket_bytes = socket.utf8();
    PackedByteArray data;
    data.resize(2 + host_bytes.length() + socket_bytes.length());
    uint8_t *out = data.ptrw();
    out[0] = route_version;
    out[1] = static_cast<uint8_t>(host_bytes.length());
    std::memcpy(out + 2, host_bytes.get_data(), static_cast<size_t>(host_bytes.length()));
    std::memcpy(out + 2 + host_bytes.length(), socket_bytes.get_data(), static_cast<size_t>(socket_bytes.length()));
    return session::CrimsonSessionEndpoint::opaque(StringName("eos"), data);
}

Ref<session::CrimsonSessionEndpoint> CrimsonEOSSessionTransport::get_host_endpoint() const {
    return route(local_user, get_host_socket());
}

CrimsonEOSPeer *CrimsonEOSSessionTransport::get_active_peer() const {
    auto *peer = Object::cast_to<CrimsonEOSPeer>(ObjectDB::get_instance(active_peer));
    return peer && peer->_get_connection_status() != MultiplayerPeer::CONNECTION_DISCONNECTED ? peer : nullptr;
}

String CrimsonEOSSessionTransport::get_host_socket() const {
#ifdef CRIMSON_WITH_EOS
    const auto *peer = get_active_peer();
    if (peer && peer->_is_server()) {
        return String::utf8(peer->state->socket.SocketName);
    }
#endif
    return {};
}

int32_t CrimsonEOSSessionTransport::get_host_remote_limit() const {
#ifdef CRIMSON_WITH_EOS
    const auto *peer = get_active_peer();
    if (peer && peer->_is_server()) {
        return peer->state->remote_limit;
    }
#endif
    return 0;
}

String CrimsonEOSSessionTransport::get_remote_product_user_id(int32_t peer_id) const {
    const auto *peer = get_active_peer();
    return peer ? peer->get_remote_product_user_id(peer_id) : String();
}

Ref<session::CrimsonSessionTransportResult> CrimsonEOSSessionTransport::open_server(int64_t, int64_t remote_limit,
        const String &) {
#ifdef CRIMSON_WITH_EOS
    if (!platform) {
        return session::CrimsonSessionTransportResult::failed(ERR_UNCONFIGURED);
    }
    if (get_active_peer()) {
        return session::CrimsonSessionTransportResult::failed(ERR_BUSY);
    }
    if (remote_limit < 1 || remote_limit > max_remotes) {
        return session::CrimsonSessionTransportResult::failed(ERR_INVALID_PARAMETER);
    }
    const String socket = fresh_socket(socket_prefix);
    if (socket.is_empty()) {
        return session::CrimsonSessionTransportResult::failed(FAILED);
    }
    Ref<CrimsonEOSPeer> peer;
    peer.instantiate();
    const Error error = peer->open(platform, local_user, socket, String(), static_cast<int32_t>(remote_limit), force_relay);
    if (error == OK) {
        active_peer = ObjectID(peer->get_instance_id());
    }
    return error == OK ? session::CrimsonSessionTransportResult::opened(peer) :
            session::CrimsonSessionTransportResult::failed(error);
#else
    (void)remote_limit;
    return session::CrimsonSessionTransportResult::failed(ERR_UNAVAILABLE);
#endif
}

Ref<session::CrimsonSessionTransportResult> CrimsonEOSSessionTransport::open_client(
        const Ref<session::CrimsonSessionEndpoint> &endpoint) {
    String host_user;
    String host_socket;
    if (!decode_route(endpoint, host_user, host_socket)) {
        return session::CrimsonSessionTransportResult::failed(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_EOS
    if (!platform) {
        return session::CrimsonSessionTransportResult::failed(ERR_UNCONFIGURED);
    }
    if (get_active_peer()) {
        return session::CrimsonSessionTransportResult::failed(ERR_BUSY);
    }
    Ref<CrimsonEOSPeer> peer;
    peer.instantiate();
    const Error error = peer->open(platform, local_user, host_socket, host_user, 1, force_relay);
    if (error == OK) {
        active_peer = ObjectID(peer->get_instance_id());
    }
    return error == OK ? session::CrimsonSessionTransportResult::opened(peer) :
            session::CrimsonSessionTransportResult::failed(error);
#else
    return session::CrimsonSessionTransportResult::failed(ERR_UNAVAILABLE);
#endif
}

void CrimsonEOSSessionTransport::disconnect_after_flush(const Ref<MultiplayerPeer> &peer, int32_t peer_id) {
    if (auto *eos_peer = Object::cast_to<CrimsonEOSPeer>(peer.ptr())) {
        eos_peer->disconnect_after_flush(peer_id);
    }
}

} // namespace crimson::integration
