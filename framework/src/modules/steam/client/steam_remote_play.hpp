// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstdint>
#include <memory>

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Snapshot of a Steam stream, usable as a key for a game-owned local seat.
class SteamRemotePlaySession : public godot::RefCounted {
    GDCLASS(SteamRemotePlaySession, godot::RefCounted)

public:
    // Return the Steam stream identifier, unique among currently connected streams.
    int64_t get_session_id() const;
    // Return the remote individual SteamID64, or an empty string when Steam has no account ID.
    godot::String get_steam_id() const;
    // Return the reported client display size in pixels or zero when unknown.
    godot::Vector2i get_client_size() const;
    // Report whether the stream was created through a Remote Play Together invitation.
    bool get_together() const;

protected:
    // Register the immutable stream fields.
    static void _bind_methods();

private:
    friend class SteamRemotePlay;

    // Copied Steam stream ID, valid independently of the connection's lifetime.
    uint32_t session_id = 0;
    // Decimal SteamID64, left empty for an anonymous guest.
    godot::String steam_id;
    // Client resolution when available from Steam.
    godot::Vector2i client_size;
    // Whether Steam marked this session as Remote Play Together.
    bool together = false;
};

// Detached input sample for a single Remote Play Together seat.
class SteamRemotePlayInputEvent : public godot::RefCounted {
    GDCLASS(SteamRemotePlayInputEvent, godot::RefCounted)

public:
    // Return the stream identifier that supplied this sample.
    int64_t get_session_id() const;
    // Return the local Godot event representation without sending it to the Input singleton.
    godot::Ref<godot::InputEvent> get_event() const;
    // Report whether this motion event includes a normalized absolute position.
    bool get_has_absolute_position() const;

protected:
    // Register the immutable input envelope fields.
    static void _bind_methods();

private:
    friend class SteamRemotePlay;

    // Copied stream ID used to route the sample to the correct seat.
    uint32_t session_id = 0;
    // One converted keyboard or mouse event owned by this sample.
    godot::Ref<godot::InputEvent> event;
    // Whether motion.position contains Steam's normalized coordinates.
    bool has_absolute_position = false;
};

// Observes Steam Remote Play streams and optionally exposes separate keyboard and mouse samples.
class SteamRemotePlay : public godot::RefCounted {
    GDCLASS(SteamRemotePlay, godot::RefCounted)

public:
    // Report whether this client has Steam Remote Play session observation available.
    bool is_available() const;
    // Return a read-only snapshot of connected streams for the process AppID.
    godot::TypedArray<SteamRemotePlaySession> sessions() const;
    // Request Steam's Remote Play Together panel when the overlay is ready.
    godot::Error open_together_overlay() const;
    // Send an invitation to an individual SteamID64 after a user requests it.
    godot::Error send_invite(const godot::String &steam_id) const;
    // Acquire or release process-wide separate keyboard and mouse input for this scope.
    godot::Error set_separate_input(bool enabled);
    // Copy at most limit input samples for the exact scope that enabled separate input.
    godot::TypedArray<SteamRemotePlayInputEvent> poll_input(int64_t limit = 64) const;

    // Subscribe to the original client start's stream callbacks.
    void attach(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<SteamClientScope> &scope);
    // Release callbacks and restore merged input if this facade owns the process input lease.
    void detach();

protected:
    // Register typed queries, actions, and stream signals.
    static void _bind_methods();

private:
    // Resolve this facade's runtime on the main thread while its exact scope is live.
    std::shared_ptr<SteamRuntime> active_runtime() const;
    // Copy session metadata while the current Steam interface owns it.
    godot::Ref<SteamRemotePlaySession> read_session(const std::shared_ptr<SteamRuntime> &active, uint32_t id) const;
    // Emit a connected or disconnected signal using a copied native ID.
    void on_session(uint32_t id, bool connected);
    // Restore merged input for the owner, including teardown after its scope token has expired.
    void release_separate_input(const std::shared_ptr<SteamRuntime> &active);

    // Runtime shared by active client starts without keeping a stopped runtime alive.
    std::weak_ptr<SteamRuntime> runtime;
    // Liveness of this facade's original start generation.
    std::weak_ptr<SteamClientScope> client_scope;
    // Dispatcher subscription for connected stream callbacks.
    uint64_t connected_subscription = 0;
    // Dispatcher subscription for disconnected stream callbacks.
    uint64_t disconnected_subscription = 0;
};

} // namespace crimson::steam
