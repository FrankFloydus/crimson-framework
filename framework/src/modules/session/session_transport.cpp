// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "session_transport.hpp"

#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::session {
namespace {

// Keep opaque adapter names small, stable, and safe to place in listing metadata.
bool valid_opaque_transport(const String &name) {
    if (name.is_empty() || name.length() > 32 || name == "enet") {
        return false;
    }
    for (int64_t index = 0; index < name.length(); ++index) {
        const char32_t ch = name[index];
        if ((ch < U'a' || ch > U'z') && (index == 0 || ((ch < U'0' || ch > U'9') && ch != U'_'))) {
            return false;
        }
    }
    return true;
}

} // namespace

void CrimsonSessionEndpoint::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("direct", "address", "port"), &CrimsonSessionEndpoint::direct);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("opaque", "transport", "route"), &CrimsonSessionEndpoint::opaque);
    ClassDB::bind_method(D_METHOD("get_address"), &CrimsonSessionEndpoint::get_address);
    ClassDB::bind_method(D_METHOD("get_port"), &CrimsonSessionEndpoint::get_port);
    ClassDB::bind_method(D_METHOD("get_transport"), &CrimsonSessionEndpoint::get_transport);
    ClassDB::bind_method(D_METHOD("get_route"), &CrimsonSessionEndpoint::get_route);
}

Ref<CrimsonSessionEndpoint> CrimsonSessionEndpoint::direct(const String &p_address, int64_t p_port) {
    const String address = p_address.strip_edges();
    if (address.is_empty() || p_port < 1 || p_port > 65535) {
        return {};
    }
    Ref<CrimsonSessionEndpoint> endpoint;
    endpoint.instantiate();
    endpoint->address = address;
    endpoint->port = p_port;
    endpoint->transport = StringName("enet");
    return endpoint;
}

Ref<CrimsonSessionEndpoint> CrimsonSessionEndpoint::opaque(const StringName &p_transport, const PackedByteArray &p_route) {
    if (!valid_opaque_transport(String(p_transport)) || p_route.is_empty() || p_route.size() > 512) {
        return {};
    }
    Ref<CrimsonSessionEndpoint> endpoint;
    endpoint.instantiate();
    endpoint->transport = p_transport;
    endpoint->route = p_route.duplicate();
    return endpoint;
}

String CrimsonSessionEndpoint::get_address() const {
    return address;
}

int64_t CrimsonSessionEndpoint::get_port() const {
    return port;
}

StringName CrimsonSessionEndpoint::get_transport() const {
    return transport;
}

PackedByteArray CrimsonSessionEndpoint::get_route() const {
    return route.duplicate();
}

void CrimsonSessionTransportResult::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("opened", "peer"), &CrimsonSessionTransportResult::opened);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("failed", "error"), &CrimsonSessionTransportResult::failed);
    ClassDB::bind_method(D_METHOD("get_error"), &CrimsonSessionTransportResult::get_error);
    ClassDB::bind_method(D_METHOD("get_peer"), &CrimsonSessionTransportResult::get_peer);
}

Ref<CrimsonSessionTransportResult> CrimsonSessionTransportResult::opened(const Ref<MultiplayerPeer> &p_peer) {
    if (p_peer.is_null()) {
        return failed(ERR_INVALID_PARAMETER);
    }
    Ref<CrimsonSessionTransportResult> result;
    result.instantiate();
    result->error = OK;
    result->peer = p_peer;
    return result;
}

Ref<CrimsonSessionTransportResult> CrimsonSessionTransportResult::failed(Error p_error) {
    Ref<CrimsonSessionTransportResult> result;
    result.instantiate();
    result->error = p_error == OK ? ERR_INVALID_PARAMETER : p_error;
    return result;
}

Error CrimsonSessionTransportResult::get_error() const {
    return error;
}

Ref<MultiplayerPeer> CrimsonSessionTransportResult::get_peer() const {
    return peer;
}

void CrimsonSessionTransport::_bind_methods() {
    GDVIRTUAL_BIND(_open_server, "port", "remote_limit", "bind_address");
    GDVIRTUAL_BIND(_open_client, "endpoint");
    GDVIRTUAL_BIND(_disconnect_after_flush, "peer", "peer_id");
}

Ref<CrimsonSessionTransportResult> CrimsonSessionTransport::open_server(int64_t port, int64_t remote_limit,
        const String &bind_address) {
    Ref<CrimsonSessionTransportResult> result;
    return GDVIRTUAL_CALL(_open_server, port, remote_limit, bind_address, result) ? result :
            CrimsonSessionTransportResult::failed(ERR_UNAVAILABLE);
}

Ref<CrimsonSessionTransportResult> CrimsonSessionTransport::open_client(const Ref<CrimsonSessionEndpoint> &endpoint) {
    Ref<CrimsonSessionTransportResult> result;
    return GDVIRTUAL_CALL(_open_client, endpoint, result) ? result : CrimsonSessionTransportResult::failed(ERR_UNAVAILABLE);
}

void CrimsonSessionTransport::disconnect_after_flush(const Ref<MultiplayerPeer> &peer, int32_t peer_id) {
    if (!GDVIRTUAL_CALL(_disconnect_after_flush, peer, peer_id)) {
        peer->disconnect_peer(peer_id, false);
    }
}

void CrimsonENetSessionTransport::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open_server", "port", "remote_limit", "bind_address"),
            &CrimsonENetSessionTransport::open_server);
    ClassDB::bind_method(D_METHOD("open_client", "endpoint"), &CrimsonENetSessionTransport::open_client);
    ClassDB::bind_method(D_METHOD("disconnect_after_flush", "peer", "peer_id"),
            &CrimsonENetSessionTransport::disconnect_after_flush);
}

Ref<CrimsonSessionTransportResult> CrimsonENetSessionTransport::open_server(int64_t port, int64_t remote_limit,
        const String &bind_address) {
    if (port < 1 || port > 65535 || remote_limit < 1 || remote_limit > 4095 ||
            (bind_address != "*" && !bind_address.is_valid_ip_address())) {
        return CrimsonSessionTransportResult::failed(ERR_INVALID_PARAMETER);
    }
    Ref<ENetMultiplayerPeer> peer;
    peer.instantiate();
    peer->set_bind_ip(bind_address);
    const Error error = peer->create_server(static_cast<int32_t>(port), static_cast<int32_t>(remote_limit));
    return error == OK ? CrimsonSessionTransportResult::opened(peer) : CrimsonSessionTransportResult::failed(error);
}

Ref<CrimsonSessionTransportResult> CrimsonENetSessionTransport::open_client(const Ref<CrimsonSessionEndpoint> &endpoint) {
    if (endpoint.is_null() || endpoint->get_transport() != StringName("enet") ||
            endpoint->get_address().is_empty() || endpoint->get_port() == 0) {
        return CrimsonSessionTransportResult::failed(ERR_INVALID_PARAMETER);
    }
    Ref<ENetMultiplayerPeer> peer;
    peer.instantiate();
    const Error error = peer->create_client(endpoint->get_address(), static_cast<int32_t>(endpoint->get_port()));
    return error == OK ? CrimsonSessionTransportResult::opened(peer) : CrimsonSessionTransportResult::failed(error);
}

void CrimsonENetSessionTransport::disconnect_after_flush(const Ref<MultiplayerPeer> &peer, int32_t peer_id) {
    const Ref<ENetMultiplayerPeer> enet = peer;
    if (enet.is_valid()) {
        const Ref<ENetPacketPeer> remote = enet->get_peer(peer_id);
        if (remote.is_valid()) {
            remote->peer_disconnect_later(0);
        }
    }
}

} // namespace crimson::session
