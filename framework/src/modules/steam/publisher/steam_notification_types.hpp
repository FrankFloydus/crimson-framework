// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/publisher/steam_publisher.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>

namespace crimson::steam {

// Immutable localization token and variables for one Steam notification text.
class SteamLocalizedText : public godot::RefCounted {
    GDCLASS(SteamLocalizedText, godot::RefCounted)

public:
    // Typed map used for localized substitutions.
    using Variables = godot::TypedDictionary<godot::StringName, godot::String>;

    // Construct an immutable detached snapshot.
    static godot::Ref<SteamLocalizedText> create(const godot::StringName &token, const Variables &variables);
    // Return the localization token, including its leading #.
    godot::StringName get_token() const;
    // Return the read-only localization substitutions.
    Variables get_variables() const;

protected:
    // Bind the snapshot factory and read-only fields.
    static void _bind_methods();

private:
    // The app's Steam localization token.
    godot::StringName token;
    // Detached localization variables keyed by their Steam token names.
    Variables variables;
};

// One verified recipient's intended state in a notification session.
class SteamNotificationUserState : public godot::RefCounted {
    GDCLASS(SteamNotificationUserState, godot::RefCounted)

public:
    // Asynchronous state published for one session participant.
    enum State {
        WAITING,
        READY,
        DONE,
    };

    // Construct an immutable detached snapshot.
    static godot::Ref<SteamNotificationUserState> create(const godot::Ref<SteamIdentity> &user, State state, const godot::Ref<SteamLocalizedText> &message);
    // Return the backend-verified launch identity.
    godot::Ref<SteamIdentity> get_user() const;
    // Return the desired asynchronous session state.
    State get_state() const;
    // Return the recipient's localized status message.
    godot::Ref<SteamLocalizedText> get_message() const;

protected:
    // Bind the snapshot factory and read-only fields.
    static void _bind_methods();

private:
    // A recipient verified by the same trusted publisher.
    godot::Ref<SteamIdentity> user;
    // Desired notification state.
    State state = WAITING;
    // Localized text shown for this recipient.
    godot::Ref<SteamLocalizedText> message;
};

// Immutable full state for creating or updating an asynchronous game session.
class SteamNotificationSessionSpec : public godot::RefCounted {
    GDCLASS(SteamNotificationSessionSpec, godot::RefCounted)

public:
    // Construct an immutable detached snapshot.
    static godot::Ref<SteamNotificationSessionSpec> create(const godot::String &context, const godot::Ref<SteamLocalizedText> &title, const godot::TypedArray<SteamNotificationUserState> &users);
    // Return the backend context as an unsigned decimal string.
    godot::String get_context() const;
    // Return the session's localized title.
    godot::Ref<SteamLocalizedText> get_title() const;
    // Return the full read-only participant state.
    godot::TypedArray<SteamNotificationUserState> get_users() const;

protected:
    // Bind the snapshot factory and read-only fields.
    static void _bind_methods();

private:
    // Backend context submitted when the session is created.
    godot::String context;
    // Title shown in the recipients' list of sessions.
    godot::Ref<SteamLocalizedText> title;
    // Every verified participant and their desired state.
    godot::TypedArray<SteamNotificationUserState> users;
};

// Detached participant state reported by Steam or accepted by a session mutation.
class SteamNotificationParticipant : public godot::RefCounted {
    GDCLASS(SteamNotificationParticipant, godot::RefCounted)

public:
    // Construct an immutable detached snapshot.
    static godot::Ref<SteamNotificationParticipant> create(const godot::String &steam_id, SteamNotificationUserState::State state, const godot::Ref<SteamLocalizedText> &message);
    // Return the provider's unsigned decimal participant SteamID64.
    godot::String get_steam_id() const;
    // Return the participant's confirmed asynchronous state.
    SteamNotificationUserState::State get_state() const;
    // Return the localized message when available.
    godot::Ref<SteamLocalizedText> get_message() const;

protected:
    // Bind the read-only fields.
    static void _bind_methods();

private:
    // Provider participant account ID. This does not grant trusted identity.
    godot::String steam_id;
    // Participant state accepted or returned by Steam.
    SteamNotificationUserState::State state = SteamNotificationUserState::WAITING;
    // Participant message returned when requested and available.
    godot::Ref<SteamLocalizedText> message;
};

// Detached Steam session identity and the confirmed asynchronous state available for it.
class SteamNotificationSession : public godot::RefCounted {
    GDCLASS(SteamNotificationSession, godot::RefCounted)

public:
    // Construct an immutable detached snapshot.
    static godot::Ref<SteamNotificationSession> create(const godot::String &session_id, const godot::String &context, const godot::Ref<SteamLocalizedText> &title, const godot::TypedArray<SteamNotificationParticipant> &users);
    // Return the exact provider session ID as an unsigned decimal string.
    godot::String get_session_id() const;
    // Return the known creation context, or an empty string when unavailable.
    godot::String get_context() const;
    // Return the confirmed localized session title.
    godot::Ref<SteamLocalizedText> get_title() const;
    // Return the detached read-only participant snapshots.
    godot::TypedArray<SteamNotificationParticipant> get_users() const;

protected:
    // Bind the read-only fields.
    static void _bind_methods();

private:
    // The canonical Steam session ID.
    godot::String session_id;
    // Creation context if known from the request or Steam response.
    godot::String context;
    // The Steam session title.
    godot::Ref<SteamLocalizedText> title;
    // Participant states available in this receipt.
    godot::TypedArray<SteamNotificationParticipant> users;
};

} // namespace crimson::steam

VARIANT_ENUM_CAST(crimson::steam::SteamNotificationUserState::State);
