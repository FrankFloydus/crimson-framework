// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "session_auth.hpp"

#include "game_session.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::session {

void CrimsonSessionIdentity::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("verified", "provider", "subject"),
            &CrimsonSessionIdentity::verified);
    ClassDB::bind_method(D_METHOD("get_provider"), &CrimsonSessionIdentity::get_provider);
    ClassDB::bind_method(D_METHOD("get_subject"), &CrimsonSessionIdentity::get_subject);
}

Ref<CrimsonSessionIdentity> CrimsonSessionIdentity::verified(const StringName &p_provider, const String &p_subject) {
    const String provider_text = p_provider;
    if (provider_text.strip_edges().is_empty() || provider_text.length() > 64 ||
            p_subject.strip_edges().is_empty() || p_subject.length() > 256) {
        return {};
    }
    Ref<CrimsonSessionIdentity> identity;
    identity.instantiate();
    identity->provider = p_provider;
    identity->subject = p_subject;
    return identity;
}

StringName CrimsonSessionIdentity::get_provider() const {
    return provider;
}

String CrimsonSessionIdentity::get_subject() const {
    return subject;
}

void CrimsonSessionAuthRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_peer_id"), &CrimsonSessionAuthRequest::get_peer_id);
    ClassDB::bind_method(D_METHOD("is_authority"), &CrimsonSessionAuthRequest::is_authority);
    ClassDB::bind_method(D_METHOD("get_challenge"), &CrimsonSessionAuthRequest::get_challenge);
    ClassDB::bind_method(D_METHOD("get_proof"), &CrimsonSessionAuthRequest::get_proof);
    ClassDB::bind_method(D_METHOD("accept", "identity"), &CrimsonSessionAuthRequest::accept);
    ClassDB::bind_method(D_METHOD("reject"), &CrimsonSessionAuthRequest::reject);
}

int32_t CrimsonSessionAuthRequest::get_peer_id() const {
    return peer_id;
}

bool CrimsonSessionAuthRequest::is_authority() const {
    return authority;
}

PackedByteArray CrimsonSessionAuthRequest::get_challenge() const {
    return challenge;
}

PackedByteArray CrimsonSessionAuthRequest::get_proof() const {
    return proof;
}

Error CrimsonSessionAuthRequest::accept(const Ref<CrimsonSessionIdentity> &identity) {
    if (identity.is_null() || identity->get_provider().is_empty() || identity->get_subject().is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    return finish(identity, true);
}

Error CrimsonSessionAuthRequest::reject() {
    return finish({}, false);
}

Error CrimsonSessionAuthRequest::finish(const Ref<CrimsonSessionIdentity> &identity, bool accepted) {
    const OS *os = OS::get_singleton();
    if (os->get_thread_caller_id() != os->get_main_thread_id()) {
        return ERR_BUSY;
    }
    if (!session_id.is_valid()) {
        return ERR_DOES_NOT_EXIST;
    }
    auto *session = Object::cast_to<CrimsonGameSession>(ObjectDB::get_instance(session_id));
    return session ? session->finish_auth(*this, identity, accepted) : ERR_DOES_NOT_EXIST;
}

void CrimsonSessionAuthenticator::_bind_methods() {
    GDVIRTUAL_BIND(_make_proof, "peer_id", "authority", "challenge");
    GDVIRTUAL_BIND(_verify, "request");
}

PackedByteArray CrimsonSessionAuthenticator::make_proof(int32_t peer_id, bool authority,
        const PackedByteArray &challenge) {
    PackedByteArray proof;
    GDVIRTUAL_CALL(_make_proof, peer_id, authority, challenge, proof);
    return proof;
}

void CrimsonSessionAuthenticator::verify(const Ref<CrimsonSessionAuthRequest> &request) {
    if (!GDVIRTUAL_CALL(_verify, request)) {
        request->reject();
    }
}

} // namespace crimson::session
