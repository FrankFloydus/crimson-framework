// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_sockets.hpp"
#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_runtime.hpp"
#endif

namespace crimson::steam {
using namespace godot;
namespace {

// Parse a canonical decimal uint64 without losing precision in Godot.
uint64_t decimal_account(const String &value) {
    if (value.is_empty() || value.length() > 20) {
        return 0;
    }
    const CharString bytes = value.utf8();
    if (bytes.length() <= 0 || bytes.length() > 20 || *bytes.get_data() == '0') {
        return 0;
    }
    const char *const end = bytes.get_data() + bytes.length();
    uint64_t account = 0;
    const auto parsed = std::from_chars(bytes.get_data(), end, account);
    return parsed.ec == std::errc() && parsed.ptr == end ? account : 0;
}

// Return a Steam account identifier without signed integer conversion.
String account_string(uint64_t account) {
    return account ? String(std::to_string(account).c_str()) : String();
}

// Validate account kind before creating endpoint configuration, including on SDK-free builds.
bool valid_socket_account(uint64_t account, bool allow_server) {
    constexpr uint64_t type_mask = 0x0f;
    constexpr uint64_t account_mask = 0xffffffff;
    constexpr uint64_t instance_mask = 0xfffff;
    const uint64_t universe = account >> 56;
    const uint64_t type = (account >> 52) & type_mask;
    const uint64_t instance = (account >> 32) & instance_mask;
    return universe >= 1 && universe <= 4 && (account & account_mask) != 0 &&
            ((type == 1 && instance <= 4) || (allow_server && (type == 3 || type == 4)));
}

// Resolve an owned facade only on the Steam scene thread.
Ref<SteamSockets> resolve_service(ObjectID id) {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        return Ref<SteamSockets>(Object::cast_to<SteamSockets>(ObjectDB::get_instance(id)));
    }
#endif
    return {};
}

#ifdef CRIMSON_WITH_STEAM
// Bound native handles across every standalone facade in the process.
constexpr size_t max_process_listeners = 16;
constexpr size_t max_process_connections = 128;
// Bound each scope independently of its other siblings.
constexpr size_t max_listeners = 8;
constexpr size_t max_connections = 64;
// Cap SDK and application work for every frame and connected socket.
constexpr int max_message_bytes = 32 * 1024;
constexpr int max_messages_per_poll = 4;
constexpr int max_frames_per_second = 128;
constexpr int max_bytes_per_second = 256 * 1024;
constexpr int sdk_buffer_bytes = 256 * 1024;
constexpr uint64_t offer_timeout_ms = 1500;
constexpr uint64_t accepted_timeout_ms = 10000;
constexpr int insecure_mask = k_nSteamNetworkConnectionInfoFlags_Unauthenticated |
        k_nSteamNetworkConnectionInfoFlags_Unencrypted;

// Native usage counters are accessed exclusively on the scene thread.
size_t process_listeners = 0;
size_t process_connections = 0;
int64_t next_socket_token = 0;

// Rate budget for one direction of an authenticated socket.
struct Traffic {
    // Starting time of the current bounded window.
    uint64_t start_ms = 0;
    // Frames submitted in the current window.
    int frames = 0;
    // Bytes submitted in the current window.
    int bytes = 0;

    // Charge one bounded frame before copying or submitting its payload.
    bool charge(uint64_t now, int size) {
        if (now - start_ms >= 1000) {
            start_ms = now;
            frames = 0;
            bytes = 0;
        }
        if (frames >= max_frames_per_second || size > max_bytes_per_second - bytes) {
            return false;
        }
        ++frames;
        bytes += size;
        return true;
    }
};

// Configure encryption, certificate requirements and native queue limits before opening a socket.
std::array<SteamNetworkingConfigValue_t, 7> socket_options(int timeout) {
    std::array<SteamNetworkingConfigValue_t, 7> options{};
    options[0].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
    options[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 0);
    options[2].SetInt32(k_ESteamNetworkingConfig_IPLocalHost_AllowWithoutAuth, 0);
    options[3].SetInt32(k_ESteamNetworkingConfig_SendBufferSize, sdk_buffer_bytes);
    options[4].SetInt32(k_ESteamNetworkingConfig_RecvBufferSize, sdk_buffer_bytes);
    options[5].SetInt32(k_ESteamNetworkingConfig_RecvBufferMessages, max_frames_per_second);
    options[6].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, timeout);
    return options;
}

// Convert a prevalidated numeric address and port through the SDK's parser.
bool parse_direct(const std::shared_ptr<SteamRuntime> &runtime, const SteamSocketEndpoint &endpoint,
        SteamNetworkingIPAddr &address) {
    const String ip = endpoint.get_address();
    const String encoded = (ip.contains(":") ? "[" + ip + "]" : ip) + ":" + String::num_int64(endpoint.get_port());
    const auto parse = runtime->symbol<decltype(&SteamAPI_SteamNetworkingIPAddr_ParseString)>(
            "SteamAPI_SteamNetworkingIPAddr_ParseString");
    return parse && parse(&address, encoded.utf8().get_data()) && address.m_port == endpoint.get_port();
}
#endif

} // namespace

void SteamSocketEndpoint::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("direct", "address", "port"), &SteamSocketEndpoint::direct);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("p2p", "steam_id", "virtual_port"), &SteamSocketEndpoint::p2p, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("expect_peer", "steam_id"), &SteamSocketEndpoint::expect_peer);
    ClassDB::bind_method(D_METHOD("get_kind"), &SteamSocketEndpoint::get_kind);
    ClassDB::bind_method(D_METHOD("get_address"), &SteamSocketEndpoint::get_address);
    ClassDB::bind_method(D_METHOD("get_port"), &SteamSocketEndpoint::get_port);
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamSocketEndpoint::get_steam_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "address", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_address");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_port");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_steam_id");
    BIND_ENUM_CONSTANT(DIRECT);
    BIND_ENUM_CONSTANT(P2P);
}

Ref<SteamSocketEndpoint> SteamSocketEndpoint::direct(const String &address, int64_t port) {
    if (port <= 0 || port > 65535 || address.length() > 45 || !address.is_valid_ip_address()) {
        return {};
    }
    Ref<SteamSocketEndpoint> endpoint;
    endpoint.instantiate();
    endpoint->address = address;
    endpoint->port = static_cast<int32_t>(port);
    return endpoint;
}

Ref<SteamSocketEndpoint> SteamSocketEndpoint::p2p(const String &id, int64_t virtual_port) {
    if (virtual_port < 0 || virtual_port > 65535 || !valid_socket_account(decimal_account(id), false)) {
        return {};
    }
    Ref<SteamSocketEndpoint> endpoint;
    endpoint.instantiate();
    endpoint->kind = P2P;
    endpoint->port = static_cast<int32_t>(virtual_port);
    endpoint->steam_id = id;
    return endpoint;
}

Ref<SteamSocketEndpoint> SteamSocketEndpoint::expect_peer(const String &id) const {
    if (kind != DIRECT || port == 0 || !valid_socket_account(decimal_account(id), true)) {
        return {};
    }
    auto endpoint = direct(address, port);
    endpoint->steam_id = id;
    return endpoint;
}

SteamSocketEndpoint::Kind SteamSocketEndpoint::get_kind() const { return kind; }
String SteamSocketEndpoint::get_address() const { return address; }
int32_t SteamSocketEndpoint::get_port() const { return port; }
String SteamSocketEndpoint::get_steam_id() const { return steam_id; }

void SteamSocketOffer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_endpoint"), &SteamSocketOffer::get_endpoint);
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamSocketOffer::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_expires_at_msec"), &SteamSocketOffer::get_expires_at_msec);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "endpoint", PROPERTY_HINT_RESOURCE_TYPE, "SteamSocketEndpoint",
            PROPERTY_USAGE_READ_ONLY), "", "get_endpoint");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "expires_at_msec", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY),
            "", "get_expires_at_msec");
}

Ref<SteamSocketEndpoint> SteamSocketOffer::get_endpoint() const { return endpoint; }
String SteamSocketOffer::get_steam_id() const { return steam_id; }
int64_t SteamSocketOffer::get_expires_at_msec() const { return expires_at_msec; }

void SteamSocketConnection::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_connected"), &SteamSocketConnection::is_connected);
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamSocketConnection::get_steam_id);
    ClassDB::bind_method(D_METHOD("send", "bytes", "delivery"), &SteamSocketConnection::send, DEFVAL(SteamDelivery::RELIABLE));
    ClassDB::bind_method(D_METHOD("path"), &SteamSocketConnection::path);
    ClassDB::bind_method(D_METHOD("close"), &SteamSocketConnection::close);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_steam_id");
    ADD_SIGNAL(MethodInfo("connected"));
    ADD_SIGNAL(MethodInfo("received", PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytes"),
            PropertyInfo(Variant::INT, "delivery")));
    ADD_SIGNAL(MethodInfo("closed", PropertyInfo(Variant::INT, "reason")));
}

bool SteamSocketConnection::is_connected() const {
    const auto service = resolve_service(owner);
    return service.is_valid() && service->account(this) != 0;
}

String SteamSocketConnection::get_steam_id() const {
    const auto service = resolve_service(owner);
    return service.is_valid() ? account_string(service->account(this)) : String();
}

Error SteamSocketConnection::send(const PackedByteArray &bytes, SteamDelivery::Mode delivery) {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->send(this, bytes, delivery) : ERR_UNAVAILABLE;
}

Ref<SteamConnectionPath> SteamSocketConnection::path() const {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->path(this) : Ref<SteamConnectionPath>();
}

Error SteamSocketConnection::close() {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->close_connection(this) : ERR_UNAVAILABLE;
}

void SteamSocketConnectionResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamSocketConnectionResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamSocketConnection",
            PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamSocketConnection> SteamSocketConnectionResult::get_value() const { return value; }

Ref<SteamSocketConnectionResult> SteamSocketConnectionResult::create(Error error,
        const Ref<SteamSocketConnection> &connection) {
    Ref<SteamSocketConnectionResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = connection;
    return result;
}

void SteamSocketConnectOperation::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_finished"), &SteamSocketConnectOperation::is_finished);
    ClassDB::bind_method(D_METHOD("result"), &SteamSocketConnectOperation::result);
    ClassDB::bind_method(D_METHOD("wait"), &SteamSocketConnectOperation::wait);
    ClassDB::bind_method(D_METHOD("cancel"), &SteamSocketConnectOperation::cancel);
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamSocketConnectionResult")));
}

bool SteamSocketConnectOperation::is_finished() const { return terminal_result.is_valid(); }
Ref<SteamSocketConnectionResult> SteamSocketConnectOperation::result() const { return terminal_result; }

Variant SteamSocketConnectOperation::wait() {
    return is_finished() ? Variant(terminal_result) : Variant(Signal(this, "completed"));
}

Error SteamSocketConnectOperation::cancel() {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->cancel(this) : ERR_UNAVAILABLE;
}

void SteamSocketConnectOperation::finish(const Ref<SteamSocketConnectionResult> &result) {
    if (terminal_result.is_null()) {
        terminal_result = result;
        handle = 0;
        emit_signal("completed", result);
    }
}

void SteamSocketListener::_bind_methods() {
    ClassDB::bind_method(D_METHOD("accept", "offer"), &SteamSocketListener::accept);
    ClassDB::bind_method(D_METHOD("reject", "offer"), &SteamSocketListener::reject);
    ClassDB::bind_method(D_METHOD("close"), &SteamSocketListener::close);
    ADD_SIGNAL(MethodInfo("connection_requested", PropertyInfo(Variant::OBJECT, "offer",
            PROPERTY_HINT_RESOURCE_TYPE, "SteamSocketOffer")));
}

Ref<SteamSocketConnectionResult> SteamSocketListener::accept(const Ref<SteamSocketOffer> &offer) {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->accept(this, offer) : SteamSocketConnectionResult::create(ERR_UNAVAILABLE);
}

Error SteamSocketListener::reject(const Ref<SteamSocketOffer> &offer) {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->reject(this, offer) : ERR_UNAVAILABLE;
}

Error SteamSocketListener::close() {
    const auto service = resolve_service(owner);
    return service.is_valid() ? service->close_listener(this) : ERR_UNAVAILABLE;
}

void SteamSocketListenerResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamSocketListenerResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamSocketListener",
            PROPERTY_USAGE_READ_ONLY), "", "get_value");
}

Ref<SteamSocketListener> SteamSocketListenerResult::get_value() const { return value; }

Ref<SteamSocketListenerResult> SteamSocketListenerResult::create(Error error, const Ref<SteamSocketListener> &listener) {
    Ref<SteamSocketListenerResult> result;
    result.instantiate();
    result->set_outcome(error);
    result->value = listener;
    return result;
}

// Native handles and claims scoped to the originating client generation.
struct SteamSockets::State {
#ifdef CRIMSON_WITH_STEAM
    // User-facing handshake state separate from the Steam wire state.
    enum Stage { OFFERED, CONNECTING, CONNECTED };

    // One exclusively owned incoming or outgoing socket.
    struct Connection {
        // Distinguishes socket handle reuse even after a stale Steam notification.
        int64_t token = 0;
        // Listen socket whose close retires this child, or zero for outgoing sockets.
        HSteamListenSocket listener = 0;
        // Local offer or handshake deadline in monotonic milliseconds.
        uint64_t deadline = 0;
        // Identity chosen by the endpoint or initially presented in the offer.
        uint64_t expected = 0;
        // Exact verified account after a successful authenticated handshake.
        uint64_t remote = 0;
        // Current public lifecycle state of the native connection.
        Stage stage = CONNECTING;
        // Direct sockets may authenticate a Steam game server as well as an individual.
        bool allow_server = false;
        // Incoming offer token while the listener may accept it.
        Ref<SteamSocketOffer> offer;
        // Outgoing operation held until one result settles.
        Ref<SteamSocketConnectOperation> operation;
        // Public connection after acceptance or authenticated outgoing completion.
        Ref<SteamSocketConnection> socket;
        // Independent quotas for native incoming and outgoing messages.
        Traffic incoming;
        Traffic outgoing;
    };

    // One active native listener.
    struct Listener {
        // Exact reference returned to the game.
        Ref<SteamSocketListener> socket;
        // Local route reported for incoming offers.
        Ref<SteamSocketEndpoint> endpoint;
        // Maximum of pending and established children.
        size_t limit = 0;
    };

    // Game-visible notifications dispatched after native resources are released.
    struct Retired {
        // Terminal outcome of this native socket.
        Error reason = ERR_UNAVAILABLE;
        // An outgoing operation that must settle on retirement, if any.
        Ref<SteamSocketConnectOperation> operation;
        // An already issued connection that must emit closed, if any.
        Ref<SteamSocketConnection> socket;

        // Publish exactly the operation or connection associated with this retirement.
        void notify() const {
            if (operation.is_valid()) {
                operation->finish(SteamSocketConnectionResult::create(reason));
            }
            if (socket.is_valid()) {
                socket->emit_signal("closed", reason);
            }
        }
    };

    // Runtime is held by SteamClient while this generation owns sockets.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact generation used to invalidate script-facing objects on client stop.
    std::weak_ptr<SteamClientScope> scope;
    // Identity of the facade that owns these entries.
    ObjectID owner;
    // Single native subscription for this facade's owned connection callbacks.
    uint64_t subscription = 0;
    // Exact native listeners indexed by the SDK handles that this facade created.
    std::map<HSteamListenSocket, Listener> listeners;
    // Exact native connection claims indexed by current SDK handle.
    std::map<HSteamNetConnection, Connection> connections;

    // Resolve the SDK only while the calling thread and scope are still valid.
    std::shared_ptr<SteamRuntime> live() const {
        if (!SteamRuntime::on_main_thread()) {
            return {};
        }
        const auto generation = scope.lock();
        return generation && generation->active ? runtime.lock() : nullptr;
    }

    // Install the callback before creating the first listener or connection.
    bool observe(const std::shared_ptr<SteamRuntime> &active);
    // Claim one SDK handle and set a process-unique lifetime token.
    Connection *claim(ISteamNetworkingSockets &sdk, HSteamNetConnection handle, Connection initial);
    // Read the live connection info only if its exact token and listener still match.
    bool read(ISteamNetworkingSockets &sdk, HSteamNetConnection handle,
            const Connection &connection, SteamNetConnectionInfo_t &info) const;
    // Resolve a public connection back to the exact owned native entry.
    Connection *find(const SteamSocketConnection *connection);
    // Validate that a still-pending offer belongs to the supplied exact listener.
    Connection *find_offer(const SteamSocketListener *listener, const Ref<SteamSocketOffer> &offer);
    // Make a public socket scoped to the current native claim.
    Ref<SteamSocketConnection> issue_socket(HSteamNetConnection handle) const;
    // Advance only a callback belonging to a current claimed handle or exact native listener.
    void on_status(const SteamNetConnectionStatusChangedCallback_t &event);
    // Recognize a connected state only after exact certificate and deadline checks.
    void connected(ISteamNetworkingSockets &sdk, HSteamNetConnection handle,
            Connection &connection, const SteamNetConnectionInfo_t &info);
    // Remove one exact connection before any pending user-visible signals.
    Retired retire(ISteamNetworkingSockets *sdk, HSteamNetConnection handle, Error reason);
    // Receive and release a bounded number of native buffers before signaling.
    void receive(ISteamNetworkingSockets &sdk, HSteamNetConnection handle, uint64_t now);
#endif
};

#ifdef CRIMSON_WITH_STEAM
bool SteamSockets::State::observe(const std::shared_ptr<SteamRuntime> &active) {
    if (!subscription) {
        subscription = active->subscribe(SteamNetConnectionStatusChangedCallback_t::k_iCallback,
                [self = owner](const void *data, size_t size) {
                    const auto service = resolve_service(self);
                    if (service.is_null() || !data || size != sizeof(SteamNetConnectionStatusChangedCallback_t)) {
                        return;
                    }
                    SteamNetConnectionStatusChangedCallback_t event{};
                    std::memcpy(&event, data, sizeof(event));
                    service->state->on_status(event);
                });
    }
    return subscription != 0;
}

SteamSockets::State::Connection *SteamSockets::State::claim(ISteamNetworkingSockets &sdk,
        HSteamNetConnection handle, Connection initial) {
    if (!handle || connections.find(handle) != connections.end() ||
            connections.size() >= max_connections || process_connections >= max_process_connections ||
            next_socket_token == (std::numeric_limits<int64_t>::max)()) {
        return nullptr;
    }
    initial.token = ++next_socket_token;
    if (!sdk.SetConnectionUserData(handle, initial.token)) {
        return nullptr;
    }
    auto inserted = connections.emplace(handle, std::move(initial));
    ++process_connections;
    return &inserted.first->second;
}

bool SteamSockets::State::read(ISteamNetworkingSockets &sdk, HSteamNetConnection handle,
        const Connection &connection, SteamNetConnectionInfo_t &info) const {
    return sdk.GetConnectionInfo(handle, &info) && info.m_nUserData == connection.token &&
            info.m_hListenSocket == connection.listener;
}

SteamSockets::State::Connection *SteamSockets::State::find(const SteamSocketConnection *connection) {
    const auto found = connections.find(connection->handle);
    return found != connections.end() && found->second.socket.ptr() == connection ? &found->second : nullptr;
}

SteamSockets::State::Connection *SteamSockets::State::find_offer(const SteamSocketListener *listener,
        const Ref<SteamSocketOffer> &offer) {
    if (offer.is_null()) {
        return nullptr;
    }
    const auto active_listener = listeners.find(listener->handle);
    const auto found = connections.find(offer->handle);
    if (active_listener == listeners.end() || active_listener->second.socket.ptr() != listener ||
            found == connections.end() || found->second.stage != OFFERED ||
            found->second.listener != listener->handle || found->second.offer != offer) {
        return nullptr;
    }
    return &found->second;
}

Ref<SteamSocketConnection> SteamSockets::State::issue_socket(HSteamNetConnection handle) const {
    Ref<SteamSocketConnection> socket;
    socket.instantiate();
    socket->handle = handle;
    socket->owner = owner;
    return socket;
}

void SteamSockets::State::on_status(const SteamNetConnectionStatusChangedCallback_t &event) {
    const auto active = live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (!sdk) {
        return;
    }
    const auto handle = event.m_hConn;
    const auto found = connections.find(handle);
    SteamNetConnectionInfo_t info{};
    if (found != connections.end()) {
        auto &connection = found->second;
        if (!read(*sdk, handle, connection, info)) {
            retire(nullptr, handle, ERR_CONNECTION_ERROR).notify();
            return;
        }
        switch (info.m_eState) {
            case k_ESteamNetworkingConnectionState_Connecting:
            case k_ESteamNetworkingConnectionState_FindingRoute:
                return;
            case k_ESteamNetworkingConnectionState_Connected:
                if (connection.stage != CONNECTED) {
                    connected(*sdk, handle, connection, info);
                }
                return;
            default:
                retire(sdk, handle, ERR_CONNECTION_ERROR).notify();
                return;
        }
    }

    const auto listener = listeners.find(event.m_info.m_hListenSocket);
    if (listener == listeners.end() || event.m_info.m_eState != k_ESteamNetworkingConnectionState_Connecting ||
            !sdk->GetConnectionInfo(handle, &info) || info.m_hListenSocket != listener->first ||
            info.m_eState != k_ESteamNetworkingConnectionState_Connecting) {
        return;
    }
    size_t children = 0;
    for (const auto &entry : connections) {
        children += entry.second.listener == listener->first;
    }
    const auto initial_account = info.m_identityRemote.GetSteamID64();
    Connection initial;
    initial.listener = listener->first;
    initial.deadline = Time::get_singleton()->get_ticks_msec() + offer_timeout_ms;
    initial.expected = initial_account;
    initial.stage = OFFERED;
    initial.allow_server = listener->second.endpoint->get_kind() == SteamSocketEndpoint::DIRECT;
    if (children >= listener->second.limit ||
            (initial_account && !valid_socket_account(initial_account, initial.allow_server))) {
        sdk->CloseConnection(handle, 0, nullptr, false);
        return;
    }
    auto *claimed = claim(*sdk, handle, std::move(initial));
    if (!claimed) {
        sdk->CloseConnection(handle, 0, nullptr, false);
        return;
    }
    Ref<SteamSocketOffer> offer;
    offer.instantiate();
    offer->handle = handle;
    offer->endpoint = listener->second.endpoint;
    offer->steam_id = account_string(initial_account);
    offer->expires_at_msec = static_cast<int64_t>(claimed->deadline);
    claimed->offer = offer;
    const auto listener_socket = listener->second.socket;
    listener_socket->emit_signal("connection_requested", offer);
}

void SteamSockets::State::connected(ISteamNetworkingSockets &sdk, HSteamNetConnection handle,
        Connection &connection, const SteamNetConnectionInfo_t &info) {
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (now >= connection.deadline) {
        retire(&sdk, handle, ERR_TIMEOUT).notify();
        return;
    }
    const uint64_t actual = info.m_identityRemote.GetSteamID64();
    if (connection.stage != CONNECTING || (info.m_nFlags & insecure_mask) ||
            !valid_socket_account(actual, connection.allow_server) ||
            (connection.expected && connection.expected != actual)) {
        retire(&sdk, handle, ERR_UNAUTHORIZED).notify();
        return;
    }
    connection.remote = actual;
    connection.stage = CONNECTED;
    if (connection.operation.is_valid()) {
        const auto operation = std::exchange(connection.operation, {});
        connection.socket = issue_socket(handle);
        operation->finish(SteamSocketConnectionResult::create(OK, connection.socket));
    } else {
        const auto socket = connection.socket;
        socket->emit_signal("connected");
    }
}

SteamSockets::State::Retired SteamSockets::State::retire(ISteamNetworkingSockets *sdk,
        HSteamNetConnection handle, Error reason) {
    auto removed = connections.extract(handle);
    if (removed.empty()) {
        return {};
    }
    --process_connections;
    auto &connection = removed.mapped();
    SteamNetConnectionInfo_t info{};
    if (sdk && read(*sdk, handle, connection, info)) {
        sdk->CloseConnection(handle, 0, nullptr, false);
    }
    if (connection.socket.is_valid()) {
        connection.socket->handle = 0;
    }
    return {reason, std::move(connection.operation), std::move(connection.socket)};
}

void SteamSockets::State::receive(ISteamNetworkingSockets &sdk, HSteamNetConnection handle, uint64_t now) {
    const auto found = connections.find(handle);
    if (found == connections.end() || found->second.stage != CONNECTED) {
        return;
    }
    auto &connection = found->second;
    SteamNetConnectionInfo_t info{};
    if (!read(sdk, handle, connection, info) || info.m_eState != k_ESteamNetworkingConnectionState_Connected ||
            (info.m_nFlags & insecure_mask) || info.m_identityRemote.GetSteamID64() != connection.remote) {
        retire(&sdk, handle, ERR_CONNECTION_ERROR).notify();
        return;
    }
    SteamNetworkingMessage_t *messages[max_messages_per_poll]{};
    const int count = sdk.ReceiveMessagesOnConnection(handle, messages, max_messages_per_poll);
    if (count < 0) {
        retire(&sdk, handle, ERR_CONNECTION_ERROR).notify();
        return;
    }
    std::array<std::pair<PackedByteArray, SteamDelivery::Mode>, max_messages_per_poll> frames;
    size_t copied = 0;
    Error failure = OK;
    for (auto *message : messages) {
        if (!message) {
            continue;
        }
        const bool reliable = (message->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
        if (message->m_conn != handle || message->m_nConnUserData != connection.token ||
                message->m_identityPeer.GetSteamID64() != connection.remote || !message->m_pData || message->m_cbSize <= 0 ||
                message->m_cbSize > max_message_bytes) {
            failure = ERR_INVALID_DATA;
        } else if (failure == OK) {
            if (connection.incoming.charge(now, message->m_cbSize)) {
                auto &frame = frames[copied++];
                if (frame.first.resize(message->m_cbSize) == OK) {
                    std::memcpy(frame.first.ptrw(), message->m_pData, message->m_cbSize);
                    frame.second = reliable ? SteamDelivery::RELIABLE : SteamDelivery::UNRELIABLE;
                } else {
                    failure = ERR_OUT_OF_MEMORY;
                }
            } else if (reliable) {
                failure = ERR_BUSY;
            }
        }
        message->Release();
    }
    if (failure != OK) {
        retire(&sdk, handle, failure).notify();
        return;
    }
    const auto socket = connection.socket;
    for (size_t index = 0; index < copied && live() && find(socket.ptr()); ++index) {
        const auto &frame = frames[index];
        socket->emit_signal("received", frame.first, frame.second);
    }
}
#endif

SteamSockets::SteamSockets() : state(std::make_unique<State>()) {}
SteamSockets::~SteamSockets() = default;

void SteamSockets::_bind_methods() {
    ClassDB::bind_method(D_METHOD("listen", "endpoint", "remote_limit"), &SteamSockets::listen, DEFVAL(8));
    ClassDB::bind_method(D_METHOD("connect_to", "endpoint", "timeout_ms"), &SteamSockets::connect_to, DEFVAL(10000));
}

void SteamSockets::attach(const std::shared_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    state->runtime = runtime;
    state->scope = scope;
    state->owner = ObjectID(get_instance_id());
#endif
}

void SteamSockets::detach() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->runtime.lock();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (active) {
        active->unsubscribe(state->subscription);
    }
    state->subscription = 0;
    state->scope.reset();
    state->runtime.reset();
    std::vector<State::Retired> notifications;
    notifications.reserve(state->connections.size());
    while (!state->connections.empty()) {
        notifications.push_back(state->retire(sdk, state->connections.begin()->first, ERR_SKIP));
    }
    auto listeners = std::exchange(state->listeners, {});
    for (const auto &entry : listeners) {
        entry.second.socket->handle = 0;
        if (sdk) {
            sdk->CloseListenSocket(entry.first);
        }
        --process_listeners;
    }
    for (const auto &notification : notifications) {
        notification.notify();
    }
#endif
}

Ref<SteamSocketListenerResult> SteamSockets::listen(const Ref<SteamSocketEndpoint> &endpoint, int64_t remote_limit) {
    if (endpoint.is_null() || remote_limit <= 0 || remote_limit > 64 ||
            (endpoint->kind == SteamSocketEndpoint::DIRECT && (endpoint->port == 0 || !endpoint->steam_id.is_empty()))) {
        return SteamSocketListenerResult::create(ERR_INVALID_PARAMETER);
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (!sdk) {
        return SteamSocketListenerResult::create(ERR_UNAVAILABLE);
    }
    if (state->listeners.size() >= max_listeners || process_listeners >= max_process_listeners) {
        return SteamSocketListenerResult::create(ERR_BUSY);
    }
    const bool p2p = endpoint->kind == SteamSocketEndpoint::P2P;
    SteamNetworkingIPAddr address{};
    if (p2p ? decimal_account(endpoint->steam_id) != active->get_user_id() : !parse_direct(active, *endpoint.ptr(), address)) {
        return SteamSocketListenerResult::create(ERR_INVALID_PARAMETER);
    }
    if (!state->observe(active)) {
        return SteamSocketListenerResult::create(ERR_BUSY);
    }
    if (p2p) {
        active->warm_relay();
    } else {
        sdk->InitAuthentication();
    }
    const auto options = socket_options(static_cast<int>(accepted_timeout_ms));
    const int count = static_cast<int>(options.size());
    const auto handle = p2p ? sdk->CreateListenSocketP2P(endpoint->port, count, options.data()) :
            sdk->CreateListenSocketIP(address, count, options.data());
    if (!handle) {
        return SteamSocketListenerResult::create(ERR_CANT_CREATE);
    }
    Ref<SteamSocketListener> listener;
    listener.instantiate();
    listener->owner = state->owner;
    listener->handle = handle;
    state->listeners.emplace(handle, State::Listener{listener, endpoint, static_cast<size_t>(remote_limit)});
    ++process_listeners;
    return SteamSocketListenerResult::create(OK, listener);
#else
    return SteamSocketListenerResult::create(ERR_UNAVAILABLE);
#endif
}

Ref<SteamSocketConnectOperation> SteamSockets::connect_to(const Ref<SteamSocketEndpoint> &endpoint, int64_t timeout_ms) {
    Ref<SteamSocketConnectOperation> operation;
    operation.instantiate();
    Error error = ERR_UNAVAILABLE;
    if (endpoint.is_null() || timeout_ms < 1000 || timeout_ms > 30000 ||
            (endpoint->kind == SteamSocketEndpoint::DIRECT && endpoint->port == 0)) {
        error = ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    else if (const auto active = state->live(); active && active->get_sockets()) {
        const bool p2p = endpoint->kind == SteamSocketEndpoint::P2P;
        const auto expected = decimal_account(endpoint->steam_id);
        SteamNetworkingIPAddr address{};
        if (p2p ? expected == active->get_user_id() :
                !parse_direct(active, *endpoint.ptr(), address) || address.IsIPv6AllZeros() ||
                        (address.IsIPv4() && (address.GetIPv4() == 0 || address.GetIPv4() >= 0xe0000000u)) ||
                        (!address.IsIPv4() && address.m_ipv6[0] == 0xff)) {
            error = ERR_INVALID_PARAMETER;
        } else if (state->connections.size() >= max_connections || process_connections >= max_process_connections ||
                !state->observe(active)) {
            error = ERR_BUSY;
        } else {
            auto &sdk = *active->get_sockets();
            SteamNetworkingIdentity identity{};
            identity.SetSteamID64(expected);
            if (p2p) {
                active->warm_relay();
            } else {
                sdk.InitAuthentication();
            }
            const auto options = socket_options(static_cast<int>(timeout_ms));
            const int count = static_cast<int>(options.size());
            const auto handle = p2p ? sdk.ConnectP2P(identity, endpoint->port, count, options.data()) :
                    sdk.ConnectByIPAddress(address, count, options.data());
            if (handle) {
                State::Connection initial;
                initial.expected = expected;
                initial.allow_server = !p2p;
                initial.deadline = Time::get_singleton()->get_ticks_msec() + static_cast<uint64_t>(timeout_ms);
                initial.operation = operation;
                if (state->claim(sdk, handle, std::move(initial))) {
                    operation->owner = state->owner;
                    operation->handle = handle;
                    return operation;
                }
                sdk.CloseConnection(handle, 0, nullptr, false);
            }
            error = ERR_CANT_CONNECT;
        }
    }
#endif
    operation->finish(SteamSocketConnectionResult::create(error));
    return operation;
}

Ref<SteamSocketConnectionResult> SteamSockets::accept(const SteamSocketListener *listener,
        const Ref<SteamSocketOffer> &offer) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (sdk) {
        auto *connection = state->find_offer(listener, offer);
        if (!connection) {
            return SteamSocketConnectionResult::create(ERR_DOES_NOT_EXIST);
        }
        const auto handle = offer->handle;
        const uint64_t now = Time::get_singleton()->get_ticks_msec();
        if (now >= connection->deadline) {
            state->retire(sdk, handle, ERR_TIMEOUT);
            return SteamSocketConnectionResult::create(ERR_TIMEOUT);
        }
        SteamNetConnectionInfo_t info{};
        if (!state->read(*sdk, handle, *connection, info) ||
                info.m_eState != k_ESteamNetworkingConnectionState_Connecting ||
                (connection->expected && info.m_identityRemote.GetSteamID64() != connection->expected) ||
                sdk->AcceptConnection(handle) != k_EResultOK) {
            state->retire(sdk, handle, ERR_CANT_CONNECT);
            return SteamSocketConnectionResult::create(ERR_CANT_CONNECT);
        }
        connection->stage = State::CONNECTING;
        connection->deadline = now + accepted_timeout_ms;
        connection->offer.unref();
        connection->socket = state->issue_socket(handle);
        return SteamSocketConnectionResult::create(OK, connection->socket);
    }
#endif
    return SteamSocketConnectionResult::create(ERR_UNAVAILABLE);
}

Error SteamSockets::reject(const SteamSocketListener *listener, const Ref<SteamSocketOffer> &offer) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (sdk) {
        const auto *connection = state->find_offer(listener, offer);
        if (!connection) {
            return ERR_DOES_NOT_EXIST;
        }
        const bool expired = Time::get_singleton()->get_ticks_msec() >= connection->deadline;
        state->retire(sdk, offer->handle, expired ? ERR_TIMEOUT : ERR_UNAUTHORIZED);
        return expired ? ERR_TIMEOUT : OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamSockets::close_listener(const SteamSocketListener *listener) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (sdk) {
        const auto handle = listener->handle;
        const auto found = state->listeners.find(handle);
        if (found == state->listeners.end() || found->second.socket.ptr() != listener) {
            return ERR_DOES_NOT_EXIST;
        }
        auto removed = state->listeners.extract(found);
        --process_listeners;
        removed.mapped().socket->handle = 0;
        std::vector<State::Retired> notifications;
        for (auto it = state->connections.begin(); it != state->connections.end();) {
            const auto current = it++;
            if (current->second.listener == handle) {
                notifications.push_back(state->retire(sdk, current->first, ERR_SKIP));
            }
        }
        sdk->CloseListenSocket(handle);
        for (const auto &notification : notifications) {
            notification.notify();
        }
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

uint64_t SteamSockets::account(const SteamSocketConnection *socket) const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    const auto *connection = sdk ? state->find(socket) : nullptr;
    SteamNetConnectionInfo_t info{};
    if (connection && connection->stage == State::CONNECTED &&
            state->read(*sdk, socket->handle, *connection, info) &&
            info.m_eState == k_ESteamNetworkingConnectionState_Connected && !(info.m_nFlags & insecure_mask) &&
            info.m_identityRemote.GetSteamID64() == connection->remote) {
        return connection->remote;
    }
#endif
    return 0;
}

Error SteamSockets::send(const SteamSocketConnection *socket, const PackedByteArray &bytes, SteamDelivery::Mode delivery) {
    if (bytes.is_empty() || bytes.size() > 32 * 1024 ||
            (delivery != SteamDelivery::RELIABLE && delivery != SteamDelivery::UNRELIABLE)) {
        return ERR_INVALID_PARAMETER;
    }
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    auto *connection = sdk ? state->find(socket) : nullptr;
    if (connection && account(socket)) {
        if (!connection->outgoing.charge(Time::get_singleton()->get_ticks_msec(), static_cast<int>(bytes.size()))) {
            return ERR_BUSY;
        }
        const int flags = delivery == SteamDelivery::RELIABLE ? k_nSteamNetworkingSend_Reliable :
                k_nSteamNetworkingSend_UnreliableNoDelay;
        const auto sent = sdk->SendMessageToConnection(socket->handle, bytes.ptr(),
                static_cast<uint32_t>(bytes.size()), flags, nullptr);
        if (sent == k_EResultOK) {
            return OK;
        }
        return sent == k_EResultLimitExceeded || sent == k_EResultIgnored ? ERR_BUSY : ERR_CONNECTION_ERROR;
    }
#endif
    return ERR_UNAVAILABLE;
}

Ref<SteamConnectionPath> SteamSockets::path(const SteamSocketConnection *socket) const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    const auto *connection = sdk ? state->find(socket) : nullptr;
    if (connection && connection->stage == State::CONNECTED) {
        return sample_steam_path(*sdk, socket->handle, connection->token, connection->remote);
    }
#endif
    return {};
}

Error SteamSockets::close_connection(const SteamSocketConnection *socket) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (sdk) {
        if (!state->find(socket)) {
            return ERR_DOES_NOT_EXIST;
        }
        state->retire(sdk, socket->handle, OK).notify();
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamSockets::cancel(const SteamSocketConnectOperation *operation) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (sdk) {
        const auto found = state->connections.find(operation->handle);
        if (found == state->connections.end() || found->second.operation.ptr() != operation) {
            return ERR_DOES_NOT_EXIST;
        }
        state->retire(sdk, operation->handle, ERR_SKIP).notify();
        return OK;
    }
#endif
    return ERR_UNAVAILABLE;
}

void SteamSockets::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = state->live();
    auto *sdk = active ? active->get_sockets() : nullptr;
    if (!sdk || state->connections.empty()) {
        return;
    }
    const auto now = Time::get_singleton()->get_ticks_msec();
    std::vector<std::pair<HSteamNetConnection, int64_t>> handles;
    handles.reserve(state->connections.size());
    for (const auto &entry : state->connections) {
        handles.emplace_back(entry.first, entry.second.token);
    }
    for (const auto &[handle, token] : handles) {
        if (!state->live()) {
            break;
        }
        const auto found = state->connections.find(handle);
        if (found == state->connections.end() || found->second.token != token) {
            continue;
        }
        const auto &connection = found->second;
        if (connection.stage == State::CONNECTED) {
            state->receive(*sdk, handle, now);
        } else if (now >= connection.deadline) {
            state->retire(sdk, handle, ERR_TIMEOUT).notify();
        }
    }
#endif
}

} // namespace crimson::steam
