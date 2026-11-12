// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/session/session_transport.hpp"

#include <godot_cpp/classes/multiplayer_peer_extension.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <cstdint>
#include <memory>

namespace crimson::integration {

class SteamRuntime;

// Godot host-star peer over Steam Networking Sockets P2P connections.
class CrimsonSteamPeer : public godot::MultiplayerPeerExtension {
    GDCLASS(CrimsonSteamPeer, godot::MultiplayerPeerExtension)

    friend class CrimsonSteamSessionTransport;

    struct State;
    // Owns the listener, polling group, and authenticated physical peer mappings.
    std::unique_ptr<State> state;

    // Open an authority listener or connect to the expected Steam account and virtual port.
    godot::Error open(const std::shared_ptr<SteamRuntime> &runtime, int32_t virtual_port, int32_t remote_limit,
            uint64_t expected_authority = 0);

protected:
    // Register diagnostics available on the Godot peer.
    static void _bind_methods();

public:
    // Create a disconnected Godot peer.
    CrimsonSteamPeer();
    // Release SDK subscriptions, poll group, and all sockets before releasing the runtime.
    ~CrimsonSteamPeer() override;

    // Return the Steam ID bound to a ready, authenticated physical connection.
    uint64_t get_remote_steam_id(int32_t peer_id) const;
    // Request a Steam linger close after queued reliable messages.
    void disconnect_after_flush(int32_t peer_id);

    // Read the first complete Godot packet and retain its buffer through the next read.
    godot::Error _get_packet(const uint8_t **buffer, int32_t *size) override;
    // Send one bounded packet to the selected target with its logical mode and channel.
    godot::Error _put_packet(const uint8_t *buffer, int32_t size) override;
    // Report complete queued packets ready for Godot.
    int32_t _get_available_packet_count() const override;
    // Report the maximum Godot payload supported by this transport.
    int32_t _get_max_packet_size() const override;
    // Read the logical channel of the next queued packet.
    int32_t _get_packet_channel() const override;
    // Read the logical transfer mode of the next queued packet.
    TransferMode _get_packet_mode() const override;
    // Set the logical channel used by outgoing packets.
    void _set_transfer_channel(int32_t channel) override;
    // Read the selected outgoing channel.
    int32_t _get_transfer_channel() const override;
    // Set the logical transfer mode used by outgoing packets.
    void _set_transfer_mode(TransferMode mode) override;
    // Read the selected outgoing transfer mode.
    TransferMode _get_transfer_mode() const override;
    // Select one peer, all peers, or all peers except the absolute value.
    void _set_target_peer(int32_t peer) override;
    // Return the SDK-bound physical Godot sender of the next queued packet.
    int32_t _get_packet_peer() const override;
    // Report whether this peer owns the host listener.
    bool _is_server() const override;
    // Process bounded SDK messages and connection deadlines on the scene thread.
    void _poll() override;
    // Close the peer silently and reset its transport state.
    void _close() override;
    // Disconnect a direct remote and suppress its local signal when forced.
    void _disconnect_peer(int32_t peer_id, bool force) override;
    // Return authority ID 1 or the host-assigned guest ID.
    int32_t _get_unique_id() const override;
    // Set the host's gate for new physical connections.
    void _set_refuse_new_connections(bool enabled) override;
    // Read the host's gate for new physical connections.
    bool _is_refusing_new_connections() const override;
    // Allow SceneMultiplayer to perform logical relay on the physical host-star.
    bool _is_server_relay_supported() const override;
    // Report status after the Steam and native ID handshakes.
    ConnectionStatus _get_connection_status() const override;
};

// Native Steam adapter selected by the game-scoped Steam service provider.
class CrimsonSteamSessionTransport : public session::CrimsonSessionTransport {
    GDCLASS(CrimsonSteamSessionTransport, session::CrimsonSessionTransport)

    // Scene-thread Steam runtime shared by the provider and active sockets.
    std::shared_ptr<SteamRuntime> runtime;
    // P2P virtual port of this adapter's next listener.
    int32_t virtual_port = 0;
    // Weak reference to the only active physical peer created by this adapter.
    godot::ObjectID active_peer;

    // Resolve the current transport without extending its lifetime.
    CrimsonSteamPeer *get_active_peer() const;

protected:
    // Register Steam route creation and listener and identity diagnostics.
    static void _bind_methods();

public:
    // Configure the signed-in Steam runtime and listener's virtual port before opening a peer.
    godot::Error configure(const std::shared_ptr<SteamRuntime> &p_runtime, int32_t p_virtual_port = 0);
    // Construct a typed Steam endpoint using the expected authority ID and virtual port.
    static godot::Ref<session::CrimsonSessionEndpoint> route(uint64_t owner_id, int32_t virtual_port);
    // Return the endpoint of the currently open authority, if any.
    godot::Ref<session::CrimsonSessionEndpoint> get_host_endpoint() const;
    // Return the current authority's physical remote-seat bound, or zero.
    int32_t get_host_remote_limit() const;
    // Resolve the provider-verified physical Steam account for a ready peer.
    uint64_t get_remote_steam_id(int32_t peer_id) const;
    // Listen on the configured Steam virtual port using the requested remote-seat limit.
    godot::Ref<session::CrimsonSessionTransportResult> open_server(int64_t port, int64_t remote_limit,
            const godot::String &bind_address) override;
    // Connect to the Steam account and virtual port in a typed endpoint.
    godot::Ref<session::CrimsonSessionTransportResult> open_client(
            const godot::Ref<session::CrimsonSessionEndpoint> &endpoint) override;
    // Close a current peer with Steam's best-effort reliable linger.
    void disconnect_after_flush(const godot::Ref<godot::MultiplayerPeer> &peer, int32_t peer_id) override;
};

} // namespace crimson::integration
