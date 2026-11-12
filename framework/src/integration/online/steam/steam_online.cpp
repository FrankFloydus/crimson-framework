// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/steam/steam_online.hpp"
#include "integration/game/crimson_game.hpp"
#include "integration/online/steam/steam_dedicated.hpp"
#include "integration/online/steam/steam_relay_coordinator.hpp"
#include "modules/steam/client/steam_client.hpp"
#include "modules/steam/client/steam_messages.hpp"
#include "modules/steam/client/steam_parties.hpp"
#include "modules/steam/client/steam_presence.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

#include <limits>
#include <utility>

#ifdef CRIMSON_WITH_STEAM
#include "integration/online/steam/steam_dedicated_auth.hpp"
#include "modules/steam/client/steam_ids.hpp"
#include "modules/steam/lobbies/steam_lobby_lease.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"
#include "modules/steam/server/steam_server_route.hpp"
#include "modules/steam/server/steam_sdr.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <charconv>
#include <cstring>
#include <string>
#include <vector>
#endif

using namespace godot;

namespace crimson::integration {

#ifdef CRIMSON_WITH_STEAM
using steam::SteamRuntime;
using steam::SteamLobbyLease;

// The single integrated game lease for the local Steam client and its private lobby.
class SteamGameScope {
public:
    // Keep the client SDK alive for the attached game and its current requests.
    std::shared_ptr<SteamRuntime> runtime;
    // Exact persistent Game owner captured with the Session.
    ObjectID owner_id;
    // Resource which configured this Session.
    ObjectID provider_id;
    // Immutable matchmaking namespace for the mounted Session.
    String lobby_namespace;
    // Channels approved by the provider settings for this exact Game.
    TypedArray<StringName> message_channels;
    // Immutable virtual port for the mounted Session.
    int32_t virtual_port = 0;
    // Immutable publication visibility for the mounted Session.
    SteamProvider::Visibility visibility = SteamProvider::PRIVATE;
    // Backend selected when the exact Session is created.
    Ref<steam::SteamCommerceBackend> commerce_backend;
    // Trusted external coordinator pinned to this persistent Session.
    Ref<SteamRelayCoordinator> relay_coordinator;
    // Transport chosen once when this persistent Session was created.
    Ref<SteamSessionTransport> transport;
    // Diagnostic retained by a failed Steam setup.
    String startup_error;
    // Whether launch arguments were consumed for this persistent Session.
    bool startup_invite_read = false;
    // Exact hosted lobby eligible to admit transport-certified guests.
    uint64_t hosted_lobby = 0;
    // App ticket issued for the Steam authority selected by this Game.
    std::shared_ptr<SteamClientTicket> client_ticket;
    // Exact listen ticket observer owned by the persistent Session.
    std::weak_ptr<SteamTicketVerifier> listen_verifier;
    // Exact live attachment that owns Rich Presence and invite routing.
    ObjectID directory_id;

    // Check the bounded SDK roster for the actual authenticated Steam account.
    bool has_lobby_member(uint64_t account) const;
};
#endif

namespace {

#ifdef CRIMSON_WITH_STEAM

// The namespace is game-specific and this tag identifies the protocol version.
constexpr const char *protocol_tag = steam::STEAM_GAME_LOBBY_PROTOCOL;
// Steam IDs are serialized as eight little-endian bytes after the session challenge.
constexpr int64_t steam_id_bytes = sizeof(uint64_t);
// Lobby membership and P2P connect notifications can arrive on different frames.
constexpr uint64_t membership_grace_ms = 2000;
// Steam can deliver the same join intent through different notifications in a short burst.
constexpr uint64_t invite_deduplication_ms = 2000;
// A private lobby cannot admit more than 64 Crimson remote actors.
constexpr size_t maximum_pending_members = 64;
// Integrated games share one account reservation independently of standalone SDK use.
std::weak_ptr<SteamGameScope> active_steam_game;

// Check that this exact reservation has not been retired by its owning game node.
bool is_active_steam_game(const std::shared_ptr<SteamGameScope> &scope) {
    return scope && active_steam_game.lock() == scope;
}

// Reserve the Steam game role without affecting other client SDK consumers.
bool claim_steam_game(const std::shared_ptr<SteamGameScope> &scope) {
    if (!scope->runtime || !active_steam_game.expired()) {
        return false;
    }
    active_steam_game = scope;
    return true;
}

// Retire only the owning node's lease, even if an external resource retains it.
void release_steam_game(const std::shared_ptr<SteamGameScope> &scope) {
    if (is_active_steam_game(scope)) {
        scope->hosted_lobby = 0;
        active_steam_game.reset();
    }
}

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

// A syntactically valid Steam launch may carry no invitation at all.
struct LaunchLobby {
    // Canonical Steam lobby candidate, or zero if absent.
    uint64_t id = 0;
    // A recognized malformed or conflicting route blocks that launch.
    bool invalid = false;
};

// Merge one exact decimal lobby operand into an existing launch candidate.
void add_launch_lobby(LaunchLobby &selection, const String &operand) {
    uint64_t lobby = 0;
    if (!parse_decimal(operand, lobby) || !valid_lobby(lobby) || id_string(lobby) != operand ||
            (selection.id != 0 && selection.id != lobby)) {
        selection.invalid = true;
    } else {
        selection.id = lobby;
    }
}

// Recognize the exact connect string generated by Crimson for Steam Rich Presence and Party launch.
LaunchLobby steam_launch_lobby(const String &text) {
    constexpr const char *command = "+connect_lobby";
    constexpr const char *prefix = "+connect_lobby ";
    const String arguments = text.strip_edges();
    LaunchLobby selection;
    if (arguments.begins_with(prefix)) {
        add_launch_lobby(selection, arguments.trim_prefix(prefix));
    } else if (arguments == command || arguments.begins_with("+connect_lobby\t")) {
        selection.invalid = true;
    }
    return selection;
}

// Merge bounded Steam launch intent with the documented OS argument fallback.
uint64_t launch_lobby(const SteamRuntime &runtime) {
    LaunchLobby selection = steam_launch_lobby(runtime.read_launch_command_line());
    OS *os = OS::get_singleton();
    const PackedStringArray groups[] = {os->get_cmdline_args(), os->get_cmdline_user_args()};
    for (const auto &args : groups) {
        for (int64_t index = 0; index < args.size(); ++index) {
            if (args[index] != "+connect_lobby") {
                continue;
            }
            add_launch_lobby(selection, ++index < args.size() ? args[index] : String());
        }
    }
    return selection.invalid ? 0 : selection.id;
}

// Copy one bounded Steam Rich Presence value without trusting its termination.
String copy_presence_text(const char *bytes) {
    if (!bytes) {
        return {};
    }
    size_t count = 0;
    while (count < k_cchMaxRichPresenceValueLength && bytes[count] != '\0') {
        ++count;
    }
    return count < k_cchMaxRichPresenceValueLength ? String::utf8(bytes, static_cast<int64_t>(count)) : String();
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
String lobby_data(SteamRuntime &runtime, uint64_t lobby, const char *key, size_t limit = 127) {
    const auto get = runtime.symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyData)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyData");
    const char *value = get ? get(runtime.get_matchmaking(), lobby, key) : nullptr;
    if (!value) {
        return {};
    }
    size_t length = 0;
    while (length <= limit && value[length] != '\0') {
        ++length;
    }
    return length <= limit ? String::utf8(value, static_cast<int64_t>(length)) : String();
}

#endif

} // namespace

#ifdef CRIMSON_WITH_STEAM

bool SteamGameScope::has_lobby_member(uint64_t account) const {
    if (hosted_lobby == 0 || account == 0) {
        return false;
    }
    const auto count_members = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_GetNumLobbyMembers)>(
            "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    const auto member_at = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    ISteamMatchmaking *matchmaking = runtime->get_matchmaking();
    if (matchmaking->GetLobbyOwner(CSteamID(hosted_lobby)).ConvertToUint64() != runtime->get_user_id()) {
        return false;
    }
    const int count = count_members && member_at ? count_members(matchmaking, hosted_lobby) : 0;
    if (count < 1 || count > static_cast<int>(maximum_pending_members) + 1) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        if (member_at(matchmaking, hosted_lobby, index) == account) {
            return true;
        }
    }
    return false;
}

void SteamRuntimeNode::_bind_methods() {}

void SteamRuntimeNode::configure(const std::shared_ptr<SteamGameScope> &scope,
        ObjectID authenticator) {
    game_scope = scope;
    authenticator_id = authenticator;
    set_name("SteamRuntime");
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_process(true);
    if (scope->runtime) {
        const std::weak_ptr<SteamGameScope> weak_scope = scope;
        ipc_subscription = scope->runtime->subscribe(IPCFailure_t::k_iCallback,
                [weak_scope, authenticator](const void *data, size_t size) {
                    if (data && size == sizeof(IPCFailure_t)) {
                        on_ipc_failure(weak_scope.lock(), authenticator);
                    }
                });
        auto *client = memnew(steam::SteamClient);
        client->set_name("SteamClient");
        client->configure_game_lobbies(StringName(scope->lobby_namespace));
        add_child(client, false, Node::INTERNAL_MODE_FRONT);
        client_id = ObjectID(client->get_instance_id());
    }
}

const std::shared_ptr<SteamGameScope> &SteamRuntimeNode::get_game_scope() const {
    return game_scope;
}

steam::SteamClient *SteamRuntimeNode::get_client() const {
    return Object::cast_to<steam::SteamClient>(ObjectDB::get_instance(client_id));
}

SteamRuntimeNode::~SteamRuntimeNode() {
    if (game_scope && game_scope->runtime) {
        game_scope->runtime->unsubscribe(ipc_subscription);
    }
    const Ref<SteamAuthenticator> authenticator(
            Object::cast_to<SteamAuthenticator>(ObjectDB::get_instance(authenticator_id)));
    if (authenticator.is_valid()) {
        authenticator->detach_owner();
    }
    release_steam_game(game_scope);
}

void SteamRuntimeNode::on_ipc_failure(const std::shared_ptr<SteamGameScope> &scope, ObjectID authenticator_id) {
    if (!is_active_steam_game(scope) || !scope->runtime || scope->runtime->is_healthy()) {
        return;
    }
    const Ref<SteamOnlineDirectory> directory(
            Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(scope->directory_id)));
    const bool owned_peer = scope->transport.is_valid() && scope->transport->retire_client_runtime();
    const bool online = owned_peer || (directory.is_valid() &&
            (directory->pending || directory->hosted != 0 || directory->joined != 0));
    if (online) {
        auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(scope->owner_id));
        if (game) {
            const auto *current_session = game->get_node_or_null(NodePath("Session"));
            const ObjectID session_id(current_session ? current_session->get_instance_id() : 0);
            if (game->leave() != OK && owned_peer) {
                if (auto *session = Object::cast_to<session::GameSession>(ObjectDB::get_instance(session_id))) {
                    session->leave();
                }
            }
        }
    }
    if (directory.is_valid()) {
        directory->retire_after_ipc_failure();
    }
    const Ref<SteamAuthenticator> authenticator(
            Object::cast_to<SteamAuthenticator>(ObjectDB::get_instance(authenticator_id)));
    if (authenticator.is_valid()) {
        authenticator->detach_owner();
    }
    release_steam_game(scope);
}

void SteamRuntimeNode::_process(double) {
    const auto active = game_scope;
    const ObjectID authentication = authenticator_id;
    if (!is_active_steam_game(active) || !active->runtime || !active->runtime->is_healthy()) {
        return;
    }
    active->runtime->pump();
    if (!is_active_steam_game(active) || !active->runtime->is_healthy()) {
        return;
    }
    const Ref<SteamAuthenticator> authenticator(
            Object::cast_to<SteamAuthenticator>(ObjectDB::get_instance(authentication)));
    if (authenticator.is_valid()) {
        authenticator->poll_membership();
    }
    const Ref<SteamOnlineDirectory> directory(
            Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(active->directory_id)));
    if (directory.is_valid()) {
        directory->poll_join_presence();
        directory->poll_hosted_resolution();
    }
}

void SteamAuthenticator::_bind_methods() {}

SteamAuthenticator::~SteamAuthenticator() {
    detach_owner();
}

void SteamAuthenticator::detach_owner() {
    if (const auto closing = std::exchange(verifier, {})) {
        closing->detach_owner();
    }
    pending.clear();
    transport.unref();
    game_scope.reset();
}

bool SteamAuthenticator::configure(const std::shared_ptr<SteamGameScope> &scope,
        const Ref<SteamSessionTransport> &selected_transport, session::GameSession &session) {
    game_scope = scope;
    transport = selected_transport;
    const std::weak_ptr<SteamGameScope> weak = scope;
    verifier = SteamTicketVerifier::for_listen(scope->runtime, selected_transport, session, [weak](uint64_t account) {
        const auto live = weak.lock();
        return is_active_steam_game(live) && live->has_lobby_member(account);
    });
    scope->listen_verifier = verifier;
    return static_cast<bool>(verifier);
}

PackedByteArray SteamAuthenticator::make_proof(
        int32_t peer_id, bool authority, const PackedByteArray &challenge) {
    if (!is_active_steam_game(game_scope) || transport.is_null()) {
        return {};
    }
    const uint64_t remote = transport->get_remote_steam_id(peer_id);
    if (!authority) {
        const auto ticket = game_scope->client_ticket ? game_scope->client_ticket->take_proof(remote) : PackedByteArray();
        if (ticket.is_empty()) {
            return {};
        }
        PackedByteArray proof = challenge;
        proof.append_array(ticket);
        return proof;
    }
    if (!valid_account(remote)) {
        return {};
    }
    PackedByteArray proof = challenge;
    proof.resize(challenge.size() + steam_id_bytes);
    proof.encode_u64(challenge.size(), game_scope->runtime->get_user_id());
    return proof;
}

void SteamAuthenticator::verify(const Ref<session::SessionAuthRequest> &request) {
    if (!is_active_steam_game(game_scope) || !verifier || transport.is_null()) {
        request->reject();
        return;
    }
    const int32_t peer = request->get_peer_id();
    const uint64_t remote = transport->get_remote_steam_id(peer);
    const PackedByteArray challenge = request->get_challenge();
    const PackedByteArray proof = request->get_proof();
    if (!request->is_authority()) {
        if (!game_scope->client_ticket || !game_scope->client_ticket->matches(remote) ||
                proof.size() != challenge.size() + steam_id_bytes ||
                proof.slice(0, challenge.size()) != challenge || proof.decode_u64(challenge.size()) != remote) {
            request->reject();
        } else {
            request->accept(session::SessionIdentity::verified("steam", id_string(remote)));
        }
        return;
    }
    if (!valid_account(remote)) {
        request->reject();
        return;
    }
    if (!game_scope->has_lobby_member(remote)) {
        if (pending.size() >= maximum_pending_members && pending.find(peer) == pending.end()) {
            request->reject();
        } else {
            pending[peer] = PendingMembership{request, remote,
                Time::get_singleton()->get_ticks_msec() + membership_grace_ms};
        }
        return;
    }
    verifier->verify(request);
}

void SteamAuthenticator::poll_membership() {
    if (!verifier) {
        return;
    }
    verifier->poll();
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
        const bool member = connected && game_scope->has_lobby_member(found->second.account);
        if (connected && !member && now < found->second.deadline) {
            continue;
        }
        const PendingMembership settled = found->second;
        pending.erase(found);
        if (member) {
            verifier->verify(settled.request);
        } else {
            settled.request->reject();
        }
    }
}

void SteamOnlineDirectory::configure(const std::shared_ptr<SteamGameScope> &scope) {
    game_scope = scope;
    transport = scope->transport;
    lobby_namespace = scope->lobby_namespace;
    virtual_port = scope->virtual_port;
    visibility = scope->visibility;
    provider_id = scope->provider_id;
    const ObjectID self(get_instance_id());
    scope->directory_id = self;
    const auto networking = steam_scope->networking();
    if (networking.is_valid()) {
        networking->set_game_peer_source([self](int32_t peer_id) -> Ref<steam::SteamConnectionPath> {
            const Ref<SteamOnlineDirectory> directory(
                    Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
            if (directory.is_null() || !directory->is_available() || directory->transport.is_null()) {
                return {};
            }
            auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(directory->game_scope->owner_id));
            auto *game_session = game ? Object::cast_to<session::GameSession>(
                    game->get_node_or_null(NodePath("Session"))) : nullptr;
            if (!game_session || peer_id == game_session->get_local_peer_id()) {
                return {};
            }
            const auto identity = game_session->get_peer_identity(peer_id);
            uint64_t admitted = 0;
            if (identity.is_null() || identity->get_provider() != StringName("steam") ||
                    !steam::parse_steam_id(identity->get_subject(), admitted)) {
                return {};
            }
            return directory->transport->get_peer_path(peer_id, admitted);
        });
    }
    party_service = steam_scope->parties();
    if (party_service.is_valid()) {
        party_service->set_game_source([self]() {
            const Ref<SteamOnlineDirectory> directory(
                    Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
            return directory.is_valid() ? directory->party_host_state() : steam::SteamPartyHostState{};
        });
    }
    if (const auto messages = steam_scope->messages(); messages.is_valid()) {
        messages->set_game_source([self](int64_t peer) {
            const Ref<SteamOnlineDirectory> directory(
                    Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
            return directory.is_valid() ? directory->message_game_peer(peer) : steam::SteamMessageGamePeer{};
        }, StringName(scope->lobby_namespace), scope->message_channels);
    }
    if (const auto presence = steam_scope->presence(); presence.is_valid()) {
        presence->game_join_route = [self](const Ref<SteamListing> &listing) {
            const Ref<SteamOnlineDirectory> directory(
                    Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
            return directory.is_valid() ? directory->set_join_presence(listing) : ERR_UNAVAILABLE;
        };
    }
    invite_subscription = scope->runtime->subscribe(GameLobbyJoinRequested_t::k_iCallback,
            [self](const void *data, size_t size) {
                const Ref<SteamOnlineDirectory> directory(
                        Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid()) {
                    directory->invite_received(data, size);
                }
            });
    presence_subscription = scope->runtime->subscribe(GameRichPresenceJoinRequested_t::k_iCallback,
            [self](const void *data, size_t size) {
                const Ref<SteamOnlineDirectory> directory(
                        Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid()) {
                    directory->presence_join_received(data, size);
                }
            });
    launch_subscription = scope->runtime->subscribe(NewUrlLaunchParameters_t::k_iCallback,
            [self](const void *, size_t) {
                const Ref<SteamOnlineDirectory> directory(
                        Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid()) {
                    directory->launch_join_received();
                }
            });
    if (!std::exchange(scope->startup_invite_read, true)) {
        const uint64_t lobby = launch_lobby(*scope->runtime);
        if (lobby != 0) {
            invite_gateway->receive(SteamListing::lobby(lobby, scope->runtime->get_app_id(),
                    StringName(lobby_namespace)), {});
        }
    }
}

steam::SteamPartyHostState SteamOnlineDirectory::party_host_state() const {
    steam::SteamPartyHostState result;
    if (!is_available() || !hosted || !membership || membership->current() != hosted ||
            visibility == SteamProvider::PRIVATE) {
        return result;
    }
    auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(game_scope->owner_id));
    if (!game || !game->is_authority() || game->get_online_listing() != id_string(hosted)) {
        return result;
    }
    auto *game_session = Object::cast_to<session::GameSession>(game->get_node_or_null(NodePath("Session")));
    auto *world = Object::cast_to<WorldSession>(game->get_node_or_null(NodePath("Immediate")));
    const auto &runtime = game_scope->runtime;
    ISteamMatchmaking *matchmaking = runtime->get_matchmaking();
    if (!game_session || !world || !game_session->is_accepting_connections() || !matchmaking ||
            matchmaking->GetLobbyOwner(CSteamID(hosted)).ConvertToUint64() != runtime->get_user_id()) {
        return result;
    }
    result.lobby_id = id_string(hosted);
    result.name_space = StringName(lobby_namespace);
    result.free_slots = world->available_remote_seats();
    result.joinable = true;
    const auto peers = game_session->get_connected_peer_ids();
    for (int64_t index = 0; index < peers.size(); ++index) {
        const int32_t peer = peers[index];
        const auto identity = game_session->get_peer_identity(peer);
        if (identity.is_valid() && identity->get_provider() == StringName("steam")) {
            const auto account = identity->get_subject();
            result.admitted.append(account);
            if (world->is_peer_world_ready(peer)) {
                result.ready.append(account);
            }
        }
    }
    return result;
}

steam::SteamMessageGamePeer SteamOnlineDirectory::message_game_peer(int64_t peer_id) const {
    if (!is_available() || peer_id <= 0 || peer_id > (std::numeric_limits<int32_t>::max)()) {
        return {};
    }
    auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(game_scope->owner_id));
    if (!game || game->get_phase() != GameRuntime::RUNNING || game->get_world_epoch() <= 0) {
        return {};
    }
    auto *game_session = Object::cast_to<session::GameSession>(game->get_node_or_null(NodePath("Session")));
    auto *world = Object::cast_to<WorldSession>(game->get_node_or_null(NodePath("Immediate")));
    if (!game_session || !world || peer_id == game_session->get_local_peer_id()) {
        return {};
    }
    if (game->is_authority() ? !world->is_peer_world_ready(static_cast<int32_t>(peer_id)) :
            peer_id != 1 || !game->get_local_player()) {
        return {};
    }
    const auto identity = game_session->get_peer_identity(peer_id);
    if (identity.is_null() || identity->get_provider() != StringName("steam")) {
        return {};
    }
    return { identity->get_subject(), game->get_world_epoch(), ObjectID(identity->get_instance_id()) };
}

void SteamOnlineDirectory::begin_publish(const Ref<session::SessionEndpoint> &,
        const StringName &world, const Ref<OnlineDirectoryRequest> &request) {
    if (!is_available()) {
        request->fail();
        return;
    }
    const auto &runtime = game_scope->runtime;
    const auto create = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_CreateLobby)>(
            "SteamAPI_ISteamMatchmaking_CreateLobby");
    const int32_t remote_limit = transport->get_host_remote_limit();
    if (!create || pending || hosted != 0 || joined != 0 || game_scope->client_ticket ||
            transport->get_host_endpoint().is_null() ||
            remote_limit < 1 || remote_limit > 64 || !steam::valid_steam_lobby_world(String(world)) || !runtime->can_track_call()) {
        request->fail();
        return;
    }
    const auto lease = SteamLobbyLease::reserve(runtime, true);
    if (!lease) {
        request->fail();
        return;
    }
    const SteamAPICall_t call = create(runtime->get_matchmaking(), k_ELobbyTypePrivate, remote_limit + 1);
    const ObjectID self(get_instance_id());
    pending = runtime->watch_call(call, LobbyCreated_t::k_iCallback, sizeof(LobbyCreated_t),
            [self, world, request, lease](const void *data) {
                const uint64_t acquired = lease->settle(created_lobby(data)) ? lease->current() : 0;
                const Ref<SteamOnlineDirectory> directory(
                        Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid() && directory->membership == lease) {
                    directory->finish_publish(acquired, world, request);
                } else {
                    lease->retire();
                }
            });
    if (pending) {
        membership = lease;
    } else {
        lease->settle(0);
        request->fail();
    }
}

void SteamOnlineDirectory::finish_publish(uint64_t lobby, const StringName &world,
        const Ref<OnlineDirectoryRequest> &request) {
    const auto &runtime = game_scope->runtime;
    pending = false;
    if (lobby == 0) {
        retire_membership();
        request->fail();
        return;
    }
    if (!is_available() || request->is_cancelled() || transport->get_host_endpoint().is_null()) {
        retire_membership();
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
            (visibility == SteamProvider::PRIVATE ||
                    matchmaking->SetLobbyType(CSteamID(lobby), static_cast<ELobbyType>(visibility))) &&
            set_joinable(matchmaking, lobby, true);
    if (!published) {
        retire_membership();
        request->fail();
        return;
    }
    hosted = lobby;
    game_scope->hosted_lobby = lobby;
    if (request->publish(id_string(lobby)) == OK) {
        steam_scope->lobby_service->set_game_lobby(membership, SteamListing::lobby(
                lobby, runtime->get_app_id(), StringName(lobby_namespace), world));
    } else {
        remove_listing(id_string(lobby));
    }
}

void SteamOnlineDirectory::begin_resolve(const String &listing_id,
        const Ref<OnlineDirectoryRequest> &request) {
    if (!is_available()) {
        request->fail();
        return;
    }
    if (listing_id.begins_with("s1:")) {
        begin_direct_resolve(listing_id, request);
        return;
    }
    if (listing_id.begins_with("h1:")) {
        begin_hosted_resolve(listing_id, request);
        return;
    }
    const auto &runtime = game_scope->runtime;
    uint64_t lobby = 0;
    uint64_t expected_owner = 0;
    const bool valid_listing = listing_id.begins_with("party1:") ?
            steam::parse_steam_party_route(listing_id, lobby, expected_owner) :
            (parse_decimal(listing_id, lobby) && valid_lobby(lobby));
    const auto join = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_JoinLobby)>(
            "SteamAPI_ISteamMatchmaking_JoinLobby");
    if (!join || pending || hosted != 0 || joined != 0 || game_scope->client_ticket ||
            !valid_listing || !runtime->can_track_call()) {
        request->fail();
        return;
    }
    const auto lease = SteamLobbyLease::reserve(runtime, false);
    if (!lease) {
        request->fail();
        return;
    }
    const SteamAPICall_t call = join(runtime->get_matchmaking(), lobby);
    const ObjectID self(get_instance_id());
    pending = runtime->watch_call(call, LobbyEnter_t::k_iCallback, sizeof(LobbyEnter_t),
            [self, lobby, expected_owner, listing_id, request, lease](const void *data) {
                const uint64_t acquired = lease->settle(entered_lobby(data)) ? lease->current() : 0;
                const Ref<SteamOnlineDirectory> directory(
                        Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_valid() && directory->membership == lease) {
                    directory->finish_resolve(acquired, lobby, expected_owner, listing_id, request);
                } else {
                    lease->retire();
                }
            });
    if (pending) {
        membership = lease;
    } else {
        lease->settle(0);
        request->fail();
    }
}

void SteamOnlineDirectory::begin_direct_resolve(const String &listing, const Ref<OnlineDirectoryRequest> &request) {
    const auto &sdk = game_scope->runtime;
    const auto route = steam::SteamServerRoute::from_listing(listing, sdk->get_app_id(), StringName(lobby_namespace));
    if (pending || hosted != 0 || joined != 0 || game_scope->client_ticket || !route.is_valid()) {
        request->fail();
        return;
    }
    prepare_client_ticket(listing, route.server_id, route.endpoint(), request);
}

void SteamOnlineDirectory::begin_hosted_resolve(const String &listing, const Ref<OnlineDirectoryRequest> &request) {
#ifdef CRIMSON_WITH_STEAM_SDR
    const auto &sdk = game_scope->runtime;
    const auto coordinator = game_scope->relay_coordinator;
    const StringName scope(lobby_namespace);
    const auto route = steam::SteamHostedRoute::from_listing(listing, sdk->get_app_id(), scope);
    if (!pending && hosted == 0 && joined == 0 && !game_scope->client_ticket && route.is_valid() &&
            coordinator.is_valid() && coordinator->supports_client()) {
        const auto authorization = SteamRelayRequest::create(SteamRelayRequest::AUTHORIZE,
                route, sdk->get_app_id(), scope, *coordinator.ptr());
        if (authorization.is_valid()) {
            authorization->set_local_account(sdk->get_user_id());
            relay_authorization = authorization;
            relay_resolution = request;
            pending = true;
            coordinator->authorize_client(authorization);
            return;
        }
    }
#endif
    request->fail();
}

void SteamOnlineDirectory::poll_hosted_resolution() {
    if (relay_authorization.is_null()) {
        return;
    }
    if (!is_available() || relay_resolution.is_null() || relay_resolution->is_cancelled()) {
        retire_hosted_resolution();
        return;
    }
    if (!relay_authorization->is_finished()) {
        return;
    }
    const auto authorization = std::exchange(relay_authorization, {});
    const auto request = std::exchange(relay_resolution, {});
    pending = false;
    const auto &sdk = game_scope->runtime;
    const String listing = authorization->get_listing_id();
    const auto route = steam::SteamHostedRoute::from_listing(listing, sdk->get_app_id(), StringName(lobby_namespace));
    if (steam::install_steam_relay_ticket(*sdk, route, authorization->take_ticket())) {
        prepare_client_ticket(listing, route.server_id, route.endpoint(), request);
    } else {
        request->fail();
    }
}

void SteamOnlineDirectory::retire_hosted_resolution() {
    const auto request = std::exchange(relay_authorization, {});
    relay_resolution.unref();
    if (request.is_valid()) {
        pending = false;
        request->cancel();
    }
}

void SteamOnlineDirectory::prepare_client_ticket(const String &listing, uint64_t authority,
        const Ref<session::SessionEndpoint> &endpoint, const Ref<OnlineDirectoryRequest> &request) {
    if (endpoint.is_null()) {
        leave_listing(listing);
        request->fail();
        return;
    }
    const ObjectID self(get_instance_id());
    ticket_listing_id = listing;
    pending = true;
    game_scope->client_ticket = SteamClientTicket::issue(game_scope->runtime, authority,
            [self, listing, endpoint, request, authority](Error error) {
                const Ref<SteamOnlineDirectory> directory(
                        Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(self)));
                if (directory.is_null() || directory->ticket_listing_id != listing) {
                    return;
                }
                directory->pending = false;
                const bool owner_still_valid = directory->joined == 0 ||
                        directory->game_scope->runtime->get_matchmaking()->GetLobbyOwner(CSteamID(directory->joined))
                                .ConvertToUint64() == authority;
                if (error != OK || !directory->is_available() || !owner_still_valid || request->is_cancelled() ||
                        request->resolve(endpoint) != OK) {
                    directory->leave_listing(listing);
                    request->fail();
                } else if (endpoint->get_transport() == StringName("steam_hosted")) {
                    directory->transport->set_hosted_client_endpoint(endpoint);
                }
            });
    if (!game_scope->client_ticket) {
        leave_listing(listing);
        request->fail();
    }
}

void SteamOnlineDirectory::retire_client_ticket() {
    if (ticket_listing_id.is_empty()) {
        return;
    }
    pending = false;
    ticket_listing_id = String();
    transport->set_hosted_client_endpoint({});
    const auto ticket = std::exchange(game_scope->client_ticket, {});
    if (ticket) {
        ticket->cancel();
    }
}

void SteamOnlineDirectory::finish_resolve(uint64_t lobby, uint64_t expected_lobby, uint64_t expected_owner,
        const String &listing, const Ref<OnlineDirectoryRequest> &request) {
    const auto &runtime = game_scope->runtime;
    pending = false;
    if (lobby == 0 || lobby != expected_lobby || !is_available() || request->is_cancelled()) {
        retire_membership();
        request->fail();
        return;
    }
    const auto owner = runtime->symbol<decltype(&SteamAPI_ISteamMatchmaking_GetLobbyOwner)>(
            "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    const uint64_t actual_owner = owner ? owner(runtime->get_matchmaking(), lobby) : 0;
    uint64_t claimed_owner = 0;
    uint64_t port = 0;
    const String world = lobby_data(*runtime, lobby, "crimson_world", steam::MAX_LOBBY_WORLD_BYTES);
    const bool compatible = valid_account(actual_owner) && actual_owner != runtime->get_user_id() &&
            (expected_owner == 0 || actual_owner == expected_owner) &&
            lobby_data(*runtime, lobby, "crimson_protocol") == protocol_tag &&
            lobby_data(*runtime, lobby, "crimson_namespace") == lobby_namespace &&
            parse_decimal(lobby_data(*runtime, lobby, "crimson_owner"), claimed_owner) &&
            actual_owner == claimed_owner && parse_decimal(lobby_data(*runtime, lobby, "crimson_port"), port) &&
            port <= 65535 && steam::valid_steam_lobby_world(world);
    const auto endpoint = compatible ? SteamSessionTransport::route(actual_owner, static_cast<int32_t>(port)) :
                                       Ref<session::SessionEndpoint>();
    if (endpoint.is_null()) {
        retire_membership();
        request->fail();
        return;
    }
    joined = lobby;
    steam_scope->lobby_service->set_game_lobby(membership, SteamListing::lobby(
            lobby, runtime->get_app_id(), StringName(lobby_namespace), StringName(world)));
    prepare_client_ticket(listing, actual_owner, endpoint, request);
}

void SteamOnlineDirectory::remove_listing(const String &listing_id) {
    uint64_t lobby = 0;
    if (parse_decimal(listing_id, lobby) && lobby != 0 && lobby == hosted) {
        retire_hosted_lobby();
    }
}

void SteamOnlineDirectory::retire_hosted_lobby() {
    const uint64_t lobby = std::exchange(hosted, 0);
    if (lobby == 0) {
        return;
    }
    suppressed_presence_lobby = 0;
    if (game_scope->hosted_lobby == lobby) {
        game_scope->hosted_lobby = 0;
    }
    clear_join_presence();
    if (const auto verifier = game_scope->listen_verifier.lock()) {
        verifier->retire_connections();
    }
    if (party_service.is_valid()) {
        party_service->retire_game_beacon();
    }
    retire_membership();
}

void SteamOnlineDirectory::retire_membership() {
    pending = false;
    const auto lease = std::exchange(membership, {});
    if (lease) {
        lease->retire();
        steam_scope->lobby_service->clear_game_lobby(lease);
    }
}

void SteamOnlineDirectory::leave_listing(const String &listing_id) {
    if (relay_authorization.is_valid() && relay_authorization->get_listing_id() == listing_id) {
        retire_hosted_resolution();
    }
    if (!ticket_listing_id.is_empty() && ticket_listing_id == listing_id) {
        retire_client_ticket();
    }
    uint64_t lobby = 0;
    uint64_t owner = 0;
    if ((parse_decimal(listing_id, lobby) || steam::parse_steam_party_route(listing_id, lobby, owner)) &&
            lobby != 0 && lobby == joined) {
        joined = 0;
        retire_membership();
    }
}

void SteamOnlineDirectory::invite_received(const void *data, size_t size) {
    if (!is_available() || !data || size != sizeof(GameLobbyJoinRequested_t)) {
        return;
    }
    GameLobbyJoinRequested_t event{};
    std::memcpy(&event, data, sizeof(event));
    receive_lobby_candidate(event.m_steamIDLobby.ConvertToUint64(), event.m_steamIDFriend.ConvertToUint64());
}

void SteamOnlineDirectory::presence_join_received(const void *data, size_t size) {
    if (!is_available() || !data || size != sizeof(GameRichPresenceJoinRequested_t)) {
        return;
    }
    GameRichPresenceJoinRequested_t event{};
    std::memcpy(&event, data, sizeof(event));
    const auto lobby = steam_launch_lobby(copy_presence_text(event.m_rgchConnect));
    if (!lobby.invalid && lobby.id != 0) {
        receive_lobby_candidate(lobby.id, event.m_steamIDFriend.ConvertToUint64());
    }
}

void SteamOnlineDirectory::launch_join_received() {
    if (is_available()) {
        const auto lobby = steam_launch_lobby(game_scope->runtime->read_launch_command_line());
        if (!lobby.invalid && lobby.id != 0) {
            receive_lobby_candidate(lobby.id, 0);
        }
    }
}

void SteamOnlineDirectory::retire_after_ipc_failure() {
    if (steam_scope.is_valid()) {
        steam_scope->initialization_error = "The Steam client connection failed. Reattach Steam after restarting the client.";
    }
    retire_attachment();
}

void SteamOnlineDirectory::receive_lobby_candidate(uint64_t lobby, uint64_t sender) {
    if (!is_available() || !valid_lobby(lobby) || !invite_gateway->receive(
                SteamListing::lobby(lobby, game_scope->runtime->get_app_id(), StringName(lobby_namespace)),
                valid_account(sender) ? id_string(sender) : String())) {
        return;
    }
    if (!is_available()) {
        return;
    }
    const Ref<SteamProvider> provider(Object::cast_to<SteamProvider>(ObjectDB::get_instance(provider_id)));
    bool ambiguous = false;
    if (provider.is_valid() && GameServiceContainer::unique_online_for(*provider.ptr(), ambiguous).ptr() == this) {
        provider->emit_signal("invite_received", id_string(lobby));
    }
}

void SteamOnlineDirectory::poll_join_presence() {
    const String route = hosted != suppressed_presence_lobby ? eligible_join_route() : String();
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (route == owned_connect_presence || now < next_presence_attempt_ms) {
        return;
    }
    if (clear_join_presence() != OK || (!route.is_empty() && publish_join_presence(route) != OK)) {
        next_presence_attempt_ms = now + 1000;
    }
}

String SteamOnlineDirectory::eligible_join_route() const {
    if (is_available() && hosted != 0 && membership && membership->current() == hosted &&
            (visibility == SteamProvider::FRIENDS_ONLY || visibility == SteamProvider::PUBLIC)) {
        const auto *game = Object::cast_to<GameRuntime>(ObjectDB::get_instance(game_scope->owner_id));
        const auto *session = game ? Object::cast_to<session::GameSession>(game->get_node_or_null(NodePath("Session"))) :
                                     nullptr;
        const auto &sdk = game_scope->runtime;
        if (game && game->is_authority() && game->get_online_listing() == id_string(hosted) &&
                session && session->is_accepting_connections() &&
                session->get_connected_peer_ids().size() < transport->get_host_remote_limit() &&
                sdk->get_matchmaking()->GetLobbyOwner(CSteamID(hosted)).ConvertToUint64() == sdk->get_user_id()) {
            return String("+connect_lobby ") + id_string(hosted);
        }
    }
    return {};
}

Error SteamOnlineDirectory::set_join_presence(const Ref<SteamListing> &listing) {
    if (!is_available()) {
        return ERR_UNAVAILABLE;
    }
    if (listing.is_null()) {
        suppressed_presence_lobby = hosted;
        return clear_join_presence();
    }
    const String route = eligible_join_route();
    if (route.is_empty() || listing->get_kind() != SteamListing::LOBBY || listing->is_social() ||
            listing->get_app_id() != game_scope->runtime->get_app_id() ||
            listing->get_namespace() != StringName(lobby_namespace) || listing->get_id() != id_string(hosted)) {
        return ERR_UNAUTHORIZED;
    }
    const Error error = publish_join_presence(route);
    if (error == OK) {
        suppressed_presence_lobby = 0;
    }
    return error;
}

Error SteamOnlineDirectory::publish_join_presence(const String &route) {
    const auto &sdk = game_scope->runtime;
    auto *friends = sdk->get_friends();
    if (!friends) {
        return ERR_UNAVAILABLE;
    }
    const char *previous = friends->GetFriendRichPresence(CSteamID(sdk->get_user_id()), "connect");
    if (previous && previous[0] != '\0') {
        return owned_connect_presence == route && copy_presence_text(previous) == route ? OK : ERR_BUSY;
    }
    if (!friends->SetRichPresence("connect", route.utf8().get_data())) {
        return FAILED;
    }
    owned_connect_presence = route;
    return OK;
}

Error SteamOnlineDirectory::clear_join_presence() {
    if (owned_connect_presence.is_empty()) {
        return OK;
    }
    const auto &sdk = game_scope->runtime;
    auto *friends = sdk->get_friends();
    if (!friends) {
        return ERR_UNAVAILABLE;
    }
    if (copy_presence_text(friends->GetFriendRichPresence(CSteamID(sdk->get_user_id()), "connect")) ==
                owned_connect_presence && !friends->SetRichPresence("connect", nullptr)) {
        return FAILED;
    }
    owned_connect_presence = String();
    return OK;
}

Error SteamOnlineDirectory::open_invite_overlay() {
    if (!is_available()) {
        return ERR_UNAVAILABLE;
    }
    const auto &runtime = game_scope->runtime;
    const auto show = runtime->symbol<decltype(&SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog)>(
            "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
    const auto ready = runtime->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
            "SteamAPI_ISteamUtils_IsOverlayEnabled");
    if (!show || !ready || !runtime->get_utils() || hosted == 0 ||
            transport->get_host_endpoint().is_null()) {
        return ERR_UNAVAILABLE;
    }
    if (runtime->get_matchmaking()->GetLobbyOwner(CSteamID(hosted)).ConvertToUint64() != runtime->get_user_id()) {
        return ERR_UNAUTHORIZED;
    }
    if (!ready(runtime->get_utils())) {
        return ERR_BUSY;
    }
    show(runtime->get_friends(), hosted);
    return OK;
}

Error SteamOnlineDirectory::invite_player(const String &steam_id) {
    uint64_t invitee = 0;
    if (!steam::parse_steam_lobby_account(steam_id, invitee) || (is_available() && invitee == game_scope->runtime->get_user_id())) {
        return ERR_INVALID_PARAMETER;
    }
    if (!is_available() || !valid_lobby(hosted) || transport.is_null() ||
            transport->get_host_endpoint().is_null()) {
        return ERR_UNCONFIGURED;
    }
    const auto &runtime = game_scope->runtime;
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

void SteamInvite::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_listing"), &SteamInvite::get_listing);
    ClassDB::bind_method(D_METHOD("get_from_steam_id"), &SteamInvite::get_from_steam_id);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "listing", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY, "SteamListing"),
            "", "get_listing");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "from_steam_id", PROPERTY_HINT_NONE, "",
                         PROPERTY_USAGE_NO_INSTANCE_STATE | PROPERTY_USAGE_READ_ONLY),
            "", "get_from_steam_id");
}

Ref<SteamListing> SteamInvite::get_listing() const { return listing; }
String SteamInvite::get_from_steam_id() const { return from_steam_id; }

void GameInvites::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &GameInvites::is_available);
    ClassDB::bind_method(D_METHOD("open_overlay"), &GameInvites::open_overlay);
    ClassDB::bind_method(D_METHOD("send", "steam_id"), &GameInvites::send);
    ClassDB::bind_method(D_METHOD("pending"), &GameInvites::pending);
    // Deliver a detached locator to the persistent menu for user confirmation.
    ADD_SIGNAL(MethodInfo("received", PropertyInfo(Variant::OBJECT, "invite", PROPERTY_HINT_NONE, "",
                                             PROPERTY_USAGE_DEFAULT, "SteamInvite")));
}

bool GameInvites::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const Ref<SteamOnlineDirectory> directory(
                Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(directory_id)));
        return directory.is_valid() && directory->is_available();
    }
#endif
    return false;
}

Error GameInvites::open_overlay() {
    if (!is_available()) {
        return ERR_UNAVAILABLE;
    }
    const Ref<SteamOnlineDirectory> directory(
            Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(directory_id)));
    return directory->open_invite_overlay();
}

Error GameInvites::send(const String &steam_id) {
    if (!is_available()) {
        return ERR_UNAVAILABLE;
    }
    const Ref<SteamOnlineDirectory> directory(
            Object::cast_to<SteamOnlineDirectory>(ObjectDB::get_instance(directory_id)));
    return directory->invite_player(steam_id);
}

Ref<SteamInvite> GameInvites::pending() const {
    return is_available() ? last_invite : Ref<SteamInvite>();
}

bool GameInvites::receive(const Ref<SteamListing> &listing, const String &sender) {
#ifdef CRIMSON_WITH_STEAM
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (listing.is_null() || (last_invite.is_valid() && last_invite->listing->get_id() == listing->get_id() &&
                now - last_invite_msec < invite_deduplication_ms)) {
        return false;
    }
    Ref<SteamInvite> invite;
    invite.instantiate();
    invite->listing = listing;
    invite->from_steam_id = sender;
    last_invite = invite;
    last_invite_msec = now;
    emit_signal("received", invite);
    return true;
#else
    return false;
#endif
}

void GameInvites::detach() {
    directory_id = ObjectID();
    last_invite.unref();
    last_invite_msec = 0;
}

void SteamScope::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ready"), &SteamScope::is_ready);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamScope::get_app_id);
    ClassDB::bind_method(D_METHOD("get_local_steam_id"), &SteamScope::get_local_steam_id);
    ClassDB::bind_method(D_METHOD("get_initialization_error"), &SteamScope::get_initialization_error);
    ClassDB::bind_method(D_METHOD("lobbies"), &SteamScope::lobbies);
    ClassDB::bind_method(D_METHOD("server_browser"), &SteamScope::server_browser);
    ClassDB::bind_method(D_METHOD("apps"), &SteamScope::apps);
    ClassDB::bind_method(D_METHOD("friends"), &SteamScope::friends);
    ClassDB::bind_method(D_METHOD("overlay"), &SteamScope::overlay);
    ClassDB::bind_method(D_METHOD("utils"), &SteamScope::utils);
    ClassDB::bind_method(D_METHOD("diagnostics"), &SteamScope::diagnostics);
    ClassDB::bind_method(D_METHOD("http"), &SteamScope::http);
    ClassDB::bind_method(D_METHOD("stats"), &SteamScope::stats);
    ClassDB::bind_method(D_METHOD("leaderboards"), &SteamScope::leaderboards);
    ClassDB::bind_method(D_METHOD("cloud"), &SteamScope::cloud);
    ClassDB::bind_method(D_METHOD("timeline"), &SteamScope::timeline);
    ClassDB::bind_method(D_METHOD("screenshots"), &SteamScope::screenshots);
    ClassDB::bind_method(D_METHOD("remote_play"), &SteamScope::remote_play);
    ClassDB::bind_method(D_METHOD("inventory"), &SteamScope::inventory);
    ClassDB::bind_method(D_METHOD("workshop"), &SteamScope::workshop);
    ClassDB::bind_method(D_METHOD("presence"), &SteamScope::presence);
    ClassDB::bind_method(D_METHOD("music"), &SteamScope::music);
    ClassDB::bind_method(D_METHOD("video"), &SteamScope::video);
    ClassDB::bind_method(D_METHOD("device"), &SteamScope::device);
    ClassDB::bind_method(D_METHOD("auth"), &SteamScope::auth);
    ClassDB::bind_method(D_METHOD("input"), &SteamScope::input);
    ClassDB::bind_method(D_METHOD("browser"), &SteamScope::browser);
    ClassDB::bind_method(D_METHOD("purchases"), &SteamScope::purchases);
    ClassDB::bind_method(D_METHOD("voice"), &SteamScope::voice);
    ClassDB::bind_method(D_METHOD("parties"), &SteamScope::parties);
    ClassDB::bind_method(D_METHOD("messages"), &SteamScope::messages);
    ClassDB::bind_method(D_METHOD("networking"), &SteamScope::networking);
    ClassDB::bind_method(D_METHOD("sockets"), &SteamScope::sockets);
}

steam::SteamClient *SteamScope::resolve_client() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread() && attachment && attachment->active) {
        auto *client = Object::cast_to<steam::SteamClient>(ObjectDB::get_instance(client_id));
        if (client && client->is_inside_tree() && !client->is_queued_for_deletion() &&
                client->get_scope_generation() == attachment && client->is_ready()) {
            return client;
        }
    }
#endif
    return nullptr;
}

bool SteamScope::is_ready() const { return resolve_client() != nullptr; }

int64_t SteamScope::get_app_id() const {
    const auto *client = resolve_client();
    return client ? client->get_app_id() : 0;
}

String SteamScope::get_local_steam_id() const {
    const auto *client = resolve_client();
    return client ? client->get_local_steam_id() : String();
}

String SteamScope::get_initialization_error() const { return initialization_error; }
Ref<SteamLobbies> SteamScope::lobbies() const { return lobby_service; }

Ref<steam::SteamServerBrowser> SteamScope::server_browser() const {
    const auto *client = resolve_client();
    return client ? client->server_browser() : Ref<steam::SteamServerBrowser>();
}

Ref<steam::SteamApps> SteamScope::apps() const {
    const auto *client = resolve_client();
    return client ? client->apps() : Ref<steam::SteamApps>();
}

Ref<steam::SteamFriends> SteamScope::friends() const {
    const auto *client = resolve_client();
    return client ? client->friends() : Ref<steam::SteamFriends>();
}

Ref<steam::SteamOverlay> SteamScope::overlay() const {
    const auto *client = resolve_client();
    return client ? client->overlay() : Ref<steam::SteamOverlay>();
}

Ref<steam::SteamUtils> SteamScope::utils() const {
    const auto *client = resolve_client();
    return client ? client->utils() : Ref<steam::SteamUtils>();
}

Ref<steam::SteamDiagnostics> SteamScope::diagnostics() const {
    const auto *client = resolve_client();
    return client ? client->diagnostics() : Ref<steam::SteamDiagnostics>();
}

Ref<steam::SteamHttp> SteamScope::http() const {
    const auto *client = resolve_client();
    return client ? client->http() : Ref<steam::SteamHttp>();
}

Ref<steam::SteamStats> SteamScope::stats() const {
    const auto *client = resolve_client();
    return client ? client->stats() : Ref<steam::SteamStats>();
}

Ref<steam::SteamLeaderboards> SteamScope::leaderboards() const {
    const auto *client = resolve_client();
    return client ? client->leaderboards() : Ref<steam::SteamLeaderboards>();
}

Ref<steam::SteamCloud> SteamScope::cloud() const {
    const auto *client = resolve_client();
    return client ? client->cloud() : Ref<steam::SteamCloud>();
}

Ref<steam::SteamTimeline> SteamScope::timeline() const {
    const auto *client = resolve_client();
    return client ? client->timeline() : Ref<steam::SteamTimeline>();
}

Ref<steam::SteamScreenshots> SteamScope::screenshots() const {
    const auto *client = resolve_client();
    return client ? client->screenshots() : Ref<steam::SteamScreenshots>();
}

Ref<steam::SteamRemotePlay> SteamScope::remote_play() const {
    const auto *client = resolve_client();
    return client ? client->remote_play() : Ref<steam::SteamRemotePlay>();
}

Ref<steam::SteamInventory> SteamScope::inventory() const {
    const auto *client = resolve_client();
    return client ? client->inventory() : Ref<steam::SteamInventory>();
}

Ref<steam::SteamWorkshop> SteamScope::workshop() const {
    const auto *client = resolve_client();
    return client ? client->workshop() : Ref<steam::SteamWorkshop>();
}

Ref<steam::SteamPresence> SteamScope::presence() const {
    const auto *client = resolve_client();
    return client ? client->presence() : Ref<steam::SteamPresence>();
}

Ref<steam::SteamMusic> SteamScope::music() const {
    const auto *client = resolve_client();
    return client ? client->music() : Ref<steam::SteamMusic>();
}

Ref<steam::SteamVideo> SteamScope::video() const {
    const auto *client = resolve_client();
    return client ? client->video() : Ref<steam::SteamVideo>();
}

Ref<steam::SteamDevice> SteamScope::device() const {
    const auto *client = resolve_client();
    return client ? client->device() : Ref<steam::SteamDevice>();
}

Ref<steam::SteamAuth> SteamScope::auth() const {
    const auto *client = resolve_client();
    return client ? client->auth() : Ref<steam::SteamAuth>();
}

Ref<steam::SteamInput> SteamScope::input() const {
    const auto *client = resolve_client();
    return client ? client->input() : Ref<steam::SteamInput>();
}

Ref<steam::SteamBrowser> SteamScope::browser() const {
    const auto *client = resolve_client();
    return client ? client->browser() : Ref<steam::SteamBrowser>();
}

Ref<steam::SteamPurchases> SteamScope::purchases() const {
    const auto *client = resolve_client();
    return client ? client->purchases() : Ref<steam::SteamPurchases>();
}

Ref<steam::SteamVoice> SteamScope::voice() const {
    const auto *client = resolve_client();
    return client ? client->voice() : Ref<steam::SteamVoice>();
}

Ref<steam::SteamParties> SteamScope::parties() const {
    const auto *client = resolve_client();
    return client ? client->parties() : Ref<steam::SteamParties>();
}

Ref<steam::SteamMessages> SteamScope::messages() const {
    const auto *client = resolve_client();
    return client ? client->messages() : Ref<steam::SteamMessages>();
}

Ref<steam::SteamNetworking> SteamScope::networking() const {
    const auto *client = resolve_client();
    return client ? client->networking() : Ref<steam::SteamNetworking>();
}

Ref<steam::SteamSockets> SteamScope::sockets() const {
    const auto *client = resolve_client();
    return client ? client->sockets() : Ref<steam::SteamSockets>();
}

void SteamServices::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("for_game", "game"), &SteamServices::for_game);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("current"), &SteamServices::current);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("server_for", "game"), &SteamServices::server_for);
}

Ref<SteamScope> SteamServices::for_game(GameRuntime *game) {
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::online_for(game));
    return directory.is_valid() ? directory->get_steam_scope() : Ref<SteamScope>();
}

Ref<SteamScope> SteamServices::current() {
    return for_game(GameServiceContainer::default_game());
}

Ref<SteamDedicatedServer> SteamServices::server_for(GameRuntime *game) {
    const Ref<SteamDedicatedOnlineDirectory> directory(GameServiceContainer::online_for(game));
    return directory.is_valid() ? directory->service() : Ref<SteamDedicatedServer>();
}

Ref<GameInvites> GameRuntime::invites() const {
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::online_for(this));
    return directory.is_valid() ? directory->get_invites() : Ref<GameInvites>();
}

void SteamOnlineDirectory::_bind_methods() {}

SteamOnlineDirectory::~SteamOnlineDirectory() {
    retire_attachment();
}

void SteamOnlineDirectory::initialize_binding(steam::SteamClient *client, const String &error, const StringName &name_space) {
    steam_scope.instantiate();
    if (client) {
        steam_scope->attachment = client->get_scope_generation();
        steam_scope->client_id = ObjectID(client->get_instance_id());
    }
    steam_scope->initialization_error = error;
    if (client) {
        steam_scope->lobby_service = client->lobbies();
    } else {
        steam_scope->lobby_service.instantiate();
        steam_scope->lobby_service->configure_game(name_space);
    }
    invite_gateway.instantiate();
    invite_gateway->directory_id = ObjectID(get_instance_id());
}

void SteamOnlineDirectory::retire_attachment() {
    if (steam_scope.is_null() || std::exchange(retired, true)) {
        return;
    }
    if (steam_scope->attachment) {
        steam_scope->attachment->active = false;
    }
#ifdef CRIMSON_WITH_STEAM
    if (game_scope) {
        game_scope->runtime->unsubscribe(std::exchange(invite_subscription, 0));
        game_scope->runtime->unsubscribe(std::exchange(presence_subscription, 0));
        game_scope->runtime->unsubscribe(std::exchange(launch_subscription, 0));
        if (game_scope->directory_id == ObjectID(get_instance_id())) {
            game_scope->directory_id = ObjectID();
        }
        clear_join_presence();
        retire_hosted_resolution();
        retire_client_ticket();
        retire_hosted_lobby();
        joined = 0;
        retire_membership();
    }
#endif
    invite_gateway->detach();
    auto *client = Object::cast_to<steam::SteamClient>(ObjectDB::get_instance(steam_scope->client_id));
    if (client) {
        client->retire_scope_generation(steam_scope->attachment);
    }
    steam_scope->lobby_service->detach();
}

bool SteamOnlineDirectory::is_game_available() const { return is_available(); }
bool SteamOnlineDirectory::supports_host_mode(bool dedicated) const { return !dedicated; }

bool SteamOnlineDirectory::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return steam_scope.is_valid() && steam_scope->is_ready() && is_active_steam_game(game_scope) &&
            game_scope->runtime && game_scope->runtime->is_healthy() && transport.is_valid();
#else
    return false;
#endif
}

bool SteamOnlineDirectory::accepts_listing(const Ref<OnlineListing> &listing) const {
#ifdef CRIMSON_WITH_STEAM
    const Ref<SteamListing> selected(listing);
    if (is_available() && selected.is_valid() && selected->get_kind() == SteamListing::SERVER) {
        const auto app = game_scope->runtime->get_app_id();
        const StringName name_space(lobby_namespace);
        return selected->get_app_id() == app && selected->get_namespace() == name_space &&
                (steam::SteamServerRoute::from_listing(selected->get_id(), app, name_space).is_valid() ||
                        steam::SteamHostedRoute::from_listing(selected->get_id(), app, name_space).is_valid());
    }
    uint64_t lobby = 0;
    uint64_t owner = 0;
    return is_available() && selected.is_valid() && selected->get_kind() == SteamListing::LOBBY &&
            !selected->is_social() &&
            selected->get_app_id() == game_scope->runtime->get_app_id() &&
            selected->get_namespace() == StringName(lobby_namespace) &&
            ((parse_decimal(selected->get_id(), lobby) && valid_lobby(lobby)) ||
                    steam::parse_steam_party_route(selected->get_id(), lobby, owner));
#else
    return false;
#endif
}

Ref<SteamScope> SteamOnlineDirectory::get_steam_scope() const { return steam_scope; }
Ref<GameInvites> SteamOnlineDirectory::get_invites() const { return invite_gateway; }

#ifndef CRIMSON_WITH_STEAM
Error SteamOnlineDirectory::open_invite_overlay() { return ERR_UNAVAILABLE; }
Error SteamOnlineDirectory::invite_player(const String &) { return ERR_UNAVAILABLE; }
#endif

void SteamProvider::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("configure"), &SteamProvider::configure);
    ClassDB::bind_method(D_METHOD("for_app", "expected_app_id"), &SteamProvider::for_app);
    ClassDB::bind_method(D_METHOD("private"), &SteamProvider::private_lobby);
    ClassDB::bind_method(D_METHOD("friends_only"), &SteamProvider::friends_only);
    ClassDB::bind_method(D_METHOD("public_lobbies"), &SteamProvider::public_lobbies);
    ClassDB::bind_method(D_METHOD("invisible_lobby"), &SteamProvider::invisible_lobby);
    ClassDB::bind_method(D_METHOD("with_lobby_namespace", "namespace"), &SteamProvider::with_lobby_namespace);
    ClassDB::bind_method(D_METHOD("on_virtual_port", "port"), &SteamProvider::on_virtual_port);
    ClassDB::bind_method(D_METHOD("prefer_relay"), &SteamProvider::prefer_relay);
    ClassDB::bind_method(D_METHOD("set_prefer_relay_routes", "enabled"), &SteamProvider::set_prefer_relay_routes);
    ClassDB::bind_method(D_METHOD("get_prefer_relay_routes"), &SteamProvider::get_prefer_relay_routes);
    ClassDB::bind_method(D_METHOD("dedicated", "settings"), &SteamProvider::dedicated);
    ClassDB::bind_method(D_METHOD("with_relay_coordinator", "coordinator"), &SteamProvider::with_relay_coordinator);
    ClassDB::bind_method(D_METHOD("with_commerce_backend", "backend"), &SteamProvider::with_commerce_backend);
    ClassDB::bind_method(D_METHOD("with_message_channels", "channels"), &SteamProvider::with_message_channels);
    ClassDB::bind_method(D_METHOD("set_lobby_visibility", "value"), &SteamProvider::set_lobby_visibility);
    ClassDB::bind_method(D_METHOD("get_lobby_visibility"), &SteamProvider::get_lobby_visibility);
    ClassDB::bind_method(D_METHOD("set_app_id", "value"), &SteamProvider::set_app_id);
    ClassDB::bind_method(D_METHOD("get_app_id"), &SteamProvider::get_app_id);
    ClassDB::bind_method(D_METHOD("set_virtual_port", "value"), &SteamProvider::set_virtual_port);
    ClassDB::bind_method(D_METHOD("get_virtual_port"), &SteamProvider::get_virtual_port);
    ClassDB::bind_method(D_METHOD("set_lobby_namespace", "value"), &SteamProvider::set_lobby_namespace);
    ClassDB::bind_method(D_METHOD("get_lobby_namespace"), &SteamProvider::get_lobby_namespace);
    ClassDB::bind_method(D_METHOD("is_ready"), &SteamProvider::is_ready);
    ClassDB::bind_method(D_METHOD("get_local_steam_id"), &SteamProvider::get_local_steam_id);
    ClassDB::bind_method(D_METHOD("get_initialization_error"), &SteamProvider::get_initialization_error);
    ClassDB::bind_method(D_METHOD("open_invite_overlay"), &SteamProvider::open_invite_overlay);
    ClassDB::bind_method(D_METHOD("invite_player", "steam_id"), &SteamProvider::invite_player);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "app_id", PROPERTY_HINT_RANGE, "0,4294967295,1"), "set_app_id", "get_app_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "virtual_port", PROPERTY_HINT_RANGE, "0,65535,1"),
            "set_virtual_port", "get_virtual_port");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "lobby_namespace"), "set_lobby_namespace", "get_lobby_namespace");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "lobby_visibility", PROPERTY_HINT_ENUM, "Private,Friends Only,Public,Invisible"),
            "set_lobby_visibility", "get_lobby_visibility");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "prefer_relay_routes"), "set_prefer_relay_routes", "get_prefer_relay_routes");
    // Preserve the legacy listing-ID invite signal for a uniquely attached provider.
    ADD_SIGNAL(MethodInfo("invite_received", PropertyInfo(Variant::STRING, "listing_id")));
    BIND_ENUM_CONSTANT(PRIVATE);
    BIND_ENUM_CONSTANT(FRIENDS_ONLY);
    BIND_ENUM_CONSTANT(PUBLIC);
    BIND_ENUM_CONSTANT(INVISIBLE);
}

session::GameSession *SteamProvider::create_session_for(GameRuntime &game) {
    if (dedicated_settings.is_valid()) {
        return SteamDedicatedRuntimeNode::create_session(game, *this, dedicated_settings,
                static_cast<uint32_t>(app_id), StringName(lobby_namespace), static_cast<int32_t>(virtual_port),
                relay_coordinator);
    }
#ifdef CRIMSON_WITH_STEAM
    auto selected = std::make_shared<SteamGameScope>();
    selected->owner_id = ObjectID(game.get_instance_id());
    selected->provider_id = ObjectID(get_instance_id());
    selected->lobby_namespace = lobby_namespace;
    selected->message_channels.assign(message_channels);
    selected->virtual_port = static_cast<int32_t>(virtual_port);
    selected->visibility = visibility;
    selected->commerce_backend = commerce_backend;
    selected->relay_coordinator = relay_coordinator;
    const auto sdk = SteamRuntime::acquire(static_cast<uint32_t>(app_id), selected->startup_error);
    selected->runtime = sdk;
    session::GameSession *session = nullptr;
    ObjectID authenticator_id;
    if (sdk && (!sdk->get_matchmaking() || !sdk->get_friends())) {
        selected->startup_error = "The Steam client does not expose the lobby and invite interfaces required for online play.";
    } else if (claim_steam_game(selected)) {
        Ref<SteamSessionTransport> transport;
        transport.instantiate();
        if (transport->configure(sdk, selected->virtual_port, prefer_relay_routes) == OK) {
            Ref<SteamAuthenticator> identity;
            identity.instantiate();
            session = memnew(session::GameSession);
            if (identity->configure(selected, transport, *session) && session->set_transport_adapter(transport) == OK &&
                    session->set_authenticator(identity) == OK) {
                selected->transport = transport;
                authenticator_id = ObjectID(identity->get_instance_id());
            } else {
                memdelete(session);
                session = nullptr;
            }
        }
        if (!session) {
            release_steam_game(selected);
            selected->startup_error = "The selected Steam transport could not be configured.";
        }
    } else if (sdk) {
        selected->startup_error = "The local Steam account is already attached to a game.";
    }
    if (!session) {
        selected->runtime.reset();
        session = GameServiceProvider::create_session_for(game);
    }
    if (session && !session->get_parent() && !session->is_queued_for_deletion()) {
        auto *pump = memnew(SteamRuntimeNode);
        pump->configure(selected, authenticator_id);
        session->add_child(pump);
    }
    return session;
#else
    return GameServiceProvider::create_session_for(game);
#endif
}

Ref<OnlineDirectory> SteamProvider::create_online_directory_for(GameRuntime &game, session::GameSession &session) {
    if (dedicated_settings.is_valid()) {
        return SteamDedicatedRuntimeNode::create_directory(game, *this, session);
    }
    Ref<SteamOnlineDirectory> directory;
    directory.instantiate();
#ifdef CRIMSON_WITH_STEAM
    const auto *pump = Object::cast_to<SteamRuntimeNode>(session.get_node_or_null(NodePath("SteamRuntime")));
    const auto selected = pump ? pump->get_game_scope() : nullptr;
    if (selected && selected->owner_id == ObjectID(game.get_instance_id()) &&
            selected->provider_id == ObjectID(get_instance_id())) {
        const StringName selected_namespace(selected->lobby_namespace);
        if (is_active_steam_game(selected) && selected->transport.is_valid()) {
            auto *client = pump->get_client();
            if (client && client->start(selected->runtime->get_app_id()) == OK) {
                client->purchases()->bind_backend(selected->commerce_backend);
                directory->initialize_binding(client, {}, selected_namespace);
                directory->configure(selected);
            } else {
                const String error = client ? client->get_initialization_error() : "The Session's Steam client is missing.";
                directory->initialize_binding(nullptr, error, selected_namespace);
            }
        } else {
            directory->initialize_binding(nullptr, selected->startup_error, selected_namespace);
        }
        return directory;
    }
    directory->initialize_binding(nullptr, "The persistent Session does not own this Steam binding.",
            StringName(lobby_namespace));
#else
    directory->initialize_binding(nullptr, "This build has no Steamworks SDK support.", StringName(lobby_namespace));
#endif
    return directory;
}

Ref<SteamProvider> SteamProvider::configure() {
    Ref<SteamProvider> provider;
    provider.instantiate();
    return provider;
}

Ref<SteamProvider> SteamProvider::branch() const {
    const auto provider = configure();
    provider->app_id = app_id;
    provider->virtual_port = virtual_port;
    provider->lobby_namespace = lobby_namespace;
    provider->message_channels.assign(message_channels);
    provider->visibility = visibility;
    provider->prefer_relay_routes = prefer_relay_routes;
    provider->dedicated_settings = dedicated_settings;
    provider->relay_coordinator = relay_coordinator;
    provider->commerce_backend = commerce_backend;
    return provider;
}

Ref<SteamProvider> SteamProvider::with_commerce_backend(const Ref<steam::SteamCommerceBackend> &backend) const {
    const auto provider = branch();
    provider->commerce_backend = backend;
    return provider;
}

Ref<SteamProvider> SteamProvider::with_message_channels(const TypedArray<StringName> &channels) const {
    if (!steam::valid_steam_message_channels(channels, true)) {
        return {};
    }
    const auto provider = branch();
    provider->message_channels.assign(channels);
    return provider;
}

Ref<SteamProvider> SteamProvider::for_app(int64_t value) const {
    if (value < 0 || value > (std::numeric_limits<uint32_t>::max)()) {
        return {};
    }
    const auto provider = branch();
    provider->app_id = value;
    return provider;
}

Ref<SteamProvider> SteamProvider::with_visibility(Visibility value) const {
    const auto provider = branch();
    provider->visibility = value;
    return provider;
}

Ref<SteamProvider> SteamProvider::prefer_relay() const {
    const auto provider = branch();
    provider->prefer_relay_routes = true;
    return provider;
}

void SteamProvider::set_prefer_relay_routes(bool enabled) { prefer_relay_routes = enabled; }
bool SteamProvider::get_prefer_relay_routes() const { return prefer_relay_routes; }

Ref<SteamProvider> SteamProvider::dedicated(const Ref<steam::SteamServer> &settings) const {
    if (settings.is_null()) {
        return {};
    }
    const auto provider = branch();
    provider->dedicated_settings = settings;
    return provider;
}

Ref<SteamProvider> SteamProvider::with_relay_coordinator(const Ref<SteamRelayCoordinator> &coordinator) const {
    const auto provider = branch();
    provider->relay_coordinator = coordinator;
    return provider;
}

Ref<SteamProvider> SteamProvider::private_lobby() const { return with_visibility(PRIVATE); }
Ref<SteamProvider> SteamProvider::friends_only() const { return with_visibility(FRIENDS_ONLY); }
Ref<SteamProvider> SteamProvider::public_lobbies() const { return with_visibility(PUBLIC); }
Ref<SteamProvider> SteamProvider::invisible_lobby() const { return with_visibility(INVISIBLE); }

Ref<SteamProvider> SteamProvider::with_lobby_namespace(const String &value) const {
    if (!steam::valid_steam_lobby_namespace(value)) {
        return {};
    }
    const auto provider = branch();
    provider->lobby_namespace = value;
    return provider;
}

Ref<SteamProvider> SteamProvider::on_virtual_port(int64_t value) const {
    if (value < 0 || value > 65535) {
        return {};
    }
    const auto provider = branch();
    provider->virtual_port = value;
    return provider;
}

void SteamProvider::set_lobby_visibility(Visibility value) {
    if (value >= PRIVATE && value <= INVISIBLE && !is_ready()) {
        visibility = value;
    }
}

SteamProvider::Visibility SteamProvider::get_lobby_visibility() const { return visibility; }

void SteamProvider::set_app_id(int64_t value) {
    if (value >= 0 && value <= (std::numeric_limits<uint32_t>::max)() && !is_ready()) {
        app_id = value;
    }
}

int64_t SteamProvider::get_app_id() const {
    return app_id;
}

void SteamProvider::set_virtual_port(int64_t value) {
    if (value >= 0 && value <= 65535 && !is_ready()) {
        virtual_port = value;
    }
}

int64_t SteamProvider::get_virtual_port() const {
    return virtual_port;
}

void SteamProvider::set_lobby_namespace(const String &value) {
    if (steam::valid_steam_lobby_namespace(value) && !is_ready()) {
        lobby_namespace = value;
    }
}

String SteamProvider::get_lobby_namespace() const {
    return lobby_namespace;
}

bool SteamProvider::is_ready() const {
    bool ambiguous = false;
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::unique_online_for(*this, ambiguous));
    return directory.is_valid() && directory->is_available();
}

String SteamProvider::get_local_steam_id() const {
    bool ambiguous = false;
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::unique_online_for(*this, ambiguous));
    return directory.is_valid() ? directory->get_steam_scope()->get_local_steam_id() : String();
}

String SteamProvider::get_initialization_error() const {
    bool ambiguous = false;
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::unique_online_for(*this, ambiguous));
    if (ambiguous) {
        return "This provider is shared by multiple games. Resolve Steam for the intended game.";
    }
    return directory.is_valid() ? directory->get_steam_scope()->get_initialization_error() : String();
}

Error SteamProvider::open_invite_overlay() {
#ifdef CRIMSON_WITH_STEAM
    bool ambiguous = false;
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::unique_online_for(*this, ambiguous));
    if (ambiguous) {
        return ERR_BUSY;
    }
    if (directory.is_valid()) {
        return directory->open_invite_overlay();
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamProvider::invite_player(const String &steam_id) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread()) {
        return ERR_BUSY;
    }
    uint64_t invitee = 0;
    if (!steam::parse_steam_lobby_account(steam_id, invitee)) {
        return ERR_INVALID_PARAMETER;
    }
    bool ambiguous = false;
    const Ref<SteamOnlineDirectory> directory(GameServiceContainer::unique_online_for(*this, ambiguous));
    if (ambiguous) {
        return ERR_BUSY;
    }
    if (directory.is_valid()) {
        return directory->invite_player(steam_id);
    }
    return ERR_UNCONFIGURED;
#else
    return ERR_UNAVAILABLE;
#endif
}

} // namespace crimson::integration
