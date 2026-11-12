// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_delivery.hpp"
#include "modules/steam/client/steam_networking.hpp"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <memory>

namespace crimson::steam {

class SteamSockets;
class SteamSocketListener;

// Immutable route for a standalone Steam socket.
class SteamSocketEndpoint : public godot::RefCounted {
    GDCLASS(SteamSocketEndpoint, godot::RefCounted)

public:
    // Connection addressing policy.
    enum Kind {
        DIRECT, // Numeric IPv4 or IPv6 and a physical UDP port.
        P2P, // Steam identity and a logical Steam virtual port.
    };

    // Create a direct route to a numeric address and physical UDP port.
    static godot::Ref<SteamSocketEndpoint> direct(const godot::String &address, int64_t port);
    // Create a P2P route to an individual Steam account and virtual port.
    static godot::Ref<SteamSocketEndpoint> p2p(const godot::String &steam_id, int64_t virtual_port = 0);
    // Branch a direct route with the expected certified Steam account.
    godot::Ref<SteamSocketEndpoint> expect_peer(const godot::String &steam_id) const;
    // Return the route kind.
    Kind get_kind() const;
    // Return the explicitly selected direct address, or an empty string for P2P.
    godot::String get_address() const;
    // Return the physical or virtual port according to the route kind.
    int32_t get_port() const;
    // Return the P2P target or optional pinned direct identity.
    godot::String get_steam_id() const;

protected:
    // Register the immutable route factories and properties.
    static void _bind_methods();

private:
    friend class SteamSockets;
    // Route kind chosen by its factory.
    Kind kind = DIRECT;
    // Numeric local or remote address explicitly supplied by the game.
    godot::String address;
    // Selected physical or virtual port. Direct port zero is unconfigured.
    int32_t port = 0;
    // Canonical expected account when configured.
    godot::String steam_id;
};

// A short-lived incoming attempt observed by exactly one listener.
class SteamSocketOffer : public godot::RefCounted {
    GDCLASS(SteamSocketOffer, godot::RefCounted)

public:
    // Return the local endpoint that received this offer.
    godot::Ref<SteamSocketEndpoint> get_endpoint() const;
    // Return Steam's initial account claim, if present.
    godot::String get_steam_id() const;
    // Return the monotonic deadline in milliseconds.
    int64_t get_expires_at_msec() const;

protected:
    // Register the detached offer properties.
    static void _bind_methods();

private:
    friend class SteamSockets;
    // Local endpoint for this offer, without a remote IP address.
    godot::Ref<SteamSocketEndpoint> endpoint;
    // Steam account presented during the incoming handshake.
    godot::String steam_id;
    // Deadline relative to Godot's monotonic clock.
    int64_t expires_at_msec = 0;
    // Native handle resolved only against the owning listener's exact entry.
    uint32_t handle = 0;
};

// A standalone socket owned by one client scope.
class SteamSocketConnection : public godot::RefCounted {
    GDCLASS(SteamSocketConnection, godot::RefCounted)

public:
    // Return whether the exact live socket passed Steam identity and encryption checks.
    bool is_connected() const;
    // Return the certified remote account while connected.
    godot::String get_steam_id() const;
    // Queue one message of at most 32 KiB on the authenticated socket.
    godot::Error send(const godot::PackedByteArray &bytes, SteamDelivery::Mode delivery = SteamDelivery::RELIABLE);
    // Read the current authenticated socket path.
    godot::Ref<SteamConnectionPath> path() const;
    // Close the owned socket, including a handshake still in progress.
    godot::Error close();

protected:
    // Register send/path/close and owned connection events.
    static void _bind_methods();

private:
    friend class SteamSockets;
    // Weak identity of the issuing service generation.
    godot::ObjectID owner;
    // Native handle resolved only against the exact owning facade.
    uint32_t handle = 0;
};

// Typed result for an accepted incoming attempt or a completed outgoing handshake.
class SteamSocketConnectionResult : public SteamResult {
    GDCLASS(SteamSocketConnectionResult, SteamResult)

public:
    // Return the connection handle when available.
    godot::Ref<SteamSocketConnection> get_value() const;

protected:
    // Register the read-only connection payload.
    static void _bind_methods();

private:
    friend class SteamSockets;
    friend class SteamSocketListener;
    // Create one settled result for a connection operation.
    static godot::Ref<SteamSocketConnectionResult> create(godot::Error error,
            const godot::Ref<SteamSocketConnection> &connection = {});
    // Exact connection owned by the requesting client generation.
    godot::Ref<SteamSocketConnection> value;
};

// Retained observation of one outgoing socket handshake.
class SteamSocketConnectOperation : public godot::RefCounted {
    GDCLASS(SteamSocketConnectOperation, godot::RefCounted)

public:
    // Return whether the operation has a terminal result.
    bool is_finished() const;
    // Return the terminal result, or null while pending.
    godot::Ref<SteamSocketConnectionResult> result() const;
    // Await settlement or read the already completed result.
    godot::Variant wait();
    // Cancel the exact handshake and retire its native connection.
    godot::Error cancel();

protected:
    // Register the retained operation and typed completion signal.
    static void _bind_methods();

private:
    friend class SteamSockets;
    // Publish this operation's terminal result once.
    void finish(const godot::Ref<SteamSocketConnectionResult> &result);
    // Weak identity of the originating service generation.
    godot::ObjectID owner;
    // Native handle resolved only against the exact pending operation.
    uint32_t handle = 0;
    // Terminal result retained for late observers.
    godot::Ref<SteamSocketConnectionResult> terminal_result;
};

// A standalone Steam listener that explicitly accepts or denies pending offers.
class SteamSocketListener : public godot::RefCounted {
    GDCLASS(SteamSocketListener, godot::RefCounted)

public:
    // Accept the exact incoming offer before its deadline.
    godot::Ref<SteamSocketConnectionResult> accept(const godot::Ref<SteamSocketOffer> &offer);
    // Reject the exact incoming offer.
    godot::Error reject(const godot::Ref<SteamSocketOffer> &offer);
    // Close this listener and every socket it accepted.
    godot::Error close();

protected:
    // Register explicit offer and listener operations.
    static void _bind_methods();

private:
    friend class SteamSockets;
    // Weak identity of the issuing service generation.
    godot::ObjectID owner;
    // Native handle resolved only against the owning listener entry.
    uint32_t handle = 0;
};

// Typed result of opening a standalone Steam listener.
class SteamSocketListenerResult : public SteamResult {
    GDCLASS(SteamSocketListenerResult, SteamResult)

public:
    // Return the opened listener when available.
    godot::Ref<SteamSocketListener> get_value() const;

protected:
    // Register the read-only listener payload.
    static void _bind_methods();

private:
    friend class SteamSockets;
    // Construct a typed result for a validated listen action.
    static godot::Ref<SteamSocketListenerResult> create(godot::Error error,
            const godot::Ref<SteamSocketListener> &listener = {});
    // Exact listener owned by the requesting client generation.
    godot::Ref<SteamSocketListener> value;
};

// Owns standalone Steam Networking Sockets on the existing client callback pipe.
class SteamSockets : public godot::RefCounted {
    GDCLASS(SteamSockets, godot::RefCounted)

public:
    // Initialize a detached service with no native sockets.
    SteamSockets();
    // Release internal storage after the owning client retires its scope.
    ~SteamSockets() override;
    // Open a listener with an explicit maximum of pending and accepted connections.
    godot::Ref<SteamSocketListenerResult> listen(const godot::Ref<SteamSocketEndpoint> &endpoint,
            int64_t remote_limit = 8);
    // Begin a certificate-checked outgoing handshake with a bounded deadline.
    godot::Ref<SteamSocketConnectOperation> connect_to(const godot::Ref<SteamSocketEndpoint> &endpoint,
            int64_t timeout_ms = 10000);

    // Attach this service to the client's exact runtime generation.
    void attach(const std::shared_ptr<SteamRuntime> &runtime, const std::shared_ptr<SteamClientScope> &scope);
    // Retire all owned native sockets before emitting any completion or closed event.
    void detach();
    // Expire offers and handshakes, then dispatch a bounded number of copied messages.
    void tick();

protected:
    // Register the public standalone listener and connection actions.
    static void _bind_methods();

private:
    friend class SteamSocketListener;
    friend class SteamSocketConnection;
    friend class SteamSocketConnectOperation;
    struct State;

    // Accept an offer owned by this exact listener and socket entry.
    godot::Ref<SteamSocketConnectionResult> accept(const SteamSocketListener *listener,
            const godot::Ref<SteamSocketOffer> &offer);
    // Deny an exact pending offer without affecting other connections.
    godot::Error reject(const SteamSocketListener *listener, const godot::Ref<SteamSocketOffer> &offer);
    // Close a listener and retire its exact pending and accepted connections.
    godot::Error close_listener(const SteamSocketListener *listener);
    // Return the authenticated remote Steam identity for this exact live socket.
    uint64_t account(const SteamSocketConnection *connection) const;
    // Queue bytes after checking exact ownership, authenticated state and the send budget.
    godot::Error send(const SteamSocketConnection *connection, const godot::PackedByteArray &bytes,
            SteamDelivery::Mode delivery);
    // Sample the exact current native connection.
    godot::Ref<SteamConnectionPath> path(const SteamSocketConnection *connection) const;
    // Retire a pending or connected socket owned by this connection handle.
    godot::Error close_connection(const SteamSocketConnection *connection);
    // Cancel only the outgoing connection owned by this operation.
    godot::Error cancel(const SteamSocketConnectOperation *operation);

    // Native ownership maps and weak SteamClient generation.
    std::unique_ptr<State> state;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamSocketEndpoint::Kind);
