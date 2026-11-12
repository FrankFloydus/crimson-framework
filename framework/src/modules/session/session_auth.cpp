// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "session_auth.hpp"

#include "game_session.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace crimson::session {

void SessionIdentity::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("verified", "provider", "subject"),
            &SessionIdentity::verified);
    ClassDB::bind_method(D_METHOD("get_provider"), &SessionIdentity::get_provider);
    ClassDB::bind_method(D_METHOD("get_subject"), &SessionIdentity::get_subject);
}

Ref<SessionIdentity> SessionIdentity::verified(const StringName &p_provider, const String &p_subject) {
    const String provider_text = p_provider;
    if (provider_text.strip_edges().is_empty() || provider_text.length() > 64 ||
            p_subject.strip_edges().is_empty() || p_subject.length() > 256) {
        return {};
    }
    Ref<SessionIdentity> identity;
    identity.instantiate();
    identity->provider = p_provider;
    identity->subject = p_subject;
    return identity;
}

StringName SessionIdentity::get_provider() const {
    return provider;
}

String SessionIdentity::get_subject() const {
    return subject;
}

void SessionAuthRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_peer_id"), &SessionAuthRequest::get_peer_id);
    ClassDB::bind_method(D_METHOD("is_authority"), &SessionAuthRequest::is_authority);
    ClassDB::bind_method(D_METHOD("get_challenge"), &SessionAuthRequest::get_challenge);
    ClassDB::bind_method(D_METHOD("get_proof"), &SessionAuthRequest::get_proof);
    ClassDB::bind_method(D_METHOD("accept", "identity"), &SessionAuthRequest::accept);
    ClassDB::bind_method(D_METHOD("reject"), &SessionAuthRequest::reject);
}

int32_t SessionAuthRequest::get_peer_id() const {
    return peer_id;
}

bool SessionAuthRequest::is_authority() const {
    return authority;
}

PackedByteArray SessionAuthRequest::get_challenge() const {
    return challenge;
}

PackedByteArray SessionAuthRequest::get_proof() const {
    return proof;
}

Error SessionAuthRequest::accept(const Ref<SessionIdentity> &identity) {
    if (identity.is_null() || identity->get_provider().is_empty() || identity->get_subject().is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    return finish(identity, true);
}

Error SessionAuthRequest::reject() {
    return finish({}, false);
}

Error SessionAuthRequest::finish(const Ref<SessionIdentity> &identity, bool accepted) {
    const OS *os = OS::get_singleton();
    if (os->get_thread_caller_id() != os->get_main_thread_id()) {
        return ERR_BUSY;
    }
    if (!session_id.is_valid()) {
        return ERR_DOES_NOT_EXIST;
    }
    auto *session = Object::cast_to<GameSession>(ObjectDB::get_instance(session_id));
    return session ? session->finish_auth(*this, identity, accepted) : ERR_DOES_NOT_EXIST;
}

void SessionAuthenticator::_bind_methods() {
    GDVIRTUAL_BIND(_make_proof, "peer_id", "authority", "challenge");
    GDVIRTUAL_BIND(_verify, "request");
}

PackedByteArray SessionAuthenticator::make_proof(int32_t peer_id, bool authority,
        const PackedByteArray &challenge) {
    PackedByteArray proof;
    GDVIRTUAL_CALL(_make_proof, peer_id, authority, challenge, proof);
    return proof;
}

void SessionAuthenticator::verify(const Ref<SessionAuthRequest> &request) {
    if (!GDVIRTUAL_CALL(_verify, request)) {
        request->reject();
    }
}

} // namespace crimson::session
