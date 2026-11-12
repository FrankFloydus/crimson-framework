// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "modules/steam/client/steam_result.hpp"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <memory>

namespace godot {
class Control;
}

namespace crimson::steam {

class SteamRuntime;
struct SteamClientScope;

// Completed modal text input, with explicit distinction between submission and dismissal.
class SteamTextEntry : public godot::RefCounted {
    GDCLASS(SteamTextEntry, godot::RefCounted)

public:
    // Copy accepted text or a successful dismissal into a new snapshot.
    static godot::Ref<SteamTextEntry> create(bool submitted, const godot::String &text = {});
    // Return whether the user explicitly submitted text, including an intentionally empty string.
    bool get_submitted() const;
    // Return the submitted text, or empty text after dismissal.
    godot::String get_text() const;

protected:
    // Register the read-only modal result fields.
    static void _bind_methods();

private:
    // Whether the user accepted the modal dialog.
    bool submitted = false;
    // Copied user-entered text exposed only on the result.
    godot::String text;
};

// Terminal outcome of a modal Steam keyboard request.
class SteamTextEntryResult : public SteamResult {
    GDCLASS(SteamTextEntryResult, SteamResult)

public:
    // Construct one settled outcome and copy its typed value.
    static godot::Ref<SteamTextEntryResult> create(godot::Error error, const godot::String &message,
            const godot::Ref<SteamTextEntry> &value = {});
    // Return the user decision or null when the dialog failed or its owner stopped.
    godot::Ref<SteamTextEntry> get_value() const;

protected:
    // Register the typed read-only payload.
    static void _bind_methods();

private:
    // Submitted text or successful user dismissal.
    godot::Ref<SteamTextEntry> value;
};

// Internal completion signal for one exact awaitable modal request.
class SteamTextEntryOperation : public godot::RefCounted {
    GDCLASS(SteamTextEntryOperation, godot::RefCounted)

public:
    // Emit one terminal result after the process broker retires its observer.
    void finish(const godot::Ref<SteamTextEntryResult> &result);

protected:
    // Register only the internal completion signal.
    static void _bind_methods();

private:
    // Guard against completing the same awaitable twice.
    bool finished = false;
};

// Scope-local client of the process keyboard broker. Kept inside SteamDevice.
class SteamDeviceKeyboard {
public:
    // Record the owner for the original SteamDevice start generation.
    void attach(godot::ObjectID owner, const std::shared_ptr<SteamRuntime> &active,
            const std::shared_ptr<SteamClientScope> &scope);
    // Retire this scope's observers and request native dismissal while preserving the broker reservation.
    void detach();
    // Dismiss the owned floating keyboard when its original field leaves or loses focus.
    void tick();
    // Open the floating keyboard beside a visible focused editable field in the active game window.
    godot::Error show_keyboard_for(godot::Control *field, int mode);
    // Await the Steam modal user's submission or dismissal.
    godot::Variant enter_text(const godot::String &prompt, const godot::String &initial,
            int64_t max_chars, bool multiline, bool hidden);
    // Ask Steam to dismiss the active keyboard if this scope owns it.
    godot::Error dismiss_keyboard();

private:
    // Hold the SDK runtime only during an active main-thread operation.
    std::shared_ptr<SteamRuntime> active_runtime() const;

    // SteamDevice receiving floating dismissal signals.
    godot::ObjectID owner;
    // SDK runtime shared with other live devices.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact scope liveness across standalone and game-owned Steam clients.
    std::weak_ptr<SteamClientScope> client_scope;
};

} // namespace crimson::steam
