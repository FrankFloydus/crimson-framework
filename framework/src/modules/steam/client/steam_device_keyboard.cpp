// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_device.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/signal.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/runtime/steam_keyboard_callback.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/text_edit.hpp>
#include <godot_cpp/classes/window.hpp>

#include <array>
#include <cstring>
#include <utility>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// Bound prompt labels and modal text buffers independently of the client's native reports.
constexpr int64_t maximum_prompt_bytes = 1024;
constexpr int64_t maximum_characters = 4096;
constexpr uint32_t maximum_utf8_buffer = maximum_characters * 4 + 1;

// Steam reports each keyboard kind through its own process-wide dismissal callback.
enum class KeyboardKind { FLOATING, MODAL };

// One exact Steam keyboard. A stopped owner leaves this lease active until Steam dismisses it.
struct KeyboardLease {
    // SDK dispatcher keeping this lease alive until a matching event or runtime shutdown.
    std::weak_ptr<SteamRuntime> runtime;
    // Exact originating SteamClient start.
    std::weak_ptr<SteamClientScope> scope;
    // Device receiving completion when still live. Empty after local retirement.
    ObjectID owner;
    // Floating keyboard target. Steam sends OS input to the focused control.
    ObjectID field;
    // Observer retained only by this lease and only while a modal result is pending.
    Ref<SteamTextEntryOperation> observer;
    // Expected SDK callback and matching dismissal action.
    KeyboardKind kind = KeyboardKind::FLOATING;
    // Unique subscription ID in the owning runtime.
    uint64_t subscription = 0;
    // Requested maximum Unicode scalar count.
    uint32_t max_chars = 0;
    // Prevent duplicate accepted dismissal requests while Steam is closing.
    bool dismiss_requested = false;
    // Prevent repeated polling-side dismissal attempts for a removed floating target.
    bool field_retired = false;
};

// Steam owns one keyboard per process. The native subscription holds the active lease strongly.
std::weak_ptr<KeyboardLease> keyboard_lease;

// Read an active rendered main window's client area only when it has OS focus.
DisplayServer *active_display() {
    auto *display = DisplayServer::get_singleton();
    return display && display->get_name() != "headless" && display->window_is_focused() ? display : nullptr;
}

// Check the exact target's local focus and editability without requiring continued OS focus.
bool active_field(Control *field) {
    if (!field || !field->is_inside_tree() || field->is_queued_for_deletion() ||
            !field->is_visible_in_tree() || !field->has_focus()) {
        return false;
    }
    if (auto *line = Object::cast_to<LineEdit>(field)) {
        return line->is_editable();
    }
    if (auto *text = Object::cast_to<TextEdit>(field)) {
        return text->is_editable();
    }
    return false;
}

// Transform the field's local bounds into clipped physical client pixels, including letterbox/stretch.
bool get_field_bounds(Control *field, const DisplayServer &display, Rect2i &out) {
    const Window *window = field->get_tree()->get_root();
    if (!window || field->get_viewport() != window || window->is_embedded()) {
        return false;
    }
    const Transform2D transform = window->get_final_transform() * field->get_global_transform_with_canvas();
    const Rect2 bounds = transform.xform(Rect2(Vector2(), field->get_size()));
    if (!bounds.is_finite() || !bounds.has_area()) {
        return false;
    }
    const Rect2 visible = bounds.intersection(Rect2(Vector2(), Vector2(display.window_get_size())));
    if (!visible.has_area()) {
        return false;
    }
    const Vector2i start(visible.position.floor());
    const Vector2i end(visible.get_end().ceil());
    out = Rect2i(start, end - start);
    return true;
}

// Validate the bounded encoded input passed to Steam's null-terminated text API.
bool valid_text(const CharString &text, int64_t byte_limit) {
    const int64_t length = text.length();
    return length <= byte_limit && (length == 0 || !std::memchr(text.get_data(), 0, static_cast<size_t>(length)));
}

// Drop native routing and process ownership before resuming game code.
void retire_lease(const std::shared_ptr<SteamRuntime> &active, const std::shared_ptr<KeyboardLease> &lease) {
    if (keyboard_lease.lock() == lease) {
        keyboard_lease.reset();
    }
    active->unsubscribe(lease->subscription);
    lease->subscription = 0;
}

// Request the exact native dismiss action while keeping the global reservation.
Error request_dismiss(const std::shared_ptr<SteamRuntime> &active, KeyboardLease &lease) {
    if (lease.dismiss_requested) {
        return OK;
    }
    using Dismiss = decltype(&SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput);
    const auto dismiss = active->symbol<Dismiss>(lease.kind == KeyboardKind::FLOATING ?
            "SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput" : "SteamAPI_ISteamUtils_DismissGamepadTextInput");
    if (!dismiss) {
        return ERR_UNAVAILABLE;
    }
    lease.dismiss_requested = dismiss(active->get_utils());
    return lease.dismiss_requested ? OK : FAILED;
}

// Fetch entered text inside its exact Steam callback and discard any inconsistent native buffer.
Error read_modal_text(const SteamRuntime &active, const KeyboardLease &lease,
        const GamepadTextInputDismissed_t &event, String &text) {
    const auto length = active.symbol<decltype(&SteamAPI_ISteamUtils_GetEnteredGamepadTextLength)>(
            "SteamAPI_ISteamUtils_GetEnteredGamepadTextLength");
    const auto read = active.symbol<decltype(&SteamAPI_ISteamUtils_GetEnteredGamepadTextInput)>(
            "SteamAPI_ISteamUtils_GetEnteredGamepadTextInput");
    const uint32_t size = length ? length(active.get_utils()) : 0;
    if (length && read && size == 0 && event.m_unSubmittedText == 0) {
        return OK;
    }
    std::array<char, maximum_utf8_buffer> buffer{};
    if (size > 0 && size <= lease.max_chars * 4 + 1 && event.m_unSubmittedText <= size &&
            size - event.m_unSubmittedText <= 1 && read && read(active.get_utils(), buffer.data(), size)) {
        const auto *terminator = static_cast<const char *>(std::memchr(buffer.data(), 0, size));
        if (terminator == buffer.data() + size - 1 &&
                (size == 1 || text.parse_utf8(buffer.data(), size - 1) == OK) && text.length() <= lease.max_chars) {
            return OK;
        }
    }
    text = String();
    return ERR_INVALID_DATA;
}

// Consume only a well-formed dismissal for the original native request kind and AppID.
void on_keyboard_closed(const std::shared_ptr<KeyboardLease> &lease, const void *record, size_t size) {
    const auto active = lease->runtime.lock();
    if (!active || keyboard_lease.lock() != lease) {
        return;
    }
    const SteamModalDismissal *event = nullptr;
    if (lease->kind == KeyboardKind::MODAL) {
        if (!record || size != sizeof(SteamModalDismissal)) {
            return;
        }
        event = static_cast<const SteamModalDismissal *>(record);
        if (!event->matches) {
            return;
        }
    } else if (size != 0 && size != sizeof(FloatingGamepadTextInputDismissed_t)) {
        return;
    }
    const auto scope = lease->scope.lock();
    const bool notify = scope && scope->active && lease->owner.is_valid();
    const Ref<SteamTextEntryOperation> observer = std::exchange(lease->observer, {});
    retire_lease(active, lease);
    if (observer.is_valid()) {
        if (!notify) {
            observer->finish(SteamTextEntryResult::create(ERR_UNAVAILABLE, "The Steam keyboard scope stopped."));
        } else if (event->error == OK) {
            observer->finish(SteamTextEntryResult::create(OK, {}, SteamTextEntry::create(event->submitted, event->text)));
        } else {
            observer->finish(SteamTextEntryResult::create(event->error, "Steam did not return valid submitted keyboard text."));
        }
    } else if (notify) {
        const Ref<SteamDevice> device(Object::cast_to<SteamDevice>(ObjectDB::get_instance(lease->owner)));
        if (device.is_valid()) {
            device->emit_signal("keyboard_dismissed");
        }
    }
}

// Reserve the global keyboard before handing control to a native Show call.
std::shared_ptr<KeyboardLease> reserve_keyboard(const std::shared_ptr<SteamRuntime> &active,
        const std::weak_ptr<SteamClientScope> &scope, ObjectID owner, KeyboardKind kind) {
    auto lease = std::make_shared<KeyboardLease>();
    lease->runtime = active;
    lease->scope = scope;
    lease->owner = owner;
    lease->kind = kind;
    const int callback = kind == KeyboardKind::FLOATING ?
            int(FloatingGamepadTextInputDismissed_t::k_iCallback) : int(GamepadTextInputDismissed_t::k_iCallback);
    lease->subscription = active->subscribe(callback, [lease](const void *record, size_t size) {
        on_keyboard_closed(lease, record, size);
    });
    if (!lease->subscription) {
        return {};
    }
    keyboard_lease = lease;
    return lease;
}

} // namespace

SteamModalDismissal copy_steam_modal_dismissal(SteamRuntime &active, const void *record, size_t size) {
    SteamModalDismissal snapshot;
    const auto lease = keyboard_lease.lock();
    if (!lease || lease->runtime.lock().get() != &active || lease->kind != KeyboardKind::MODAL ||
            !record || size != sizeof(GamepadTextInputDismissed_t)) {
        return snapshot;
    }
    const auto submitted = static_cast<const uint8_t *>(record)[offsetof(GamepadTextInputDismissed_t, m_bSubmitted)];
    if (submitted > 1) {
        return snapshot;
    }
    GamepadTextInputDismissed_t event{};
    std::memcpy(&event, record, sizeof(event));
    if (event.m_unAppID != active.get_app_id()) {
        return snapshot;
    }
    snapshot.matches = true;
    snapshot.submitted = event.m_bSubmitted;
    const auto scope = lease->scope.lock();
    if (event.m_bSubmitted && scope && scope->active && lease->observer.is_valid()) {
        snapshot.error = read_modal_text(active, *lease, event, snapshot.text);
    }
    return snapshot;
}
#endif

void SteamTextEntry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_submitted"), &SteamTextEntry::get_submitted);
    ClassDB::bind_method(D_METHOD("get_text"), &SteamTextEntry::get_text);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "submitted"), "", "get_submitted");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "text"), "", "get_text");
}

Ref<SteamTextEntry> SteamTextEntry::create(bool submitted, const String &text) {
    Ref<SteamTextEntry> entry;
    entry.instantiate();
    entry->submitted = submitted;
    entry->text = submitted ? text : String();
    return entry;
}

bool SteamTextEntry::get_submitted() const { return submitted; }
String SteamTextEntry::get_text() const { return text; }

void SteamTextEntryResult::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_value"), &SteamTextEntryResult::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "value", PROPERTY_HINT_RESOURCE_TYPE, "SteamTextEntry"), "", "get_value");
}

Ref<SteamTextEntryResult> SteamTextEntryResult::create(Error error, const String &message,
        const Ref<SteamTextEntry> &value) {
    Ref<SteamTextEntryResult> result;
    result.instantiate();
    result->set_outcome(error, message);
    result->value = value;
    return result;
}

Ref<SteamTextEntry> SteamTextEntryResult::get_value() const { return value; }

void SteamTextEntryOperation::_bind_methods() {
    ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::OBJECT, "result", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamTextEntryResult")));
}

void SteamTextEntryOperation::finish(const Ref<SteamTextEntryResult> &result) {
    if (!finished) {
        finished = true;
        emit_signal("completed", result);
    }
}

void SteamDeviceKeyboard::attach(ObjectID new_owner, const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
    owner = new_owner;
    runtime = active;
    client_scope = scope;
}

void SteamDeviceKeyboard::detach() {
    const ObjectID retired_owner = owner;
    owner = ObjectID();
    const auto active = runtime.lock();
    runtime.reset();
    client_scope.reset();
#ifdef CRIMSON_WITH_STEAM
    const auto lease = keyboard_lease.lock();
    if (!active || !lease || lease->owner != retired_owner || lease->runtime.lock() != active) {
        return;
    }
    lease->owner = ObjectID();
    lease->field = ObjectID();
    lease->scope.reset();
    const auto observer = std::exchange(lease->observer, {});
    request_dismiss(active, *lease);
    if (observer.is_valid()) {
        observer->finish(SteamTextEntryResult::create(ERR_UNAVAILABLE, "The Steam keyboard scope stopped."));
    }
#endif
}

void SteamDeviceKeyboard::tick() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    const auto lease = keyboard_lease.lock();
    if (!active || !lease || lease->owner != owner || lease->runtime.lock() != active ||
            lease->kind != KeyboardKind::FLOATING || lease->dismiss_requested || lease->field_retired) {
        return;
    }
    auto *field = Object::cast_to<Control>(ObjectDB::get_instance(lease->field));
    if (!active_field(field)) {
        lease->field_retired = true;
        request_dismiss(active, *lease);
    }
#endif
}

std::shared_ptr<SteamRuntime> SteamDeviceKeyboard::active_runtime() const {
#ifdef CRIMSON_WITH_STEAM
    if (SteamRuntime::on_main_thread()) {
        const auto scope = client_scope.lock();
        if (scope && scope->active) {
            return runtime.lock();
        }
    }
#endif
    return {};
}

Error SteamDeviceKeyboard::show_keyboard_for(Control *field, int mode) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || !active->get_utils()) {
        return ERR_UNAVAILABLE;
    }
    if (!keyboard_lease.expired()) {
        return ERR_BUSY;
    }
    if (mode < SteamDevice::SINGLE_LINE || mode > SteamDevice::NUMERIC || !active_field(field)) {
        return ERR_INVALID_PARAMETER;
    }
    const DisplayServer *display = active_display();
    if (!display) {
        return ERR_UNAVAILABLE;
    }
    Rect2i bounds;
    if (!get_field_bounds(field, *display, bounds)) {
        return ERR_INVALID_PARAMETER;
    }
    const auto show = active->symbol<decltype(&SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput)>(
            "SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput");
    if (!show) {
        return ERR_UNAVAILABLE;
    }
    const auto lease = reserve_keyboard(active, client_scope, owner, KeyboardKind::FLOATING);
    if (!lease) {
        return ERR_BUSY;
    }
    lease->field = ObjectID(field->get_instance_id());
    if (show(active->get_utils(), static_cast<EFloatingGamepadTextInputMode>(mode),
            bounds.position.x, bounds.position.y, bounds.size.x, bounds.size.y)) {
        return OK;
    }
    retire_lease(active, lease);
#endif
    return ERR_UNAVAILABLE;
}

Variant SteamDeviceKeyboard::enter_text(const String &prompt, const String &initial, int64_t max_chars,
        bool multiline, bool hidden) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (!active || !active->get_utils()) {
        return SteamTextEntryResult::create(ERR_UNAVAILABLE, "The Steam keyboard scope is unavailable.");
    }
    if (!keyboard_lease.expired()) {
        return SteamTextEntryResult::create(ERR_BUSY, "A Steam keyboard is already open or closing.");
    }
    if (max_chars <= 0 || max_chars > maximum_characters || initial.length() > max_chars ||
            prompt.length() > maximum_prompt_bytes) {
        return SteamTextEntryResult::create(ERR_INVALID_PARAMETER, "The Steam keyboard prompt or text limits are invalid.");
    }
    const CharString description = prompt.utf8();
    const CharString existing = initial.utf8();
    if (!valid_text(description, maximum_prompt_bytes) || !valid_text(existing, max_chars * 4)) {
        return SteamTextEntryResult::create(ERR_INVALID_PARAMETER, "The Steam keyboard prompt or text limits are invalid.");
    }
    const auto show = active->symbol<decltype(&SteamAPI_ISteamUtils_ShowGamepadTextInput)>(
            "SteamAPI_ISteamUtils_ShowGamepadTextInput");
    if (show && active_display()) {
        const auto lease = reserve_keyboard(active, client_scope, owner, KeyboardKind::MODAL);
        if (!lease) {
            return SteamTextEntryResult::create(ERR_BUSY, "Steam keyboard callback routing is busy.");
        }
        lease->max_chars = static_cast<uint32_t>(max_chars);
        lease->observer.instantiate();
        if (show(active->get_utils(), hidden ? k_EGamepadTextInputModePassword : k_EGamepadTextInputModeNormal,
                    multiline ? k_EGamepadTextInputLineModeMultipleLines : k_EGamepadTextInputLineModeSingleLine,
                    description.get_data(), lease->max_chars, existing.get_data())) {
            return Signal(lease->observer.ptr(), "completed");
        }
        retire_lease(active, lease);
    }
#endif
    return SteamTextEntryResult::create(ERR_UNAVAILABLE, "Steam could not open a modal keyboard in the active window.");
}

Error SteamDeviceKeyboard::dismiss_keyboard() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    if (active && active->get_utils()) {
        const auto lease = keyboard_lease.lock();
        if (!lease) {
            return ERR_DOES_NOT_EXIST;
        }
        return lease->owner == owner && lease->runtime.lock() == active ? request_dismiss(active, *lease) : ERR_BUSY;
    }
#endif
    return ERR_UNAVAILABLE;
}

void SteamDevice::_bind_keyboard_methods() {
    ClassDB::bind_method(D_METHOD("show_keyboard_for", "field", "mode"), &SteamDevice::show_keyboard_for,
            DEFVAL(SINGLE_LINE));
    ClassDB::bind_method(D_METHOD("enter_text", "prompt", "initial", "max_chars", "multiline", "hidden"),
            &SteamDevice::enter_text, DEFVAL(String()), DEFVAL(64), DEFVAL(false), DEFVAL(false));
    ClassDB::bind_method(D_METHOD("dismiss_keyboard"), &SteamDevice::dismiss_keyboard);
    ADD_SIGNAL(MethodInfo("keyboard_dismissed"));
    BIND_ENUM_CONSTANT(SINGLE_LINE);
    BIND_ENUM_CONSTANT(MULTILINE);
    BIND_ENUM_CONSTANT(EMAIL);
    BIND_ENUM_CONSTANT(NUMERIC);
}

Error SteamDevice::show_keyboard_for(Control *field, FloatingKeyboardMode mode) {
    return keyboard.show_keyboard_for(field, mode);
}

Variant SteamDevice::enter_text(const String &prompt, const String &initial, int64_t max_chars, bool multiline, bool hidden) {
    return keyboard.enter_text(prompt, initial, max_chars, multiline, hidden);
}

Error SteamDevice::dismiss_keyboard() { return keyboard.dismiss_keyboard(); }
void SteamDevice::tick() { keyboard.tick(); }

} // namespace crimson::steam
