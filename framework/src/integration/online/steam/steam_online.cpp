// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_online.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

#include <limits>

#ifdef CRIMSON_WITH_STEAM
#include "integration/online/steam/steam_runtime.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>

#include <charconv>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#endif

using namespace godot;

namespace crimson::integration {
namespace {

// Keep app-specific lobby namespaces comparable across games that share a test AppID.
bool valid_namespace(const String &value) {
    if (value.is_empty() || value.length() > 64) {
        return false;
    }
    for (int64_t index = 0; index < value.length(); ++index) {
        const char32_t c = value[index];
        if (!((c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') ||
                    (c >= U'a' && c <= U'z') || c == U'_' || c == U'-')) {
            return false;
        }
    }
    return true;
}

#ifdef CRIMSON_WITH_STEAM

// The namespace is game-specific and this tag identifies the protocol version.
constexpr const char *protocol_tag = "crimson-steam-v1";
// Steam IDs are serialized as eight little-endian bytes after the session challenge.
constexpr int64_t steam_id_bytes = sizeof(uint64_t);
// Lobby membership and P2P connect notifications can arrive on different frames.
constexpr uint64_t membership_grace_ms = 2000;
// A private lobby cannot admit more than 64 Crimson remote actors.
constexpr size_t maximum_pending_members = 64;

// Format a Steam ID without going through a floating-point or Variant integer.
String id_string(uint64_t id) {
    return String(std::to_string(id).c_str());
}

// Parse a bounded decimal ID from an untrusted lobby result or local invite.
bool parse_decimal(const String &text, uint64_t &value) {
    if (text.is_empty() || text.length() > 20) {
        return false;
    }
    const CharString ascii = text.utf8();
    const char *first = ascii.get_data();
    const char *last = first + ascii.length();
    const auto parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc() && parsed.ptr == last;
}

// Check the Steam account type returned by the certified P2P transport.
bool valid_account(uint64_t id) {
    const CSteamID steam_id(id);
    return steam_id.IsValid() && steam_id.GetEAccountType() == k_EAccountTypeIndividual;
}

// Restrict listing IDs to real Steam lobby accounts.
bool valid_lobby(uint64_t id) {
    const CSteamID lobby(id);
    return lobby.IsValid() && lobby.IsLobby();
}

// Release exact local membership, including a lobby completed after the game was freed.
void leave_lobby(SteamRuntime &runtime, uint64_t lobby, bool hosting) {
    if (lobby == 0) {
        return;
    }
    if (hosting) {
        const auto close = runtime.symbol<decltype(&SteamAPI_ISteamMatchmaking_SetLobbyJoinable)>(
                "SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
        if (close) {
            close(runtime.get_matchmaking(), lobby, false);
        }
        runtime.clear_host_lobby(lobby);
    }
    const auto leave = runtime.symbol<decltype(&SteamAPI_ISteamMatchmaking_LeaveLobby)>(
            "SteamAPI_ISteamMatchmaking_LeaveLobby");
    if (leave) {
        leave(runtime.get_matchmaking(), lobby);
    }
}

// Decode a successful create result without using SDK-owned storage after dispatch.
uint64_t created_lobby(const void *data) {
    if (!data) {
        return 0;
    }
    LobbyCreated_t result{};
    std::memcpy(&result, data, sizeof(result));
    return result.m_eResult == k_EResultOK && valid_lobby(result.m_ulSteamIDLobby) ? result.m_ulSteamIDLobby : 0;
}

// Decode a successful lobby entry result.
uint64_t entered_lobby(const void *data) {
    if (!data) {
        return 0;
    }
    LobbyEnter_t result{};
    std::memcpy(&result, data, sizeof(result));
    return result.m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess &&
                    valid_lobby(result.m_ulSteamIDLobby) ? result.m_ulSteamIDLobby : 0;
}

// Read one bounded piece of untrusted lobby metadata.
String lobby_data(SteamRuntime &runtime, uint64_t lobby, const char *key) {
    const auto get = runtime.symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyData)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyData");
    const char *value = get ? get(runtime.get_matchmaking(), lobby, key) : nullptr;
    if (!value) {
        return {};
    }
    size_t length = 0;
    while (length < 128 && value[length] != '\0') {
        ++length;
    }
    return length < 128 ? String::utf8(value, static_cast<int64_t>(length)) : String();
}

#endif

} // namespace

#ifdef CRIMSON_WITH_STEAM

void CrimsonSteamRuntimeNode::_bind_methods() {}

void CrimsonSteamRuntimeNode::configure(const std::shared_ptr<SteamRuntime> &sdk,
        CrimsonSteamAuthenticator &authenticator) {
    runtime = sdk;
    authenticator_id = ObjectID(authenticator.get_instance_id());
    set_name("SteamRuntime");
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process(true);
}

CrimsonSteamRuntimeNode::~CrimsonSteamRuntimeNode() {
    if (runtime) {
        runtime->release_game();
    }
}

void CrimsonSteamRuntimeNode::_process(double) {
    const auto active = runtime;
    if (active) {
        active->pump();
        const Ref<CrimsonSteamAuthenticator> authenticator(
                Object::cast_to<CrimsonSteamAuthenticator>(ObjectDB::get_instance(authenticator_id)));
        if (authenticator.is_valid()) {
            authenticator->poll_membership();
        }
    }
}

void CrimsonSteamAuthenticator::_bind_methods() {}

void CrimsonSteamAuthenticator::configure(const std::shared_ptr<SteamRuntime> &sdk,
        const Ref<CrimsonSteamSessionTransport> &selected_transport) {
    runtime = sdk;
    transport = selected_transport;
}

PackedByteArray CrimsonSteamAuthenticator::make_proof(
        int32_t peer_id, bool, const PackedByteArray &challenge) {
    const uint64_t remote = transport->get_remote_steam_id(peer_id);
    if (!runtime || !valid_account(remote)) {
        return {};
    }
    PackedByteArray proof = challenge;
    proof.resize(challenge.size() + steam_id_bytes);
    proof.encode_u64(challenge.size(), runtime->get_user_id());
    return proof;
}

void CrimsonSteamAuthenticator::verify(const Ref<session::CrimsonSessionAuthRequest> &request) {
    const int32_t peer = request->get_peer_id();
    const uint64_t remote = transport->get_remote_steam_id(peer);
    const PackedByteArray challenge = request->get_challenge();
    const PackedByteArray proof = request->get_proof();
    if (!runtime || !valid_account(remote) || proof.size() != challenge.size() + steam_id_bytes ||
            proof.slice(0, challenge.size()) != challenge || proof.decode_u64(challenge.size()) != remote) {
        request->reject();
        return;
    }
    if (request->is_authority() && !runtime->is_host_lobby_member(remote)) {
        if (pending.size() >= maximum_pending_members && pending.find(peer) == pending.end()) {
            request->reject();
        } else {
            pending[peer] = PendingMembership{request, remote,
                Time::get_singleton()->get_ticks_msec() + membership_grace_ms};
        }
        return;
    }
    request->accept(session::CrimsonSessionIdentity::verified("steam", id_string(remote)));
}

void CrimsonSteamAuthenticator::poll_membership() {
    if (pending.empty()) {
        return;
    }
    std::vector<int32_t> peers;
    peers.reserve(pending.size());
    for (const auto &entry : pending) {
        peers.push_back(entry.first);
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    for (const int32_t peer : peers) {
        const auto found = pending.find(peer);
        if (found == pending.end()) {
            continue;
        }
        const bool connected = transport->get_remote_steam_id(peer) == found->second.account;
        const bool member = connected && runtime->is_host_lobby_member(found->second.account);
        if (connected && !member && now < found->second.deadline) {
            continue;
        }
        const PendingMembership settled = found->second;
        pending.erase(found);
        if (member) {
            settled.request->accept(session::CrimsonSessionIdentity::verified("steam", id_string(settled.account)));
        } else {
            settled.request->reject();
        }
    }
}

void CrimsonSteamOnlineDirectory::_bind_methods() {}

void CrimsonSteamOnlineDirectory::configure(const std::shared_ptr<SteamRuntime> &sdk,
        const Ref<CrimsonSteamSessionTransport> &selected_transport, const String &selected_namespace,
        int32_t selected_port, ObjectID provider) {
    runtime = sdk;
    transport = selected_transport;
    lobby_namespace = selected_namespace;
    virtual_port = selected_port;
    provider_id = provider;
    const ObjectID self(get_instance_id());
    invite_subscription = runtime->subscribe(GameLobbyJoinRequested_t::k_iCallback,
            [self](const void *data, size_t size) {
                const Ref<CrimsonSteamOnlineDirectory> directory(
                        Object::cast_to<CrimsonSteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid()) {
                    directory->invite_received(data, size);
                }
            });
}

CrimsonSteamOnlineDirectory::~CrimsonSteamOnlineDirectory() {
    if (runtime) {
        runtime->unsubscribe(invite_subscription);
        leave_lobby(*runtime, hosted, true);
        leave_lobby(*runtime, joined, false);
    }
}

void CrimsonSteamOnlineDirectory::begin_publish(const Ref<session::CrimsonSessionEndpoint> &,
        const StringName &world, const Ref<CrimsonOnlineDirectoryRequest> &request) {
    const auto create = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_CreateLobby)>(
            "SteamAPI_ISteamMatchmaking_CreateLobby");
    const int32_t remote_limit = transport->get_host_remote_limit();
    if (!create || pending || hosted != 0 || joined != 0 || transport->get_host_endpoint().is_null() ||
            remote_limit < 1 || remote_limit > 64 || world.is_empty() || !runtime->can_track_call()) {
        request->fail();
        return;
    }
    const SteamAPICall_t call = create(runtime->get_matchmaking(), k_ELobbyTypePrivate, remote_limit + 1);
    const ObjectID self(get_instance_id());
    const std::weak_ptr<SteamRuntime> sdk = runtime;
    pending = runtime->watch_call(call, LobbyCreated_t::k_iCallback, sizeof(LobbyCreated_t),
            [self, sdk, world, request](const void *data) {
                const Ref<CrimsonSteamOnlineDirectory> directory(
                        Object::cast_to<CrimsonSteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid()) {
                    directory->finish_publish(data, world, request);
                } else if (const auto active = sdk.lock()) {
                    leave_lobby(*active, created_lobby(data), true);
                }
            });
    if (!pending) {
        request->fail();
    }
}

void CrimsonSteamOnlineDirectory::finish_publish(const void *data, const StringName &world,
        const Ref<CrimsonOnlineDirectoryRequest> &request) {
    pending = false;
    const uint64_t lobby = created_lobby(data);
    if (lobby == 0) {
        request->fail();
        return;
    }
    if (request->is_cancelled() || transport->get_host_endpoint().is_null()) {
        leave_lobby(*runtime, lobby, true);
        request->fail();
        return;
    }
    const auto set_joinable = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_SetLobbyJoinable)>(
            "SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
    const auto set_data = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_SetLobbyData)>(
            "SteamAPI_ISteamMatchmaking_SetLobbyData");
    const auto owner = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyOwner)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    ISteamMatchmaking *matchmaking = runtime->get_matchmaking();
    const bool published = set_joinable && set_data && owner &&
            owner(matchmaking, lobby) == runtime->get_user_id() &&
            set_joinable(matchmaking, lobby, false) &&
            set_data(matchmaking, lobby, "crimson_protocol", protocol_tag) &&
            set_data(matchmaking, lobby, "crimson_namespace", lobby_namespace.utf8().get_data()) &&
            set_data(matchmaking, lobby, "crimson_owner", id_string(runtime->get_user_id()).utf8().get_data()) &&
            set_data(matchmaking, lobby, "crimson_port", String::num_int64(virtual_port).utf8().get_data()) &&
            set_data(matchmaking, lobby, "crimson_world", String(world).utf8().get_data()) &&
            set_joinable(matchmaking, lobby, true);
    if (!published) {
        leave_lobby(*runtime, lobby, true);
        request->fail();
        return;
    }
    hosted = lobby;
    runtime->set_host_lobby(lobby);
    if (request->publish(id_string(lobby)) != OK) {
        remove_listing(id_string(lobby));
    }
}

void CrimsonSteamOnlineDirectory::begin_resolve(const String &listing_id,
        const Ref<CrimsonOnlineDirectoryRequest> &request) {
    uint64_t lobby = 0;
    const auto join = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_JoinLobby)>(
            "SteamAPI_ISteamMatchmaking_JoinLobby");
    if (!join || pending || hosted != 0 || joined != 0 || !parse_decimal(listing_id, lobby) ||
            !valid_lobby(lobby) || !runtime->can_track_call()) {
        request->fail();
        return;
    }
    const SteamAPICall_t call = join(runtime->get_matchmaking(), lobby);
    const ObjectID self(get_instance_id());
    const std::weak_ptr<SteamRuntime> sdk = runtime;
    pending = runtime->watch_call(call, LobbyEnter_t::k_iCallback, sizeof(LobbyEnter_t),
            [self, sdk, lobby, request](const void *data) {
                const Ref<CrimsonSteamOnlineDirectory> directory(
                        Object::cast_to<CrimsonSteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid()) {
                    directory->finish_resolve(data, lobby, request);
                } else if (const auto active = sdk.lock()) {
                    leave_lobby(*active, entered_lobby(data), false);
                }
            });
    if (!pending) {
        request->fail();
    }
}

void CrimsonSteamOnlineDirectory::finish_resolve(const void *data, uint64_t expected_lobby,
        const Ref<CrimsonOnlineDirectoryRequest> &request) {
    pending = false;
    const uint64_t lobby = entered_lobby(data);
    if (lobby == 0 || lobby != expected_lobby || request->is_cancelled()) {
        leave_lobby(*runtime, lobby, false);
        request->fail();
        return;
    }
    const auto owner = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyOwner)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    const uint64_t actual_owner = owner ? owner(runtime->get_matchmaking(), lobby) : 0;
    uint64_t claimed_owner = 0;
    uint64_t port = 0;
    const bool compatible = valid_account(actual_owner) && actual_owner != runtime->get_user_id() &&
            lobby_data(*runtime, lobby, "crimson_protocol") == protocol_tag &&
            lobby_data(*runtime, lobby, "crimson_namespace") == lobby_namespace &&
            parse_decimal(lobby_data(*runtime, lobby, "crimson_owner"), claimed_owner) &&
            actual_owner == claimed_owner && parse_decimal(lobby_data(*runtime, lobby, "crimson_port"), port) &&
            port <= 65535;
    const auto endpoint = compatible ? CrimsonSteamSessionTransport::route(actual_owner, static_cast<int32_t>(port)) :
                                       Ref<session::CrimsonSessionEndpoint>();
    if (endpoint.is_null() || request->resolve(endpoint) != OK) {
        leave_lobby(*runtime, lobby, false);
        request->fail();
        return;
    }
    joined = lobby;
}

void CrimsonSteamOnlineDirectory::remove_listing(const String &listing_id) {
    uint64_t lobby = 0;
    if (parse_decimal(listing_id, lobby) && lobby != 0 && lobby == hosted) {
        leave_lobby(*runtime, std::exchange(hosted, 0), true);
    }
}

void CrimsonSteamOnlineDirectory::leave_listing(const String &listing_id) {
    uint64_t lobby = 0;
    if (parse_decimal(listing_id, lobby) && lobby != 0 && lobby == joined) {
        leave_lobby(*runtime, std::exchange(joined, 0), false);
    }
}

void CrimsonSteamOnlineDirectory::invite_received(const void *data, size_t size) {
    if (!data || size != sizeof(GameLobbyJoinRequested_t)) {
        return;
    }
    GameLobbyJoinRequested_t event{};
    std::memcpy(&event, data, sizeof(event));
    const uint64_t lobby = event.m_steamIDLobby.ConvertToUint64();
    const Ref<CrimsonSteamProvider> provider(Object::cast_to<CrimsonSteamProvider>(ObjectDB::get_instance(provider_id)));
    if (provider.is_valid() && valid_lobby(lobby)) {
        provider->emit_signal("invite_received", id_string(lobby));
    }
}

Error CrimsonSteamOnlineDirectory::open_invite_overlay() {
    const auto show = runtime->symbol<decltype(&SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog)>(
            "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
    const auto ready = runtime->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
            "SteamAPI_ISteamUtils_IsOverlayEnabled");
    if (!show || !ready || !runtime->get_utils() || hosted == 0) {
        return ERR_UNAVAILABLE;
    }
    if (!ready(runtime->get_utils())) {
        return ERR_BUSY;
    }
    show(runtime->get_friends(), hosted);
    return OK;
}

Error CrimsonSteamOnlineDirectory::invite_player(uint64_t invitee) {
    if (!valid_lobby(hosted) || !runtime || transport.is_null() || transport->get_host_endpoint().is_null()) {
        return ERR_UNCONFIGURED;
    }
    const auto owner = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyOwner)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    const auto invite = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_InviteUserToLobby)>(
            "SteamAPI_ISteamMatchmaking_InviteUserToLobby");
    ISteamMatchmaking *matchmaking = runtime->get_matchmaking();
    if (!owner || !invite || !matchmaking) {
        return ERR_UNAVAILABLE;
    }
    if (owner(matchmaking, hosted) != runtime->get_user_id()) {
        return ERR_UNAUTHORIZED;
    }
    return invite(matchmaking, hosted, invitee) ? OK : ERR_CANT_CONNECT;
}

#endif

void CrimsonSteamProvider::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_app_id", "value"), &CrimsonSteamProvider::set_app_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &CrimsonSteamProvider::get_app_id);
    ClassDB::bind_method(D_METHOD("set_virtual_port", "value"), &CrimsonSteamProvider::set_virtual_port);
    ClassDB::bind_method(D_METHOD("get_virtual_port"), &CrimsonSteamProvider::get_virtual_port);
    ClassDB::bind_method(D_METHOD("set_lobby_namespace", "value"), &CrimsonSteamProvider::set_lobby_namespace);
    ClassDB::bind_method(D_METHOD("get_lobby_namespace"), &CrimsonSteamProvider::get_lobby_namespace);
    ClassDB::bind_method(D_METHOD("is_ready"), &CrimsonSteamProvider::is_ready);
    ClassDB::bind_method(D_METHOD("get_local_steam_id"), &CrimsonSteamProvider::get_local_steam_id);
    ClassDB::bind_method(D_METHOD("get_initialization_error"), &CrimsonSteamProvider::get_initialization_error);
    ClassDB::bind_method(D_METHOD("open_invite_overlay"), &CrimsonSteamProvider::open_invite_overlay);
    ClassDB::bind_method(D_METHOD("invite_player", "steam_id"), &CrimsonSteamProvider::invite_player);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_RANGE, "0,4294967295,1"), "set_app_id", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "virtual_port", PROPERTY_HINT_RANGE, "0,65535,1"),
            "set_virtual_port", "get_virtual_port");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "lobby_namespace"), "set_lobby_namespace", "get_lobby_namespace");
    ADD_SIGNAL(MethodInfo("invite_received", PropertyInfo(Variant::STRING, "listing_id")));
}

session::CrimsonGameSession *CrimsonSteamProvider::create_session() {
#ifdef CRIMSON_WITH_STEAM
    runtime.reset();
    transport_id = ObjectID();
    directory_id = ObjectID();
    const auto sdk = SteamRuntime::acquire(static_cast<uint32_t>(app_id), initialization_error);
    if (sdk && sdk->claim_game()) {
        Ref<CrimsonSteamSessionTransport> transport;
        transport.instantiate();
        if (transport->configure(sdk, static_cast<int32_t>(virtual_port)) == OK) {
            Ref<CrimsonSteamAuthenticator> identity;
            identity.instantiate();
            identity->configure(sdk, transport);
            auto *session = memnew(session::CrimsonGameSession);
            if (session->set_transport_adapter(transport) == OK && session->set_authenticator(identity) == OK) {
                auto *pump = memnew(CrimsonSteamRuntimeNode);
                pump->configure(sdk, *identity.ptr());
                session->add_child(pump);
                runtime = sdk;
                transport_id = ObjectID(transport->get_instance_id());
                return session;
            }
            memdelete(session);
        }
        sdk->release_game();
        initialization_error = "The selected Steam transport could not be configured.";
    } else if (sdk) {
        initialization_error = "The local Steam account is already attached to a Crimson game.";
    }
#else
    initialization_error = "This Crimson build has no Steamworks SDK support.";
#endif
    return CrimsonServiceProvider::create_session();
}

Ref<CrimsonOnlineDirectory> CrimsonSteamProvider::create_online_directory() {
#ifdef CRIMSON_WITH_STEAM
    const auto sdk = runtime.lock();
    const Ref<CrimsonSteamSessionTransport> transport(
            Object::cast_to<CrimsonSteamSessionTransport>(ObjectDB::get_instance(transport_id)));
    if (sdk && transport.is_valid()) {
        Ref<CrimsonSteamOnlineDirectory> directory;
        directory.instantiate();
        directory->configure(sdk, transport, lobby_namespace, static_cast<int32_t>(virtual_port),
                ObjectID(get_instance_id()));
        directory_id = ObjectID(directory->get_instance_id());
        return directory;
    }
#endif
    return {};
}

void CrimsonSteamProvider::set_app_id(int64_t value) {
    if (value >= 0 && value <= (std::numeric_limits<uint32_t>::max)() && !is_ready()) {
        app_id = value;
    }
}

int64_t CrimsonSteamProvider::get_app_id() const {
    return app_id;
}

void CrimsonSteamProvider::set_virtual_port(int64_t value) {
    if (value >= 0 && value <= 65535 && !is_ready()) {
        virtual_port = value;
    }
}

int64_t CrimsonSteamProvider::get_virtual_port() const {
    return virtual_port;
}

void CrimsonSteamProvider::set_lobby_namespace(const String &value) {
    if (valid_namespace(value) && !is_ready()) {
        lobby_namespace = value;
    }
}

String CrimsonSteamProvider::get_lobby_namespace() const {
    return lobby_namespace;
}

bool CrimsonSteamProvider::is_ready() const {
#ifdef CRIMSON_WITH_STEAM
    return !runtime.expired() && !transport_id.is_null();
#else
    return false;
#endif
}

String CrimsonSteamProvider::get_local_steam_id() const {
#ifdef CRIMSON_WITH_STEAM
    if (const auto sdk = runtime.lock()) {
        return id_string(sdk->get_user_id());
    }
#endif
    return {};
}

String CrimsonSteamProvider::get_initialization_error() const {
    return initialization_error;
}

Error CrimsonSteamProvider::open_invite_overlay() {
#ifdef CRIMSON_WITH_STEAM
    const Ref<CrimsonSteamOnlineDirectory> directory(
            Object::cast_to<CrimsonSteamOnlineDirectory>(ObjectDB::get_instance(directory_id)));
    if (directory.is_valid()) {
        return directory->open_invite_overlay();
    }
#endif
    return ERR_UNAVAILABLE;
}

Error CrimsonSteamProvider::invite_player(const String &steam_id) {
#ifdef CRIMSON_WITH_STEAM
    const OS *os = OS::get_singleton();
    if (os->get_thread_caller_id() != os->get_main_thread_id()) {
        return ERR_BUSY;
    }
    uint64_t invitee = 0;
    if (!parse_decimal(steam_id, invitee) || !valid_account(invitee)) {
        return ERR_INVALID_PARAMETER;
    }
    const auto sdk = runtime.lock();
    if (sdk && invitee == sdk->get_user_id()) {
        return ERR_INVALID_PARAMETER;
    }
    const Ref<CrimsonSteamOnlineDirectory> directory(
            Object::cast_to<CrimsonSteamOnlineDirectory>(ObjectDB::get_instance(directory_id)));
    if (directory.is_valid()) {
        return directory->invite_player(invitee);
    }
    return ERR_UNCONFIGURED;
#else
    return ERR_UNAVAILABLE;
#endif
}

} // namespace crimson::integration
