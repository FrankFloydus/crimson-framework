// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "modules/steam/client/steam_remote_play.hpp"

#include "modules/steam/client/steam_client_scope.hpp"

#include <godot_cpp/core/class_db.hpp>

#ifdef CRIMSON_WITH_STEAM
#include "modules/steam/client/steam_ids.hpp"
#include "modules/steam/runtime/steam_runtime.hpp"

#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#endif

namespace crimson::steam {
using namespace godot;

#ifdef CRIMSON_WITH_STEAM
namespace {

// The Steam keyboard and mouse switch belongs to the process. Only its owner can drain or release it.
ObjectID separate_input_owner;
// Bound each Godot input allocation batch even when the caller requests an excessive limit.
constexpr uint32_t maximum_input_batch = 256;
// Bound local enumeration of streams for an implausible SDK session count.
constexpr uint32_t maximum_sessions = 256;

// Resolve the optional Remote Play interface without a static Steam import.
ISteamRemotePlay *get_remote_play(const std::shared_ptr<SteamRuntime> &runtime) {
    const auto get = runtime ? runtime->symbol<decltype(&SteamAPI_SteamRemotePlay_v004)>(
                                       "SteamAPI_SteamRemotePlay_v004")
                             : nullptr;
    return get ? get() : nullptr;
}

// Convert a USB keyboard scancode to a Godot physical key. Unsupported usages are left unmapped.
Key physical_key(int scan) {
    if (scan >= 4 && scan <= 29) {
        return Key(KEY_A + scan - 4);
    }
    if (scan >= 30 && scan <= 38) {
        return Key(KEY_1 + scan - 30);
    }
    if (scan >= 58 && scan <= 69) {
        return Key(KEY_F1 + scan - 58);
    }
    if (scan >= 89 && scan <= 97) {
        return Key(KEY_KP_1 + scan - 89);
    }
    if (scan >= 104 && scan <= 115) {
        return Key(KEY_F13 + scan - 104);
    }
    switch (scan) {
        case 39: return KEY_0;
        case 40: return KEY_ENTER;
        case 41: return KEY_ESCAPE;
        case 42: return KEY_BACKSPACE;
        case 43: return KEY_TAB;
        case 44: return KEY_SPACE;
        case 45: return KEY_MINUS;
        case 46: return KEY_EQUAL;
        case 47: return KEY_BRACKETLEFT;
        case 48: return KEY_BRACKETRIGHT;
        case 49: return KEY_BACKSLASH;
        case 51: return KEY_SEMICOLON;
        case 52: return KEY_APOSTROPHE;
        case 53: return KEY_QUOTELEFT;
        case 54: return KEY_COMMA;
        case 55: return KEY_PERIOD;
        case 56: return KEY_SLASH;
        case 57: return KEY_CAPSLOCK;
        case 70: return KEY_PRINT;
        case 71: return KEY_SCROLLLOCK;
        case 72: return KEY_PAUSE;
        case 73: return KEY_INSERT;
        case 74: return KEY_HOME;
        case 75: return KEY_PAGEUP;
        case 76: return KEY_DELETE;
        case 77: return KEY_END;
        case 78: return KEY_PAGEDOWN;
        case 79: return KEY_RIGHT;
        case 80: return KEY_LEFT;
        case 81: return KEY_DOWN;
        case 82: return KEY_UP;
        case 83: return KEY_NUMLOCK;
        case 84: return KEY_KP_DIVIDE;
        case 85: return KEY_KP_MULTIPLY;
        case 86: return KEY_KP_SUBTRACT;
        case 87: return KEY_KP_ADD;
        case 88: return KEY_KP_ENTER;
        case 98: return KEY_KP_0;
        case 99: return KEY_KP_PERIOD;
        case 101: return KEY_MENU;
        case 224: case 228: return KEY_CTRL;
        case 225: case 229: return KEY_SHIFT;
        case 226: case 230: return KEY_ALT;
        case 227: case 231: return KEY_META;
        default: return KEY_NONE;
    }
}

// Preserve both the physical position and the character provided by the remote keyboard layout.
Ref<InputEventKey> make_key_event(const RemotePlayInputKey_t &sample, bool pressed) {
    const int scan = sample.m_eScancode;
    const Key physical = physical_key(scan);
    const uint32_t character = sample.m_unKeycode;
    const bool printable = character >= 32 && character != 127 && character <= 0x10ffff &&
            (character < 0xd800 || character > 0xdfff);
    if (physical == KEY_NONE && !printable) {
        return {};
    }
    Ref<InputEventKey> event;
    event.instantiate();
    event->set_pressed(pressed);
    event->set_physical_keycode(physical);
    const String label = printable ? String::chr(static_cast<char32_t>(character)).to_upper() : String();
    event->set_keycode(label.is_empty() ? physical : Key(label[0]));
    if (printable && pressed) {
        event->set_unicode(character);
    }
    if (scan >= 224 && scan <= 231) {
        event->set_location(scan < 228 ? KEY_LOCATION_LEFT : KEY_LOCATION_RIGHT);
    }
    const uint32_t mods = sample.m_unModifiers;
    event->set_shift_pressed((mods & (k_ERemotePlayKeyModifierLeftShift | k_ERemotePlayKeyModifierRightShift)) != 0);
    event->set_ctrl_pressed((mods & (k_ERemotePlayKeyModifierLeftControl | k_ERemotePlayKeyModifierRightControl)) != 0);
    event->set_alt_pressed((mods & (k_ERemotePlayKeyModifierLeftAlt | k_ERemotePlayKeyModifierRightAlt)) != 0);
    event->set_meta_pressed((mods & (k_ERemotePlayKeyModifierLeftGUI | k_ERemotePlayKeyModifierRightGUI)) != 0);
    return event;
}

// Map a Steam button token to a Godot mouse button.
MouseButton mouse_button(ERemotePlayMouseButton button) {
    switch (button) {
        case k_ERemotePlayMouseButtonLeft: return MOUSE_BUTTON_LEFT;
        case k_ERemotePlayMouseButtonRight: return MOUSE_BUTTON_RIGHT;
        case k_ERemotePlayMouseButtonMiddle: return MOUSE_BUTTON_MIDDLE;
        case k_ERemotePlayMouseButtonX1: return MOUSE_BUTTON_XBUTTON1;
        case k_ERemotePlayMouseButtonX2: return MOUSE_BUTTON_XBUTTON2;
        default: return MOUSE_BUTTON_NONE;
    }
}

// Map directional wheel input to Godot's scroll buttons.
MouseButton wheel_button(ERemotePlayMouseWheelDirection direction) {
    switch (direction) {
        case k_ERemotePlayMouseWheelUp: return MOUSE_BUTTON_WHEEL_UP;
        case k_ERemotePlayMouseWheelDown: return MOUSE_BUTTON_WHEEL_DOWN;
        case k_ERemotePlayMouseWheelLeft: return MOUSE_BUTTON_WHEEL_LEFT;
        case k_ERemotePlayMouseWheelRight: return MOUSE_BUTTON_WHEEL_RIGHT;
        default: return MOUSE_BUTTON_NONE;
    }
}

// Build a button or scroll event without injecting it into Godot's local input queue.
Ref<InputEventMouseButton> make_button_event(MouseButton button, bool pressed, float factor = 1.0f) {
    if (button == MOUSE_BUTTON_NONE || !std::isfinite(factor) || factor <= 0.0f) {
        return {};
    }
    Ref<InputEventMouseButton> event;
    event.instantiate();
    event->set_button_index(button);
    event->set_pressed(pressed);
    event->set_factor(factor);
    return event;
}

// Convert one SDK sample. Absolute positions stay normalized until the game maps them to a seat viewport.
Ref<InputEvent> convert_input(const RemotePlayInput_t &sample, bool &absolute) {
    switch (sample.m_eType) {
        case k_ERemotePlayInputMouseMotion: {
            const auto &motion = sample.m_MouseMotion;
            Ref<InputEventMouseMotion> event;
            event.instantiate();
            const Vector2 relative(motion.m_nDeltaX, motion.m_nDeltaY);
            event->set_relative(relative);
            event->set_screen_relative(relative);
            absolute = motion.m_bAbsolute && std::isfinite(motion.m_flNormalizedX) &&
                    std::isfinite(motion.m_flNormalizedY);
            if (absolute) {
                event->set_position(Vector2(std::clamp(motion.m_flNormalizedX, 0.0f, 1.0f),
                        std::clamp(motion.m_flNormalizedY, 0.0f, 1.0f)));
            }
            return event;
        }
        case k_ERemotePlayInputMouseButtonDown:
        case k_ERemotePlayInputMouseButtonUp:
            return make_button_event(mouse_button(sample.m_eMouseButton), sample.m_eType == k_ERemotePlayInputMouseButtonDown);
        case k_ERemotePlayInputMouseWheel:
            return make_button_event(wheel_button(sample.m_MouseWheel.m_eDirection), true, std::abs(sample.m_MouseWheel.m_flAmount));
        case k_ERemotePlayInputKeyDown:
        case k_ERemotePlayInputKeyUp:
            return make_key_event(sample.m_Key, sample.m_eType == k_ERemotePlayInputKeyDown);
        default:
            return {};
    }
}

// Copy and route the current ABI's connection record to its exact facade.
template <typename Event>
uint32_t read_session_callback(const void *record, size_t size) {
    if (!record || size != sizeof(Event)) {
        return 0;
    }
    Event event{};
    std::memcpy(&event, record, sizeof(event));
    return event.m_unSessionID;
}

} // namespace
#endif

void SteamRemotePlaySession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_session_id"), &SteamRemotePlaySession::get_session_id);
    ClassDB::bind_method(D_METHOD("get_steam_id"), &SteamRemotePlaySession::get_steam_id);
    ClassDB::bind_method(D_METHOD("get_client_size"), &SteamRemotePlaySession::get_client_size);
    ClassDB::bind_method(D_METHOD("get_together"), &SteamRemotePlaySession::get_together);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "session_id"), "", "get_session_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "steam_id"), "", "get_steam_id");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "client_size"), "", "get_client_size");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "together"), "", "get_together");
}

int64_t SteamRemotePlaySession::get_session_id() const { return session_id; }
String SteamRemotePlaySession::get_steam_id() const { return steam_id; }
Vector2i SteamRemotePlaySession::get_client_size() const { return client_size; }
bool SteamRemotePlaySession::get_together() const { return together; }

void SteamRemotePlayInputEvent::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_session_id"), &SteamRemotePlayInputEvent::get_session_id);
    ClassDB::bind_method(D_METHOD("get_event"), &SteamRemotePlayInputEvent::get_event);
    ClassDB::bind_method(D_METHOD("get_has_absolute_position"), &SteamRemotePlayInputEvent::get_has_absolute_position);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "session_id"), "", "get_session_id");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent"), "", "get_event");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_absolute_position"), "", "get_has_absolute_position");
}

int64_t SteamRemotePlayInputEvent::get_session_id() const { return session_id; }
Ref<InputEvent> SteamRemotePlayInputEvent::get_event() const { return event; }
bool SteamRemotePlayInputEvent::get_has_absolute_position() const { return has_absolute_position; }

void SteamRemotePlay::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_available"), &SteamRemotePlay::is_available);
    ClassDB::bind_method(D_METHOD("sessions"), &SteamRemotePlay::sessions);
    ClassDB::bind_method(D_METHOD("open_together_overlay"), &SteamRemotePlay::open_together_overlay);
    ClassDB::bind_method(D_METHOD("send_invite", "steam_id"), &SteamRemotePlay::send_invite);
    ClassDB::bind_method(D_METHOD("set_separate_input", "enabled"), &SteamRemotePlay::set_separate_input);
    ClassDB::bind_method(D_METHOD("poll_input", "limit"), &SteamRemotePlay::poll_input, DEFVAL(64));
    ADD_SIGNAL(MethodInfo("session_connected", PropertyInfo(Variant::OBJECT, "session", PROPERTY_HINT_RESOURCE_TYPE,
            "SteamRemotePlaySession")));
    ADD_SIGNAL(MethodInfo("session_disconnected", PropertyInfo(Variant::INT, "session_id")));
}

void SteamRemotePlay::attach(const std::shared_ptr<SteamRuntime> &active,
        const std::shared_ptr<SteamClientScope> &scope) {
#ifdef CRIMSON_WITH_STEAM
    if (!SteamRuntime::on_main_thread() || connected_subscription || !scope || !scope->active || !get_remote_play(active)) {
        return;
    }
    const ObjectID id(get_instance_id());
    connected_subscription = active->subscribe(SteamRemotePlaySessionConnected_t::k_iCallback,
            [id](const void *record, size_t size) {
                const Ref<SteamRemotePlay> service(Object::cast_to<SteamRemotePlay>(ObjectDB::get_instance(id)));
                if (service.is_valid()) {
                    service->on_session(read_session_callback<SteamRemotePlaySessionConnected_t>(record, size), true);
                }
            });
    disconnected_subscription = active->subscribe(SteamRemotePlaySessionDisconnected_t::k_iCallback,
            [id](const void *record, size_t size) {
                const Ref<SteamRemotePlay> service(Object::cast_to<SteamRemotePlay>(ObjectDB::get_instance(id)));
                if (service.is_valid()) {
                    service->on_session(read_session_callback<SteamRemotePlaySessionDisconnected_t>(record, size), false);
                }
            });
    if (!connected_subscription || !disconnected_subscription) {
        active->unsubscribe(connected_subscription);
        active->unsubscribe(disconnected_subscription);
        connected_subscription = 0;
        disconnected_subscription = 0;
        return;
    }
    runtime = active;
    client_scope = scope;
#endif
}

void SteamRemotePlay::detach() {
#ifdef CRIMSON_WITH_STEAM
    const auto active = runtime.lock();
    release_separate_input(active);
    if (active) {
        active->unsubscribe(connected_subscription);
        active->unsubscribe(disconnected_subscription);
    }
    connected_subscription = 0;
    disconnected_subscription = 0;
    runtime.reset();
    client_scope.reset();
#endif
}

std::shared_ptr<SteamRuntime> SteamRemotePlay::active_runtime() const {
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

bool SteamRemotePlay::is_available() const {
#ifdef CRIMSON_WITH_STEAM
    return get_remote_play(active_runtime()) != nullptr;
#else
    return false;
#endif
}

TypedArray<SteamRemotePlaySession> SteamRemotePlay::sessions() const {
    TypedArray<SteamRemotePlaySession> snapshots;
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_remote_play(active);
    if (interface) {
        const auto count = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_GetSessionCount)>(
                "SteamAPI_ISteamRemotePlay_GetSessionCount");
        const auto get_id = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_GetSessionID)>(
                "SteamAPI_ISteamRemotePlay_GetSessionID");
        if (count && get_id) {
            const uint32_t size = (std::min)(count(interface), maximum_sessions);
            for (uint32_t index = 0; index < size; ++index) {
                const auto session = read_session(active, get_id(interface, static_cast<int>(index)));
                if (session.is_valid()) {
                    snapshots.append(session);
                }
            }
        }
    }
#endif
    snapshots.make_read_only();
    return snapshots;
}

Ref<SteamRemotePlaySession> SteamRemotePlay::read_session(const std::shared_ptr<SteamRuntime> &active, uint32_t id) const {
#ifdef CRIMSON_WITH_STEAM
    auto *interface = get_remote_play(active);
    if (id == 0 || !interface) {
        return {};
    }
    const auto get_user = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_GetSessionSteamID)>(
            "SteamAPI_ISteamRemotePlay_GetSessionSteamID");
    const auto get_resolution = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_BGetSessionClientResolution)>(
            "SteamAPI_ISteamRemotePlay_BGetSessionClientResolution");
    const auto is_together = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_BSessionRemotePlayTogether)>(
            "SteamAPI_ISteamRemotePlay_BSessionRemotePlayTogether");
    Ref<SteamRemotePlaySession> snapshot;
    snapshot.instantiate();
    snapshot->session_id = id;
    if (get_user) {
        const CSteamID user(get_user(interface, id));
        if (user.IsValid() && user.GetEAccountType() == k_EAccountTypeIndividual) {
            snapshot->steam_id = String::utf8(std::to_string(user.ConvertToUint64()).c_str());
        }
    }
    int width = 0;
    int height = 0;
    if (get_resolution && get_resolution(interface, id, &width, &height) && width > 0 && height > 0) {
        snapshot->client_size = Vector2i(width, height);
    }
    snapshot->together = is_together && is_together(interface, id);
    return snapshot;
#else
    return {};
#endif
}

void SteamRemotePlay::on_session(uint32_t id, bool connected) {
    const auto active = active_runtime();
    if (!active || id == 0) {
        return;
    }
    if (connected) {
        const auto session = read_session(active, id);
        if (session.is_valid()) {
            emit_signal("session_connected", session);
        }
    } else {
        emit_signal("session_disconnected", static_cast<int64_t>(id));
    }
}

Error SteamRemotePlay::open_together_overlay() const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_remote_play(active);
    if (interface) {
        const auto overlay_ready = active->symbol<decltype(&SteamAPI_ISteamUtils_IsOverlayEnabled)>(
                "SteamAPI_ISteamUtils_IsOverlayEnabled");
        const auto open = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_ShowRemotePlayTogetherUI)>(
                "SteamAPI_ISteamRemotePlay_ShowRemotePlayTogetherUI");
        if (overlay_ready && open && active->get_utils()) {
            if (!overlay_ready(active->get_utils())) {
                return ERR_BUSY;
            }
            return open(interface) ? OK : ERR_UNAVAILABLE;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamRemotePlay::send_invite(const String &steam_id) const {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_remote_play(active);
    if (interface) {
        uint64_t target = 0;
        if (!parse_steam_account(steam_id, target) || target == active->get_user_id()) {
            return ERR_INVALID_PARAMETER;
        }
        const auto send = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_BSendRemotePlayTogetherInvite)>(
                "SteamAPI_ISteamRemotePlay_BSendRemotePlayTogetherInvite");
        if (send) {
            return send(interface, target) ? OK : FAILED;
        }
    }
#endif
    return ERR_UNAVAILABLE;
}

Error SteamRemotePlay::set_separate_input(bool enabled) {
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_remote_play(active);
    if (!interface) {
        return ERR_UNAVAILABLE;
    }
    const ObjectID id(get_instance_id());
    if (separate_input_owner.is_valid() && separate_input_owner != id) {
        return ERR_BUSY;
    }
    if (!enabled) {
        release_separate_input(active);
        return OK;
    }
    if (separate_input_owner == id) {
        return OK;
    }
    const auto enable = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_BEnableRemotePlayTogetherDirectInput)>(
            "SteamAPI_ISteamRemotePlay_BEnableRemotePlayTogetherDirectInput");
    const auto disable = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_DisableRemotePlayTogetherDirectInput)>(
            "SteamAPI_ISteamRemotePlay_DisableRemotePlayTogetherDirectInput");
    const auto read = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_GetInput)>("SteamAPI_ISteamRemotePlay_GetInput");
    if (!enable || !disable || !read || !enable(interface)) {
        return ERR_UNAVAILABLE;
    }
    separate_input_owner = id;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

void SteamRemotePlay::release_separate_input(const std::shared_ptr<SteamRuntime> &active) {
#ifdef CRIMSON_WITH_STEAM
    if (separate_input_owner != ObjectID(get_instance_id())) {
        return;
    }
    separate_input_owner = ObjectID();
    auto *interface = get_remote_play(active);
    if (interface) {
        const auto disable = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_DisableRemotePlayTogetherDirectInput)>(
                "SteamAPI_ISteamRemotePlay_DisableRemotePlayTogetherDirectInput");
        if (disable) {
            disable(interface);
        }
    }
#endif
}

TypedArray<SteamRemotePlayInputEvent> SteamRemotePlay::poll_input(int64_t limit) const {
    TypedArray<SteamRemotePlayInputEvent> snapshots;
#ifdef CRIMSON_WITH_STEAM
    const auto active = active_runtime();
    auto *interface = get_remote_play(active);
    if (limit > 0 && interface && separate_input_owner == ObjectID(get_instance_id())) {
        const auto read = active->symbol<decltype(&SteamAPI_ISteamRemotePlay_GetInput)>("SteamAPI_ISteamRemotePlay_GetInput");
        if (read) {
            std::array<RemotePlayInput_t, maximum_input_batch> native{};
            const auto capacity = static_cast<uint32_t>((std::min)(limit, int64_t(maximum_input_batch)));
            const uint32_t count = (std::min)(read(interface, native.data(), capacity), capacity);
            for (uint32_t index = 0; index < count; ++index) {
                if (native[index].m_unSessionID == 0) {
                    continue;
                }
                bool absolute = false;
                const auto event = convert_input(native[index], absolute);
                if (event.is_valid()) {
                    Ref<SteamRemotePlayInputEvent> snapshot;
                    snapshot.instantiate();
                    snapshot->session_id = native[index].m_unSessionID;
                    snapshot->event = event;
                    snapshot->has_absolute_position = absolute;
                    snapshots.append(snapshot);
                }
            }
        }
    }
#endif
    snapshots.make_read_only();
    return snapshots;
}

} // namespace crimson::steam
