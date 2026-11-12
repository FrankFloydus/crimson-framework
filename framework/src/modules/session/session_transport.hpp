// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace crimson::session {

// Runtime endpoint passed to a transport adapter when opening a client.
class SessionEndpoint : public godot::RefCounted {
    GDCLASS(SessionEndpoint, godot::RefCounted)

    // Direct host address, with surrounding whitespace removed.
    godot::String address;
    // Direct port, or zero on an unset endpoint.
    int64_t port = 0;
    // Stable adapter name, with enet reserved for direct addresses.
    godot::StringName transport;
    // Bounded adapter-specific route without authentication credentials.
    godot::PackedByteArray route;

protected:
    // Register direct endpoint construction and read-only access.
    static void _bind_methods();

public:
    // Construct a direct endpoint, returning null for an empty address or invalid port.
    static godot::Ref<SessionEndpoint> direct(const godot::String &p_address, int64_t p_port);
    // Construct a route for a named adapter, returning null for an invalid name or size.
    static godot::Ref<SessionEndpoint> opaque(const godot::StringName &p_transport,
            const godot::PackedByteArray &p_route);
    // Read the direct host address.
    godot::String get_address() const;
    // Read the direct port.
    int64_t get_port() const;
    // Read the adapter that understands this endpoint.
    godot::StringName get_transport() const;
    // Read the adapter-specific route bytes, empty for a direct endpoint.
    godot::PackedByteArray get_route() const;
};

// Immutable result of opening a new Godot multiplayer peer.
class SessionTransportResult : public godot::RefCounted {
    GDCLASS(SessionTransportResult, godot::RefCounted)

    // Exact opening failure, or OK with an initialized peer.
    godot::Error error = godot::ERR_UNCONFIGURED;
    // Fresh peer transferred to the session on success.
    godot::Ref<godot::MultiplayerPeer> peer;

protected:
    // Register result construction and read-only access.
    static void _bind_methods();

public:
    // Construct a successful result, rejecting null peers.
    static godot::Ref<SessionTransportResult> opened(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    // Construct a failure result, normalizing OK to ERR_INVALID_PARAMETER.
    static godot::Ref<SessionTransportResult> failed(godot::Error p_error);
    // Read the outcome of the transport open call.
    godot::Error get_error() const;
    // Read the newly initialized peer, or null on failure.
    godot::Ref<godot::MultiplayerPeer> get_peer() const;
};

// Creates fresh Godot peers while Game Session retains admission and lifecycle ownership.
class SessionTransport : public godot::Resource {
    GDCLASS(SessionTransport, godot::Resource)

protected:
    // Open a server peer with the requested remote-seat limit and bind address.
    GDVIRTUAL3R(godot::Ref<SessionTransportResult>, _open_server, int64_t, int64_t, godot::String)
    // Open a client peer for an endpoint understood by this transport.
    GDVIRTUAL1R(godot::Ref<SessionTransportResult>, _open_client, godot::Ref<SessionEndpoint>)
    // Retire an admitted guest after the peer sends already queued reliable messages.
    GDVIRTUAL2(_disconnect_after_flush, godot::Ref<godot::MultiplayerPeer>, int32_t)
    // Register the factory and graceful disconnection hooks.
    static void _bind_methods();

public:
    // Invoke the scripted server factory; an absent implementation fails explicitly.
    virtual godot::Ref<SessionTransportResult> open_server(int64_t port, int64_t remote_limit,
            const godot::String &bind_address);
    // Invoke the scripted client factory; an absent implementation fails explicitly.
    virtual godot::Ref<SessionTransportResult> open_client(const godot::Ref<SessionEndpoint> &endpoint);
    // Invoke the graceful hook or request a normal Godot disconnect if it is absent.
    virtual void disconnect_after_flush(const godot::Ref<godot::MultiplayerPeer> &peer, int32_t peer_id);
};

// Native direct UDP transport used when no adapter is selected.
class ENetSessionTransport : public SessionTransport {
    GDCLASS(ENetSessionTransport, SessionTransport)

protected:
    // Expose the built-in factories for composition by a custom adapter.
    static void _bind_methods();

public:
    // Bind and initialize a new ENet server peer.
    godot::Ref<SessionTransportResult> open_server(int64_t port, int64_t remote_limit,
            const godot::String &bind_address) override;
    // Initialize an ENet connection attempt to a direct endpoint.
    godot::Ref<SessionTransportResult> open_client(const godot::Ref<SessionEndpoint> &endpoint) override;
    // Schedule ENet's reliable queue to drain before disconnecting the guest.
    void disconnect_after_flush(const godot::Ref<godot::MultiplayerPeer> &peer, int32_t peer_id) override;
};

} // namespace crimson::session
