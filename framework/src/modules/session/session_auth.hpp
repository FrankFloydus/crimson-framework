// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cstdint>

namespace crimson::session {

class GameSession;

// A provider-verified account identity for an authenticated connection.
class SessionIdentity : public godot::RefCounted {
    GDCLASS(SessionIdentity, godot::RefCounted)

    // Namespace controlled by the verifier.
    godot::StringName provider;
    // Stable provider account identifier, never an authentication credential.
    godot::String subject;

protected:
    // Register the identity factory and read-only accessors.
    static void _bind_methods();

public:
    // Construct a validated identity, or return null for invalid fields.
    static godot::Ref<SessionIdentity> verified(const godot::StringName &p_provider,
            const godot::String &p_subject);
    // Return the verified provider namespace.
    godot::StringName get_provider() const;
    // Return the stable account identifier in that provider.
    godot::String get_subject() const;
};

// One authority or guest verification tied to a pending connection and session generation.
class SessionAuthRequest : public godot::RefCounted {
    GDCLASS(SessionAuthRequest, godot::RefCounted)

    friend class GameSession;

    // Weak ID of the native session issuing the request.
    godot::ObjectID session_id;
    // Lifecycle generation during which this proof was received.
    uint64_t generation = 0;
    // Monotonic attempt ID that distinguishes reuse of the same peer ID.
    uint64_t attempt = 0;
    // Actual Godot connection ID from the authentication callback.
    int32_t peer_id = 0;
    // Whether the local verifier is the gameplay authority.
    bool authority = false;
    // Challenge issued to this connection.
    godot::PackedByteArray challenge;
    // Received proof bytes for the trusted provider to verify.
    godot::PackedByteArray proof;

    // Complete this exact attempt through its still-live native session.
    godot::Error finish(const godot::Ref<SessionIdentity> &identity, bool accepted);

protected:
    // Register verification request getters and completion actions.
    static void _bind_methods();

public:
    // Return the connection whose proof is being verified.
    int32_t get_peer_id() const;
    // Return whether the local verifier is acting as authority.
    bool is_authority() const;
    // Return the challenge that the proof must answer.
    godot::PackedByteArray get_challenge() const;
    // Return the bounded proof received from this connection.
    godot::PackedByteArray get_proof() const;
    // Settle this attempt with a verified identity on the scene thread.
    godot::Error accept(const godot::Ref<SessionIdentity> &identity);
    // Deny this attempt on the scene thread.
    godot::Error reject();
};

// Trusted game or platform adapter for connection proof creation and verification.
class SessionAuthenticator : public godot::Resource {
    GDCLASS(SessionAuthenticator, godot::Resource)

    friend class GameSession;

protected:
    // Answer the remote challenge from locally prepared credentials. Empty denies the connection.
    GDVIRTUAL3R(godot::PackedByteArray, _make_proof, int32_t, bool, godot::PackedByteArray)
    // Verify the remote response, and accept or reject the request on the scene thread.
    GDVIRTUAL1(_verify, godot::Ref<SessionAuthRequest>)
    // Register the typed authentication hooks.
    static void _bind_methods();
    // Build a proof natively or invoke the scripted hook.
    virtual godot::PackedByteArray make_proof(int32_t peer_id, bool authority,
            const godot::PackedByteArray &challenge);
    // Verify natively or invoke the scripted hook, rejecting when it is absent.
    virtual void verify(const godot::Ref<SessionAuthRequest> &request);
};

} // namespace crimson::session
