// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "integration/online/eos/eos_online.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_EOS
#include "integration/online/eos/eos_runtime.hpp"
#include "integration/online/eos/eos_transport.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>

#include <eos_auth.h>
#include <eos_connect.h>
#include <eos_lobby.h>
#include <eos_sdk.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>
#endif

using namespace godot;

namespace crimson::integration {

namespace {

// Finish a sign-in operation without exposing SDK credentials or token data.
void finish_login(const Ref<CrimsonGameOperation> &operation, bool ok, const char *code) {
    Dictionary result;
    result["ok"] = ok;
    result["code"] = code;
    operation->finish(result);
}

#ifdef CRIMSON_WITH_EOS

// Shared directory bucket and attribute key used by the native EOS provider.
constexpr char lobby_bucket[] = "crimson-eos-p2p-v1";
constexpr char socket_attribute[] = "CrimsonSocket";
// Session proof cap includes the challenge prefix.
constexpr int64_t proof_limit = 4096;
// Bound a developer sign-in independently of SDK task scheduling.
constexpr uint64_t login_timeout_ms = 20000;
// Allow Epic's lobby member view to catch up with the P2P connection notification.
constexpr uint64_t membership_grace_ms = 2000;
constexpr uint64_t membership_poll_ms = 100;
constexpr size_t max_pending_admissions = 64;

#define CRIMSON_EOS_ONLINE_SYMBOLS(X) \
    X(EOS_Platform_GetAuthInterface) \
    X(EOS_Platform_GetConnectInterface) \
    X(EOS_Platform_GetLobbyInterface) \
    X(EOS_Auth_Login) \
    X(EOS_Auth_CopyIdToken) \
    X(EOS_Auth_IdToken_Release) \
    X(EOS_Connect_Login) \
    X(EOS_Connect_CreateUser) \
    X(EOS_Connect_CopyIdToken) \
    X(EOS_Connect_IdToken_Release) \
    X(EOS_Connect_VerifyIdToken) \
    X(EOS_ProductUserId_ToString) \
    X(EOS_ProductUserId_FromString) \
    X(EOS_Lobby_CreateLobby) \
    X(EOS_Lobby_DestroyLobby) \
    X(EOS_Lobby_JoinLobby) \
    X(EOS_Lobby_LeaveLobby) \
    X(EOS_Lobby_CopyLobbyDetailsHandle) \
    X(EOS_Lobby_UpdateLobbyModification) \
    X(EOS_LobbyModification_AddAttribute) \
    X(EOS_LobbyModification_Release) \
    X(EOS_Lobby_UpdateLobby) \
    X(EOS_Lobby_CreateLobbySearch) \
    X(EOS_LobbySearch_SetLobbyId) \
    X(EOS_LobbySearch_Find) \
    X(EOS_LobbySearch_CopySearchResultByIndex) \
    X(EOS_LobbySearch_Release) \
    X(EOS_LobbyDetails_CopyInfo) \
    X(EOS_LobbyDetails_Info_Release) \
    X(EOS_LobbyDetails_CopyAttributeByKey) \
    X(EOS_Lobby_Attribute_Release) \
    X(EOS_LobbyDetails_GetMemberCount) \
    X(EOS_LobbyDetails_GetMemberByIndex) \
    X(EOS_LobbyDetails_Release)

// Typed entry points loaded from the exact runtime owned by EosPlatform.
struct EosOnlineApi {
#define CRIMSON_DECLARE_SYMBOL(name) decltype(&::name) name = nullptr;
    CRIMSON_EOS_ONLINE_SYMBOLS(CRIMSON_DECLARE_SYMBOL)
#undef CRIMSON_DECLARE_SYMBOL

    // Resolve the complete online capability before issuing SDK requests.
    bool bind(const EosPlatform &platform) {
#define CRIMSON_BIND_SYMBOL(name) \
        name = reinterpret_cast<decltype(name)>(platform.get_proc_address(#name)); \
        if (!name) { return false; }
        CRIMSON_EOS_ONLINE_SYMBOLS(CRIMSON_BIND_SYMBOL)
#undef CRIMSON_BIND_SYMBOL
        return true;
    }
};
#undef CRIMSON_EOS_ONLINE_SYMBOLS

// Verify the scene-thread and SDK-specific text bounds before passing C strings.
bool on_main_thread() {
    const OS *os = OS::get_singleton();
    return os && os->get_main_thread_id() == os->get_thread_caller_id();
}

// Reject embedded NUL and oversize UTF-8 without rewriting identifiers.
bool bounded_text(const String &text, int64_t maximum) {
    const CharString bytes = text.utf8();
    return bytes.length() > 0 && bytes.length() <= maximum &&
            std::memchr(bytes.get_data(), '\0', bytes.length()) == nullptr;
}

// A verified EOS peer still waiting for the host's lobby member view.
struct PendingAdmission {
    Ref<session::CrimsonSessionAuthRequest> request;
    String user;
    std::string listing;
    uint64_t deadline;
};

// Accept the current native attempt only with the verified EOS identity.
void accept_eos_peer(const Ref<session::CrimsonSessionAuthRequest> &request, const String &user) {
    const auto identity = session::CrimsonSessionIdentity::verified("eos", user);
    if (identity.is_valid()) {
        request->accept(identity);
    } else {
        request->reject();
    }
}

#endif
} // namespace

#ifdef CRIMSON_WITH_EOS

// Owns the signed-in EOS context and tracks the live game's shared transport.
struct EosOnlineState {
    // Vendor entry points captured from the authenticated platform's module.
    EosOnlineApi api;
    // Shared handle pumped by the scene-owned poller.
    std::shared_ptr<EosPlatform> platform;
    // Epic Account Services handle used for developer sign-in.
    EOS_HAuth auth = nullptr;
    // Game Services handle used for login and admission identity.
    EOS_HConnect connect = nullptr;
    // Game Services handle used by the lobby directory.
    EOS_HLobby lobby = nullptr;
    // Context the signed Connect token must identify.
    std::string product_id;
    std::string sandbox_id;
    std::string deployment_id;
    // Signed-in local product identity.
    EOS_ProductUserId user = nullptr;
    // Canonical representation of the signed-in local product identity.
    String user_string;
    // Login settles once, independently of SDK callback arrival.
    Ref<CrimsonGameOperation> login;
    // Monotonic bound on the sign-in operation.
    uint64_t login_deadline = 0;
    // True when the pump or pending login was retired.
    bool closed = false;
    // Single game's transport used to read actual physical PUID mappings.
    Ref<CrimsonEOSSessionTransport> transport;
    // Weak ID of the game Session holding that transport.
    ObjectID session_id;
    // Published or pending-created lobbies this provider must retire.
    std::set<std::string> listings;
    // Only the current published listing may authorize incoming guests.
    std::string published_listing;
    // Verified incoming identities awaiting this lobby's member snapshot.
    std::vector<PendingAdmission> pending_admissions;
    uint64_t next_membership_poll = 0;
    // Active SDK search handles released before the platform is retired.
    std::set<EOS_HLobbySearch> searches;
    // Lobby memberships acquired by successful SDK joins.
    std::set<std::string> memberships;
    // SDK joins and departures still awaiting completion.
    std::set<std::string> pending_memberships;

    // Report whether the platform has a usable local Connect identity.
    bool ready() const {
        return !closed && platform && platform->get_handle() && user && !user_string.is_empty();
    }

    // Convert an SDK PUID into the same canonical form used by EOS transport.
    String stringify(EOS_ProductUserId id) const {
        std::array<char, EOS_PRODUCTUSERID_MAX_LENGTH + 1> buffer{};
        int32_t size = static_cast<int32_t>(buffer.size());
        return id && api.EOS_ProductUserId_ToString(id, buffer.data(), &size) == EOS_EResult::EOS_Success ?
                String::utf8(buffer.data()) : String();
    }

    // Match the token's signed deployment context and the current physical EOS sender.
    bool matches_peer(const EOS_Connect_VerifyIdTokenCallbackInfo &info, const String &expected_user,
            int32_t peer_id) const {
        return ready() && info.ResultCode == EOS_EResult::EOS_Success &&
                info.ProductId && product_id == info.ProductId &&
                info.SandboxId && sandbox_id == info.SandboxId &&
                info.DeploymentId && deployment_id == info.DeploymentId &&
                stringify(info.ProductUserId) == expected_user && transport.is_valid() &&
                transport->get_remote_product_user_id(peer_id) == expected_user;
    }

    // Read the current host lobby's membership from the signed-in EOS platform.
    std::vector<String> get_lobby_members() const {
        std::vector<String> result;
        if (!ready() || published_listing.empty()) {
            return result;
        }
        EOS_Lobby_CopyLobbyDetailsHandleOptions copy{};
        copy.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
        copy.LocalUserId = user;
        copy.LobbyId = published_listing.c_str();
        EOS_HLobbyDetails details = nullptr;
        if (api.EOS_Lobby_CopyLobbyDetailsHandle(lobby, &copy, &details) != EOS_EResult::EOS_Success || !details) {
            return result;
        }
        EOS_LobbyDetails_GetMemberCountOptions count{};
        count.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
        const uint32_t size = std::min(api.EOS_LobbyDetails_GetMemberCount(details, &count),
                static_cast<uint32_t>(EOS_LOBBY_MAX_LOBBY_MEMBERS));
        result.reserve(size);
        EOS_LobbyDetails_GetMemberByIndexOptions member{};
        member.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;
        for (uint32_t index = 0; index < size; ++index) {
            member.MemberIndex = index;
            result.push_back(stringify(api.EOS_LobbyDetails_GetMemberByIndex(details, &member)));
        }
        api.EOS_LobbyDetails_Release(details);
        return result;
    }

    // Admit only verified peers who belong to the exact listing for this live listener.
    void poll_admissions() {
        if (pending_admissions.empty()) {
            return;
        }
        const uint64_t now = Time::get_singleton()->get_ticks_msec();
        if (now < next_membership_poll) {
            return;
        }
        next_membership_poll = now + membership_poll_ms;
        const std::string listing = published_listing;
        const auto members = get_lobby_members();
        auto pending = std::exchange(pending_admissions, {});
        for (auto &candidate : pending) {
            const bool current = now < candidate.deadline && ready() && !listing.empty() &&
                    listing == published_listing &&
                    listing == candidate.listing && transport.is_valid() &&
                    transport->get_remote_product_user_id(candidate.request->get_peer_id()) == candidate.user;
            if (current && std::find(members.begin(), members.end(), candidate.user) != members.end()) {
                accept_eos_peer(candidate.request, candidate.user);
            } else if (current) {
                pending_admissions.push_back(std::move(candidate));
            } else {
                candidate.request->reject();
            }
        }
    }

    // Withdraw one owned lobby exactly once while the platform is usable.
    void destroy_listing(std::string id) {
        if (published_listing == id) {
            published_listing.clear();
        }
        if (!listings.erase(id) || !platform || !platform->get_handle()) {
            return;
        }
        EOS_Lobby_DestroyLobbyOptions options{};
        options.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
        options.LocalUserId = user;
        options.LobbyId = id.c_str();
        api.EOS_Lobby_DestroyLobby(lobby, &options, nullptr, on_destroyed);
    }

    // Retire all public state and cancel any outstanding bootstrap operation.
    void close(const char *code) {
        if (closed) {
            return;
        }
        closed = true;
        auto *game_session = Object::cast_to<session::CrimsonGameSession>(ObjectDB::get_instance(session_id));
        if (game_session && game_session->is_inside_tree()) {
            game_session->leave();
        }
        while (!listings.empty()) {
            destroy_listing(*listings.begin());
        }
        for (EOS_HLobbySearch search : searches) {
            api.EOS_LobbySearch_Release(search);
        }
        searches.clear();
        for (const auto &id : memberships) {
            EOS_Lobby_LeaveLobbyOptions options{};
            options.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
            options.LocalUserId = user;
            options.LobbyId = id.c_str();
            api.EOS_Lobby_LeaveLobby(lobby, &options, nullptr, on_untracked_departure);
        }
        memberships.clear();
        pending_memberships.clear();
        pending_admissions.clear();
        transport.unref();
        platform.reset();
        const Ref<CrimsonGameOperation> operation = login;
        login.unref();
        if (operation.is_valid()) {
            finish_login(operation, false, code);
        }
    }

    // Complete the login after EOS has established a valid local product identity.
    void signed_in(EOS_ProductUserId id) {
        if (login.is_null()) {
            return;
        }
        user_string = stringify(id);
        if (user_string.is_empty()) {
            close("EosInvalidLocalUser");
            return;
        }
        user = id;
        const Ref<CrimsonGameOperation> operation = login;
        login.unref();
        finish_login(operation, true, "Ok");
    }

    // Complete an SDK lobby destroy without calling back into a retired scope.
    static void EOS_CALL on_destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo *) {}
    // Complete a teardown-only leave without retaining the game scope.
    static void EOS_CALL on_untracked_departure(const EOS_Lobby_LeaveLobbyCallbackInfo *) {}
};

namespace {

// Callback cell retained by the SDK platform, with weak references to game state.
struct CallbackCell {
    // Owner of the EOS ClientData retention.
    std::weak_ptr<EosPlatform> platform;
    // Login/game state needed only while the scene owner is alive.
    std::weak_ptr<EosOnlineState> state;
};

// Keep callback data alive until one completion or platform release.
template <typename T>
void *hold(const std::shared_ptr<EosOnlineState> &state, const std::shared_ptr<T> &cell) {
    cell->platform = state->platform;
    cell->state = state;
    return state->platform->retain_callback(cell);
}

// Consume the one registered callback without extending the scope past its owner.
template <typename T>
std::shared_ptr<T> take(void *data) {
    if (!data) {
        return {};
    }
    const auto platform = static_cast<T *>(data)->platform.lock();
    return platform ? std::static_pointer_cast<T>(platform->take_callback(data)) : nullptr;
}

// Continue a new user's private deployment login through EOS Connect CreateUser.
void EOS_CALL on_user_created(const EOS_Connect_CreateUserCallbackInfo *info) {
    const auto cell = take<CallbackCell>(info->ClientData);
    const auto state = cell ? cell->state.lock() : nullptr;
    if (state && !state->closed) {
        if (info->ResultCode == EOS_EResult::EOS_Success) {
            state->signed_in(info->LocalUserId);
        } else {
            state->close("EosCreateUserFailed");
        }
    }
}

// Establish an existing identity or create the first product user when authorized by EOS.
void EOS_CALL on_connect_login(const EOS_Connect_LoginCallbackInfo *info) {
    const auto cell = take<CallbackCell>(info->ClientData);
    const auto state = cell ? cell->state.lock() : nullptr;
    if (!state || state->closed) {
        return;
    }
    if (info->ResultCode == EOS_EResult::EOS_Success) {
        state->signed_in(info->LocalUserId);
        return;
    }
    if (info->ResultCode != EOS_EResult::EOS_InvalidUser || !info->ContinuanceToken) {
        state->close("EosConnectLoginFailed");
        return;
    }
    void *data = hold(state, std::make_shared<CallbackCell>());
    if (!data) {
        state->close("EosCallbackUnavailable");
        return;
    }
    EOS_Connect_CreateUserOptions options{};
    options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
    options.ContinuanceToken = info->ContinuanceToken;
    state->api.EOS_Connect_CreateUser(state->connect, &options, data, on_user_created);
}

// Exchange the authenticated Epic account's signed ID token for an EOS product identity.
void EOS_CALL on_epic_login(const EOS_Auth_LoginCallbackInfo *info) {
    const auto cell = take<CallbackCell>(info->ClientData);
    const auto state = cell ? cell->state.lock() : nullptr;
    if (!state || state->closed) {
        return;
    }
    if (info->ResultCode != EOS_EResult::EOS_Success || !info->LocalUserId) {
        state->close("EosDeveloperLoginFailed");
        return;
    }
    EOS_Auth_CopyIdTokenOptions copy{};
    copy.ApiVersion = EOS_AUTH_COPYIDTOKEN_API_LATEST;
    copy.AccountId = info->LocalUserId;
    EOS_Auth_IdToken *token = nullptr;
    if (state->api.EOS_Auth_CopyIdToken(state->auth, &copy, &token) != EOS_EResult::EOS_Success || !token) {
        state->close("EosEpicTokenUnavailable");
        return;
    }
    void *data = hold(state, std::make_shared<CallbackCell>());
    if (!data) {
        state->api.EOS_Auth_IdToken_Release(token);
        state->close("EosCallbackUnavailable");
        return;
    }
    EOS_Connect_Credentials credential{};
    credential.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credential.Type = EOS_EExternalCredentialType::EOS_ECT_EPIC_ID_TOKEN;
    credential.Token = token->JsonWebToken;
    EOS_Connect_LoginOptions options{};
    options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    options.Credentials = &credential;
    state->api.EOS_Connect_Login(state->connect, &options, data, on_connect_login);
    state->api.EOS_Auth_IdToken_Release(token);
}

// One pending verification, tied to the native Game Session request and physical PUID.
struct VerifyCell : CallbackCell {
    // The exact connection-generation verification callback.
    Ref<session::CrimsonSessionAuthRequest> request;
    // Canonical PUID obtained from the authenticated physical connection.
    String expected_user;
    // Published lobby associated with this incoming connection attempt.
    std::string listing;
    // Bounded copied JWT retained for the SDK request.
    std::string token;
};

// Publish an identity only if EOS verified the same PUID as the pending physical peer.
void EOS_CALL on_id_token_verified(const EOS_Connect_VerifyIdTokenCallbackInfo *info) {
    const auto cell = take<VerifyCell>(info->ClientData);
    const auto state = cell ? cell->state.lock() : nullptr;
    if (!cell) {
        return;
    }
    if (!state || !state->matches_peer(*info, cell->expected_user, cell->request->get_peer_id())) {
        cell->request->reject();
        return;
    }
    if (!cell->request->is_authority()) {
        accept_eos_peer(cell->request, cell->expected_user);
        return;
    }
    if (cell->listing.empty() || cell->listing != state->published_listing ||
            state->pending_admissions.size() >= max_pending_admissions) {
        cell->request->reject();
        return;
    }
    state->pending_admissions.push_back({cell->request, cell->expected_user, cell->listing,
        Time::get_singleton()->get_ticks_msec() + membership_grace_ms});
}

} // namespace

void CrimsonEOSPoller::_bind_methods() {}

void CrimsonEOSPoller::_process(double) {
    const auto current = state;
    if (!current || current->closed) {
        return;
    }
    if (current->login.is_valid() && Time::get_singleton()->get_ticks_msec() >= current->login_deadline) {
        queue_free();
        current->close("EosLoginTimeout");
        return;
    }
    const ObjectID self(get_instance_id());
    const auto platform = current->platform;
    if (platform) {
        platform->tick();
    }
    if (!current->closed) {
        current->poll_admissions();
    }
    if (current->closed) {
        auto *poller = Object::cast_to<CrimsonEOSPoller>(ObjectDB::get_instance(self));
        if (poller) {
            poller->queue_free();
        }
    }
}

void CrimsonEOSPoller::_exit_tree() {
    const auto current = state;
    if (current) {
        current->close("Cancelled");
    }
}

void CrimsonEOSAuthenticator::_bind_methods() {}

PackedByteArray CrimsonEOSAuthenticator::make_proof(int32_t, bool, const PackedByteArray &challenge) {
    const auto current = state;
    if (!current || !current->ready() || challenge.is_empty() || challenge.size() >= proof_limit) {
        return {};
    }
    EOS_Connect_CopyIdTokenOptions options{};
    options.ApiVersion = EOS_CONNECT_COPYIDTOKEN_API_LATEST;
    options.LocalUserId = current->user;
    EOS_Connect_IdToken *token = nullptr;
    if (current->api.EOS_Connect_CopyIdToken(current->connect, &options, &token) != EOS_EResult::EOS_Success || !token) {
        return {};
    }
    const auto length = token->JsonWebToken ? static_cast<int64_t>(std::strlen(token->JsonWebToken)) : 0;
    PackedByteArray proof;
    if (length > 0 && length <= proof_limit - challenge.size() && proof.resize(challenge.size() + length) == OK) {
        std::memcpy(proof.ptrw(), challenge.ptr(), challenge.size());
        std::memcpy(proof.ptrw() + challenge.size(), token->JsonWebToken, length);
    }
    current->api.EOS_Connect_IdToken_Release(token);
    return proof;
}

void CrimsonEOSAuthenticator::verify(const Ref<session::CrimsonSessionAuthRequest> &request) {
    const auto current = state;
    if (!current || !current->ready() || current->transport.is_null() ||
            (request->is_authority() && current->published_listing.empty())) {
        request->reject();
        return;
    }
    const PackedByteArray challenge = request->get_challenge();
    const PackedByteArray proof = request->get_proof();
    const String expected = current->transport->get_remote_product_user_id(request->get_peer_id());
    if (expected.is_empty() || challenge.is_empty() || proof.size() <= challenge.size() ||
            proof.size() > proof_limit || std::memcmp(proof.ptr(), challenge.ptr(), challenge.size()) != 0 ||
            std::memchr(proof.ptr() + challenge.size(), '\0', proof.size() - challenge.size())) {
        request->reject();
        return;
    }
    const auto cell = std::make_shared<VerifyCell>();
    cell->request = request;
    cell->expected_user = expected;
    cell->listing = current->published_listing;
    cell->token.assign(reinterpret_cast<const char *>(proof.ptr() + challenge.size()),
            static_cast<size_t>(proof.size() - challenge.size()));
    void *data = hold(current, cell);
    if (!data) {
        request->reject();
        return;
    }
    const CharString expected_utf8 = expected.utf8();
    EOS_Connect_IdToken token{};
    token.ApiVersion = EOS_CONNECT_IDTOKEN_API_LATEST;
    token.ProductUserId = current->api.EOS_ProductUserId_FromString(expected_utf8.get_data());
    token.JsonWebToken = cell->token.c_str();
    EOS_Connect_VerifyIdTokenOptions options{};
    options.ApiVersion = EOS_CONNECT_VERIFYIDTOKEN_API_LATEST;
    options.IdToken = &token;
    current->api.EOS_Connect_VerifyIdToken(current->connect, &options, data, on_id_token_verified);
}

namespace {

// Carries one publication through lobby creation and the socket metadata update.
struct PublishCell : CallbackCell {
    // Directory capability canceled when the originating online host leaves.
    Ref<CrimsonOnlineDirectoryRequest> request;
    // Actual P2P socket already opened by that host.
    std::string socket;
    // EOS lobby created by the first SDK completion.
    std::string listing;
    // SDK interface and owner used for a late-create compensation.
    EOS_HLobby lobby = nullptr;
    EOS_ProductUserId owner = nullptr;
};

// Publish only after EOS accepted the socket attribute.
void EOS_CALL on_lobby_updated(const EOS_Lobby_UpdateLobbyCallbackInfo *info) {
    const auto cell = take<PublishCell>(info->ClientData);
    const auto current = cell ? cell->state.lock() : nullptr;
    if (!current || current->closed) {
        return;
    }
    const String listing = String::utf8(cell->listing.c_str());
    if (info->ResultCode != EOS_EResult::EOS_Success || cell->request->is_cancelled() ||
            cell->request->publish(listing) != OK) {
        current->destroy_listing(cell->listing);
        cell->request->fail();
    } else {
        current->published_listing = cell->listing;
    }
}

// Destroy a lobby created after the game owner closed, if its platform is still ticking.
void destroy_late_listing(const PublishCell &cell, EOS_LobbyId listing) {
    const auto platform = cell.platform.lock();
    if (!platform || !platform->get_handle()) {
        return;
    }
    auto destroy = reinterpret_cast<decltype(&EOS_Lobby_DestroyLobby)>(
            platform->get_proc_address("EOS_Lobby_DestroyLobby"));
    if (!destroy) {
        return;
    }
    EOS_Lobby_DestroyLobbyOptions options{};
    options.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
    options.LocalUserId = cell.owner;
    options.LobbyId = listing;
    destroy(cell.lobby, &options, nullptr, EosOnlineState::on_destroyed);
}

// Stage the socket attribute on the newly created, authority-owned EOS Lobby.
void EOS_CALL on_lobby_created(const EOS_Lobby_CreateLobbyCallbackInfo *info) {
    const auto cell = take<PublishCell>(info->ClientData);
    if (!cell) {
        return;
    }
    if (info->ResultCode != EOS_EResult::EOS_Success || !info->LobbyId) {
        cell->request->fail();
        return;
    }
    const auto current = cell->state.lock();
    if (!current || current->closed) {
        destroy_late_listing(*cell, info->LobbyId);
        cell->request->fail();
        return;
    }
    cell->listing = info->LobbyId;
    current->listings.insert(cell->listing);
    if (cell->request->is_cancelled()) {
        current->destroy_listing(cell->listing);
        cell->request->fail();
        return;
    }

    EOS_Lobby_UpdateLobbyModificationOptions modify{};
    modify.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
    modify.LocalUserId = current->user;
    modify.LobbyId = cell->listing.c_str();
    EOS_HLobbyModification handle = nullptr;
    if (current->api.EOS_Lobby_UpdateLobbyModification(current->lobby, &modify, &handle) !=
            EOS_EResult::EOS_Success || !handle) {
        current->destroy_listing(cell->listing);
        cell->request->fail();
        return;
    }
    EOS_Lobby_AttributeData attribute{};
    attribute.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    attribute.Key = socket_attribute;
    attribute.Value.AsUtf8 = cell->socket.c_str();
    attribute.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
    EOS_LobbyModification_AddAttributeOptions add{};
    add.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
    add.Attribute = &attribute;
    add.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
    const auto added = current->api.EOS_LobbyModification_AddAttribute(handle, &add);
    void *data = added == EOS_EResult::EOS_Success ? hold(current, cell) : nullptr;
    if (!data) {
        current->api.EOS_LobbyModification_Release(handle);
        current->destroy_listing(cell->listing);
        cell->request->fail();
        return;
    }
    EOS_Lobby_UpdateLobbyOptions update{};
    update.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
    update.LobbyModificationHandle = handle;
    current->api.EOS_Lobby_UpdateLobby(current->lobby, &update, data, on_lobby_updated);
    current->api.EOS_LobbyModification_Release(handle);
}

// One lobby-ID search, retained until the SDK returns the lookup result.
struct SearchCell : CallbackCell {
    // Request completed with the typed route or an error.
    Ref<CrimsonOnlineDirectoryRequest> request;
    // Search handle owned by the still-live EOS state.
    EOS_HLobbySearch search = nullptr;
    // Exact advertised listing this request is allowed to resolve.
    std::string expected_id;
};

// Holds the exact lobby membership and route while a join or leave is in flight.
struct MembershipCell : CallbackCell {
    // Lookup capability canceled when the originating game retires the join.
    Ref<CrimsonOnlineDirectoryRequest> request;
    // Route seen in the immutable pre-join snapshot.
    Ref<session::CrimsonSessionEndpoint> route;
    // Listing to join or retire.
    std::string listing;
    // SDK lobby interface and local user used for late membership cleanup.
    EOS_HLobby lobby = nullptr;
    EOS_ProductUserId user = nullptr;
};

// Derive the P2P target from the SDK's authenticated lobby owner and bounded metadata.
Ref<session::CrimsonSessionEndpoint> route_from_lobby(const EosOnlineState &current,
        EOS_HLobbyDetails details, const std::string &expected) {
    EOS_LobbyDetails_CopyInfoOptions copy_info{};
    copy_info.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
    EOS_LobbyDetails_Info *info = nullptr;
    if (current.api.EOS_LobbyDetails_CopyInfo(details, &copy_info, &info) != EOS_EResult::EOS_Success || !info) {
        return {};
    }
    const bool matches = info->LobbyId && expected == info->LobbyId && info->BucketId &&
            std::strcmp(info->BucketId, lobby_bucket) == 0;
    const String owner = matches ? current.stringify(info->LobbyOwnerUserId) : String();
    current.api.EOS_LobbyDetails_Info_Release(info);
    if (owner.is_empty() || owner == current.user_string) {
        return {};
    }
    EOS_LobbyDetails_CopyAttributeByKeyOptions copy_attribute{};
    copy_attribute.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;
    copy_attribute.AttrKey = socket_attribute;
    EOS_Lobby_Attribute *attribute = nullptr;
    if (current.api.EOS_LobbyDetails_CopyAttributeByKey(details, &copy_attribute, &attribute) !=
            EOS_EResult::EOS_Success || !attribute) {
        return {};
    }
    Ref<session::CrimsonSessionEndpoint> endpoint;
    if (attribute->Data && attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING &&
            attribute->Data->Value.AsUtf8) {
        endpoint = CrimsonEOSSessionTransport::route(owner, String::utf8(attribute->Data->Value.AsUtf8));
    }
    current.api.EOS_Lobby_Attribute_Release(attribute);
    return endpoint;
}

// Clear the membership guard only when its SDK departure finishes.
void EOS_CALL on_lobby_left(const EOS_Lobby_LeaveLobbyCallbackInfo *info) {
    const auto cell = take<MembershipCell>(info->ClientData);
    const auto current = cell ? cell->state.lock() : nullptr;
    if (current && !current->closed) {
        current->pending_memberships.erase(cell->listing);
    }
}

// Retire one known local membership and guard the listing until the departure settles.
void leave_member(const std::shared_ptr<EosOnlineState> &current, const std::string &listing) {
    if (!current->memberships.erase(listing) || !current->platform || !current->platform->get_handle()) {
        return;
    }
    const auto cell = std::make_shared<MembershipCell>();
    cell->listing = listing;
    void *data = hold(current, cell);
    if (data) {
        current->pending_memberships.insert(listing);
    }
    EOS_Lobby_LeaveLobbyOptions options{};
    options.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
    options.LocalUserId = current->user;
    options.LobbyId = listing.c_str();
    current->api.EOS_Lobby_LeaveLobby(current->lobby, &options, data,
            data ? on_lobby_left : EosOnlineState::on_untracked_departure);
}

// Compensate a successful SDK join that arrived after the owning state was closed.
void leave_late_member(const MembershipCell &cell, EOS_LobbyId listing) {
    const auto platform = cell.platform.lock();
    if (!platform || !platform->get_handle()) {
        return;
    }
    auto leave = reinterpret_cast<decltype(&EOS_Lobby_LeaveLobby)>(
            platform->get_proc_address("EOS_Lobby_LeaveLobby"));
    if (!leave) {
        return;
    }
    EOS_Lobby_LeaveLobbyOptions options{};
    options.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
    options.LocalUserId = cell.user;
    options.LobbyId = listing;
    leave(cell.lobby, &options, nullptr, EosOnlineState::on_untracked_departure);
}

// Resolve only after EOS membership succeeded and the route still matches the owner's lobby.
void EOS_CALL on_lobby_joined(const EOS_Lobby_JoinLobbyCallbackInfo *info) {
    const auto cell = take<MembershipCell>(info->ClientData);
    if (!cell) {
        return;
    }
    const bool joined = info->ResultCode == EOS_EResult::EOS_Success && info->LobbyId;
    const auto current = cell->state.lock();
    if (!current || current->closed) {
        if (joined) {
            leave_late_member(*cell, info->LobbyId);
        }
        cell->request->fail();
        return;
    }
    current->pending_memberships.erase(cell->listing);
    if (!joined) {
        cell->request->fail();
        return;
    }
    current->memberships.insert(info->LobbyId);
    Ref<session::CrimsonSessionEndpoint> route;
    if (!cell->request->is_cancelled() && cell->listing == info->LobbyId) {
        EOS_Lobby_CopyLobbyDetailsHandleOptions options{};
        options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
        options.LobbyId = cell->listing.c_str();
        options.LocalUserId = current->user;
        EOS_HLobbyDetails details = nullptr;
        if (current->api.EOS_Lobby_CopyLobbyDetailsHandle(current->lobby, &options, &details) ==
                EOS_EResult::EOS_Success && details) {
            route = route_from_lobby(*current, details, cell->listing);
            current->api.EOS_LobbyDetails_Release(details);
        }
    }
    if (route.is_null() || route->get_route() != cell->route->get_route() ||
            cell->request->resolve(route) != OK) {
        leave_member(current, info->LobbyId);
        cell->request->fail();
    }
}

// Resolve one exact lobby ID and always release the owned search and details handles.
void EOS_CALL on_lobby_found(const EOS_LobbySearch_FindCallbackInfo *info) {
    const auto cell = take<SearchCell>(info->ClientData);
    const auto current = cell ? cell->state.lock() : nullptr;
    if (!current || current->closed || !current->searches.erase(cell->search)) {
        return;
    }
    Ref<session::CrimsonSessionEndpoint> endpoint;
    EOS_HLobbyDetails details = nullptr;
    if (info->ResultCode == EOS_EResult::EOS_Success && !cell->request->is_cancelled()) {
        EOS_LobbySearch_CopySearchResultByIndexOptions options{};
        options.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
        if (current->api.EOS_LobbySearch_CopySearchResultByIndex(cell->search, &options, &details) ==
                EOS_EResult::EOS_Success && details) {
            endpoint = route_from_lobby(*current, details, cell->expected_id);
        }
    }
    current->api.EOS_LobbySearch_Release(cell->search);
    if (endpoint.is_null() || !current->pending_memberships.empty() || !current->memberships.empty()) {
        if (details) {
            current->api.EOS_LobbyDetails_Release(details);
        }
        cell->request->fail();
        return;
    }
    const auto membership = std::make_shared<MembershipCell>();
    membership->request = cell->request;
    membership->route = endpoint;
    membership->listing = cell->expected_id;
    membership->lobby = current->lobby;
    membership->user = current->user;
    void *data = hold(current, membership);
    if (!data) {
        current->api.EOS_LobbyDetails_Release(details);
        cell->request->fail();
        return;
    }
    current->pending_memberships.insert(membership->listing);
    EOS_Lobby_JoinLobbyOptions options{};
    options.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
    options.LobbyDetailsHandle = details;
    options.LocalUserId = current->user;
    current->api.EOS_Lobby_JoinLobby(current->lobby, &options, data, on_lobby_joined);
    current->api.EOS_LobbyDetails_Release(details);
}

} // namespace

void CrimsonEOSLobbyDirectory::_bind_methods() {}

void CrimsonEOSLobbyDirectory::begin_publish(const Ref<session::CrimsonSessionEndpoint> &endpoint,
        const StringName &, const Ref<CrimsonOnlineDirectoryRequest> &request) {
    const auto current = state;
    if (!current || !current->ready() || current->transport.is_null() || endpoint.is_valid()) {
        request->fail();
        return;
    }
    const String socket = current->transport->get_host_socket();
    const int32_t remote_limit = current->transport->get_host_remote_limit();
    if (socket.is_empty() || remote_limit < 1 || remote_limit >= EOS_LOBBY_MAX_LOBBY_MEMBERS) {
        request->fail();
        return;
    }
    const auto cell = std::make_shared<PublishCell>();
    cell->request = request;
    cell->socket = socket.utf8().get_data();
    cell->owner = current->user;
    cell->lobby = current->lobby;
    void *data = hold(current, cell);
    if (!data) {
        request->fail();
        return;
    }
    EOS_Lobby_CreateLobbyOptions options{};
    options.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    options.LocalUserId = current->user;
    options.MaxLobbyMembers = static_cast<uint32_t>(remote_limit + 1);
    options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    options.BucketId = lobby_bucket;
    options.bDisableHostMigration = EOS_TRUE;
    current->api.EOS_Lobby_CreateLobby(current->lobby, &options, data, on_lobby_created);
}

void CrimsonEOSLobbyDirectory::begin_resolve(const String &listing_id,
        const Ref<CrimsonOnlineDirectoryRequest> &request) {
    const auto current = state;
    if (!current || !current->ready() || !current->pending_memberships.empty() ||
            !current->memberships.empty() || !bounded_text(listing_id, 128)) {
        request->fail();
        return;
    }
    EOS_Lobby_CreateLobbySearchOptions create{};
    create.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    create.MaxResults = 1;
    EOS_HLobbySearch search = nullptr;
    if (current->api.EOS_Lobby_CreateLobbySearch(current->lobby, &create, &search) !=
            EOS_EResult::EOS_Success || !search) {
        request->fail();
        return;
    }
    const auto cell = std::make_shared<SearchCell>();
    cell->request = request;
    cell->search = search;
    cell->expected_id = listing_id.utf8().get_data();
    EOS_LobbySearch_SetLobbyIdOptions id{};
    id.ApiVersion = EOS_LOBBYSEARCH_SETLOBBYID_API_LATEST;
    id.LobbyId = cell->expected_id.c_str();
    const auto set = current->api.EOS_LobbySearch_SetLobbyId(search, &id);
    void *data = set == EOS_EResult::EOS_Success ? hold(current, cell) : nullptr;
    if (!data) {
        current->api.EOS_LobbySearch_Release(search);
        request->fail();
        return;
    }
    current->searches.insert(search);
    EOS_LobbySearch_FindOptions find{};
    find.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
    find.LocalUserId = current->user;
    current->api.EOS_LobbySearch_Find(search, &find, data, on_lobby_found);
}

void CrimsonEOSLobbyDirectory::remove_listing(const String &listing_id) {
    if (state && !state->closed) {
        state->destroy_listing(listing_id.utf8().get_data());
    }
}

void CrimsonEOSLobbyDirectory::leave_listing(const String &listing_id) {
    if (state && !state->closed) {
        leave_member(state, listing_id.utf8().get_data());
    }
}

#endif

void CrimsonEOSProvider::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(),
            D_METHOD("configure", "product_id", "sandbox_id", "deployment_id", "client_id", "client_secret", "force_relay"),
            &CrimsonEOSProvider::configure, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("login_developer", "owner", "address", "credential_name"),
            &CrimsonEOSProvider::login_developer);
    ClassDB::bind_method(D_METHOD("is_ready"), &CrimsonEOSProvider::is_ready);
    ClassDB::bind_method(D_METHOD("get_product_user_id"), &CrimsonEOSProvider::get_product_user_id);
}

Ref<CrimsonEOSProvider> CrimsonEOSProvider::configure(const String &product_id,
        const String &sandbox_id, const String &deployment_id, const String &client_id,
        const String &client_secret, bool force_relay) {
    Ref<CrimsonEOSProvider> provider;
    provider.instantiate();
    provider->product = product_id;
    provider->sandbox = sandbox_id;
    provider->deployment = deployment_id;
    provider->client = client_id;
    provider->secret = client_secret;
    provider->relay_only = force_relay;
    return provider;
}

Ref<CrimsonGameOperation> CrimsonEOSProvider::login_developer(Node *owner,
        const String &address, const String &credential_name) {
    Ref<CrimsonGameOperation> operation;
    operation.instantiate();
#ifdef CRIMSON_WITH_EOS
    if (!on_main_thread() || !owner || !owner->is_inside_tree() || owner->is_queued_for_deletion() ||
            !bounded_text(address, 256) || !bounded_text(credential_name, 256) ||
            !bounded_text(product, EOS_PLATFORM_OPTIONS_PRODUCTID_MAX_LENGTH) ||
            !bounded_text(sandbox, EOS_PLATFORM_OPTIONS_SANDBOXID_MAX_LENGTH) ||
            !bounded_text(deployment, EOS_PLATFORM_OPTIONS_DEPLOYMENTID_MAX_LENGTH) ||
            !bounded_text(client, 512) || !bounded_text(secret, 512)) {
        finish_login(operation, false, "EosInvalidSettings");
        return operation;
    }
    if (state && !state->closed) {
        finish_login(operation, false, "EosLoginAlreadyStarted");
        return operation;
    }
    const CharString product_utf8 = product.utf8();
    const CharString sandbox_utf8 = sandbox.utf8();
    const CharString deployment_utf8 = deployment.utf8();
    const CharString client_utf8 = client.utf8();
    const CharString secret_utf8 = secret.utf8();
    EOS_Platform_Options platform_options{};
    platform_options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform_options.ProductId = product_utf8.get_data();
    platform_options.SandboxId = sandbox_utf8.get_data();
    platform_options.DeploymentId = deployment_utf8.get_data();
    platform_options.ClientCredentials.ClientId = client_utf8.get_data();
    platform_options.ClientCredentials.ClientSecret = secret_utf8.get_data();
    platform_options.Flags = EOS_PF_DISABLE_OVERLAY | EOS_PF_DISABLE_SOCIAL_OVERLAY;
    platform_options.TickBudgetInMilliseconds = 2;
    Error error = OK;
    const auto current = std::make_shared<EosOnlineState>();
    current->product_id = product_utf8.get_data();
    current->sandbox_id = sandbox_utf8.get_data();
    current->deployment_id = deployment_utf8.get_data();
    current->platform = EosPlatform::create(platform_options, error);
    if (!current->platform || !current->api.bind(*current->platform)) {
        finish_login(operation, false, "EosPlatformUnavailable");
        return operation;
    }
    const auto handle = current->platform->get_handle();
    current->auth = current->api.EOS_Platform_GetAuthInterface(handle);
    current->connect = current->api.EOS_Platform_GetConnectInterface(handle);
    current->lobby = current->api.EOS_Platform_GetLobbyInterface(handle);
    if (!current->auth || !current->connect || !current->lobby) {
        finish_login(operation, false, "EosInterfaceUnavailable");
        return operation;
    }
    void *data = hold(current, std::make_shared<CallbackCell>());
    if (!data) {
        finish_login(operation, false, "EosCallbackUnavailable");
        return operation;
    }
    current->login = operation;
    current->login_deadline = Time::get_singleton()->get_ticks_msec() + login_timeout_ms;
    state = current;
    auto *poller = memnew(CrimsonEOSPoller);
    poller->state = current;
    poller->set_process_mode(Node::PROCESS_MODE_ALWAYS);
    const ObjectID poller_id(poller->get_instance_id());
    owner->add_child(poller);
    poller = Object::cast_to<CrimsonEOSPoller>(ObjectDB::get_instance(poller_id));
    if (!poller || poller->get_parent() != owner || poller->is_queued_for_deletion() || current->closed) {
        if (poller && !poller->get_parent()) {
            memdelete(poller);
        }
        current->close("EosPollerUnavailable");
        return operation;
    }
    poller->set_process(true);

    const CharString address_utf8 = address.utf8();
    const CharString name_utf8 = credential_name.utf8();
    EOS_Auth_Credentials credential{};
    credential.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    credential.Id = address_utf8.get_data();
    credential.Token = name_utf8.get_data();
    credential.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
    EOS_Auth_LoginOptions options{};
    options.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    options.Credentials = &credential;
    options.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
    current->api.EOS_Auth_Login(current->auth, &options, data, on_epic_login);
#else
    (void)owner;
    (void)address;
    (void)credential_name;
    finish_login(operation, false, "EosSdkUnavailable");
#endif
    return operation;
}

bool CrimsonEOSProvider::is_ready() const {
#ifdef CRIMSON_WITH_EOS
    return on_main_thread() && state && state->ready();
#else
    return false;
#endif
}

String CrimsonEOSProvider::get_product_user_id() const {
#ifdef CRIMSON_WITH_EOS
    return is_ready() ? state->user_string : String();
#else
    return {};
#endif
}

session::CrimsonGameSession *CrimsonEOSProvider::create_session() {
#ifdef CRIMSON_WITH_EOS
    if (!is_ready() || ObjectDB::get_instance(state->session_id)) {
        return nullptr;
    }
    Ref<CrimsonEOSSessionTransport> transport;
    transport.instantiate();
    if (transport->configure(state->platform, state->user_string, "crimson", relay_only) != OK) {
        return nullptr;
    }
    Ref<CrimsonEOSAuthenticator> auth;
    auth.instantiate();
    auth->state = state;
    auto *session = memnew(crimson::session::CrimsonGameSession);
    if (session->set_transport_adapter(transport) != OK || session->set_authenticator(auth) != OK) {
        memdelete(session);
        return nullptr;
    }
    state->transport = transport;
    state->session_id = ObjectID(session->get_instance_id());
    return session;
#else
    return nullptr;
#endif
}

Ref<CrimsonOnlineDirectory> CrimsonEOSProvider::create_online_directory() {
#ifdef CRIMSON_WITH_EOS
    if (is_ready() && ObjectDB::get_instance(state->session_id)) {
        Ref<CrimsonEOSLobbyDirectory> directory;
        directory.instantiate();
        directory->state = state;
        return directory;
    }
#endif
    return {};
}

} // namespace crimson::integration
