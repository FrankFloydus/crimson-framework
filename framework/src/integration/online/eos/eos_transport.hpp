// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/session/session_transport.hpp"

#include <godot_cpp/classes/multiplayer_peer_extension.hpp>
#include <godot_cpp/core/object.hpp>
#include <memory>

namespace crimson::integration {

class EosPlatform;

// Godot peer over one scoped EOS P2P socket with a host-star topology.
class CrimsonEOSPeer : public godot::MultiplayerPeerExtension {
    GDCLASS(CrimsonEOSPeer, godot::MultiplayerPeerExtension)

    friend class CrimsonEOSSessionTransport;

    struct State;
    // Owns one socket and the associated EOS peer mappings.
    std::unique_ptr<State> state;

    // Open a listener or begin connecting to a specific EOS authority.
    godot::Error open(const std::shared_ptr<EosPlatform> &platform, const godot::String &local_user,
            const godot::String &socket, const godot::String &remote_host, int32_t remote_limit, bool force_relay);

protected:
    // Register read-only connection diagnostics.
    static void _bind_methods();

public:
    // Create a disconnected peer.
    CrimsonEOSPeer();
    // Remove notifications and close physical connections before releasing the platform.
    ~CrimsonEOSPeer() override;

    // Return the physical connection's authenticated EOS Product User ID.
    godot::String get_remote_product_user_id(int32_t peer_id) const;
    // Return the observed SDK path as direct, relay, or unknown.
    godot::StringName get_peer_path(int32_t peer_id) const;
    // Drain the reliable channel zero before closing the specified guest.
    void disconnect_after_flush(int32_t peer_id);

    // Read the next packet and keep its bytes alive until the following read.
    godot::Error _get_packet(const uint8_t **buffer, int32_t *size) override;
    // Send one bounded Godot packet using the selected target, mode, and channel.
    godot::Error _put_packet(const uint8_t *buffer, int32_t size) override;
    // Return the number of complete packets ready for Godot.
    int32_t _get_available_packet_count() const override;
    // Return the largest supported reliable Godot packet.
    int32_t _get_max_packet_size() const override;
    // Return the Godot channel of the next available packet.
    int32_t _get_packet_channel() const override;
    // Return the Godot mode of the next available packet.
    TransferMode _get_packet_mode() const override;
    // Set the logical Godot channel for outgoing packets.
    void _set_transfer_channel(int32_t channel) override;
    // Return the selected outgoing channel.
    int32_t _get_transfer_channel() const override;
    // Set the delivery mode for outgoing packets.
    void _set_transfer_mode(TransferMode mode) override;
    // Return the selected outgoing delivery mode.
    TransferMode _get_transfer_mode() const override;
    // Select a peer, all peers, or all peers except the absolute value.
    void _set_target_peer(int32_t peer) override;
    // Return the physical Godot sender of the next available packet.
    int32_t _get_packet_peer() const override;
    // Report whether this peer owns the authority.
    bool _is_server() const override;
    // Process bounded incoming datagrams and connection deadlines.
    void _poll() override;
    // Close the peer without emitting per-peer disconnection signals.
    void _close() override;
    // Remove a physical connection immediately or after reliable delivery.
    void _disconnect_peer(int32_t peer, bool force) override;
    // Return ID 1 on authority and the assigned ID after client handshake.
    int32_t _get_unique_id() const override;
    // Change the server admission gate for new physical connections.
    void _set_refuse_new_connections(bool enable) override;
    // Report the current physical admission gate.
    bool _is_refusing_new_connections() const override;
    // Let SceneMultiplayer manage logical guest relay over the star.
    bool _is_server_relay_supported() const override;
    // Report Godot connection readiness.
    ConnectionStatus _get_connection_status() const override;
};

// Native adapter used by an EOS provider after Connect sign-in.
class CrimsonEOSSessionTransport : public session::CrimsonSessionTransport {
    GDCLASS(CrimsonEOSSessionTransport, session::CrimsonSessionTransport)

    // Provider-owned platform, shared with any live peers it creates.
    std::shared_ptr<EosPlatform> platform;
    // Logged-in local EOS Product User ID, never used as an admission proof.
    godot::String local_user;
    // Prefix for each newly generated match listener socket.
    godot::String socket_prefix;
    // Force Epic relay for a deliberate provider configuration or live test.
    bool force_relay = false;
    // Weak connection reference used only by the matching online provider.
    godot::ObjectID active_peer;

    // Resolve the currently open physical peer without extending its lifetime.
    CrimsonEOSPeer *get_active_peer() const;

protected:
    // Register endpoint construction and listener route access.
    static void _bind_methods();

public:
    // Configure the adapter with a live platform and a signed-in local EOS user.
    godot::Error configure(const std::shared_ptr<EosPlatform> &p_platform, const godot::String &p_local_user,
            const godot::String &p_socket, bool p_force_relay = false);
    // Construct a typed opaque route from a host Product User ID and socket.
    static godot::Ref<session::CrimsonSessionEndpoint> route(const godot::String &host_user, const godot::String &socket);
    // Return the configured listener route to publish in an online directory.
    godot::Ref<session::CrimsonSessionEndpoint> get_host_endpoint() const;
    // Return the socket of the currently open authority, empty while offline or joining.
    godot::String get_host_socket() const;
    // Return the current authority's remote-seat limit, or zero when not hosting.
    int32_t get_host_remote_limit() const;
    // Resolve a physical peer to the PUID reported by EOS before game admission.
    godot::String get_remote_product_user_id(int32_t peer_id) const;
    // Listen over EOS with the Game Session remote-seat limit.
    godot::Ref<session::CrimsonSessionTransportResult> open_server(int64_t port, int64_t remote_limit,
            const godot::String &bind_address) override;
    // Open the EOS socket advertised by the resolved endpoint.
    godot::Ref<session::CrimsonSessionTransportResult> open_client(
            const godot::Ref<session::CrimsonSessionEndpoint> &endpoint) override;
    // Close after the already queued reliable admission denial is delivered.
    void disconnect_after_flush(const godot::Ref<godot::MultiplayerPeer> &peer, int32_t peer_id) override;
};

} // namespace crimson::integration
