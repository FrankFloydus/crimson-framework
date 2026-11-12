// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_dedicated.hpp"

#include "integration/game/crimson_game.hpp"
#include "integration/online/steam/steam_online.hpp"
#include "integration/online/steam/steam_relay_coordinator.hpp"
#include "modules/steam/server/steam_inventory_verifier.hpp"
#include "modules/steam/server/steam_server_progression.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include <utility>
#include <limits>

#ifdef CRIMSON_WITH_STEAM
#include "integration/online/steam/steam_dedicated_auth.hpp"
#include "integration/online/steam/steam_transport.hpp"
#include "modules/steam/server/steam_server_route.hpp"
#include "modules/steam/server/steam_server_runtime.hpp"
#include "modules/steam/server/steam_sdr.hpp"

#include <godot_cpp/classes/crypto.hpp>
#endif

namespace crimson::integration {
using namespace godot;
using steam::SteamServer;

void SteamServerStatus::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ready"), &SteamServerStatus::is_ready);
    ClassDB::bind_method(D_METHOD("is_listed"), &SteamServerStatus::is_listed);
    ClassDB::bind_method(D_METHOD("is_advertisement_requested"), &SteamServerStatus::is_advertisement_requested);
    ClassDB::bind_method(D_METHOD("is_secure"), &SteamServerStatus::is_secure);
    ClassDB::bind_method(D_METHOD("get_authenticated_players"), &SteamServerStatus::get_authenticated_players);
    ClassDB::bind_method(D_METHOD("get_world_id"), &SteamServerStatus::get_world_id);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ready"), "", "is_ready");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "listed"), "", "is_listed");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "advertisement_requested"), "", "is_advertisement_requested");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "secure"), "", "is_secure");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "authenticated_players"), "", "get_authenticated_players");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "world_id"), "", "get_world_id");
}

bool SteamServerStatus::is_ready() const { return ready; }
bool SteamServerStatus::is_listed() const { return listed; }
bool SteamServerStatus::is_advertisement_requested() const { return listed; }
bool SteamServerStatus::is_secure() const { return secure; }
int32_t SteamServerStatus::get_authenticated_players() const { return authenticated_players; }
StringName SteamServerStatus::get_world_id() const { return world_id; }

void SteamDedicatedServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("status"), &SteamDedicatedServer::status);
    ClassDB::bind_method(D_METHOD("get_initialization_error"), &SteamDedicatedServer::get_initialization_error);
    ClassDB::bind_method(D_METHOD("inventory_verifier"), &SteamDedicatedServer::inventory_verifier);
    ClassDB::bind_method(D_METHOD("stats", "peer_id"), &SteamDedicatedServer::stats);
    // Observe changes to the exact Game's Steam certificate, world, publication and admitted count.
    ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::OBJECT, "status", PROPERTY_HINT_NONE, "",
                                              PROPERTY_USAGE_DEFAULT, "SteamServerStatus")));
}

Ref<SteamServerStatus> SteamDedicatedServer::status() const {
    const auto *os = OS::get_singleton();
    if (os && os->get_thread_caller_id() == os->get_main_thread_id()) {
        const Ref<SteamDedicatedOnlineDirectory> directory(
                Object::cast_to<SteamDedicatedOnlineDirectory>(ObjectDB::get_instance(directory_id)));
        if (directory.is_valid()) {
            return directory->status();
        }
    }
    Ref<SteamServerStatus> idle;
    idle.instantiate();
    return idle;
}

String SteamDedicatedServer::get_initialization_error() const { return initialization_error; }
Ref<steam::SteamInventoryVerifier> SteamDedicatedServer::inventory_verifier() const { return inventory_service; }

Ref<steam::SteamServerPlayerStats> SteamDedicatedServer::stats(int64_t peer_id) const {
    const auto *os = OS::get_singleton();
    if (peer_id <= 1 || peer_id > (std::numeric_limits<int32_t>::max)() || !os ||
            os->get_thread_caller_id() != os->get_main_thread_id()) {
        return {};
    }
    const Ref<SteamDedicatedOnlineDirectory> directory(
            Object::cast_to<SteamDedicatedOnlineDirectory>(ObjectDB::get_instance(directory_id)));
    return directory.is_valid() ? directory->player_stats(static_cast<int32_t>(peer_id)) : Ref<steam::SteamServerPlayerStats>();
}

void SteamDedicatedServer::notify() {
    const auto current = status();
    if (previous.is_valid() && current->ready == previous->ready && current->listed == previous->listed &&
            current->secure == previous->secure && current->authenticated_players == previous->authenticated_players &&
            current->world_id == previous->world_id) {
        return;
    }
    previous = current;
    emit_signal("status_changed", current);
}

void SteamDedicatedRuntimeNode::_bind_methods() {}

SteamDedicatedRuntimeNode::~SteamDedicatedRuntimeNode() {
#ifdef CRIMSON_WITH_STEAM
    const Ref<SteamDedicatedAuthenticator> authenticator(
            Object::cast_to<SteamDedicatedAuthenticator>(ObjectDB::get_instance(auth_id)));
    if (authenticator.is_valid()) {
        authenticator->detach_owner();
    }
#endif
}

session::GameSession *SteamDedicatedRuntimeNode::create_session(GameRuntime &game, const SteamProvider &provider,
        const Ref<SteamServer> &options, uint32_t app_id, const StringName &name_space, int32_t virtual_port,
        const Ref<SteamRelayCoordinator> &coordinator) {
    auto *session = memnew(session::GameSession);
    auto *node = memnew(SteamDedicatedRuntimeNode);
    node->set_name("SteamDedicated");
    node->set_process_mode(PROCESS_MODE_ALWAYS);
    node->set_process(true);
    node->game_id = ObjectID(game.get_instance_id());
    node->provider_id = ObjectID(provider.get_instance_id());
    node->session_id = ObjectID(session->get_instance_id());
    node->settings = options;
    node->name_space = name_space;
    node->virtual_port = virtual_port;
    node->relay_coordinator = coordinator;
#ifdef CRIMSON_WITH_STEAM
    if (options.is_valid() && !options->get_relay_profile().is_empty() &&
            (coordinator.is_null() || !coordinator->supports_host())) {
        node->startup_error = "The selected hosted Steam relay profile requires a trusted server coordinator.";
    } else {
        node->runtime = steam::SteamServerRuntime::acquire(options, app_id, name_space, node->startup_error);
    }
    if (node->runtime) {
        Ref<SteamSessionTransport> transport;
        transport.instantiate();
        Ref<SteamDedicatedAuthenticator> authenticator;
        authenticator.instantiate();
        if (transport->configure_dedicated(node->runtime, options, virtual_port) == OK &&
                authenticator->configure(node->runtime, transport, *session) &&
                session->set_transport_adapter(transport) == OK && session->set_authenticator(authenticator) == OK) {
            node->transport = transport;
            node->auth_id = ObjectID(authenticator->get_instance_id());
            node->progression = std::make_shared<steam::SteamServerProgression>(node->runtime);
        } else {
            session->set_transport_adapter({});
            node->runtime.reset();
            node->startup_error = "The Steam dedicated Session could not install its secure transport.";
        }
    }
#else
    node->startup_error = "This build has no Steamworks SDK support.";
#endif
    session->add_child(node);
    return session;
}

Ref<OnlineDirectory> SteamDedicatedRuntimeNode::create_directory(GameRuntime &game, const SteamProvider &provider,
        session::GameSession &session) {
    auto *node = Object::cast_to<SteamDedicatedRuntimeNode>(session.get_node_or_null(NodePath("SteamDedicated")));
    Ref<SteamDedicatedOnlineDirectory> directory;
    directory.instantiate();
    if (node && node->game_id == ObjectID(game.get_instance_id()) &&
            node->provider_id == ObjectID(provider.get_instance_id()) &&
            node->session_id == ObjectID(session.get_instance_id()) && !node->directory_id.is_valid()) {
        directory->attach(node, node->startup_error);
    } else {
        directory->attach(nullptr, "The persistent Session does not own this Steam dedicated attachment.");
    }
    return directory;
}

void SteamDedicatedRuntimeNode::_process(double) {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime;
    if (!sdk) {
        return;
    }
    const ObjectID expected_directory = directory_id;
    const auto stats = progression;
    const Ref<SteamDedicatedAuthenticator> authenticator(
            Object::cast_to<SteamDedicatedAuthenticator>(ObjectDB::get_instance(auth_id)));
    sdk->pump();
    if (authenticator.is_valid()) {
        authenticator->poll();
    }
    if (stats) {
        stats->tick();
    }
    const Ref<SteamDedicatedOnlineDirectory> directory(
            Object::cast_to<SteamDedicatedOnlineDirectory>(ObjectDB::get_instance(expected_directory)));
    if (directory.is_valid()) {
        directory->advance();
    }
#endif
}

void SteamDedicatedOnlineDirectory::_bind_methods() {}

SteamDedicatedOnlineDirectory::~SteamDedicatedOnlineDirectory() { retire_attachment(); }

void SteamDedicatedOnlineDirectory::attach(SteamDedicatedRuntimeNode *node, const String &error) {
    status_service.instantiate();
    status_service->inventory_service.instantiate();
    status_service->initialization_error = error;
    if (node) {
        node_id = ObjectID(node->get_instance_id());
        node->directory_id = ObjectID(get_instance_id());
        status_service->directory_id = node->directory_id;
#ifdef CRIMSON_WITH_STEAM
        const ObjectID self(get_instance_id());
        if (node->runtime) {
            status_service->inventory_service->attach(node->runtime, [self](uint64_t account) {
                const Ref<SteamDedicatedOnlineDirectory> owner(
                        Object::cast_to<SteamDedicatedOnlineDirectory>(ObjectDB::get_instance(self)));
                return owner.is_valid() ? owner->authenticated_account(account) : ObjectID();
            });
        }
#endif
    }
}

SteamDedicatedRuntimeNode *SteamDedicatedOnlineDirectory::resolve_node() const {
    if (retired) {
        return nullptr;
    }
    auto *node = Object::cast_to<SteamDedicatedRuntimeNode>(ObjectDB::get_instance(node_id));
    return node && node->directory_id == ObjectID(get_instance_id()) ? node : nullptr;
}

ObjectID SteamDedicatedOnlineDirectory::authenticated_account(uint64_t account, int32_t peer_id) const {
#ifdef CRIMSON_WITH_STEAM
    const auto *node = resolve_node();
    if (!node || account == 0 || listing_id.is_empty() || node->transport.is_null()) {
        return {};
    }
    const auto *session = Object::cast_to<session::GameSession>(ObjectDB::get_instance(node->session_id));
    if (!session || session->get_state() != session::GameSession::HOSTING) {
        return {};
    }
    const String expected = String::num_uint64(account);
    const PackedInt32Array admitted = session->get_connected_peer_ids();
    for (int64_t index = 0; index < admitted.size(); ++index) {
        const int32_t peer = admitted[index];
        if ((peer_id == 0 || peer == peer_id) && node->transport->get_remote_steam_id(peer) == account) {
            const auto identity = session->get_peer_identity(peer);
            if (identity.is_valid() && identity->get_provider() == StringName("steam") &&
                    identity->get_subject() == expected) {
                return ObjectID(identity->get_instance_id());
            }
        }
    }
#endif
    return {};
}

bool SteamDedicatedOnlineDirectory::is_game_available() const {
#ifdef CRIMSON_WITH_STEAM
    const auto *node = resolve_node();
    return node && node->is_inside_tree() && node->runtime && node->transport.is_valid();
#else
    return false;
#endif
}

bool SteamDedicatedOnlineDirectory::supports_host_mode(bool dedicated) const { return dedicated; }

void SteamDedicatedOnlineDirectory::begin_publish(const Ref<session::SessionEndpoint> &endpoint,
        const StringName &world, const Ref<OnlineDirectoryRequest> &request) {
    uint32_t advertised = 0;
    const auto *node = resolve_node();
    if (!is_game_available() || pending.is_valid() || !listing_id.is_empty() ||
            (endpoint.is_valid() && (!node->settings->get_relay_profile().is_empty() ||
                    !SteamServer::parse_ipv4(endpoint->get_address(), advertised) || advertised == 0))) {
        request->fail();
        return;
    }
    pending = request;
    world_id = world;
    public_ipv4 = advertised;
}

void SteamDedicatedOnlineDirectory::begin_resolve(const String &, const Ref<OnlineDirectoryRequest> &request) {
    request->fail();
}

void SteamDedicatedOnlineDirectory::advance() {
#ifdef CRIMSON_WITH_STEAM
    auto *node = resolve_node();
    if (!node || !node->runtime) {
        return;
    }
    const auto sdk = node->runtime;
    if (!listing_id.is_empty() && (!sdk->is_ready() || sdk->get_local_socket_id() != published_server_id)) {
        auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(node->game_id));
        if (game && game->is_inside_tree()) {
            game->leave();
        }
        return;
    }
    if (pending.is_valid()) {
        if (pending->is_cancelled()) {
            pending.unref();
            world_id = StringName();
            const auto registration = std::exchange(relay_registration, {});
            if (registration.is_valid()) {
                registration->cancel();
            }
        } else if (sdk->is_ready()) {
            if (node->settings->get_relay_profile().is_empty()) {
                advance_direct_publication(*node);
            } else {
                advance_hosted_publication(*node);
            }
        }
    }
    if (retired) {
        return;
    }
    const auto service = status_service;
    service->inventory_service->tick();
    service->notify();
#endif
}

#ifdef CRIMSON_WITH_STEAM

void SteamDedicatedOnlineDirectory::advance_direct_publication(SteamDedicatedRuntimeNode &node) {
    const auto sdk = node.runtime;
    const uint32_t configured_ipv4 = node.settings->get_bind_ipv4();
    const uint32_t address = public_ipv4 != 0 ? public_ipv4 :
            (configured_ipv4 != 0 ? configured_ipv4 : sdk->get_public_ipv4());
    if (address == 0) {
        return;
    }
    const steam::SteamServerRoute route{sdk->get_local_socket_id(), address, node.settings->get_game_port()};
    const String identifier = route.listing_id(sdk->get_app_id(), node.name_space);
    const auto request = std::exchange(pending, {});
    if (!identifier.is_empty() && sdk->publish(world_id, node.transport->get_host_remote_limit()) &&
            request->publish(identifier) == OK) {
        listing_id = identifier;
        published_server_id = route.server_id;
    } else {
        sdk->unpublish();
        world_id = StringName();
        request->fail();
    }
}

void SteamDedicatedOnlineDirectory::advance_hosted_publication(SteamDedicatedRuntimeNode &node) {
    const auto sdk = node.runtime;
    const auto coordinator = node.relay_coordinator;
    if (relay_registration.is_null()) {
        Ref<Crypto> entropy;
        entropy.instantiate();
        const auto nonce = entropy->generate_random_bytes(8);
        const steam::SteamHostedRoute route{sdk->get_local_socket_id(), static_cast<uint16_t>(node.virtual_port),
            nonce.size() == 8 ? static_cast<uint64_t>(nonce.decode_u64(0)) : 0, node.settings->get_relay_profile()};
        const auto login = steam::steam_hosted_server_login(*sdk, route.listing_id(sdk->get_app_id(), node.name_space), world_id);
        if (!login.is_empty() && coordinator.is_valid()) {
            const auto registration = SteamRelayRequest::create(SteamRelayRequest::REGISTER, route, sdk->get_app_id(),
                    node.name_space, *coordinator.ptr());
            if (registration.is_valid()) {
                registration->set_server_login(world_id, login);
                relay_registration = registration;
                coordinator->register_server(registration);
                return;
            }
        }
        const auto request = std::exchange(pending, {});
        world_id = StringName();
        request->fail();
        return;
    }
    if (!relay_registration->is_finished()) {
        return;
    }
    const auto registration = std::exchange(relay_registration, {});
    const auto request = std::exchange(pending, {});
    const String identifier = registration->get_listing_id();
    const auto route = steam::SteamHostedRoute::from_listing(identifier, sdk->get_app_id(), node.name_space);
    const auto endpoint = route.endpoint();
    const bool acknowledged = registration->take_registration();
    if (acknowledged && endpoint.is_valid() && route.server_id == sdk->get_local_socket_id() &&
            sdk->publish(world_id, node.transport->get_host_remote_limit()) && request->publish(identifier) == OK) {
        listing_id = identifier;
        published_server_id = route.server_id;
        node.transport->set_hosted_endpoint(endpoint);
        return;
    }
    sdk->unpublish();
    world_id = StringName();
    request->fail();
    if (acknowledged && coordinator.is_valid()) {
        coordinator->withdraw(identifier);
    }
}

#endif

void SteamDedicatedOnlineDirectory::remove_listing(const String &listing) {
    if (listing_id.is_empty() || listing_id != listing) {
        return;
    }
    listing_id = String();
    published_server_id = 0;
    world_id = StringName();
#ifdef CRIMSON_WITH_STEAM
    if (auto *node = resolve_node()) {
        const auto coordinator = node->relay_coordinator;
        node->runtime->unpublish();
        node->transport->set_hosted_endpoint({});
        const Ref<SteamDedicatedAuthenticator> authenticator(
                Object::cast_to<SteamDedicatedAuthenticator>(ObjectDB::get_instance(node->auth_id)));
        if (authenticator.is_valid()) {
            authenticator->retire_connections();
        }
        if (listing.begins_with("h1:") && coordinator.is_valid()) {
            coordinator->withdraw(listing);
        }
    }
#endif
}

void SteamDedicatedOnlineDirectory::retire_attachment() {
    auto *node = resolve_node();
    if (std::exchange(retired, true)) {
        return;
    }
    pending.unref();
    const auto registration = std::exchange(relay_registration, {});
    const String listing = std::exchange(listing_id, {});
    const auto service = status_service;
    if (service.is_valid()) {
        service->directory_id = ObjectID();
    }
    Ref<SteamRelayCoordinator> coordinator;
    published_server_id = 0;
    world_id = StringName();
    if (node) {
        node->directory_id = ObjectID();
        coordinator = node->relay_coordinator;
#ifdef CRIMSON_WITH_STEAM
        if (node->runtime) {
            node->runtime->unpublish();
            node->transport->set_hosted_endpoint({});
            const Ref<SteamDedicatedAuthenticator> authenticator(
                    Object::cast_to<SteamDedicatedAuthenticator>(ObjectDB::get_instance(node->auth_id)));
            if (authenticator.is_valid()) {
                authenticator->retire_connections();
            }
        }
#endif
    }
    if (registration.is_valid()) {
        registration->cancel();
    }
    if (listing.begins_with("h1:") && coordinator.is_valid()) {
        coordinator->withdraw(listing);
    }
    if (service.is_valid()) {
        service->inventory_service->close();
        service->notify();
    }
}

Ref<SteamDedicatedServer> SteamDedicatedOnlineDirectory::service() const { return status_service; }

Ref<steam::SteamServerPlayerStats> SteamDedicatedOnlineDirectory::player_stats(int32_t peer_id) const {
#ifdef CRIMSON_WITH_STEAM
    const auto *node = resolve_node();
    if (!node || !node->progression || node->transport.is_null()) {
        return {};
    }
    const uint64_t account = node->transport->get_remote_steam_id(peer_id);
    const ObjectID admitted = authenticated_account(account, peer_id);
    if (!admitted.is_valid()) {
        return {};
    }
    return node->progression->for_peer(account, admitted, [self = ObjectID(get_instance_id()), account, peer_id]() {
        const Ref<SteamDedicatedOnlineDirectory> owner(
                Object::cast_to<SteamDedicatedOnlineDirectory>(ObjectDB::get_instance(self)));
        return owner.is_valid() ? owner->authenticated_account(account, peer_id) : ObjectID();
    });
#else
    return {};
#endif
}

Ref<SteamServerStatus> SteamDedicatedOnlineDirectory::status() const {
    Ref<SteamServerStatus> snapshot;
    snapshot.instantiate();
#ifdef CRIMSON_WITH_STEAM
    const auto *node = resolve_node();
    if (node && node->runtime) {
        const auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(node->game_id));
        const auto *session = Object::cast_to<session::GameSession>(ObjectDB::get_instance(node->session_id));
        snapshot->secure = node->runtime->is_ready();
        snapshot->listed = node->runtime->is_listed();
        if (!listing_id.is_empty() && session && game) {
            snapshot->world_id = world_id;
            snapshot->ready = game->get_phase() == GameRuntime::RUNNING && session->is_accepting_connections();
            snapshot->authenticated_players = static_cast<int32_t>(session->get_connected_peer_ids().size());
        }
    }
#endif
    return snapshot;
}

} // namespace crimson::integration
