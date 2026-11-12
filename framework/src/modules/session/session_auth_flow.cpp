// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "game_session.hpp"

#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace godot;

namespace crimson::session {
namespace {

constexpr uint8_t auth_version = 1;
constexpr uint8_t challenge_frame = 1;
constexpr uint8_t proof_frame = 2;
constexpr int64_t frame_header_size = 2;
constexpr int64_t challenge_size = 32;
constexpr int64_t max_proof_size = 4096;
constexpr size_t max_pending = 64;
constexpr double max_auth_timeout = 30.0;
constexpr int32_t authority_id = 1;

constexpr std::array<const char *, 2> pending_signals = { "peer_authenticating", "peer_authentication_failed" };

// Prefix the bounded payload with the native framing version and message kind.
PackedByteArray make_frame(uint8_t kind, const PackedByteArray &payload) {
    PackedByteArray data;
    if (data.resize(frame_header_size + payload.size()) != OK) {
        return {};
    }
    uint8_t *bytes = data.ptrw();
    bytes[0] = auth_version;
    bytes[1] = kind;
    std::copy_n(payload.ptr(), payload.size(), bytes + frame_header_size);
    return data;
}

// Validate engine peer IDs before narrowing them for SceneMultiplayer.
bool valid_peer_id(int64_t peer_id) {
    return peer_id > 0 && peer_id <= std::numeric_limits<int32_t>::max();
}

} // namespace

Error CrimsonGameSession::set_authenticator(const Ref<CrimsonSessionAuthenticator> &authenticator) {
    if (state != IDLE || ending || opening) {
        return ERR_BUSY;
    }
    configured_authenticator = authenticator;
    return OK;
}

Ref<CrimsonSessionAuthenticator> CrimsonGameSession::get_authenticator() const {
    return configured_authenticator;
}

Ref<CrimsonSessionIdentity> CrimsonGameSession::get_peer_identity(int64_t peer_id) const {
    if (!valid_peer_id(peer_id) || (state != HOSTING && state != CLIENT) || !owns_auth_scope()) {
        return {};
    }
    const auto found = identities.find(static_cast<int32_t>(peer_id));
    return found == identities.end() ? Ref<CrimsonSessionIdentity>() : found->second;
}

Error CrimsonGameSession::check_auth_scope(const Ref<MultiplayerAPI> &api) const {
    if (configured_authenticator.is_null()) {
        return OK;
    }
    const Ref<SceneMultiplayer> scene_api = api;
    if (scene_api.is_null()) {
        return ERR_UNAVAILABLE;
    }
    if (scene_api->get_auth_callback().is_valid()) {
        return ERR_ALREADY_IN_USE;
    }
    const double timeout = scene_api->get_auth_timeout();
    return std::isfinite(timeout) && timeout > 0.0 && timeout <= max_auth_timeout ? OK : ERR_INVALID_PARAMETER;
}

Error CrimsonGameSession::connect_auth(const Ref<MultiplayerAPI> &api) {
    if (configured_authenticator.is_null()) {
        return OK;
    }
    const Error available = check_auth_scope(api);
    if (available != OK) {
        return available;
    }
    auth_api = api;
    previous_auth_callback = auth_api->get_auth_callback();
    auth_signals = { callable_mp(this, &CrimsonGameSession::on_peer_authenticating),
        callable_mp(this, &CrimsonGameSession::on_peer_authentication_failed) };
    for (size_t index = 0; index < auth_signals.size(); ++index) {
        const Error connected = auth_api->connect(pending_signals[index], auth_signals[index]);
        if (connected != OK) {
            return connected;
        }
    }
    auth_callback = callable_mp(this, &CrimsonGameSession::on_auth_data);
    auth_api->set_auth_callback(auth_callback);
    return OK;
}

void CrimsonGameSession::disconnect_auth() {
    if (auth_api.is_valid()) {
        for (size_t index = 0; index < auth_signals.size(); ++index) {
            if (auth_api->is_connected(pending_signals[index], auth_signals[index])) {
                auth_api->disconnect(pending_signals[index], auth_signals[index]);
            }
        }
        if (auth_callback.is_valid() && auth_api->get_auth_callback() == auth_callback) {
            auth_api->set_auth_callback(previous_auth_callback);
        }
    }
    auth_signals = {};
    auth_callback = {};
    previous_auth_callback = {};
    auth_api.unref();
    auth_attempts.clear();
    identities.clear();
    auth_failed = false;
}

bool CrimsonGameSession::owns_auth_scope() const {
    return auth_api.is_valid() && owns_current_transport() && auth_api->get_auth_callback() == auth_callback;
}

void CrimsonGameSession::on_peer_authenticating(int64_t peer_id) {
    if (!valid_peer_id(peer_id) || !owns_auth_scope()) {
        return;
    }
    const auto id = static_cast<int32_t>(peer_id);
    const bool valid_host = state == HOSTING && id > authority_id && !transport->is_refusing_new_connections() &&
            auth_attempts.size() + connected_peers.size() < remote_limit;
    const bool valid_client = state == CONNECTING && id == authority_id && !auth_failed;
    if ((!valid_host && !valid_client) || auth_attempts.size() >= max_pending || auth_attempts.count(id)) {
        reject_auth_peer(id);
        return;
    }

    Ref<Crypto> entropy;
    entropy.instantiate();
    const PackedByteArray challenge = entropy->generate_random_bytes(challenge_size);
    if (challenge.size() != challenge_size) {
        reject_auth_peer(id);
        return;
    }
    auth_attempts.emplace(id, AuthAttempt{ ++next_auth_attempt, AuthStage::CHALLENGE, challenge, {}, {}, {} });
    const Ref<SceneMultiplayer> api = auth_api;
    const uint64_t active_generation = generation;
    const PackedByteArray frame = make_frame(challenge_frame, challenge);
    if ((frame.is_empty() || api->send_auth(id, frame) != OK) && generation == active_generation) {
        reject_auth_peer(id);
    }
}

void CrimsonGameSession::on_peer_authentication_failed(int64_t peer_id) {
    if (!valid_peer_id(peer_id) || !owns_auth_scope()) {
        return;
    }
    auth_attempts.erase(static_cast<int32_t>(peer_id));
    if (state == CONNECTING && peer_id == authority_id) {
        auth_failed = true;
    }
}

void CrimsonGameSession::on_auth_data(int64_t peer_id, const PackedByteArray &data) {
    if (!valid_peer_id(peer_id) || !owns_auth_scope()) {
        return;
    }
    const auto id = static_cast<int32_t>(peer_id);
    auto pending = auth_attempts.find(id);
    if (pending == auth_attempts.end() || data.size() <= frame_header_size ||
            data.size() > frame_header_size + max_proof_size || data[0] != auth_version) {
        reject_auth_peer(id);
        return;
    }

    if (data[1] == challenge_frame && pending->second.stage == AuthStage::CHALLENGE &&
            data.size() == frame_header_size + challenge_size) {
        pending->second.remote_challenge = data.slice(frame_header_size);
        pending->second.stage = AuthStage::CHALLENGE_RECEIVED;
        return;
    }
    const AuthStage stage = pending->second.stage;
    if (data[1] == proof_frame && pending->second.remote_proof.is_empty() &&
            (stage == AuthStage::CHALLENGE_RECEIVED || stage == AuthStage::BUILDING_PROOF || stage == AuthStage::PROOF)) {
        pending->second.remote_proof = data.slice(frame_header_size);
        return;
    }
    reject_auth_peer(id);
}

Error CrimsonGameSession::finish_auth(const CrimsonSessionAuthRequest &request,
        const Ref<CrimsonSessionIdentity> &identity, bool accepted) {
    auto pending = auth_attempts.find(request.peer_id);
    if (request.generation != generation || !owns_auth_scope() || pending == auth_attempts.end() ||
            pending->second.id != request.attempt || pending->second.stage != AuthStage::VERIFYING ||
            !auth_api->get_authenticating_peers().has(request.peer_id)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (accepted) {
        pending->second.identity = identity;
        pending->second.stage = AuthStage::VERIFIED;
    } else {
        reject_auth_peer(request.peer_id);
    }
    return OK;
}

void CrimsonGameSession::process_authentication() {
    std::array<int32_t, max_pending> pending_ids;
    size_t count = 0;
    for (const auto &[id, attempt] : auth_attempts) {
        if (attempt.stage == AuthStage::CHALLENGE_RECEIVED || attempt.stage == AuthStage::VERIFIED ||
                (attempt.stage == AuthStage::PROOF && !attempt.remote_proof.is_empty()) || attempt.engine_connected) {
            pending_ids[count++] = id;
        }
    }
    const uint64_t active_generation = generation;
    for (size_t index = 0; index < count; ++index) {
        if (generation != active_generation || !owns_auth_scope()) {
            return;
        }
        advance_auth_attempt(pending_ids[index]);
    }
}

void CrimsonGameSession::advance_auth_attempt(int32_t peer_id) {
    auto pending = auth_attempts.find(peer_id);
    if (pending == auth_attempts.end()) {
        return;
    }
    const uint64_t attempt_id = pending->second.id;
    const uint64_t active_generation = generation;
    const Ref<SceneMultiplayer> api = auth_api;

    if (pending->second.stage == AuthStage::CHALLENGE_RECEIVED) {
        const Ref<CrimsonSessionAuthenticator> authenticator = configured_authenticator;
        const PackedByteArray challenge = pending->second.remote_challenge;
        pending->second.remote_challenge.clear();
        pending->second.stage = AuthStage::BUILDING_PROOF;
        const PackedByteArray proof = authenticator->make_proof(peer_id, state == HOSTING, challenge);
        pending = auth_attempts.find(peer_id);
        if (generation != active_generation || !owns_auth_scope() || pending == auth_attempts.end() ||
                pending->second.id != attempt_id || pending->second.stage != AuthStage::BUILDING_PROOF) {
            return;
        }
        const PackedByteArray frame = proof.is_empty() || proof.size() > max_proof_size ? PackedByteArray() :
                make_frame(proof_frame, proof);
        pending->second.stage = AuthStage::PROOF;
        if ((frame.is_empty() || api->send_auth(peer_id, frame) != OK) && generation == active_generation) {
            reject_auth_peer(peer_id);
        }
        return;
    }

    if (pending->second.stage == AuthStage::PROOF && !pending->second.remote_proof.is_empty()) {
        const Ref<CrimsonSessionAuthenticator> authenticator = configured_authenticator;
        pending->second.stage = AuthStage::VERIFYING;
        Ref<CrimsonSessionAuthRequest> request;
        request.instantiate();
        request->session_id = ObjectID(get_instance_id());
        request->generation = generation;
        request->attempt = attempt_id;
        request->peer_id = peer_id;
        request->authority = state == HOSTING;
        request->challenge = pending->second.challenge;
        request->proof = pending->second.remote_proof;
        pending->second.remote_proof.clear();
        authenticator->verify(request);
        return;
    }

    if (pending->second.stage == AuthStage::VERIFIED) {
        pending->second.stage = AuthStage::CONFIRMING;
        const Error completed = api->complete_auth(peer_id);
        pending = auth_attempts.find(peer_id);
        if (generation != active_generation || !owns_auth_scope() || pending == auth_attempts.end() ||
                pending->second.id != attempt_id || pending->second.stage != AuthStage::CONFIRMING) {
            return;
        }
        if (completed != OK) {
            reject_auth_peer(peer_id);
            return;
        }
        pending->second.stage = AuthStage::WAITING_REMOTE;
    }

    if (pending->second.stage == AuthStage::WAITING_REMOTE && pending->second.engine_connected &&
            admit_authenticated_peer(peer_id) && state == HOSTING) {
        on_peer_connected(peer_id);
    }
}

bool CrimsonGameSession::admit_authenticated_peer(int32_t peer_id) {
    auto pending = auth_attempts.find(peer_id);
    if (!owns_auth_scope() || pending == auth_attempts.end() || pending->second.stage != AuthStage::WAITING_REMOTE ||
            !pending->second.engine_connected) {
        return false;
    }
    identities.emplace(peer_id, pending->second.identity);
    auth_attempts.erase(pending);
    return true;
}

void CrimsonGameSession::reject_auth_peer(int32_t peer_id) {
    auth_attempts.erase(peer_id);
    identities.erase(peer_id);
    if (state == CONNECTING && peer_id == authority_id) {
        auth_failed = true;
    }
    if (auth_api.is_valid() && owns_current_transport() &&
            transport->get_connection_status() == MultiplayerPeer::CONNECTION_CONNECTED) {
        const Ref<SceneMultiplayer> api = auth_api;
        api->disconnect_peer(peer_id);
    }
}

} // namespace crimson::session
